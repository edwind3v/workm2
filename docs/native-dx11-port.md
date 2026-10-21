# Native DX11 client port

## Parked experimental snapshot

This work is saved on `codex/dx11-experimental` for future development. It is
based on the pushed v3 checkpoint `27d4cc010d7cc8775aa7835d40790d2de46fa3fe`.
The user deferred further DX11 work after reporting invisible in-game models
and excessive darkening. The latest snapshot corrects the donor/object transform
accessor mismatch and SSAO matrix/normal errors, and both configurations and all
shaders compile; those last visual corrections have not been confirmed in-game.
Treat this branch as unfinished renderer work, not a validated release.

To resume, switch to this branch, read this guide, build the client and shaders,
and deploy the matching executable and root shaders together. `main` remains
at the v3 checkpoint. Do not reuse DX11 binaries with the v3 source checkout.

## What changed

The donor is `D3D11-Martysama`, revision `0cd724a`. The starting client checkpoint
is `27d4cc010d7cc8775aa7835d40790d2de46fa3fe`.

This build uses a native D3D11 device, context, swap chain and shaders throughout
the renderer. The earlier DX11 preview rendered into a DX9 texture; that bridge
is now excluded from the build and its application hooks have been removed.
Both Debug and Release use DX11. All projects retain **v143 / Win32**.

| Area | Implementation |
| --- | --- |
| Device and state | DXGI presentation, native buffers/textures, shader and pipeline caches, resize handling |
| Characters | GPU skinning, separate skinned/rigid passes, rigid-part instancing, native shadow passes |
| World | Native terrain, splats, water, static objects, dungeons, sky, trees and snow |
| Effects | Particle batching and compute billboard generation, native mesh effects, weapon/fly traces |
| Interface | Native images, minimap/atlas, guild marks, clipped/scaled UI and GDI font atlas uploads |
| Video | Media Foundation decoding with native DX11 frame uploads |
| Graphics options | MSAA, bloom, SSAO, god rays, celestial bodies and the donor fog treatment |

Existing native Owsap loading, motion/static caches, staged sector preparation,
archive read-ahead, shared motion libraries and cached name geometry remain.
The bounded pose pool and default mesh-binding reuse are retained. Miles audio
and Python 2.7 remain; this port does not enable Cython or replace gameplay code
with the donor's gameplay systems.

## Adaptations made to the donor

- Names and entity visibility retain their existing rules. Character/effect/shadow
  limits default to **zero / unlimited**. The donor's fixed dead-character cap is
  not applied.
- Distance-based update/deformation/animation skipping is disabled by default.
  Its optional paths preserve full updates for the main character and other PCs.
- Instancing splits groups at the 1,024-instance upload capacity. That capacity
  does not become a visibility limit. Material palettes are compared completely,
  and tinted/translucent characters keep individual rendering.
- Skinned and rigid draws are separated to avoid drawing mixed model parts twice.
  Tree batches retain individual LOD and alpha thresholds.
- Upload failures have individual rigid-mesh/tree fallbacks.
- The existing cached name geometry uses a native dynamic vertex ring and an
  immutable index buffer. Small-buffer/CPU fallbacks retain all eligible names.
- DDS parsing retains dimension, mip-layout and payload bounds checks. BC1/2/3
  blocks upload directly. Other supported images use bounded stb decoding, exact
  magenta keying and mip generation. The donor's broad green-key removal is not
  applied.
- Post-processing supports single-sample rendering and independent bloom/SSAO
  controls. The world is resolved and processed before names/interface/cursor
  rendering; presentation does not resolve over the finished interface.
- Map-authored fog enable/range settings are retained; the donor's global fog
  distance override is not used.
- Device removal is reported and closes the client for a restart. The port does
  not pretend a removed GPU device has been reset successfully.

## Build and install

1. Open `Metin2Client.sln` in Visual Studio 2022 with the **v143 C++ tools** and a
   Windows SDK installed. Select **Release / Win32** and build **UserInterface**
   with its project dependencies. Debug also builds the same native renderer.
2. The normal post-build command still copies the executable to the configured
   game directory. During development you can suppress it with MSBuild property
   `/p:PostBuildEventUseInBuild=false`.
3. The client also needs the HLSL files in `client_pack/root/shaders`. Close the
   game and run from the source directory:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy_dx11.ps1 -Deploy
   ```

   Use `-GameDirectory 'X:\Your Game Client'` or `-Pml 'X:\PackMakerLite.exe'` if
   needed. Omit `-Deploy` to prepare the package without changing the game files.

The deployment tool backs up the current executable and root archive pair,
copies the current unpacked root into a staging directory, merges the shaders,
packs it with PML, then extracts the result and checks every source file's SHA256.
When the existing AssetInspector is available, it also checks that original root
members and their compression policies are preserved. It refuses deployment while
the target game is running. Receipts and backups are under
`bin/dx11-deployment-<timestamp>/`.

The build requires no new third-party runtime DLLs beyond Windows components.
Keep the existing client DLLs, including `python27.dll`, `granny2.dll` and Miles
dependencies. `D3DCOMPILER_47.dll` and Media Foundation must be available on the
target Windows installation. stb is compiled in; its pinned source and license
are under `third_party/stb`.

To compile the shader sources separately with the Windows SDK:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_dx11_shaders.ps1
```

The `.cso` files are build artifacts. This client loads the packaged `.hlsl`
sources, so deploy the HLSL files as well as the executable.

## Quality settings and slower PCs

The native renderer requires **GPU feature level 11.0 / Shader Model 5.0**.
Installing a newer DirectX runtime cannot add that hardware capability. It does
not silently fall back to CPU rendering if hardware creation fails. The donor's
explicit `d3d11_warp.txt` diagnostic is retained for intentional software runs.

Normal defaults are the donor's 4x MSAA, bloom and SSAO. Unsupported MSAA sample
counts fall back, and optional render-target allocation failures disable the
affected effect. The existing `NO_ANTIALIASING` preference is honored.

These additional keys persist in `metin2.cfg`. For a lower GPU cost, close the
game and set:

```text
DX11_MSAA_SAMPLES 1
DX11_BLOOM 0
DX11_SSAO 0
DX11_GODRAYS 0
```

MSAA accepts 1, 2, 4 or 8 and takes effect after restarting. Bloom, SSAO and god rays
also have native Python APIs: `app.SetBloomEnabled`, `app.SetSSAOEnabled`,
`app.SetGodRaysEnabled`; `app.SetBloomParams` exposes threshold/intensity.
The existing shadow-quality settings remain available. These settings do not hide
names, players, monsters or NPCs.

Optional entity policies are in `UserInterface/Locale_inc.h`:

- `CHAR_RENDER_LIMIT_DEFAULT`, `EFFECT_LIMIT_DEFAULT`,
  `SHADOW_RENDER_LIMIT_DEFAULT`: zero means unlimited.
- `ENABLE_UPDATE_CULLING`, `ENABLE_DEFORM_CULLING`, `ENABLE_ANIMATION_LOD`:
  commented out by default. Their distances/cadences are declared beside them.
- Runtime APIs `app.SetCharacterRenderLimit` and `app.SetEffectLimit` are exposed
  for deliberate opt-in use. No root script turns them on.

## Startup correction

The donor's VTF batch state used a namespace-scope `thread_local` vector whose
constructor reserved storage. The reported startup crash occurred inside that
constructor, before graphics initialization. Batch state now belongs to the VTF
manager and uses a fixed 1,024-entry array, matching the existing GPU upload
capacity. Constructing and appending batch data no longer allocate memory.
Rendering uses the manager's single immediate context; no worker shares this
batch state. Larger groups still split into batches without hiding entities.

The native renderer also requires one live `DX::StepTimer`. The application owns
it immediately after the existing `CTimer`, before the world and effect managers,
so camera initialization and later sky/water/effect calls can access it safely.
It is ticked once per application update; its separate frame limiter is disabled.
The existing `CTimer` continues to drive gameplay and application frame pacing.
This corrects the reported `CSingleton<DX::StepTimer>::instance()` assertion in
`CGraphicBase::SetPositionCamera()` during startup script execution.

## Rendering and exit corrections

- The donor's `CGraphicObjectInstance::GetMatrix()` API is provided alongside
  the existing `GetTransform()`. Both return the instance's world matrix. Without
  this accessor, donor bounding/collision code selected the inherited global
  matrix-stack accessor, leaving in-game bounds near the origin and rejecting
  actors during frustum culling. The original visibility rules stay enabled.
- SSAO projection and inverse projection use the same transposed upload convention
  as other HLSL matrices. Reconstructed normals face the camera in RH view space;
  sample radius/bias use the game's world units. Invalid/out-of-view samples are
  neutral, and contact shading cannot multiply the whole scene down to black.
- Buffer/texture helpers route their bindings through the native shader cache.
  The character pass explicitly restores camera constants and invalidates cached
  native bindings after terrain/trees. Missing diffuse maps use white fallback
  rather than keeping a permanently transparent startup fallback.
- Native vertex buffers retain their declared layout after allocation. GR2 skin
  data is converted when an exporter uses a different vertex format; unused bone
  influences do not index the mesh binding table. Pose uploads stay within the
  actual skeleton. GPU system inputs are excluded from vertex-buffer layouts.
- Fonts retain GDI grayscale coverage in a BGRA8 atlas. DX9 half-pixel offsets
  are removed and glyph origins are snapped to pixels. Text is drawn after world
  bloom/SSAO/god rays, on the single-sample output.
- Sun/moon disks use alpha blending and circular edge coverage. God rays sample
  a bounded region near the sun, normalize accumulated weights and fade at the
  screen edge, avoiding the donor's full-scene brightness accumulation.
- `EndThreadLoading` joins its worker without destroying shared engine resources.
  Existing application teardown releases owners, then `CPythonResource` clears
  deleting resources and the remaining resource map before device shutdown.
- `DX11_ACTORS` reports actor count and submitted draws every five seconds;
  `DX11_MESH` includes a small initial sample of model/shader/texture bindings.
  These diagnose draw submission; they do not prove pixels rendered correctly.

## In-game checks

Compilation and archive integrity do not establish visual correctness or frame
times on a particular PC. Check the following with the deployed Release client:

1. Login, selection/creation, entering a map, teleporting, relogging and changing
   character gender. Check that the loading overlay hands off cleanly.
2. Walk and run through Cape, desert, towns, a dungeon and a wooded map. Inspect
   terrain seams, objects, water, fog, sky and shadows.
3. Meet new players and spawn dense monster/metin groups. Keep names enabled.
   Check body/weapon/hair/costume parts, mounts, effects and guild marks.
4. Check inventory/tooltips/chat, clipped windows, minimap/atlas and any videos.
5. Alt-tab, minimize/restore, resize and compare the lower-cost graphics settings.

Share `syserr.txt` after those checks. The existing map/text profiling remains
available for comparing CPU frame stalls. A `DX11_READY` line identifies the
feature level, actual MSAA count, instancing availability and compute-particle
availability after device initialization. DX11 alone does not eliminate archive
I/O, cold asset construction or server visibility timing costs.

## Restore the previous client

Close the game. From the deployment's `original` directory, copy
`metin2client.exe` back to the game directory and copy **both** `root.eix` and
`root.epk` back to `pack`. If there was an older loose `root/shaders` folder, its
backup is alongside the executable. Source changes remain uncommitted until a
separate commit is requested.

## Reference

[Microsoft's feature-level 11.0 format support table](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/format-support-for-direct3d-11-0-feature-level-hardware)
documents the native format requirements used by this backend, including BGRA
vertex colors.
