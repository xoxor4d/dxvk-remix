# Remix API additions of the GTA IV branch

Companion of [`RemixApi.md`](RemixApi.md), which stays at its Remix Plus state on this
branch. Each section below names the hub section it extends and the sentence it follows,
so the text can be merged into the hub (or kept apart) when the branch is rebased. The
dated entries are in
[`../documentation/RemixApiChangelog_gta4.md`](../documentation/RemixApiChangelog_gta4.md),
the generated field tables in [`../RemixApiSurface_gta4.md`](../RemixApiSurface_gta4.md).

## Materials: `remixapi_MaterialInfoGameTexturesEXT`

Hub section "Materials", after the table of type-specific extensions (the
`remixapi_MaterialInfoPortalEXT` row) and before "The runtime uses `info->hash` to dedupe":

`remixapi_MaterialInfoGameTexturesEXT` can be chained in addition (any
position in the chain). It hands the runtime one of the game's own D3D9
textures (`IDirect3DTexture9*`, filled before `CreateMaterial`):

```c
typedef struct remixapi_MaterialInfoGameTexturesEXT {
  remixapi_StructType sType;          // ..._MATERIAL_INFO_GAME_TEXTURES_EXT
  void*               pNext;
  IDirect3DTexture9*  colorTexture;
  remixapi_Bool       colorIsEmissive; // 0.1000.5
} remixapi_MaterialInfoGameTexturesEXT;
```

If the material is opaque and its `albedoTexture` is empty, it renders like
a non-replaced D3D9 draw of that texture: the D3D9 texture is the albedo (no
second upload), `rtx.legacyMaterial.*` defaults replace the `OpaqueEXT`
constants (only alpha test / blend and the sampler filter / wrap are kept),
and the material's replacement, capture and texture-category key is the
texture's image hash, so it shares `mat_<hash>` with D3D9 draws. A matching
replacement is merged over it like on the D3D9 path; changes to
`rtx.legacyMaterial.*` and `rtx.ignoreAlphaOnTextures` apply live. Materials
without the extension are unchanged.

With `colorIsEmissive` (0.1000.5) the colormap is also the material's emissive
texture, lit with the `emissiveIntensity` and `emissiveColorConstant` of the
`remixapi_MaterialInfo` (the one input the legacy defaults do not replace).
Unlike an additive blend state this keeps the instance in the ordered geometry,
so it has motion vectors; a replacement of the key still takes precedence.

## Instances: `remixapi_InstanceInfoD3DOverridesEXT`, object picking

Hub section "Instances (per-frame draw)", extension table: a new row before
`remixapi_InstanceInfoObjectPickingEXT`, and that row gains its second sentence:

| Extension | Purpose |
| :-- | :-- |
| `remixapi_InstanceInfoD3DOverridesEXT` | The per-draw values the D3D9 path reads from unused render states: texture categories (runtime bit order), modifier flags, hash seed / override (changes the hash used for replacement lookups), emissive scale, packed wetness and the RS211-216 values. Lets an API draw look exactly like the D3D9 draw it replaces. |
| `remixapi_InstanceInfoObjectPickingEXT` | Tags the instance with a 32-bit ID readable via the picking API. Use non-zero values below `0x80000000`; without it, the runtime assigns per-submesh values from `0x80000000` up while picking is active (dev-menu texture selection). |

## `SetupCamera`: bridge

Hub section "`SetupCamera`", after "...to specify position/forward/up/right + FOV + aspect + planes.":

The bridge forwards both forms (32-bit clients included).

## Instances: retained instances

New hub section after "Instances (per-frame draw)". Remix Plus V2 ships retained instances with a scene
owned lane; this branch carries a legacy-lane subset with the same names, so host code written against
it ports to V2 (host chosen handles excepted).

A static object is described once and replayed by the runtime every frame, instead of one `DrawInstance`
per placement per frame. The runtime keeps each entry's description, runs it through the regular external
draw path until the preserve path reports it unchanged for `rtx.retained.pinAfterPreservedFrames` (2)
frames, then pins it: a frame only keeps a pinned instance alive. A description change, a visibility
change, a texture cache clear, an option that invalidates draw translation or active object picking drops
the pin and the entry takes the full path again.

| Function | Purpose |
| :-- | :-- |
| `CreateRetainedInstances(infos, handles, count)` | Registers `count` draws (`remixapi_InstanceInfo` with its usual pNext chain) under host chosen handles: nonzero, unique per live entry. Nothing is created unless every item is valid. |
| `UpdateRetainedInstance(handle, info)` | Replaces the whole description (mesh, categories, blend, D3D overrides, bones). Keeps the visibility. |
| `UpdateRetainedInstances(updates, count)` | `remixapi_RetainedInstanceUpdate` per item: `REMIXAPI_RETAINED_UPDATE_VISIBILITY` shows or hides the entry (hidden is sticky and costs nothing per frame), `REMIXAPI_RETAINED_UPDATE_TRANSFORM` moves it (a whole-record change on this branch). |
| `DestroyRetainedInstances(handles, count)` | Forgets the entries; their instances age out through the normal garbage collection. Destroy a retained instance before its mesh. |
| `DrawRetainedInstances(sceneOrigin)` | Once per frame, draws every live visible entry. `sceneOrigin` must be `NULL` (no origin rebase on this branch). Issue it before the frame's injection point (for the GTA IV mod: before the orthographic quad that triggers it), a draw after it lands in the next frame, and only in frames whose camera was set up from the world view: the replay uses the frame's camera like any other draw. |

Commands apply in command stream order relative to the other API calls, so an entry created or shown
before the frame's `DrawRetainedInstances` is drawn in that frame, and a `DestroyRetainedInstances` issued
before `DestroyMesh` runs before it. Errors: `REMIX_DEVICE_WAS_NOT_REGISTERED`, `INVALID_ARGUMENTS` (null
pointers, zero count, a zero handle, a wrong `sType`, unknown flags, a non-null origin); conditions the
runtime only sees later (duplicate create, unknown handle, mesh destroyed under a live entry) are logged
once, since the bridge forwards these calls without a response.

The bridge forwards all five (32-bit clients included); a batch is one message per 512 items. The
`[Retained]` log line (`rtx.retained.logFrameInterval`) counts entries, visible, pinned, preserved, full
replays and dropped entries.

## Convention namespaces: `rtx.water.*`

Row to add to the "Convention namespaces" table:

| Namespace | Channel | Page |
| :-- | :-- | :-- |
| `rtx.water.*` | `SetConfigVariable` | [`RemixWaterAPI_gta4.md`](RemixWaterAPI_gta4.md) |

## Meshes: `remixapi_MeshInfoReplacementHashEXT`

Hub section "Meshes", after "The padding is reserved for future runtime use; leave it zeroed.":

`remixapi_MeshInfo::hash` is the handle. By default it is also the key
USD mesh replacements are looked up by and the name captures give the
mesh. Chain `remixapi_MeshInfoReplacementHashEXT` to use another key.
Like on D3D9 draws, where the mesh key is the geometry asset hash XOR the
draw's material hash, the key is `replacementHash` XOR the material key
of the drawn instance: the hash of its material's game color texture
(`remixapi_MaterialInfoGameTexturesEXT`) with the instance's
`remixapi_InstanceInfoD3DOverridesEXT` hash seed / override applied.
Pass the geometry asset hash a D3D9 draw of the same geometry would get,
and replacements authored against D3D9 captures keep applying when the
geometry moves to the API. Several meshes may share it. `0` keeps the
handle hash.

## Convention namespaces: `rtx.wet.*`

Row to add to the "Convention namespaces" table:

| Namespace | Channel | Page |
| :-- | :-- | :-- |
| `rtx.wet.*` | `SetConfigVariable` | [`RemixWetnessAPI_gta4.md`](RemixWetnessAPI_gta4.md) |

## Instances: `REMIXAPI_INSTANCE_CATEGORY_BIT_CARD_NORMALS`

Hub section "Instances (per-frame draw)", after "The full list of category bits is
`remixapi_InstanceCategoryBit` in the header.":

`REMIXAPI_INSTANCE_CATEGORY_BIT_CARD_NORMALS` (bit 27, 0.1000.6) shades the
instance with a bent normal built per hit instead of its vertex normals: the
direction from the card center (the midpoint of the triangle's longest edge) to
the hit point, mixed with world up by `rtx.cardNormals.upWeight`. Meant for flat
LOD trees made of crossed cards, which otherwise shade as one lit and one black
plane. The same effect by texture: list the material key in
`rtx.cardNormalsTextures`; for colormap-keyed materials that is the colormap's
image hash.
