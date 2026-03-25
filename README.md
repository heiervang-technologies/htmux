# htmux

_[Heiervang Technologies](https://github.com/heiervang-technologies) fork of [tmux](https://github.com/tmux/tmux)_

[HT Discussions](https://github.com/orgs/heiervang-technologies/discussions) | [Fork Management Guide](https://github.com/orgs/heiervang-technologies/discussions/3) | [Upstream tmux](https://github.com/tmux/tmux)

---

## HT Fork Changes

This is the Heiervang Technologies fork of tmux. The `ht` branch contains the following changes on top of upstream `master`:

| Change | Description | Contributed back? |
|--------|-------------|-------------------|
| Atomic send-keys flush | Explicit bufferevent flush after `send-keys -l` content, ensuring Enter arrives after content in nested tmux | Planned |

### Branch Strategy

- **`main`** — Clean mirror of upstream `master`. Never commit directly.
- **`ht`** — Default branch. All HT-specific changes live here.

For questions or discussion, see the [HT Discussions page](https://github.com/orgs/heiervang-technologies/discussions).
For fork management details, see the [Fork Management Guide](https://github.com/orgs/heiervang-technologies/discussions/3).

---

_Below is the original upstream README._

---

## Welcome to tmux!

tmux is a terminal multiplexer: it enables a number of terminals to be created,
accessed, and controlled from a single screen. tmux may be detached from a
screen and continue running in the background, then later reattached.

This release runs on OpenBSD, FreeBSD, NetBSD, Linux, macOS and Solaris.

### Dependencies

tmux depends on libevent 2.x, available from:
https://github.com/libevent/libevent/releases/latest

It also depends on ncurses, available from:
https://invisible-mirror.net/archives/ncurses/

To build tmux, a C compiler (for example gcc or clang), make, pkg-config and a
suitable yacc (yacc or bison) are needed.

### Installation

To build and install tmux from a release tarball, use:

```
./configure && make
sudo make install
```

To get and build the latest from version control (requires autoconf, automake and pkg-config):

```
git clone https://github.com/heiervang-technologies/ht-tmux.git
cd ht-tmux
sh autogen.sh
./configure && make
sudo make install
```

### Documentation

For documentation on using tmux, see the tmux.1 manpage. View it from the source tree with:

```
nroff -mdoc tmux.1 | less
```

A small example configuration is in example_tmux.conf.

Other documentation is available in the wiki: https://github.com/tmux/tmux/wiki

Also see the tmux FAQ: https://github.com/tmux/tmux/wiki/FAQ

### License

This file and the CHANGES files are licensed under the ISC license. All other
files have a license and copyright notice at their start.
