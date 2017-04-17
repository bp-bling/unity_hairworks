﻿#include "pch.h"
#include "hwInternal.h"
#include "hwContext.h"
#include "IUnityGraphics.h"

struct hwPluginContext
{
    IUnityInterfaces    *unity_interface;
    IUnityGraphics      *unity_graphics;
    IUnityGraphicsD3D11 *unity_graphics_d3d11;
    ID3D11Device        *d3d11_device;
    hwContext           *hw_ctx;
    hwLogCallback       log_callback;

    hwPluginContext()
        : unity_interface(nullptr)
        , unity_graphics(nullptr)
        , unity_graphics_d3d11(nullptr)
        , d3d11_device(nullptr)
        , hw_ctx(nullptr)
        , log_callback(nullptr)
    {}
};
hwPluginContext g_ctx;

#define g_unity_interface       g_ctx.unity_interface
#define g_unity_graphics        g_ctx.unity_graphics
#define g_unity_graphics_d3d11  g_ctx.unity_graphics_d3d11
#define g_d3d11_device          g_ctx.d3d11_device
#define g_hw_ctx                g_ctx.hw_ctx
#define g_log_callback          g_ctx.log_callback


static void UNITY_INTERFACE_API UnityOnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    // nothing todo?
    if (eventType == kUnityGfxDeviceEventInitialize) {
    }
}

static void UNITY_INTERFACE_API UnityRenderEvent(int eventID)
{
    if (eventID == 0) {
        if (auto ctx = hwGetContext()) {
            ctx->flush();
        }
    }
	else if (eventID == 1)
	{
		if (auto ctx = hwGetContext()) {
			ctx->flushVR();
		}
	}
	else if (eventID == 2)
	{
		if (auto ctx = hwGetContext()) {
			ctx->flushVRSinglePass();
		}
	}
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    g_unity_interface = unityInterfaces;
    g_unity_graphics = g_unity_interface->Get<IUnityGraphics>();
    if (g_unity_graphics->GetRenderer() == kUnityGfxRendererD3D11) {
        g_unity_graphics_d3d11 = g_unity_interface->Get<IUnityGraphicsD3D11>();
        g_d3d11_device = g_unity_graphics_d3d11->GetDevice();
        g_unity_graphics->RegisterDeviceEventCallback(UnityOnGraphicsDeviceEvent);

        // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
        // to not miss the event in case the graphics device is already initialized
        UnityOnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
    }
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginUnload()
{
    g_unity_graphics->UnregisterDeviceEventCallback(UnityOnGraphicsDeviceEvent);
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
hwGetRenderEventFunc()
{
    return UnityRenderEvent;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// PatchLibrary 用のコンテキスト移動処理群

hwExport hwPluginContext* hwGetPluginContext()
{
    return &g_ctx;
}
typedef hwPluginContext* (*hwGetPluginContextT)();

// PatchLibrary で突っ込まれたモジュールは UnityPluginLoad() が呼ばれないので、
// DLL_PROCESS_ATTACH のタイミングで先にロードされているモジュールからコンテキストを移管して同等の処理を行う。
BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason_for_call, LPVOID reserved)
{
    if (reason_for_call == DLL_PROCESS_ATTACH)
    {
        if (HMODULE m = ::GetModuleHandleA("HairWorksIntegration.dll")) {
            auto proc = (hwGetPluginContextT)::GetProcAddress(m, "hwGetPluginContext");
            if (proc) {
                auto *old = proc();
                g_ctx = *old;
                if (g_ctx.unity_graphics) {
                    g_ctx.unity_graphics->RegisterDeviceEventCallback(UnityOnGraphicsDeviceEvent);
                }
                if (old->hw_ctx) {
                    g_ctx.hw_ctx = new hwContext();
                    g_ctx.hw_ctx->move(*old->hw_ctx);
                    delete old->hw_ctx;
                    old->hw_ctx = g_ctx.hw_ctx;
                }
            }
        }
    }
    else if (reason_for_call == DLL_PROCESS_DETACH)
    {
    }
    return TRUE;
}

// "DllMain already defined in MSVCRT.lib" 対策
#if defined(_M_IX86)
extern "C" { int __afxForceUSRDLL; }
#elif defined(_M_X64)
extern "C" { int _afxForceUSRDLL; }
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////




void hwLogImpl(const char* fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);

    char buf[2048];
    vsprintf(buf, fmt, vl);
#ifdef hwWindows
    ::OutputDebugStringA(buf);
#else // hwWindows
    printf(buf);
#endif // hwWindows
    if (g_log_callback) { g_log_callback(buf); }

    va_end(vl);
}


extern "C" {

hwExport int hwGetSDKVersion()
{
    return GFSDK_HAIRWORKS_VERSION;
}

hwExport bool hwLoadHairWorks()
{
    if (auto sdk = hwContext::loadSDK()) {
        return true;
    }
    return false;
}

hwExport void hwUnloadHairWorks()
{
    hwContext::unloadSDK();
}

hwExport bool hwInitialize()
{
    if (g_hw_ctx != nullptr) {
        return true;
    }

    g_hw_ctx = new hwContext();
    if (g_hw_ctx->initialize(g_d3d11_device)) {
        return true;
    }
    else {
        hwFinalize();
        return false;
    }
}

hwExport void hwFinalize()
{
    delete g_hw_ctx;
    g_hw_ctx = nullptr;
}

hwExport hwContext* hwGetContext()
{
    hwInitialize();
    return g_hw_ctx;
}



hwExport void hwSetLogCallback(hwLogCallback cb)
{
    g_log_callback = cb;
}

hwExport hwHShader hwShaderLoadFromFile(const char *path)
{
    if (path == nullptr || path[0] == '\0') { return hwNullHandle; }
    if (auto ctx = hwGetContext()) {
        return ctx->shaderLoadFromFile(path);
    }
    return hwNullHandle;
}
hwExport void hwShaderRelease(hwHShader sid)
{
    if (auto ctx = hwGetContext()) {
        ctx->shaderRelease(sid);
    }
}

hwExport void hwShaderReload(hwHShader sid)
{
    if (auto ctx = hwGetContext()) {
        ctx->shaderReload(sid);
    }
}


hwExport hwHAsset hwAssetLoadFromFile(const char *path)
{
    if (path == nullptr || path[0]=='\0') { return hwNullHandle; }
    if (auto ctx = hwGetContext()) {
        return ctx->assetLoadFromFile(path, nullptr);
    }
    return hwNullHandle;
}
hwExport void hwAssetRelease(hwHAsset aid)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetRelease(aid);
    }
}

hwExport void hwAssetReload(hwHAsset aid)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetReload(aid);
    }
}

hwExport const char* hwAssetGetTextureName(hwHAsset aid, int textureType)
{
	if (auto ctx = hwGetContext()) {
		return ctx->assetGetTextureName(aid, textureType);
	}
	return nullptr;
}

hwExport void hwEnableVRRendering(bool enable)
{
	if (auto ctx = hwGetContext()) {
		return ctx->EnableVRRendering(enable);
	}
}

hwExport int hwAssetGetNumBones(hwHAsset aid)
{
    if (auto ctx = hwGetContext()) {
        return ctx->assetGetNumBones(aid);
    }
    return 0;
}

hwExport const char* hwAssetGetBoneName(hwHAsset aid, int nth)
{
    if (auto ctx = hwGetContext()) {
        return ctx->assetGetBoneName(aid, nth);
    }
    return nullptr;
}

hwExport void hwAssetGetBoneIndices(hwHAsset aid, hwFloat4 &o_indices)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetGetBoneIndices(aid, o_indices);
    }
}

hwExport void hwAssetGetBoneWeights(hwHAsset aid, hwFloat4 &o_weight)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetGetBoneWeights(aid, o_weight);
    }
}

hwExport void hwAssetGetBindPose(hwHAsset aid, int nth, hwMatrix &o_mat)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetGetBindPose(aid, nth, o_mat);
    }
}

hwExport void hwAssetGetDefaultDescriptor(hwHAsset aid, hwHairDescriptor &o_desc)
{
    if (auto ctx = hwGetContext()) {
        ctx->assetGetDefaultDescriptor(aid, o_desc);
    }
}


hwExport hwHInstance hwInstanceCreate(hwHAsset aid)
{
    if (auto ctx = hwGetContext()) {
        return ctx->instanceCreate(aid);
    }
    return hwNullHandle;
}
hwExport void hwInstanceRelease(hwHInstance iid)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceRelease(iid);
    }
}
hwExport void hwInstanceGetBounds(hwHInstance iid, hwFloat3 *o_min, hwFloat3 *o_max)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceGetBounds(iid, *o_min, *o_max);
    }
}
hwExport void hwInstanceGetDescriptor(hwHInstance iid, hwHairDescriptor *desc)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceGetDescriptor(iid, *desc);
    }
}
hwExport void hwInstanceSetDescriptor(hwHInstance iid, const hwHairDescriptor *desc)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceSetDescriptor(iid, *desc);
    }
}
hwExport void hwInstanceSetTexture(hwHInstance iid, hwTextureType type, hwTexture *tex)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceSetTexture(iid, type, tex);
    }
}
hwExport void hwInstanceSetTextureIntoDevice(hwHInstance iid, hwTextureType type)
{
	if (auto ctx = hwGetContext()) {
		ctx->instanceSetTextureIntoDevice(iid, type);
	}
}

hwExport void hwInstanceUpdateSkinningMatrices(hwHInstance iid, int num_bones, hwMatrix *matrices)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceUpdateSkinningMatrices(iid, num_bones, matrices);
    }
}

hwExport void hwInstanceUpdateSkinningMatricesAsync(hwHInstance iid, int num_bones, hwMatrix *matrices, bool vrMode)
{
	if (auto ctx = hwGetContext()) {
		ctx->instanceUpdateSkinningMatricesAsync(iid, num_bones, matrices, vrMode);
	}
}

hwExport void hwInstanceUpdateSkinningDQs(hwHInstance iid, int num_bones, hwDQuaternion *dqs)
{
    if (auto ctx = hwGetContext()) {
        ctx->instanceUpdateSkinningDQs(iid, num_bones, dqs);
    }
}


hwExport void hwBeginScene(bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->beginScene(vrMode);
    }
}
hwExport void hwEndScene(bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->endScene(vrMode);
    }
}

hwExport void hwSetViewProjection(const hwMatrix *view, const hwMatrix *proj, float fov)
{
    if (auto ctx = hwGetContext()) {
        ctx->setViewProjection(*view, *proj, fov);
    }
}

hwExport void hwSetViewProjectionStereo(const hwMatrix *view, const hwMatrix *proj, const hwMatrix *view2, const hwMatrix *proj2, float fov, bool singlePassStereo)
{
	if (auto ctx = hwGetContext()) {
		ctx->setViewProjectionStereo(*view, *proj, *view2, *proj2, fov, singlePassStereo);
	}
}

hwExport void hwSetRenderTarget(hwTexture *framebuffer, hwTexture *depthbuffer, bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->setRenderTarget(framebuffer, depthbuffer, vrMode);
    }
}

hwExport void hwSetShader(hwHShader sid, bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->setShader(sid, vrMode);
    }
}

hwExport void hwSetLights(int num_lights, const hwLightData *lights, bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->setLights(num_lights, lights, vrMode);
    }
}

hwExport void hwRender(hwHInstance iid, bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->render(iid, vrMode);
    }
}

hwExport void hwRenderShadow(hwHInstance iid, bool vrMode)
{
    if (auto ctx = hwGetContext()) {
        ctx->renderShadow(iid, vrMode);
    }
}

hwExport void hwStepSimulation(float dt, bool vrMode, bool singlePassVR)
{
    if (auto ctx = hwGetContext()) {
        ctx->stepSimulation(dt, vrMode, singlePassVR);
    }
}

hwExport void hwSetShuttingDownFlag()
{
	if (auto ctx = hwGetContext()) {
		ctx->SetShuttingDownFlag();
	}
}

} // extern "C"
