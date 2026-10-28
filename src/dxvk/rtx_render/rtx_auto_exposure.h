/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "dxvk_format.h"
#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"

#include "../spirv/spirv_code_buffer.h"
#include "../util/util_matrix.h"
#include "rtx_options.h"

namespace dxvk {

  class DxvkDevice;

  class DxvkAutoExposure: public CommonDeviceObject {
  public:
    explicit DxvkAutoExposure(DxvkDevice* device);
    ~DxvkAutoExposure();

    void dispatch(
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::RaytracingOutput& rtOutput,
      const float frameTimeMilliseconds,
      bool resetHistory = false);

    void showImguiSettings();

    void createResources(Rc<DxvkContext> ctx);
    const Resources::Resource& getExposureTexture() const { return m_exposure; }

  private:

    void dispatchAutoExposure(
      Rc<DxvkContext> ctx,
      Rc<DxvkSampler> linearSampler,
      const Resources::RaytracingOutput& rtOutput,
      const float frameTimeMilliseconds);

    Rc<vk::DeviceFn> m_vkd;

    Resources::Resource m_exposure;
    Resources::Resource m_exposureHistogram;

    bool m_resetState = true;

    // Eye-adaptation settings.
    //
    // Perceptual auto-exposure pipeline (replaces the prior
    // Naka-Rushton resolve):
    //   1. Per-pixel CIE 170-2 luminosity Yf (Stockman-Sharpe LMS,
    //      shared with the psycho17 tonemap operator) is binned into a
    //      log2-Yf histogram.
    //   2. The resolve pass takes a geometric (log) mean across bins,
    //      giving the adapted scene Yf level the observer model would
    //      settle on.
    //   3. The target exposure scale is computed from a first-site
    //      cone-contrast law, exposure = Y_target / (Y_adapt + Y_noise)
    //      (Stockman & Brainard 2010), with Y_target = mid-gray (0.18)
    //      and Y_noise = the cone-system noise floor. This caps the
    //      dark-scene boost without an arbitrary clamp.
    //   4. The stored exposure is advanced toward that target in
    //      log-space with asymmetric exponential dynamics — light_tau
    //      while brightening (cone-bleach), dark_tau while dimming
    //      (rod-recovery). Log-space blending makes the tau invariant
    //      to absolute scene level.
    //
    // The exposure scalar this produces is consumed by every tonemap
    // operator. The psycho17 operator additionally reads it back as an
    // adaptive-state hint (in BT.709 mid-gray space) for its cone-
    // response stage; the other operators ignore that channel.
    RTX_OPTION("rtx.autoExposure", bool, enabled, true,
               "Automatically adjusts exposure so the image is neither too bright nor too dark. "
               "Uses a perceptual observer model (geometric-mean Yf + first-site cone contrast).");
    RTX_OPTION("rtx.autoExposure", float, lightAdaptTau, 0.15f,
               "Photopic (light) adaptation time constant in seconds. "
               "Controls how quickly the eye dims down when the scene brightens. "
               "Smaller = faster response. Typical range: 0.05 – 0.50 s.");
    RTX_OPTION("rtx.autoExposure", float, darkAdaptTau, 0.75f,
               "Scotopic (dark) adaptation time constant in seconds. "
               "Controls how slowly the eye brightens up when the scene dims. "
               "Larger = slower response. Typical range: 0.25 – 3.00 s.");
    RTX_OPTION("rtx.autoExposure", float, targetAdaptedYf, 0.18f,
               "Adaptation target Yf (mid-gray reflectance, default 0.18). "
               "After exposure is applied the scene's geometric-mean Yf lands here. "
               "Raise above 0.18 to bias the image brighter; lower to darken it.");
    RTX_OPTION("rtx.autoExposure", float, maxExposure, 8.0f,
               "Maximum auto-exposure multiplier. Caps how much the algorithm is allowed "
               "to brighten the image when looking at something dark (1.0 = no brightening, "
               "8.0 = up to 8x brighter, ~56 = uncapped default behaviour). Lower this if "
               "shadows / dark rooms get blown out into mid-gray.");
    RTX_OPTION("rtx.autoExposure", float, minExposure, 0.0f,
               "Minimum auto-exposure multiplier. Caps how much the algorithm is allowed to darken the image "
               "when the metered area is bright (0 = no floor, 0.25 = at most 2 stops darker than neutral, "
               "0.125 = at most 3 stops). Raise this if bright walls, ground or sky in the metered area pull "
               "the rest of the scene too dark.");
    RTX_OPTION("rtx.autoExposure", float, meteringSigma, 0.25f,
               "Radius of the center-weighted metering, as a Gaussian sigma in units of the shorter screen axis. "
               "0.25 meters a spot around the screen centre (the periphery barely counts), 0.5 weights most of "
               "the frame, 2.0 is close to a plain average. Larger values react less to what happens to be in "
               "the centre of the view.");
  };
  
}
