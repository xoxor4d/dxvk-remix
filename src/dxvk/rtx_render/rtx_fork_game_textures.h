#pragma once

// rtx_fork_game_textures.h — fork-owned. Game (D3D9) textures handed to the
// runtime through remixapi_MaterialInfoGameTexturesEXT, either chained into
// remixapi_CreateMaterial or set for subsequent D3D9 draws with
// remixapi_SetDrawGameTextures. Consumers: API material albedo / colormap key
// (rtx_fork_game_textures.cpp, rtx_fork_submit.cpp) and AutoPBR
// (rtx_fork_autopbr.cpp).

#include "rtx_constants.h"
#include "rtx_materials.h"
#include "../dxvk_image.h"

#include <remix/remix_c.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace dxvk {
  namespace game_textures {

    // Matches remixapi_GameTextureUsage.
    enum Usage : uint32_t {
      Color = 0,
      Normal,
      Specular,
      Count
    };

    struct Texture {
      // Sample view of the D3D9 texture; keeps the image alive.
      Rc<DxvkImageView> view;
      // D3D9 image hash, identical to the hash a D3D9 draw computes.
      XXH64_hash_t hash = kEmptyHash;
      uint32_t width = 0;
      uint32_t height = 0;
      std::string name;

      bool isValid() const {
        return view != nullptr && hash != kEmptyHash;
      }
    };

    struct TextureSet {
      std::string materialName;
      std::string shaderName;
      std::array<Texture, Usage::Count> textures;
      // API material without albedoTexture: the COLOR texture is its albedo and
      // its image hash the material key.
      bool albedoFromColor = false;

      const Texture& color() const {
        return textures[Usage::Color];
      }
    };

    // Any thread. API materials created while AutoPBR was idle only resolved
    // their COLOR texture; this has the next API-thread entry (D3D9 end of
    // frame, SetDrawGameTextures) resolve their NORMAL / SPECULAR textures.
    void requestDeferredResolve();

    // Render thread. Colormap key of an API material, kEmptyHash when it has none.
    XXH64_hash_t findMaterialKey(remixapi_MaterialHandle handle);

    // Render thread, colormap-keyed materials only. The material to draw:
    // `replacement` merged over the game state like the D3D9 path, or (no
    // replacement) the legacy-defaults material, rebuilt when the
    // rtx.legacyMaterial.* / ignoreAlphaOnTextures values it depends on change.
    std::shared_ptr<MaterialData> keyedDrawMaterial(
      remixapi_MaterialHandle handle,
      const std::shared_ptr<MaterialData>& replacement);

  } // namespace game_textures
} // namespace dxvk
