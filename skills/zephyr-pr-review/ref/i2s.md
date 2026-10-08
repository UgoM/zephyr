# I2S reference

Read by a lens when the diff touches `drivers/i2s/`, `include/zephyr/drivers/i2s.h`,
`dts/bindings/i2s/`, `tests/drivers/i2s/` or `samples/drivers/i2s/`.

## Protocol facts

- I2S and left/right-justified: always two channels on the wire; FSYNC (WS) is one channel word
  long. PCM short: FSYNC is one SCK period; PCM long may use one channel word (PR 82144).
- `channels == 1` with I2S/LJ/RJ means the same sample on both channels, not one physical channel
  (PR 89074).
- Every driver supports `I2S_FMT_DATA_FORMAT_I2S` unless the hardware cannot; if it cannot,
  `tests/drivers/i2s/i2s_api` is adjusted in the same PR.
- MCLK is a multiple of FS.

## API contract

- Per-direction state: functions that report state take an `i2s_dir`, and `I2S_DIR_BOTH` is
  rejected when one value is returned (PR 108181).
- Underrun/overrun maps to `I2S_STATE_ERROR` as documented in the `i2s_interface` group.
- Enable/disable paths are symmetric for TX and RX (PR 110000).
- Block size is not clamped to the hardware FIFO; the driver streams a block through the FIFO in
  several DMA transfers.
- `struct i2s_config` carries no vendor or TDM-specific fields; slot masks belong in a dedicated
  TDM structure, peripheral knobs in DT (PR 110573, PR 108374).
- Optional DT properties (`fs-ratio`) read with `DT_INST_PROP_OR(n, fs_ratio, 0)` and the invalid
  value checked where used (PR 102868).

## Tests and samples

- `tests/drivers/i2s/i2s_api` must pass; the invalid-trigger test leaves the queue clean for the
  next test (PR 105234).
- No vendor-specific checks in the generic tests (put them in `tests/boards/<vendor>/`,
  PR 90720); no Kconfig parametrizing runtime behaviour with one scenario per value: query
  support and skip (PR 73160).
- Test buffers' element size matches the configured word size.
- A new sample is its own commit, with README, subject prefix `samples: i2s: <name>:` (PR 95610).
- Identical per-core overlays become one `*_common.overlay` included by each.
- `LOG_DBG`, not `LOG_INF`, in per-block paths (PR 75943); `FIELD_GET()` for register fields.

## Kconfig

- Driver options uppercase and driver-scoped (`I2S_NRF_TDM_*`, PR 95249); none for code that does
  not exist.

## STM32 SAI

- Shared IP reuses the existing compatible (`st,stm32h7-i2s`, `st,stm32-sai`); see `ref/stm32.md`.
