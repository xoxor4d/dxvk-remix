// src/dxvk/rtx_render/rtx_fork_game_textures_keyed.cpp
//
// Fork-owned file. Render-thread half of the game texture support: materials
// keyed by their colormap hash (CreateMaterial / DestroyMaterial lambdas,
// submitExternalDraw). Kept free of D3D9 dependencies.
//
// See docs/fork-touchpoints.md and docs/RemixAutoPbrAPI.md.

#include "rtx_fork_game_textures.h"

#include "rtx_fork_autopbr.h"
#include "rtx_fork_hooks.h"
#include "rtx_options.h"

#include <atomic>
#include <unordered_map>
#include <utility>

namespace dxvk {
namespace {

  std::atomic<bool> s_resolveRetained { false };

  struct KeyedMaterial {
    XXH64_hash_t key = kEmptyHash;
    // What a D3D9 draw takes from the game: albedo, sampler, alpha state.
    OpaqueMaterialData gameState;
    std::shared_ptr<MaterialData> legacy;
    XXH64_hash_t legacyStamp = kEmptyHash;
    // Last replacement merged for this material, and the merge result.
    std::shared_ptr<MaterialData> replacementSource;
    std::shared_ptr<MaterialData> mergedReplacement;
  };

  // Render thread only.
  std::unordered_map<remixapi_MaterialHandle, KeyedMaterial> s_keyedMaterials;

  // The values a legacy-defaults material is built from.
  XXH64_hash_t legacyDefaultsStamp(XXH64_hash_t colorHash) {
    XXH64_hash_t h = 0;
    auto add = [&h](const auto& value) {
      h = XXH64(&value, sizeof(value), h);
    };
    add(LegacyMaterialDefaults::anisotropy());
    add(LegacyMaterialDefaults::emissiveIntensity());
    add(LegacyMaterialDefaults::albedoConstant());
    add(LegacyMaterialDefaults::opacityConstant());
    add(LegacyMaterialDefaults::roughnessConstant());
    add(LegacyMaterialDefaults::metallicConstant());
    add(LegacyMaterialDefaults::emissiveColorConstant());
    add(LegacyMaterialDefaults::enableEmissive());
    add(LegacyMaterialDefaults::enableThinFilm());
    add(LegacyMaterialDefaults::alphaIsThinFilmThickness());
    add(LegacyMaterialDefaults::thinFilmThicknessConstant());
    add(LegacyMaterialDefaults::useAlbedoTextureIfPresent());
    add(LegacyMaterialDefaults::ignoreAlphaChannel());
    const auto& ignoreAlpha = RtxOptions::ignoreAlphaOnTextures();
    add(ignoreAlpha.find(colorHash) != ignoreAlpha.end());
    return h;
  }

  // The state a D3D9 draw takes from the game rather than from the material:
  // albedo, sampler filter / wrap and alpha test / blend.
  void copyGameState(const OpaqueMaterialData& src, OpaqueMaterialData& dst) {
    dst.setAlbedoOpacityTexture(src.getAlbedoOpacityTexture());
    dst.setFilterMode(src.getFilterMode());
    dst.setWrapModeU(src.getWrapModeU());
    dst.setWrapModeV(src.getWrapModeV());
    dst.setUseLegacyAlphaState(src.getUseLegacyAlphaState());
    dst.setAlphaTestType(src.getAlphaTestType());
    dst.setAlphaTestReferenceValue(src.getAlphaTestReferenceValue());
    dst.setBlendEnabled(src.getBlendEnabled());
    dst.setBlendType(src.getBlendType());
    dst.setInvertedBlend(src.getInvertedBlend());
  }

  // Non-replaced D3D9 material equivalent (cf. LegacyMaterialData::as /
  // MaterialData::fromLegacy): rtx.legacyMaterial.* defaults, legacy-defaults
  // shading, game state.
  MaterialData makeLegacyMaterial(const KeyedMaterial& keyed) {
    MaterialData legacy = MaterialData::fromLegacy(LegacyMaterialData {});
    OpaqueMaterialData& opaque = legacy.getOpaqueMaterialData();

    copyGameState(keyed.gameState, opaque);
    if (!LegacyMaterialDefaults::useAlbedoTextureIfPresent()) {
      opaque.setAlbedoOpacityTexture(TextureRef {});
    }

    const auto& ignoreAlpha = RtxOptions::ignoreAlphaOnTextures();
    opaque.setIgnoreAlphaChannel(LegacyMaterialDefaults::ignoreAlphaChannel() ||
                                 ignoreAlpha.find(keyed.key) != ignoreAlpha.end());
    return legacy;
  }

} // anonymous namespace

namespace game_textures {

  XXH64_hash_t findMaterialKey(remixapi_MaterialHandle handle) {
    if (s_keyedMaterials.empty()) {
      return kEmptyHash;
    }
    auto it = s_keyedMaterials.find(handle);
    return it != s_keyedMaterials.end() ? it->second.key : kEmptyHash;
  }

  std::shared_ptr<MaterialData> keyedDrawMaterial(
      remixapi_MaterialHandle handle,
      const std::shared_ptr<MaterialData>& replacement) {
    auto it = s_keyedMaterials.find(handle);
    if (it == s_keyedMaterials.end()) {
      return replacement;
    }
    KeyedMaterial& keyed = it->second;

    if (replacement != nullptr) {
      if (replacement->getType() != MaterialDataType::Opaque) {
        return replacement;
      }
      // Same as mergeLegacyMaterial on the D3D9 path: parameters the replacement
      // does not author come from the game state or the replacement defaults.
      if (keyed.replacementSource != replacement || keyed.mergedReplacement == nullptr) {
        auto merged = std::make_shared<MaterialData>(*replacement);
        merged->getOpaqueMaterialData().merge(keyed.gameState);
        keyed.replacementSource = replacement;
        keyed.mergedReplacement = std::move(merged);
      }
      return keyed.mergedReplacement;
    }

    const XXH64_hash_t stamp = legacyDefaultsStamp(keyed.key);
    if (keyed.legacy == nullptr || keyed.legacyStamp != stamp) {
      keyed.legacy = std::make_shared<MaterialData>(makeLegacyMaterial(keyed));
      keyed.legacyStamp = stamp;
    }
    return keyed.legacy;
  }

  void requestDeferredResolve() {
    s_resolveRetained = true;
  }

  bool hasDeferredResolve() {
    return s_resolveRetained.load(std::memory_order_relaxed);
  }

  bool consumeDeferredResolve() {
    return s_resolveRetained.exchange(false);
  }

  void clearKeyedMaterials() {
    s_keyedMaterials.clear();
  }

} // namespace game_textures

namespace fork_hooks {

  MaterialData applyMaterialGameTextures(
      remixapi_MaterialHandle handle,
      MaterialData&& material,
      const std::shared_ptr<const game_textures::TextureSet>& gameTextures) {
    // A handle can be re-created; drop what the previous material registered.
    forgetMaterialGameTextures(handle);

    if (gameTextures == nullptr) {
      return std::move(material);
    }

    if (!gameTextures->albedoFromColor || material.getType() != MaterialDataType::Opaque) {
      AutoPbr::registerApiMaterial(handle, material.getHash(), gameTextures);
      return std::move(material);
    }

    // Same key a D3D9 draw of this colormap gets, so mat_<hash> replacements
    // and captures are shared between both paths.
    const game_textures::Texture& color = gameTextures->color();
    KeyedMaterial& keyed = s_keyedMaterials[handle];
    keyed.key = color.hash;

    OpaqueMaterialData gameState = material.getOpaqueMaterialData();
    gameState.setAlbedoOpacityTexture(TextureRef(color.view));
    copyGameState(gameState, keyed.gameState);
    keyed.legacy = std::make_shared<MaterialData>(makeLegacyMaterial(keyed));
    keyed.legacyStamp = legacyDefaultsStamp(keyed.key);

    AutoPbr::registerApiMaterial(handle, keyed.key, gameTextures);
    return MaterialData(*keyed.legacy);
  }

  void forgetMaterialGameTextures(remixapi_MaterialHandle handle) {
    s_keyedMaterials.erase(handle);
    AutoPbr::unregisterApiMaterial(handle);
  }

  XXH64_hash_t externalMaterialKey(remixapi_MaterialHandle handle, const MaterialData& material) {
    const XXH64_hash_t key = game_textures::findMaterialKey(handle);
    return key != kEmptyHash ? key : material.getHash();
  }

} // namespace fork_hooks
} // namespace dxvk
