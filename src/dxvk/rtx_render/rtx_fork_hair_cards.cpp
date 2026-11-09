// src/dxvk/rtx_render/rtx_fork_hair_cards.cpp
//
// Fork-owned file. Hair card candidate alpha test: option, shader constant, UI and the per instance surface state.

#include "rtx_fork_hair_cards.h"

#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"
#include "rtx_materials.h"

#include "rtx/pass/raytrace_args.h"

namespace dxvk {

  namespace {
    // Set by applyHairCardCandidate while the frame's instances come in, read and cleared when the constants are filled
    bool s_hairCardSeen = false;
  }

  void HairCardOptions::fillShaderParams(RaytraceArgs& constants) {
    constants.hairCardCandidateActive = (candidateAlphaTest() && s_hairCardSeen) ? 1u : 0u;
    s_hairCardSeen = false;
  }

  void HairCardOptions::showImguiSettings() {
    RemixGui::Checkbox("Hair Cards: Alpha Test at the Ray Query Candidate", &candidateAlphaTestObject());
  }

namespace fork_hooks {

  void fillHairCardShaderParams(RaytraceArgs& constants) {
    HairCardOptions::fillShaderParams(constants);
  }

  void showHairCardSettings() {
    HairCardOptions::showImguiSettings();
  }

  void applyHairCardCandidate(bool isHairCard, bool opaqueMaterial, RtSurface& surface) {
    const auto& alphaState = surface.alphaState;
    surface.candidateAlphaTest = HairCardOptions::candidateAlphaTest() && isHairCard && opaqueMaterial
      && !alphaState.isFullyOpaque && alphaState.isBlendingDisabled;
    s_hairCardSeen |= surface.candidateAlphaTest;
  }

} // namespace fork_hooks

} // namespace dxvk
