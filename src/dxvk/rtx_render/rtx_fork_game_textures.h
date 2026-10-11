#pragma once

// rtx_fork_game_textures.h — fork-owned. The game's D3D9 colormap handed to
// remixapi_CreateMaterial through remixapi_MaterialInfoGameTexturesEXT: an
// opaque material without albedoTexture uses it as albedo and its image hash
// as replacement / category key (same mat_<hash> as a D3D9 draw of it).
// Resolution on the API thread: rtx_fork_game_textures.cpp. Keyed materials on
// the render thread: rtx_fork_game_textures_keyed.cpp.

#include "rtx_constants.h"
#include "rtx_materials.h"
#include "../dxvk_image.h"

#include <remix/remix_c.h>

#include <cstdint>
#include <memory>

namespace dxvk {
  namespace game_textures {

    struct Texture {
      // Sample view of the D3D9 texture; keeps the image alive.
      Rc<DxvkImageView> view;
      // D3D9 image hash, identical to the hash a D3D9 draw computes.
      XXH64_hash_t hash = kEmptyHash;

      bool isValid() const {
        return view != nullptr && hash != kEmptyHash;
      }
    };

    // Render thread. Colormap key of an API material, kEmptyHash when it has none.
    XXH64_hash_t findMaterialKey(remixapi_MaterialHandle handle);

    // Render thread, colormap-keyed materials only. The material to draw:
    // `replacement` merged over the game state like the D3D9 path, or (no
    // replacement) the legacy-defaults material, rebuilt when the
    // rtx.legacyMaterial.* / ignoreAlphaOnTextures values it depends on change.
    std::shared_ptr<MaterialData> keyedDrawMaterial(
      remixapi_MaterialHandle handle,
      const std::shared_ptr<MaterialData>& replacement);

    // Render thread, with the CS thread idle (teardown).
    void clearKeyedMaterials();

  } // namespace game_textures
} // namespace dxvk
