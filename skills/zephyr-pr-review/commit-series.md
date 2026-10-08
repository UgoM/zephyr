---
description: Use when reviewing any Zephyr branch or PR with one or more commits.
---

# Commit series lens

One job: is the series shaped so it can be reviewed, bisected and merged? Gitlint and Checkpatch
format checks are covered by `compliance-check.md`; `#NNNNN` leaks by
`upstream-reference-check.md`. Do not repeat them.

## Procedure

1. `git log --reverse --format='%h %s' <base>..HEAD`. For each commit, run
   `git show --stat <sha>` and write one line: the areas it touches (SoC, bindings, dts, driver,
   board, test, sample, doc, CI, MAINTAINERS).
2. Flag every commit that touches two unrelated areas, or carries noise hunks (whitespace,
   reformatting, renames) unrelated to its subject. Mechanical changes (renames, moves done with
   `git mv`, refactors) go in their own commit before the functional one.
3. Check the order: SoC → bindings → drivers → dts/boards → tests/samples → docs. A dts node
   committed before its binding, or a board before its SoC, is a finding.
4. Bisectability: for each commit, does anything it references come from a later commit (a
   Kconfig symbol, a binding, a header, a function)? Name the symbol and both commits.
5. For each subject, compare the prefix with `git log --format=%s -20 -- <main path>`. A prefix
   not used in that path's history is a finding (PR 109770). The summary says precisely what
   changed.
6. For each body: it says why, and how it was tested (board name, test suite). Claims about
   performance or size carry numbers.
7. Trailers: exactly one `Assisted-by:` if a model helped, no `Co-authored-by:`, no AI mention
   elsewhere; a `Signed-off-by:` matching the author is the human's to add, so only report it
   missing.
8. File prelude: copyright line(s) then `SPDX-License-Identifier`. New files carry the current
   year and the real holder (PR 114756); an existing line is never replaced, only added to;
   imported files keep their licence verbatim.
9. Scope: list the areas of the whole PR. If they split cleanly across `MAINTAINERS.yml` areas
   (shield vs board, PR 109004; driver vs dts vs boards, PR 107565), say which commits form the
   separate PR.

## Rules

- Fold review fixes into the commit owning the lines; no fixup, squash or merge commits.
- A change touching a driver also updates its sibling users when the same bug exists there
  (OSPI and MSPI, PR 108040), or the body says why not.
- A new SoC, board or feature lands with at least one in-tree board or test that builds it
  (PR 115936).
- Rebase on main, never merge main in.
- No dead weight: symbols, files, includes, buffers and config lines the change makes unused are
  removed in the same commit; comments that restate the code are not added.
- If a PR description exists, it matches the commits after the last push.

## Output

```
[commit <sha> "<subject>"] finding — evidence (the stat line, symbol or log output checked)
```

Then the per-commit area table from step 1. Nothing found: `commit-series: clean (N commits)`.
