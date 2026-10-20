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

## Pending before switching Metin2 gameplay to DX11

1. Wire the new DX11 resource types into real Metin2 texture and model loading,
   with update/release behavior tied to the appropriate D3D11 device lifetime.
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
