# Remix API changelog of the GTA IV branch

Companion of [`RemixApiChangelog.md`](RemixApiChangelog.md), which stays at its Remix Plus
state on this branch. Same format; these entries sit on top of Remix Plus's `[0.1000.1]`.
Merge them into the hub changelog when the branch is rebased.

## [0.1000.10]

LOD fade partner removed (2026-11-09).

### Removed
- `REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER` and `REMIXAPI_LOD_FADE_STATE_PARTNER`. Bit 10 of the modifier
  word is free again; bits 8-31 of `remixapi_RetainedInstanceUpdate::lodFadeState` must be 0 (an update with
  any of them set is rejected). The hand over of a partner hit to the fading side did not help the cross fades
  it was made for. Same layout, the bridge needs no rebuild. `REMIXAPI_VERSION` is now `0.1000.10`.

## [0.1000.9]

LOD fade partner (2026-11-08).

### Added
- `REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER` (bit 10 of the D3D overrides modifier word): the opaque side
  of a cross fade while the other side fades. A hit on it goes to a kept cell of a fading surface on the
  same plane, so the fading side wins where it keeps a cell whichever of the two the traversal returns
  first. Not part of the material identity.
- `REMIXAPI_LOD_FADE_STATE_PARTNER`: bit 8 of `remixapi_RetainedInstanceUpdate::lodFadeState`.

### Changed
- `remixapi_RetainedInstanceUpdate::lodFadeOpacity` is `lodFadeState`: bits 0-7 stay the opacity, bit 8 is
  the partner bit. Same layout, the bridge needs no rebuild. `REMIXAPI_VERSION` is now `0.1000.9`.

## [0.1000.8]

LOD fade opacity of retained instances (2026-11-08).

### Added
- `REMIXAPI_RETAINED_UPDATE_LOD_FADE` and `remixapi_RetainedInstanceUpdate::lodFadeOpacity`: sets the
  LOD fade opacity (bits 24-31 of the D3D overrides modifier word, 255 = not fading) of a retained
  instance without a new description. The opacity alone keeps the entry pinned; a fade starting or
  ending takes the full path once. See `docs/RemixLodFadeAPI_gta4.md`.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.8` (was `0.1000.7`). `sizeof(remixapi_RetainedInstanceUpdate)`
  grows by 4; the bridge copies the struct, so client and server are rebuilt together.
- A LOD fade opacity no longer changes the material identity of a draw: a fading draw takes the
  preserve path, the dynamic path runs once when the fade starts and once when it ends.

## [0.1000.7]

Retained instances (2026-10-31). A legacy-lane subset of Remix Plus V2's retained instances with the
same names; not worth picking onto V2.

### Added
- `remixapi_RetainedInstanceUpdate`, `REMIXAPI_RETAINED_UPDATE_TRANSFORM` and
  `REMIXAPI_RETAINED_UPDATE_VISIBILITY`.
- `CreateRetainedInstances`, `UpdateRetainedInstance`, `UpdateRetainedInstances`,
  `DestroyRetainedInstances` and `DrawRetainedInstances`, appended to `remixapi_Interface`
  after `GetGameValue`. Handles are host chosen (nonzero, unique per live entry); V2 assigns
  them in the runtime.
- Bridge commands for all five.
- `rtx.retained.pinAfterPreservedFrames`, `rtx.retained.logFrameInterval`.
- `rtx.wet.globalWetness` and bit 23 of the packed wetness value (global wetness flag), see
  `docs/RemixWetnessAPI_gta4.md`: the frame's wetness is one runtime constant, so a retained
  instance needs no update while it rains.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.7` (was `0.1000.6`). `sizeof(remixapi_Interface)` grows
  from 328 to 368 on x64. Additive, older callers never read the new slots.
- The raindrop scale of the packed wetness value has 7 bits instead of 8 (0..10 over 127).

## [0.1000.6]

CardNormals instance category (2026-10-26).

### Added
- `REMIXAPI_INSTANCE_CATEGORY_BIT_CARD_NORMALS` (bit 27) in
  `remixapi_InstanceCategoryBit`: the instance is shaded with a bent normal built
  per hit (card center to hit point, mixed with world up) instead of its vertex
  normals, for flat LOD card trees. Runtime category `CardNormals`, texture list
  `rtx.cardNormalsTextures`, tunable `rtx.cardNormals.upWeight`.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.6` (was `0.1000.5`). Additive, older callers
  never set the bit.

## [0.1000.5]

Emissive colormap-keyed materials (2026-10-24).

### Added
- `remixapi_MaterialInfoGameTexturesEXT::colorIsEmissive`: the game colormap is also
  the emissive texture of the keyed material, with the `emissiveIntensity` /
  `emissiveColorConstant` of the `remixapi_MaterialInfo`. Keeps the instance in the
  ordered geometry (motion vectors), unlike the emissive-blend override of an
  additive blend state. A USD replacement of the key still takes precedence.
- Bridge: `CreateMaterial` forwards the flag.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.5` (was `0.1000.4`). Additive, the field is only
  read from callers reporting `0.1000.5` or newer.

## [0.1000.4]

Replacement key for API meshes (2026-10-14).

### Added
- `remixapi_MeshInfoReplacementHashEXT` (`REMIXAPI_STRUCT_TYPE_MESH_INFO_REPLACEMENT_HASH_EXT`),
  chainable into `remixapi_MeshInfo` for `CreateMesh` and `CreateMeshBatched`. Its
  `replacementHash` XOR the drawn instance's material key (as on D3D9 draws, where
  the mesh key is the geometry asset hash XOR the material hash) is the key USD mesh
  replacements are looked up by and captures name the mesh by, instead of the handle
  hash. Several meshes may share it.
- Bridge: `CreateMesh` forwards the extension.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.4` (was `0.1000.3`). Additive, binaries built
  against `0.1000.3` keep working.

## [0.1000.3]

Per-draw D3D9 render-state overrides for API instances (2026-10-11).

### Added
- `remixapi_InstanceInfoD3DOverridesEXT` (`REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_D3D_OVERRIDES_EXT`),
  chainable into `remixapi_InstanceInfo`. Carries the values the D3D9 path reads
  from unused render states (RS42 categories, RS149 modifier flags, RS150 hash
  override, RS220 hash seed, RS169 emissive scale, RS210 packed wetness,
  RS211-216). The hash seed / override also applies to replacement lookups of
  API draws, as it does for D3D9 draws.

### Changed
- `REMIXAPI_VERSION` is now `0.1000.3` (was `0.1000.2`). Additive, binaries built
  against `0.1000.2` keep working.

### Fixed
- Bridge: skinned `remixapi_MeshInfoSurfaceTriangles` sent their blend weights and
  blend indices with a wrong size (vertex count x element count x bones per vertex);
  both arrays are now sent with `blendWeights_count` / `blendIndices_count` elements
  (2026-10-12).
- Bridge: `SetupCamera` is forwarded to the runtime (the client slot was empty, so
  32-bit clients could not supply a camera). Matrices and
  `remixapi_CameraInfoParameterizedEXT` are both supported (2026-10-12).

## [0.1000.2]

API materials that reuse the game's D3D9 colormap (2026-10-04).

### Added
- `remixapi_MaterialInfoGameTexturesEXT` (`REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT`),
  `{ sType, pNext, colorTexture }`, chainable into `remixapi_MaterialInfo`. An opaque
  material with an empty `albedoTexture` renders like a non-replaced D3D9 draw of
  that texture and uses its image hash as replacement / capture / category key.
- Bridge: `CreateMaterial` forwards the extension (the texture as D3D object id).

### Changed
- `REMIXAPI_VERSION` is now `0.1000.2` (was `0.1000.1`). Additive, `remixapi_Interface`
  is unchanged.

### Fixed
- Instances submitted without `remixapi_InstanceInfoObjectPickingEXT` all
  shared picking value 0 (the first D3D9 draw's ID), so dev-menu texture
  picking / highlighting could not resolve their textures. While picking is
  active they now get per-submesh values from `0x80000000` up; explicit
  values are unchanged.
