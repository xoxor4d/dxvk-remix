#pragma once

// rtx_fork_lod_fade.h - fork-owned. LOD cross fade (rtx.lodFade.*): draws carrying the LOD fade modifier are skipped
// by every ray of a pixel whose blue noise sample is at or above their opacity, so a mesh and its lod blend instead of
// swapping. The shader side lives in shaders/rtx/algorithm/rtx_fork_lod_fade.slangh; plugin-facing surface
// documented in docs/RemixLodFadeAPI_gta4.md.

#include "rtx_option.h"

struct RaytraceArgs;

namespace dxvk {

  class LodFadeOptions {
  public:
    RTX_OPTION("rtx.lodFade", bool, enable, true,
               "Fades draws that carry the LOD fade modifier (bit 9 of the per draw modifier word, opacity in its top byte) "
               "by their opacity, with one blue noise sample per pixel and frame. Off draws them opaque.");
    RTX_OPTION_ARGS("rtx.lodFade", uint32_t, debugForceOpacity, 255,
                    "Fades every surface by this opacity instead of its own LOD fade opacity. 255 = off.",
                    args.minValue = 0u, args.maxValue = 255u);

    static void fillShaderParams(RaytraceArgs& constants);
    static void showImguiSettings();
  };

} // namespace dxvk
