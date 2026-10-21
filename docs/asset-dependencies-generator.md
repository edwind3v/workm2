# Generate asset_dependencies.txt

## Ready-to-use tool

The repository includes `tools/asset_dependencies_portable.zip`. It contains:

- `GenerateAssetDependencies.exe`: the Win32 generator.
- `granny2.dll`: the matching Granny 2.11.8.0 runtime used by this client.
- `GenerateAssetDependencies.cmd`: a double-click launcher that keeps the result visible.
- `README.md`: this guide.

No Python, Visual Studio, installed game client, or earlier optimization reports
are needed to run the portable bundle. Keep the bundled EXE and DLL together.
The executable uses a statically linked C++ runtime. Use this matching Granny
runtime with this project's assets; arbitrary files from newer Granny SDKs may
require a compatible reader and should not be assumed supported.

## Beginner steps

1. Close the game and finish saving your asset changes.
2. Unpack **every archive listed in `pack/Index`** using PML unpack. Keep each
   archive's original name and directory structure. Do not flatten the folders.
3. Extract the **contents** of `asset_dependencies_portable.zip` directly into
   `Game Client/pack`, next to `Index` and the unpacked archive folders.
4. Double-click `GenerateAssetDependencies.cmd`. Let it finish. Successful output
   says `Generated:` and shows the model/dependency counts. The console remains
   open until you press a key.
5. Read `pack/asset_dependencies_report.txt`. `UNRESOLVED` entries name references
   for which no matching unpacked asset exists. The tool does not invent a path
   or change any models/textures. Correct missing assets or invalid paths in your
   source assets as appropriate, then run the tool again.
6. The generated file is **`pack/root/asset_dependencies.txt`**. Right-click the
   **root folder** and choose **PML pack**. Deploy the resulting `root.eix` and
   `root.epk` together. Repack/deploy any model or texture archives you changed too.
7. Restart the client. The worker loads the dependency list once per process, so
   modifying the file while the game is open does not refresh that process.

Expected layout **before** you launch the CMD file:

```text
pack/
  Index
  GenerateAssetDependencies.exe
  GenerateAssetDependencies.cmd
  granny2.dll
  README.md
  root/
    playersettingmodule.py
    ...
  yw_monster/
    ymir work/
      monster/
        ...
  yw_monster2/
    ymir work/
      monster2/
        ...
  yw_pc/
    ymir work/
      pc/
        ...
  ...other unpacked archive folders...
  root.eix
  root.epk
  ...other archive pairs...
```

Archive names are taken from your own Index; they do not have to use `yw_` names.
The extraction format supported here is one directory per archive, with virtual
paths underneath it. For example, `yw_monster/ymir work/monster/...` becomes
`d:/ymir work/monster/...`. A `d/ymir work/...` extraction prefix is also supported.
`root` must be present. Retain `Index` even if you move just the unpacked folders
and tool to a different machine; the archives themselves are not required when
Index and all its unpacked directories are available.

Do not put the tool in `pack/root`, since its default scan directory is the
**directory containing the EXE**. Extra tool files in the outer pack directory
are not part of the root archive. PML should pack the root folder, not the entire
outer pack folder.

## What it generates

The generator reads the actual GR2 texture tables using Granny. It inspects
models and animations without saving or modifying them. It resolves relative
texture names against the model's virtual directory, matching this client's
`CGrannyMaterial::__GetImagePointer` behavior. It includes only texture paths
found in the unpacked asset inventory. Embedded textures without an external
filename need no external dependency record.

The output is sorted, deduplicated, binary-written text with **LF newlines and
no BOM**, as required by the current runtime parser:

```text
ASSET_DEPS_V1
d:/ymir work/monster/example/example.gr2<TAB>d:/ymir work/monster/example/example.dds
```

`<TAB>` above means one actual tab character. You normally never edit this file
by hand. The generated list covers GR2-to-texture dependencies; race/motion/effect
text-file discovery is handled separately by the client's existing read-ahead
code. Additional per-instance skin replacements and new appearance systems still
need their own runtime preparation requests.

The manifest helps a **requested** model bring its associated texture bytes into
the cache ahead of use. It does not preload the whole catalog at startup or
eliminate Granny decoding, actor construction or GPU uploads.

## Patch priority, reports and failure behavior

- `Index` order is used, with the first matching virtual path taking precedence,
  matching the normal pack dictionary insertion in this client. Duplicates are
  listed in the report, including the selected physical file.
- Keep unpacked folders synchronized with the archives you will actually deploy.
  The generator cannot discover unsaved or stale extracted data. Loose-file
  overrides and archives delivered dynamically by the server can also differ
  from the static Index inventory.
- If Index is absent, the tool falls back to alphabetically sorted `.eix` names
  and emits a warning. Supply Index to preserve the intended patch priority.
- A missing archive folder, unreadable/unsupported GR2, filesystem error, or
  exceeded runtime manifest limit fails generation. An existing manifest is
  preserved. Scan errors found while inspecting models are listed in the report.
- Unresolved texture references are reported and omitted; these do not by
  themselves fail generation. They are existing asset/reference issues, not
  proof that an asset is visibly broken. No basename matching or guessed paths
  are used.
- Existing manifest and report files receive unique `.bak-...` backups before
  replacement. Default backups go into `pack/asset_dependencies_backups`, outside
  root, so repacking root does not include old manifests. Custom outputs use an
  `asset_dependencies_backups` folder beside the output. Completed output
  replaces the old file only after it is fully
  written and closed. After a write failure, rely on the console exit status;
  the report can say the scan was ready even if the final replacement failed.
- Runtime limits are enforced: 16 MiB total text, 65,536 records, 128 textures
  per model, and 2,048 characters per record.
- Exit codes: **0** = generated; **1** = failed; **2** = command-line usage error.

Run again after adding/removing models or textures, changing a GR2 texture
reference, replacing a patch version, or changing pack order. Running it before
each asset release is a straightforward way to keep the list current. Updating
it does not require recompiling the client with the existing manifest reader.

## Command line and automation

From PowerShell, inside pack:

```powershell
.\GenerateAssetDependencies.exe
if ($LASTEXITCODE -ne 0) { throw 'Dependency generation failed; do not deploy root' }
```

For an unattended build, call the EXE directly; the CMD launcher deliberately
pauses. You can store the tool somewhere else and give explicit paths:

```powershell
.\GenerateAssetDependencies.exe --pack 'E:\My Game\pack'
```

To generate a separate output without replacing the live root source:

```powershell
.\GenerateAssetDependencies.exe --pack 'E:\My Game\pack' --output 'E:\My Build\asset_dependencies.txt' --report 'E:\My Build\asset_dependencies_report.txt'
```

Relative command-line paths are relative to the shell's working directory. The
default pack path is relative to the EXE, so double-clicking works independently
of the launcher's current directory. Use `--help` for the available options.

## Rebuild the portable tool

Sources are `tools/GenerateAssetDependencies.cpp` and
`tools/GenerateAssetDependencies.vcxproj`. Build **Release / Win32 / v143** with
the project's existing `Extern/include`, `Extern/lib`, and `Extern/dll` folders.
The generator is a separate project; rebuilding it does not rebuild the game.

From a Visual Studio 2022 Developer PowerShell at the repository root:

```powershell
msbuild .\tools\GenerateAssetDependencies.vcxproj /p:Configuration=Release /p:Platform=Win32 /m
.\tools\package_asset_dependencies.ps1
```

The EXE is written to `bin/asset-dependencies-tool`. Copy
`../Extern/dll/granny2.11.8.0.dll` beside it as **`granny2.dll`**, and distribute
that EXE/DLL pair with the CMD launcher and this guide. The packaging script does
these copies and refreshes `tools/asset_dependencies_portable.zip`. The portable
ZIP in this commit already contains the built files.

## Generation recorded for this commit

The standalone generator read the current unpacked assets in Index order:
14,712 effective GR2 files, 7,259 models with resolved textures, 12,608 dependency
pairs, 140 unresolved references, and zero inspection errors. This differs from
the earlier physical GR2 count because shadowed patch copies are inspected only
once. The 1,307,158-byte manifest matches the already deployed manifest byte for
byte (SHA-256 `14b5a63d2b14861d3d0e926c27007165233906f321965a4c50e6589c6be65402`).
No live archives were repacked or replaced during this checkpoint.
