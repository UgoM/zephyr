---
description: Use when a diff touches any Kconfig*, *_defconfig, *.conf file, or CONFIG_ conditionals in C or CMake.
---

# Kconfig lens

One job: does each symbol exist where it should, with the right scope and no dead weight?
`KconfigBasic` in CI already catches undefined symbols; do not repeat it.

## Procedure

1. List every symbol added or changed. For each one, write down the enclosing `if`/`menu` chain
   (read the file top to bottom, not only the hunk). A symbol or `select` outside an `if` gate
   for its SoC/driver bleeds into every other build: finding.
2. Place the symbol in the hierarchy: family → series → SoC → board. Open the sibling series'
   `Kconfig`/`Kconfig.defconfig` and check that the same symbol lives at the same level there
   (series-wide facts like `SOC_SERIES_REVISION` live at series level, not family).
3. For each `default`, check: `default n`, or a default equal to the one inherited, is redundant
   (`doc/build/kconfig/tips.rst`, redundant defaults); a `default y` on a driver must be
   `default y` + `depends on DT_HAS_<COMPAT>_ENABLED`, not unconditional.
4. For each `select`, check the target has no unsatisfied `depends on` (`tips.rst`, select vs
   depends on). For SoC selects, check the grouping order used by the sibling file.
5. For `*_defconfig` and `prj.conf`: every line must be needed to boot or to build the app. A
   driver option already implied by a `status = "okay"` node through `DT_HAS_*` is redundant;
   a driver needed by a DTS-enabled node but missing is a finding.
6. An option that changes test behaviour at runtime is a finding: the test should query
   support and cover all cases.
7. Grep the whole PR for each renamed symbol: the rename must be complete in every file. In
   CMake, `zephyr_library_sources_ifdef()` takes the `CONFIG_` name (PR 117468).
8. Load the matching `ref/<vendor>.md` and check its Kconfig section.

## Rules

- Every `config` and `menuconfig` has `help`, indented one tab plus two spaces.
- Driver options live in `drivers/<class>/Kconfig.<driver>`, sourced from the class Kconfig, and
  are named `<CLASS>_<DRIVER>_*` in uppercase.
- A Kconfig choice must not lock out sibling drivers of the same class.
- `$(dt_has_compat,...)` / `DT_HAS_*_ENABLED` gates driver symbols, not SoC-series lists.
- Invalid combinations fail with `BUILD_ASSERT` or `#error` in code, not silently.
- Defaults scoped to the board or SoC that needs them, never a global default changed for one
  platform (PR 105223).
- No option for code that does not exist yet.
- Log level via `module = X` / `source "subsys/logging/Kconfig.template.log_config"`, not a
  hand-written level choice.
- No unused symbol left behind after the change.
- In C, `if (IS_ENABLED(CONFIG_X))` in statements, `IF_ENABLED()`/`COND_CODE_1()` in
  initializers and macro bodies, `#ifdef` only where the code would not compile otherwise.
- A long block's `#else`/`#endif` echo the `#if` condition in a comment; a redefined macro
  says why.
- Init priority comes from `CONFIG_<CLASS>_INIT_PRIORITY`.

## Output

```
[path:line] finding — evidence (enclosing if-chain, sibling file or tips.rst section)
```

No evidence, no finding. Nothing found: `kconfig: clean (N symbols checked)`.
