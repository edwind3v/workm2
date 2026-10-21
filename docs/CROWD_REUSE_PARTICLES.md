# Crowd pose reuse and particle streaming

This follows the ordered rigid mesh batching and shared GPU skinning constants
changes. It targets steady gameplay in crowded scenes. Loading and map transition
texture uploads are unchanged.

## Pose and matrix reuse

* Models with an empty Granny control list reuse their sampled rest pose while
  their parent bone matrix is unchanged. The complete control list is checked,
  including blending controls. A final animated sample is never cached as a rest
  pose merely because its control was freed after sampling.
* Rigid batch candidates cache their mesh matrices and uniformity decision when
  the actor world matrix and the composite bone matrices are byte identical.
  Constant animation tracks can benefit without changing animation sampling or
  animation timing. A changed bone or world matrix rebuilds the original result.
* Model replacement and binding creation invalidate matrix reuse. Pose revisions
  remain unique across pooled instances and skeleton replacement.
* No distance animation throttling, particle reduction, visibility changes or
  changes to attachment bone queries are introduced.

## Particle rendering

Camera vectors and the normalized Y billboard basis are prepared once per
particle system render. Each particle prepares its orientation and attachment
position once; two and three face billboards reuse that preparation for their
existing face angles and order. Vertices are written directly into the ordered
batch, retaining the original UVs, color, stretch calculation and rotation math.
The existing Transform overloads remain available for the compatibility path.

Particle batches now use a 16,384 vertex dynamic buffer with NOOVERWRITE writes
and DISCARD on wrap, plus a static index buffer with the existing triangle order.
Buffers are released with the device's other transient buffers and recreated
lazily. Creation, locking, unlocking or drawing failures fall back to the previous
DrawIndexedPrimitiveUP path. Stream zero and the index binding have the same null
postconditions as the previous submission.

Simulation, particle counts, texture animation, texture boundaries, blending,
system order, mesh effects and lights retain their existing behavior. This is
CPU preparation reuse and buffer streaming; it does not move particle simulation
or billboard construction into a vertex shader.

## Diagnostics and comparison switches

`[MAP_PROFILE] START version=8` identifies this build's profiler schema.
`CROWD_REUSE` lines report:

* `rest_pose_reuse`: reuse with an empty animation control list.
* `mesh_matrices_reuse` / `mesh_matrices_builds`: rigid candidate matrix cache hits
  and rebuilds.
* `particle_stream_draws` / `particle_stream_fallbacks`: submissions through the
  dynamic buffer or the previous UP path.
* `particle_basis_reuse`: additional billboard faces using a prepared basis.

`[PARTICLE_STREAM] ACTIVE` is printed after the first successful streamed draw.
`FALLBACK` records a resource or draw failure once per device lifetime. The usual
effect detail counters still report the final draw result.

Optional process startup environment switches isolate these changes:

* `M2_CROWD_REUSE=0`: disable the new rest pose and rigid matrix reuse.
* `M2_PARTICLE_PREP=0`: use the previous particle geometry preparation.
* `M2_PARTICLE_STREAM=0`: use the previous UP particle submissions.

Version 9 disables all three by default after the version 8 crowd run showed
regressions and nearly zero cache hits in the busy metin scene. Set a switch to
`1` to explicitly opt back in. The earlier ordered model batching remains enabled.

Compare the same camera, entity population and
effects in the desert. Runtime FPS, visual correctness and frame time changes
require an in-game run; successful compilation alone does not establish them.
