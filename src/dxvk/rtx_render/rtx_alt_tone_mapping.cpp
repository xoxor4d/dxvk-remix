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
#include "rtx_alt_tone_mapping.h"
#include "rtx_fork_hooks.h"
#include "rtx_tone_mapping.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx.h"
#include "rtx_imgui.h"
#include "rtx/pass/tonemap/tonemapping.h"

#include <rtx_shaders/tonemapping_alt.h>

namespace dxvk {
  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class AltTonemappingShader : public ManagedShader {
      SHADER_SOURCE(AltTonemappingShader, VK_SHADER_STAGE_COMPUTE_BIT, tonemapping_alt)

      PUSH_CONSTANTS(AltToneMappingArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(ALT_TONEMAPPING_COLOR_INPUT_OUTPUT)
        RW_TEXTURE1D_READONLY(ALT_TONEMAPPING_EXPOSURE_INPUT)
      END_PARAMETER()
    };
  }

  DxvkAltToneMapping::DxvkAltToneMapping(DxvkDevice* device)
  : CommonDeviceObject(device) {
  }

  void DxvkAltToneMapping::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    if (isActive()) {
      AltTonemappingShader::getShader();
    }
  }

  void DxvkAltToneMapping::showImguiSettings() {
    RemixGui::DragFloat("Exposure", &exposureBiasObject(), 0.01f, -4.f, 4.f);
    fork_hooks::showHdrOutputSettings();
  }

  void DxvkAltToneMapping::dispatch(
    Rc<RtxContext> ctx,
    Rc<DxvkImageView> exposureView,
    const Resources::RaytracingOutput& rtOutput,
    bool autoExposureEnabled) {

    ScopedGpuProfileZone(ctx, "Alt Tone Mapping");

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    const Resources::Resource& colorBuffer = rtOutput.m_finalOutput.resource(Resources::AccessType::ReadWrite);
    const VkExtent3D workgroups = util::computeBlockCount(colorBuffer.view->imageInfo().extent, VkExtent3D { 16, 16, 1 });

    AltToneMappingArgs pushArgs = {};
    pushArgs.enableAutoExposure = autoExposureEnabled;
    pushArgs.exposureFactor = exposureFactor();
    pushArgs.peak = tonemapPeak();
    pushArgs.colorGradingEnabled = DxvkToneMapping::colorGradingEnabled();
    pushArgs.colorBalance = DxvkToneMapping::colorBalance();
    pushArgs.contrast = DxvkToneMapping::contrast();
    pushArgs.saturation = DxvkToneMapping::saturation();

    ctx->bindResourceView(ALT_TONEMAPPING_COLOR_INPUT_OUTPUT, colorBuffer.view, nullptr);
    ctx->bindResourceView(ALT_TONEMAPPING_EXPOSURE_INPUT, exposureView, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, AltTonemappingShader::getShader());
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }
}
