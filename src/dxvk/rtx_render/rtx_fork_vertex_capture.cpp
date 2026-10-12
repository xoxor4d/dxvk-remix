// src/dxvk/rtx_render/rtx_fork_vertex_capture.cpp
//
// Fork-owned file. Pooled vertex capture buffers for D3D9Rtx::prepareVertexCapture.
//
// Vertex capture writes the shaded vertices of a draw into a buffer that is
// only read in the same frame, when SceneManager::processGeometryInfo copies
// the geometry into its BLAS owned buffers. Upstream created one Vulkan buffer
// per captured draw. This file hands out slices of larger pages instead and
// reuses a page once the frame that used it is through the CS thread and the
// GPU.
//
// See docs/fork-touchpoints.md for the full fork-hooks catalogue.

#include "rtx_fork_hooks.h"
#include "rtx_imgui.h"
#include "rtx_options.h"

#include "../dxvk_device.h"

#include <mutex>
#include <vector>

namespace dxvk {

  namespace {

    struct VertexCaptureOptions {
      RTX_OPTION("rtx", bool, poolVertexCaptureBuffers, true,
                 "Hands out the vertex capture buffers of shader draws as slices of shared 4 MB pages, reused once their frame "
                 "went through the CS thread and the GPU. Off creates a buffer per captured draw (upstream behavior).");
    };

    constexpr VkDeviceSize kPageSize = 4ull << 20;
    // getCurrentFrameId() counts presents on the CS thread, the D3D9 thread
    // that allocates runs ahead of it by up to the frame latency
    constexpr uint32_t kReuseFrameDelay = 8;
    constexpr uint32_t kTrimFrameDelay = 600;

    struct Page {
      Rc<DxvkBuffer> buffer;
      VkDeviceSize used = 0;
      uint32_t frameId = 0; // frame of the last slice handed out
    };

    std::mutex s_mutex;
    std::vector<Page> s_pages;
    size_t s_current = SIZE_MAX;

    Rc<DxvkBuffer> createPage(DxvkDevice* device, const VkDeviceSize size) {
      DxvkBufferCreateInfo info;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
      info.access = VK_ACCESS_TRANSFER_READ_BIT;
      info.stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.size = size;
      return device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::AppBuffer, "Vertex Capture Buffer");
    }

    bool isReusable(const Page& page, const uint32_t frameId) {
      return frameId - page.frameId >= kReuseFrameDelay && !page.buffer->isInUse();
    }

    // only called while no page is current
    void trimPages(const uint32_t frameId) {
      for (size_t i = 0; i < s_pages.size();) {
        if (frameId - s_pages[i].frameId >= kTrimFrameDelay && !s_pages[i].buffer->isInUse()) {
          std::swap(s_pages[i], s_pages.back());
          s_pages.pop_back();
        } else {
          ++i;
        }
      }
    }

  } // namespace

  namespace fork_hooks {

    DxvkBufferSlice allocVertexCaptureSlice(DxvkDevice* device, VkDeviceSize size) {
      if (!VertexCaptureOptions::poolVertexCaptureBuffers()) {
        return DxvkBufferSlice(createPage(device, size));
      }

      std::lock_guard lock(s_mutex);

      const uint32_t frameId = device->getCurrentFrameId();
      const VkDeviceSize alignment = std::max<VkDeviceSize>(256, device->properties().core.properties.limits.minStorageBufferOffsetAlignment);
      size = align(size, alignment);

      const auto fits = [size](const Page& page) { return page.buffer->info().size - page.used >= size; };

      if (s_current >= s_pages.size() || !fits(s_pages[s_current])) {
        s_current = SIZE_MAX;
        for (size_t i = 0; i < s_pages.size(); ++i) {
          if (isReusable(s_pages[i], frameId) && s_pages[i].buffer->info().size >= size) {
            s_pages[i].used = 0;
            s_current = i;
            break;
          }
        }

        if (s_current == SIZE_MAX) {
          trimPages(frameId);
          s_pages.push_back({ createPage(device, std::max(kPageSize, size)), 0, frameId });
          s_current = s_pages.size() - 1;
        }
      }

      Page& page = s_pages[s_current];
      const VkDeviceSize offset = page.used;
      page.used += size;
      page.frameId = frameId;
      return DxvkBufferSlice(page.buffer, offset, size);
    }

    void showVertexCaptureOptions() {
      RemixGui::Checkbox("Pool Vertex Capture Buffers", &VertexCaptureOptions::poolVertexCaptureBuffersObject());
    }

  } // namespace fork_hooks

} // namespace dxvk
