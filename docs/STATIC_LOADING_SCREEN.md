# Cached loading image for warps

The rejected independent Win32 presenter, WIC animation cache, scene capture,
render-target readback and changed presentation/network flow have been reverted.
The client uses its original UI renderer in both windowed and fullscreen modes.

Before the native asset-loading worker starts, `CPythonApplication::Create`
loads the four existing `loading0.sub` through `loading3.sub` resources with
`CResourceManager::LoadStaticCache`, the same cache used for motion assets.
The subimages retain their referenced image textures. Common UI paths are used
when available, with the active locale's existing loading directory as fallback.
`app.GetLoadingScreenImages()` exposes the actual successfully cached names.

`MainStream.Create` creates one persistent `WarpShowerWindow` and its four
image instances before gameplay. Character entry chooses a background and uses
that same cached image in `NewLoadingWindow`. Subsequent warps show the selected
image instance again; they do not construct an animation, load a layout, read
image files or decode new textures. A new character entry may choose another
cached variant. Scaling is refreshed only if the screen size changes.

The stream retains the loading window while the old game interface is destroyed.
The existing destination-world readiness/render notification hides it. Escape,
return to login and stream destruction keep their existing cancellation paths.
There is no animation timer, extra presentation thread or scene capture.

The existing resource manager owns cache/device recovery and shutdown. The
legacy archive format is unchanged. The root archive keeps all 98 names and
type-2 entries; only `introloading.py`, `networkmodule.py` and `uiwarpshower.py`
have changed decoded payloads. Other pack archives are preserved.

The Release build and root archive round-trip checks passed. A live server
warp has not been verified, and no loading-time benchmark was run.
`[LOADING_SCREEN] cached_images=4 ...` in `syserr.txt` identifies startup caching.
