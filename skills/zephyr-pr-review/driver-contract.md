---
description: Use when a diff adds or changes a driver .c file, a driver API op, a subsystem public API, or its tests.
---

# Driver contract lens

One job: does the driver fit the device model like its siblings, and does each operation do
exactly what the public API documents, on every path?

## Procedure

1. Name the closest sibling driver in the directory and compare shapes; a deviation counts only
   if most of the directory follows the pattern (give the `grep -l` count).
2. List every API op the diff implements or changes (`DEVICE_API(...)` members) and every
   public function it adds; copy each one's `@retval` list from its Doxygen.
3. Walk every return path. Write the value returned and whether it is documented. Undocumented
   values, a swallowed error, or `-EIO` for a bad argument are findings (PR 89776). Returns are
   checked before outputs are used.
4. Parameter types match the prototype; a narrowing cast is preceded by a bounds check
   (PR 110693, PR 111229); unsigned values are never tested `< 0` (PR 108187).
5. Every early return after taking a lock, starting a clock or enabling an IRQ releases it. A
   failed start clears state; on error the peripheral is left disabled (PR 109297, PR 88631).
6. State machines: for each op and each state, say what happens. `stop` stops the hardware
   (PR 101685); PM suspend/resume covers every state (PR 117092); `_deinit` undoes `_init`.
7. ISR-callable ops do not sleep, take a mutex or log heavily. Locks are taken before the state
   check; a function needing a held lock asserts it.
8. Tests in `tests/drivers/<class>/` cover the failure path and clean up after it (PR 105234);
   generic API tests have no vendor checks (PR 90720). Name the suite that must pass.
9. Load the matching `ref/<subsystem>.md` and check its contract section.

## Rules

- `static DEVICE_API(<class>, <name>)` for the API instance.
- `const struct <driver>_config` holds everything read-only (bus, pins, clocks, pinctrl,
  `irq_config_func`); `struct <driver>_data` holds runtime state. No typedef'd structs.
- `LOG_MODULE_REGISTER(<name>, CONFIG_<CLASS>_LOG_LEVEL)` in the main file, `LOG_MODULE_DECLARE`
  elsewhere. `LOG_ERR` with the errno for failures the caller cannot see, no "Error" word;
  per-transfer traces at `LOG_DBG`.
- Arguments checked at the API boundary (`CHECKIF` where used); invariants by `__ASSERT_NO_MSG`.
- No default silently changed (a "harmless" enable bit): make it lazy or prove it neutral.
- No vendor knobs in a generic `struct <class>_config`; hardware choices come from DT
  (PR 110573, PR 108374). Generic code does not hardcode a clock topology.
- Register maps and private types in `drivers/<class>/<driver>.h`, never under `include/`; no
  `static const` tables in any header.
- Hot paths: `static inline` over function-like macros.
- A bitmask parameter is not an `enum` type: `A | B` is an `int`, which C++ will not narrow.

## Output

```
[path:line] finding — evidence (sibling file, header:line of the @retval, the path walked)
```

Then the op × return-path table from step 3. Nothing found: `driver-contract: clean (N ops)`.
