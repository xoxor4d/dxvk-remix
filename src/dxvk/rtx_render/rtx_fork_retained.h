// src/dxvk/rtx_render/rtx_fork_retained.h
//
// Fork-owned. Retained instances for the Remix API: a host registers a draw once
// (CreateRetainedInstances), reports changes (UpdateRetainedInstance(s),
// DestroyRetainedInstances) and replays the whole table with one DrawRetainedInstances
// call per frame. The replay runs every visible entry through
// SceneManager::submitExternalDraw until the preserve path reports it unchanged for a
// few frames; the entry is then pinned and a frame only keeps its instance alive (the
// two preserveInstance calls), until a change to the entry or a global signal (texture
// cache generation, option invalidation, object picking) drops the pin.
//
// Handles are chosen by the host. Commands apply in call order at the next
// DrawRetainedInstances. Hidden entries are skipped; their instances age out through
// the normal garbage collection.
//
// Remix Plus V2 ships retained instances with a scene owned lane; this is the legacy
// lane subset and is not worth picking onto V2 (docs/fork-touchpoints_gta4.md).

#pragma once

#include "rtx_scene_manager.h"

#include <remix/remix_c.h>

#include <memory>
#include <vector>

namespace dxvk {

class RtxContext;

namespace fork_hooks {

  struct RetainedCommand {
    enum class Type : uint8_t { Create, Describe, SetTransform, SetVisible, SetLodFade, Destroy };

    Type type = Type::Create;
    uint64_t handle = 0;
    std::unique_ptr<ExternalDrawState> state;  // Create, Describe
    Matrix4 transform;                         // SetTransform
    bool visible = true;                       // SetVisible
    uint16_t lodFadeState = 255;               // SetLodFade: remixapi_RetainedInstanceUpdate::lodFadeState
  };

  using ConvertInstanceFn = std::unique_ptr<ExternalDrawState> (*)(const remixapi_InstanceInfo&);
  using ConvertTransformFn = Matrix4 (*)(const remixapi_Transform&);

  // API thread. Every item is validated before any is converted; false = invalid arguments, nothing queued.
  bool retainedBuildCreates(const remixapi_InstanceInfo* infos, const uint64_t* handles, uint32_t count,
                            ConvertInstanceFn convert, std::vector<RetainedCommand>& out);
  bool retainedBuildDescribe(uint64_t handle, const remixapi_InstanceInfo* info,
                             ConvertInstanceFn convert, std::vector<RetainedCommand>& out);
  bool retainedBuildUpdates(const remixapi_RetainedInstanceUpdate* updates, uint32_t count,
                            ConvertTransformFn convert, std::vector<RetainedCommand>& out);
  bool retainedBuildDestroys(const uint64_t* handles, uint32_t count, std::vector<RetainedCommand>& out);

  // CS thread
  void retainedApply(std::vector<RetainedCommand>& commands);
  void retainedDraw(RtxContext* ctx);

  // Friend of SceneManager: the keep-alive of a pinned entry needs what externalDrawTryPreserve uses.
  struct RetainedSceneAccess {
    static void preserve(SceneManager& scene, RtInstance& instance, const DrawCallState* drawCall) {
      scene.preserveInstance(instance, drawCall);
    }
    static InstanceManager& instances(SceneManager& scene) {
      return scene.m_instanceManager;
    }
    static DrawCallTracker& tracker(SceneManager& scene) {
      return scene.m_drawCallTracker;
    }
    static uint32_t textureCacheGenerationValidForPreserve(const SceneManager& scene) {
      return scene.m_textureCacheGenerationValidForPreserve;
    }
  };

} // namespace fork_hooks
} // namespace dxvk
