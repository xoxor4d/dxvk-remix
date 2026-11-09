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
| `REMIXAPI_VERSION_PATCH` | `10` |
| `REMIXAPI_RETAINED_UPDATE_TRANSFORM` | `0x1u` |
| `REMIXAPI_RETAINED_UPDATE_VISIBILITY` | `0x2u` |
| `REMIXAPI_RETAINED_UPDATE_LOD_FADE` | `0x4u` |

## `remixapi_StructType`

| Name | Value | Notes |
| :-- | :-- | :-- |
| `REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT` | `29` |  |
| `REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_D3D_OVERRIDES_EXT` | `30` |  |
| `REMIXAPI_STRUCT_TYPE_MESH_INFO_REPLACEMENT_HASH_EXT` | `31` |  |

## `remixapi_InstanceCategoryBit`

| Name | Value | Notes |
| :-- | :-- | :-- |
| `REMIXAPI_INSTANCE_CATEGORY_BIT_CARD_NORMALS` | `1 << 27` | 0.1000.6: shaded with a bent normal (card center to hit point, mixed with world up) instead of the vertex normals; for flat LOD card trees. |

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
| `remixapi_Bool` | `colorIsEmissive` | 0.1000.5: the colormap is also the emissive texture, lit with the material's emissiveIntensity / emissiveColorConstant (ordered geometry, unlike an additive blend state) |

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

### `remixapi_RetainedInstanceUpdate`

One item of UpdateRetainedInstances: the handle, and the transform, visibility and LOD fade opacity the flags select.

| Type | Field | Notes |
| :-- | :-- | :-- |
| `uint64_t` | `handle` |  |
| `remixapi_Transform` | `transform` | valid with REMIXAPI_RETAINED_UPDATE_TRANSFORM |
| `uint32_t` | `flags` | REMIXAPI_RETAINED_UPDATE_* |
| `remixapi_Bool` | `visible` | valid with REMIXAPI_RETAINED_UPDATE_VISIBILITY |
| `uint32_t` | `lodFadeState` | valid with REMIXAPI_RETAINED_UPDATE_LOD_FADE (0.1000.9, `lodFadeOpacity` in 0.1000.8): bits 0-7 the LOD fade opacity of the entry's D3D overrides (bits 24-31 of the modifier word), 255 = opaque. Bits 8-31 must be 0 (0.1000.10 dropped the partner bit of 0.1000.9) |

## `remixapi_Interface`

Appended after `GetGameValue` (0.1000.7). Host chosen nonzero handles; commands apply in command stream
order; every live visible entry is drawn every frame until destroyed; destroy a retained instance before
its mesh.

| Type | Field | Notes |
| :-- | :-- | :-- |
| `PFN_remixapi_CreateRetainedInstances` | `CreateRetainedInstances` | `(const remixapi_InstanceInfo* infos, const uint64_t* handles, uint32_t count)`; nothing is created unless every item is valid |
| `PFN_remixapi_UpdateRetainedInstance` | `UpdateRetainedInstance` | `(uint64_t handle, const remixapi_InstanceInfo* info)`; whole description, keeps the visibility |
| `PFN_remixapi_UpdateRetainedInstances` | `UpdateRetainedInstances` | `(const remixapi_RetainedInstanceUpdate* updates, uint32_t count)` |
| `PFN_remixapi_DestroyRetainedInstances` | `DestroyRetainedInstances` | `(const uint64_t* handles, uint32_t count)` |
| `PFN_remixapi_DrawRetainedInstances` | `DrawRetainedInstances` | `(const float* sceneOrigin)`; once per frame, sceneOrigin must be NULL |
