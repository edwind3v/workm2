# Asset preparation and texture loading

For a standalone, ready-to-use dependency generator and beginner instructions,
see [asset-dependencies-generator.md](asset-dependencies-generator.md). The
portable ZIP does not require this pipeline's inventory or Python installation.

This update extends the v2 checkpoint `f0bc94b`. Native loading/streaming, text
batching, animation timing, visibility, entity counts, authored placement and
low-memory behavior remain in use. Build both configurations as Win32/v143.
No Cython, replacement font system or runtime texture compressor is added.

## Native changes

- DDS detection continues to inspect file contents, including DDS bytes stored
  under legacy image filenames. Other image formats keep D3DX format/filter,
  color-key and low-memory conversion behavior.
- The DXT reader checks magic, header sizes, dimensions, mip counts, resource
  type and every mip's payload range. Single-slice DDS files tagged as volume
  resources remain accepted as 2D, including existing Ent Elder/Empire Stone
  textures. True multi-slice volumes and cubemaps use the legacy fallback.
  Block sizes use rounded-up 4x4 dimensions
  with a minimum block. The mip limit increases from 12 to 16.
- Compressed mip copies honor both source block rows and destination pitch.
  This replaces the flat memcpy and quartered-top-level offset assumptions.
- Compatible DDS textures upload through one temporary SYSTEMMEM texture into
  a static DEFAULT texture. The temporary is released immediately. Low-memory
  mip bias remains. Unsupported allocation/format paths fall back to D3DX.
  Failed copies/locks/transfers do not leave partially initialized textures.
- Mip filtering uses supported LINEAR/POINT/NONE modes. Capabilities are cached
  at initialization/reset. Existing min/mag quality and anisotropy limits stay
  unchanged. SpeedTree's invalid anisotropic mip request is corrected too.
- Vertex/pixel shader constant uploads skip identical valid ranges. Device
  default-state initialization and restore invalidate the cache. Failed API
  uploads remain invalid; failed external state-block restoration invalidates
  the shader cache. BeginScene transform synchronization is retained.
- The existing low-priority byte worker reads optional `asset_dependencies.txt`
  from root once. It discovers GR2 texture dependencies through that manifest.
  Speculative readiness includes referenced textures, rather than just the
  model file. Missing/evicted dependencies are requested again. Existing
  256-pending/384-ready and 16/48 MiB byte-cache limits remain; this is not an
  all-model preload and does not perform GPU or Granny work on the byte worker.

The build marker is `START version=5 ... asset_preparation=1`. New profiler
stages include `texture.load`, `texture.dds_upload`, `texture.dds_allocate`,
`texture.dds_copy`, `texture.dds_transfer` and `texture.legacy_upload`.

## Offline tooling

`tools/AssetInspector.vcxproj` builds a Win32 inspector using the same selected
Granny SDK and DLL as the client. It fingerprints the loaded object graph using
SHA-256: typed members, arrays, names, skeleton/mesh data, animation curves and
ExtendedData. It excludes only top-level exporter/art-tool/source provenance.
Pointer values are replaced by traversal identities. Unknown pointer members
or invalid data prevent acceptance. Archive-index inspection also checks the
client's legacy index representation and reports compression types.

`tools/asset_pipeline.py` requires Python 3.10+, Pillow and NumPy. Its commands:

1. `unpack`: back up and hash every original EIX/EPK pair; PML-unpack only missing
   extracted folders. Existing extracted folders are retained.
2. `inventory`: record files/hashes/texture headers and inspect all GR2 files.
3. `prepare-gr2`: write separate candidates with PlatformConvert targeting
   32-bit/little-endian and CompressBitKnit2. The SDK rewrite prepares the current
   file representation. Check each command's exit/output, then fingerprint all
   candidates. Accept only equal runtime data with a smaller serialized file.
4. `prepare-dds`: preserve good BC1/BC3 DDS. Consider suitable opaque world/model
   images only, with no resize or forced sRGB conversion. Preserve transparency,
   magenta color keys, masks, UI and special texture paths. Check decoded output
   opacity, dimensions, RMSE <= 3, max channel error <= 40 and <= 0.1% of pixels
   with channel error > 16; require a smaller file. These numeric checks are not
   a substitute for the user's in-game appearance review. Keep original virtual
   filenames even when their contents become DDS.
5. `dependencies`: derive a sorted GR2-to-texture manifest from actual SDK data,
   including relative material paths. Report unresolved references separately.
6. `promote`: preflight source/candidate hashes, back up modified extracted
   assets, then replace individual files atomically and add the manifest.
7. `repack`: PML-pack modified archives in a separate workspace snapshot. Preserve
   every original member and its compression/encryption type. Reject unsupported
   types. PML-unpack every staged archive and compare every packed member's hash.
8. `deploy`: require the game to be closed, preflight all archive hashes, and
   replace verified archive pairs. Originals stay in the backup directory.
9. `rollback`: with the game closed, restore original pairs and changed loose
   assets, removing only the generated manifest. Refuse to overwrite files edited
   since deployment. Engine source can separately be reviewed/reverted in Git.

No origin moves, namespace removal, tangent generation, mesh merging, lossy
vertex packing or broad custom-data removal are applied. Size reductions and
file hashes establish preparation results; they do not establish FPS gains.

For this run, working data and immutable originals live under
`bin/asset-preparation-v3`. `archives.json`, `inventory.json`, baseline/prepared
inspection reports, accepted/rejected lists, `promotion.json`, `repacked.json`
and `deployed.json` record the exact decisions and deployed files. Do not delete
that folder while rollback is needed.

The full scan covered 89,638 files and 14,751 GR2 files. The conservative GR2
profile accepted 10,718 smaller files with matching fingerprints, reducing their
combined serialized size by 95,575,417 bytes. It retained 3,932 files whose strict
graph fingerprint differed and 101 without a size reduction. A mismatch is a
reason to retain the original, not proof that a visible defect would occur.
Five opaque textures passed conversion checks (243,322 bytes saved); existing
block-compressed textures were preserved. The generated manifest covers 7,259
models and 12,608 texture references. The 140 unresolved source references are
reported separately rather than guessed or silently renamed.

Deployment completed for 15 modified archive pairs after round-trip checks of
54,545 packed members. Original members retain their compression/encryption
types. These archive pairs total 1,729,313,556 bytes, down from 1,824,889,964
bytes. All 24 original archive pairs and changed extracted assets are backed up.
`bin/asset-preparation-v3/run-summary.json` records the deployed Release hash,
archive list, final build status and the exact preparation totals.

Copies of the completed run summary and deployed archive hashes are tracked as
`docs/asset-preparation-run-summary.json` and
`docs/asset-preparation-deployed-archives.json`. Optimized archive payloads and
rollback originals remain outside Git. The root dependency manifest is included
in the tracked `client_pack/root` overlay.

## User capture

Restart the deployed Release client. Repeat Cape/desert routes, first visits
and revisits, teleports, crowded combat, mounts/costumes/effects, camera movement,
window resize and minimize/restore. Inspect terrain/building textures, alpha
edges, distant mips, names, attachments and static-object placement. Compare
first-load pauses separately from continuing crowd FPS. Exit normally and share
the new `syserr.txt`; look for the v5 marker and new texture stages. The offline
tool's cached standalone decode durations are not directly comparable to game
frame times, package I/O or GPU completion.
