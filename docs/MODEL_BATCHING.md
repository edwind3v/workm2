# Model draw batching

Enabled by default. `M2_MODEL_BATCHING=0` at process startup restores the original
draw submissions, independently of `M2_GPU_SKINNING`.

## Within-mesh batching

At model load, eligible consecutive triangle groups with the same material are
combined within one mesh and one material-type pass. A separate index buffer
copies their triangles in **the existing linked-list draw order**, including
the order inside each group. The original buffer and lists remain available
for collision, previews, fallback, and comparison.

The renderer keeps the same world/bone attachment matrices, instance material
overrides, textures, lighting, specular settings, transparency blend state,
depth state, and actor/pass order. Material changes and mesh boundaries end a
batch. It does not sort triangles by material, merge different actors, reduce
particle counts, skip animations, or change effect update rates. Armour and
weapon particle/mesh effect rendering is unchanged.

Unsupported ranges, groups exceeding the device primitive limit, unprofitable
lists, or additional index data exceeding 32 MiB per model use the original
draws. Buffer allocation/upload failure also retains the original path. The
extra CPU index copy is retained for rebuilding the optional device buffer.

`MODEL_BATCH_ACTIVE` reports the first merged draw. `model_draws_saved` in
`MAP_PROFILE` reports omitted submissions; `actor_draw_calls` reports the
remaining group submissions. These counts are not FPS measurements. Compare
the same camera position and spawned metins in game to assess frame times.

## Batching across rigid meshes

The renderer also builds an ordered, rebased index list across rigid mesh parts
of a model with XYZ/NORMAL/one-UV vertices. Consecutive groups using the same
material can merge. The shared vertex buffer, every triangle, UV, material and
triangle order are retained. This is separate from the earlier within-mesh path.

Each instance checks its current part matrices during `UpdateWorldMatrices`.
Exact matches qualify. Tiny rest-pose floating-point residuals can qualify only
when a bound using the actual vertex magnitudes limits position changes to
0.0001 world units and inverse-transpose normal changes to 0.000001, with a
maximum linear coefficient difference of 0.00000001. Larger differences retain
the original separate draws, so independently animated parts remain supported.
The original matrices, attachments and pose updates are never modified.

This matters for `metinstone_01.gr2`: asset inspection found 89 rigid meshes,
7,550 vertices and 89 groups. Its rest-pose composite matrices contain tiny
roundoff residuals (up to approximately 0.0000086 translation units), so a byte
comparison alone would reject even the stationary asset.

Custom shader/vertex-blend passes, unsupported layouts, more than 65,536 rigid
vertices, device limits or failed optional uploads use the original path. A
failed merged draw retries only that batch's original groups to avoid drawing
earlier transparent batches twice. `M2_RIGID_BATCHING=0` disables this stage;
`M2_MODEL_BATCHING=0` disables both stages at model load.

`RIGID_BATCH_READY` describes candidate source groups versus merged draws;
`RIGID_BATCH_ACTIVE` confirms a successful merged draw. `DRAW_OPT` reports
`rigid_batch_draws`, `rigid_draws_saved` and `rigid_matrix_fallbacks`. The last
counter counts list visits rejected by the matrix guard. Actual FPS and
activation in animated game scenes still require an in-game comparison.

Future effects still require visual checks with their actual assets. Preserve
the same rules when extending the renderer: combine only identical draw state,
retain triangle order, and leave per-effect attachments and timing intact.
