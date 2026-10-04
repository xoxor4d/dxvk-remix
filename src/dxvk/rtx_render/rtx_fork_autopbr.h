#pragma once

// rtx_fork_autopbr.h — fork-owned. AutoPBR collects which game normal /
// specular textures belong to which material (keyed by the material's
// replacement hash), dumps the textures to <game>/rtx-remix/imgdump/ and
// writes associations.json + comp_autoconvert.usda for the offline
// conversion scripts. Fed by rtx_fork_game_textures.cpp; plugin-facing
// surface documented in docs/RemixAutoPbrAPI.md.

#include "rtx_fork_game_textures.h"
#include "rtx_option.h"

#include <remix/remix_c.h>

namespace dxvk {
  class RtxContext;

  class AutoPbr {
  public:
    RTX_OPTION_ARGS("rtx.autopbr", int, exportsPerFrame, 8,
                    "Maximum number of AutoPBR texture dumps issued per frame while collecting.",
                    args.minValue = 1);
    RTX_OPTION_ARGS("rtx.autopbr", int, autosaveInterval, 32,
                    "AutoPBR writes associations.json after this many new or updated associations. 0 disables autosave.",
                    args.minValue = 0);

    static bool isCollecting();
    static void setCollecting(bool collecting);

    // Records textures for the material with replacement key materialHash and
    // queues dumps for images not on disk yet. Thread-safe; no-op while not
    // collecting. fallbackColor is used when the set has no COLOR entry.
    static void addAssociation(
      XXH64_hash_t materialHash,
      const game_textures::TextureSet& textures,
      const game_textures::Texture* fallbackColor = nullptr);

    // Live API materials, so starting a collection also picks up materials
    // created before it. Thread-safe.
    static void registerApiMaterial(
      remixapi_MaterialHandle handle,
      XXH64_hash_t materialHash,
      std::shared_ptr<const game_textures::TextureSet> textures);
    static void unregisterApiMaterial(remixapi_MaterialHandle handle);

    // Render thread. Adds the deferred NORMAL / SPECULAR textures of a live API
    // material and records it.
    static void completeApiMaterial(remixapi_MaterialHandle handle, const game_textures::TextureSet& textures);

    // Number of API material textures still waiting for data (UI counter).
    static void setTexturesWaitingForData(uint32_t count);

    // Drops every reference to device resources (queued dumps, live API
    // materials), saves unsaved associations and waits for pending file
    // writes. Associations and the dumped-file index are kept.
    static void reset();

    // Render thread, once per frame.
    static void endFrame(RtxContext& ctx);

    static void showImguiSettings();
  };

} // namespace dxvk
