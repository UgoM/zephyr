---
name: zephyr-pr-review
description: Use when reviewing Zephyr RTOS pull requests, patches, or branches before submission. Triggers on "review this PR", "review my patch", "check my Zephyr code", a zephyrproject-rtos PR URL, or a diff of a Zephyr tree.
---

# Zephyr PR review

Each lens file in this folder does one job and backs every finding with what it checked. This
file routes the change to lenses and merges their output.

## 1. Get the change

- PR: `gh pr view <N> --json title,body,baseRefName,url`, then `gh pr diff <N>`; for commits,
  check it out rebased on `upstream/main` as `AGENTS.md` describes.
- Branch: `git log --reverse upstream/main..<branch>` and `git diff upstream/main...<branch>`.
- Pasted diff: review it as given and say commit-level lenses could not run.

Default: `upstream/main...HEAD` (`origin/main` in a plain clone).

## 2. Route

From `git diff --name-only`, select lenses; each runs at most once.

| Lens | Runs when |
|---|---|
| `compliance-check.md` | always |
| `upstream-reference-check.md` | always |
| `commit-series.md` | always (needs commits) |
| `zephyr-conventions.md` | always |
| `devicetree.md` | `*.dts`, `*.dtsi`, `*.overlay`, `dts/bindings/`, `include/zephyr/dt-bindings/`, `DT_` in C hunks |
| `kconfig.md` | `Kconfig*`, `*_defconfig`, `*.conf`, `CONFIG_` conditionals in C or CMake |
| `hw-correctness.md` | `drivers/`, `soc/`, `dts/` hunks with register, clock, IRQ, DMA, pin or timing values |
| `driver-contract.md` | `*.c` under `drivers/`, `subsys/`, `soc/`; `include/zephyr/drivers/`, `include/zephyr/<subsys>/`, `tests/drivers/` |
| `docs-samples.md` | `doc/`, `*.rst`, `samples/`, `tests.yaml`, `MAINTAINERS.yml`, `.github/workflows/` |

Reference data handed to the lenses that run:

| Paths | Reference |
|---|---|
| `soc/st/stm32/`, `dts/arm/st/`, `boards/st/`, `*stm32*`, `st,stm32` | `ref/stm32.md` |
| `soc/atmel/`, `dts/arm/atmel/`, `boards/atmel/`, `*sam*`, `atmel,` | `ref/sam.md` |
| `soc/nxp/`, `dts/arm/nxp/`, `boards/nxp/`, `*mcux*`, `*nxp*` | `ref/nxp.md` |
| `drivers/audio/`, `include/zephyr/audio/`, `dts/bindings/audio/`, audio samples/tests | `ref/audio.md` |
| `drivers/i2s/`, `i2s.h`, `dts/bindings/i2s/`, I2S samples/tests | `ref/i2s.md` |
| `drivers/dma/`, `dma*.h`, `dts/bindings/dma/`, `tests/drivers/dma/` | `ref/dma.md` |
| `subsys/tracing/`, `include/zephyr/tracing/`, `scripts/tracing/`, tracing samples/tests/docs | `ref/tracing.md` |

State the routing (lenses run, refs loaded) at the top of the review.

## 3. Run the lenses

Run each lens as a subagent when available, otherwise in sequence, with the lens file, the
matching refs, the diff, the commit list and the PR metadata. Drop any finding without evidence.

## 4. Merge

Deduplicate: when two lenses report the same line, keep the one whose lens owns that concern.
Sort by severity, then by file.

```
## Zephyr PR review

Lenses: <list> · Refs: <list>

### Blocking
- [path:line] finding — evidence (lens)

### Should fix
- [path:line] finding — evidence (lens)

### Unverified
- [path:line] fact that could not be checked, and where it was searched (lens)

### Clean
- <lens>: clean (<what was checked>)

### Reviewers
Output of `./scripts/get_maintainer.py path <changed files>`.
```

Blocking means CI will fail, behaviour is wrong, a documented rule is broken, or an upstream
reference leaks. Everything else is "Should fix". No praise section and no padding.

Never write a bare `#NNNNN` in the review; see `upstream-reference-check.md`.
