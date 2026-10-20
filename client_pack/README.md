# Native loading pack overlay

These are the game-pack files changed by the Owsap loading integration. They are
tracked here because the live `Game Client/pack` directory is outside this Git
repository. This is an overlay, not a complete client pack.

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
