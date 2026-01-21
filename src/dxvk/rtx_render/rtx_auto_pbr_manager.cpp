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

#include "rtx_auto_pbr_manager.h"
#include "rtx_asset_exporter.h"
//#include "rtx_context.h"
#include "../dxvk_device.h"
#include "../dxvk_context.h"

//#include "../../util/util_env.h"
#include "../../util/log/log.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace dxvk {

  RtxAutoPBRManager& RtxAutoPBRManager::instance() {
    static RtxAutoPBRManager s_instance;
    return s_instance;
  }
  
  std::string RtxAutoPBRManager::getImgDumpPath() const {
    return "./rtx-remix/imgdump/";
  }
  
  std::string RtxAutoPBRManager::getColorPath() const {
    return getImgDumpPath() + "color/";
  }
  
  std::string RtxAutoPBRManager::getNormalPath() const {
    return getImgDumpPath() + "normal/";
  }
  
  std::string RtxAutoPBRManager::getSpecularPath() const {
    return getImgDumpPath() + "specular/";
  }
  
  std::string RtxAutoPBRManager::getHeightPath() const {
    return getImgDumpPath() + "height/";
  }
  
  std::string RtxAutoPBRManager::getAssociationsFilePath() const {
    return getImgDumpPath() + "associations.json";
  }
  
  std::string RtxAutoPBRManager::hashToHexString(XXH64_hash_t hash) const {
    std::ostringstream ss;
    ss << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << hash;
    return ss.str();
  }
  
  void RtxAutoPBRManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
      return;
    }
  
    m_enabled = enabled;
  
    if (enabled) {
      Logger::info("[AutoPBR] Enabled - creating output directories");
      ensureDirectoriesExist();
      Logger::info(str::format("[AutoPBR] Output path: ", getImgDumpPath()));
      // Reset auto-save timer
      m_lastSaveTime = std::chrono::steady_clock::now();
    } else {
      Logger::info("[AutoPBR] Disabled");
      // Save associations before disabling
      if (!m_associations.empty()) {
        Logger::info("[AutoPBR] Saving associations before disabling...");
        saveAssociationsToFile();
      }
    }
  }
  
  bool RtxAutoPBRManager::checkAndAutoSave() {
    if (!m_enabled || !m_autoSaveEnabled || m_associations.empty()) {
      return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSaveTime).count();
    
    if (elapsed >= static_cast<long long>(m_autoSaveIntervalSeconds * 1000.0f)) {
      Logger::info(str::format("[AutoPBR] Auto-saving associations (", m_associations.size(), " entries)..."));
      bool success = saveAssociationsToFile();
      m_lastSaveTime = now;
      return success;
    }
    
    return false;
  }
  
  void RtxAutoPBRManager::ensureDirectoriesExist() {
    std::error_code ec;
    
    auto createDir = [&ec](const std::string& path) {
      if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path, ec);
        if (!ec) {
          Logger::info(str::format("[AutoPBR] Created directory: ", path));
        } else {
          Logger::err(str::format("[AutoPBR] Failed to create directory: ", path, " - ", ec.message()));
        }
      }
    };
    
    createDir(getImgDumpPath());
    createDir(getColorPath());
    createDir(getNormalPath());
    createDir(getSpecularPath());
    createDir(getHeightPath());
  }
  
  void RtxAutoPBRManager::beginDrawCall() {
    if (!m_enabled) return;
    m_currentDrawCall.clear();
  }
  
  void RtxAutoPBRManager::processTexture(XXH64_hash_t hash,
                                         D3D9CommonTexture::AutoPBRTextureCategory category,
                                         uint32_t textureSlot,
                                         int32_t specChannelIndex,
                                         const std::string& shaderName,
                                         const std::string& textureName,
                                         Rc<DxvkImage> image) {
    
    if (!m_enabled || hash == 0) return;
    
    // Get image dimensions
    uint32_t width = 0, height = 0;
    if (image != nullptr) {
      const auto& extent = image->info().extent;
      width = extent.width;
      height = extent.height;
    }
    
    switch (category) {
      case D3D9CommonTexture::AutoPBRTextureCategory::Colormap:
        m_currentDrawCall.colormapHash = hash;
        m_currentDrawCall.colormapImage = image;
        m_currentDrawCall.colormapSlot = textureSlot;
        if (m_debugDataEnabled) {
          m_currentDrawCall.shaderName = shaderName;
          m_currentDrawCall.colormapTextureName = textureName;
        }
        break;
        
      case D3D9CommonTexture::AutoPBRTextureCategory::NormalMap:
        m_currentDrawCall.normalHash = hash;
        m_currentDrawCall.normalImage = image;
        m_currentDrawCall.normalWidth = width;
        m_currentDrawCall.normalHeight = height;
        if (m_debugDataEnabled) {
          m_currentDrawCall.normalTextureName = textureName;
        }
        break;
        
      case D3D9CommonTexture::AutoPBRTextureCategory::Specular:
        m_currentDrawCall.specularHash = hash;
        m_currentDrawCall.specularImage = image;
        m_currentDrawCall.specularWidth = width;
        m_currentDrawCall.specularHeight = height;
        m_currentDrawCall.specChannelIndex = specChannelIndex;
        if (m_debugDataEnabled) {
          m_currentDrawCall.specularTextureName = textureName;
        }
        break;
        
      case D3D9CommonTexture::AutoPBRTextureCategory::Height:
        m_currentDrawCall.heightHash = hash;
        m_currentDrawCall.heightImage = image;
        m_currentDrawCall.heightWidth = width;
        m_currentDrawCall.heightHeight = height;
        if (m_debugDataEnabled) {
          m_currentDrawCall.heightTextureName = textureName;
        }
        break;
        
      default:
        break;
    }
  }
  
  void RtxAutoPBRManager::endDrawCall(DxvkDevice* device) {
    if (!m_enabled) {
      return;
    }
    
    // Only process if we have a colormap
    if (!m_currentDrawCall.hasColormap()) {
      return;
    }
    
    // Update associations
    updateAssociation(m_currentDrawCall);
    
    // Queue texture dumps (only if not already dumped)
    if (m_currentDrawCall.colormapImage != nullptr && m_dumpedColormaps.find(m_currentDrawCall.colormapHash) == m_dumpedColormaps.end()) {
      queueTextureDump(m_currentDrawCall.colormapImage, m_currentDrawCall.colormapHash, "color");
      m_dumpedColormaps.insert(m_currentDrawCall.colormapHash);
      m_statsDumpedColors++;
    }
    
    if (m_currentDrawCall.normalImage != nullptr && m_dumpedNormals.find(m_currentDrawCall.normalHash) == m_dumpedNormals.end()) {
      queueTextureDump(m_currentDrawCall.normalImage, m_currentDrawCall.normalHash, "normal");
      m_dumpedNormals.insert(m_currentDrawCall.normalHash);
      m_statsDumpedNormals++;
    }
    
    if (m_currentDrawCall.specularImage != nullptr) {
      // Create unique key for specular: combine hash with channel index
      // This allows dumping the same texture multiple times for different channels
      XXH64_hash_t specKey = m_currentDrawCall.specularHash;
      if (m_currentDrawCall.specChannelIndex >= 0) {
        specKey = XXH64(&m_currentDrawCall.specChannelIndex, sizeof(int32_t), specKey);
      }
      
      if (m_dumpedSpeculars.find(specKey) == m_dumpedSpeculars.end()) {
        queueTextureDump(m_currentDrawCall.specularImage, m_currentDrawCall.specularHash, "specular", m_currentDrawCall.specChannelIndex);
        m_dumpedSpeculars.insert(specKey);
        m_statsDumpedSpeculars++;
      }
    }
    
    if (m_currentDrawCall.heightImage != nullptr && 
        m_dumpedHeights.find(m_currentDrawCall.heightHash) == m_dumpedHeights.end()) {
      queueTextureDump(m_currentDrawCall.heightImage, m_currentDrawCall.heightHash, "height");
      m_dumpedHeights.insert(m_currentDrawCall.heightHash);
      m_statsDumpedHeights++;
    }
    
    // Check if we should auto-save
    checkAndAutoSave();
  }
  
  void RtxAutoPBRManager::updateAssociation(const CurrentDrawCallData& data) {
    auto& assoc = m_associations[data.colormapHash];
    
    // Always update colormap hash
    assoc.colormapHash = data.colormapHash;
    
    // Update normal if present and higher resolution (or not yet set)
    if (data.hasNormal()) {
      uint64_t newRes = static_cast<uint64_t>(data.normalWidth) * data.normalHeight;
      uint64_t oldRes = static_cast<uint64_t>(assoc.normalWidth) * assoc.normalHeight;
      if (assoc.normalHash == 0 || newRes > oldRes) {
        assoc.normalHash = data.normalHash;
        assoc.normalWidth = data.normalWidth;
        assoc.normalHeight = data.normalHeight;
        if (m_debugDataEnabled) {
          assoc.normalTextureName = data.normalTextureName;
        }
      }
    }
    
    // Update specular if present and higher resolution
    if (data.hasSpecular()) {
      uint64_t newRes = static_cast<uint64_t>(data.specularWidth) * data.specularHeight;
      uint64_t oldRes = static_cast<uint64_t>(assoc.specularWidth) * assoc.specularHeight;
      if (assoc.specularHash == 0 || newRes > oldRes) {
        assoc.specularHash = data.specularHash;
        assoc.specularWidth = data.specularWidth;
        assoc.specularHeight = data.specularHeight;
        assoc.specChannelIndex = data.specChannelIndex;
        if (m_debugDataEnabled) {
          assoc.specularTextureName = data.specularTextureName;
        }
      }
    }
    
    // Update height if present and higher resolution
    if (data.hasHeight()) {
      uint64_t newRes = static_cast<uint64_t>(data.heightWidth) * data.heightHeight;
      uint64_t oldRes = static_cast<uint64_t>(assoc.heightWidth) * assoc.heightHeight;
      if (assoc.heightHash == 0 || newRes > oldRes) {
        assoc.heightHash = data.heightHash;
        assoc.heightWidth = data.heightWidth;
        assoc.heightHeight = data.heightHeight;
        if (m_debugDataEnabled) {
          assoc.heightTextureName = data.heightTextureName;
        }
      }
    }
    
    // Update debug info
    if (m_debugDataEnabled) {
      if (!data.shaderName.empty()) {
        assoc.shaderName = data.shaderName;
      }
      if (!data.colormapTextureName.empty()) {
        assoc.colormapTextureName = data.colormapTextureName;
      }
    }
  }
  
  void RtxAutoPBRManager::queueTextureDump(Rc<DxvkImage> image, 
                                           XXH64_hash_t hash, 
                                           const std::string& subfolder, 
                                           int32_t specChannelIndex) {
  
    std::lock_guard<std::mutex> lock(m_pendingDumpsMutex);
    m_pendingDumps.push_back({image, hash, subfolder, specChannelIndex});
  }
  
  void RtxAutoPBRManager::processPendingDumps(Rc<DxvkContext> ctx) {
    std::vector<PendingDump> dumps;
    {
      std::lock_guard<std::mutex> lock(m_pendingDumpsMutex);
      dumps.swap(m_pendingDumps);
    }
    
    if (dumps.empty()) {
      return;
    }
    
    auto& exporter = ctx->getCommonObjects()->metaExporter();
    for (const auto& dump : dumps) {
      std::string dir = getImgDumpPath() + dump.subfolder + "/";
      
      // For specular textures with channel extraction, include channel info in filename
      // The actual channel extraction happens during roughness conversion
      std::string filename;
      if (dump.subfolder == "specular" && dump.specChannelIndex >= 0) {
        static const char* channelSuffix[] = { "_chR", "_chG", "_chB" };
        filename = hashToHexString(dump.hash) + channelSuffix[dump.specChannelIndex] + ".dds";
      } else {
        filename = hashToHexString(dump.hash) + ".dds";
      }
      
      std::string fullPath = dir + filename;
      
      // Create directory if needed
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      if (ec) {
        Logger::err(str::format("[AutoPBR] Failed to create directory: ", dir, " - ", ec.message()));
        continue;
      }
      
      exporter.dumpImageToFile(ctx, dir, filename, dump.image);
    }
  }
  
  void RtxAutoPBRManager::dumpTexture(Rc<DxvkContext> ctx, 
                                      Rc<DxvkImage> image, 
                                      XXH64_hash_t hash, 
                                      const std::string& subfolder) {
  
    std::string dir = getImgDumpPath() + subfolder + "/";
    std::string filename = hashToHexString(hash) + ".dds";
    std::string fullPath = dir + filename;
    
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      Logger::err(str::format("[AutoPBR] Failed to create directory: ", dir, " - ", ec.message()));
      return;
    }
    
    auto& exporter = ctx->getCommonObjects()->metaExporter();
    exporter.dumpImageToFile(ctx, dir, filename, image);
  }
  
  bool RtxAutoPBRManager::loadAssociationsFromFile() {
    const std::string filepath = getAssociationsFilePath();
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
      Logger::info(str::format("[AutoPBR] No associations file found at: ", filepath));
      return false;
    }
    
    try {
      // Simple JSON parsing (not using a library to avoid dependencies)
      std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
      file.close();
      
      // Parse associations array
      size_t pos = content.find("\"associations\"");
      if (pos == std::string::npos) {
        Logger::warn("[AutoPBR] Invalid associations file format");
        return false;
      }
      
      // Find the array start
      pos = content.find('[', pos);
      if (pos == std::string::npos) {
        return false;
      }
      
      size_t arrayEnd = content.rfind(']');
      if (arrayEnd == std::string::npos || arrayEnd <= pos) {
        return false;
      }
      
      // Parse each object
      size_t objStart = pos;
      int loadedCount = 0;
      
      while ((objStart = content.find('{', objStart)) != std::string::npos && objStart < arrayEnd) {
        size_t objEnd = content.find('}', objStart);
        if (objEnd == std::string::npos || objEnd > arrayEnd) {
          break;
        }
        
        std::string objStr = content.substr(objStart, objEnd - objStart + 1);
        
        TextureAssociation assoc;
        
        // Parse hash fields (simple string extraction)
        auto parseHash = [&objStr](const std::string& key) -> XXH64_hash_t {
          size_t keyPos = objStr.find("\"" + key + "\"");
          if (keyPos == std::string::npos) {
            return 0;
          }
          size_t valStart = objStr.find(':', keyPos);
          if (valStart == std::string::npos) {
            return 0;
          }
          size_t quoteStart = objStr.find('"', valStart);
          if (quoteStart == std::string::npos) {
            return 0;
          }
          size_t quoteEnd = objStr.find('"', quoteStart + 1);
          if (quoteEnd == std::string::npos) {
            return 0;
          }
          std::string hashStr = objStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
          return std::stoull(hashStr, nullptr, 16);
        };
        
        auto parseString = [&objStr](const std::string& key) -> std::string {
          size_t keyPos = objStr.find("\"" + key + "\"");
          if (keyPos == std::string::npos) {
            return "";
          }
          size_t valStart = objStr.find(':', keyPos);
          if (valStart == std::string::npos) {
            return "";
          }
          size_t quoteStart = objStr.find('"', valStart);
          if (quoteStart == std::string::npos) {
            return "";
          }
          size_t quoteEnd = objStr.find('"', quoteStart + 1);
          if (quoteEnd == std::string::npos) {
            return "";
          }
          return objStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        };
        
        auto parseUint = [&objStr](const std::string& key) -> uint32_t {
          size_t keyPos = objStr.find("\"" + key + "\"");
          if (keyPos == std::string::npos) {
            return 0;
          }
          size_t valStart = objStr.find(':', keyPos);
          if (valStart == std::string::npos) {
            return 0;
          }
  
          // Skip whitespace
          while (valStart < objStr.size() && (objStr[valStart] == ':' || objStr[valStart] == ' ')) {
            valStart++;
          }
  
          size_t valEnd = objStr.find_first_of(",}", valStart);
          if (valEnd == std::string::npos) {
            return 0;
          }
          std::string valStr = objStr.substr(valStart, valEnd - valStart);
          return std::stoul(valStr);
        };
        
        auto parseInt = [&objStr](const std::string& key, int32_t defaultVal = -1) -> int32_t {
          size_t keyPos = objStr.find("\"" + key + "\"");
          if (keyPos == std::string::npos) {
            return defaultVal;
          }
          size_t valStart = objStr.find(':', keyPos);
          if (valStart == std::string::npos) {
            return defaultVal;
          }
  
          // Skip whitespace
          while (valStart < objStr.size() && (objStr[valStart] == ':' || objStr[valStart] == ' ')) {
            valStart++;
          }
  
          size_t valEnd = objStr.find_first_of(",}", valStart);
          if (valEnd == std::string::npos) {
            return defaultVal;
          }
          std::string valStr = objStr.substr(valStart, valEnd - valStart);
          return std::stol(valStr);
        };
        
        assoc.colormapHash = parseHash("colormap_hash");
        assoc.normalHash = parseHash("normal_hash");
        assoc.specularHash = parseHash("specular_hash");
        assoc.heightHash = parseHash("height_hash");
        
        assoc.specChannelIndex = parseInt("spec_channel_index", -1);
        
        assoc.normalWidth = parseUint("normal_width");
        assoc.normalHeight = parseUint("normal_height");
        assoc.specularWidth = parseUint("specular_width");
        assoc.specularHeight = parseUint("specular_height");
        assoc.heightWidth = parseUint("height_width");
        assoc.heightHeight = parseUint("height_height");
        
        assoc.shaderName = parseString("shader_name");
        assoc.colormapTextureName = parseString("colormap_name");
        assoc.normalTextureName = parseString("normal_name");
        assoc.specularTextureName = parseString("specular_name");
        assoc.heightTextureName = parseString("height_name");
        
        if (assoc.colormapHash != 0) {
          m_associations[assoc.colormapHash] = assoc;
          
          // Also track as dumped if we have the hashes
          if (assoc.colormapHash != 0) m_dumpedColormaps.insert(assoc.colormapHash);
          if (assoc.normalHash != 0) m_dumpedNormals.insert(assoc.normalHash);
          if (assoc.specularHash != 0) m_dumpedSpeculars.insert(assoc.specularHash);
          if (assoc.heightHash != 0) m_dumpedHeights.insert(assoc.heightHash);
          
          loadedCount++;
        }
        
        objStart = objEnd + 1;
      }
      
      Logger::info(str::format("[AutoPBR] Loaded ", loadedCount, " associations from file"));
      return true;
    }
    catch (const std::exception& e) {
      Logger::err(str::format("[AutoPBR] Failed to parse associations file: ", e.what()));
      return false;
    }
  }
  
  bool RtxAutoPBRManager::saveAssociationsToFile() {
    const std::string filepath = getAssociationsFilePath();
    
    // Create directory
    std::error_code ec;
    std::filesystem::create_directories(getImgDumpPath(), ec);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
      Logger::err(str::format("[AutoPBR] Failed to create associations file: ", filepath));
      return false;
    }
    
    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"associations\": [\n";
    
    bool first = true;
    for (const auto& [hash, assoc] : m_associations) {
      if (!first) file << ",\n";
      first = false;
      
      file << "    {\n";
      file << "      \"colormap_hash\": \"" << hashToHexString(assoc.colormapHash) << "\",\n";
      file << "      \"normal_hash\": \"" << hashToHexString(assoc.normalHash) << "\",\n";
      file << "      \"specular_hash\": \"" << hashToHexString(assoc.specularHash) << "\",\n";
      file << "      \"height_hash\": \"" << hashToHexString(assoc.heightHash) << "\",\n";
      file << "      \"spec_channel_index\": " << assoc.specChannelIndex << ",\n";
      file << "      \"normal_width\": " << assoc.normalWidth << ",\n";
      file << "      \"normal_height\": " << assoc.normalHeight << ",\n";
      file << "      \"specular_width\": " << assoc.specularWidth << ",\n";
      file << "      \"specular_height\": " << assoc.specularHeight << ",\n";
      file << "      \"height_width\": " << assoc.heightWidth << ",\n";
      file << "      \"height_height\": " << assoc.heightHeight << ",\n";
      file << "      \"shader_name\": \"" << assoc.shaderName << "\",\n";
      file << "      \"colormap_name\": \"" << assoc.colormapTextureName << "\",\n";
      file << "      \"normal_name\": \"" << assoc.normalTextureName << "\",\n";
      file << "      \"specular_name\": \"" << assoc.specularTextureName << "\",\n";
      file << "      \"height_name\": \"" << assoc.heightTextureName << "\"\n";
      file << "    }";
    }
    
    file << "\n  ]\n";
    file << "}\n";
    
    file.close();
    Logger::info(str::format("[AutoPBR] Saved ", m_associations.size(), " associations to file"));
    return true;
  }
  
  void RtxAutoPBRManager::clearAllTrackedData() {
    m_associations.clear();
    m_dumpedColormaps.clear();
    m_dumpedNormals.clear();
    m_dumpedSpeculars.clear();
    m_dumpedHeights.clear();
    m_statsDumpedColors = 0;
    m_statsDumpedNormals = 0;
    m_statsDumpedSpeculars = 0;
    m_statsDumpedHeights = 0;
    m_currentDrawCall.clear();
    
    Logger::info("[AutoPBR] Cleared all tracked data");
  }
  
  bool RtxAutoPBRManager::generateCompAutoconvertUSDA() {
    if (m_associations.empty()) {
      Logger::warn("[AutoPBR] No associations to generate USDA from");
      return false;
    }
    
    const std::string usdaPath = getImgDumpPath() + "comp_autoconvert.usda";
    std::ofstream usdaFile(usdaPath);
    
    if (!usdaFile.is_open()) {
      Logger::err(str::format("[AutoPBR] Failed to create USDA file: ", usdaPath));
      return false;
    }
    
    // Optional verbose log file
    std::ofstream logFile;
    if (m_verboseLogging) {
      const std::string logPath = getImgDumpPath() + "comp_autoconvert_log.txt";
      logFile.open(logPath);
    }
    
    // Write USDA header
    usdaFile << "#usda 1.0\n";
    usdaFile << "(\n";
    usdaFile << "    upAxis = \"Z\"\n";
    usdaFile << ")\n\n";
    
    usdaFile << "over \"RootNode\"\n";
    usdaFile << "{\n";
    usdaFile << "    over \"Looks\"\n";
    usdaFile << "    {\n";
    
    int materialCount = 0;
    int skippedCount = 0;
    
    if (m_verboseLogging && logFile.is_open()) {
      logFile << "================================================================\n";
      logFile << "  Auto PBR USDA Generation Log\n";
      logFile << "================================================================\n";
      logFile << "  Total associations: " << m_associations.size() << "\n";
      logFile << "================================================================\n\n";
    }
    
    for (const auto& [hash, assoc] : m_associations) {
      // Skip if no PBR textures
      bool hasAnyPBR = assoc.normalHash != 0 || assoc.specularHash != 0 || assoc.heightHash != 0;
      if (!hasAnyPBR || assoc.excludeFromUsda) {
        skippedCount++;
        continue;
      }
      
      std::string hashStr = hashToHexString(assoc.colormapHash);
      std::string matName = "mat_" + hashStr;
      
      // Write material override using 'over' (not 'def')
      usdaFile << "        over \"" << matName << "\"\n";
      usdaFile << "        {\n";
      usdaFile << "            over \"Shader\"\n";
      usdaFile << "            {\n";
      
      if (assoc.normalHash != 0) {
        usdaFile << "                asset inputs:normalmap_texture = @./assets/autoconv/" 
                 << hashToHexString(assoc.normalHash) << "_normal_oth.dds@\n";
      }
      
      if (assoc.specularHash != 0) {
        // Build roughness filename: HASH_rough.dds or HASH_chR_rough.dds (channel before _rough)
        std::string filename = hashToHexString(assoc.specularHash);
        if (assoc.specChannelIndex >= 0) {
          static const char* channelSuffix[] = { "_chR", "_chG", "_chB" };
          filename += channelSuffix[assoc.specChannelIndex];
        }
        filename += "_rough.dds";
        
        usdaFile << "                asset inputs:reflectionroughness_texture = @./assets/autoconv/" 
                 << filename << "@\n";
      }
      
      if (assoc.heightHash != 0) {
        usdaFile << "                asset inputs:height_texture = @./assets/autoconv/" 
                 << hashToHexString(assoc.heightHash) << "_height.dds@\n";
        usdaFile << "                float inputs:displace_in = 0.1\n";
      }
      
      usdaFile << "            }\n";
      usdaFile << "        }\n\n";
      
      // Write to log file
      if (m_verboseLogging && logFile.is_open()) {
        logFile << "--------------------------------------------------------------------------------\n";
        logFile << "  Material: " << matName << "\n";
        logFile << "--------------------------------------------------------------------------------\n";
        logFile << "  Colormap Hash: 0x" << hashStr << "\n";
        if (!assoc.shaderName.empty()) {
          logFile << "  Shader: " << assoc.shaderName << "\n";
        }
        logFile << "  Textures:\n";
        if (!assoc.colormapTextureName.empty()) {
          logFile << "    Colormap: " << assoc.colormapTextureName << "\n";
        }
        if (assoc.normalHash != 0) {
          logFile << "    Normal: " << (assoc.normalTextureName.empty() ? "N/A" : assoc.normalTextureName)
                  << " (0x" << hashToHexString(assoc.normalHash) << ", " 
                  << assoc.normalWidth << "x" << assoc.normalHeight << ")\n";
        }
        if (assoc.specularHash != 0) {
          logFile << "    Specular: " << (assoc.specularTextureName.empty() ? "N/A" : assoc.specularTextureName)
                  << " (0x" << hashToHexString(assoc.specularHash) << ", "
                  << assoc.specularWidth << "x" << assoc.specularHeight
                  << ", specChannelIndex=" << assoc.specChannelIndex;
          if (assoc.specChannelIndex >= 0) {
            static const char* channelNames[] = { "R", "G", "B" };
            logFile << " [" << channelNames[assoc.specChannelIndex] << "]";
          } else {
            logFile << " [ALL]";
          }
          logFile << ") -> roughness\n";
        }
        if (assoc.heightHash != 0) {
          logFile << "    Height: " << (assoc.heightTextureName.empty() ? "N/A" : assoc.heightTextureName)
                  << " (0x" << hashToHexString(assoc.heightHash) << ", "
                  << assoc.heightWidth << "x" << assoc.heightHeight << ")\n";
        }
        logFile << "\n";
      }
      
      materialCount++;
    }
    
    usdaFile << "    }\n";
    usdaFile << "}\n";
    
    usdaFile.close();
    
    if (m_verboseLogging && logFile.is_open()) {
      logFile << "================================================================\n";
      logFile << "  Summary\n";
      logFile << "================================================================\n";
      logFile << "  Materials generated: " << materialCount << "\n";
      logFile << "  Materials skipped: " << skippedCount << "\n";
      logFile.close();
    }
    
    Logger::info(str::format("[AutoPBR] Generated USDA with ", materialCount, " materials (", skippedCount, " skipped)"));
    return true;
  }

} // namespace dxvk
