# DX9Ex GPU skinning

## Crowd setup optimization

Shared fog, lighting, material and texture-coordinate constants are cached by
the state manager's vertex-processing revision plus both texture-transform
flags. View/projection/texture matrices, material values, active lights and all
render states consumed by the shader builder invalidate the cache when changed.
Custom shaders still use the CPU fallback. Each draw retains its own original
world/view/projection multiplication order, inverse-transpose normals and full
bone palette; animation updates and vertex calculations are unchanged.

Registers 0–10 (per-draw matrices) upload separately from 11–35 (shared state).
The state manager compares/copies constant ranges in bulk and avoids identical
material API calls. Compiler literal registers remain reserved. Device resets
and failed state restoration invalidate the caches.

`DRAW_OPT` logs `skin_common_builds` and `skin_common_reuse` for comparison with
`actor.gpu_skin_submit` CPU time. Reuse is not itself a measured FPS gain.

GPU skinning is enabled by default on hardware with vertex shader 3.0,
256 vertex constant registers, and packed byte vertex declarations. No loose
shader or pack changes are needed. The shader is compiled from embedded HLSL
once per device. Set `M2_GPU_SKINNING=0` before launching for the CPU comparison.

The original weighted vertices are converted once when a Granny mesh loads,
then uploaded to a shared, static GPU buffer on first use. Each instance sends
its current mesh-to-skeleton bone palette to the vertex shader. Granny still
evaluates animation poses on the CPU for attachments, collisions, and gameplay.
Supported instances skip CPU vertex deformation and dynamic vertex uploads.

Each mesh supports up to 72 palette bones and four influences per vertex.
Weights are preserved as normalized bytes; extra influences are never silently
discarded. Linked equipment uses the instance's own Granny mesh binding.
Rigid meshes retain their existing render path.

The vertex shader supplies diffuse lighting, vertex fog, texture transforms,
camera-space reflection coordinates, and projected texture coordinates to the
existing fixed-function pixel/material pipeline. It supports up to four active
directional/point lights. Unsupported hardware, mesh formats, invalid bindings,
spotlights, more than four lights, point lights with ambient illumination,
vertex specular lighting, custom shaders, user clip planes, or additional
texture stages fall back to the CPU path. If any deformable mesh in a model
cannot use GPU skinning, that instance uses CPU skinning. A pass that needs the
CPU path generates the current CPU vertices on demand and reuses them where
the existing pose/buffer cache permits it.

DX11 model previews require CPU-deformed vertices, so enabling
`M2_DX11_MODEL_PREVIEW` temporarily keeps skinning on the CPU. Disable the
preview for GPU skinning comparisons.

## In-game comparison

1. Launch the newly built Release client normally. GPU skinning is on by default.
2. Enter a crowded map. Check `syserr.txt` for `GPU_SKINNING_READY` followed by
   `GPU_SKINNING_ACTIVE`. READY means setup succeeded; ACTIVE confirms an
   actual indexed character draw succeeded. `GPU_SKINNING_FALLBACK reason=...`
   explains each distinct fallback condition once.
3. Check players, monsters, mounts, hair, armor/costumes, weapon attachments,
   animations, transparency, lighting, and both shadow settings. Repeat after
   resizing, minimizing/restoring, warping, and changing equipment.
4. Exit, then launch with `M2_GPU_SKINNING=0` and compare the same camera,
   actor count, animation, and graphics settings in the same map. Restarting
   is required because the environment setting is read at process startup.

`tools/run_skinning_client.ps1 -Mode gpu` and `-Mode cpu` launch the configured
Game Client folder with the selected mode and the DX11 preview disabled.
The script accepts `-ClientDirectory` for another test installation.
It waits for the client to exit, then saves a separate log and metadata file
under `bin/skinning_profiles` for each mode. Use `-Scenario` to label a repeatable
camera/map test. The same scene, camera, character count, effects, and graphics
settings are needed before an overall improvement can be claimed.

Character selection uses temporary spotlight/point lights. They are now routed
through the shared lighting state and restored when returning to interface
rendering. Spotlight preview passes use CPU skinning; their lights do not carry
over into world GPU draws.

The existing map profiler adds `gpu_skin_draws`, `gpu_skin_vertices`, and
`cpu_skin_fallbacks` to its crowd reports. The existing `actor.vertex_skin`,
`actor.vertex_lock`, and `actor.vertex_unlock` stages show remaining CPU work;
`actor.gpu_skin_submit` measures GPU-path **CPU submission time**, not GPU time.
`gpu_skin_vertices` counts vertices scheduled on the GPU during deformation;
`gpu_skin_draws` counts successful draws. A later pass can still require CPU
fallback, so use the fallback counters when interpreting them.

## Automated offscreen check

Run `tools/check_gpu_skinning.cmd` with Visual Studio 2022 and the project's
Extern libraries installed. The check opens a hidden DX9Ex device, compiles
the production shader, and compares its rendered pixels with Granny's actual
CPU-deformed vertices through the original fixed-function pipeline.

The fixtures exercise two/four-weight source conversion, the highest palette
index (71), remapped skeleton indices, rotated bones, nonuniform world scale,
unlit and directional/point lighting, linear/exponential/squared exponential
fog, reflection mapping, projected texture coordinates, transformed UVs, and
normal normalization on/off. These synthetic checks do not establish visual
parity for every game asset or an FPS improvement in a live game scene.
