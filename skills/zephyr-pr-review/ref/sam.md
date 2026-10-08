# Atmel / Microchip SAM reference

Read by a lens when the diff touches `soc/atmel/`, `dts/arm/atmel/`, `boards/atmel/`,
`drivers/**/*sam*`, `atmel,` compatibles or `hal_atmel`.

## Sources of truth

- HAL: `modules/hal/atmel/asf/sam/include/<series>/` (component headers `component/*.h` for
  register fields, `instance/*.h` for addresses, `pio/*.h` for pin functions).
- Pattern bindings: `dts/bindings/pinctrl/atmel,sam-usart.yaml` and siblings.

## Kconfig

- Hierarchy is family (`SAM_D5X_E5X`) then series (`SAMD5X`, `SAME5X`); each level has its own
  Kconfig and series facts (`SOC_SERIES_REVISION`) live at series level.
- Symbols and `select`s are inside the `if` for their family/series, or they apply to every
  Atmel part.
- Kconfig names match the DT compatible they gate.
- Board defconfig is minimal; a driver for a node the board dts enables by default is the one
  allowed addition.

## Devicetree

- Pin configuration uses pinctrl (`pinctrl-device.yaml`, `pinctrl-0`/`pinctrl-names`); the
  legacy pinmux driver is end-of-life.
- Pin definitions carry their `sam,func` (or series equivalent) in every dtsi the PR touches.
- Clock and peripheral ids come from DT (`clocks = <&gclk ...>`, `<&pmc PMC_TYPE_PERIPHERAL n>`),
  not constants in the driver.
- Nodes are ordered by address in every touched dtsi.

## Commits and docs

- SoC/board series order: SoC, then bindings, then drivers, then boards.
- Refactors keep the original copyright line.
- Board docs use the RST `list-table` feature table with the full feature list, hardware-accurate
  (PSRAM is not SDRAM); board images are downscaled and compressed.

