# Fork touchpoints of the GTA IV branch (`game/gta4_plus_api`)

Companion of [`fork-touchpoints.md`](fork-touchpoints.md). That file is Remix Plus's inventory and stays
untouched on this branch; everything the GTA IV work adds or changes on top of it is listed here, in the
same format, one workstream per commit. Keep the two files apart: a rebase of this branch onto a newer
Remix Plus or upstream never has to merge this file against theirs.

## Re-applying a workstream

1. Fork-owned files (`src/dxvk/rtx_render/rtx_fork_*.cpp/.h`, `src/dxvk/shaders/rtx/algorithm/rtx_fork_*.slangh`)
   are copied as they are; add them to `src/dxvk/meson.build` where the other `rtx_fork_*` files are listed.
2. Hook call sites and inline tweaks in upstream files are listed with their function and the neighbouring
   code to search for. Re-add the one-line dispatch / the small edit by hand; the quoted anchors are more
   reliable than line numbers.
3. API surface changes (`public/include/remix/remix_c.h`, `remix.h`, `rtx_remix_specialization.inl`, the
   bridge serializers) are summarized in [`RemixApi_gta4.md`](RemixApi_gta4.md) and
   [`../documentation/RemixApiChangelog_gta4.md`](../documentation/RemixApiChangelog_gta4.md). After a rebase run
   `python -I scripts-common/generate_remix_api_md.py` to refresh `RemixApiSurface.md` (derived, never merged);
   the sections this branch adds to it are kept in [`../RemixApiSurface_gta4.md`](../RemixApiSurface_gta4.md).
4. New `rtx.*` options (listed per workstream) show up in `RtxOptions.md` after one game run with
   `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1`; this branch does not regenerate that file.
5. The bridge client and server are a protocol pair: whenever a workstream touches `bridge/`, deploy both.

Dates are the dates of the original work; the history of this branch is squashed.

---

## Workstream - Colormap-keyed API materials (fork - 2026-10-04)

API `0.1000.2`: `remixapi_MaterialInfoGameTexturesEXT { sType, pNext, colorTexture }`.
An opaque API material without `albedoTexture` uses the game's D3D9 colormap as
albedo and its image hash as replacement / capture / category key, like a
non-replaced D3D9 draw of that texture. Fork-owned logic lives in
`rtx_fork_game_textures.{h,cpp}` (API thread texture resolution, teardown) and
`rtx_fork_game_textures_keyed.cpp` (render thread keyed materials, D3D9-free so
the unit tests link without the d3d9 sources).

- **`public/include/remix/remix_c.h`** - inline. *`REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT = 29`, `remixapi_MaterialInfoGameTexturesEXT`, version `0.1000.2`; the PATCH history comment above `REMIXAPI_VERSION_MAJOR`.*
- **`public/include/remix/remix.h`** - inline. *`MaterialInfoGameTexturesEXT` wrapper; `sizeof(remixapi_Interface)` stays 328.*
- **`src/dxvk/rtx_render/rtx_remix_specialization.inl`** - inline, 3 LOC. *Registers `remixapi_MaterialInfoGameTexturesEXT` (AllTypes / ToEnum / Root = `remixapi_MaterialInfo`).*
- **`src/dxvk/rtx_render/rtx_remix_api.cpp`** - hooks.
  - `remixapi_CreateMaterial`: `fork_hooks::resolveMaterialGameTextures` on the API thread; the finalized material passes through `fork_hooks::applyMaterialGameTextures` in the CS lambda.
  - `remixapi_DestroyMaterial`: `fork_hooks::forgetMaterialGameTextures` in the CS lambda.
  - `remixapi_Shutdown`: `fork_hooks::shutdownGameTextures(s_dxvkDevice)` next to `shutdownCallbacks()`.
  - `remixapi_DestroyMesh`: erases a not yet flushed `CreateMeshBatched` entry of the same handle from `s_pendingMeshCreates` before emitting the destroy (a pending create registered afterwards brought the mesh back).
- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - hook call sites changed in `SceneManager::submitExternalDraw`: `externalDrawMaterialReplacement` and `externalDrawTextureCategories` take the submesh's material handle, and `setHashOverride` uses `fork_hooks::externalMaterialKey(handle, *material, ...)` (equals `material->getHash()` for materials without the extension).
- **`src/d3d9/d3d9_device.cpp`** - hook. *`fork_hooks::shutdownGameTextures(this)` in `D3D9DeviceEx::~D3D9DeviceEx` after `SynchronizeCsThread()`, plus the `rtx_fork_hooks.h` include.*
- **`src/d3d9/d3d9_rtx.cpp`** - inline, 1 LOC. *`rtx_fork_hooks.h` include.*
- **`src/dxvk/meson.build`** - inline. *Registers the three fork files.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`, `rtx_fork_submit.cpp`** - fork-owned. *The submit hooks draw colormap-keyed materials like D3D9 draws (replacement merged over the game state, or the legacy-defaults material) and use the key for categories.*
- **`bridge/src/{client/remix_api.cpp, server/main.cpp, util/util_remixapi.{h,cpp}}`** - inline. *`MaterialInfoGameTexturesEXT` serialization (texture proxy sent as D3D object id, resolved via `gpD3DResources`), `CreateMesh` pNext loop fix.*

---

## Workstream - Per-draw D3D overrides for API instances (fork - 2026-10-11)

API `0.1000.3`: `remixapi_InstanceInfoD3DOverridesEXT` carries the per-draw
values the D3D9 path reads from unused render states (GTA4 compatibility) so
API draws can replace D3D9 draws without losing wetness / emissive / categories.

- **`public/include/remix/remix_c.h`** - inline. *`REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_D3D_OVERRIDES_EXT = 30`, `remixapi_InstanceInfoD3DOverridesEXT`, version `0.1000.3`.*
- **`public/include/remix/remix.h`** - inline. *`InstanceInfoD3DOverridesEXT` wrapper.*
- **`src/dxvk/rtx_render/rtx_remix_specialization.inl`** - inline, 3 LOC. *Registers the struct (AllTypes / ToEnum / Root = `remixapi_InstanceInfo`).*
- **`src/dxvk/rtx_render/rtx_remix_api.cpp`** - inline tweak at `RemixAPIPrivateAccessor::toRtDrawState` (after the `remixapi_InstanceInfoBlendEXT` block), 15 LOC. *Copies the extension into the `LegacyMaterialData::remix*FromD3D` fields that `d3d9_rtx_utils.cpp` fills for D3D9 draws; the GTA4 blocks in `rtx_scene_manager.cpp` and `rtx_types.h` then apply unchanged.*
- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - hook call sites in `SceneManager::submitExternalDraw`: `externalDrawMaterialReplacement` and `externalMaterialKey` take the draw's `LegacyMaterialData` so the hash seed / override applies to API draws.
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`, `rtx_fork_submit.cpp`, `rtx_fork_game_textures_keyed.cpp`** - fork-owned. *`applyD3DHashOverrides` (same rule as `LegacyMaterialData::updateCachedHash`) used for replacement lookups and the hash override of API draws; texture categories keep keying off the image hash like D3D9 draws.*
- **`bridge/src/{client/remix_api.cpp, server/main.cpp, util/util_remixapi.{h,cpp}}`** - inline. *Serialization of the new `DrawInstance` extension.*

---

## Workstream - External draws behave like D3D9 draws (fork - 2026-10-04 / 2026-10-08)

Replacement instances of API meshes, object picking of API draws.

- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - hooks in `SceneManager::submitExternalDraw` (the Remix Plus hook list for this function grows from 4 to 7 call sites):
  - replacement branch, around `drawReplacements` → `fork_hooks::externalDrawPrepareReplacementInstance` / `fork_hooks::externalDrawFinishReplacementInstance`. *Prepare clears the ReplacementInstance when its `activeReplacements` is not this bucket (mirrors the D3D9 `activeReplacementsMatch` → `clear()`, so `setup()` sizes prims for the bucket and records it for hot reload), resolves submesh 0's API / keyed / USD-replaced material (hash override, texture categories, object picking) and returns it as the `includeOriginal` material, and warns once when an `includeOriginal` bucket targets a multi-submesh API mesh. Finish sets `frameLastSeen`, `categoryFlags` and `isSkinned` like `submitDrawState`, so the RI is not garbage collected every frame (kept originals vanished, added references were orphaned). Replaces the call site's former default `LegacyMaterialData`.*
  - after the replacement branch, before the submesh loop → `fork_hooks::externalDrawReleaseReplacementInstance`. *Clears the RI when it still holds a replacement bucket that no longer applies (override removed, replacements disabled), releasing the replacement prims.*
  - `fork_hooks::externalDrawObjectPicking` (existing Remix Plus hook, fork-owned body changed): *draws without `remixapi_InstanceInfoObjectPickingEXT` get a per-submesh `drawCallID` from `0x80000000` up (above D3D9 draw IDs) while picking is active, so dev-menu texture picking / highlighting resolves them; they kept id 0 before.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`, `rtx_fork_submit.cpp`** - fork-owned. *The three hooks above.*
---

## src/dxvk/rtx_render/rtx_texture_manager.cpp / rtx_texture_manager.h (fork - 2026-10-04)

- **Inline tweak** at `SamplerFeedback::fetchNoisyMipCounts` (+ `m_cachedAssetMipShift` member, alloc/free in ctor/dtor) — ~20 LOC.
  *Sampler feedback reports the accessed mip of a 4096-wide texture (`calcMipLevelAccessedForSamplerFeedback`), but upstream subtracted it from each asset's own mip count, so a 512px replacement streamed 3 levels too low (64px). The accessed level is now shifted by `ceil(log2(4096 / maxDim))` per texture, for the stamp's own texture and for every related (normal/roughness/...) texture, instead of copying the stamp texture's mip count to related textures. Fixes blurry normal/roughness on partial replacements without a replacement albedo. Upstream bugfix candidate.*
