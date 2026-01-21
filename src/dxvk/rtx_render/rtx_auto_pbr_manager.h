/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <chrono>

#include "../dxvk_image.h"
#include "../../d3d9/d3d9_common_texture.h"

namespace dxvk {

class DxvkDevice;
class DxvkContext;

/**
 * \brief Auto PBR Manager
 * 
 * Manages automatic PBR texture extraction and association building.
 * Tracks textures by category (colormap, normal, specular, height),
 * dumps them to disk, and generates USDA material overrides.
 */
class RtxAutoPBRManager {
public:
  /**
   * \brief Texture association data
   * 
   * Associates a colormap with its PBR textures (normal, specular, height).
   * The colormap hash is the primary key.
   */
  struct TextureAssociation {
    XXH64_hash_t colormapHash = 0;
    XXH64_hash_t normalHash = 0;
    XXH64_hash_t specularHash = 0;
    XXH64_hash_t heightHash = 0;
    
    // Specular channel extraction: -1 = all channels, 0 = R, 1 = G, 2 = B
    int32_t specChannelIndex = -1;
    
    // Resolution tracking (for merge conflict resolution - prefer higher res)
    uint32_t normalWidth = 0, normalHeight = 0;
    uint32_t specularWidth = 0, specularHeight = 0;
    uint32_t heightWidth = 0, heightHeight = 0;
    
    // Debug info (optional, only when verbose mode is enabled)
    std::string shaderName;
    std::string colormapTextureName;
    std::string normalTextureName;
    std::string specularTextureName;
    std::string heightTextureName;
    
    // Manual override
    bool excludeFromUsda = false;
  };

  /**
   * \brief Current draw call texture accumulator
   * 
   * Tracks textures seen during a single draw call, then commits them
   * to associations when the draw call ends.
   */
  struct CurrentDrawCallData {
    XXH64_hash_t colormapHash = 0;
    XXH64_hash_t normalHash = 0;
    XXH64_hash_t specularHash = 0;
    XXH64_hash_t heightHash = 0;
    
    Rc<DxvkImage> colormapImage;
    Rc<DxvkImage> normalImage;
    Rc<DxvkImage> specularImage;
    Rc<DxvkImage> heightImage;
    
    uint32_t colormapSlot = 0;
    int32_t specChannelIndex = -1;  // -1 = all channels, 0 = R, 1 = G, 2 = B
    uint32_t normalWidth = 0, normalHeight = 0;
    uint32_t specularWidth = 0, specularHeight = 0;
    uint32_t heightWidth = 0, heightHeight = 0;
    
    std::string shaderName;
    std::string colormapTextureName;
    std::string normalTextureName;
    std::string specularTextureName;
    std::string heightTextureName;
    
    bool hasColormap() const { return colormapHash != 0; }
    bool hasNormal() const { return normalHash != 0; }
    bool hasSpecular() const { return specularHash != 0; }
    bool hasHeight() const { return heightHash != 0; }
    bool hasAnyPBRTexture() const { return hasNormal() || hasSpecular() || hasHeight(); }
    
    void clear() {
      colormapHash = normalHash = specularHash = heightHash = 0;
      colormapImage = normalImage = specularImage = heightImage = nullptr;
      colormapSlot = 0;
      specChannelIndex = -1;
      normalWidth = normalHeight = 0;
      specularWidth = specularHeight = heightWidth = heightHeight = 0;
      shaderName.clear();
      colormapTextureName.clear();
      normalTextureName.clear();
      specularTextureName.clear();
      heightTextureName.clear();
    }
  };

  static RtxAutoPBRManager& instance();
  
  // Enable/disable
  bool isEnabled() const { return m_enabled; }
  void setEnabled(bool enabled);
  
  // Ensure all output directories exist
  void ensureDirectoriesExist();
  
  // Verbose logging for USDA generation
  bool isVerboseLoggingEnabled() const { return m_verboseLogging; }
  void setVerboseLogging(bool enabled) { m_verboseLogging = enabled; }
  
  // Debug data collection (shader/texture names)
  bool isDebugDataEnabled() const { return m_debugDataEnabled; }
  void setDebugDataEnabled(bool enabled) { m_debugDataEnabled = enabled; }
  
  // Auto-save settings
  bool isAutoSaveEnabled() const { return m_autoSaveEnabled; }
  void setAutoSaveEnabled(bool enabled) { m_autoSaveEnabled = enabled; }
  
  float getAutoSaveIntervalSeconds() const { return m_autoSaveIntervalSeconds; }
  void setAutoSaveIntervalSeconds(float seconds) { m_autoSaveIntervalSeconds = std::max(1.0f, seconds); }
  
  /**
   * \brief Check if it's time to auto-save and do so if needed
   * 
   * Call this periodically (e.g., at end of frame or draw call).
   * Returns true if a save was performed.
   */
  bool checkAndAutoSave();

  /**
   * \brief Begin a new draw call
   * 
   * Called at the start of each draw call to reset the texture accumulator.
   */
  void beginDrawCall();

  /**
   * \brief Process a texture during draw call
   * 
   * Called for each texture that has Auto PBR metadata set.
   * Accumulates texture info until endDrawCall() is called.
   */
  void processTexture(
    XXH64_hash_t hash,
    D3D9CommonTexture::AutoPBRTextureCategory category,
    uint32_t textureSlot,
    int32_t specChannelIndex,
    const std::string& shaderName,
    const std::string& textureName,
    Rc<DxvkImage> image);

  /**
   * \brief End the current draw call
   * 
   * Commits accumulated textures to the association table and
   * triggers texture dumping if needed.
   */
  void endDrawCall(DxvkDevice* device);

  /**
   * \brief Dump a texture to disk
   * 
   * Uses the AssetExporter to save the texture as a DDS file.
   */
  void dumpTexture(Rc<DxvkContext> ctx, Rc<DxvkImage> image, 
                   XXH64_hash_t hash, const std::string& subfolder);

  /**
   * \brief Check if there are pending texture dumps
   */
  bool hasPendingDumps() const {
    std::lock_guard<std::mutex> lock(m_pendingDumpsMutex);
    return !m_pendingDumps.empty();
  }

  /**
   * \brief Process all pending texture dumps
   * 
   * Must be called from a context with DxvkContext access.
   */
  void processPendingDumps(Rc<DxvkContext> ctx);

  // Stats
  int getTrackedCount() const { return static_cast<int>(m_associations.size()); }
  int getDumpedColorCount() const { return m_statsDumpedColors; }
  int getDumpedNormalCount() const { return m_statsDumpedNormals; }
  int getDumpedSpecularCount() const { return m_statsDumpedSpeculars; }
  int getDumpedHeightCount() const { return m_statsDumpedHeights; }

  // File operations
  bool loadAssociationsFromFile();
  bool saveAssociationsToFile();
  void clearAllTrackedData();
  bool generateCompAutoconvertUSDA();

private:
  RtxAutoPBRManager() = default;
  ~RtxAutoPBRManager() = default;
  
  RtxAutoPBRManager(const RtxAutoPBRManager&) = delete;
  RtxAutoPBRManager& operator=(const RtxAutoPBRManager&) = delete;

  // Settings
  bool m_enabled = false;
  bool m_verboseLogging = false;
  bool m_debugDataEnabled = false;
  bool m_autoSaveEnabled = true;
  float m_autoSaveIntervalSeconds = 30.0f;
  
  // Current draw call state
  CurrentDrawCallData m_currentDrawCall;
  
  // Association data (colormap hash -> association)
  std::unordered_map<XXH64_hash_t, TextureAssociation> m_associations;
  
  // Tracking which textures have been dumped (to avoid duplicates)
  std::unordered_set<XXH64_hash_t> m_dumpedColormaps;
  std::unordered_set<XXH64_hash_t> m_dumpedNormals;
  std::unordered_set<XXH64_hash_t> m_dumpedSpeculars;
  std::unordered_set<XXH64_hash_t> m_dumpedHeights;
  
  // Statistics
  int m_statsDumpedColors = 0;
  int m_statsDumpedNormals = 0;
  int m_statsDumpedSpeculars = 0;
  int m_statsDumpedHeights = 0;
  
  // Pending textures to dump (deferred to avoid stalling draw calls)
  struct PendingDump {
    Rc<DxvkImage> image;
    XXH64_hash_t hash;
    std::string subfolder;
    int32_t specChannelIndex = -1;  // -1 = all channels, 0 = R, 1 = G, 2 = B (only for specular)
  };
  std::vector<PendingDump> m_pendingDumps;
  mutable std::mutex m_pendingDumpsMutex;
  
  // Last save time for periodic auto-save
  std::chrono::steady_clock::time_point m_lastSaveTime;
  
  // Helper functions
  std::string getImgDumpPath() const;
  std::string getColorPath() const;
  std::string getNormalPath() const;
  std::string getSpecularPath() const;
  std::string getHeightPath() const;
  std::string getAssociationsFilePath() const;
  
  std::string hashToHexString(XXH64_hash_t hash) const;
  
  void updateAssociation(const CurrentDrawCallData& data);
  void queueTextureDump(Rc<DxvkImage> image, XXH64_hash_t hash, const std::string& subfolder, int32_t specChannelIndex = -1);
};

} // namespace dxvk

