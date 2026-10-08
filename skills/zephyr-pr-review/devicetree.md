---
description: Use when a diff touches dts/bindings/, any .dts/.dtsi/.overlay, include/zephyr/dt-bindings/, or DT_* macros in C.
---

# Devicetree lens

One job: is the devicetree description correct and consistent with its siblings? CI already
checks formatting and schema (`DevicetreeLinting`, `DevicetreeBindingsCheck`).

## Procedure

1. List every added or changed node with its `reg`, sorted by address; a node out of order in
   its parent is a finding.
2. For a SoC dtsi, diff the node set against the closest sibling series dtsi: peripherals, `reg`
   sizes, `interrupts`, `clocks`, `dmas`, `ranges`. Each difference needs an RM reason.
3. For each binding property added or changed, read it against
   `doc/build/dts/bindings-upstream.rst`:
   - `default:` description says why that value, not "default is X";
     `default` plus `required: true` is a contradiction;
   - properties that only make sense together are `required`;
   - `enum` values are lowercase-with-dashes, each documented, and the behaviour when the
     property is absent is stated;
   - a string `enum` beats several `bool`s for one choice;
   - no `zephyr,`/vendor prefix on generic properties, a vendor prefix on vendor ones;
   - the description says what the hardware does, not how the driver uses it.
4. For each `DT_INST_PROP_OR(inst, p, x)` in C, open the binding: if `p` has `default:`, the
   `_OR` is dead; if the C fallback differs from the binding default, it is a bug.
5. For each binding `include:`, the base matches the role (`pinctrl-device.yaml` with pinctrl, the
   bus base for `on-bus`) and no property the base provides is redeclared.
6. In `tests/` and `samples/` overlays, every enabled peripheral has `pinctrl-0`; a node disabled
   to free a pin has a comment naming the pin.
7. For a new compatible, `grep -r dts/bindings` for one already covering the same IP.
8. In each driver `.c`, check the DT plumbing against a sibling driver of the same class:
   `#define DT_DRV_COMPAT` right after the licence header, before includes; access through
   `DT_INST_*`; instances from `DT_INST_FOREACH_STATUS_OKAY` with `DEVICE_DT_INST_DEFINE` (or
   the class wrapper) and `PM_DEVICE_DT_INST_DEFINE` in the same macro; MMIO from
   `DEVICE_MMIO_*` or `DT_INST_REG_ADDR`, never a literal address.
9. Load the matching `ref/<vendor>.md` and check its DT section.

## Rules

- SoC dtsi uses no `&label` references; it declares nodes. Board dts uses `&label`.
- No SoC-specific addresses in shield overlays.
- `status = "okay"` usage is consistent with the rest of the file.
- Interrupts ascend; `interrupt-names` match the RM.
- Generic node names per the DT spec (`i2c@`, `codec@`, `dma-controller@`).
- Binding `include:` is a bare string for one unfiltered file, the `- name:` form otherwise.
- Fixed clock dividers use `fixed-factor-clock`.
- Named `#define`s in `include/zephyr/dt-bindings/` headers instead of raw numbers in dts.
- Compatibles are quoted; descriptions say "device", not "node".
- Every property is described; units are in the property name (`-microvolt`, `-microamp`).
- Renaming or removing a binding property needs a migration-guide entry.

## Output

```
[path:line] finding — evidence (sibling file, binding line or RM section checked)
```

No evidence, no finding. Nothing found: `devicetree: clean (N nodes, M properties checked)`.
