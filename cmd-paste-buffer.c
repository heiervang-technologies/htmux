/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Paste paste buffer if present.
 */

static enum cmd_retval	cmd_paste_buffer_exec(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_paste_buffer_entry = {
	.name = "paste-buffer",
	.alias = "pasteb",

	.args = { "db:E:prSs:t:", 0, 0, NULL },
	.usage = "[-dprS] [-E enter-count] [-s separator] "
		 CMD_BUFFER_USAGE " " CMD_TARGET_PANE_USAGE,

	.target = { 't', CMD_FIND_PANE, 0 },

	.flags = CMD_AFTERHOOK,
	.exec = cmd_paste_buffer_exec
};

static void
cmd_paste_buffer_paste(struct window_pane *wp, const char *buf, size_t len)
{
	char	*cp;
	size_t	 n;

	n = utf8_stravisx(&cp, buf, len, VIS_SAFE|VIS_NOSLASH);
	bufferevent_write(wp->event, cp, n);
	free(cp);
}

/*
 * Timer callback: fire Enter after paste idle or max timeout.
 */
static void
cmd_paste_buffer_enter_fire(int fd, short events, void *arg)
{
	struct window_pane	*wp = arg;

	/* Cancel the other timer. */
	evtimer_del(&wp->paste_idle_timer);
	evtimer_del(&wp->paste_max_timer);

	/* Clear the pending flag. */
	wp->flags &= ~PANE_PASTE_PENDING;

	/* Send carriage return. */
	if (wp->fd != -1 && wp->event != NULL)
		bufferevent_write(wp->event, "\r", 1);
	wp->paste_enter_count--;

	log_debug("paste-enter: fired enter for %%%u, %d remaining",
	    wp->id, wp->paste_enter_count);

	/* More enters to send? Schedule with 50ms spacing. */
	if (wp->paste_enter_count > 0) {
		struct timeval tv = { 0, 50000 }; /* 50ms */
		/* Don't re-set PANE_PASTE_PENDING: subsequent enters are
		 * fixed-delay, not adaptive. Avoids idle timer interference. */
		evtimer_set(&wp->paste_max_timer,
		    cmd_paste_buffer_enter_fire, wp);
		evtimer_add(&wp->paste_max_timer, &tv);
		return;
	}

	/* All enters sent. Resume the client command queue. */
	if (wp->paste_enter_item != NULL) {
		cmdq_continue(wp->paste_enter_item);
		wp->paste_enter_item = NULL;
	}
}

static enum cmd_retval
cmd_paste_buffer_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = cmd_get_args(self);
	struct cmd_find_state	*target = cmdq_get_target(item);
	struct window_pane	*wp = target->wp;
	struct paste_buffer	*pb;
	const char		*sepstr, *bufname, *bufdata, *bufend, *line;
	size_t			 seplen, bufsize, len;
	int			 bracket = args_has(args, 'p');
	int			 enter = args_has(args, 'E');
	int			 enter_count = 1;

	if (window_pane_exited(wp)) {
		cmdq_error(item, "target pane has exited");
		return (CMD_RETURN_ERROR);
	}

	if (enter && (wp->flags & PANE_PASTE_PENDING)) {
		cmdq_error(item, "paste already pending on this pane");
		return (CMD_RETURN_ERROR);
	}

	/* Parse -E count (default 1). */
	if (enter) {
		const char *estr = args_get(args, 'E');
		if (estr != NULL) {
			const char *errstr;
			enter_count = strtonum(estr, 1, 10, &errstr);
			if (errstr != NULL) {
				cmdq_error(item, "enter count %s: %s",
				    estr, errstr);
				return (CMD_RETURN_ERROR);
			}
		}
	}

	bufname = NULL;
	if (args_has(args, 'b'))
		bufname = args_get(args, 'b');

	if (bufname == NULL)
		pb = paste_get_top(NULL);
	else {
		pb = paste_get_name(bufname);
		if (pb == NULL) {
			cmdq_error(item, "no buffer %s", bufname);
			return (CMD_RETURN_ERROR);
		}
	}

	if (pb != NULL && ~wp->flags & PANE_INPUTOFF) {
		sepstr = args_get(args, 's');
		if (sepstr == NULL) {
			if (args_has(args, 'r'))
				sepstr = "\n";
			else
				sepstr = "\r";
		}
		seplen = strlen(sepstr);

		if (bracket && (wp->screen->mode & MODE_BRACKETPASTE))
			bufferevent_write(wp->event, "\033[200~", 6);

		bufdata = paste_buffer_data(pb, &bufsize);
		bufend = bufdata + bufsize;

		for (;;) {
			line = memchr(bufdata, '\n', bufend - bufdata);
			if (line == NULL)
				break;
			len = line - bufdata;
			if (args_has(args, 'S'))
				bufferevent_write(wp->event, bufdata, len);
			else
				cmd_paste_buffer_paste(wp, bufdata, len);
			bufferevent_write(wp->event, sepstr, seplen);

			bufdata = line + 1;
		}
		if (bufdata != bufend) {
			len = bufend - bufdata;
			if (args_has(args, 'S'))
				bufferevent_write(wp->event, bufdata, len);
			else
				cmd_paste_buffer_paste(wp, bufdata, len);
		}

		if (bracket && (wp->screen->mode & MODE_BRACKETPASTE))
			bufferevent_write(wp->event, "\033[201~", 6);
	}

	if (pb != NULL && args_has(args, 'd'))
		paste_free(pb);

	/* If -E, set up deferred enter with adaptive timing. */
	if (enter && pb != NULL) {
		struct timeval max_tv;
		long long max_ms;

		/* Get configurable timeouts. */
		max_ms = options_get_number(wp->options,
		    "paste-enter-timeout");
		max_tv.tv_sec = max_ms / 1000;
		max_tv.tv_usec = (max_ms % 1000) * 1000;

		/* Store state on the pane. */
		wp->paste_enter_count = enter_count;
		wp->paste_enter_item = item;
		wp->flags |= PANE_PASTE_PENDING;

		/* Initialize and start the idle timer (reset by read cb). */
		evtimer_set(&wp->paste_idle_timer,
		    cmd_paste_buffer_enter_fire, wp);

		/* Start the hard max timeout. */
		evtimer_set(&wp->paste_max_timer,
		    cmd_paste_buffer_enter_fire, wp);
		evtimer_add(&wp->paste_max_timer, &max_tv);

		log_debug("paste-enter: started for %%%u, max %lldms, "
		    "count %d", wp->id, max_ms, enter_count);

		return (CMD_RETURN_WAIT);
	}

	return (CMD_RETURN_NORMAL);
}
