# Remix API surface additions of the GTA IV branch

`RemixApiSurface.md` is generated from `public/include/remix/remix_c.h` by
`scripts-common/generate_remix_api_md.py` and kept at its Remix Plus state on this branch.
This file lists what regenerating it on this branch adds. After a rebase, regenerate
instead of merging. (Regenerating also picks up two header comments Remix Plus already has
but whose generated file predates them, the `Deprecated` note on
`REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_TRANSPARENCY_LAYER` and the four DLSS control-mask
fields of `remixapi_MaterialInfoOpaqueEXT`; neither is this branch's.)

## Macros

| Macro | Value |
| :-- | :-- |
| `REMIXAPI_VERSION_PATCH` | `4` |

## `remixapi_StructType`

| Name | Value | Notes |
| :-- | :-- | :-- |
| `REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT` | `29` |  |
| `REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_D3D_OVERRIDES_EXT` | `30` |  |
| `REMIXAPI_STRUCT_TYPE_MESH_INFO_REPLACEMENT_HASH_EXT` | `31` |  |

## Structs

### `remixapi_InstanceInfoD3DOverridesEXT`

Per draw overrides that the D3D9 path reads from unused render states (GTA4 compatibility).
Categories and modifier flags are applied like RS42 / RS149, the hash fields change the material
hash used for replacement lookups, the remaining values feed the modifier flags. Absent fields
behave like a D3D9 draw with those render states at 0.

| Type | Field | Notes |
| :-- | :-- | :-- |
| `remixapi_StructType` | `sType` |  |
| `void*` | `pNext` |  |
| `uint32_t` | `categoryFlags` | RS42: runtime InstanceCategories bit order, OR'ed into the instance categories |
| `uint32_t` | `modifierFlags` | RS149: REMIX_MODIFIER_FROM_D3D bits |
| `uint64_t` | `hashOverride` | RS150: replaces the material hash when non-zero |
| `uint32_t` | `hashSeed` | RS220: re-hashes the material hash when non-zero, takes precedence over hashOverride |
| `float` | `emissiveScale` | RS169: emissive intensity multiplier (EMISSIVE_SCALAR modifier) |
| `uint32_t` | `packedWetness` | RS210: two 16 bit wetness parameters (ROUGHNESS_SCALAR modifier) |
| `float` | `multiUse[6]` | RS211-216: UV animation / translucent parameters |

### `remixapi_MaterialInfoGameTexturesEXT`

Chained from remixapi_MaterialInfoOpaqueEXT::pNext. On an opaque material with an empty
albedoTexture, this D3D9 texture (filled before CreateMaterial) is used as albedo and its
image hash becomes the material's replacement / capture / category key, the same key a
D3D9 draw of that texture gets.

| Type | Field | Notes |
| :-- | :-- | :-- |
| `remixapi_StructType` | `sType` |  |
| `void*` | `pNext` |  |
| `IDirect3DTexture9*` | `colorTexture` |  |

### `remixapi_MeshInfoReplacementHashEXT`

Chained to remixapi_MeshInfo: the geometry part of the key USD mesh replacements are looked up by and
captures name the mesh by, instead of remixapi_MeshInfo::hash (which stays the handle). Like on D3D9 draws
the key is replacementHash ^ the material key of the drawn instance (its game color texture hash with the
instance's D3D hash overrides), so the geometry asset hash a D3D9 draw of the same geometry gets makes
replacements authored against D3D9 captures apply. Several meshes may share it. 0 keeps the handle hash.

| Type | Field | Notes |
| :-- | :-- | :-- |
| `remixapi_StructType` | `sType` |  |
| `void*` | `pNext` |  |
| `uint64_t` | `replacementHash` |  |
