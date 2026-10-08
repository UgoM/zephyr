# Audio reference (codec, DMIC, haptics, charger)

Read by a lens when the diff touches `drivers/audio/`, `include/zephyr/audio/`,
`dts/bindings/audio/`, `samples/drivers/audio/`, `tests/drivers/audio/`,
`tests/drivers/build_all/audio/`, or the haptics/charger/fuel-gauge equivalents.

## Contract (`include/zephyr/audio/codec.h`, `dmic.h`)

- New API fields document unit, range and who defines them; "codec-specific" is not a unit
  (PR 111229: gain in dB clamped to +/-12). A value that must match a fixed set (EQ band
  centres) says so or exposes the set.
- Driver parameter types match the public field (`uint32_t band`, not `int`) (PR 111229).
- Shell and API input is range-checked before any narrowing cast; out of range returns
  `-EINVAL` (PR 110693).
- A new feature does not flip existing behaviour: writing a previously untouched enable bit
  (`EQ_ENA`) is either proven neutral at reset values in the commit body or done lazily on first
  use (PR 111229).
- Generic code (codec shell, samples) does not hardcode which side drives BCLK/FS; precedent:
  `CONFIG_SAMPLE_USE_CODEC_CLOCK` in `samples/drivers/i2s/echo/` (PR 110693).
- `start_output`/`stop_output` and mute touch only mute bits, never stored volume (PR 106836).
- Gain or other settings that the hardware can change at runtime are runtime properties, not DT.
- `-EINVAL` for bad data, `-EIO` for a failed bus access.

## Driver shape

- Register field ranges equal the datasheet table exactly; cite revision and table. Known case:
  WM8904 Rev 4.1, Table 69 `FLL_OUTDIV` max 32, Table 71 `FLL_CLK_REF_DIV` max 8 (PR 112540).
  Datasheet: https://statics.cirrus.com/pubs/proDatasheet/WM8904_Rev4.1.pdf
- Register reset side effects per the datasheet (a soft reset re-enabling charging, the
  `REG_RESET`/`EN_CHG` case): the driver restores safe state after reset.
- Device-ID / REVID check at init for the silicon revisions supported.
- Datasheet timing constants are named, with the source.
- Every call that can fail is checked, including DAC/clock start and stop (PR 115001); bounds
  with `IN_RANGE()`.
- No `static const` tables or struct definitions in driver headers; keep them in the `.c`.
- Repetitive switch arms keyed by register want a lookup table (minor; not for a minimal fix).
- Read-only per-instance data (pins, ranges) in `_config`; precedent `drivers/haptics/cirrus/cs40l26.c`.
- File naming follows `drivers/audio/` (`dac_stm32.c`, `dmic_stm32_mdf.c`), not `vendor_class.c`.
- Per-driver Kconfig in `drivers/audio/Kconfig.<driver>`; a DMIC symbol under one driver must not
  lock out a sibling (DFSDM vs MDF).
- New codecs are added to `tests/drivers/build_all/audio/`.
- Release notes go in the Audio section, not Bluetooth: Audio (PR 105255).

## Bindings

- Every property described, with the description under the property name; no example DTS.
- Integer over string for numeric hardware values, unless the integer would be undocumented
  magic (the WM8904 `mic-bias` string was accepted on that basis).
- Generic properties carry no vendor prefix; units in micro (`-microvolt`, `-microamp`);
  generic battery data in `battery.yaml`; `on-bus` comes from the base.
- A GPIO property says whether it is an input or an output and what it drives.

## Board enablement with a codec

- Find every board using the codec (`grep -rl <compatible> boards/`; WM8904 is on four NXP and two
  ST boards). A driver change builds for all of them and leaves their behaviour unchanged.
- The PR states the MCLK source (PCK, SAI MCLK, FLL), `fs-ratio`/`mclk-frequency`, whether RX was
  tested, mic-bias and input PGA settings (PR 114415). MCLK is a multiple of FS, not of the CPU
  clock.
