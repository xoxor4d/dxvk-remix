// src/dxvk/rtx_render/rtx_fork_skinning.cpp
//
// Fork-owned file. Skinning options read by RtxGeometryUtils::dispatchSkinning.
//
// See docs/fork-touchpoints.md.

#include "rtx_fork_hooks.h"

#include <algorithm>
#include "rtx_options.h"

namespace dxvk {

  namespace {

    struct SkinningOptions {
      RTX_OPTION("rtx", uint32_t, skinningCpuVertexLimit, 0,
                 "Skinned draws with at most this many vertices are skinned on the CPU, which writes every vertex to the "
                 "GPU with its own buffer update (position and normal: two per vertex). 0 skins everything with the "
                 "compute shader. Upstream uses 256.");
      RTX_OPTION("rtx", bool, dynamicBlasForAnimatedMeshes, true,
                 "Meshes whose geometry changes every frame (skinned, or drawn through a vertex shader) get their own "
                 "BLAS even when they are small enough for the merged BLAS. In the merged BLAS such a mesh dirties its "
                 "whole bucket every frame: every instance in the bucket is re-sorted on the CPU and the bucket's BLAS "
                 "is rebuilt on the GPU. Off restores upstream's size-only rule.");
      RTX_OPTION("rtx.opacityMicromap", uint32_t, rebindFrameInterval, 16,
                 "Newly baked opacity micromaps are bound to their BLAS at most every N frames. Binding re-sorts every "
                 "BLAS bucket and rebuilds the merged BLAS on the GPU, which with thousands of meshes streaming in "
                 "happened nearly every frame. 1 binds them the frame after they were baked (upstream).");
    };

    // frame of the last rebind and whether micromaps were baked since
    uint32_t s_ommRebindFrame = 0;
    bool s_ommBakedSinceRebind = false;

  } // namespace

  namespace fork_hooks {

    uint32_t skinningCpuVertexLimit() {
      return SkinningOptions::skinningCpuVertexLimit();
    }

    bool keepAnimatedMeshesOutOfMergedBlas() {
      return SkinningOptions::dynamicBlasForAnimatedMeshes();
    }

    bool ommRebindDue(const bool bakedThisFrame, const uint32_t frame) {
      s_ommBakedSinceRebind |= bakedThisFrame;
      const uint32_t interval = std::max(1u, SkinningOptions::rebindFrameInterval());
      if (!s_ommBakedSinceRebind || frame - s_ommRebindFrame < interval) {
        return false;
      }
      s_ommBakedSinceRebind = false;
      s_ommRebindFrame = frame;
      return true;
    }

  } // namespace fork_hooks

} // namespace dxvk
