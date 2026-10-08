# NXP reference

Read by a lens when the diff touches `boards/nxp/`, `dts/arm/nxp/`, `soc/nxp/` or
`drivers/**/*mcux*`/`*nxp*`.

## Audio boards

- SAI, MICFIL and audio PLL nodes: MCLK routing and `fs-ratio` come from the board DT; see
  `ref/audio.md` and `ref/i2s.md`.
- Board `.conf` entries made unnecessary by a driver change are removed in the same PR.

## DMA

- eDMA peripheral transfers need the hardware request enabled (`ERQ`); see `ref/dma.md`.
- An eDMA default change for one client (SAI) is scoped to that client, not all eDMA users
  (PR 105223).
