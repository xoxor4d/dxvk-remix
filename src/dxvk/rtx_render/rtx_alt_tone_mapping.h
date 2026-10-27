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
  // and HDR10 output. It ignores User Brightness and the global tonemapper's exposure and color grading.
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

    // HDR10 output is active while the D3D9 swap chain presents to an ST.2084 swap chain from a float back buffer.
    // The image then stays relative to paper white, up to the peak, and the present blit encodes it to BT.2020 PQ.
    static bool hdrOutputActive();
    static void setHdrOutputActive(bool active);

    // Returns hdrPeakNits / hdrPaperWhiteNits while HDR10 output is active.
    // Returns 1 otherwise.
    static float tonemapPeak();

    // The HDR10 back buffer holds 1.0 at UI white, so the game's SDR UI lands at hdrUiNits.
    // The scene is scaled by hdrPaperWhiteNits / hdrUiNits before its gamma encode to stay at paper white.
    static float hdrUiNitsClamped();

    // The nits options within their documented ranges.
    static float hdrPeakNitsClamped();
    static float hdrPaperWhiteNitsClamped();
    static float hdrSceneToBackBufferScale();

    RTX_OPTION("rtx.altTonemap", float, exposureBias, -2.0f,
               "The exposure in EV applied by the alt tonemapper, on top of auto exposure when it is enabled. "
               "User Brightness and rtx.tonemap.exposureBias do not affect the alt tonemapper.");

    RTX_OPTION("rtx.altTonemap", bool, hdrEnabled, true,
               "Allows HDR10 output (BT.2020 primaries, ST.2084 PQ, 10-bit swap chain) while the alt tonemapper is selected. "
               "HDR10 is used when Windows has HDR on for the game's display when the game creates its swap chain, and SDR "
               "otherwise. The other tonemappers always present SDR. Set False to always output SDR.");
    RTX_OPTION("rtx.altTonemap", float, hdrPeakNits, 1000.0f,
               "HDR10 output: the display's peak brightness in nits, which the tone curve rolls highlights off towards. "
               "Range 80 to 10000.");
    RTX_OPTION("rtx.altTonemap", float, hdrPaperWhiteNits, 203.0f,
               "HDR10 output: the brightness of the scene's SDR white in nits. 203 is the BT.2408 reference. "
               "Range 80 to 1000, and at most rtx.altTonemap.hdrPeakNits.");
    RTX_OPTION("rtx.altTonemap", float, hdrUiNits, 203.0f,
               "HDR10 output: the brightness of UI white in nits, for the game's UI, the HUD and the Remix menu. "
               "Range 80 to 1000.");
  };

}
