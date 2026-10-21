# CPU particles and FPS display — version 11

## Particle work

Particle frame buckets now use pointer vectors with stable compaction instead
of individually allocated list nodes. Surviving particles retain their order.
Texture migrants are still staged separately, newborn particles are appended
before migrants, and every original update and render iteration retains its
ordering. Particle objects remain in the existing pool. No counts, lifetimes,
visibility rules, material states, attachment transforms or emission schedules
were reduced.

The normal game uses a compact particle update instead of allocating and walking
a virtual decorator chain per particle. Six per-particle curve cursors retain
the original boundary/retirement behavior. Packed color interpolation retains
the separate multiply/add operations. Rotation, texture animation, scale Y,
scale X, color, gravity and air resistance execute in the original order.
Texture timing retains its strict negative-time loop, BYTE frame-count behavior
for CCW/random decorators, and random-number calls. Emission bursts still inherit
their first particle's resolved rotation and texture directions, as legacy
decorator cloning did. Movement integration and angular orbit code are unchanged.

`M2_PARTICLE_COMPACT_UPDATE=0` retains decorator updates with the new vector
storage. The world editor keeps its existing decorators. The original render
preparation/stream switches remain opt-in; this pass does not enable them.

`[PARTICLE_CPU] ACTIVE` announces actual compact initialization once. Version 11
`CPU_CROWD` reports successful `particle_compact_updates` and `actor_lod_no_work`
counts for captured frames. Existing particle and fallback counters remain.

## Character work

Collision spheres previously built and multiplied a full translation matrix to
read only its translation row. They now transform the sphere center as a
homogeneous point without a perspective divide. Current/previous sphere positions
and movement additions are still updated on every original call.

LOD distance calculations return early only when no controller has more than one
model, matching the existing per-controller return. Models with alternatives
keep the original LOD selection and animation frequency. Empty attachment lists
return before accessing the effect manager and timer; existing and future
attachments continue through the original update code.

Dynamic dog poses are still evaluated independently. This is a smaller character
CPU pass, not shared animation sampling or GPU crowd instancing; a large gain
for 3,000 dogs has not been established.

## FPS label and delivery

The minimap creates an outlined `FPS: N` line 18 pixels below its existing server
name. Coordinates and observer count move down 18 pixels. Parenting to the open
minimap follows its visibility. Python polls `app.GetRenderFPS()` at most four
times per second and changes text only when the number changes. The existing
native FPS counter now divides rendered/update counts by the actual elapsed
measurement window; the value represents rendered frames, not simulation ticks.

The Release Win32 client compiled with zero errors (19 existing warnings in
rebuilt code). The minimap compiled with Python 2.7. Packaging used the actual live
root archive: all 98 extracted members survived and only `uiminimap.py` changed.
`tools/deploy_crowd_fps.py` retains the original archives/script and a deployment
receipt under `bin/crowd_cpu_fps_package_20261021_191652`. Pre-edit source snapshots
are in `bin/crowd_cpu_fps_backup_20261021_191652`.

No game session or automated tests were run. In-game appearance, combat, movement,
equipment effects and crowded-scene FPS still require the user's manual run.
Map-change loading and texture uploading were not changed.
