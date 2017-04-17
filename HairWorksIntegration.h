#pragma once

#ifdef _WIN32
    #ifdef hwImpl
        #define hwExport __declspec(dllexport)
    #else
        #define hwExport __declspec(dllimport)
    #endif
#endif

typedef GFSDK_HairSDK                   hwSDK;
typedef GFSDK_HairAssetID               hwAssetID;
typedef GFSDK_HairInstanceID            hwInstanceID;
typedef GFSDK_HairInstanceDescriptor    hwHairDescriptor;
typedef GFSDK_HairConversionSettings    hwConversionSettings;
typedef GFSDK_HAIR_TEXTURE_TYPE         hwTextureType;

typedef gfsdk_float3            hwFloat3;
typedef gfsdk_float4            hwFloat4;
typedef gfsdk_dualquaternion    hwDQuaternion;
typedef gfsdk_float4x4          hwMatrix;
typedef uint32_t                hwHShader;      // H stands for Handle
typedef uint32_t                hwHAsset;       // 
typedef uint32_t                hwHInstance;    // 

typedef ID3D11Device                    hwDevice;
typedef ID3D11Texture2D                 hwTexture;
typedef ID3D11ShaderResourceView        hwSRV;
typedef ID3D11RenderTargetView          hwRTV;

typedef void(__stdcall *hwLogCallback)(const char *);
#define hwNullAssetID       GFSDK_HairAssetID_NULL
#define hwNullInstanceID    GFSDK_HairInstanceID_NULL
#define hwNullHandle        0xFFFFFFFF
#define hwMaxLights         8


struct  hwShaderData;
struct  hwAssetData;
struct  hwInstanceData;
struct  hwLightData;
class   hwContext;


// Unity plugin callbacks
extern "C" {
hwExport int            hwGetSDKVersion();
hwExport bool           hwLoadHairWorks();
hwExport void           hwUnloadHairWorks();

hwExport bool           hwInitialize();
hwExport void           hwFinalize();
hwExport hwContext*     hwGetContext();
hwExport int            hwGetFlushEventID();
hwExport void           hwSetLogCallback(hwLogCallback cb);

hwExport hwHShader      hwShaderLoadFromFile(const char *path);
hwExport void           hwShaderRelease(hwHShader sid);
hwExport void           hwShaderReload(hwHShader sid);

hwExport hwHAsset       hwAssetLoadFromFile(const char *path);
hwExport void           hwAssetRelease(hwHAsset aid);
hwExport void           hwAssetReload(hwHAsset aid);
hwExport int            hwAssetGetNumBones(hwHAsset aid);
hwExport const char*    hwAssetGetBoneName(hwHAsset aid, int nth);
hwExport void           hwAssetGetBoneIndices(hwHAsset aid, hwFloat4 &o_indices);
hwExport void           hwAssetGetBoneWeights(hwHAsset aid, hwFloat4 &o_weight);
hwExport void           hwAssetGetBindPose(hwHAsset aid, int nth, hwMatrix &o_mat);
hwExport void           hwAssetGetDefaultDescriptor(hwHAsset aid, hwHairDescriptor &o_desc);

// WayGate 
hwExport const char*    hwAssetGetTextureName(hwHAsset aid, int textureType);
hwExport void hwEnableVRRendering(bool enable);
hwExport void hwSetShuttingDownFlag();
// WayGate

hwExport hwHInstance    hwInstanceCreate(hwHAsset aid);
hwExport void           hwInstanceRelease(hwHInstance iid);
hwExport void           hwInstanceGetAssetID(hwHInstance iid);
hwExport void           hwInstanceGetBounds(hwHInstance iid, hwFloat3 *o_min, hwFloat3 *o_max);
hwExport void           hwInstanceGetDescriptor(hwHInstance iid, hwHairDescriptor *o_desc);
hwExport void           hwInstanceSetDescriptor(hwHInstance iid, const hwHairDescriptor *desc);
hwExport void           hwInstanceSetTexture(hwHInstance iid, hwTextureType type, hwTexture *tex);
hwExport void			hwInstanceSetTextureIntoDevice(hwHInstance hi, hwTextureType type);
hwExport void           hwInstanceUpdateSkinningMatrices(hwHInstance iid, int num_bones, hwMatrix *matrices);
hwExport void			hwInstanceUpdateSkinningMatricesAsync(hwHInstance iid, int num_bones, hwMatrix *matrices, bool vrMode);
hwExport void           hwInstanceUpdateSkinningDQs(hwHInstance iid, int num_bones, hwDQuaternion *dqs);

hwExport void           hwBeginScene(bool vrMode);
hwExport void           hwEndScene(bool vrMode);
hwExport void           hwSetViewProjection(const hwMatrix *view, const hwMatrix *proj, float fov);
hwExport void           hwSetViewProjectionStereo(const hwMatrix *view, const hwMatrix *proj, const hwMatrix *view2, const hwMatrix *proj2, float fov, bool singlePassStereo);
hwExport void           hwSetRenderTarget(hwTexture *framebuffer, hwTexture *depthbuffer, bool vrMode);
hwExport void           hwSetShader(hwHShader sid, bool vrMode);
hwExport void           hwSetLights(int num_lights, const hwLightData *lights, bool vrMode);
hwExport void           hwRender(hwHInstance iid, bool vrMode);
hwExport void           hwRenderShadow(hwHInstance iid, bool vrMode);
hwExport void           hwStepSimulation(float dt, bool vrMode, bool singlePassVR);
} // extern "C"
