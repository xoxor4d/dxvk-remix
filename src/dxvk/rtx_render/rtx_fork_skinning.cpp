// src/dxvk/rtx_render/rtx_fork_skinning.cpp
//
// Fork-owned file. Skinning options read by RtxGeometryUtils::dispatchSkinning.
//
// See docs/fork-touchpoints.md.

#include "rtx_fork_hooks.h"
#include "rtx_options.h"

namespace dxvk {

  namespace {

    struct SkinningOptions {
      RTX_OPTION("rtx", uint32_t, skinningCpuVertexLimit, 0,
                 "Skinned draws with at most this many vertices are skinned on the CPU, which writes every vertex to the "
                 "GPU with its own buffer update (position and normal: two per vertex). 0 skins everything with the "
                 "compute shader. Upstream uses 256.");
    };

  } // namespace

  namespace fork_hooks {

    uint32_t skinningCpuVertexLimit() {
      return SkinningOptions::skinningCpuVertexLimit();
    }

  } // namespace fork_hooks

} // namespace dxvk
