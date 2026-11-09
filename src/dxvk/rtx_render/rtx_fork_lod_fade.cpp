// src/dxvk/rtx_render/rtx_fork_lod_fade.cpp
//
// Fork-owned file. LOD cross fade: options, shader constants, UI and the per instance surface state.
// See docs/RemixLodFadeAPI_gta4.md.

#include "rtx_fork_lod_fade.h"

#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"
#include "rtx_materials.h"

#include "rtx/pass/raytrace_args.h"

#include "../imgui/imgui.h"

#include <algorithm>

namespace dxvk {

  namespace {
    // Set by applyLodFade while the frame's instances come in, read and cleared when the constants are filled
    bool s_fadingSeen = false;
  }

  void LodFadeOptions::fillShaderParams(RaytraceArgs& constants) {
    constants.lodFadeDebugForceOpacity = std::min(debugForceOpacity(), 255u);
    constants.lodFadeActive = (enable() && (s_fadingSeen || debugForceOpacity() != 255u)) ? 1u : 0u;
    s_fadingSeen = false;
  }

  void LodFadeOptions::showImguiSettings() {
    RemixGui::Checkbox("Fade LOD Cross Fades", &enableObject());
    if (enable()) {
      ImGui::Indent();
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
    s_fadingSeen |= surface.lodFadeOpacity != 255;
    return surface.lodFadeOpacity != 255;
  }

} // namespace fork_hooks

} // namespace dxvk
