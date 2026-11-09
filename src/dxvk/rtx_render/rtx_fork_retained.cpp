// src/dxvk/rtx_render/rtx_fork_retained.cpp
//
// Fork-owned. The retained instance table and its per frame replay, see rtx_fork_retained.h.

#include "rtx_fork_retained.h"
#include "rtx_fork_hooks.h"

#include "rtx_asset_replacer.h"
#include "rtx_context.h"
#include "rtx_draw_call_tracker.h"
#include "rtx_instance_manager.h"
#include "rtx_materials.h"
#include "rtx_option_manager.h"
#include "rtx_options.h"
#include "rtx_texture_manager.h"
#include "rtx_types.h"

#include "dxvk_device.h"
#include "dxvk_objects.h"
#include "dxvk_scoped_annotation.h"

#include "../../util/log/log.h"
#include "../../util/util_once.h"
#include "../../util/util_string.h"

#include <unordered_map>

namespace dxvk {
namespace {

  class RetainedOptions {
    RTX_OPTION_ARGS("rtx.retained", uint32_t, pinAfterPreservedFrames, 2,
                    "Consecutive frames the preserve path has to report a retained instance unchanged before it is pinned. "
                    "A pinned entry skips the per frame checks and only keeps its instance alive.",
                    args.minValue = 1u, args.maxValue = 60u);
    RTX_OPTION_ARGS("rtx.retained", uint32_t, logFrameInterval, 0,
                    "Logs a [Retained] line with the entry counts (visible, pinned, preserved, full replays, dropped) every "
                    "this many frames. 0 = off.",
                    args.minValue = 0u, args.maxValue = 100000u);
  };

  struct Entry {
    ExternalDrawState state;  // as converted from the host's description; the replay works on a copy
    XXH64_hash_t identityHash = 0;
    bool visible = true;
    bool pinned = false;
    uint32_t preservedStreak = 0;
  };

  struct FrameStats {
    uint32_t visible = 0;
    uint32_t pinned = 0;
    uint32_t preserved = 0;
    uint32_t full = 0;
    uint32_t dropped = 0;
  };

  // CS thread only
  std::unordered_map<uint64_t, Entry> s_entries;

  bool validInstanceInfo(const remixapi_InstanceInfo* info) {
    return info != nullptr && info->sType == REMIXAPI_STRUCT_TYPE_INSTANCE_INFO && info->mesh != nullptr;
  }

  void unpin(Entry& entry) {
    entry.pinned = false;
    entry.preservedStreak = 0;
  }

  void setDescription(Entry& entry, ExternalDrawState&& state) {
    entry.state = std::move(state);
    entry.identityHash = entry.state.computeExternalDrawIdentityHash();
    unpin(entry);
  }

  Entry* findEntry(const uint64_t handle, const char* call) {
    const auto it = s_entries.find(handle);
    if (it == s_entries.end()) {
      ONCE(Logger::warn(str::format("[Retained] ", call, ": unknown handle 0x", std::hex, handle, std::dec)));
      return nullptr;
    }
    return &it->second;
  }

  // The keep-alive of a pinned entry: what externalDrawTryPreserve does for an unchanged draw, without the
  // checks the entry passed in the frames before. False when the instance is gone or changed.
  bool keepAlive(SceneManager& scene, const Entry& entry, const uint32_t frame) {
    ReplacementInstance* ri = fork_hooks::RetainedSceneAccess::tracker(scene).findReplacementInstanceByIdentity(entry.identityHash);
    if (ri == nullptr || ri->prims.size() != 1 || !ri->dirtyFlags.isClear()) {
      return false;
    }

    // a second DrawRetainedInstances in the same frame id (the id is the present count, which the CS thread can run
    // ahead of) finds the entry alive already
    if (ri->frameLastSeen == frame) {
      return true;
    }

    RtInstance* instance = ri->prims[0].getInstance();
    if (instance == nullptr || instance->isMarkedForGC() || instance->getBlas() == nullptr || instance->getBlas()->frameLastUpdated == frame) {
      return false;
    }

    fork_hooks::RetainedSceneAccess::preserve(scene, *instance, &entry.state.drawCall);
    fork_hooks::RetainedSceneAccess::instances(scene).preserveInstance(*instance, entry.state.drawCall, nullptr);
    ri->frameLastSeen = frame;
    return true;
  }

} // namespace

namespace fork_hooks {

  bool retainedBuildCreates(const remixapi_InstanceInfo* infos, const uint64_t* handles, uint32_t count,
                            ConvertInstanceFn convert, std::vector<RetainedCommand>& out) {
    if (infos == nullptr || handles == nullptr || count == 0) {
      return false;
    }
    for (uint32_t i = 0; i < count; i++) {
      if (handles[i] == 0 || !validInstanceInfo(&infos[i])) {
        return false;
      }
    }

    out.reserve(out.size() + count);
    for (uint32_t i = 0; i < count; i++) {
      RetainedCommand& command = out.emplace_back();
      command.type = RetainedCommand::Type::Create;
      command.handle = handles[i];
      command.state = convert(infos[i]);
    }
    return true;
  }

  bool retainedBuildDescribe(uint64_t handle, const remixapi_InstanceInfo* info,
                             ConvertInstanceFn convert, std::vector<RetainedCommand>& out) {
    if (handle == 0 || !validInstanceInfo(info)) {
      return false;
    }

    RetainedCommand& command = out.emplace_back();
    command.type = RetainedCommand::Type::Describe;
    command.handle = handle;
    command.state = convert(*info);
    return true;
  }

  bool retainedBuildUpdates(const remixapi_RetainedInstanceUpdate* updates, uint32_t count,
                            ConvertTransformFn convert, std::vector<RetainedCommand>& out) {
    constexpr uint32_t kKnownFlags = REMIXAPI_RETAINED_UPDATE_TRANSFORM | REMIXAPI_RETAINED_UPDATE_VISIBILITY | REMIXAPI_RETAINED_UPDATE_LOD_FADE;
    if (updates == nullptr || count == 0) {
      return false;
    }
    for (uint32_t i = 0; i < count; i++) {
      if (updates[i].handle == 0 || updates[i].flags == 0 || (updates[i].flags & ~kKnownFlags) != 0) {
        return false;
      }
      if ((updates[i].flags & REMIXAPI_RETAINED_UPDATE_LOD_FADE) && updates[i].lodFadeState > 0x1FFu) {
        return false;
      }
    }

    out.reserve(out.size() + count);
    for (uint32_t i = 0; i < count; i++) {
      const remixapi_RetainedInstanceUpdate& update = updates[i];
      if (update.flags & REMIXAPI_RETAINED_UPDATE_TRANSFORM) {
        RetainedCommand& command = out.emplace_back();
        command.type = RetainedCommand::Type::SetTransform;
        command.handle = update.handle;
        command.transform = convert(update.transform);
      }
      if (update.flags & REMIXAPI_RETAINED_UPDATE_VISIBILITY) {
        RetainedCommand& command = out.emplace_back();
        command.type = RetainedCommand::Type::SetVisible;
        command.handle = update.handle;
        command.visible = update.visible != 0;
      }
      if (update.flags & REMIXAPI_RETAINED_UPDATE_LOD_FADE) {
        RetainedCommand& command = out.emplace_back();
        command.type = RetainedCommand::Type::SetLodFade;
        command.handle = update.handle;
        command.lodFadeState = static_cast<uint16_t>(update.lodFadeState);
      }
    }
    return true;
  }

  bool retainedBuildDestroys(const uint64_t* handles, uint32_t count, std::vector<RetainedCommand>& out) {
    if (handles == nullptr || count == 0) {
      return false;
    }
    for (uint32_t i = 0; i < count; i++) {
      if (handles[i] == 0) {
        return false;
      }
    }

    out.reserve(out.size() + count);
    for (uint32_t i = 0; i < count; i++) {
      RetainedCommand& command = out.emplace_back();
      command.type = RetainedCommand::Type::Destroy;
      command.handle = handles[i];
    }
    return true;
  }

  void retainedApply(std::vector<RetainedCommand>& commands) {
    for (RetainedCommand& command : commands) {
      switch (command.type) {
      case RetainedCommand::Type::Create: {
        // ONCE captures by value and RetainedCommand is move-only, so the lambdas below only see the handle
        const uint64_t handle = command.handle;
        auto [it, inserted] = s_entries.try_emplace(handle);
        if (!inserted) {
          ONCE(Logger::warn(str::format("[Retained] CreateRetainedInstances: handle 0x", std::hex, handle, std::dec,
                                        " is live already, its description is replaced")));
        }
        setDescription(it->second, std::move(*command.state));
        break;
      }
      case RetainedCommand::Type::Describe:
        if (Entry* entry = findEntry(command.handle, "UpdateRetainedInstance")) {
          setDescription(*entry, std::move(*command.state));
        }
        break;
      case RetainedCommand::Type::SetTransform:
        if (Entry* entry = findEntry(command.handle, "UpdateRetainedInstances")) {
          entry->state.drawCall.modifyTransformData().objectToWorld = command.transform;
          entry->identityHash = entry->state.computeExternalDrawIdentityHash();
          unpin(*entry);
        }
        break;
      case RetainedCommand::Type::SetVisible:
        if (Entry* entry = findEntry(command.handle, "UpdateRetainedInstances")) {
          entry->visible = command.visible;
          if (!command.visible) {
            unpin(*entry);
          }
        }
        break;
      case RetainedCommand::Type::SetLodFade:
        if (Entry* entry = findEntry(command.handle, "UpdateRetainedInstances")) {
          // the fade bit is part of the material identity and changes the instance flags, so it takes the full path
          // once; the opacity byte and the partner bit alone are picked up by the keep-alive (SceneManager::preserveInstance)
          uint32_t& modifiers = entry->state.drawCall.modifyMaterialData().remixModifierFromD3D;
          const bool wasFading = (modifiers & REMIX_MODIFIER_FROM_D3D_LOD_FADE) != 0;
          const uint32_t opacity = command.lodFadeState & 0xFFu;
          const bool fading = opacity != 255;
          modifiers &= ~(REMIX_MODIFIER_FROM_D3D_LOD_FADE | REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER | (0xFFu << REMIX_MODIFIER_FROM_D3D_LOD_FADE_OPACITY_SHIFT));
          if (fading) {
            modifiers |= REMIX_MODIFIER_FROM_D3D_LOD_FADE | (opacity << REMIX_MODIFIER_FROM_D3D_LOD_FADE_OPACITY_SHIFT);
          }
          if (command.lodFadeState & REMIXAPI_LOD_FADE_STATE_PARTNER) {
            modifiers |= REMIX_MODIFIER_FROM_D3D_LOD_FADE_PARTNER;
          }
          if (fading != wasFading) {
            unpin(*entry);
          }
        }
        break;
      case RetainedCommand::Type::Destroy: {
        const uint64_t handle = command.handle;
        if (s_entries.erase(handle) == 0) {
          ONCE(Logger::warn(str::format("[Retained] DestroyRetainedInstances: unknown handle 0x", std::hex, handle, std::dec)));
        }
        break;
      }
      }
    }
  }

  void retainedDraw(RtxContext* ctx) {
    ScopedCpuProfileZone();

    DxvkDevice* device = ctx->getDevice().ptr();
    const uint32_t frame = device->getCurrentFrameId();

    SceneManager& scene = ctx->getSceneManager();
    DxvkObjects* common = device->getCommon();
    AssetReplacer& replacer = *scene.getAssetReplacer();

    // the signals that send an unchanged draw down the dynamic path in externalDrawTryPreserve; a pinned entry
    // has to pass that check again before it is pinned again
    const bool pinAllowed =
      RtxOptions::enablePreservePath() && externalPreserveEnabled() &&
      !RtxOptionManager::isDrawcallTranslationInvalid() &&
      !common->getResources().getRaytracingOutput().m_primaryObjectPicking.isValid() &&
      common->getTextureManager().getTextureCacheGeneration() == RetainedSceneAccess::textureCacheGenerationValidForPreserve(scene);
    const uint32_t pinAfter = RetainedOptions::pinAfterPreservedFrames();

    FrameStats stats;
    for (auto it = s_entries.begin(); it != s_entries.end();) {
      Entry& entry = it->second;
      if (!entry.visible) {
        ++it;
        continue;
      }
      stats.visible++;

      if (!pinAllowed) {
        unpin(entry);
      }

      if (entry.pinned) {
        if (keepAlive(scene, entry, frame)) {
          stats.pinned++;
          ++it;
          continue;
        }
        unpin(entry);
      }

      if (replacer.accessExternalMesh(entry.state.mesh) == nullptr) {
        const uintptr_t meshHandle = reinterpret_cast<uintptr_t>(entry.state.mesh);
        ONCE(Logger::warn(str::format("[Retained] mesh 0x", std::hex, meshHandle, std::dec,
                                      " of a retained instance was destroyed, the instance is dropped. Destroy retained instances before their mesh.")));
        it = s_entries.erase(it);
        stats.dropped++;
        continue;
      }

      resetExternalDrawPreserveReport();
      ctx->commitExternalGeometryToRT(std::make_unique<ExternalDrawState>(entry.state));

      if (externalDrawWasPreserved()) {
        stats.preserved++;
        if (++entry.preservedStreak >= pinAfter) {
          entry.pinned = true;
        }
      } else {
        stats.full++;
        entry.preservedStreak = 0;
      }
      ++it;
    }

    const uint32_t interval = RetainedOptions::logFrameInterval();
    if (interval != 0 && frame % interval == 0) {
      Logger::info(str::format("[Retained] frame ", frame, ": entries ", s_entries.size(), " visible ", stats.visible,
                               " pinned ", stats.pinned, " preserved ", stats.preserved, " full ", stats.full, " dropped ", stats.dropped));
    }
  }

} // namespace fork_hooks
} // namespace dxvk
