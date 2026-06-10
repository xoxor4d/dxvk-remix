/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/color_grading.slangh"
#include "rtx/pass/tonemap/tonemap_operators.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT)]]
RWTexture2D<float4> InColorBuffer;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_TONE_CURVE_INPUT)]]
Sampler1D<float> InToneCurve;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT)]]
RWTexture1D<float> InExposure;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT)]]
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<ToneMappingApplyToneMappingArgs> cb;

float3 dynamicToneMapper(float3 inputColor)
{
  float luminance = max(calcBt709Luminance(inputColor), exp2(cb.toneCurveMinStops));

  const float logLuminance = log2(luminance);
  const float biasedLuminance = (logLuminance - cb.toneCurveMinStops) / (cb.toneCurveMaxStops - cb.toneCurveMinStops);

  const float outLogLuminance = InToneCurve.SampleLevel(biasedLuminance, 0).r;
  float outLuminance = exp2(outLogLuminance);

  float3 mappedColor = inputColor * outLuminance / luminance;

  return mappedColor;
}

float3 applyToneMapping(float3 color, uint2 pixelPosition)
{
  color *= getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor);

  if (cb.toneMappingEnabled != 0)
  {
    if (cb.debugMode == 0)
    {
      if (cb.skipDynamicToneCurve == 0)
      {
        color = dynamicToneMapper(color);

        if (cb.finalizeWithACES)
        {
          color = ACESFilm(color, cb.useLegacyACES);
        }
      }
      else if (cb.tonemapOperator != tonemapOperatorRemixGlobal &&
               cb.tonemapOperator != tonemapOperatorRemixLocal)
      {
        const float3 adaptiveStateBT709 = float3(0.18f, 0.18f, 0.18f);
        color = applyTonemapOperator(cb.tonemapOperator, color, /*suppressBlackLevelClamp=*/false,
                                     cb.hableExposureBias,
                                     cb.hableShoulderStrength,
                                     cb.hableLinearStrength,
                                     cb.hableLinearAngle,
                                     cb.hableToeStrength,
                                     cb.hableToeNumerator,
                                     cb.hableToeDenominator,
                                     cb.hableWhitePoint,
                                     cb.agxSaturation,
                                     cb.agxLook,
                                     cb.psycho17PeakValue,
                                     cb.psycho17Exposure,
                                     cb.psycho17Highlights,
                                     cb.psycho17Shadows,
                                     cb.psycho17Contrast,
                                     cb.psycho17PurityScale,
                                     cb.psycho17BleachingIntensity,
                                     cb.psycho17ClipPoint,
                                     cb.psycho17HueRestore,
                                     cb.psycho17AdaptationContrast,
                                     cb.psycho17WhiteCurveMode,
                                     cb.psycho17ConeResponseExponent,
                                     cb.psycho17GamutCompression,
                                     cb.psycho17GamutCompressionMode,
                                     adaptiveStateBT709);
      }
    }
    else
    {
      float2 texCoord = pixelPosition / float2(imageSize(InColorBuffer));

      if (texCoord.x > 0.475f)
      {
        color = dynamicToneMapper(color);
      }

      float histogramValue = InToneCurve.SampleLevel(texCoord.x, 0).r;

      const float curveScale = 0.5;
      float plotValue = 1.0 + histogramValue * curveScale / 15.5876;
      plotValue = 1.0f - plotValue / 1.0;
      float width = 0.01f;

      if (texCoord.y >= plotValue - width/2 && texCoord.y <= plotValue + width/2)
      {
        color = mix(color, float3(1.0f, 1.0f, 1.0f), 0.5f);
      }
    }
  }

  if (cb.colorGradingEnabled > 0)
  {
    color = applyColorGrading(color, cb.colorBalance, cb.contrast, cb.saturation);
  }

  return color;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  float3 inputColor = imageLoad(InColorBuffer, threadId).xyz;

  float3 outputColor = applyToneMapping(inputColor, threadId);

  outputColor = saturate(outputColor);

  OutColorBuffer[threadId] = vec4(outputColor, 1.0f);
}
