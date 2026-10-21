# Map movement diagnostics

The crowd-rendering build uses `START version=4 crowd_rendering=1` (with other
fields between them). It retains the v3 measurements below and adds `CROWD` /
`CROWD_SUMMARY` counters and finer animation/effect stages. See
[crowd-rendering-optimization.md](crowd-rendering-optimization.md) for the
implementation, counter meanings and the new capture procedure.

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
game errors. `START version=3 movement_streaming=1 hitch_ms=16.7` confirms the
new diagnostic build is running (other fields appear between these values).
The version is the diagnostic format version, not the optimization version:
the gameplay implementation remains the v1 experimental optimization.
`SUMMARY` appears
about every five seconds while world updates are captured, even without stalls.

At most one detailed capture is written per one-second reporting interval: the
frame with the longest captured work among qualifying frames. Work of at least
16.7 ms qualifies, including the smaller stutters previously counted only in
summaries. A larger hitch can replace a smaller one in the same interval.
`HITCH` includes
the original tick, frame ID, map, player position, sector, foreground status,
whether rendering began, and truncated-event counts. `COUNTS` lists actual cold
resource-load calls and object/terrain/area creation or destruction calls;
these count attempts, not guaranteed successful loads. `other_hitches` counts
additional qualifying frames in that interval.

`FRAME_STAGE` adds up every measured call by stage in that captured frame,
including calls shorter than 1 ms. Up to twelve stages with the largest self
time are printed. This distinguishes a single expensive call from hundreds of
small calls even when none appears in the individual `EVENT` list.

Each five-second summary also emits:

- `WINDOW`: frame/tick boundaries, rendered-frame counts, overruns that rendered,
  the last map, map changes within that window and stage-capacity overflows.
  A window can contain both map-entry and normal gameplay frames. A zero map
  change count does not prove that no loading occurred.
- `STAGE_SUMMARY`: call counts, participating frames, inclusive/self totals,
  inclusive average per captured world frame, maximum inclusive sum in one
  frame, maximum individual call, and self time in frames above 16.7/33.3 ms.
  The over-budget frame counts mean the stage participated in those frames;
  they do not establish that it caused the overrun. Selection includes the top
  twelve stages by self time, top six by inclusive time, and active `pack.*`
  stages, with at most 32 rows per window.

All stage totals include sub-millisecond calls and are independent of event
truncation. A fixed registry holds 128 distinct stage names; `stage_drops`
reports any calls that could not be classified if that capacity is exhausted.
Repeated/recursive scopes of the same stage contribute each call to inclusive
totals; use self totals for attribution rather than summing nested totals.

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
- `resource.pack_read` includes file access and unpacking. Its new children are
  `pack.get` (also covers metadata reads), `pack.read_ahead_lookup`,
  `pack.lock_wait`, `pack.lock_held`, `pack.lookup`, `pack.entry`,
  `pack.open_map`, `pack.loose_open_map`, `pack.validate` and `pack.unpack`.
  Lock wait measures `EnterCriticalSection`; lock-held time starts after that
  acquisition and includes nested entry access. Validation covers configured
  checksums, and unpacking includes decompression/decryption, allocation and
  supplementary-data assembly. Mapping is not a physical-disk timer: Windows
  can fault mapped pages later during validation, unpacking or resource decode.
  A lock wait alone does not identify which other thread held the lock.
- `metadata.interpret` separates buffer allocation/copy and text interpretation
  from the file fetch inside the existing `metadata.parse` scope.
- `network.actor_visibility_update`, `network.packet_dispatch` and
  `network.enter_game_phase` separate local actor visibility maintenance, packet
  handling and entering the game phase inside `app.network`.
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
No profiler heap allocation is performed per frame. Stage collection also uses
fixed buffers and never writes logs while the pack lock is held. Reports are emitted after
capture closes, and their elapsed write time is reported in
`previous_flush_ms`. Logging and instrumentation still affect real timings.

Build `Metin2Client.sln`, Release/Win32, v143. The v3 changes add instrumentation
to the existing v1 optimization; further optimization choices should follow
the captured results. `M2_MAP_PROFILE=0` disables diagnostics, not the v1
movement optimization.
