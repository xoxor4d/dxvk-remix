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
#pragma once

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx_fork_tonemap.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkPipelineManager;

  // Alternative tonemapper: exposure, the tone curve in shaders/rtx/pass/tonemap/alt_tone_curve.slangh,
  // and HDR10 output. It ignores User Brightness and the global tonemapper's exposure; the global
  // tonemapper's color grading applies after its curve.
  // The HDR10 output state and display settings live in RtxForkHdrOutput, shared with Psycho17.
  class DxvkAltToneMapping : public CommonDeviceObject {
  public:
    explicit DxvkAltToneMapping(DxvkDevice* device);

    void dispatch(
      Rc<RtxContext> ctx,
      Rc<DxvkImageView> exposureView,
      const Resources::RaytracingOutput& rtOutput,
      bool autoExposureEnabled);

    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void showImguiSettings();

    static bool isActive() { return RtxForkGlobalTonemap::tonemapOperator() == TonemapOperator::Alt; }

    static float exposureFactor() { return exp2f(exposureBias()); }

    static bool hdrOutputActive() { return RtxForkHdrOutput::active(); }

    // The curve's peak relative to paper white: 1 in SDR, peakNits / paperWhiteNits under HDR10 output.
    static float tonemapPeak() { return RtxForkHdrOutput::peak(); }

    RTX_OPTION("rtx.altTonemap", float, exposureBias, -2.0f,
               "The exposure in EV applied by the alt tonemapper, on top of auto exposure when it is enabled. "
               "User Brightness and rtx.tonemap.exposureBias do not affect the alt tonemapper.");
  };

}
