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
      RTX_OPTION("rtx", bool, dynamicBlasForAnimatedMeshes, true,
                 "Meshes whose geometry changes every frame (skinned, or drawn through a vertex shader) get their own "
                 "BLAS even when they are small enough for the merged BLAS. In the merged BLAS such a mesh dirties its "
                 "whole bucket every frame: every instance in the bucket is re-sorted on the CPU and the bucket's BLAS "
                 "is rebuilt on the GPU. Off restores upstream's size-only rule.");
    };

  } // namespace

  namespace fork_hooks {

    uint32_t skinningCpuVertexLimit() {
      return SkinningOptions::skinningCpuVertexLimit();
    }

    bool keepAnimatedMeshesOutOfMergedBlas() {
      return SkinningOptions::dynamicBlasForAnimatedMeshes();
    }

  } // namespace fork_hooks

} // namespace dxvk
