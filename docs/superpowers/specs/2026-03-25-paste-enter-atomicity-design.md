# htmux: Atomic Paste+Enter (`paste-buffer -E`)

**Date:** 2026-03-25
**Status:** Approved
**Authors:** linux-wizard (Claude Opus), gemini (Gemini Pro) — reviewed by Markus

## Problem

When chaining `paste-buffer -p \; send-keys C-m`, both commands write to the same libevent bufferevent output buffer in a single event loop iteration. The paste content and Enter arrive at the receiving application in one `write()` syscall. Applications using bracketed paste mode perform heavy processing after receiving the paste-end marker (`ESC[201~`) and many call `tcflush(fd, TCIFLUSH)` to discard typeahead — which wipes out the Enter that was part of the same write.

This affects all agent workflows that use `tmux-tool paste --enter` or `director send`, causing unreliable command submission especially in nested tmux scenarios.

### Root Cause

Enter arrives too close to paste content. The app's `tcflush()` discards it. This is not a sender ordering issue (tmux already writes them in order) — it is a receiver timing issue.

### Why Previous Workarounds Are Insufficient

The current bash-level workaround in `tmux-tool` uses a two-phase approach:
1. **Phase 1:** 1.5s minimum sleep after paste (handles silent blockers)
2. **Phase 2:** Cursor position polling every 100ms (handles visible renderers)

This works but adds 1.5-3.5s latency to every paste operation. At scale (multi-agent workflows with hundreds of paste+enter operations), this is unacceptable.

## Design

### New Flag: `paste-buffer -E [count]`

A new `-E` flag on `paste-buffer` that performs an atomic paste-then-enter sequence with adaptive timing.

**Usage:**
```
htmux paste-buffer -E        # paste + 1 enter
htmux paste-buffer -E 2      # paste + 2 enters (double-enter for agent CLIs)
htmux paste-buffer -E -p     # with bracketed paste mode (combinable with existing flags)
```

### Architecture

```
paste-buffer -E -p
    |
    v
[1] Write ESC[200~ + content + ESC[201~ to pane bufferevent
    |
    v
[2] Set wp->flags |= PANE_PASTE_PENDING
    |
    v
[3] Start hard max timeout (evtimer, default 2000ms)
    |
    v
[4] Return CMD_RETURN_WAIT (blocks client command queue, not server)
    |
    v
[5] Event loop runs. Two things can happen:
    |
    +---> Pane produces output (window_pane_read_callback fires)
    |     -> Reset idle timer to 50ms
    |     -> On next output: reset again
    |     -> When idle timer expires (no output for 50ms):
    |        -> Fire Enter (step 6)
    |
    +---> No output for 2000ms (hard max timeout fires)
           -> Fire Enter (step 6)
    |
    v
[6] Fire Enter sequence:
    a. Clear PANE_PASTE_PENDING flag
    b. Cancel any remaining timers
    c. Write \r to pane bufferevent
    d. If enter_count > 1: start 50ms timer for next Enter
    e. When all Enters sent: call cmdq_continue() to resume client
```

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| `CMD_RETURN_WAIT` instead of async return | Prevents race condition where agent scripts send more keys before delayed Enter fires. Blocks client command queue only, not server thread. Same pattern as `confirm-before`. |
| Idle timer (50ms) on pane output | Detects when app finishes processing paste. Fast apps get Enter ~50ms after rendering completes. |
| Hard max timeout (2000ms default) | Handles "silent blocker" apps that process paste without producing output, then `tcflush`. Configurable via tmux option. |
| No input buffering | The `-E` flag is for programmatic paste, not interactive typing. `CMD_RETURN_WAIT` already serializes the calling client's commands. |
| Hookable via `window_pane_read_callback` | Pane read callback already exists in `window.c:1025`. We add idle timer reset logic when `PANE_PASTE_PENDING` is set. |

### Files Modified

| File | Changes |
|------|---------|
| `cmd-paste-buffer.c` | Add `-E` flag parsing, idle timer + max timeout setup, `CMD_RETURN_WAIT` return |
| `window.c` | Add idle timer reset in `window_pane_read_callback` when `PANE_PASTE_PENDING` set |
| `tmux.h` | Add `PANE_PASTE_PENDING` flag, paste-enter timer fields to `struct window_pane` |
| `configure.ac` / `Makefile.am` | Rename binary from `tmux` to `htmux` |

### Struct Changes

```c
// In struct window_pane (tmux.h):
#define PANE_PASTE_PENDING 0x8000  // or next available flag

// New fields:
struct event     paste_enter_timer;    // idle timer (50ms after last output)
struct event     paste_max_timer;      // hard max timeout
struct cmdq_item *paste_enter_item;    // cmdq item to continue when done
int              paste_enter_count;    // remaining enters to send
```

### Configuration

New tmux option:
```
set -g paste-enter-timeout 2000   # max wait in ms (default 2000)
set -g paste-enter-idle 50        # idle threshold in ms (default 50)
```

### Binary Rename

htmux installs as `htmux`, not `tmux`. Both can coexist on the same system. Changes:
- `configure.ac`: rename binary
- Socket path uses `htmux-*` instead of `tmux-*`
- Default config file: `~/.htmux.conf` (falls back to `~/.tmux.conf`)

## Testing

### Unit Tests (C)

- Timer fires after idle threshold when pane produces then stops output
- Timer fires at max timeout when pane produces no output
- `CMD_RETURN_WAIT` blocks client queue, `cmdq_continue` resumes it
- Multiple `-E` enters fire with correct spacing
- Flag cleared correctly on timer fire

### Integration Tests (bash)

Extend existing `tests/test_nested_tmux.sh` and `tests/test_paste_enter.sh`:
- `htmux paste-buffer -E` to shell (direct pane)
- `htmux paste-buffer -E` to shell (nested htmux)
- `htmux paste-buffer -E` to bracketed paste app with `tcflush` (direct)
- `htmux paste-buffer -E` to bracketed paste app with `tcflush` (nested)
- `htmux paste-buffer -E 2` double-enter to agent CLI
- 20x sequential `paste-buffer -E` consistency test
- Script atomicity: verify next command waits for Enter to fire
- Various payload sizes: 50B, 500B, 2KB, 5KB

### Reliability Target

100% success rate across 100 consecutive paste+enter operations in nested htmux with a bracketed paste app that calls `tcflush` after 1s processing.

## Migration Path

1. `tmux-tool` detects htmux availability and uses `htmux paste-buffer -E` instead of the two-phase sleep+poll workaround
2. `director send` switches to htmux when available
3. Fallback to existing workaround when running under stock tmux

## Appendix: Rejected Approaches

| Approach | Why Rejected |
|----------|-------------|
| `tcdrain()` after paste write | Blocks tmux server thread. Non-starter for multiplexer. |
| Fixed delay timer only | Cannot adapt to app processing time. Either too slow (wastes time) or too fast (gets tcflushed). |
| Write barrier at bufferevent level | Issue is receiver timing, not sender ordering. Kernel PTY buffers regardless. |
| Async return without `CMD_RETURN_WAIT` | Agent scripts can send keys before delayed Enter fires, breaking atomicity. |
