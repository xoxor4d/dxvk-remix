#pragma once

// rtx_fork_lod_fade.h - fork-owned. Dithered LOD cross fade (rtx.lodFade.*): draws carrying the LOD fade modifier are
// skipped by every ray wherever a hash of the hit position is at or above their opacity, so a mesh and its lod blend
// instead of swapping. The shader side lives in shaders/rtx/algorithm/rtx_fork_lod_fade.slangh; plugin-facing surface
// documented in docs/RemixLodFadeAPI_gta4.md.

#include "rtx_option.h"

struct RaytraceArgs;

namespace dxvk {

  class LodFadeOptions {
  public:
    RTX_OPTION("rtx.lodFade", bool, enable, true,
               "Dithers draws that carry the LOD fade modifier (bit 9 of the per draw modifier word, opacity in its top byte) "
               "by their opacity. Off draws them opaque.");
    RTX_OPTION_ARGS("rtx.lodFade", float, cellSize, 0.1f,
                    "Edge length in world units of the cells the dither pattern is made of.",
                    args.minValue = 0.001f);
    RTX_OPTION("rtx.lodFade", bool, animated, false,
               "Rolls a new dither pattern every frame, so temporal accumulation turns it into a smooth blend. "
               "Off keeps the pattern fixed in world space: a dissolve without temporal noise.");
    RTX_OPTION_ARGS("rtx.lodFade", uint32_t, debugForceOpacity, 255,
                    "Dithers every surface by this opacity instead of its own LOD fade opacity. 255 = off.",
                    args.minValue = 0u, args.maxValue = 255u);

    static void fillShaderParams(RaytraceArgs& constants);
    static void showImguiSettings();
  };

} // namespace dxvk
