// src/dxvk/rtx_render/rtx_fork_game_textures.cpp
//
// Fork-owned file. API thread half of the colormap-keyed API materials
// (remixapi_MaterialInfoGameTexturesEXT): resolves the game's D3D9 texture
// under the D3D9 device lock so the CreateMaterial lambda can use it as albedo.
// Render thread half: rtx_fork_game_textures_keyed.cpp.
//
// See docs/fork-touchpoints.md.

#include "rtx_fork_game_textures.h"

#include "rtx_fork_hooks.h"

#include "../../d3d9/d3d9_device.h"       // D3D9DeviceEx, LockDevice, SynchronizeCsThread
#include "../../d3d9/d3d9_texture.h"      // D3D9Texture2D, D3D9CommonTexture

#include "../../util/log/log.h"
#include "../../util/util_once.h"

namespace dxvk {
namespace {

  // Device whose resources the keyed materials reference.
  D3D9DeviceEx* s_owner = nullptr;

  bool isEmptyPath(remixapi_Path path) {
    return path == nullptr || path[0] == L'\0';
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
  bool resolveTexture(D3D9Texture2D* texture, game_textures::Texture& dst) {
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
    return true;
  }

} // anonymous namespace

namespace fork_hooks {

  std::shared_ptr<const game_textures::Texture> resolveMaterialGameTextures(
      D3D9DeviceEx* remixDevice,
      const remixapi_MaterialInfo& info,
      const remixapi_MaterialInfoGameTexturesEXT* ext) {
    if (remixDevice == nullptr || ext == nullptr) {
      return nullptr;
    }
    if (ext->sType != REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_GAME_TEXTURES_EXT) {
      ONCE(Logger::warn("[RTX-GameTextures] Ignoring malformed remixapi_MaterialInfoGameTexturesEXT."));
      return nullptr;
    }
    if (!isEmptyPath(info.albedoTexture)) {
      ONCE(Logger::warn("[RTX-GameTextures] remixapi_MaterialInfoGameTexturesEXT is ignored on a material with an albedoTexture."));
      return nullptr;
    }

    auto lock = remixDevice->LockDevice();
    s_owner = remixDevice;

    D3D9Texture2D* texture = asTexture2D(remixDevice, ext->colorTexture);
    auto color = std::make_shared<game_textures::Texture>();
    if (texture == nullptr || !resolveTexture(texture, *color)) {
      ONCE(Logger::warn("[RTX-GameTextures] The colorTexture of a material without albedoTexture is missing or not filled "
                        "before CreateMaterial; it renders without albedo."));
      return nullptr;
    }
    return color;
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
    game_textures::clearKeyedMaterials();
    s_owner = nullptr;
  }

} // namespace fork_hooks
} // namespace dxvk
