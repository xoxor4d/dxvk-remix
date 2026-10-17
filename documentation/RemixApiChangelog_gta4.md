# Remix API changelog of the GTA IV branch

Companion of [`RemixApiChangelog.md`](RemixApiChangelog.md), which stays at its Remix Plus
state on this branch. Same format; these entries sit on top of Remix Plus's `[0.1000.1]`.
Merge them into the hub changelog when the branch is rebased.

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
