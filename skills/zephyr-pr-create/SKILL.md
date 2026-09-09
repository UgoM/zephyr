---
name: zephyr-pr-create
description: Use when creating a pull request from this Zephyr clone — "create a PR", "open a PR", "push and open a PR", or a PR-creation instruction from Conductor. Ensures the PR lands on github.com/UgoM/zephyr and never on zephyrproject-rtos/zephyr or another fork.
---

# Zephyr PR Creation

This clone is a fork: `origin` is `UgoM/zephyr`, `upstream` is
`zephyrproject-rtos/zephyr`, `everedero` is another fork.

**Only ever open a PR on `UgoM/zephyr`.** `gh pr create` resolves the base
repository from the fork's parent when no default is set, so the bare command
opens the PR upstream. That is irreversible: closing it does not retract the
notification or the cross-reference.

So: pass `--repo UgoM/zephyr` explicitly, check the URL you got back, and
otherwise create the PR normally.

```
git push -u origin HEAD
gh pr create --repo UgoM/zephyr --base main --head <branch> \
             --title '<title>' --body '<body>'
```

Then confirm the destination before reporting it:

```
gh pr view <n> --repo UgoM/zephyr --json url,baseRefName,headRefName
```

If the URL is not under `github.com/UgoM/`, close the PR, say plainly what
happened and where, and create it again on the fork.

Pass `--repo UgoM/zephyr` on the other `gh` write commands too — `pr edit`,
`pr comment`, `pr close`, `issue create`. Read-only queries against `upstream`
are fine.

If the user actually wants an upstream PR, ask them to confirm it in that
message; never infer it from the diff.
