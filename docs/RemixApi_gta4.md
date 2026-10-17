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

## Instances: `remixapi_InstanceInfoD3DOverridesEXT`, object picking

Hub section "Instances (per-frame draw)", extension table: a new row before
`remixapi_InstanceInfoObjectPickingEXT`, and that row gains its second sentence:

| Extension | Purpose |
| :-- | :-- |
| `remixapi_InstanceInfoD3DOverridesEXT` | The per-draw values the D3D9 path reads from unused render states: texture categories (runtime bit order), modifier flags, hash seed / override (changes the hash used for replacement lookups), emissive scale, packed wetness and the RS211-216 values. Lets an API draw look exactly like the D3D9 draw it replaces. |
| `remixapi_InstanceInfoObjectPickingEXT` | Tags the instance with a 32-bit ID readable via the picking API. Use non-zero values below `0x80000000`; without it, the runtime assigns per-submesh values from `0x80000000` up while picking is active (dev-menu texture selection). |
