# Map movement diagnostics

This diagnostic build enables map profiling automatically in both Debug and
Release. Set the process environment variable `M2_MAP_PROFILE=0` before launch to
disable capture. The profiler does not alter loading, visibility, collision,
resource retention, graphics settings, or the existing engine timer.

## Capture a walk

1. Completely close the old client, then start the rebuilt client normally.
2. Walk or ride across the map for two or three minutes, including places where
   the game pauses. Travel far enough to cross several terrain sectors.
3. Turn back and repeat the same route. A first-pass-only pause points toward
   cold resources; pauses on both passes may involve sector construction,
   object recreation, cleanup, rendering, or memory pressure.
4. Exit normally, then save `Game Client/syserr.txt` before starting another
   client session: the existing logger overwrites this file at launch.
5. Supply the entire log plus the map name, route, and whether the pause was on
   the first or repeated pass. No root repack is required.

## Report format

Every diagnostic line starts with `[MAP_PROFILE]`; these are measurements, not
game errors. `START` confirms the diagnostic build is running. `SUMMARY` appears
about every five seconds while world updates are captured, even without stalls.

At most one detailed capture is written per one-second reporting interval: the
frame with the longest captured work among qualifying frames. `HITCH` includes
the original tick, frame ID, map, player position, sector, foreground status,
whether rendering began, and truncated-event counts. `COUNTS` lists actual cold
resource-load calls and object/terrain/area creation or destruction calls;
these count attempts, not guaranteed successful loads. `other_hitches` counts
additional qualifying frames in that interval.

The capture retains up to 192 events of at least 1 ms, keeping the longest when
full. Each detailed report prints the 16 longest retained events. Shorter calls
still contribute to parent durations and operation counts. The final pending
capture and summary are flushed on normal client shutdown.

- `work_ms` is elapsed wall time within the captured portion of `Process()`,
  ending before intentional frame-pacing sleep. It includes API waits and OS
  scheduling delays; it is not a CPU-cycle or GPU execution-time measurement.
- `outside_process_ms` includes the preceding frame-pacing sleep, window message
  handling, and time outside the capture. A large value alone does not identify
  map loading. Foreground gaps of at least 100 ms can also trigger a capture.
- `total_ms` includes nested instrumented operations. Do not sum parent and
  child totals. `self_ms` subtracts measured child scopes; it includes work that
  has no finer instrumentation and some diagnostic overhead.
- `resource.pack_read` includes packed-file access and unpacking.
- `resource.decode` includes the resource's `OnLoad`, including nested assets
  and any graphics work it performs. This fixes the diagnostic blind spot of
  the original load-cost field, which stops before `OnLoad`.
- Terrain stages separate height/normals, attributes, tile/blend-mask generation,
  shadow/minimap resources, and geometry. Object stages identify the property
  CRC, property file, and model/tree/effect file where available.
- `map.bulk_cleanup`, `map.area_destroy`, and `map.terrain_destroy` identify map
  deletion costs independently of the resource manager's deletion budget.
- Update/render stages distinguish resource loading from animation, collision
  collection, shadows, effects, drawing, and `Present` waits.

Scopes use a per-thread fixed buffer. Only the thread inside a diagnostic frame
captures events; the existing startup loading worker does not write into it.
No profiler heap allocation is performed per frame. Reports are emitted after
capture closes, and their elapsed write time is reported in
`previous_flush_ms`. Logging and instrumentation still affect real timings.

Build `Metin2Client.sln`, Release/Win32, v143. This is instrumentation only;
optimization choices should follow the captured results.
