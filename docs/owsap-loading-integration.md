# Owsap loading integration

The enabled client path registers character assets, animations, effects, sounds,
NPC names, and race heights in `UserInterface/PythonLoading.cpp` and its native
tables in `PythonLoading.hpp`. The Python asset routines are retained for the
disabled feature configuration. They do not perform registration when native
loading is enabled.

## Entry points and ownership

| Stage | Entry point | Work |
| --- | --- | --- |
| Application creation | `CPythonApplication::Create` | Starts the owned `m_pyLoading` worker after graphics creation. |
| Native preload | `CPythonLoading::__LoadData` | Initializes dust/pick effects, sounds, effects/flying data/emoticons, NPC list, race heights, player races/motions, colors, and emotion icons. |
| Character selection | `CPythonNetworkStream::SetSelectPhase` | Joins the preload worker before selection UI can access player races. |
| Locale labels | `CPythonLoading::JoinThreadLoading` | Registers localized titles on the interpreter thread. Uses the real Python import hook for `localeInfo`. |
| Locale tables | `LoadLocaleData` | Loads item/mob/skill tables and the native guild building list. |
| Loading UI | `introLoading.NewLoadingWindow` | Displays a background and sends the character-selection packet. Has no Python asset-loading queue. |
| Main-character packet | All four `RecvMainCharacter*` handlers | Call native `LoadBackground`, which warps the map, assigns skills, sets view distance/center, and starts the game. |
| Warp reconnect | `RecvWarpPacket` / `MainStream.SetWarpPhase` | Shows a stream-owned overlay, closes old phase windows, sends selection, and lets the native main-character handler load the destination. The overlay survives interface destruction. |
| Destination reveal | `GameWindow.OnRender` / `MainStream.OnWarpWorldRendered` | After drawing the destination, checks native `loading.IsWorldReady()` for game phase, map readiness, and a main-character instance. Only then hides the overlay. Warp phase changes bypass the normal curtain fades. |
| Guild building UI | `guild.GetGuildBuildingList` | Reads metadata already parsed in C++; the UI does not reload the data file. Metadata survives per-guild state resets. |
| Shutdown | `CPythonApplication::Clear` / `Destroy` | Releases the native guild-list Python reference before interpreter finalization; joins/stops the asset worker before destroying engine managers. |

The Owsap reference places feature guards inside `__InitData`, `__LoadGameSound`,
`__LoadGameEffect`, and the five character loaders. Those guards are retained.
Restoring the `LoadGameData` dispatcher did not enable Python animation/effect
loading. The enabled production phase now uses `NewLoadingWindow` and never
enters the legacy `LoadingWindow.LoadData` queue.

In this client's `PythonSkill.cpp`, `skill.LoadSkillData()` returns `None`
immediately. Skill descriptions/tables are loaded by native locale loading;
skill-slot assignment is in `CPythonLoading::RegisterSkills`.

## Corrections made during the audit

- Constructed the missing loader member, which caused the singleton assertion.
- Replaced `PyImport_AddModule("localeInfo")` with a real import. The client's
  custom importer searches packed scripts and matches module names without case.
  Creating an empty module could shadow the already loaded locale module.
- Checked locale objects before converting them, copied their string data, and
  released the owned Python references. `TOOLTIP_WARRIOR` belongs to `localeInfo`
  loaded from `locale_game.txt`; it is not initialized by player asset setup.
- Added Owsap's missing lightweight loading window and repaired the warp
  selection handshake. The earlier warp hook opened game UI prematurely.
- Added the loading escape path and connected the supplied warp overlay and its
  59 image frames.
- Corrected the overlay's lifetime after a live report of disappearance and gray/
  black frames. The guide places it inside `Interface`, but this client's
  `GameWindow.Close()` tears that interface down before the destination loads.
  The stream now owns one persistent overlay on the `CURTAIN` layer, with an
  opaque backdrop. Normal login fades still operate; cancellation and shutdown
  release the overlay.
- Connected the native guild list to the building UI and preserved its lifetime
  across map changes and guild resets.
- Added the missing Wolfman bleeding and refined-claw effects to the C++ table,
  matched the flame effect path, and preserved locale-specific empire/GM paths.
- Preserved the original legacy byte strings for the Python emoticon commands.

## Verification and limits

`tools/check_owsap_native_flow.py` runs with Python 2.7 and checks:

- Syntax of the edited scripts under the client's Python version.
- The enabled phase selects the native loading window, sends one selection
  packet, and has no legacy asset queue.
- Warp closes old windows once and sends its required packet without recursive
  phase cleanup.
- Python animation/effect/sound/character asset routines return before engine
  calls when native loading is enabled.
- All 561 literal asset references in the migrated Python groups appear in the
  native source/tables. This checks coverage, not full semantic equivalence.
- Every warp animation image referenced by the layout is installed.
- The actual Python 2.7 C API import function honors a custom import hook and
  returns the populated module.

The C++ solution was built as Release / Win32 and Debug / Win32 with v143. Logs
are `bin/owsap-release-build.log` and `bin/owsap-debug-build.log`. These checks do not establish live server login,
map rendering correctness, loading-time improvement, or crowded-scene FPS.

`tools/check_warp_overlay_lifetime.py` additionally simulates repeated teleports,
old-interface destruction, each failed readiness condition, destination-render
notification, escape to login, and shutdown. It confirms that readiness alone
does not hide the overlay before the destination's render callback.

The guide's `Warp()` map load remains synchronous on the main thread. The
animation cannot advance while that call blocks; its last frame must remain
visible. Removing that pause requires incremental map loading or a separate
rendering design. Keeping the overlay alive fixes premature disappearance and
exposed gray/black phase frames, without moving map loading onto an unsafe
graphics thread.

## Files to package

The modified pack inputs are tracked in `client_pack/` as an overlay. The checks
use that overlay by default; set `METIN2_PACK_ROOT` to check another unpacked pack
directory. This captures the pack changes alongside the C++ source in Git.

Deploy the rebuilt `bin/Release/metin2client.exe`. Repack the modified `root`
scripts, `pack/uiscript/uiscript/warpshowerwindow.py`, and the added `znr_*.png` frames
under the `locale` pack's `locale/common/ui/loading` and `locale/ae/ui/loading`
directories. Existing loading backgrounds were retained.

Original files from this completion pass are saved under
`bin/owsap-loading-backup-20261020-232910`.
The warp lifetime correction has a separate backup under
`bin/owsap-warp-backup-20261020-234320`.
