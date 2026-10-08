# Tracing reference

Read by a lens when the diff touches `subsys/tracing/`, `include/zephyr/tracing/`,
`scripts/tracing/`, `samples/subsys/tracing/`, `tests/subsys/tracing/`, `doc/services/tracing/`
or the CI workflow that runs them.

## Format (CTF)

- The event-id field width in C matches the TSDL metadata (`subsys/tracing/ctf/tsdl/metadata`);
  ids beyond 255 need the `uint16_t` id type on both sides. A mismatch breaks babeltrace.
- A CTF record has no length field: a decoder meeting an unknown id cannot skip it. The decoder
  stops explicitly with a warning, and the docs say so.
- New ids do not collide with or renumber existing ones; every recorded trace depends on them.
- Event ids, lane ids and format constants have one source of truth, not a copy in a fallback
  table.

## Data path

- Nothing is read and not emitted, reordered, re-timed or sent to the wrong lane. Keyword or
  "first match wins" routing heuristics are explicit and documented.
- Readers and viewers handle resume, replay and cursor moves without losing state.
- No `printk` or other output enabled by default as a side effect.
- Hooks are cheap: macros over `__weak` function calls on the hot path; debug strings only at
  `LOG_DBG`.
- A new hook covers every path that produces the event (idle, ISR, networking), not one object
  type in isolation; a special case for one kernel object needs the general design first.

## Samples, tests, CI

- A tracing sample demonstrates a feature; extra scenarios, fixtures and output checks belong in
  `tests/subsys/tracing/`.
- The README documents what each backend/mode requires and the exact commands; run them.
- The workflow's `paths:` filter triggers on the new files, or the new tests never run.
- Behaviour or default changes come with deprecation and migration notes; performance or
  footprint claims come with measured numbers.
- `CHECKIF` for API argument checks so they compile out; `MAINTAINERS.yml` updated for new files.
