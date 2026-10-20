# Graphics migration notes

This branch introduces a Direct3D 11 backend alongside the existing Direct3D
9Ex renderer. The game world is **still drawn by Direct3D 9Ex**. Migration is
incremental because the current engine calls the D3D9 device directly from
graphics, terrain, actors, effects, trees, fonts, and UI code.

## Implemented in the first step

- `EterLib/DX11Device.h/.cpp`: hardware device initialization, a two-buffer
  flip-model swap chain, color/depth targets, resizing, clearing, and presenting.
- `EterLib/DX11ShaderLibrary.h/.cpp`: loading DXBC shaders from files or memory,
  creating each of the six D3D11 shader stages, retaining vertex-shader bytecode
  for input-layout creation, and binding a shader to its stage.
- Both source units compile as part of the Win32 `eterlib` Visual Studio project.

These classes do not intercept, translate, or replace existing D3D9 calls.
No game-specific shader binaries or client assets are checked into this branch.

## Second checkpoint: live, opt-in DX11 draw test

- `DX11Device` now identifies the actual DXGI adapter and its output(s),
  preferring the adapter driving the game window when available.
- `DX11Device::CreateOffscreen` supports a color/depth target without creating
  a second swap chain on the game's D3D9-owned window.
- At the end of a successful **windowed** D3D9 device initialization,
  `EterLib/GrpDevice.cpp` can invoke `DX11Probe` when explicitly requested.
- `DX11Probe` compiles an independent `vs_5_0`/`ps_5_0` program, creates real
  shaders via `DX11ShaderLibrary`, uploads a test texture to an SRV, binds a
  sampler, draws a textured full-screen triangle on the GPU, reads its center
  pixel back, checks the result, and optionally
  writes `dx11_probe.bmp` in the game's working directory.
- The `DX11_DEVICE` diagnostic includes the real adapter name, LUID, VRAM,
  adapter/output indices, and D3D feature level. `DX11_TEST_DRAW` confirms
  the shader draw/readback; `DX11_TEST_FAILED` reports the failing stage.
- Probe failure does **not** stop the original DX9 client from launching.

### How to run this checkpoint on Windows

1. Build the modified solution as `Win32` and copy the resulting client files
   into a separate test installation. The project may need to be retargeted
   from its original toolset to the one installed on your PC (VS2022: `v143`).
2. In Visual Studio open the **UserInterface** project properties, go to
   **Configuration Properties > Debugging > Environment**, and enter
   `M2_DX11_TEST=1`. Alternatively, start the client from a command prompt
   after running `set M2_DX11_TEST=1` in that prompt.
3. Launch in **windowed mode**. Inspect `syserr.txt` (or the client's stderr)
   for `DX11_DEVICE` and `DX11_TEST_DRAW`. Open `dx11_probe.bmp` beside the
   game executable, or in the current working directory configured in VS.
4. Remove the environment variable to return to entirely unchanged DX9-only
   startup. The test does not alter `metin2.cfg`.

Expected successful output (values are illustrative):

```text
DX11_DEVICE adapter=<GPU> luid=<LUID> vram_mb=<MB> render_index=<N> render_outputs=<N> output_index=<N> feature_level=0xb000
DX11_TEST_DRAW mode=offscreen center_rgba=128,127,64,255 image_saved=1 game_renderer=DX9Ex
```

This validates one genuine Direct3D 11 vertex/pixel draw inside the **game
process**, but it is **not yet a DX11 game renderer** and it is not expected
to increase FPS. Shader programs from another binary, including recovered
private SM5 bytecode, are intentionally not bundled with the public source.

## Third checkpoint: reusable DX11 geometry and texture resources

- `EterLib/DX11Resources.h/.cpp` implements D3D11 vertex, index, and constant
  buffers (including dynamic uploads), RGBA8 textures/SRVs with optional
  dynamic uploads, vertex input layouts built against the actual compiled
  vertex-shader signature, and a baseline opaque/two-sided state set.
- With `M2_DX11_TEST=1`, the original test still runs, then an additional
  **indexed mesh** test runs on a separate offscreen target. It creates four
  vertices and six indices, dynamically uploads a solid-color texture, updates
  an object constant buffer, and uses `DrawIndexed` through an explicit layout.
- Successful `DX11_MESH_DRAW` reports `vertices=4 indices=6`,
  `center_rgba=32,160,224,255`, and the result of saving
  `dx11_mesh_probe.bmp`. The BMP should show a colored rectangle on black.
  Failure reports `DX11_MESH_FAILED stage=<stage> hr=<HRESULT>`.
- These resources are deliberately independent from the D3D9 resource manager;
  the mesh is **test geometry**, not an existing Metin2 model. This proves
  the new buffer/input-layout/pipeline path works before it is connected to
  actual game meshes, skeletal transforms, or material data.
- Both DX11 tests remain opt-in, diagnostic, and safe to fail without stopping
  the playable DX9Ex client. No private game-specific shaders or assets added.

## Fourth checkpoint: one real game texture from the pack manager

- With `M2_DX11_ASSET_PATH` set to the virtual path of a **real texture** in
  the running client's packs, `CPythonApplication::Process` performs one
  additional test after game initialization. For example, use a known TGA,
  DDS or PNG path from your own client's game data; paths inside a pack do
  not have to exist as loose files.
- `DX11AssetProbe.cpp` retrieves the resource through `CEterPackManager`,
  temporarily decodes it to CPU-side RGBA8 using the existing D3DX9 loader,
  uploads an immutable D3D11 texture, and draws an aspect-correct indexed
  quad on a separate 256x256 render target. The result is read back as
  `dx11_asset_probe.bmp` in the current working directory.
- Successful log: `DX11_ASSET_DRAW path="..." source=<W>x<H>
  center_rgba=<R>,<G>,<B>,<A> image_saved=1 game_renderer=DX9Ex`.
  On failure, `DX11_ASSET_FAILED path="..." stage=<stage> hr=<HRESULT>`
  distinguishes a missing packed file, unsupported image and DX11 failures.
- To run both DX11 tests and the real-asset test in Visual Studio, set the
  **Debugging > Environment** property to two separate lines:

  ```text
  M2_DX11_TEST=1
  M2_DX11_ASSET_PATH=<actual in-pack image path>
  ```

- This is an asset-upload integration checkpoint, **not** proof of model,
  texture-atlas, or material parity with the reference renderer. The D3DX9
  decode bridge is only temporary; it is not part of the final DX11 frame
  renderer and does not run during normal play.

## Fifth checkpoint: show one DX11-produced asset in the actual game window

- On **Windows / windowed D3D9Ex**, the optional `M2_DX11_SHARED_PREVIEW=1`
  setting attempts a Direct3D 9Ex / Direct3D 11 shared GPU texture. The image
  is still loaded from `M2_DX11_ASSET_PATH` through the existing pack manager.
- The D3D9Ex device creates a 256x256 shared render-target texture; DX11 opens
  it and renders the packed icon into it using the same indexed mesh and
  shader pipeline as the asset probe. After a DX11 flush, the D3D9Ex device
  reads its center pixel, verifying cross-API access.
- The original D3D9Ex UI render pass can then display the DX11-produced
  shared texture as a **96x96 preview in the upper-right corner**. Rendering
  restores D3D9 state, and the shared texture is released on device loss or
  shutdown. Failure leaves the existing game renderer usable.
- To test this checkpoint, set **three** environment variables under
  `UserInterface > Properties > Debugging > Environment`:

  ```text
  M2_DX11_TEST=1
  M2_DX11_ASSET_PATH=icon/item/27995.tga
  M2_DX11_SHARED_PREVIEW=1
  ```

- `DX11_SHARED_READY ... format=... center_rgba=...` means the GPU allocation
  was opened and read by both APIs. `DX11_SHARED_PRESENT ...` means the
  D3D9Ex overlay draw returned success. The image should be visible while
  the normal game continues running. On failure, `DX11_SHARED_FAILED
  stage=... hr=...` identifies the failed interop or overlay operation.
- D3D9Ex/D3D11 interop has driver, texture-format and same-adapter limits;
  neither a successful shared preview nor a DX11 shader draw means the game
  world is now rendered by DX11. The bridge is a temporary validation tool,
  **not** Baerim's native DX11 presentation path or an FPS optimization.
- Remove `M2_DX11_SHARED_PREVIEW` (or set it to `0`) to disable the preview.
  The earlier offscreen probes are independent of this toggle.

## Sixth checkpoint: first real Granny model geometry in DX11

- `M2_DX11_MODEL_PREVIEW=1` enables a one-shot capture of an actual Granny
  mesh encountered during gameplay or character preview. The client copies
  **CPU-side** `TPNTVertex` position/normal/UV data and the mesh's real 16-bit
  Granny triangle indices; it does not try to lock the in-use D3D9 GPU buffers.
- For an animated mesh, the capture runs immediately after the existing
  `CGrannyModelInstance::DeformPNTVertices()` operation, while the CPU output
  is available. Rigid meshes can be captured directly from the Granny source
  on the normal model render path. This test captures just one mesh and one
  animation pose, not a full character or continuous skeletal animation.
- The first suitable mesh (12-30,000 vertices, 36-90,000 indices) is stored
  once, then rendered **on DX11** using real vertex/index buffers, a
  POSITION/NORMAL/TEXCOORD input layout, an explicit view/projection matrix,
  backface-independent rasterization, and a depth-enabled render target.
  The geometry is normalized into an inspection camera, so it won't inherit
  the actor's current world position. A deliberately simple blue debug
  material distinguishes the new pass from Baerim's complete shaded models.
- DX11 outputs into a D3D9Ex/DX11 shared 256x256 texture. If the driver accepts
  this allocation, the game displays a **144x144 model preview** on the right,
  beneath the optional icon test. This preview uses the existing DX9Ex UI to
  display genuine DX11 geometry, not to issue the geometry draw itself.
- Test in Visual Studio by adding one line to the prior environment values:

  ```text
  M2_DX11_MODEL_PREVIEW=1
  ```

  Enter a map with visible characters/NPCs or load the character-selection
  scene; no model screenshot should appear until the engine actually supplies
  a suitable mesh. `DX11_MODEL_READY source=granny_deformed|granny_rigid
  vertices=... indices=...` confirms the DX11 model draw and D3D9 readback.
  `DX11_MODEL_PRESENT ...` confirms the overlay's first successful draw.
  `drawn_pixels` must be nonzero (the readback is compared against the clear
  background); `DX11_MODEL_FAILED stage=... hr=...` identifies setup/draw
  failures, including a model that did not produce visible pixels.
- The actual material textures, D3D9 alpha/sphere-map state, world camera
  matrices, render batching, continuously updated animation, shadows, and the
  final DX11 scene swap chain have **not** been migrated. Those are later
  stages on the path toward the Baerim model/shadow shaders. Setting the
  variable to `0` disables all model capture code on normal clients.

## Pending before switching Metin2 gameplay to DX11

1. Replace the diagnostic asset decode bridge with a persistent DX11-side
   resource manager and implement real game texture/model lifetime handling.
2. Translate the complete D3D9 render/sampler state matrix, blending, culling,
   alpha testing, depth, and fixed-function texture stages to DX11 pipelines.
3. Populate the per-frame, per-material, and per-object constant buffers from
   the actual world/view/projection, lighting, and object transform data.
4. Port models, skeletal animation, terrain, effects, trees, and the UI.
5. Add device-loss handling, configuration selection, and compatibility tests.
6. Connect DX11's actual scene/presentation path only after these draw paths
   can render correctly, with a selectable DX9 compatibility fallback.
7. Profile actual frame times and GPU workloads before performance claims.

## Build guidance

Open `Metin2Client.sln` in Visual Studio and compile `eterlib` as `Win32`.
The build uses the Windows SDK headers and libraries for D3D11/DXGI in addition
to the original D3D9 dependencies. This commit is source-level only and has not
been compiled against the complete client SDK/dependencies in this environment.

The opt-in offscreen checkpoint is linked from the real game initialization.
The existing D3D9 render loop remains the functional client path until its
resources and rendering calls have been migrated.
