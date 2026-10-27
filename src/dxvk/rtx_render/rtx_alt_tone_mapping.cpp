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
#include <algorithm>
#include <atomic>

#include "rtx_alt_tone_mapping.h"
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

    std::atomic<bool> s_hdrOutputActive { false };
  }

  DxvkAltToneMapping::DxvkAltToneMapping(DxvkDevice* device)
  : CommonDeviceObject(device) {
  }

  bool DxvkAltToneMapping::hdrOutputActive() {
    return s_hdrOutputActive.load(std::memory_order_relaxed);
  }

  void DxvkAltToneMapping::setHdrOutputActive(bool active) {
    if (s_hdrOutputActive.exchange(active, std::memory_order_relaxed) != active) {
      Logger::info(str::format("[Alt Tonemapper] HDR10 output ", active ? "active" : "inactive"));
    }
  }

  float DxvkAltToneMapping::tonemapPeak() {
    if (!hdrOutputActive()) {
      return 1.0f;
    }

    return std::max(hdrPeakNitsClamped() / hdrPaperWhiteNitsClamped(), 1.0f);
  }

  float DxvkAltToneMapping::hdrPeakNitsClamped() {
    return std::clamp(hdrPeakNits(), 80.0f, 10000.0f);
  }

  float DxvkAltToneMapping::hdrPaperWhiteNitsClamped() {
    return std::clamp(hdrPaperWhiteNits(), 80.0f, 1000.0f);
  }

  float DxvkAltToneMapping::hdrUiNitsClamped() {
    return std::clamp(hdrUiNits(), 80.0f, 1000.0f);
  }

  float DxvkAltToneMapping::hdrSceneToBackBufferScale() {
    return hdrPaperWhiteNitsClamped() / hdrUiNitsClamped();
  }

  void DxvkAltToneMapping::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    if (isActive()) {
      AltTonemappingShader::getShader();
    }
  }

  void DxvkAltToneMapping::showImguiSettings() {
    RemixGui::DragFloat("Exposure", &exposureBiasObject(), 0.01f, -4.f, 4.f);

    // The HDR controls only apply while HDR10 output runs. In SDR the curve peaks at 1.
    if (hdrOutputActive()) {
      RemixGui::DragFloat("Peak Brightness", &hdrPeakNitsObject(), 1.0f, 80.0f, 10000.0f, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Paper White", &hdrPaperWhiteNitsObject(), 1.0f, 80.0f, 1000.0f, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("UI Brightness", &hdrUiNitsObject(), 1.0f, 80.0f, 1000.0f, "%.0f nits", ImGuiSliderFlags_AlwaysClamp);
    } else {
      ImGui::TextWrapped("HDR10 output is inactive. It starts when Windows HDR is on for this display when the game creates its swap chain.");
    }
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

    ctx->bindResourceView(ALT_TONEMAPPING_COLOR_INPUT_OUTPUT, colorBuffer.view, nullptr);
    ctx->bindResourceView(ALT_TONEMAPPING_EXPOSURE_INPUT, exposureView, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, AltTonemappingShader::getShader());
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }
}
