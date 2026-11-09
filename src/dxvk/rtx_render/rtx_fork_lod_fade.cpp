// src/dxvk/rtx_render/rtx_fork_lod_fade.cpp
//
// Fork-owned file. Dithered LOD cross fade: options, shader constants, UI and the per instance surface state.
// See docs/RemixLodFadeAPI_gta4.md.

#include "rtx_fork_lod_fade.h"

#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"
#include "rtx_materials.h"

#include "rtx/pass/raytrace_args.h"

#include "../imgui/imgui.h"

#include <algorithm>

namespace dxvk {

  void LodFadeOptions::fillShaderParams(RaytraceArgs& constants) {
    constants.lodFadeCellSize = std::max(cellSize(), 0.001f);
    constants.lodFadeAnimated = animated() ? 1u : 0u;
    constants.lodFadeDebugForceOpacity = std::min(debugForceOpacity(), 255u);
  }

  void LodFadeOptions::showImguiSettings() {
    RemixGui::Checkbox("Dither LOD Cross Fades", &enableObject());
    if (enable()) {
      ImGui::Indent();
      RemixGui::DragFloat("Dither Cell Size", &cellSizeObject(), 0.005f, 0.001f, 10.0f, "%.3f");
      RemixGui::Checkbox("Animated Dither", &animatedObject());
      RemixGui::SliderInt("Debug: Force Opacity on all Surfaces", &debugForceOpacityObject(), 0, 255);
      ImGui::Unindent();
    }
  }

namespace fork_hooks {

  void fillLodFadeShaderParams(RaytraceArgs& constants) {
    LodFadeOptions::fillShaderParams(constants);
  }

  void showLodFadeSettings() {
    LodFadeOptions::showImguiSettings();
  }

  bool applyLodFade(const LegacyMaterialData& drawMaterialData, RtSurface& surface) {
    const uint32_t modifiers = drawMaterialData.remixModifierFromD3D;
    const bool enabled = LodFadeOptions::enable();
    const bool fading = enabled && (modifiers & REMIX_MODIFIER_FROM_D3D_LOD_FADE) != 0;

    surface.lodFadeOpacity = fading ? static_cast<uint8_t>(modifiers >> REMIX_MODIFIER_FROM_D3D_LOD_FADE_OPACITY_SHIFT) : uint8_t(255);
    surface.lodFadePartner = enabled && (modifiers & REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER) != 0;
    return surface.lodFadeOpacity != 255;
  }

} // namespace fork_hooks

} // namespace dxvk
