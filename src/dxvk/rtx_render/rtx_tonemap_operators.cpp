#include "rtx_tonemap_operators.h"
#include "rtx_options.h"
#include "rtx_imgui.h"

#include "../imgui/imgui.h"

namespace dxvk {

  namespace {
    static const char* k_operatorItems =
      "Remix Global\0Remix Local\0None\0Hill ACES\0Narkowicz ACES\0Hable Filmic\0AgX\0Lottes\0PsychoV17_Beta\0Gran Turismo 7\0Neutwo\0\0";

    static void writeOperatorParams(ToneMappingApplyToneMappingArgs& args, TonemapOperator op) {
      if (op == TonemapOperator::Lottes) {
        args.hableExposureBias     = RtxTonemapLottes::hdrMax();
        args.hableShoulderStrength = RtxTonemapLottes::contrast();
        args.hableLinearStrength   = RtxTonemapLottes::shoulder();
        args.hableLinearAngle      = RtxTonemapLottes::midIn();
        args.hableToeStrength      = RtxTonemapLottes::midOut();
        args.hableToeNumerator     = 0.0f;
        args.hableToeDenominator   = 0.0f;
        args.hableWhitePoint       = 0.0f;
      } else {
        args.hableExposureBias     = RtxTonemapHableFilmic::exposureBias();
        args.hableShoulderStrength = RtxTonemapHableFilmic::shoulderStrength();
        args.hableLinearStrength   = RtxTonemapHableFilmic::linearStrength();
        args.hableLinearAngle      = RtxTonemapHableFilmic::linearAngle();
        args.hableToeStrength      = RtxTonemapHableFilmic::toeStrength();
        args.hableToeNumerator     = RtxTonemapHableFilmic::toeNumerator();
        args.hableToeDenominator   = RtxTonemapHableFilmic::toeDenominator();
        args.hableWhitePoint       = RtxTonemapHableFilmic::whitePoint();
      }

      args.agxSaturation = RtxTonemapAgX::saturation();
      args.agxLook       = static_cast<uint32_t>(RtxTonemapAgX::look());
      args.agxPad0       = 0.0f;
      args.agxPad1       = 0.0f;

      args.psycho17PeakValue            = 1.0f;
      args.psycho17Exposure             = RtxTonemapPsycho17::exposure();
      args.psycho17Highlights           = RtxTonemapPsycho17::highlights();
      args.psycho17Shadows              = RtxTonemapPsycho17::shadows();
      args.psycho17Contrast             = RtxTonemapPsycho17::contrast();
      args.psycho17PurityScale          = RtxTonemapPsycho17::purityScale();
      args.psycho17BleachingIntensity   = RtxTonemapPsycho17::bleachingIntensity();
      args.psycho17ClipPoint            = RtxTonemapPsycho17::clipPoint();
      args.psycho17HueRestore           = RtxTonemapPsycho17::hueRestore();
      args.psycho17AdaptationContrast   = RtxTonemapPsycho17::adaptationContrast();
      args.psycho17WhiteCurveMode       = static_cast<uint32_t>(RtxTonemapPsycho17::whiteCurveMode());
      args.psycho17ConeResponseExponent = RtxTonemapPsycho17::coneResponseExponent();
      args.psycho17GamutCompression     = RtxTonemapPsycho17::gamutCompression();
      args.psycho17GamutCompressionMode = static_cast<uint32_t>(RtxTonemapPsycho17::gamutCompressionMode());
      args.psycho17Pad0                 = 0.f;
      args.psycho17Pad1                 = 0.f;
    }

    static void showHableFilmicSliders() {
      ImGui::Indent();
      ImGui::Text("Hable Filmic Parameters:");
      if (ImGui::Button("Preset: Uncharted 2")) {
        RtxTonemapHableFilmic::shoulderStrengthObject().setDeferred(0.15f);
        RtxTonemapHableFilmic::linearStrengthObject()  .setDeferred(0.50f);
        RtxTonemapHableFilmic::linearAngleObject()     .setDeferred(0.10f);
        RtxTonemapHableFilmic::toeStrengthObject()     .setDeferred(0.20f);
        RtxTonemapHableFilmic::toeNumeratorObject()    .setDeferred(0.02f);
        RtxTonemapHableFilmic::toeDenominatorObject()  .setDeferred(0.30f);
        RtxTonemapHableFilmic::whitePointObject()      .setDeferred(11.2f);
      }
      ImGui::SameLine();
      if (ImGui::Button("Preset: Half-Life: Alyx")) {
        RtxTonemapHableFilmic::shoulderStrengthObject().setDeferred(0.319f);
        RtxTonemapHableFilmic::linearStrengthObject()  .setDeferred(0.5047f);
        RtxTonemapHableFilmic::linearAngleObject()     .setDeferred(0.1619f);
        RtxTonemapHableFilmic::toeStrengthObject()     .setDeferred(0.4667f);
        RtxTonemapHableFilmic::toeNumeratorObject()    .setDeferred(0.0f);
        RtxTonemapHableFilmic::toeDenominatorObject()  .setDeferred(0.7475f);
        RtxTonemapHableFilmic::whitePointObject()      .setDeferred(3.9996f);
      }
      RemixGui::DragFloat("Exposure Bias",     &RtxTonemapHableFilmic::exposureBiasObject(),     0.05f,  0.0f,  8.0f, "%.2f");
      RemixGui::DragFloat("Shoulder Strength", &RtxTonemapHableFilmic::shoulderStrengthObject(), 0.005f, 0.0f,  1.0f, "%.4f");
      RemixGui::DragFloat("Linear Strength",   &RtxTonemapHableFilmic::linearStrengthObject(),   0.005f, 0.0f,  1.0f, "%.4f");
      RemixGui::DragFloat("Linear Angle",      &RtxTonemapHableFilmic::linearAngleObject(),      0.005f, 0.0f,  1.0f, "%.4f");
      RemixGui::DragFloat("Toe Strength",      &RtxTonemapHableFilmic::toeStrengthObject(),      0.005f, 0.0f,  1.0f, "%.4f");
      RemixGui::DragFloat("Toe Numerator",     &RtxTonemapHableFilmic::toeNumeratorObject(),     0.001f, 0.0f,  0.5f, "%.4f");
      RemixGui::DragFloat("Toe Denominator",   &RtxTonemapHableFilmic::toeDenominatorObject(),   0.005f, 0.0f,  1.0f, "%.4f");
      RemixGui::DragFloat("White Point",       &RtxTonemapHableFilmic::whitePointObject(),       0.1f,   0.1f, 20.0f, "%.4f");
      ImGui::Unindent();
    }

    static void showAgXSliders() {
      ImGui::Indent();
      ImGui::Text("AgX Controls:");
      RemixGui::DragFloat("Saturation", &RtxTonemapAgX::saturationObject(), 0.01f, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Combo("Look", &RtxTonemapAgX::lookObject(), "None\0Golden\0Punchy\0\0");
      ImGui::Unindent();
    }

    static void showLottesSliders() {
      ImGui::Indent();
      ImGui::Text("Lottes 2016 Parameters:");
      RemixGui::DragFloat("HDR Max",         &RtxTonemapLottes::hdrMaxObject(),   0.5f,   1.0f,  64.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Lottes Contrast", &RtxTonemapLottes::contrastObject(), 0.01f,  1.0f,   3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shoulder",        &RtxTonemapLottes::shoulderObject(), 0.01f,  0.5f,   2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Mid In",          &RtxTonemapLottes::midInObject(),    0.005f, 0.01f,  1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Mid Out",         &RtxTonemapLottes::midOutObject(),   0.005f, 0.01f,  1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Unindent();
    }

    static void showPsycho17Sliders() {
      ImGui::Indent();
      ImGui::Text("PsychoV17_Beta Parameters:");
      RemixGui::DragFloat("Exposure",               &RtxTonemapPsycho17::exposureObject(),             0.01f,  0.01f,  10.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Highlights",             &RtxTonemapPsycho17::highlightsObject(),           0.01f,  0.0f,    5.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shadows",                &RtxTonemapPsycho17::shadowsObject(),              0.01f,  0.0f,    5.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Contrast",               &RtxTonemapPsycho17::contrastObject(),             0.01f,  0.0f,    5.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Purity Scale",           &RtxTonemapPsycho17::purityScaleObject(),          0.01f,  0.0f,    5.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Bleaching Intensity",    &RtxTonemapPsycho17::bleachingIntensityObject(),   0.01f,  0.0f,    1.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Hue Restore",            &RtxTonemapPsycho17::hueRestoreObject(),           0.01f,  0.0f,    1.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Cone Response Exponent", &RtxTonemapPsycho17::coneResponseExponentObject(), 0.01f,  0.01f,  10.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Gamut Compression",      &RtxTonemapPsycho17::gamutCompressionObject(),     0.01f,  0.0f,    1.f, "%.3f",  ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Combo("Gamut Compression Mode", &RtxTonemapPsycho17::gamutCompressionModeObject(), "BT.709\0BT.2020\0\0");
      ImGui::Unindent();
    }
  }

  bool RtxTonemapOperators::usesRemixGlobalPath() {
    return tonemapOperator() == TonemapOperator::RemixGlobal;
  }

  bool RtxTonemapOperators::usesRemixLocalPath() {
    return tonemapOperator() == TonemapOperator::RemixLocal;
  }

  bool RtxTonemapOperators::skipDynamicToneCurve() {
    return tonemapOperator() != TonemapOperator::RemixGlobal;
  }

  void RtxTonemapOperators::populateTonemapOperatorArgs(ToneMappingApplyToneMappingArgs& args) {
    const TonemapOperator op = tonemapOperator();
    args.tonemapOperator = static_cast<uint32_t>(op);
    args.skipDynamicToneCurve = skipDynamicToneCurve() ? 1u : 0u;
    writeOperatorParams(args, op);
  }

  void RtxTonemapOperators::syncTonemappingModeFromOperator() {
    const TonemapOperator op = tonemapOperator();
    if (op == TonemapOperator::RemixLocal) {
      RtxOptions::tonemappingModeObject().setDeferred(TonemappingMode::Local);
    } else {
      RtxOptions::tonemappingModeObject().setDeferred(TonemappingMode::Global);
    }
  }

  void RtxTonemapOperators::showTonemapOperatorUI() {
    if (RemixGui::Combo("Tonemapping Operator", &tonemapOperatorObject(), k_operatorItems)) {
      syncTonemappingModeFromOperator();
    }

    const TonemapOperator op = tonemapOperator();
    if (op == TonemapOperator::HableFilmic) {
      showHableFilmicSliders();
    } else if (op == TonemapOperator::AgX) {
      showAgXSliders();
    } else if (op == TonemapOperator::Lottes) {
      showLottesSliders();
    } else if (op == TonemapOperator::Psycho17) {
      showPsycho17Sliders();
    }
  }

}
