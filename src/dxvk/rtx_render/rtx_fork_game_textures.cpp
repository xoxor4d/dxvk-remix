// src/dxvk/rtx_render/rtx_fork_game_textures.cpp
//
// Fork-owned file. Game texture info (remixapi_MaterialInfoGameTexturesEXT):
//   * setDrawGameTextures, onD3D9DrawMaterial
//       pending record for the following D3D9 draws, associated with each
//       draw's material hash for AutoPBR
//   * resolve/apply/release/forgetMaterialGameTextures
//       API materials; one without albedoTexture and with a COLOR texture is
//       drawn like a non-replaced D3D9 draw of that texture, keyed by its hash
//   * externalMaterialKey, findMaterialKey, keyedDrawMaterial
//       colormap-keyed replacement / capture / categories
//   * onD3D9EndFrame, shutdownGameTextures
//
// Thread model:
//   API thread (the thread issuing D3D9 calls), under the D3D9 device lock:
//     texture resolution (PreLoadAll), s_pendingDraw, s_retained. Applying the
//     record there keeps it ordered with the draws without a CS round trip.
//   Render thread: s_keyedMaterials (CreateMaterial / DestroyMaterial lambdas,
//     submitExternalDraw).
//
// See docs/fork-touchpoints.md and docs/RemixAutoPbrAPI.md.

#include "rtx_fork_game_textures.h"

#include "rtx_fork_autopbr.h"
#include "rtx_fork_hooks.h"
#include "rtx_options.h"

#include "../../d3d9/d3d9_device.h"       // D3D9DeviceEx, LockDevice, EmitCs
#include "../../d3d9/d3d9_texture.h"      // D3D9Texture2D, D3D9CommonTexture

#include "../../util/log/log.h"
#include "../../util/util_once.h"
#include "../../util/util_string.h"

#include <atomic>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dxvk {
namespace {

  using game_textures::Usage;

  // Device whose resources the state below references.
  D3D9DeviceEx* s_owner = nullptr;

  // Pending SetDrawGameTextures record. API thread, under the device lock.
  std::shared_ptr<const game_textures::TextureSet> s_pendingDraw;

  // NORMAL / SPECULAR textures of API materials created while AutoPBR was idle.
  // Private refs keep the texture objects (not the device) alive until they are
  // resolved, the material is destroyed or the device goes away.
  // API thread, under the device lock.
  struct RetainedTextures {
    std::array<Com<D3D9Texture2D, false>, Usage::Count> textures;
    std::array<std::string, Usage::Count> names;
    // Deferred resolves that found a texture without data.
    uint32_t attempts = 0;

    bool empty() const {
      return textures[Usage::Normal] == nullptr && textures[Usage::Specular] == nullptr;
    }
  };
  std::unordered_map<remixapi_MaterialHandle, RetainedTextures> s_retained;
  // s_retained.size(), readable without the device lock.
  std::atomic<size_t> s_retainedCount { 0 };
  std::atomic<bool> s_resolveRetained { false };

  // A texture still without data is retried on this many end of frames.
  constexpr uint32_t kMaxResolveAttempts = 300;

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

  constexpr uint32_t kAllUsages = (1u << Usage::Count) - 1;

  std::string toString(const char* value) {
    return value != nullptr ? std::string { value } : std::string {};
  }

  bool isEmptyPath(remixapi_Path path) {
    return path == nullptr || path[0] == L'\0';
  }

  bool isValidInfo(const remixapi_MaterialInfoGameTexturesEXT& info) {
    return info.sType == REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT &&
           (info.textures_count == 0 || info.textures_values != nullptr);
  }

  void syncRetainedCount() {
    s_retainedCount = s_retained.size();
  }

  // Only textures of the Remix device can be treated as D3D9Texture2D.
  D3D9Texture2D* asTexture2D(D3D9DeviceEx* device, IDirect3DTexture9* texture) {
    if (texture == nullptr) {
      return nullptr;
    }
    if (texture->GetType() != D3DRTYPE_TEXTURE) {
      ONCE(Logger::warn("[RTX-GameTextures] Ignoring a non-2D game texture."));
      return nullptr;
    }
    IDirect3DDevice9* owner = nullptr;
    if (FAILED(texture->GetDevice(&owner)) || owner == nullptr) {
      return nullptr;
    }
    const bool isRemixTexture = owner == static_cast<IDirect3DDevice9*>(device);
    owner->Release();
    if (!isRemixTexture) {
      ONCE(Logger::warn("[RTX-GameTextures] Ignoring a game texture that belongs to another D3D9 device."));
      return nullptr;
    }
    return static_cast<D3D9Texture2D*>(texture);
  }

  // Caller holds the device lock. Returns false when the texture has no data yet.
  bool resolveTexture(D3D9Texture2D* texture, const std::string& name, game_textures::Texture& dst) {
    D3D9CommonTexture* common = texture->GetCommonTexture();
    if (common == nullptr || common->GetImage() == nullptr) {
      return false;
    }

    // Managed textures upload and hash lazily on their first bind. Force that
    // here (IDirect3DResource9::PreLoad) for textures no draw ever binds.
    common->PreLoadAll();

    const Rc<DxvkImage>& image = common->GetImage();
    if (image->getHash() == kEmptyHash) {
      // Mip 0 was never written.
      return false;
    }

    // Non-sRGB view, as for a draw with the default D3DSAMP_SRGBTEXTURE = 0.
    // The opaque material shader linearizes either view kind the same way.
    dst.view = common->GetSampleView(false);
    dst.hash = image->getHash();
    dst.width = image->info().extent.width;
    dst.height = image->info().extent.height;
    dst.name = name;
    return true;
  }

  // Caller holds the device lock. Resolves the usages in usageMask; the other
  // usages are handed to `deferred` (when given) for later resolution.
  std::shared_ptr<game_textures::TextureSet> resolveSet(
      D3D9DeviceEx* device,
      const remixapi_MaterialInfoGameTexturesEXT& info,
      uint32_t usageMask,
      RetainedTextures* deferred) {
    auto set = std::make_shared<game_textures::TextureSet>();
    set->materialName = toString(info.materialName);
    set->shaderName = toString(info.shaderName);

    for (uint32_t i = 0; i < info.textures_count; ++i) {
      const remixapi_GameTexture& src = info.textures_values[i];
      const uint32_t usage = static_cast<uint32_t>(src.usage);
      D3D9Texture2D* texture = usage < Usage::Count ? asTexture2D(device, src.texture) : nullptr;
      if (texture == nullptr) {
        continue;
      }

      if ((usageMask & (1u << usage)) == 0) {
        if (deferred != nullptr) {
          deferred->textures[usage] = texture;
          deferred->names[usage] = toString(src.name);
        }
        continue;
      }

      resolveTexture(texture, toString(src.name), set->textures[usage]);
    }

    return set;
  }

  // Caller holds the device lock. Hands the NORMAL / SPECULAR textures of the
  // retained materials to AutoPBR, ordered after the materials' CreateMaterial
  // lambdas. Textures without data stay retained and are retried on the
  // following end of frames, up to kMaxResolveAttempts.
  void resolveRetained(D3D9DeviceEx* device) {
    if (!s_resolveRetained.exchange(false) || !AutoPbr::isCollecting() || s_retained.empty()) {
      return;
    }

    std::vector<std::pair<remixapi_MaterialHandle, std::shared_ptr<const game_textures::TextureSet>>> resolved;
    uint32_t waiting = 0;
    for (auto it = s_retained.begin(); it != s_retained.end();) {
      RetainedTextures& retained = it->second;
      auto set = std::make_shared<game_textures::TextureSet>();
      bool any = false;
      for (uint32_t u : { Usage::Normal, Usage::Specular }) {
        if (retained.textures[u] != nullptr &&
            resolveTexture(retained.textures[u].ptr(), retained.names[u], set->textures[u])) {
          // The resolved view now keeps the image alive.
          retained.textures[u] = nullptr;
          any = true;
        }
      }
      if (any) {
        resolved.emplace_back(it->first, std::move(set));
      }

      if (retained.empty() || ++retained.attempts >= kMaxResolveAttempts) {
        it = s_retained.erase(it);
      } else {
        waiting += (retained.textures[Usage::Normal] != nullptr) + (retained.textures[Usage::Specular] != nullptr);
        ++it;
      }
    }
    syncRetainedCount();
    AutoPbr::setTexturesWaitingForData(waiting);
    if (waiting > 0) {
      s_resolveRetained = true;
    }

    if (!resolved.empty()) {
      device->EmitCs([cResolved = std::move(resolved)](DxvkContext*) {
        for (const auto& [handle, textures] : cResolved) {
          AutoPbr::completeApiMaterial(handle, *textures);
        }
      });
    }
  }

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

  void requestDeferredResolve() {
    s_resolveRetained = true;
  }

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

} // namespace game_textures

namespace fork_hooks {

  remixapi_ErrorCode setDrawGameTextures(
      D3D9DeviceEx* remixDevice,
      const remixapi_MaterialInfoGameTexturesEXT* info) {
    if (info == nullptr) {
      D3D9DeviceLock lock = remixDevice != nullptr ? remixDevice->LockDevice() : D3D9DeviceLock {};
      s_pendingDraw.reset();
      return REMIXAPI_ERROR_CODE_SUCCESS;
    }
    if (remixDevice == nullptr) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!isValidInfo(*info)) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    auto lock = remixDevice->LockDevice();
    // Only AutoPBR consumes the record; skip the texture work while it is idle.
    if (AutoPbr::isCollecting()) {
      s_owner = remixDevice;
      resolveRetained(remixDevice);
      s_pendingDraw = resolveSet(remixDevice, *info, kAllUsages, nullptr);
    } else {
      s_pendingDraw.reset();
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  void onD3D9DrawMaterial(const LegacyMaterialData& material) {
    if (s_pendingDraw == nullptr) {
      return;
    }

    const XXH64_hash_t materialHash = material.getHash();
    if (materialHash == kEmptyHash) {
      return;
    }

    if (s_pendingDraw->color().isValid()) {
      AutoPbr::addAssociation(materialHash, *s_pendingDraw);
      return;
    }

    // No COLOR entry: the colormap is what the draw binds.
    game_textures::Texture color;
    const TextureRef& colorTexture = material.getColorTexture();
    if (DxvkImageView* view = colorTexture.getImageView()) {
      color.view = view;
      color.hash = colorTexture.getImageHash();
      color.width = view->image()->info().extent.width;
      color.height = view->image()->info().extent.height;
    }
    AutoPbr::addAssociation(materialHash, *s_pendingDraw, &color);
  }

  void onD3D9EndFrame(D3D9DeviceEx* remixDevice, bool callInjectRtx) {
    // The window-proc path ends frames off the API thread; leave it alone.
    if (!callInjectRtx || !s_resolveRetained.load(std::memory_order_relaxed)) {
      return;
    }
    auto lock = remixDevice->LockDevice();
    resolveRetained(remixDevice);
  }

  std::shared_ptr<const game_textures::TextureSet> resolveMaterialGameTextures(
      D3D9DeviceEx* remixDevice,
      const remixapi_MaterialInfo& info,
      const remixapi_MaterialInfoGameTexturesEXT* ext) {
    if (remixDevice == nullptr) {
      return nullptr;
    }
    const auto handle = reinterpret_cast<remixapi_MaterialHandle>(info.hash);

    if (ext == nullptr || !isValidInfo(*ext)) {
      if (ext != nullptr) {
        ONCE(Logger::warn("[RTX-GameTextures] Ignoring malformed remixapi_MaterialInfoGameTexturesEXT."));
      }
      // The handle may be re-created without the extension.
      if (s_retainedCount.load(std::memory_order_relaxed) > 0) {
        auto lock = remixDevice->LockDevice();
        s_retained.erase(handle);
        syncRetainedCount();
      }
      return nullptr;
    }

    auto lock = remixDevice->LockDevice();
    s_owner = remixDevice;

    // COLOR is needed for drawing; NORMAL / SPECULAR only once AutoPBR collects.
    const bool collecting = AutoPbr::isCollecting();
    RetainedTextures deferred;
    std::shared_ptr<game_textures::TextureSet> set =
      resolveSet(remixDevice, *ext, collecting ? kAllUsages : (1u << Usage::Color), collecting ? nullptr : &deferred);

    if (deferred.empty()) {
      s_retained.erase(handle);
    } else {
      s_retained[handle] = std::move(deferred);
    }
    syncRetainedCount();

    if (isEmptyPath(info.albedoTexture)) {
      set->albedoFromColor = set->color().isValid();
      if (!set->albedoFromColor) {
        ONCE(Logger::warn("[RTX-GameTextures] A material without albedoTexture has no usable COLOR game texture "
                          "(missing, or not filled before CreateMaterial); it renders without albedo."));
      }
    }
    return set;
  }

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

  void releaseMaterialGameTextures(D3D9DeviceEx* remixDevice, remixapi_MaterialHandle handle) {
    if (remixDevice == nullptr) {
      return;
    }
    if (s_retainedCount.load(std::memory_order_relaxed) == 0) {
      return;
    }
    auto lock = remixDevice->LockDevice();
    s_retained.erase(handle);
    syncRetainedCount();
  }

  void forgetMaterialGameTextures(remixapi_MaterialHandle handle) {
    s_keyedMaterials.erase(handle);
    AutoPbr::unregisterApiMaterial(handle);
  }

  XXH64_hash_t externalMaterialKey(remixapi_MaterialHandle handle, const MaterialData& material) {
    const XXH64_hash_t key = game_textures::findMaterialKey(handle);
    return key != kEmptyHash ? key : material.getHash();
  }

  void shutdownGameTextures(D3D9DeviceEx* remixDevice) {
    // Another device (e.g. a short-lived probe device) going away.
    if (remixDevice != nullptr && s_owner != nullptr && remixDevice != s_owner) {
      return;
    }
    D3D9DeviceLock lock = remixDevice != nullptr ? remixDevice->LockDevice() : D3D9DeviceLock {};
    if (remixDevice != nullptr) {
      // The render-thread state below must not be in use.
      remixDevice->SynchronizeCsThread();
    }
    s_pendingDraw.reset();
    s_retained.clear();
    syncRetainedCount();
    s_keyedMaterials.clear();
    s_owner = nullptr;
    AutoPbr::reset();
  }

} // namespace fork_hooks
} // namespace dxvk
