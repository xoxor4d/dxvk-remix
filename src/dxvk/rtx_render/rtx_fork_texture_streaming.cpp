// src/dxvk/rtx_render/rtx_fork_texture_streaming.cpp
//
// Fork-owned file. Texture streaming options read by RtxTextureManager.
//
// See docs/fork-touchpoints.md.

#include "rtx_fork_hooks.h"
#include "rtx_options.h"

namespace dxvk {

  namespace {

    struct TextureStreamingOptions {
      RTX_OPTION("rtx", uint32_t, textureGcFrameInterval, 2,
                 "Run the texture manager's garbage collection (sampler feedback evaluation, mip budget, demotion) every "
                 "N frames. It walks every managed texture and costs about a millisecond per run in scenes with "
                 "thousands of textures; streaming reacts N frames slower. 1 runs it every frame (upstream).");
    };

  } // namespace

  namespace fork_hooks {

    bool textureGarbageCollectionDue(const uint32_t frame) {
      const uint32_t interval = TextureStreamingOptions::textureGcFrameInterval();
      return interval <= 1u || frame % interval == 0u;
    }

  } // namespace fork_hooks

} // namespace dxvk
