# Movement loading optimization - v1 experimental

This is the first experimental movement optimization checkpoint. The user's
in-game capture confirms improved perceived movement smoothness and use of
read-ahead, shared motion libraries and object preparation. First-use monster
setup, model texture dependencies and smaller frame overruns remain open work;
this checkpoint does not guarantee stutter-free gameplay.

## Implemented scope

1. `CRaceData::AcquireMotionLibrary` constructs a shared lookup and reference
   library for a race's animation variants. Actors share this immutable library
   instead of allocating a reference wrapper and map node for every animation.
   Each actor keeps its existing clocks, Granny controls, pose, collision, and
   motion selection. A weak reference in the race avoids permanently pinning
   every encountered race's library. Local motion overrides still work.
2. Motion descriptions, race shapes and sound descriptions use leased parsed
   metadata. Each interpreting thread has an LRU limited to 384 entries and an
   estimated 8 MiB. Reentrant acquisition gets a private loader cursor. Active
   leases cannot be evicted; the main cache is released before parser pools at
   shutdown. The shared group-node pool is synchronized.
3. A single low-priority file worker reads/unpacks requested bytes. Normalized
   names deduplicate queued/in-flight requests. Up to 256 requests and 384
   completed entries are retained, with a 16 MiB byte budget, or 48 MiB on
   machines with at least 8 GiB of physical memory. Files larger than half that
   budget use the normal load path. The worker discovers file dependencies in
   race/motion/effect descriptions and motion lists. Foreground reads consume
   prepared bytes where available; a read already in progress can still overlap
   a foreground fallback. No Granny/model/graphics resource construction runs
   on the worker.
4. `CMappedFile` can retain an owning shared buffer, so cache eviction and map
   cancellation do not invalidate a reader. Map changes cancel queued work by
   generation; pack-key changes invalidate prepared bytes under the pack lock.
   Worker shutdown joins before pack destruction. The LZO reusable-buffer pool
   is synchronized, and failed file mappings are checked before decoding.
5. Incoming actors outside the current visible range request race-description
   bytes and their dependencies when available. Visible actors are created
   immediately through the ordinary complete-appearance path. This is driven
   by received race IDs, not per-map or per-monster whitelists. Area properties
   also request upcoming building, tree, effect and collision-file bytes.
6. Walking toward a sector boundary queues the next neighborhood. Terrain
   preparation is split into property/data stages, individual ground blend
   masks, and individual geometry patches, with a 1.5 ms soft frame budget.
   A required terrain section adopts completed work or finishes remaining work
   synchronously. A section is not marked ready before its full preparation.
7. Static objects are considered before their existing loading/visible range.
   Creation candidates are ordered by distance and reuse scratch vectors. One
   shared 2 ms soft budget controls speculative creation across areas. Required
   visible objects bypass that budget so appearance/collision is not withheld.
   Separate preparation/removal radii reduce rebuilding when moving back and
   forth. Draw distance and entity/name visibility settings are unchanged.
8. The active 3x3 sections retain at most three nearby spare terrain/area
   sections. Farther sections enter persistent cleanup queues instead of being
   bulk-destroyed on the next area-list update. Area cleanup destroys object
   batches; terrain cleanup releases texture/geometry units. Normal cleanup has
   a 0.75 ms soft budget, increased to 3 ms when the area backlog exceeds six.
   Terrain cleanup is prioritized when its backlog grows. Map teardown can
   still perform full cleanup while changing maps.
9. Machines with at most 4 GiB of physical memory, low available physical or
   process virtual memory, or high memory load use smaller object margins, at
   most two speculative terrain jobs, and one spare section. Other machines
   stage at most six terrain jobs. New jobs stop while terrain cleanup is backed
   up. These limits bound the new caches/preparation; they do not replace the
   existing four-hour resource-retention policy or cap total engine memory.

## Diagnostic comparison

The existing `[MAP_PROFILE]` output remains automatically enabled. `START`
identifies `version=2 movement_streaming=1`. `OPT` accompanies detailed stalls;
`OPT_SUMMARY` accompanies the five-second frame summaries. Added counters are:

- `metadata_hits` / `metadata_misses`: leased parsed-description reuse.
- `motion_library_hits` / `motion_library_builds`: sharing across actors.
- `read_ahead_hits`: file reads served from prepared owning byte buffers.
- `terrain_prepared`: terrain sections reaching complete readiness.
- `required_terrain_fallbacks`: required loads that still had unfinished work.
  Initial map entry normally includes these; distinguish it from walking.
- `objects_prepared_ahead`: creations beyond the existing visible/loading radius.

New event stages separate race/shape/motion/sound parsing, shared-library
construction, packet-driven actor creation, Granny decoding, material/model
construction, instance binding, and model uploads. Streaming and gradual cleanup
have their own stages. Parent timings are inclusive and must not be summed with
their children. See `map-movement-diagnostics.md` for capture instructions.

## Practical limits

The budgets are soft: one file/driver/third-party call can exceed its budget.
An entirely unfamiliar asset arriving without lead time can still require
synchronous work. This implementation does not delay actor visibility, replace
appearance with a placeholder, postpone combat animations, or preload the
entire monster catalogue. Granny resource decoding remains on its existing
engine thread. Improvements require a new in-game capture; compilation alone
does not establish smoothness or gameplay correctness.

## Build and deployment

Build `Metin2Client.sln` as Release/Win32 or Debug/Win32 with v143. No pack/root
changes or new external runtime dependencies are required. The user-facing
test executable is the Release build. Source and baseline syserr backups are
under `bin/movement-source-backup-*`; the previous executable is backed up
separately before deployment.
