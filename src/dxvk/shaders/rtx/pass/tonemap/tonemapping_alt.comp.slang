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
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/alt_tone_curve.slangh"

[[vk::binding(ALT_TONEMAPPING_COLOR_INPUT_OUTPUT)]]
RWTexture2D<float4> InOutColorBuffer;

[[vk::binding(ALT_TONEMAPPING_EXPOSURE_INPUT)]]
RWTexture1D<float> InExposure;

layout(push_constant)
ConstantBuffer<AltToneMappingArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= uint2(imageSize(InOutColorBuffer))))
  {
    return;
  }

  float3 color = imageLoad(InOutColorBuffer, threadId).xyz;
  color *= getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor);
  color = toneCurve(color, cb.peak);

  // The clamp also keeps NIS stable.
  // HDR10 output keeps values up to the peak for the PQ encode at present.
  color = clamp(color, 0.0f, max(cb.peak, 1.0f));

  InOutColorBuffer[threadId] = float4(color, 1.0f);
}
