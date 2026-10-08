---
description: Use when a diff touches drivers/, soc/, or dts with register writes, clock, IRQ, DMA request, pin or timing values.
---

# Hardware correctness lens

One job: are the hardware facts in the diff true for every SoC/part the code builds for? This lens
does not judge style or API semantics.

## Procedure

1. Extract every hardware fact from the diff: register offset or field, bitmask, clock source or
   divider range, IRQ number, DMA request line, pin/AF number, reset value, timing constant.
   Write them as a list before checking any.
2. For each fact, find its definition: the HAL or CMSIS header under `modules/hal/<vendor>/`
   (`west list` for the path; the parent `modules/` of the workspace), or the sibling series'
   driver/dtsi in tree. Record the file and line. A fact you cannot locate is reported as
   "unverified", never as correct.
3. For each `#if`/`#ifdef` on a series or part number, list which series actually have the
   feature according to the HAL. Prefer a capability macro from the HAL (`#ifdef HPDMA1`,
   `RCC_PLLP_DIV_2_31_SUPPORT`) over a series list; flag guards that are too wide or too narrow.
4. For each range (divider, PLL multiplier, FLL/MCLK ratio), compare the min/max in code and in
   the binding with the datasheet table. Name the table and revision. Check off-by-one at both
   ends and wrap-around in the arithmetic.
5. For each read of a multi-word counter or status, check for a consistent-read loop (hi/lo/hi).
   For each poll loop, check it is `WAIT_FOR()` or bounded with a timeout and an error return.
6. For each register write in an init or reset path, check the datasheet reset side effects
   (writing a reset register clears others; a field only valid in a given mode).
7. For codec/peripheral clocks: MCLK is an integer multiple of the frame rate; the ratio used in
   code matches DT (`fs-ratio`, `mclk-frequency`) and the board actually routes that clock.
8. Load every `ref/*.md` matching the vendor and subsystem, and check each listed pitfall
   against the diff.

## Rules

- Values come from DT or the HAL, not magic numbers; derived values use the HAL macro.
- Register fields and sizes use `BIT()`, `GENMASK()`, `FIELD_GET()`/`FIELD_PREP()`, `KB()`,
  `IN_RANGE()`, `DIV_ROUND_UP()` instead of open-coded arithmetic.
- The LL function over open-coded register access; no `volatile` casts on CMSIS fields.
- Register access through `#define` offsets and `sys_read32`/`sys_write32`, or the vendor HAL;
  not ad-hoc struct overlays.
- A datasheet quirk or errata workaround carries a comment naming the errata or section.
- Pin pull and polarity match the schematic (floating buttons need a pull-up); `gpio_pin_get_dt`
  already applies `GPIO_ACTIVE_LOW`, so do not invert again.
- Buffer sizes derive from the payload, not from the hardware FIFO size (PR 112073).
- Do not emulate in software a hardware bit the part provides.

## Output

```
[path:line] finding — evidence (HAL header:line, datasheet table, or sibling file)
[path:line] unverified: <fact> — where it was searched
```

Nothing found: `hw-correctness: clean (N facts verified, M unverified)`.
