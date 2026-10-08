# STM32 reference

Read by a lens when the diff touches `soc/st/stm32/`, `dts/arm/st/`, `boards/st/`,
`drivers/**/*stm32*` or `st,stm32*` compatibles. Each item is a fact or a known pitfall to check.

## Sources of truth

- HAL and LL: `modules/hal/stm32/stm32cube/stm32<series>xx/drivers/include/`
  (`stm32<series>xx_ll_*.h`); CMSIS register definitions in `.../soc/stm32<part>.h`. Capability macros there (`RCC_PLLP_DIV_2_31_SUPPORT`,
  `RCC_PLLM_DIV_1_16_SUPPORT`, `HPDMA1`) beat hand-written series lists.
- Clock drivers to pattern-match: `drivers/clock_control/clock_stm32l4_l5_wb_wl.c`,
  `drivers/clock_control/clock_stm32f2_f4_f7.c`.
- Facts are per SoC, not per family: check the RM of each part the guard covers.

## Kconfig

- A HAL name shared by several parts (WL30x/31x/33x as `STM32WL3XX`) needs
  `STM32CUBE_SOC_NAME_OVERRIDE` in `Kconfig.defconfig`, inside `if SOC_SERIES_...`, written to work
  for every sibling part, not one.
- SoC `Kconfig` keeps the canonical `select` grouping (architecture/CPU, hardware, software,
  STM32-specific), blocks separated by a blank line and comment.
- Board defconfigs do not enable `CONFIG_GPIO=y`, `CONFIG_SPI_STM32_INTERRUPT=y` or similar
  defaults.
- Guards match the exact subset (`CONFIG_SOC_SERIES_STM32L4X && !CONFIG_SOC_STM32L4PLUS`) and
  include parts that exist but are not yet in tree (`CONFIG_SOC_STM32F401XB`).
- HAL2 defines sit together under one `#ifdef CONFIG_STM32_HAL2`.

## Devicetree

- Series dtsi files are named after the HAL part (`stm32wl33xx.dtsi`, not `stm32wl33.dtsi`).
- Declare every node with `reg` for all packages; a node not bonded on some packages gets an
  "Available only on specific packages" note, not a commented-out block (pattern: STM32G0).
- Clocks: `clocks = <&rcc STM32_CLOCK(BUS, BIT)>` with bus and bit from the RM RCC chapter
  (PR 103487).
- Add every GPIO port the RM lists for the part (PR 106769); verify every Arduino header pin
  against the board UM (PR 108943).
- Shared IP reuses the existing compatible (`st,stm32h7-i2s`), no new per-series compatible.
- DMA request count is the number of lines (0..144 means 145).
- Cache line size: C5 has 16-byte lines (`i-cache-line-size = <16>`).
- Newer compatibles replace old properties (`st,stm32-pwr-wkupctrl` for wake-up pins).
- FDCAN clock calibration unit has its own MMIO and node; do not fold its IRQ into FDCAN.
- `zephyr,system-timer` replaced `stm32_lp_tick_source`; boards and migration notes follow.
- Shield overlays carry no SoC-dependent addresses; those go in the board dtsi.

## Hardware pitfalls

- ITRx availability differs by timer and series.
- PLL `div-p`: L47x/L48x allow 7 or 17; L49x/L4Ax allow 2..31 (PLLxPDIV). F2 and F401 have no
  PLLSAI; F2/F401 `div-p` values have no generated macro, so they need a `BUILD_ASSERT`, as does
  an invalid L4 `mul-n`.
- PLL bindings carry min/max for `div-m`, `mul-n`, `div-p` with per-series sub-ranges commented;
  validation covers PLLSAI1, PLLSAI2 and PLLI2S, not only the main PLL.
- H72x/H73x ADC3 has no channel preselection. ADC `CALADDOS` is writable only in calibration
  mode. A calibration function does single-ended or differential, not both.
- H7 GPIO `ODEN` handling exists only on H74x/H75x (PR 112087).
- SDHC R2 response registers are read `RESP4`..`RESP1`.
- CMSIS register fields are already `volatile`.
- Use the LL function when one exists; LL helpers touch one register each.
- Flash extended ops guarded by `CONFIG_FLASH_EX_OP_ENABLED`; keep `HAL_StatusTypeDef` where the
  HAL returns it.
- Inter-processor channels on STM32 are IPCC, not IPM; name them so.
- No `#if SOC_SERIES` block that contains only a comment (PR 99821); no deprecated LL/HAL calls
  (PR 104749); prefer LL over HAL (PR 100463).

## Boards and tests

- Board buttons: pull and polarity per schematic and UM (floating buttons need `GPIO_PULL_UP`).
- `model`/`compatible` follow sibling ST boards.
- Test overlays under `tests/drivers/*/boards/` have `pinctrl-0` (PR 107959); a conflicting node
  is disabled with a comment naming the pin.
- `platform_allow` lists in shared tests do not grow per STM32 board; an STM32 overlay must not
  break other vendors' builds of the same test.
