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
---

## Workstream - Constant-rate particle spawn needs a live emitter (fork - 2026-10-04)

Fixes a GPU page fault in `particle_system_spawn` (Aftermath read translation
error -> `VK_ERROR_DEVICE_LOST`) when an API client destroys its meshes and
recreates the same map a few frames later. `RtxParticleSystemManager::simulate`
forced `spawnParticleCount = maxNumParticles` every frame for constant-rate
systems (`spawnRatePerSecond >= maxNumParticles`), whether or not an emitter
spawned this frame. The system outlives its emitters for
`spawnBurstDuration + maxTimeToLive`, and during that window the kernel read the
last-written context map and stale `GpuSpawnContext`s (`writeSpawnContextsToGpu`
returns early when nothing spawned). Their bindless buffer slots belonged to
BLAS entries that GC had unregistered; `RetainedBufferTable` recycles freed
slots, so the new map's smaller buffers sat behind the old indices while
`numTriangles` / offsets still described the old mesh.

- **`src/dxvk/rtx_render/rtx_fork_particle_spawn.cpp`** - fork-owned file. *`fork_hooks::constantRateSpawnCount()`: full respawn only when this frame's contexts registered and resolved, otherwise 0 (existing particles keep evolving).*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`** - fork-owned change. *Declares `constantRateSpawnCount`.*
- **`src/dxvk/rtx_render/rtx_particle_system.cpp`** - fork-touchpoint hook. *One line in `simulate`'s constant-rate branch dispatches into the hook.*
---

## Workstream - Spawn occlusion traces only a fresh TLAS (fork - 2026-10-04)

Fixes the remaining `particle_system_spawn` page fault on map reload, seen only
with precipitation (the one system with `traceSpawnOcclusion`). The spawn kernel
traces `getTLAS(Opaque).accelStructure`, which at `simulate()` time is whatever
TLAS was built last, and `sceneTlasValid` only checked it was non-null.
`SceneManager::clear` (camera cut on the respawn teleport, asset reload,
last-external-mesh teardown) and the BLAS GC free the BLASes without touching
the TLAS. Frames without instances skip the TLAS build entirely. The next frame
the precipitation emitter is drawn, the kernel traverses a TLAS whose instance
descriptors point at freed BLAS memory.

- **`src/dxvk/rtx_render/rtx_fork_particle_spawn.cpp`** - fork-owned file. *`onOpaqueTlasBuilt` / `onAccelStructuresCleared` / `isPreviousOpaqueTlasTraceable`: tracing is allowed only when the Opaque TLAS was built on the previous frame and no clear has happened since.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`** - fork-owned change. *Declares the three hooks.*
- **`src/dxvk/rtx_render/rtx_accel_manager.cpp`** - fork-touchpoint hooks. *`rtx_fork_hooks.h` include; `onOpaqueTlasBuilt` after `internalBuildTlas<Tlas::Opaque>` in `buildTlas`; `onAccelStructuresCleared` at the end of `AccelManager::clear`.*
- **`src/dxvk/rtx_render/rtx_particle_system.cpp`** - fork-touchpoint inline tweak. *`setupConstants`'s `sceneTlasValid` and the `s_spawnTraceTlasValid` diagnostic also require `isPreviousOpaqueTlasTraceable`.*
---

## Workstream - Skinned API mesh serialization (fork - 2026-10-12)

The bridge sized the blend weight and blend index arrays of a skinned
`remixapi_MeshInfoSurfaceTriangles` as vertex count x total element count x
bones per vertex, so any skinned API mesh over-read the client buffer and
over-allocated the IPC data. Both arrays are now sized by their total element
count, as `remix_c.h` documents.

- **`bridge/src/util/util_remixapi.cpp`** - inline. *Blend weight / index sizes of skinned mesh surfaces (size calculation, serialize, deserialize).*
---

## Workstream - SetupCamera through the bridge (fork - 2026-10-12)

The bridge client left `remixapi_Interface.SetupCamera` empty, so a 32-bit
client whose frame is drawn entirely through `DrawInstance` never gave the
runtime a camera (the D3D9 path only reads it while committing a ray traced
draw). The camera info is sent field by field, the optional
`remixapi_CameraInfoParameterizedEXT` included.

- **`bridge/src/util/util_commands.h`** - inline. *`RemixApi_SetupCamera` command and its name.*
- **`bridge/src/client/remix_api.cpp`** - inline. *`remixapi_SetupCamera` stub, interface slot assigned.*
- **`bridge/src/server/main.cpp`** - inline. *`RemixApi_SetupCamera` handler calling `g_remix.SetupCamera`.*
---

## Workstream - Captures with skinned API meshes (fork - 2026-10-12)

Capturing a scene with skinned `DrawInstance` meshes crashed in
`GameCapturer::captureMeshBlending`: the mesh took `bonesPerVertex` from the
instance skinning state, which API instances fill before their mesh geometry
is attached (always 0), and the weight loop ran to `bonesPerVertex - 1` as
`size_t`. The count now comes from the geometry the blend buffers belong to;
D3D9 fixed function skinning copies the same value into both.

- **`src/dxvk/rtx_render/rtx_game_capturer.cpp`** - inline. *`captureMesh` reads `bonesPerVertex` from the raster geometry.*
---

## Workstream - CS thread cost of API instances (fork - 2026-10-12)

A Tracy capture (GTA IV through the bridge, ~3,800 API instances per frame) showed
the dxvk-cs thread as the frame limiter: ~9 ms of per-draw processing and ~7 ms of
scene build per frame.

External draws never took the preserve path D3D9 draws take for unchanged
draws (`SceneManager::submitDrawState`, `usePreservePath`); every API instance
ran `processDrawCallState` (draw call cache, instance update, surface material)
every frame. `fork_hooks::externalDrawTryPreserve` (rtx_fork_submit.cpp) takes
the preserve path for a one-submesh external draw found by its exact identity
when nothing it depends on changed (`rtx.preserveUnchangedExternalDraws`).

`RtxGeometryUtils::dispatchSkinning` skinned draws of up to 256 vertices on the
CPU and wrote every vertex to the GPU with its own buffer update (16k
`vkCmdUpdateBuffer` per frame). `rtx.skinningCpuVertexLimit` (default 0) sends
everything through the compute shader. `mergeInstancesIntoBlas` then put every
small skinned mesh into the merged BLAS, where one skinned part updated every
frame re-sorted and rebuilt the whole bucket (~6 ms per frame on the CS thread
with ~9,000 instances); meshes with bones or drawn through a vertex shader keep
their own BLAS (`rtx.dynamicBlasForAnimatedMeshes`, on by default).

- **`src/dxvk/rtx_render/rtx_fork_submit.cpp`** - fork-owned file. *`externalDrawTryPreserve`, `rtx.preserveUnchangedExternalDraws`.*
- **`src/dxvk/rtx_render/rtx_fork_skinning.cpp`** - fork-owned file. *`rtx.skinningCpuVertexLimit`, `skinningCpuVertexLimit()`; `rtx.dynamicBlasForAnimatedMeshes`, `keepAnimatedMeshesOutOfMergedBlas()`.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`** - fork-owned change. *Declares the hooks.*
- **`src/dxvk/meson.build`** - fork-owned change. *Adds `rtx_fork_skinning.cpp`.*
- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - hook. *`submitExternalDraw`: `externalDrawTryPreserve` after the ReplacementInstance lookup, `RTX_OPTION_INVALIDATION_SCOPE` over the dynamic path (as the D3D9 path).*
- **`src/dxvk/rtx_render/rtx_scene_manager.h`** - inline. *Forward declaration and friend for `externalDrawTryPreserve`.*
- **`src/dxvk/rtx_render/rtx_types.h`** - inline. *`ReplacementInstance::externalMaterialIdentityHash` / `externalReplacementMaterial`.*
- **`src/dxvk/rtx_render/rtx_geometry_utils.cpp`** - hook. *`dispatchSkinning`: CPU vertex limit from `fork_hooks::skinningCpuVertexLimit()`.*
- **`src/dxvk/rtx_render/rtx_accel_manager.cpp`** - hook. *`mergeInstancesIntoBlas`: `forceMergedBlas` no longer merges small meshes animated every frame (`fork_hooks::keepAnimatedMeshesOutOfMergedBlas`).*
---

## Workstream - CS thread cost of API instances, second pass (fork - 2026-10-12)

A second capture (~9,800 instances): the preserve path itself cost ~1.3 us per
instance (`submitExternalDraw` 8.6 ms self), the BLAS bucket cache check 6.9 ms
and the texture garbage collection 1.3 ms per frame; every frame the opacity
micromap manager baked something, the next frame marked all BLAS buckets dirty.

The preserve hook now refreshes the instance the way `preserveReplacementInstance`
does minus the `DrawCallState` copy into the `BlasEntry` input (that re-points
billboard creation at this frame's vertex memory, which only D3D9 draws need; an
api mesh's geometry is owned by the replacer), without the geometry override and
without the per frame material replacement lookups the D3D9 path does not do
either. The sampler and view transform setup of `submitExternalDraw` moved below
the hook: only the dynamic path uses them. The per draw picking ids of external
draws are handed out on the dynamic path, so the preserve hook steps aside while
picking is active (preserved instances kept id 0, the texture picker outlined all
static geometry as one object).

- **`src/dxvk/rtx_render/rtx_fork_submit.cpp`** - fork-owned file. *`externalDrawTryPreserve` refresh path, picking check.*
- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - hook. *`submitExternalDraw`: sampler / view transform setup after `externalDrawTryPreserve`.*
- **`src/dxvk/rtx_render/rtx_fork_texture_streaming.cpp`** - fork-owned file. *`rtx.textureGcFrameInterval` (default 2), `textureGarbageCollectionDue()`.*
- **`src/dxvk/rtx_render/rtx_texture_manager.cpp`** - hook. *Sampler feedback readback: `garbageCollection` only when `fork_hooks::textureGarbageCollectionDue(curframe)`.*
- **`src/dxvk/rtx_render/rtx_fork_skinning.cpp`** - fork-owned file. *`rtx.opacityMicromap.rebindFrameInterval` (default 16), `ommRebindDue()`.*
- **`src/dxvk/rtx_render/rtx_accel_manager.cpp`** - inline. *`mergeInstancesIntoBlas`: the per frame `unordered_set` of live instances is a sorted static scratch vector + `binary_search`; the two `hasNewlyBuiltOmms` checks go through `fork_hooks::ommRebindDue`, which latches baked micromaps and binds them at most every N frames.*
- **`src/dxvk/meson.build`** - fork-owned change. *Adds `rtx_fork_texture_streaming.cpp`.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`** - fork-owned change. *Declares the hooks.*

---

## Workstream - AnimatedWater shoreline fade (fork - 2026-10-13)

Removes the bright seam and the hard edge where translucent water meets
opaque geometry. Near the shore, PSTR shows the terrain under shallow water
with almost no absorption, and the water's Fresnel reflection is added on
top. That makes the shallow strip brighter than both dry ground and deep
water, and DLSS-RR / NRD smear the reflection onto the dry side, because
the guide buffers describe the continuous terrain. At each resolved
AnimatedWater translucent hit, `forkWaterShoreFade` traces a short
opaque-only probe straight down for the water depth and offsets it by a
pseudo-height (the signed slope of the animated normal). The resulting
fade drives base reflectivity, normal perturbation (blended toward the
view direction so grazing Schlick Fresnel vanishes too), IOR, thin-wall
absorption, diffuse layer and emission to zero toward the shore.

The depth probes force non-opaque traversal and commit only `isStatic && isFullyOpaque`
surfaces, so moving and skinned objects are skipped (a body standing in the water would
otherwise read as shallow ground directly above it); those are faded by a soft-intersection
probe along the incoming ray (`rtx.water.objectFadeWidth`, smoothstep). When the center probe
misses, two more probes offset horizontally by `rtx.water.shoreProbeSpread` plus the pixel
footprint are traced and the shallowest hit is used, so cracks / T-junctions between merged
API chunks do not read as deep water (a bright zig-zag line). Inside the fade, thick water is
a thin layer as thick as the water is deep (or the object distance), absorbing with the
material's coefficient scaled by the fade (`rtx.water.shoreThinLayer`); thick water otherwise
absorbs along the ray inside its volume, which the fade does not reach, and faded shallow
water stayed as dark as deep water. Defaults are tuned for GTA IV (shore fade width 0.2).
Plugin-facing options: `rtx.water.*`, see docs/RemixWaterAPI_gta4.md.

- **`src/dxvk/shaders/rtx/algorithm/rtx_fork_water_shore.slangh`** - fork-owned file. *`forkWaterShoreFade`: depth probes + pseudo-height fade applied to the TranslucentSurfaceMaterialInteraction; returns true when fully faded.*
- **`src/dxvk/shaders/rtx/algorithm/resolve.slangh`** - fork-touchpoint hook. *Includes the fork header; in `resolveVertex`'s translucent branch, dispatches `forkWaterShoreFade` guarded by `cb.waterShoreFadeEnable` and AnimatedWater or the `D3D_TRANSLUCENT_MODIFIER_WATER_SHORELINE` material flag.*
- **`src/dxvk/rtx_render/rtx_materials.h`** - inline. *RS149 bit 3 is `REMIX_MODIFIER_FROM_D3D_WATER_SHORELINE` (was the unused decal dirt bit).*
- **`src/dxvk/shaders/rtx/utility/shared_constants.h`** - inline. *`D3D_TRANSLUCENT_MODIFIER_WATER_SHORELINE`.*
- **`src/dxvk/rtx_render/rtx_scene_manager.cpp`** - inline. *Translucent `createSurfaceMaterial` sets `D3D_TRANSLUCENT_MODIFIER_WATER_SHORELINE` from RS149 bit 3, so the GTA4 mod can request the fade per draw.*
- **`src/dxvk/shaders/rtx/pass/raytrace_args.h`** - fork-touchpoint inline tweak. *`waterShoreFadeEnable`, `waterShoreFadeDistance`, `waterShoreCutDepth`, `waterShoreFadeWidth`, `waterShoreHeightScale`, `waterShoreProbeSpread`, `waterObjectFadeWidth`, `waterShoreThinLayer` appended at the END of RaytraceArgs (no existing offsets move).*
- **`src/dxvk/rtx_render/rtx_fork_water.h` / `.cpp`** - fork-owned files. *`WaterOptions` (`rtx.water.*`), constant fill (probe range raised to cover the full fade band) and ImGui widgets.*
- **`src/dxvk/rtx_render/rtx_fork_hooks.h`** - fork-owned change. *Declares `fillWaterShaderParams` and `showWaterShoreSettings`; forward-declares `RaytraceArgs`.*
- **`src/dxvk/rtx_render/rtx_context.cpp`** - fork-touchpoint hook. *One line after `TranslucentMaterialOptions::fillShaderParams` dispatches `fillWaterShaderParams`.*
- **`src/dxvk/imgui/dxvk_imgui.cpp`** - fork-touchpoint hook. *One line at the end of Material Options -> PBR Material Modifiers -> Translucent dispatches `showWaterShoreSettings`.*
- **`src/dxvk/meson.build`** - fork-touchpoint inline tweak. *Registers `rtx_fork_water.cpp/.h`.*
- **`docs/RemixWaterAPI_gta4.md`**, **`docs/RemixApi_gta4.md`** - spoke page + Convention namespaces row for `rtx.water.*`.

---

## Workstream - Transmission origin clamp at translucent/opaque contact (fork - 2026-10-13)

Fixes a sawtooth of bright pixels exactly where a translucent surface intersects opaque geometry
(water meeting the shore, also in stock Remix). Rays that penetrate a surface are offset along the
flipped triangle normal (`rayOffsetSurfaceOriginHelper`, ray.slangh). In the thin wedge next to the
contact line, the opaque surface lies closer behind the hit than that offset, so the transmission
origin lands behind it. With thin-walled translucents the ray stays outside the medium and culls
back faces, so it passes under the terrain and picks up sky. With thick ones it hits the terrain
from below. The wedge is sub-pixel, but the leaked radiance is high-dynamic-range, so it survives
anti-aliasing as a line. `forkClampTransmissionRay` traces an opaque-only probe across the offset
for translucent penetration; if it hits, the origin is moved to half the hit distance, in front
of the opaque surface. The shore fade also skips fully faded water hits outright, continuing the
ray from a clamped origin.

- **`src/dxvk/shaders/rtx/algorithm/rtx_fork_transmission_origin.slangh`** - fork-owned file. *`forkClampPenetratingOrigin`, `forkClampTransmissionRay`.*
- **`src/dxvk/shaders/rtx/algorithm/resolve.slangh`** - fork-touchpoint hook. *Includes the header; the shore fade dispatch continues the ray (`resolveVertexFinalContinue` + clamped origin) when `forkWaterShoreFade` reports the water fully faded.*
- **`src/dxvk/shaders/rtx/algorithm/geometry_resolver.slangh`** - fork-touchpoint hooks. *The three transmission PSR spawns (first hit, PSR continuation, PSR prepare) wrap `rayCreateDirection` in `forkClampTransmissionRay`.*
- **`src/dxvk/shaders/rtx/algorithm/integrator.slangh`** - fork-touchpoint hook. *`sampleDirection` wraps `rayCreateDirection` in `forkClampTransmissionRay`.*
