/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "log/log.h"
#include "util_bridgecommand.h"
#include "util_devicecommand.h"
#include "util_remixapi.h"
#include "d3d9_texture.h"

using namespace remixapi::util;

namespace remixapi {

bool g_bInterfaceInitialized = false;
PFN_remixapi_BridgeCallback g_beginSceneCallback = nullptr;
PFN_remixapi_BridgeCallback g_endSceneCallback = nullptr;
PFN_remixapi_BridgeCallback g_presentCallback = nullptr;

template<typename T>
inline void send(ClientMessage& msg, const T& val) {
  msg.send_data(sizeof(T), &val);
}

template<>
inline void send(ClientMessage& msg, const remixapi_Float3D& vec3) {
  send(msg, vec3.x);
  send(msg, vec3.y);
  send(msg, vec3.z);
}

template<>
inline void send(ClientMessage& msg, const remixapi_Path& path) {
  const auto nonNullPath = (path) ? (path) : L"";
  msg.send_data(wcslen(nonNullPath) * sizeof(wchar_t), nonNullPath);
}

template<>
inline void send(ClientMessage& msg, const char* const& c_str) {
  msg.send_data((uint32_t) strlen(c_str) + 1, c_str);
}

template<typename RemixApiHandleT>
inline void sendHandle(ClientMessage& msg, const Handle<RemixApiHandleT>& handle) {
  msg.send_data(handle.uid);
}

template<>
inline void send(ClientMessage& msg, const Bool& b) {
  uint32_t boolVal = 0x0;
  boolVal |= (uint8_t)b;
  msg.send_data(boolVal);
}

template<typename SerializableT>
auto serializeAndSend(ClientMessage& msg, const SerializableT& serializable) {
  static_assert(is_serializable_v<SerializableT>, "serializeAndSend(...)  may only be called with defined Serializable<T> types");
  msg.send_data(ToRemixApiStructEnum<SerializableT::BaseT>);
  const auto serializableSize = serializable.size();
  auto pSlzd = new uint8_t[serializableSize];
  serializable.serialize(pSlzd);
  msg.send_data(serializableSize, pSlzd);
  delete pSlzd;
}


remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMaterial(
  const remixapi_MaterialInfo* info,
  remixapi_MaterialHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateMaterial);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_MATERIAL_INFO);

  MaterialHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateMaterial);

    serializeAndSend<serialize::MaterialInfo>(c, *info);

    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(pNext)) {
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT:
        {
          auto* pOpaqueMat = static_cast<const remixapi_MaterialInfoOpaqueEXT* const>(infoItr);
          send(c, Bool::True); 
          serializeAndSend<serialize::MaterialInfoOpaque>(c, *pOpaqueMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT:
        {
          auto* pOpaqueSubsurfaceMat = static_cast<const remixapi_MaterialInfoOpaqueSubsurfaceEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoOpaqueSubsurface>(c, *pOpaqueSubsurfaceMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT:
        {
          auto* pTranslucentMat = static_cast<const remixapi_MaterialInfoTranslucentEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoTranslucent>(c, *pTranslucentMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT:
        {
          auto* pPortalMat = static_cast<const remixapi_MaterialInfoPortalEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoPortal>(c, *pPortalMat);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_CreateMaterial] Unknown sType. Skipping.");
          break;
        }
      }
      infoItr = pNext;
    }
    send(c, Bool::False);
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMaterial(remixapi_MaterialHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyMaterial);
  MaterialHandle materialHandle(handle);
  if(!materialHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyMaterial);
    sendHandle(c, materialHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMesh(
  const remixapi_MeshInfo* info,
  remixapi_MeshHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateMesh);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_MESH_INFO);

  MeshHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateMesh);
    
    serializeAndSend<serialize::MeshInfo>(c, *info);

    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      switch (getSType(pNext)) {
        default:
        {
          Logger::warn("[remixapi_CreateMesh] Unknown sType. Skipping.");
          break;
        }
      }
    }
    sendHandle(c, newHandle);
  }
  
  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMesh(remixapi_MeshHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyMesh);
  MeshHandle meshHandle(handle);
  if(!meshHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyMesh);
    sendHandle(c, meshHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawInstance(const remixapi_InstanceInfo* info) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DrawInstance);
  {
    ClientMessage c(Commands::RemixApi_DrawInstance);

    serializeAndSend<serialize::InstanceInfo>(c, *info);

    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(pNext)) {
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT:
        {
          auto* pObjectPicking = static_cast<const remixapi_InstanceInfoObjectPickingEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoObjectPicking>(c, *pObjectPicking);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT:
        {
          auto* pBlend = static_cast<const remixapi_InstanceInfoBlendEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoBlend>(c, *pBlend);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT:
        {
          auto* pXforms = static_cast<const remixapi_InstanceInfoBoneTransformsEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoTransforms>(c, *pXforms);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT:
        {
          auto* pParticle = static_cast<const remixapi_InstanceInfoParticleSystemEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoParticleSystem>(c, *pParticle);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_DrawInstance] Unknown sType. Skipping.");
          break;
        }
      }
    }
    send(c, Bool::False);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateLight(
  const remixapi_LightInfo* info,
  remixapi_LightHandle*     out_handle) {
    
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateLight);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_LIGHT_INFO);

  LightHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateLight);

    serializeAndSend<serialize::LightInfo>(c, *info);
    
    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(infoItr)) {
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT:
        {
          auto* pSphere = static_cast<const remixapi_LightInfoSphereEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoSphere>(c, *pSphere);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT:
        {
          auto* pRect = static_cast<const remixapi_LightInfoRectEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoRect>(c, *pRect);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT:
        {
          auto* pDisk = static_cast<const remixapi_LightInfoDiskEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDisk>(c, *pDisk);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT:
        {
          auto* pCylinder = static_cast<const remixapi_LightInfoCylinderEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoCylinder>(c, *pCylinder);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT:
        {
          auto* pDistant = static_cast<const remixapi_LightInfoDistantEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDistant>(c, *pDistant);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT:
        {
          auto* pDome = static_cast<const remixapi_LightInfoDomeEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDome>(c, *pDome);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT:
        {
          auto* pUSD = static_cast<const remixapi_LightInfoUSDEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoUSD>(c, *pUSD);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_CreateLight] Unknown sType. Skipping.");
          break;
        }
      }
    }
    send(c, Bool::False);
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyLight(remixapi_LightHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyLight);
  LightHandle lightHandle(handle);
  if(!lightHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyLight);
    sendHandle(c, lightHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawLightInstance(remixapi_LightHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DrawLightInstance);
  LightHandle lightHandle(handle);
  if(!lightHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DrawLightInstance);
    sendHandle(c, lightHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_SetConfigVariable(const char* var, const char* value) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_SetConfigVariable);
  if (!var || !value) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_SetConfigVariable);
    send(c, var);
    send(c, value);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_GetTextureHash(IDirect3DTexture9* texture, uint64_t* out_hash) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_GetTextureHash);
  if (!texture || !out_hash) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }

  // Cast to Direct3DTexture9_LSS to get the server-side handle
  // The texture parameter should be a bridge wrapper object
  auto* bridgeTexture = static_cast<Direct3DTexture9_LSS*>(texture);
  const uint32_t serverHandle = static_cast<uint32_t>(bridgeTexture->getId());

  Logger::info(format_string("[remixapi_dxvk_GetTextureHash] Client texture ptr: 0x%p, Server handle: 0x%X", 
                             texture, serverHandle));

  uint32_t currentUID = 0;
  {
    ClientMessage c(Commands::RemixApi_GetTextureHash);
    c.send_data(serverHandle); // Send the server-side handle
    currentUID = c.get_uid();
  }

  WAIT_FOR_SERVER_RESPONSE("remixapi_dxvk_GetTextureHash", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);
  
  // Get the status code from the server
  remixapi_ErrorCode status = (remixapi_ErrorCode)DeviceBridge::get_data();
  
  if (status == REMIXAPI_ERROR_CODE_SUCCESS) {
    // Get the 64-bit hash from the server (sent as two 32-bit values)
    uint32_t hashLow = (uint32_t)DeviceBridge::get_data();
    uint32_t hashHigh = (uint32_t)DeviceBridge::get_data();
    *out_hash = ((uint64_t)hashHigh << 32) | hashLow;
    
    Logger::info(format_string("[remixapi_dxvk_GetTextureHash] Received hash parts - Low: 0x%X, High: 0x%X, Combined: 0x%llX", 
                               hashLow, hashHigh, *out_hash));
  } else {
    Logger::err(format_string("[remixapi_dxvk_GetTextureHash] Failed with status: %d", status));
  }
  
  DeviceBridge::pop_front();
  
  return status;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_SetTextureCategory(
    IDirect3DTexture9* texture,
    remixapi_dxvk_TextureCategory category,
    uint32_t textureSlot,
    int32_t specChannelIndex,
    const char* shaderName,
    const char* textureName) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_SetTextureCategory);
  if (!texture) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }

  // Cast to Direct3DTexture9_LSS to get the server-side handle
  auto* bridgeTexture = static_cast<Direct3DTexture9_LSS*>(texture);
  const uint32_t serverHandle = static_cast<uint32_t>(bridgeTexture->getId());

  // Prepare string lengths (0 if null)
  const uint32_t shaderNameLen = shaderName ? static_cast<uint32_t>(strlen(shaderName)) : 0;
  const uint32_t textureNameLen = textureName ? static_cast<uint32_t>(strlen(textureName)) : 0;

  uint32_t currentUID = 0;
  {
    ClientMessage c(Commands::RemixApi_SetTextureCategory);
    c.send_data(serverHandle);
    c.send_data(static_cast<uint32_t>(category));
    c.send_data(textureSlot);
    c.send_data(specChannelIndex);
    c.send_data(shaderNameLen);
    if (shaderNameLen > 0) {
      c.send_data(shaderNameLen, shaderName);
    }
    c.send_data(textureNameLen);
    if (textureNameLen > 0) {
      c.send_data(textureNameLen, textureName);
    }
    currentUID = c.get_uid();
  }

  WAIT_FOR_SERVER_RESPONSE("remixapi_dxvk_SetTextureCategory", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);
  
  remixapi_ErrorCode status = (remixapi_ErrorCode)DeviceBridge::get_data();
  DeviceBridge::pop_front();
  
  return status;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_CreateD3D9(
  remixapi_Bool       editorModeEnabled,
  IDirect3D9Ex**      out_pD3D9) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_CreateD3D9);
  Logger::err("[remixapi_dxvk_CreateD3D9] Not yet supported. Device used by Remix API defaults to "
                                         "most recently created by client application.");
  *out_pD3D9 = nullptr;
  return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_RegisterD3D9Device(IDirect3DDevice9Ex* d3d9Device) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_RegisterD3D9Device);
  if (!d3d9Device) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  Logger::err("[remixapi_dxvk_RegisterD3D9Device] Not yet supported. Device used by Remix API defaults to "
                                                 "most recently created by client application.");
  return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
}

// https://stackoverflow.com/a/27490954
constexpr bool strings_equal(char const * a, char const * b) {
  return *a == *b && (*a == '\0' || strings_equal(a + 1, b + 1));
}

extern "C" {

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_InitializeLibrary(
    const remixapi_InitializeLibraryInfo* info,
    remixapi_Interface*                   out_result) {

    static_assert(strings_equal(__func__, remixapi::exported_func_name::initRemixApi));
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_InitializeLibrary);
    
    if (!GlobalOptions::getExposeRemixApi()) {
      Logger::err("Remix API is not enabled. This is currently an experimental feature and must be explicitly enabled \
                   in the `bridge.conf`. Please set `exposeRemixApi = True` if you are sure you want it enabled.");
      return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
    }
    if (!info || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (!out_result) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    auto interf = remixapi_Interface {};
    {
      // interf.Startup = remixapi_Startup;
      // interf.Shutdown = remixapi_Shutdown;
      // interf.Present = remixapi_Present;
      interf.CreateMaterial = remixapi_CreateMaterial;
      interf.DestroyMaterial = remixapi_DestroyMaterial;
      interf.CreateMesh = remixapi_CreateMesh;
      interf.DestroyMesh = remixapi_DestroyMesh;
      // interf.SetupCamera = remixapi_SetupCamera;
      interf.DrawInstance = remixapi_DrawInstance;
      interf.CreateLight = remixapi_CreateLight;
      interf.DestroyLight = remixapi_DestroyLight;
      interf.DrawLightInstance = remixapi_DrawLightInstance;
      interf.SetConfigVariable = remixapi_SetConfigVariable;
      interf.dxvk_CreateD3D9 = remixapi_dxvk_CreateD3D9;
      interf.dxvk_RegisterD3D9Device = remixapi_dxvk_RegisterD3D9Device;
      // interf.dxvk_GetExternalSwapchain = remixapi_dxvk_GetExternalSwapchain;
      // interf.dxvk_GetVkImage = remixapi_dxvk_GetVkImage;
      interf.dxvk_GetTextureHash = remixapi_dxvk_GetTextureHash;
      interf.dxvk_SetTextureCategory = remixapi_dxvk_SetTextureCategory;
      // interf.dxvk_CopyRenderingOutput = remixapi_dxvk_CopyRenderingOutput;
      // interf.dxvk_SetDefaultOutput = remixapi_dxvk_SetDefaultOutput;
      // interf.pick_RequestObjectPicking = remixapi_pick_RequestObjectPicking;
      // interf.pick_HighlightObjects = remixapi_pick_HighlightObjects;
    }

    *out_result = interf;
    remixapi::g_bInterfaceInitialized = true;

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }
  
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_RegisterCallbacks(
    PFN_remixapi_BridgeCallback beginSceneCallback,
    PFN_remixapi_BridgeCallback endSceneCallback,
    PFN_remixapi_BridgeCallback presentCallback) {

    static_assert(strings_equal(__func__, remixapi::exported_func_name::registerCallbacks));
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_RegisterCallbacks);

    remixapi::g_beginSceneCallback = beginSceneCallback;
    remixapi::g_endSceneCallback = endSceneCallback;
    remixapi::g_presentCallback = presentCallback;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

}

}
