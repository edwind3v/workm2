# Native loading pack overlay

These are the game-pack files changed by the Owsap loading integration. They are
tracked here because the live `Game Client/pack` directory is outside this Git
repository. This is an overlay, not a complete client pack.

`root/asset_dependencies.txt` is also tracked here for the asset read-ahead
optimization. Regenerate it from all unpacked client assets when GR2 texture
references or pack priority change. See `docs/asset-dependencies-generator.md`
and the ready-to-use `tools/asset_dependencies_portable.zip`. Repack root after
updating the manifest. The optimized GR2/texture archive payloads themselves are
outside this source repository; retain the local asset pipeline backups and
deployment records described in `docs/asset-preparation-optimization.md`.

Copy the contents of each directory into the corresponding unpacked pack:

- `root/` into the root pack's input directory.
- `uiscript/uiscript/` into the uiscript pack's virtual `uiscript/` directory.
- `locale/locale/` into the locale pack's virtual `locale/` directory.

Repack those packs and deploy them together with the client executable built
from this commit. In particular, the warp scripts call the new native
`loading.IsWorldReady()` binding and require the matching executable.

The `znr_*.png` loading frames come from the supplied Owsap implementation guide.
They are included for the common UI path and the separate Arabic UI path.

The Python 2.7 checks in `tools/` use this overlay by default. Set
`METIN2_PACK_ROOT` to an external unpacked pack directory to check a deployed
source tree instead.
