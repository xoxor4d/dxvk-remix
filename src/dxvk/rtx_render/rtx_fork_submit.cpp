// src/dxvk/rtx_render/rtx_fork_submit.cpp
//
// Fork-owned file. Contains the implementations of fork_hooks:: functions
// for the SceneManager::submitExternalDraw path, lifted from
// rtx_scene_manager.cpp during the 2026-04-18 fork touchpoint-pattern refactor.
//
// See docs/fork-touchpoints.md for the full fork-hooks catalogue.
//
// NOTE: externalDrawObjectPicking accesses SceneManager::m_drawCallMeta, which
// is a private member. This file requires that SceneManager declare
// fork_hooks::externalDrawObjectPicking as a friend, OR that DrawCallMetaInfo
// and m_drawCallMeta be made accessible via a public accessor. Flagged for
// Phase 4 build-validation fixup.

#include "rtx_fork_hooks.h"

#include "rtx_asset_replacer.h"   // AssetReplacer, AssetReplacement
#include "rtx_fork_game_textures.h" // game_textures::findMaterialKey, keyedDrawMaterial
#include "rtx_options.h"          // RtxOptions::*, fast_unordered_set, InstanceCategories
#include "rtx_option_manager.h"   // RtxOptionManager::isDrawcallTranslationInvalid
#include "rtx_instance_manager.h" // RtInstance
#include "rtx_texture_manager.h"  // RtxTextureManager::getTextureCacheGeneration
#include "rtx_preserved_object_picking.h" // preserveInstancesWithObjectPicking
#include "rtx_scene_manager.h"    // SceneManager, DrawCallMetaInfo

#include "dxvk_device.h"          // DxvkDevice::getCommon()->getResources()

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include <unordered_map>
#include <unordered_set>

namespace dxvk {
namespace {

  // Picking values for API draws without remixapi_InstanceInfoObjectPickingEXT:
  // above the D3D9 draw call IDs (0, 1, ... per frame) and below the default
  // picking clear value (UINT32_MAX). Render thread only.
  constexpr ObjectPickingValue kFirstAutoPickingValue = 0x80000000u;
  ObjectPickingValue s_nextAutoPickingValue = kFirstAutoPickingValue;
  ObjectPickingValue s_lastAutoPickingValue = 0;
  uint32_t s_autoPickingFrameId = kInvalidFrameIndex;

  // API mesh handle hash -> remixapi_MeshInfoReplacementHashEXT key. Render thread only.
  std::unordered_map<XXH64_hash_t, XXH64_hash_t> s_externalMeshReplacementHashes;

  class GeometryHashLogOptions {
    RTX_OPTION("rtx", bool, debugLogGeometryHashes, false,
               "Logs the geometry asset hash and its position / index / descriptor parts once per D3D9 mesh, and the replacement key once per "
               "API mesh, each with whether a USD replacement exists. For checking that API meshes carry the asset hash of the D3D9 draw.");
  };

  // Hashes already logged since the option was enabled. Render thread only.
  std::unordered_set<XXH64_hash_t> s_loggedD3D9GeometryHashes;
  std::unordered_set<XXH64_hash_t> s_loggedExternalMeshes;

  void logExternalMeshReplacementKey(XXH64_hash_t meshHash, XXH64_hash_t key, XXH64_hash_t materialKey, bool replaced) {
    if (!GeometryHashLogOptions::debugLogGeometryHashes()) {
      if (!s_loggedExternalMeshes.empty()) {
        s_loggedExternalMeshes.clear();
      }
      return;
    }

    if (s_loggedExternalMeshes.insert(meshHash).second) {
      Logger::info(str::format("[GeometryHash] API mesh 0x", std::hex, meshHash,
                               key != meshHash ? " key 0x" : " no key, handle hash 0x", key,
                               " material key 0x", materialKey, std::dec,
                               replaced ? " replaced" : ""));
    }
  }

} // anonymous namespace

namespace fork_hooks {

  // ---------------------------------------------------------------------------
  // externalDrawMeshReplacement
  //
  // Checks for a USD mesh/light replacement keyed on externalMeshAssetHash,
  // same as the D3D9 draw-call path. Returns the replacement bucket if one is
  // found (caller must call drawReplacements and then return), or null if no
  // replacement exists.
  // ---------------------------------------------------------------------------
  std::shared_ptr<const ReplacementBucket> externalDrawMeshReplacement(
      AssetReplacer& replacer, XXH64_hash_t meshHash,
      remixapi_MaterialHandle materialHandle, const LegacyMaterialData& drawMaterialData) {
    XXH64_hash_t materialKey = kEmptyHash;
    if (s_externalMeshReplacementHashes.count(meshHash) != 0) {
      if (const MaterialData* material = replacer.accessExternalMaterial(materialHandle)) {
        materialKey = externalMaterialKey(materialHandle, *material, drawMaterialData);
      }
    }

    const XXH64_hash_t key = externalMeshAssetHash(meshHash, materialKey);
    auto replacements = replacer.getReplacementsForMesh(key);
    logExternalMeshReplacementKey(meshHash, key, materialKey, replacements != nullptr);
    return replacements;
  }

  void logD3D9GeometryHash(const DrawCallState& drawCall, XXH64_hash_t assetHash, bool replaced) {
    if (!GeometryHashLogOptions::debugLogGeometryHashes()) {
      if (!s_loggedD3D9GeometryHashes.empty()) {
        s_loggedD3D9GeometryHashes.clear();
      }
      return;
    }

    if (!s_loggedD3D9GeometryHashes.insert(assetHash).second) {
      return;
    }

    const RasterGeometry& geometry = drawCall.getGeometryData();
    Logger::info(str::format("[GeometryHash] D3D9 hash 0x", std::hex, assetHash,
                             " positions 0x", geometry.hashes[HashComponents::VertexPosition],
                             " indices 0x", geometry.hashes[HashComponents::Indices],
                             " descriptor 0x", geometry.hashes[HashComponents::GeometryDescriptor],
                             " material 0x", drawCall.getMaterialData().getHash(), std::dec,
                             " index count ", geometry.indexCount,
                             " vertex count ", geometry.vertexCount,
                             " position stride ", geometry.positionBuffer.stride(),
                             " format ", static_cast<uint32_t>(geometry.positionBuffer.vertexFormat()),
                             replaced ? " replaced" : ""));
  }

  void setExternalMeshReplacementHash(XXH64_hash_t meshHash, XXH64_hash_t replacementHash) {
    if (replacementHash != 0) {
      s_externalMeshReplacementHashes[meshHash] = replacementHash;
    } else {
      s_externalMeshReplacementHashes.erase(meshHash);
    }
  }

  XXH64_hash_t externalMeshAssetHash(XXH64_hash_t meshHash, XXH64_hash_t materialKey) {
    const auto it = s_externalMeshReplacementHashes.find(meshHash);
    return it != s_externalMeshReplacementHashes.end() ? it->second ^ materialKey : meshHash;
  }

  MaterialData externalDrawPrepareReplacementInstance(
      DxvkDevice& device,
      AssetReplacer& replacer,
      SceneManager& scene,
      DrawCallState& drawCall,
      remixapi_MaterialHandle handle,
      size_t submeshCount,
      const std::shared_ptr<const ReplacementBucket>& pReplacements,
      ReplacementInstance& replacementInstance) {
    // An RI first drawn without this bucket (no replacement yet, or a stale or
    // swapped one) has prims sized for that state; drawReplacements only runs
    // setup() on an empty RI.
    if (replacementInstance.activeReplacements.get() != pReplacements.get()) {
      replacementInstance.clear();
    }

    if (submeshCount > 1) {
      for (const auto& replacement : pReplacements->replacements) {
        if (replacement.includeOriginal) {
          ONCE(Logger::warn("[RTX-Mesh] A replacement with includeOriginal targets an API mesh with more than one submesh; only the first submesh is drawn as the original."));
          break;
        }
      }
    }

    XXH64_hash_t textureHash = 0;
    const MaterialData* material = replacer.accessExternalMaterial(handle);
    std::shared_ptr<MaterialData> replacementMaterialKeepAlive;
    if (material != nullptr) {
      replacementMaterialKeepAlive = externalDrawMaterialReplacement(replacer, handle, material, drawCall.getMaterialData());

      drawCall.modifyMaterialData().setHashOverride(externalMaterialKey(handle, *material, drawCall.getMaterialData()));

      externalDrawTextureCategories(handle, material, drawCall, textureHash);
    }

    externalDrawObjectPicking(device, drawCall, textureHash, scene);

    return material != nullptr ? *material : MaterialData(LegacyMaterialData::createDefault());
  }

  void externalDrawFinishReplacementInstance(
      DxvkDevice& device,
      const DrawCallState& drawCall,
      ReplacementInstance& replacementInstance) {
    replacementInstance.frameLastSeen = device.getCurrentFrameId();
    replacementInstance.categoryFlags = drawCall.getCategoryFlags().raw();
    replacementInstance.isSkinned = drawCall.getSkinningState().numBones > 0;
  }

  void externalDrawReleaseReplacementInstance(ReplacementInstance& replacementInstance) {
    if (replacementInstance.activeReplacements != nullptr) {
      replacementInstance.clear();
    }
  }

  // ---------------------------------------------------------------------------
  // externalDrawTryPreserve
  //
  // The identity hash of an external draw covers mesh, material, bones,
  // categories and transform, so a ReplacementInstance found by its exact
  // identity (dirty flags clear) is the same draw as last frame. The D3D9 path
  // then refreshes the instance instead of re-running processDrawCallState;
  // external draws always ran the dynamic path. The conditions mirror
  // SceneManager::submitDrawState's usePreservePath. The refresh is what
  // preserveReplacementInstance does minus the BlasEntry input copy: that
  // re-points billboard / beam creation at this frame's vertex memory, which
  // D3D9 draws need because theirs gets recycled; an api mesh's geometry is
  // owned by the replacer and does not move.
  // ---------------------------------------------------------------------------
  namespace {
    struct ExternalDrawOptions {
      RTX_OPTION("rtx", bool, preserveUnchangedExternalDraws, true,
                 "Remix API instances unchanged since the last frame (same mesh, transform, material, bones and categories) "
                 "keep their instance instead of running the per draw scene processing again, like unchanged D3D9 draws.");
    };
  }

  bool externalDrawTryPreserve(SceneManager& scene, const Rc<DxvkContext>& ctx, ExternalDrawState& state,
                               const std::shared_ptr<const std::vector<RasterGeometry>>& submeshes,
                               ReplacementInstance& ri) {
    if (!RtxOptions::enablePreservePath() || !ExternalDrawOptions::preserveUnchangedExternalDraws()) {
      return false;
    }
    if (submeshes->size() != 1) {
      return false;
    }

    DxvkDevice* device = ctx->getDevice().ptr();
    // externalDrawObjectPicking hands out the per draw picking ids on the dynamic path
    if (device->getCommon()->getResources().getRaytracingOutput().m_primaryObjectPicking.isValid()) {
      return false;
    }
    const uint32_t frame = device->getCurrentFrameId();
    const bool seenThisFrame = ri.frameLastSeen == frame;
    const RasterGeometry& submesh = (*submeshes)[0];
    const XXH64_hash_t materialIdentity = state.drawCall.getMaterialData().computeIdentityHash();

    RtInstance* instance = ri.prims.size() == 1 ? ri.prims[0].getInstance() : nullptr;
    const bool unchanged =
      instance != nullptr && instance->getBlas() != nullptr &&
      instance->getBlas()->frameLastUpdated != frame &&
      !seenThisFrame && ri.dirtyFlags.isClear() &&
      !RtxOptionManager::isDrawcallTranslationInvalid() &&
      !state.optionalParticleDesc.has_value() &&
      !state.categories.test(InstanceCategories::ParticleEmitter) &&
      ri.activeReplacements == nullptr &&
      externalDrawMeshReplacement(*scene.m_pReplacer, reinterpret_cast<XXH64_hash_t>(state.mesh), submesh.externalMaterial, state.drawCall.getMaterialData()) == nullptr &&
      ri.externalMaterialIdentityHash == materialIdentity &&
      device->getCommon()->getTextureManager().getTextureCacheGeneration() == scene.m_textureCacheGenerationValidForPreserve;

    if (!unchanged) {
      // the dynamic path runs next with exactly this material state
      if (!seenThisFrame) {
        ri.externalMaterialIdentityHash = materialIdentity;
      }
      return false;
    }

    preserveInstancesWithObjectPicking(
      ri.prims,
      state.drawCall.drawCallID,
      [&](RtInstance& inst) {
        scene.preserveInstance(inst, &state.drawCall);
        scene.m_instanceManager.preserveInstance(inst, state.drawCall, nullptr);
      },
      [&] {
        scene.trackObjectPickingMeta(state.drawCall, state.drawCall.drawCallID);
      });
    ri.recalculateBoundingBox(state.drawCall.getTransformData().objectToWorld, &submesh.boundingBox);
    ri.frameLastSeen = frame;
    return true;
  }

  // ---------------------------------------------------------------------------
  // externalDrawMaterialReplacement
  //
  // Checks for a USD material replacement via getReplacementMaterial() and
  // updates the caller's material pointer in-place if one is found. Materials
  // keyed by a game colormap look up by that key and draw like D3D9 draws (see
  // rtx_fork_game_textures.cpp).
  // ---------------------------------------------------------------------------
  std::shared_ptr<MaterialData> externalDrawMaterialReplacement(
      AssetReplacer& replacer, remixapi_MaterialHandle handle, const MaterialData*& material,
      const LegacyMaterialData& drawMaterialData) {
    const XXH64_hash_t key = game_textures::findMaterialKey(handle);
    if (key != kEmptyHash) {
      std::shared_ptr<MaterialData> keyed = game_textures::keyedDrawMaterial(
        handle, replacer.getReplacementMaterial(applyD3DHashOverrides(drawMaterialData, key)));
      if (keyed != nullptr) {
        material = keyed.get();
      }
      return keyed;
    }

    // Check for material replacement (matches the D3D9 draw path behavior).
    // Upstream hands replacement materials out by shared_ptr so a hot reload
    // cannot free them under a live draw; return the owner to the caller so
    // `material` stays valid for the rest of the submesh iteration.
    std::shared_ptr<MaterialData> pReplacementMaterial =
      replacer.getReplacementMaterial(applyD3DHashOverrides(drawMaterialData, material->getHash()));
    if (pReplacementMaterial != nullptr) {
      material = pReplacementMaterial.get();
    }
    return pReplacementMaterial;
  }

  // ---------------------------------------------------------------------------
  // externalDrawTextureCategories
  //
  // Auto-applies all texture-based instance categories for API-submitted draws.
  // Resolves the albedo texture hash from the material's opaque data and writes
  // it to outTextureHash (so subsequent hooks — e.g. object-picking — can use
  // it). Then looks the hash up against every RtxOption category set.
  // Colormap-keyed materials use their key, which a replacement cannot change.
  // ---------------------------------------------------------------------------
  void externalDrawTextureCategories(
      remixapi_MaterialHandle handle,
      const MaterialData* material,
      DrawCallState& drawCall,
      XXH64_hash_t& outTextureHash) {
    // Auto-apply texture categories for API-submitted content (matches D3D9 behavior).
    // For API materials, the albedo texture hash is what D3D9's setupCategoriesForTexture()
    // pattern normally keys off, so look it up directly from the material's opaque data.
    outTextureHash = game_textures::findMaterialKey(handle);
    if (outTextureHash == kEmptyHash && material->getType() == MaterialDataType::Opaque) {
      const auto& opaqueMat = material->getOpaqueMaterialData();
      if (opaqueMat.getAlbedoOpacityTexture().isValid()) {
        outTextureHash = opaqueMat.getAlbedoOpacityTexture().getImageHash();
      }
    }

    if (outTextureHash != 0 && outTextureHash != kEmptyHash) {
      auto applyCategory = [&](const fast_unordered_set& hashSet, InstanceCategories cat) {
        if (hashSet.find(outTextureHash) != hashSet.end()) {
          drawCall.modifyCategoryFlags().set(cat);
        }
      };

      // The same set as DrawCallState::setupCategoriesForTexture
      applyCategory(RtxOptions::worldSpaceUiTextures(), InstanceCategories::WorldUI);
      applyCategory(RtxOptions::worldSpaceUiBackgroundTextures(), InstanceCategories::WorldMatte);
      applyCategory(RtxOptions::ignoreTextures(), InstanceCategories::Ignore);
      applyCategory(RtxOptions::ignoreLights(), InstanceCategories::IgnoreLights);
      applyCategory(RtxOptions::antiCullingTextures(), InstanceCategories::IgnoreAntiCulling);
      applyCategory(RtxOptions::motionBlurMaskOutTextures(), InstanceCategories::IgnoreMotionBlur);
      applyCategory(RtxOptions::opacityMicromapIgnoreTextures(), InstanceCategories::IgnoreOpacityMicromap);
      applyCategory(RtxOptions::ignoreAlphaOnTextures(), InstanceCategories::IgnoreAlphaChannel);
      applyCategory(RtxOptions::ignoreBakedLightingTextures(), InstanceCategories::IgnoreBakedLighting);
      applyCategory(RtxOptions::hideInstanceTextures(), InstanceCategories::Hidden);
      applyCategory(RtxOptions::particleTextures(), InstanceCategories::Particle);
      applyCategory(RtxOptions::beamTextures(), InstanceCategories::Beam);
      applyCategory(RtxOptions::decalTextures(), InstanceCategories::DecalStatic);
      applyCategory(RtxOptions::dynamicDecalTextures(), InstanceCategories::DecalDynamic);
      applyCategory(RtxOptions::singleOffsetDecalTextures(), InstanceCategories::DecalSingleOffset);
      applyCategory(RtxOptions::nonOffsetDecalTextures(), InstanceCategories::DecalNoOffset);
      applyCategory(RtxOptions::animatedWaterTextures(), InstanceCategories::AnimatedWater);
      applyCategory(RtxOptions::playerModelTextures(), InstanceCategories::ThirdPersonPlayerModel);
      applyCategory(RtxOptions::playerModelBodyTextures(), InstanceCategories::ThirdPersonPlayerBody);
      applyCategory(RtxOptions::terrainTextures(), InstanceCategories::Terrain);
      applyCategory(RtxOptions::skyBoxTextures(), InstanceCategories::Sky);
      applyCategory(RtxOptions::particleEmitterTextures(), InstanceCategories::ParticleEmitter);
      applyCategory(RtxOptions::hairCardTextures(), InstanceCategories::HairCards);
      applyCategory(RtxOptions::disableBackfacecullingTextures(), InstanceCategories::DisableBackfaceCulling);
    }
  }

  // ---------------------------------------------------------------------------
  // externalDrawObjectPicking
  //
  // Stores per-draw texture hash metadata in SceneManager::m_drawCallMeta when
  // object picking is active, mirroring the D3D9 draw path which populates
  // m_drawCallMeta in processDrawCallState. API draws supply their own
  // drawCallID via remixapi_InstanceInfoObjectPickingEXT; draws without it get
  // a per-submesh value (kFirstAutoPickingValue and up) while picking is active.
  //
  // ACCESS NOTE: this function uses SceneManager::m_drawCallMeta (private) and
  // SceneManager::DrawCallMetaInfo (private nested type). A friend declaration
  // for this function is required in SceneManager, or the member / type must be
  // made accessible via a public helper. See file-level comment above.
  // ---------------------------------------------------------------------------
  void externalDrawObjectPicking(
      DxvkDevice& device,
      DrawCallState& drawCall,
      XXH64_hash_t textureHash,
      SceneManager& scene) {
    // Store texture hash metadata for object picking (mirrors the D3D9 draw
    // path which populates m_drawCallMeta in processDrawCallState).
    const bool objectPickingActive = device.getCommon()->getResources().getRaytracingOutput()
      .m_primaryObjectPicking.isValid();
    if (!objectPickingActive) {
      return;
    }

    // No client value: a unique one per submesh, so picking resolves this
    // draw's texture instead of D3D9 draw 0's. The previous submesh of the same
    // draw left its auto value in drawCall.
    if (drawCall.drawCallID == 0 || drawCall.drawCallID == s_lastAutoPickingValue) {
      const uint32_t frameId = device.getCurrentFrameId();
      if (frameId != s_autoPickingFrameId) {
        s_autoPickingFrameId = frameId;
        s_nextAutoPickingValue = kFirstAutoPickingValue;
      }
      drawCall.drawCallID = s_nextAutoPickingValue++;
      s_lastAutoPickingValue = drawCall.drawCallID;
    }

    if (textureHash != 0 && textureHash != kEmptyHash) {
      auto meta = SceneManager::DrawCallMetaInfo {};
      meta.legacyTextureHash = textureHash;

      std::lock_guard lock { scene.m_drawCallMeta.mutex };
      auto [iter, isNew] = scene.m_drawCallMeta.infos[scene.m_drawCallMeta.ticker].emplace(drawCall.drawCallID, meta);
      ONCE_IF_FALSE(isNew, Logger::warn(
        "Found multiple API draw calls with the same \'objectPickingValue\'. "
        "Some objects might not be available through object picking"));
    }
  }

} // namespace fork_hooks
} // namespace dxvk
