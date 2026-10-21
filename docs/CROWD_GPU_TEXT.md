# Crowd rendering, profiler version 9

## Defaults after the version 8 comparison

The recorded version 8 metin run was slower than version 7 at a comparable
particle population. Exact matrix reuse had almost no hits, and the particle
stream path increased CPU submission time. Version 9 leaves `M2_CROWD_REUSE`,
`M2_PARTICLE_PREP`, and `M2_PARTICLE_STREAM` disabled unless explicitly set to `1`.
Ordered model/rigid batching and the shared skinning constants remain enabled.

## Instanced world text

The existing cached font layout feeds a 40 byte glyph instance: screen-space
bounds, depth, color and atlas UV bounds. A static four-corner quad and a dynamic
instance stream replace four CPU-expanded 24 byte vertices per glyph quad. This
reduces vertex upload data by about 58%; it is not an FPS prediction.

The vertex shader selects the original bounds and UV endpoints, and applies
world, view and projection separately. A matching pixel shader reproduces the
font texture/color modulation. Both shaders use shader model 3, matching DX9
hardware instancing requirements. Outlines retain all four offset quads and the
existing painter order; only adjacent runs sharing a font texture are merged.
There is no font atlas repacking, change to label selection, layout, picking,
distance policy, glyph rasterization or font filtering.

Clip rectangles, emoji, hyperlinks and cursor labels retain their existing
renderer at flush barriers. Unsupported hardware/state and buffer failures use
the previous expanded path. After a failed instanced draw, only the failed run
and remaining runs are replayed. Successful alpha draws are never repeated.
Stream offsets, both instance frequencies, shaders, vertex format/declaration,
stream bindings and index binding are restored. Transient buffers are released
and recreated with the existing world text device lifecycle.

`METIN2_TEXT_RENDERER=cached` selects the previous cached expanded renderer.
`METIN2_TEXT_RENDERER=tutorial` retains its older compatibility option.
`METIN2_TEXT_PROFILE=1` still enables the separate detailed text report and
nonblocking GPU timer queries. The ordinary map log now includes text stage CPU
times without requiring this setting.

## Character submission

Packed bone palette columns are cached per instance and mesh at the exact pose
revision and binding. Shadow and main draws can use the same packed data. Bone
matrices, influences, animation sampling, animation timing, LOD and attachment
queries are unchanged. Bind/model replacement clears the instance caches.

Unlit passes with no normal/reflection coordinate generation skip the unused
normal inverse transpose. Lit passes and generated normal/reflection coordinates
retain the existing calculation. GPU constant upload comparisons still run, so
another actor or shader cannot invalidate the palette on the device silently.

## Ordered particle batches across textures

Compatible modulated particle runs can now share a draw with up to four textures.
Each vertex carries its texture slot; matching vertex and pixel shaders select
that texture without sorting quads or changing their order. Pending runs preserve
the original blend settings and flush when blending changes, a fifth texture is
needed, capacity is reached, or an unsupported state is encountered.

The effect manager brackets its particle queue; standalone effect renders have
their own bracket. Mesh effects are explicit flush barriers. This keeps armour,
weapon and other mesh effects in their existing positions relative to particles.
Fogged passes, custom shaders, transformed/generated UVs, extra texture stages,
non-modulated operations and unsupported hardware retain the previous path.
The effective source sampler settings, including addressing and sRGB reads, are
copied to the additional texture slots and restored afterward. Particle counts,
simulation, culling, per-particle colors, UVs and attachment transforms are kept.

The batch uses the existing UP submission approach; the version 8 dynamic
particle streaming path remains off. A failed combined draw replays its original
texture runs and disables further combinations for that device lifetime.
`M2_PARTICLE_MULTI_TEXTURE=0` disables this new combination path.

Deferred submissions can occur after a per-asset detail scope closes. Use the
global batch counters and `effects.particle_multi_submit` to measure submission;
per-asset detail timings continue to measure CPU preparation at the original
call sites.

## Log fields

* `[GPU_TEXT] READY` reports resource creation; `ACTIVE` requires a successful
  instanced draw. `FALLBACK` reports upload/draw/resource failures.
* `CROWD_GPU`: `skin_palette_reuse`, `skin_unlit_matrices`,
  `text_instance_quads`, `text_instance_draws`, `text_expanded_draws`,
  `text_upload_bytes`.
* `[PARTICLE_MULTI] ACTIVE` requires a successful combined draw.
* `EFFECT_BATCH`: `multi_draws`, `particle_draws_saved`, `multi_fallbacks`.
* Stages: `text.distance`, `text.selection`, `text.projection`,
  `text.arrangement`, `text.render`, `text.submit`, and
  `effects.particle_multi_submit`. Timings are CPU wall time.

Compilation checks C++ and all four embedded shaders. FPS, frame pacing and
visual behavior require the user's in-game comparison with a comparable camera,
entity population and effect workload.
