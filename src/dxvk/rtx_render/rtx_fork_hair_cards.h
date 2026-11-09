#pragma once

// rtx_fork_hair_cards.h - fork-owned. Hair cards (rtx.hairCardTextures) alpha tested at the ray query candidate
// (rtx.hairCards.*): a cut out texel is never committed and the traversal goes on behind the card. Resolving the alpha
// test after the hit re-traces from an origin pushed through the card by the self intersection offset, and the scalp
// under a hair cap lies within that offset, so such a ray started inside the head. The shader side lives in
// shaders/rtx/algorithm/rtx_fork_hair_cards.slangh.

#include "rtx_option.h"

struct RaytraceArgs;

namespace dxvk {

  class HairCardOptions {
  public:
    RTX_OPTION("rtx.hairCards", bool, candidateAlphaTest, true,
               "Alpha tests hair cards (rtx.hairCardTextures) during traversal, at the ray query candidate, instead of after the hit. "
               "A cut out texel is then never a hit and a surface right behind the card (the head under a hair cap) is not skipped by the "
               "re-trace. Costs a texture tap per hair card candidate and drops the force-opaque ray flag while a hair card is in the scene.");

    static void fillShaderParams(RaytraceArgs& constants);
    static void showImguiSettings();
  };

} // namespace dxvk
