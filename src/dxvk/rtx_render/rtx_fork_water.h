#pragma once

// rtx_fork_water.h - fork-owned. Shoreline fade for AnimatedWater translucents
// (rtx.water.*). The shader side lives in
// shaders/rtx/algorithm/rtx_fork_water_shore.slangh; plugin-facing surface
// documented in docs/RemixWaterAPI.md.

#include "rtx_option.h"

struct RaytraceArgs;

namespace dxvk {

  class WaterOptions {
  public:
    RTX_OPTION("rtx.water", bool, shoreFadeEnable, true,
               "Fades AnimatedWater translucent surfaces out where the opaque surface below them is shallow, "
               "giving a soft, animated shoreline instead of a hard intersection line.");
    RTX_OPTION_ARGS("rtx.water", float, shoreFadeDistance, 48.0f,
                    "Range in world units of the downward probe that measures water depth. Water deeper than this is never faded. "
                    "Raised internally to at least shoreCutDepth + shoreFadeWidth + shoreHeightScale.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.water", float, shoreCutDepth, 0.5f,
                    "Water depth in world units at and below which the water is fully faded out.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.water", float, shoreFadeWidth, 16.0f,
                    "Depth range in world units over which the water fades in above shoreCutDepth.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.water", float, shoreHeightScale, 24.0f,
                    "World units of depth offset per unit of animated normal slope. Makes the shoreline irregular and moves it with the "
                    "water normal animation. 0 gives a smooth depth-only fade.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.water", float, shoreProbeSpread, 0.5f,
                    "Horizontal offset in world units (added to the pixel footprint) of the two extra depth probes traced when the center "
                    "probe misses, so cracks and T-junctions between meshes are not mistaken for deep water.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.water", float, objectFadeWidth, 6.0f,
                    "Distance in world units along the view ray over which water fades in behind opaque objects that intersect it "
                    "(soft intersection, like soft particles). 0 disables.",
                    args.minValue = 0.0f);

    static void fillShaderParams(RaytraceArgs& constants);
    static void showImguiSettings();
  };

} // namespace dxvk
