---
description: Use when a diff touches doc/, any README.rst or index.rst, samples/, tests/**/tests.yaml, release notes, the migration guide, or MAINTAINERS.yml.
---

# Docs and samples lens

One job: does the user-facing surface (docs, samples, test metadata) say true things and stay
lean? `SphinxLint` and `YAMLLint` in CI cover syntax; do not repeat them.

## Procedure

1. For each user-visible change in the diff (new API, removed or renamed symbol, binding property,
   Kconfig option, behaviour change), check `doc/releases/release-notes-X.Y.rst` and, if it
   breaks users, `doc/releases/migration-guide-X.Y.rst` in the same PR. `X.Y` is the next release
   (`cat VERSION`). Entries use `:dtcompatible:`, `:kconfig:option:`, `:c:func:`, `:github:`
   and sit in the matching section.
2. For each command or path written in docs or a README, run or `ls` it. A command that does not
   work as written is a finding.
3. For a new sample: `grep -rl` the samples tree for one that already shows the same API. If one
   exists, the new sample must justify itself or become an overlay of the existing one.
4. For each `tests.yaml` scenario: list `platform_allow`, `integration_platforms`,
   `extra_args`, `harness`/`harness_config`. Flag:
   - scenarios that duplicate another one's coverage;
   - a shield or board-specific scenario inside a generic sample (move it under
     `samples/shields/` or a board overlay);
   - more than one integration platform for a sample, or one listed for a single-platform
     scenario;
   - board configuration in `extra_args` that belongs in a board overlay or `.conf`;
   - a console harness regex that does not match the sample output.
5. For each overlay or `.conf` in tests/samples, check a wiring comment exists when it relies on
   jumpers or loopback.
6. For a new board: doc uses the board template sections, RST `list-table` for features,
   hardware-accurate tables, and an image under the size limit (compressed).
7. If new files enter an area, `./scripts/get_maintainer.py path <files>` resolves them; if not,
   `MAINTAINERS.yml` needs an entry.

## Rules

- A sample does not assume a shield is attached; a sample is not a test matrix: infrastructure
  belongs in `tests/`.
- The README of a sample says what each mode or scenario requires.
- Diagrams use graphviz or mermaid, not ASCII art.
- `:c:enum:` for an enum type, `:c:enumerator:` for a value; short-form links per
  `doc/contribute/documentation/guidelines.rst`.
- American English; acronyms expanded on first use.
- Default values exposed by a shell command or sample are documented.
- If a doc change is non-trivial, the author has checked the CI-rendered page.
- A project rule restated in docs is quoted, not paraphrased (PR 117725).
- Board docs explain non-obvious hardware setup (jumpers, solder bridges, shared pins).
- No `TC_SKIP` after `ztest_test_skip()`; `TC_PRINT` lines end with a newline.
- CI path filters (`.github/workflows/`) actually trigger on the files the PR adds.

## Output

```
[path:line] finding — evidence (the command run, file compared, or section checked)
```

Nothing found: `docs-samples: clean`.
