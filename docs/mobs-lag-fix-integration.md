# World-name performance integration

## Scope and visibility

The base `ANTI_MOB_LAGS_FIX` and its follow-up device corrections remain under
one switch: `ENABLE_FIX_MOBS_LAG` in `UserInterface/Locale_inc.h`.
The world-label path now implements the subsequent performance plan, steps 1-5.

Name visibility keeps the existing rules: the same 3500-unit show-all radius,
owner visibility and pickability checks, guild-wall exclusions, hovered/targeted
exceptions, and item/chat behavior. There is no name-count cap, automatic name
reduction, changed name-distance setting, or new player-facing quality option.
Wholly offscreen glyph rectangles can skip submission, including their outline
margin; selection, layout, overlap arrangement and picking still use the full
eligible lists. No entity is hidden by this rendering optimization.

## Implementation

1. **Profiling:** opt-in fixed-storage frame timing, CPU stage timing, eligible
   counts, submitted labels, offscreen skips, cache builds/hits, tracked container
   capacity growth, transient tutorial map nodes, draws, locks, upload bytes,
   fallback/failure counters and exact item overlap checks. GPU timestamp queries
   are sampled asynchronously when supported. No query flush/wait is performed.
   Summaries append to `text_performance.txt` approximately every five seconds.
2. **Visible collections:** contiguous vectors retain capacity between frames.
   Per-tail generation markers replace linear duplicate searches. The show-all
   traversal passes the existing tail pointer directly. Generation wrap, pooled
   tail reuse and deletion are handled explicitly.
3. **Cached layouts:** unchanged world text retains local glyph rectangles,
   atlas references and UVs. Identical text/font/layout configurations share a
   geometry object. Position and color changes reuse that object. Layout/font/
   text changes invalidate it. The shared cache index is limited to 1024 entries
   and an estimated 4 MiB; active labels retain their referenced layouts.
   Map clear and system shutdown release references. Font revisions protect
   cached glyph references across font recreation.
4. **Cross-label streaming:** adjacent compatible text runs share indexed quad
   submissions, preserving their original painter order. Four vertices per quad
   use reusable 16-bit indices. A dynamic buffer appends with `NOOVERWRITE` and
   wraps with `DISCARD`, using up to 4096 quads per chunk. Capacity overflow
   flushes another chunk without dropping labels. Device allocation can fall
   back to smaller buffers, then indexed CPU submission. Guild marks, item boxes,
   cursor/IME, emoji, hyperlinks and explicitly clipped text are order barriers.
   Special text keeps its existing renderer.
5. **Early work reduction:** threshold comparisons use squared distances with
   the existing numeric radii. Shared name dimensions/layouts avoid repeated
   geometry work. Item-name overlap uses reusable 64-pixel screen buckets and
   the original exact rectangle test. It selects the earliest matching label in
   the existing list and preserves the 20-adjustment limit and placement rule.
   Oversized rectangles or bucket exhaustion fall back to exact candidate checks.

The spatial grid bounds node storage at 65536 entries. The text stream uses
fixed CPU storage and a small GPU working buffer; it does not create one texture
per label. Cache misses and new population high-water marks can allocate memory.
The ordinary warmed world-name submission path avoids per-label temporary maps
and vectors. The counters describe these managed allocations, not every heap
allocation performed by the entire game or graphics driver.

## DX9Ex lifecycle

`CGraphicDevice` owns creation/destruction of the text stream alongside PDT
buffers. Recovery, browser-mode changes and resize paths release/unbind buffers,
restore the state cache, recreate buffers and retry deferred resets. Character
shadow and existing DX11 diagnostic preview targets follow the application
recovery path. The renderer requires the existing DX9Ex capabilities; it does
not require DX11, new shaders, instancing or render-to-texture name caches.

## Controlled comparison

The default renderer is `cached`. To record a run, execute the helper from this
source directory against the deployed executable, for example:

```powershell
.\tools\profile_world_names.ps1 -Client 'E:\#111 METIN2 GRAPHICS MARTY 5.9\#MartySama 5.9 DX9Ex\Game Client\metin2client.exe' -Renderer cached -Scenario '200-monsters-warm' -EntityCount 200
```

After exiting that client, run the same command with `-Renderer tutorial` and
repeat the same scene. The helper sets environment variables only for its child
process. `EntityCount` records metadata; it does not spawn entities. It records
an executable SHA256 and session details beside the deployed client. Launching
the executable normally leaves profiling disabled and uses the cached renderer.

The tutorial comparison uses the previous allocation-heavy renderer. Both modes
use the improved visible collections and item layout, so this isolates renderer
cost. A whole-system baseline requires the prior executable with the same scene
and build configuration. Existing visibility controls remain under game control.

Suggested captures: 50/200/500 monsters and metins, cold spawning and warmed
scenes, stationary and moving cameras, combat, overlapping dropped items, mixed
font pages/languages and levels/titles/guild marks. Keep resolution, camera,
visibility settings and entity count the same between renderer comparisons.

Interpretation:

- `frame_ms_p50/p95/p99/max` measures intervals between world-label frames,
  including frame pacing and stalls. It is not GPU-only time.
- Stage timings are average CPU wall milliseconds per captured frame.
- `gpu_pass_ms_p95` is delayed GPU timeline elapsed time for the text-tail pass;
  it can include command-submission gaps, item boxes and marks. `gpu_samples=0`
  means unsupported or not yet available, not zero GPU cost.
- Counts are totals across the reported `frames`; divide by that count for a
  per-frame comparison. Compare eligible-name counts before comparing timings.
- `capacity_growths` and `cache_builds` should settle after the scene warms.
  `transient_map_nodes` should be zero for ordinary cached world labels.
  Special-text fallback can still use tutorial containers.
- `cache_index_bytes` estimates shared-index retention, excluding active label
  ownership, generic UI text, the spatial grid and stream buffers.
- Profiling adds overhead and writes periodic summaries. Also assess normal
  launches with profiling disabled. No FPS guarantee follows from compilation.

## Build and deployment

Build `Metin2Client.sln` for **Win32** using **v143**. Release output is
`bin/Release/metin2client.exe`; Debug output is `bin/Debug/metin2client.exe`.
Use Release for performance comparisons. Deploy the executable with the same
existing packs; this update requires no Python pack changes.

The project contains an existing post-build copy to its configured debugger
working directory. Check which configuration was built last before comparing
clients. Do not use a Debug executable as a Release performance baseline.

Compilation and runtime results are recorded separately in the task report.
In-game FPS, minimum-spec behavior and visual parity require live captures;
no scene measurements are inferred from the source changes.
