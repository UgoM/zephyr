# DMA reference

Read by a lens when the diff touches `drivers/dma/`, `include/zephyr/drivers/dma*`,
`dts/bindings/dma/` or `tests/drivers/dma/`.

## Paths to walk

- `stop` and `free`: a flag cleared unconditionally and then tested makes the hardware halt code
  dead (PR 107009). Walk each branch with the state it is entered in.
- `stop` called from an ISR must stop the engine, not only mask its interrupt, or the transfer
  continues (PR 101685).
- Every early return after taking the channel lock releases it (PR 107009).
- A failed `start` clears the peripheral DMA request and the driver's busy state (PR 109297).
- An enabled channel can be active, pending or blocked waiting for a peripheral request; a PM
  transition check covers all three (PR 117092).
- Scatter/gather: with both gather and scatter disabled, no gather/scatter branch runs
  (PR 105366).

## Parameter checks

- Channel bounds use `>=` against the count; per-channel indices (TCC) are checked too
  (PR 107469).
- `dma_cfg->head_block != NULL` before dereferencing (PR 107009).
- No `< 0` tests on unsigned values (PR 108187).

## Trigger semantics

- Peripheral-to-memory start enables the hardware request (`ERQ=1` on NXP eDMA-style engines), not
  only a software start (PR 97841).

## Tests

- `tests/drivers/dma/` (every suite in the directory) pass on the
  target; failure paths are covered as well as success.
- `ztest_test_skip()` is not followed by `return TC_SKIP` (PR 99835); `TC_PRINT` ends with `\n`.
- No new sample for flows the m2m tests already validate.

## Shape

- No typedef'd structs; no SoC/family `#ifdef` forests; register offsets as `#define` with
  `sys_read32`/`sys_write32` or the vendor HAL.
- Driver Kconfig scoped to the driver (`DMA_DW_AXI_CCU_SUPPORT`, PR 108187); no `u32`.
- Driver, dts, boards and doc changes in separate commits in dependency order (PR 107565).
- Defaults scoped to the client that needs them (I2S/SAI), not changed for every eDMA user
  (PR 105223).
