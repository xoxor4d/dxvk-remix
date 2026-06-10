#pragma once

#include <cstdint>

#include "rtx_option.h"
#include "rtx/pass/tonemap/tonemapping.h"

namespace dxvk {

  enum class TonemapOperator : uint32_t {
    RemixGlobal   = 0,
    RemixLocal    = 1,
    None          = 2,
    ACESHill      = 3,
    ACESNarkowicz = 4,
    HableFilmic   = 5,
    AgX           = 6,
    Lottes        = 7,
    Psycho17      = 8,
    GT7           = 9,
    Neutwo        = 10,
  };

  class RtxTonemapOperators {
  public:
    RTX_OPTION_ENV("rtx.tonemap", TonemapOperator, tonemapOperator, TonemapOperator::RemixLocal, "DXVK_TONEMAP_OPERATOR",
                   "Tonemapping path/operator selection.\n"
                   "0 = Remix Global (dynamic tone curve), 1 = Remix Local, 2 = None, "
                   "3 = Hill ACES, 4 = Narkowicz ACES, 5 = Hable Filmic, 6 = AgX, 7 = Lottes, "
                   "8 = PsychoV17_Beta, 9 = Gran Turismo 7, 10 = Neutwo.");

    static bool usesRemixGlobalPath();
    static bool usesRemixLocalPath();
    static bool skipDynamicToneCurve();

    static void populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs& args);
    static void showTonemapOperatorUI();
    static void syncTonemappingModeFromOperator();
  };

  class RtxTonemapHableFilmic {
    RTX_OPTION("rtx.tonemap.hable", float, exposureBias,     2.00f, "Hable Filmic: pre-operator exposure multiplier.");
    RTX_OPTION("rtx.tonemap.hable", float, shoulderStrength, 0.15f, "Hable Filmic: A — shoulder strength.");
    RTX_OPTION("rtx.tonemap.hable", float, linearStrength,   0.50f, "Hable Filmic: B — linear strength.");
    RTX_OPTION("rtx.tonemap.hable", float, linearAngle,      0.10f, "Hable Filmic: C — linear angle.");
    RTX_OPTION("rtx.tonemap.hable", float, toeStrength,      0.20f, "Hable Filmic: D — toe strength.");
    RTX_OPTION("rtx.tonemap.hable", float, toeNumerator,     0.02f, "Hable Filmic: E — toe numerator.");
    RTX_OPTION("rtx.tonemap.hable", float, toeDenominator,   0.30f, "Hable Filmic: F — toe denominator.");
    RTX_OPTION("rtx.tonemap.hable", float, whitePoint,       4.00f, "Hable Filmic: W — linear-scene white point.");
  };

  class RtxTonemapAgX {
    RTX_OPTION("rtx.tonemap.agx", float, saturation, 1.0f, "AgX saturation multiplier. Range [0.0, 2.0].");
    RTX_OPTION("rtx.tonemap.agx", int,   look,       0,    "AgX look preset: 0 = None, 1 = Golden, 2 = Punchy.");
  };

  class RtxTonemapLottes {
    RTX_OPTION("rtx.tonemap.lottes", float, hdrMax,   16.0f, "Lottes: peak HDR white value. Range [1.0, 64.0].");
    RTX_OPTION("rtx.tonemap.lottes", float, contrast,  1.2f, "Lottes: contrast control. Range [1.0, 3.0].");
    RTX_OPTION("rtx.tonemap.lottes", float, shoulder,  1.0f, "Lottes: shoulder strength. Range [0.5, 2.0].");
    RTX_OPTION("rtx.tonemap.lottes", float, midIn,    0.18f, "Lottes: mid-grey input. Range [0.01, 1.0].");
    RTX_OPTION("rtx.tonemap.lottes", float, midOut,   0.18f, "Lottes: mid-grey output. Range [0.01, 1.0].");
  };

  class RtxTonemapPsycho17 {
    RTX_OPTION("rtx.tonemap.psycho17", float, exposure,             1.0f,   "Psycho17: pre-operator exposure multiplier.");
    RTX_OPTION("rtx.tonemap.psycho17", float, highlights,           1.0f,   "Psycho17: highlight compression strength.");
    RTX_OPTION("rtx.tonemap.psycho17", float, shadows,              1.0f,   "Psycho17: shadow lifting strength.");
    RTX_OPTION("rtx.tonemap.psycho17", float, contrast,             1.0f,   "Psycho17: contrast adjustment.");
    RTX_OPTION("rtx.tonemap.psycho17", float, purityScale,          1.0f,   "Psycho17: chromatic purity scale.");
    RTX_OPTION("rtx.tonemap.psycho17", float, bleachingIntensity,   1.0f,   "Psycho17: Hunt-effect bleaching intensity.");
    RTX_OPTION("rtx.tonemap.psycho17", float, clipPoint,          100.0f,   "Psycho17: unused parity field.");
    RTX_OPTION("rtx.tonemap.psycho17", float, hueRestore,           1.0f,   "Psycho17: source-hue restoration.");
    RTX_OPTION("rtx.tonemap.psycho17", float, adaptationContrast,   1.0f,   "Psycho17: unused parity field.");
    RTX_OPTION("rtx.tonemap.psycho17", int,   whiteCurveMode,       0,      "Psycho17: unused parity field.");
    RTX_OPTION("rtx.tonemap.psycho17", float, coneResponseExponent, 1.0f,   "Psycho17: cone response exponent.");
    RTX_OPTION("rtx.tonemap.psycho17", float, gamutCompression,     1.0f,   "Psycho17: output gamut compression.");
    RTX_OPTION("rtx.tonemap.psycho17", int,   gamutCompressionMode, 1,      "Psycho17: 0 = BT.709, 1 = BT.2020.");
  };

}
