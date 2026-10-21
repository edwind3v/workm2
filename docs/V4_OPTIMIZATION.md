# V4 optimization checkpoint

This checkpoint captures all client optimization work through profiler version
11, including the user's successful crowded-metin run and the minimap FPS label.

## Included

- DX9 GPU skinning with per-mesh palettes up to 72 bones, original lighting and
  material behavior, CPU fallback and palette packing caches.
- Ordered rigid mesh batching and constant metin pose/mesh-matrix reuse.
- GPU text instancing with original outlines, texture coordinates, painter order
  and an expanded-vertex compatibility fallback.
- Ordered multi-texture particle batching, stable particle arrays and compact
  particle curve updates with legacy timing and random-number ordering.
- Character update diagnostics, cheaper collision-center transforms and avoided
  LOD calculations for models with no alternatives.
- Sampled profiling, foreground frame pacing and rendered FPS below the minimap
  server name.
- Source, pack overlay, shader sources, build/package helpers and detailed notes.

The last deployed Release executable and complete current root archive pair are
in `releases/v4-optimization.zip`, alongside their SHA256 manifest and the last two
captured profiling logs. The root archive contains the FPS label script; no other
archive member was changed by its deployment. Other game asset archives remain
at the v3 asset optimization baseline documented in the repository.

## Observed result and remaining work

The user reported smooth metins at the approximately 61 FPS cap. At comparable
10,600 particle quads per render, sampled particle update CPU time fell from
approximately 3.95 ms to 1.52 ms between profiler versions 10 and 11. This is a
scene comparison, not a controlled benchmark or an unlimited-crowd guarantee.
The 3,000-dog run remained slow (18.8 FPS in one steady window), dominated by
character updates, animated pose evaluation and individual model draws.

The release compiled with zero errors and existing warnings. The FPS script
compiled with Python 2.7. Subsequent shared-animation/mob-render work begins after
this checkpoint and must preserve motion timing, combat, collision and attached
equipment effects. Map-change texture uploading remains outside the scope.
