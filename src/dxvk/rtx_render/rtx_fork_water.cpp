// src/dxvk/rtx_render/rtx_fork_water.cpp
//
// Fork-owned file. Shoreline fade for AnimatedWater translucents: options,
// shader constants and UI. See docs/RemixWaterAPI.md.

#include "rtx_fork_water.h"

#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"

#include "rtx/pass/raytrace_args.h"

#include "../imgui/imgui.h"

#include <algorithm>

namespace dxvk {

  void WaterOptions::fillShaderParams(RaytraceArgs& constants) {
    const float cutDepth = std::max(shoreCutDepth(), 0.0f);
    const float fadeWidth = std::max(shoreFadeWidth(), 0.0f);
    const float heightScale = std::max(shoreHeightScale(), 0.0f);

    constants.waterShoreFadeEnable = shoreFadeEnable() ? 1u : 0u;
    // A probe miss means "fully deep", so the probe must reach past every partially faded depth.
    constants.waterShoreFadeDistance = std::max(shoreFadeDistance(), cutDepth + fadeWidth + heightScale);
    constants.waterShoreCutDepth = cutDepth;
    constants.waterShoreFadeWidth = fadeWidth;
    constants.waterShoreHeightScale = heightScale;
    constants.waterShoreProbeSpread = std::max(shoreProbeSpread(), 0.0f);
    constants.waterObjectFadeWidth = std::max(objectFadeWidth(), 0.0f);
  }

  void WaterOptions::showImguiSettings() {
    RemixGui::Checkbox("Shoreline Fade for Animated Water", &shoreFadeEnableObject());
    if (shoreFadeEnable()) {
      ImGui::Indent();
      RemixGui::DragFloat("Probe Distance", &shoreFadeDistanceObject(), 0.5f, 0.0f, 10000.0f, "%.1f");
      RemixGui::DragFloat("Cut Depth", &shoreCutDepthObject(), 0.05f, 0.0f, 1000.0f, "%.2f");
      RemixGui::DragFloat("Fade Width", &shoreFadeWidthObject(), 0.1f, 0.0f, 1000.0f, "%.1f");
      RemixGui::DragFloat("Height Scale", &shoreHeightScaleObject(), 0.1f, 0.0f, 1000.0f, "%.1f");
      RemixGui::DragFloat("Probe Spread", &shoreProbeSpreadObject(), 0.01f, 0.0f, 100.0f, "%.2f");
      RemixGui::DragFloat("Object Fade Width", &objectFadeWidthObject(), 0.1f, 0.0f, 1000.0f, "%.1f");
      ImGui::Unindent();
    }
  }

namespace fork_hooks {

  void fillWaterShaderParams(RaytraceArgs& constants) {
    WaterOptions::fillShaderParams(constants);
  }

  void showWaterShoreSettings() {
    WaterOptions::showImguiSettings();
  }

} // namespace fork_hooks

} // namespace dxvk
