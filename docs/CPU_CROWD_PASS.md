# CPU crowd pass — version 10

## Asset inspection and constant animation proof

The actual metinstone loop and attack assets contain 89 transform tracks, all
constant. The dog idle clip contains 38 tracks, including 28 animated rotation
tracks and 8 animated position tracks. The metin break animation has 88 animated
position and rotation tracks. The latter two are ineligible for constant reuse.

`ConstantMotion.h` is shared by the client and the inspector. Eligibility requires
one track group, no vector tracks, no periodic or translated loop, no accumulation
flags, and constant/identity position, orientation and scale/shear curves for every
track. This is evaluated at animation binding, not by scanning curves per frame.

The runtime also requires compatible control bindings, no model/track masks,
default control weights and at least full effective pose coverage. Overlapping
wait controls may reuse only if they bind the same constant animation. Motion,
parent, local-time reset or control mutations force fresh sampling. Changes to
the parent matrix also force sampling. Clocks, control completion and game-side
motion events continue to advance. Different clips, dynamic curves, partial
weights and masked controls retain the original sampling path.

A stable pose revision plus a byte-identical actor world matrix can reuse mesh
matrices and the existing rigid batching decision. This checks one world matrix;
it does not revive the unsuccessful per-frame bone snapshot scan from version 8.
Model and mesh binding replacement invalidate the matrix result.

`M2_CONSTANT_MOTION=0` disables both new reuse paths. `[CONSTANT_MOTION] ACTIVE`
requires an actual reuse. `ANIMATION_REUSE` reports `constant_pose_reuse` and
`revision_matrices_reuse` in profiled frames.

## Lightweight pacing and profiling overhead comparison

Normal startup now samples detailed capture once every 17 Process frames. The
older all-frame profiling is available with `M2_MAP_PROFILE=full` (or `1`).
`M2_MAP_PROFILE=light` (or `0`) disables detailed capture; a lightweight pacing
report remains active. `M2_MAP_PROFILE=compare` alternates full/light profiling
blocks every five seconds in the same executable with identical renderer options.
The comparison cannot determine profiler overhead unless scene/camera/population
remain comparable. It does not compare the animation optimization on/off.

`tools/run_crowd_profile.ps1 -Mode compare` launches the interactive game with this
setting and saves the resulting syserr and executable hash under bin/cpu_profiles
after the user exits. It refuses to start another client over an existing session.
It does not modify packs, game settings, simulation or rendering options.

`[FRAME_PACING]` reports actual foreground world Process/render counts, elapsed
time, rendered FPS, render-finish interval p50/p95/p99/max, interval threshold
counts, Process wall work and live actor count range. Intervals include frame
pacing sleep and intervening updates; Process work is measured before intentional
sleep and includes profiling overhead and any blocking render API calls. These
are not GPU execution times or display presentation measurements. Map changes and
focus interruptions end the current window and reset interval history. Reports
use fixed storage and sort at most 1,024 interval samples once per five seconds.

Use FRAME_PACING for FPS. In version 10, MAP_PROFILE WINDOW counts only profiled
frames; dividing those rendered counts by window duration is not actual FPS in
sampled mode. Stage timings/counters likewise describe captured frames. New
`avg_active_frame_ms` divides a stage's total by the frames where it ran.

## Character update breakdown

New sampled timing groups cover instance state, physics/rotation/combo, root
motion, ground queries, advancing collision points, attacks/movement, motion
events, combo/damage, manager collision/picking/deletion and actor transforms.
Actor transforms separately expose clock/LOD updates, transform processing,
world matrices, attachment/collision points, bounds and attributes. Operation
order, update frequency, collision rules, visibility and picking stay unchanged.

These groups run only on sampled detail frames. They identify the next expensive
operations without adding per-component clocks to every entity on every frame.
The detailed stage capacity is raised to 192 and reports include the update
groups. Existing GPU text, ordered particle batches and skinning stay enabled.

The asset inspector was compiled and used to read the assets. The Release client
must still be compared in-game; asset eligibility and compilation do not establish
an FPS improvement or validate all gameplay transitions.
