// src/dxvk/rtx_render/rtx_fork_rain_map.cpp
//
// Fork-owned file. Rain map: a top-down height field of the static geometry around the
// camera. Two compute passes per frame: one downward ray per texel (rain_map_trace), then
// the relief of the result (rain_map_relief): depression and slope per texel. The wetness
// model reads it for the rain shadow and for where water pools. See docs/RemixWetnessAPI_gta4.md.

#include "rtx_fork_rain_map.h"

#include "rtx_fork_hooks.h"
#include "rtx_fork_wetness.h"
#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/fork_rain_map/rain_map_binding_indices.h"
#include "rtx/pass/raytrace_args.h"

#include <rtx_shaders/rain_map_trace.h>
#include <rtx_shaders/rain_map_relief.h>

#include <algorithm>
#include <cmath>

namespace dxvk {

  namespace {

    class RainMapTraceShader : public ManagedShader {
      SHADER_SOURCE(RainMapTraceShader, VK_SHADER_STAGE_COMPUTE_BIT, rain_map_trace)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        RW_TEXTURE2D(RAIN_MAP_BINDING_HEIGHT_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(RainMapTraceShader);

    class RainMapReliefShader : public ManagedShader {
      SHADER_SOURCE(RainMapReliefShader, VK_SHADER_STAGE_COMPUTE_BIT, rain_map_relief)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        TEXTURE2D(RAIN_MAP_BINDING_HEIGHT_INPUT)
        RW_TEXTURE2D(RAIN_MAP_BINDING_MAP_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(RainMapReliefShader);

    Resources::Resource s_height;
    Resources::Resource s_map;
    Resources::Resource s_placeholder;
    uint32_t s_resolution = 0;
    bool s_enabled = false;
    bool s_traced = false;

    uint32_t requestedResolution() {
      const uint32_t res = std::clamp(uint32_t(WetnessOptions::mapResolution()), 64u, 2048u);
      return (res + 7u) & ~7u;
    }

    void ensureResources(RtxContext& ctx) {
      const uint32_t res = requestedResolution();
      if (s_map.isValid() && s_resolution == res) {
        return;
      }
      Rc<DxvkContext> dctx = &ctx;
      const VkExtent3D extent = { res, res, 1 };
      s_height = Resources::createImageResource(dctx, "Fork Rain Map Height", extent, VK_FORMAT_R32G32_SFLOAT);
      s_map = Resources::createImageResource(dctx, "Fork Rain Map", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
      s_resolution = res;
      s_traced = false;
    }

  } // anonymous namespace

  void ForkRainMap::prepare(RtxContext& ctx, RaytraceArgs& constants) {
    s_enabled = WetnessOptions::puddleQuality() >= 2;

    const uint32_t res = requestedResolution();
    const float extent = std::max(WetnessOptions::mapExtent(), 8.0f);
    const float texel = extent / float(res);

    // Snapped to the texel grid so the map does not swim with the camera
    const Vector3 camera = ctx.getSceneManager().getCamera().getPosition();
    const float originX = std::floor(camera.x / texel) * texel - extent * 0.5f;
    const float originY = std::floor(camera.y / texel) * texel - extent * 0.5f;

    constants.wetPuddleQuality = uint32_t(std::clamp(WetnessOptions::puddleQuality(), 0, 2));
    constants.wetMapResolution = res;
    constants.wetMapOriginX = originX;
    constants.wetMapOriginY = originY;
    constants.wetMapExtent = extent;
    constants.wetMapTexel = texel;
    constants.wetMapTraceTop = camera.z + std::max(WetnessOptions::mapTraceAbove(), 1.0f);
    constants.wetMapTraceBottom = camera.z - std::max(WetnessOptions::mapTraceBelow(), 1.0f);
  }

  void ForkRainMap::dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    if (!s_enabled) {
      return;
    }
    ensureResources(ctx);

    ScopedGpuProfileZone(&ctx, "Fork Rain Map");

    const uint32_t groups = (s_resolution + 7u) / 8u;

    ctx.bindCommonRayTracingResources(rtOutput);
    // The map is written below; the common set must not read it in the same dispatch
    ctx.bindResourceView(BINDING_FORK_RAIN_MAP, view(ctx), nullptr);

    ctx.bindResourceView(RAIN_MAP_BINDING_HEIGHT_OUTPUT, s_height.view, nullptr);
    ctx.getCommandList()->trackResource<DxvkAccess::Write>(s_height.image);
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RainMapTraceShader::getShader());
    ctx.dispatch(groups, groups, 1);

    ctx.bindResourceView(RAIN_MAP_BINDING_HEIGHT_INPUT, s_height.view, nullptr);
    ctx.bindResourceView(RAIN_MAP_BINDING_MAP_OUTPUT, s_map.view, nullptr);
    ctx.getCommandList()->trackResource<DxvkAccess::Read>(s_height.image);
    ctx.getCommandList()->trackResource<DxvkAccess::Write>(s_map.image);
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RainMapReliefShader::getShader());
    ctx.dispatch(groups, groups, 1);

    s_traced = true;
  }

  Rc<DxvkImageView> ForkRainMap::view(RtxContext& ctx) {
    if (s_enabled && s_traced && s_map.isValid()) {
      return s_map.view;
    }
    if (!s_placeholder.isValid()) {
      Rc<DxvkContext> dctx = &ctx;
      s_placeholder = Resources::createImageResource(dctx, "Fork Rain Map Placeholder", VkExtent3D { 1, 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
    return s_placeholder.view;
  }

namespace fork_hooks {

  void dispatchRainMap(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ForkRainMap::dispatch(ctx, rtOutput);
  }

  Rc<DxvkImageView> rainMapView(RtxContext& ctx) {
    return ForkRainMap::view(ctx);
  }

} // namespace fork_hooks

} // namespace dxvk
