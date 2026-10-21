# Crowded-area diagnostics (MAP_PROFILE version 7)

Version 7 adds `DRAW_OPT` records for rigid batching savings, matrix-guard
fallbacks, and shared skinning constant builds/reuse. `DETAIL_ASSET` model rows
with mesh=-2 represent ordered rigid batches spanning mesh parts. Their phases
are `rigid_one`, `rigid_two`, or `rigid_untextured`.

Detailed capture is enabled by default with the existing map profiler. It
samples one Process frame in 17 and writes bounded summaries every five seconds
to syserr.txt. `M2_CROWD_DETAIL=0` disables only this additional capture;
`M2_MAP_PROFILE=0` disables the whole map profiler.

Stand in the crowded desert area for about 20–30 seconds, looking at the metins
and their effects. Then move/look away briefly and return to the same view.
Exit normally to flush the final partial window and share syserr.txt.

The additional records are:

- `DETAIL_WINDOW`: sampled update and rendered frame counts, row capacity drops,
  and the associated ordinary profiler window. These are sampled counts, not
  totals for every frame or an FPS measurement.
- `DETAIL_ASSET`: join by `(first_frame, id)` to the records below. Includes the
  GR2 asset, model and mesh names/index, mesh/material type, texturing method,
  and pass (`actor_main`, `model_main`, `model_blend`, `shadow_map`,
  `shadow_projected`, `pc_blocker`, or `effects`). Unclassified direct rendering
  is labelled `unspecified`. Mesh type 0 is rigid, 1 is deformable; material
  type 0 is diffuse, 1 is blend.
- `DETAIL_TIME`: calls, sampled frames containing this row, cumulative CPU wall
  time, maximum call and maximum sampled-frame time. A model call is one mesh
  visit in one material-type/texturing pass; an effect root call is one actual
  update/render invocation, not a count of distinct instances. Timings include
  nested work and instrumentation inside the scope. They are not GPU timings.
- `DETAIL_MODEL`: requested groups, merged submissions saved, material binds,
  actual device attempts, successful/failed attempts, zero/negative triangle
  attempts, triangles from successful draws, successful GPU skin draws, and
  groups skipped because CPU fallback vertices were unavailable. A failed GPU
  attempt followed by a CPU retry counts as two device attempts. Empty groups
  and failed draws retain their original behavior; diagnostics never skip them.
- `DETAIL_EFFECT`: particle quads/batches, unbatched submissions and failures,
  effect mesh submissions/failures/triangles, particle system update/render
  calls, summed live particle counts observed at those calls, and calls with
  the particle attach flag enabled. `live_update` is measured before each system
  update. Quads can exceed particles for two/three-face billboards.

Effect filenames identify the owning MSE script. Root `update`/`render` rows
include child `particles_*`, `meshes_*`, and `lights_update` work: do not add a
parent and its children. Divide root render counts by sampled rendered frames;
divide update counts by sampled update frames. For example, batches/quads shows
how small the particle draws are. The attach flag describes particle-local
coordinates and does not by itself identify an effect's owning actor.

Each window reports up to six model rows by CPU time plus six by group count,
eight effect root rows by time, and eight effect component rows by time. Fixed
capacity storage avoids per-draw allocation and log I/O. Capacity drops are
explicit. Asset/name fields are bounded copies, safe after models are unloaded.

This change only observes existing calls. Loading-screen behavior, rendering
order, particle counts, attachment transforms, and animation timing are unchanged.
