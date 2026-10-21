# Experimental crowd rendering optimization

This extends the v1 movement optimization and diagnostic format v3. It implements
the five authorized rendering/instance/effect changes. No GPU skinning, entity
limits, visibility changes, animation-rate reductions or new runtime dependency
are introduced. Compilation does not establish in-game visual correctness or a
particular crowd size/FPS on low-spec hardware.

## Animated vertex buffers

- Combined WRITEONLY/DYNAMIC usage is detected with bit tests. It no longer
  selects a READONLY lock while animation code writes vertices.
- Animation deformation has an explicit full-content replacement path using
  DISCARD for dynamic buffers. General/partial buffer locks keep their existing
  semantics. The vertex stride is stored at creation rather than recalculated
  for every lock.
- Successful locks and device destruction advance a buffer-content version.
  This invalidates cached skinning after another LOD writes the shared buffer
  or its device objects are recreated.
- Growing an LOD controller's shared buffer rebinds its existing model variants
  before returning the previous buffer to the pool. Allocation failure retains
  existing variants' buffers; a larger new model can use its local-buffer path.

## Animation reuse

Pose input revisions change on the existing Granny clock updates, motion
changes/copies, forced motion endings, time resets and attachment changes.
An unchanged input revision and unchanged attachment matrix reuse the actor's
sampled pose. Each newly sampled pose receives a globally unique revision,
including after instance pooling or LOD replacement.

Linked parts use the current skeleton owner's pose revision. Skinned vertices
are reused only when that revision, buffer identity and buffer-content version
all match. Mesh world matrices are still updated, so actor movement is applied
even when the sampled animation pose is reused. Gameplay/collision/event updates
and the existing animation clocks/rates are unchanged.

This primarily saves repeated evaluations/uploads for unchanged samples and
repeated passes. An animation advancing every frame still needs new skinning.

## Draw submission

Adjacent triangle groups with the same material keep that material active until
it changes or the mesh-list pass ends. This avoids repeated material save/restore
work without sorting primitives or changing transparent/fading actor order.
Equivalent specular settings return early. Two-texture passes reuse adjacent
material texture lookups. Existing actor sorting and state-manager caching stay
in place; this is not hardware instancing of whole monsters.

## Instance preparation and bounded reuse

- Self-skeleton mesh bindings reuse the binding already owned by the loaded
  mesh. The instance's model reference keeps it alive. Different target
  skeletons retain independently owned bindings and the original destruction
  path; no cross-skeleton remapping is substituted.
- World-pose storage is reused only for matching bone counts and is fully
  sampled before normal first use. The idle pool holds at most 128 poses and
  8 MiB. Under memory pressure it retains at most 32 poses and 2 MiB. Pose size
  comes from the Granny SDK. The pool is released at model-instance shutdown.
- The existing idle deformation-buffer pool is bounded to 8 MiB, or 2 MiB
  under memory pressure, and trimmed on buffer allocation/return. Initial
  reserves are smaller on constrained machines. Live actors' required buffers
  are not capped by this idle-cache limit.

These changes make repeated instance creation cheaper. They do not remove the
synchronous decoding cost of a completely unfamiliar race's files.

## Effects and particles

- Texture-frame migration uses list splice instead of allocating/freeing a
  list node for each migrated particle. Lifetime, emission and update order
  stay the same; emission/death can still allocate/free list nodes.
- Compatible particle quads are submitted as indexed triangle batches of at
  most 128 quads. Fixed scratch arrays and a reusable index array avoid a new
  per-frame heap allocation. Particle, face and texture-frame order are
  preserved, including attached and two-/three-face billboards.
- Each quad carries its original texture-factor color as a constant vertex
  diffuse color. Color/alpha arguments, lighting and FVF are restored afterward.
  Custom states that do not match the supported single-texture particle path
  use the original per-quad drawing path. No particle-count reduction is made.
- The effect manager retains its sorted list while membership is unchanged.
  Insertion/removal invalidates the list before pointers can be reused. The
  original comparator and rendering order are retained. Scratch storage is
  released with all effect instances.

## Diagnostics and user capture

`START version=4 movement_streaming=1 crowd_rendering=1` identifies this build.
The 16.7 ms threshold and bounded v3 stage reporting remain. Added stages cover
pose evaluation, mesh matrices/bindings, vertex-buffer allocation, lock,
CPU skinning, unlock, draw submission, particle updates/submission, effect
mesh/light updates and effect sorting.

`CROWD` and `CROWD_SUMMARY` report pose samples/reuse, skin uploads/reuse,
skinned vertices, binding/pose/buffer-pool reuse, material applications, actor
draw calls, particle quads/batches/node migrations and effect-sort rebuilds.
Counters include attempts and applicable non-character model/effect paths;
they are not counts of unique monsters or GPU execution-time measurements.

For feedback, restart the Release client, repeat the desert route, revisit
the same monsters, and observe a crowded area with names/effects enabled.
Include moving, combat, deaths, mounts, costumes and teleporting in the capture
when practical. Exit normally and preserve syserr.txt before restarting.

Build Release/Win32 or Debug/Win32 using v143. No root repack is required.
