/*
* Copyright (c) 2023-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/pass/view_distance_args.h"

// Note: Ensure 16B alignment
struct SkyNearDistanceArgs {
  uint16_t distanceMode;
  uint16_t distanceFunction;
  uint enableShadows;
  float distanceThresholdOrFadeMin;
  float distanceFadeSpan;
  float noiseScale;
  uint pad0;
  uint pad1;
  uint pad2;
};

#ifdef __cplusplus
static_assert((sizeof(SkyNearDistanceArgs) & 15) == 0);

namespace dxvk {

struct SkyNearDistanceOptions {
  friend class ImGUI;
  friend class RtxOptions;

  RTX_OPTION("rtx.skyNearDistance", ViewDistanceMode, distanceMode, ViewDistanceMode::None, "The sky near-distance mode for reprojected 3D skybox geometry. None disables it, Hard Cutoff rejects sky closer than a threshold, and Coherent Noise feathers primary visibility in with distance using a stable worldspace noise pattern. Shadows hard-cut at the threshold (Hard Cutoff) or fade max (Coherent Noise).");
  RTX_OPTION("rtx.skyNearDistance", ViewDistanceFunction, distanceFunction, ViewDistanceFunction::Euclidean, "The sky near-distance function, Euclidean is a simple distance from the camera, whereas Planar Euclidean will ignore distance across the world's \"up\" direction.");
  RTX_OPTION_ARGS("rtx.skyNearDistance", float, distanceThreshold, 200.0f, "Reject reprojected sky geometry closer than this distance based on the result of the distance function, only used for the Hard Cutoff sky near-distance mode.",
                  args.minValue = 0.0f);
  public: static void distanceFadeMinOnChange(DxvkDevice* device);
  RTX_OPTION_ARGS("rtx.skyNearDistance", float, distanceFadeMin, 100.0f, "The distance based on the result of the distance function below which reprojected sky geometry is always rejected for primary visibility, only used for the Coherent Noise sky near-distance mode.",
                  args.minValue = 0.0f, args.onChangeCallback = &distanceFadeMinOnChange);
  public: static void distanceFadeMaxOnChange(DxvkDevice* device);
  RTX_OPTION_ARGS("rtx.skyNearDistance", float, distanceFadeMax, 300.0f, "The distance based on the result of the distance function at and beyond which reprojected sky geometry is always kept for primary visibility, only used for the Coherent Noise sky near-distance mode. Shadows also hard-cut at this distance (no shadows in the fade band).",
                  args.minValue = 0.0f, args.onChangeCallback = &distanceFadeMaxOnChange);
  RTX_OPTION("rtx.skyNearDistance", float, noiseScale, 3.0f, "The scale per meter value applied to the world space position fed into the noise generation function for generating the fade in Coherent Noise sky near-distance mode.");
  RTX_OPTION("rtx.skyNearDistance", bool, enableShadows, true, "When true, reprojected 3D skybox geometry can cast shadows onto the world. When false, reprojected sky casts no shadows. With near-distance enabled, shadows hard-cut at the threshold (Hard Cutoff) or fade max (Coherent Noise).");

public:
  static void fillShaderParams(SkyNearDistanceArgs& args, float meterToWorldUnitScale) {
    const auto cachedDistanceMode = distanceMode();
    const auto cachedDistanceFadeMax = distanceFadeMax();
    const auto cachedDistanceFadeMin = distanceFadeMin();

    args.distanceMode = static_cast<uint16_t>(cachedDistanceMode);
    args.distanceFunction = static_cast<uint16_t>(distanceFunction());
    args.enableShadows = enableShadows() ? 1u : 0u;

    if (cachedDistanceMode == ViewDistanceMode::HardCutoff) {
      args.distanceThresholdOrFadeMin = distanceThreshold();
    } else if (cachedDistanceMode == ViewDistanceMode::CoherentNoise) {
      assert(cachedDistanceFadeMax >= cachedDistanceFadeMin);

      args.distanceThresholdOrFadeMin = cachedDistanceFadeMin;
      args.distanceFadeSpan = cachedDistanceFadeMax - cachedDistanceFadeMin;
      args.noiseScale = noiseScale() / meterToWorldUnitScale;
    }
  }
};

} // namespace dxvk

#endif
