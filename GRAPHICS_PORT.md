# Graphics migration notes

This branch introduces a Direct3D 11 backend foundation alongside the existing
Direct3D 9Ex renderer. The active client rendering path has **not** been switched.

## Implemented in the first step

- `EterLib/DX11Device.h/.cpp`: hardware device initialization, a two-buffer
  flip-model swap chain, color/depth targets, resizing, clearing, and presenting.
- `EterLib/DX11ShaderLibrary.h/.cpp`: loading DXBC shaders from files or memory,
  creating each of the six D3D11 shader stages, retaining vertex-shader bytecode
  for input-layout creation, and binding a shader to its stage.
- Both source units compile as part of the Win32 `eterlib` Visual Studio project.

These classes do not intercept, translate, or replace existing D3D9 calls.
No game-specific shader binaries or client assets are checked into this branch.

## Pending before enabling DX11 in the client

1. Port texture, vertex/index-buffer, and input-layout management from D3D9.
2. Translate saved render/sampler state and fixed-function texture stages to
   DX11 pipeline states and explicit shaders.
3. Bind per-frame, per-material, and per-object constant buffers correctly.
4. Port models, skeletal animation, terrain, effects, trees, and the UI.
5. Add device-loss handling, configuration selection, and compatibility tests.
6. Profile actual frame times and GPU workloads before performance claims.

## Build guidance

Open `Metin2Client.sln` in Visual Studio and compile `eterlib` as `Win32`.
The build uses the Windows SDK headers and libraries for D3D11/DXGI in addition
to the original D3D9 dependencies. This commit is source-level only and has not
been compiled against the complete client SDK/dependencies in this environment.

To test the DX11 path at runtime, a future integration step must call
`graphics::DX11Device::Create`, then `BeginFrame`/`Present`, and invoke
`DX11ShaderLibrary` with real bytecode and corresponding pipeline resources.
The existing D3D9 render loop remains the functional client path until then.
