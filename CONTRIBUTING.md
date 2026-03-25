# Contributing to ht-tmux

## HT Fork Management

This is the [Heiervang Technologies](https://github.com/heiervang-technologies) fork of [tmux](https://github.com/tmux/tmux). For the full fork management process, see the [Fork Management Guide](https://github.com/orgs/heiervang-technologies/discussions/3).

### Branch Conventions

- **`main`** — Clean fast-forward mirror of upstream `master`. Never commit directly.
- **`ht`** — Default branch. All HT-specific changes live here.
- **Feature branches** — Create from `ht`, squash-merge back via PR.

### Sync Workflow

```bash
# Update main from upstream
git checkout main
git fetch upstream
git merge --ff-only upstream/master
git push origin main

# Rebase ht onto updated main
git checkout ht
git rebase main
git push --force-with-lease origin ht
```

### Commit Standards

- Conventional commits: `feat(<scope>): description`
- Clean linear history on `ht` — no merge commits
- One commit per logical change

For all inquiries, see [HT Discussions](https://github.com/orgs/heiervang-technologies/discussions).

---

## Upstream Contributing

For non-fork-specific changes, follow the upstream tmux contribution guidelines. Bug reports, feature suggestions and code contributions can be sent to tmux-users@googlegroups.com or via GitHub issues and pull requests on the [upstream repository](https://github.com/tmux/tmux).
