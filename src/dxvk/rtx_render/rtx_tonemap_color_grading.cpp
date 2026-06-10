/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx_tonemap_color_grading.h"
#include "rtx_tone_mapping.h"
#include "rtx_imgui.h"

#include "../imgui/imgui.h"

namespace dxvk {

  void TonemapColorGrading::populateApplyArgs(ToneMappingApplyToneMappingArgs& args) {
    args.colorGradingEnabled = DxvkToneMapping::colorGradingEnabled();
    args.colorBalance = DxvkToneMapping::colorBalance();
    args.contrast = DxvkToneMapping::contrast();
    args.saturation = DxvkToneMapping::saturation();
  }

  void TonemapColorGrading::populateFinalCombineArgs(FinalCombineArgs& args) {
    args.colorGradingEnabled = DxvkToneMapping::colorGradingEnabled();
    args.colorBalance = DxvkToneMapping::colorBalance();
    args.contrast = DxvkToneMapping::contrast();
    args.saturation = DxvkToneMapping::saturation();
  }

  void TonemapColorGrading::showImguiSettings() {
    RemixGui::Checkbox("Color Grading Enabled", &DxvkToneMapping::colorGradingEnabledObject());
    if (DxvkToneMapping::colorGradingEnabled()) {
      ImGui::Indent();
      RemixGui::DragFloat("Contrast", &DxvkToneMapping::contrastObject(), 0.01f, 0.f, 1.f);
      RemixGui::DragFloat("Saturation", &DxvkToneMapping::saturationObject(), 0.01f, 0.f, 1.f);
      RemixGui::DragFloat3("Color Balance", &DxvkToneMapping::colorBalanceObject(), 0.01f, 0.f, 1.f);
      ImGui::Unindent();
    }
  }

}
