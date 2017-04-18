#include "pch.h"
#include "hwInternal.h"
#include "hwContext.h"
#include "DXUT.h"

#if defined(_M_IX86)
    #define hwSDKDLL "GFSDK_HairWorks.win32.dll"
#elif defined(_M_X64)
    #define hwSDKDLL "GFSDK_HairWorks.win64.dll"
#endif

bool operator==(const hwConversionSettings &a, const hwConversionSettings &b)
{
#define cmp(V) a.V==b.V
    return cmp(m_targetUpAxisHint) && cmp(m_targetHandednessHint) && cmp(m_pConversionMatrix) && cmp(m_targetSceneUnit);
#undef cmp
}

bool hwFileToString(std::string &o_buf, const char *path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { return false; }
    f.seekg(0, std::ios::end);
    o_buf.resize(f.tellg());
    f.seekg(0, std::ios::beg);
    f.read(&o_buf[0], o_buf.size());
    return true;
}

// Prepare the render target for HairWorks rendering
void PrepareHairWorksRenderTarget(ID3D11DeviceContext* pd3dContext, ID3D11Device *DirectXDvice)
{
	bool setRenderTargetZFunc	= false;
	bool clearZBuffer			= false;

	if (setRenderTargetZFunc == true)
	{
		unsigned int depthRef;
		D3D11_DEPTH_STENCIL_DESC descriptor;
		ID3D11DepthStencilState *depthState;

		pd3dContext->OMGetDepthStencilState(&depthState, &depthRef);
		if (depthState == nullptr)
		{
			descriptor.DepthEnable = true;
			descriptor.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			descriptor.DepthFunc = D3D11_COMPARISON_GREATER;
			descriptor.StencilEnable = false;
			DirectXDvice->CreateDepthStencilState(&descriptor, &depthState);
			pd3dContext->OMSetDepthStencilState(depthState, depthRef);
		}
		else
		{
			depthState->GetDesc(&descriptor);
			descriptor.DepthFunc = D3D11_COMPARISON_GREATER;
			DirectXDvice->CreateDepthStencilState(&descriptor, &depthState);
			pd3dContext->OMSetDepthStencilState(depthState, depthRef);
		}
	}

	if (clearZBuffer == true)
	{
		// Get the depth stencil and render target
		ID3D11RenderTargetView* pRTV;
		ID3D11DepthStencilView* pDSV;
		pd3dContext->OMGetRenderTargets(1, &pRTV, &pDSV);
		pd3dContext->OMSetRenderTargets(1, &pRTV, pDSV);
		pd3dContext->ClearDepthStencilView(pDSV, D3D11_CLEAR_DEPTH, 1.0, 0);
	}
}

hwSDK *g_hw_sdk = nullptr;
hwSDK* hwContext::loadSDK()
{
    if (g_hw_sdk) {
        return g_hw_sdk;
    }

    char path[MAX_PATH] = {0};
    if(path[0] == 0) {
        // get path to this module
        HMODULE mod = 0;
        ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&hwInitialize, &mod);
        DWORD size = ::GetModuleFileNameA(mod, path, sizeof(path));
        for (int i = size - 1; i >= 0; --i) {
            if (path[i] == '\\') {
                path[i + 1] = '\0';
                std::strncat(path, hwSDKDLL, MAX_PATH);
                break;
            }
        }
    }
    g_hw_sdk = GFSDK_LoadHairSDK(path, GFSDK_HAIRWORKS_VERSION);
    hwLog("hwContext::loadSDK(): %s (%s)\n", g_hw_sdk ? "succeeded" : "failed", path);
    return g_hw_sdk;
}

void hwContext::unloadSDK()
{
    if (g_hw_sdk) {
        g_hw_sdk->Release();
        g_hw_sdk = nullptr;
        hwLog("hwContext::unloadSDK()\n");
    }
}

hwContext::hwContext()
{
	InitVRVariables();
}

hwContext::~hwContext()
{
    finalize();
}

void hwContext::EnableVRRendering(bool enabled)
{
	m_VRRendering = enabled;
}

void hwContext::InitVRVariables()
{
	m_shuttingDown  = 0;
	m_currentVRPass = 0;
	m_VRRendering   = false;
}

void hwContext::StoreMatrixLocally(const hwMatrix &view, const hwMatrix &proj, float fov, int eyeSlot)
{
	if (eyeSlot == 0)
	{
		memcpy(&m_view0, &view, sizeof(hwMatrix));
		memcpy(&m_proj0, &proj, sizeof(hwMatrix));
		float fov0 = fov;
	}
	else
	{
		memcpy(&m_view1, &view, sizeof(hwMatrix));
		memcpy(&m_proj1, &proj, sizeof(hwMatrix));
		float fov1 = fov;
	}
}

bool hwContext::valid() const
{
    return m_d3ddev!=nullptr && m_d3dctx!=nullptr && g_hw_sdk!=nullptr;
}

bool hwContext::initialize(hwDevice *d3d_device)
{
    if (d3d_device == nullptr)
		return false;

    g_hw_sdk = loadSDK();
    
	if (g_hw_sdk != nullptr)
	{
        hwLog("GFSDK_LoadHairSDK() succeeded.\n");
    }
    else
	{
        hwLog("GFSDK_LoadHairSDK() failed.\n");
        return false;
    }

    if (g_hw_sdk->InitRenderResources((ID3D11Device*)d3d_device) == GFSDK_HAIR_RETURN_OK)
	{
        hwLog("GFSDK_HairSDK::InitRenderResources() succeeded.\n");
    }
    else
	{
        hwLog("GFSDK_HairSDK::InitRenderResources() failed.\n");
        finalize();
        return false;
    }

    m_d3ddev = (ID3D11Device*)d3d_device;
	m_d3ddev->GetImmediateContext(&m_d3dctx);

	if (g_hw_sdk->SetCurrentContext(m_d3dctx) == GFSDK_HAIR_RETURN_OK) 
	{
		hwLog("GFSDK_HairSDK::SetCurrentContext() succeeded.\n");
	}
	else
	{
		hwLog("GFSDK_HairSDK::SetCurrentContext() failed.\n");
		finalize();
		return false;
	}

	// set the Z mode here
	CD3D11_DEPTH_STENCIL_DESC depthDesc;
	depthDesc.DepthEnable		= true;
	depthDesc.DepthWriteMask	= D3D11_DEPTH_WRITE_MASK_ALL;
	depthDesc.DepthFunc			= D3D11_COMPARISON_GREATER_EQUAL;
	depthDesc.StencilEnable		= false;
	m_d3ddev->CreateDepthStencilState(&depthDesc, &m_rs_enable_depth);

	// create constant buffer for hair rendering pixel shader
	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = sizeof(hwConstantBuffer);
	bufferDesc.StructureByteStride = 0;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.MiscFlags = 0;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_d3ddev->CreateBuffer(&bufferDesc, 0, &m_rs_constant_buffer);

	// rasterized mode
	D3D11_RASTERIZER_DESC rsdesc;
	memset(&rsdesc, 0, sizeof(rsdesc));
	rsdesc.FillMode = D3D11_FILL_SOLID;
	rsdesc.CullMode = D3D11_CULL_NONE;
	m_d3ddev->CreateRasterizerState(&rsdesc, &m_RasterState);

	// sampler is null at the beginning
	m_HairShaderSampler = nullptr;

	// 
    return true;
}

void hwContext::finalize()
{
    for (auto &i : m_instances) { instanceRelease(i.handle); }
    m_instances.clear();

    for (auto &i : m_assets) { assetRelease(i.handle); }
    m_assets.clear();

    for (auto &i : m_shaders) { shaderRelease(i.handle); }
    m_shaders.clear();

    for (auto &i : m_srvtable) { i.second->Release(); }
    m_srvtable.clear();

    for (auto &i : m_rtvtable) { i.second->Release(); }
    m_rtvtable.clear();

    if (m_rs_enable_depth) {
        m_rs_enable_depth->Release();
        m_rs_enable_depth = nullptr;
    }

    if (m_d3dctx)
    {
        m_d3dctx->Release();
        m_d3dctx = nullptr;
    }
}

void hwContext::move(hwContext &from)
{
#define mov(V) V=from.V; from.V=decltype(V)();
    mov(m_d3dctx);
    mov(m_d3ddev);
    mov(m_shaders);
    mov(m_assets);
    mov(m_instances);
    mov(m_srvtable);
    mov(m_rtvtable);
    mov(m_rs_enable_depth);
    mov(m_rs_constant_buffer);
#undef mov
}

hwShaderData& hwContext::newShaderData()
{
    auto i = std::find_if(m_shaders.begin(), m_shaders.end(), [](const hwShaderData &v) { return !v; });
    if (i != m_shaders.end()) { return *i; }

    hwShaderData tmp;
    tmp.handle = m_shaders.size();
    m_shaders.push_back(tmp);
    return m_shaders.back();
}

hwHShader hwContext::shaderLoadFromFile(const std::string &path)
{
    {
        auto i = std::find_if(m_shaders.begin(), m_shaders.end(), [&](const hwShaderData &v) { return v.path == path; });
        if (i != m_shaders.end() && i->ref_count > 0) {
            ++i->ref_count;
            return i->handle;
        }
    }

    std::string bin;
    if (!hwFileToString(bin, path.c_str())) {
        hwLog("failed to load shader (%s)\n", path.c_str());
        return hwNullHandle;
    }

    hwShaderData& v = newShaderData();
    v.path = path;
    if (SUCCEEDED(m_d3ddev->CreatePixelShader(&bin[0], bin.size(), nullptr, &v.shader))) {
        v.ref_count = 1;
        hwLog("CreatePixelShader(%s) : %d succeeded.\n", path.c_str(), v.handle);
        return v.handle;
    }
    else {
        hwLog("CreatePixelShader(%s) failed.\n", path.c_str());
    }
    return hwNullHandle;
}

void hwContext::shaderRelease(hwHShader hs)
{
    if (hs >= m_shaders.size()) { return; }

    auto &v = m_shaders[hs];
    if (v.ref_count > 0 && --v.ref_count == 0) {
        v.shader->Release();
        v.invalidate();
        hwLog("shaderRelease(%d)\n", hs);
    }
}

void hwContext::shaderReload(hwHShader hs)
{
    if (hs >= m_shaders.size()) { return; }

    auto &v = m_shaders[hs];
    // release existing shader
    if (v.shader) {
        v.shader->Release();
        v.shader = nullptr;
    }

    // reload
    std::string bin;
    if (!hwFileToString(bin, v.path.c_str())) {
        hwLog("failed to reload shader (%s)\n", v.path.c_str());
        return;
    }
    if (SUCCEEDED(m_d3ddev->CreatePixelShader(&bin[0], bin.size(), nullptr, &v.shader))) {
        hwLog("CreatePixelShader(%s) : %d reloaded.\n", v.path.c_str(), v.handle);
    }
    else {
        hwLog("CreatePixelShader(%s) failed to reload.\n", v.path.c_str());
    }
}

hwAssetData& hwContext::newAssetData()
{
    auto i = std::find_if(m_assets.begin(), m_assets.end(), [](const hwAssetData &v) { return !v; });
    if (i != m_assets.end()) { return *i; }


    hwAssetData tmp;
    tmp.handle = m_assets.size();
    m_assets.push_back(tmp);
    return m_assets.back();
}

hwHAsset hwContext::assetLoadFromFile(const std::string &path, const hwConversionSettings *_settings)
{
	bool forceUnityConversion = true;

	hwConversionSettings settings;
	if (_settings != nullptr) { settings = *_settings; }

	{
		auto i = std::find_if(m_assets.begin(), m_assets.end(),
			[&](const hwAssetData &v) { return v.path == path && v.settings == settings; });
		if (i != m_assets.end() && i->ref_count > 0) {
			++i->ref_count;
			return i->aid;
		}
	}

	hwAssetData& v = newAssetData();
	v.settings		= settings;
	v.path			= path;
																										// force import settings in Unity
	GFSDK_HairConversionSettings *conversionSettings = new GFSDK_HairConversionSettings;
	{
		conversionSettings->m_targetHandednessHint	= GFSDK_HAIR_LEFT_HANDED;
		conversionSettings->m_targetUpAxisHint		= GFSDK_HAIR_Y_UP;
		//conversionSettings->m_targetSceneUnit		= 1.0f; // centimeter
		//conversionSettings->m_pConversionMatrix = s_scaleMatrix;
	}
	
	GFSDK_HairConversionSettings *pSettings = nullptr;
	if (forceUnityConversion == true)
		pSettings = conversionSettings;

	if (g_hw_sdk->LoadHairAssetFromFile(path.c_str(), &v.aid, nullptr, pSettings) == GFSDK_HAIR_RETURN_OK)
	{
		v.ref_count = 1;

		hwLog("GFSDK_HairSDK::LoadHairAssetFromFile(\"%s\") : %d succeeded.\n", path.c_str(), v.handle);
		return v.handle;
	}
	else
	{
		hwLog("GFSDK_HairSDK::LoadHairAssetFromFile(\"%s\") failed.\n", path.c_str());
	}
	return hwNullHandle;
}

void hwContext::assetRelease(hwHAsset ha)
{
    if (ha >= m_assets.size()) { return; }

    auto &v = m_assets[ha];
    if (v.ref_count > 0 && --v.ref_count==0) {
        if (g_hw_sdk->FreeHairAsset(v.aid) == GFSDK_HAIR_RETURN_OK) {
            hwLog("GFSDK_HairSDK::FreeHairAsset(%d) succeeded.\n", ha);
        }
        else {
            hwLog("GFSDK_HairSDK::FreeHairAsset(%d) failed.\n", ha);
        }
        v.invalidate();
    }
}

void hwContext::assetReload(hwHAsset ha)
{
    if (ha >= m_assets.size()) { return; }

    auto &v = m_assets[ha];
    // release existing asset
    if (g_hw_sdk->FreeHairAsset(v.aid)) {
        v.aid = hwNullAssetID;
    }

    // reload
    if (g_hw_sdk->LoadHairAssetFromFile(v.path.c_str(), &v.aid, nullptr, &v.settings) == GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::LoadHairAssetFromFile(\"%s\") : %d reloaded.\n", v.path.c_str(), v.handle);
    }
    else {
        hwLog("GFSDK_HairSDK::LoadHairAssetFromFile(\"%s\") failed to reload.\n", v.path.c_str());
    }
}

int hwContext::assetGetNumBones(hwHAsset ha) const
{
    uint32_t r = 0;
    if (ha >= m_assets.size()) { return r; }

    if (g_hw_sdk->GetNumBones(m_assets[ha].aid, &r) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::GetNumBones(%d) failed.\n", ha);
    }
    return r;
}

const char* hwContext::assetGetBoneName(hwHAsset ha, int nth) const
{
    static char tmp[256];
    if (ha >= m_assets.size()) { tmp[0] = '\0'; return tmp; }

    if (g_hw_sdk->GetBoneName(m_assets[ha].aid, nth, tmp) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::GetBoneName(%d) failed.\n", ha);
    }
    return tmp;
}

const char* hwContext::assetGetTextureName(hwHAsset ha, int textureType) const
{
	static char textureFileName[1024];
	if (GFSDK_HAIR_RETURN_OK == g_hw_sdk->GetTextureName(m_assets[ha].aid, (GFSDK_HAIR_TEXTURE_TYPE)textureType, textureFileName))
		return textureFileName;
	else
		return nullptr;
}

void hwContext::assetGetBoneIndices(hwHAsset ha, hwFloat4 &o_indices) const
{
    if (ha >= m_assets.size()) { return; }

    if (g_hw_sdk->GetBoneIndices(m_assets[ha].aid, &o_indices) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::GetBoneIndices(%d) failed.\n", ha);
    }
}

void hwContext::assetGetBoneWeights(hwHAsset ha, hwFloat4 &o_weight) const
{
    if (ha >= m_assets.size()) { return; }

    if (g_hw_sdk->GetBoneWeights(m_assets[ha].aid, &o_weight) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::GetBoneWeights(%d) failed.\n", ha);
    }
}

void hwContext::assetGetBindPose(hwHAsset ha, int nth, hwMatrix &o_mat)
{
    if (ha >= m_assets.size()) { return; }

    if (g_hw_sdk->GetBindPose(m_assets[ha].aid, nth, &o_mat) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::GetBindPose(%d, %d) failed.\n", ha, nth);
    }
}

void hwContext::assetGetDefaultDescriptor(hwHAsset ha, hwHairDescriptor &o_desc) const
{
    if (ha >= m_assets.size()) { return; }

    if (g_hw_sdk->CopyInstanceDescriptorFromAsset(m_assets[ha].aid, o_desc) != GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::CopyInstanceDescriptorFromAsset(%d) failed.\n", ha);
    }
}

hwInstanceData& hwContext::newInstanceData()
{
    auto i = std::find_if(m_instances.begin(), m_instances.end(), [](const hwInstanceData &v) { return !v; });
    if (i != m_instances.end()) { return *i; }

    hwInstanceData tmp;
    tmp.handle = m_instances.size();
    m_instances.push_back(tmp);
    return m_instances.back();
}

hwHInstance hwContext::instanceCreate(hwHAsset ha)
{
	if (ha >= m_assets.size()) { return hwNullHandle; }
	hwInstanceData& v = newInstanceData();
	v.hasset = ha;
	if (g_hw_sdk->CreateHairInstance(m_assets[ha].aid, &v.iid) == GFSDK_HAIR_RETURN_OK) {
		hwLog("GFSDK_HairSDK::CreateHairInstance(%d) : %d succeeded.\n", ha, v.handle);
	}
	else
	{
		hwLog("GFSDK_HairSDK::CreateHairInstance(%d) failed.\n", ha);
	}
	return v.handle;
}

void hwContext::instanceRelease(hwHInstance hi)
{
    if (hi >= m_instances.size()) { return; }
    auto &v = m_instances[hi];

    if (g_hw_sdk->FreeHairInstance(v.iid) == GFSDK_HAIR_RETURN_OK) {
        hwLog("GFSDK_HairSDK::FreeHairInstance(%d) succeeded.\n", hi);
    }
    else {
        hwLog("GFSDK_HairSDK::FreeHairInstance(%d) failed.\n", hi);
    }
    v.invalidate();
}

void hwContext::instanceGetBounds(hwHInstance hi, hwFloat3 &o_min, hwFloat3 &o_max) const
{
    if (hi >= m_instances.size()) { return; }
    auto &v = m_instances[hi];

    if (g_hw_sdk->GetBounds(v.iid, &o_min, &o_max) != GFSDK_HAIR_RETURN_OK)
    {
        hwLog("GFSDK_HairSDK::GetBounds(%d) failed.\n", hi);
    }
}

void hwContext::instanceGetDescriptor(hwHInstance hi, hwHairDescriptor &desc) const
{
	if (hi >= m_instances.size()) { return; }
	auto &v = m_instances[hi];

	if (g_hw_sdk->CopyCurrentInstanceDescriptor(v.iid, desc) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::CopyCurrentInstanceDescriptor(%d) failed.\n", hi);
	}	
}

void hwContext::instanceSetDescriptor(hwHInstance hi, const hwHairDescriptor &desc)
{
	if (hi >= m_instances.size()) { return; }
	auto &v = m_instances[hi];

	if (g_hw_sdk->UpdateInstanceDescriptor(v.iid, desc) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::UpdateInstanceDescriptor(%d) failed.\n", hi);
	}	
}

void hwContext::instanceSetTexture(hwHInstance hi, hwTextureType type, hwTexture *tex)
{
	if (m_d3dctx != nullptr && g_hw_sdk != nullptr)
	{
		if (hi >= m_instances.size()) { return; }
		auto &v = m_instances[hi];

		auto *srv = getSRV(tex);
		if (!srv || g_hw_sdk->SetTextureSRV(v.iid, type, srv) != GFSDK_HAIR_RETURN_OK)
		{
			hwLog("GFSDK_HairSDK::SetTextureSRV(%d, %d) failed.\n", hi, type);
		}
	}
}

void hwContext::instanceSetTextureIntoDevice(hwHInstance hi, hwTextureType type)
{
	if (m_d3dctx != nullptr && g_hw_sdk != nullptr)
	{
		// set texture sampler for texture color in hair shader
		ID3D11SamplerState **states = new  ID3D11SamplerState*[1];
		ID3D11SamplerState *localSampler;
		getSamplerLinear(m_d3ddev, &localSampler);
		states[0] = localSampler;
		m_d3dctx->PSSetSamplers(0, 1, states);

		if (hi >= m_instances.size()) { return; }
		auto &v = m_instances[hi];

		// set textures
		ID3D11ShaderResourceView* ppTextureSRVs[2];
		g_hw_sdk->GetTextureSRV(v.iid, GFSDK_HAIR_TEXTURE_ROOT_COLOR, &ppTextureSRVs[0]);
		g_hw_sdk->GetTextureSRV(v.iid, GFSDK_HAIR_TEXTURE_TIP_COLOR, &ppTextureSRVs[1]);

		// set to resource slot for our shader
		m_d3dctx->PSSetShaderResources(GFSDK_HAIR_NUM_SHADER_RESOUCES, 2, ppTextureSRVs);
	}
}

void hwContext::getSamplerLinear(ID3D11Device* pd3dDevice, ID3D11SamplerState** ppTexSampler)
{
	if (m_d3dctx != nullptr)
	{
		if (m_HairShaderSampler == nullptr)
		{
			D3D11_SAMPLER_DESC samplerDesc[1] = {
				D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
				D3D11_TEXTURE_ADDRESS_CLAMP,
				D3D11_TEXTURE_ADDRESS_CLAMP,
				D3D11_TEXTURE_ADDRESS_CLAMP,
				0.0, 0, D3D11_COMPARISON_NEVER, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, D3D11_FLOAT32_MAX,
			};

			pd3dDevice->CreateSamplerState(samplerDesc, ppTexSampler);
			m_HairShaderSampler = *ppTexSampler;
		}
		else
		{
			*ppTexSampler = m_HairShaderSampler;
		}
	}
	else
	{
		*ppTexSampler = nullptr;
	}
}

//
void hwContext::instanceUpdateSkinningMatrices(hwHInstance hi, int num_bones, hwMatrix *matrices)
{
    if (matrices == nullptr) { return; }
    if (hi >= m_instances.size()) { return; }
    auto &v = m_instances[hi];

    if (g_hw_sdk->UpdateSkinningMatrices(v.iid, num_bones, matrices) != GFSDK_HAIR_RETURN_OK)
    {
        hwLog("GFSDK_HairSDK::UpdateSkinningMatrices(%d) failed.\n", hi);
    }
}

//
void hwContext::instanceUpdateSkinningMatricesAsync(hwHInstance hi, int num_bones, hwMatrix *matrices, bool vrMode)
{
	if (matrices == nullptr) { return; }
	if (hi >= m_instances.size()) { return; }
	auto &v = m_instances[hi];

	// store matrix locally
	int startIndex = 0;
	addBoneMatricesToBuffer(matrices, num_bones, startIndex);
	
	pushDeferredCall([=]() {
		instanceUpdateSkinningMatricesAsyncImpl(v.iid, startIndex, num_bones);
	}, vrMode);
}

//
void hwContext::instanceUpdateSkinningMatricesAsyncImpl(hwInstanceID instance, int matrixIndex, int numMatrix)
{
	if (g_hw_sdk->UpdateSkinningMatrices(instance, numMatrix, m_bonesMatrixBuffer + matrixIndex) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::UpdateSkinningMatrices(%d) failed.\n", instance);
	}
}

void hwContext::instanceUpdateSkinningDQs(hwHInstance hi, int num_bones, hwDQuaternion *dqs)
{
    if (dqs == nullptr) { return; }
    if (hi >= m_instances.size()) { return; }
    auto &v = m_instances[hi];

    if (g_hw_sdk->UpdateSkinningDQs(v.iid, num_bones, dqs) != GFSDK_HAIR_RETURN_OK)
    {
        hwLog("GFSDK_HairSDK::UpdateSkinningDQs(%d) failed.\n", hi);
    }
}

void hwContext::beginScene(bool vrMode)
{
	if (vrMode == true)
	{
		m_mutexVR.lock();
	}
	else
	{
		m_mutex.lock();
	}
}

void hwContext::endScene(bool vrMode)
{
	if (vrMode == true)
	{
		m_mutexVR.unlock();
	}
	else
	{
		m_mutex.unlock();
	}
}

void hwContext::pushDeferredCall(const DeferredCall &c, bool useVRQueue)
{
	if (useVRQueue == true)
	{
		m_commandsVR.push_back(c);
	}
	else
	{
		m_commands.push_back(c);
	}
}

void hwContext::setRenderTarget(hwTexture *framebuffer, hwTexture *depthbuffer, bool vrMode)
{
    pushDeferredCall([=]() {
        setRenderTargetImpl(framebuffer, depthbuffer);
    }, vrMode);
}

void hwContext::setShader(hwHShader hs, bool vrMode)
{
    pushDeferredCall([=]() {
        setShaderImpl(hs);
    }, vrMode);
}

void hwContext::setLights(int num_lights, const hwLightData *lights_, bool vrMode)
{
    std::array<hwLightData, hwMaxLights> lights;
    num_lights = std::min<int>(num_lights, hwMaxLights);
    std::copy(lights_, lights_ + num_lights, &lights[0]);
    pushDeferredCall([=]() {
        setLightsImpl(num_lights, &lights[0]);
    }, vrMode);
}

void hwContext::render(hwHInstance hi, bool vrMode)
{
    pushDeferredCall([=]() {
        renderImpl(hi);
    }, vrMode);
}

void hwContext::renderShadow(hwHInstance hi, bool vrMode)
{
    pushDeferredCall([=]() {
        renderShadowImpl(hi);
    }, vrMode);
}

void hwContext::stepSimulation(float dt, bool vrMode, bool singlePassVR)
{
    pushDeferredCall([=]() {
        stepSimulationImpl(dt, vrMode, singlePassVR);
    }, vrMode);
}

hwSRV* hwContext::getSRV(hwTexture *tex)
{
    {
        auto i = m_srvtable.find(tex);
        if (i != m_srvtable.end())
		{
            return i->second;
        }
    }

    hwSRV *ret = nullptr;
    if (SUCCEEDED(m_d3ddev->CreateShaderResourceView(tex, nullptr, &ret))) 
	{
        m_srvtable[tex] = ret;
    }
    return ret;
}

hwRTV* hwContext::getRTV(hwTexture *tex)
{
    {
        auto i = m_rtvtable.find(tex);
        if (i != m_rtvtable.end())
		{
            return i->second;
        }
    }

    hwRTV *ret = nullptr;
    if (SUCCEEDED(m_d3ddev->CreateRenderTargetView(tex, nullptr, &ret))) 
	{
        m_rtvtable[tex] = ret;
    }
    return ret;
}

void hwContext::setRenderTargetImpl(hwTexture *framebuffer, hwTexture *depthbuffer)
{
    // todo
}

void hwContext::setViewProjectionStereo(const hwMatrix &view, const hwMatrix &proj, const hwMatrix &view2, const hwMatrix &proj2, float fov, bool singlePassStereo)
{
	pushDeferredCall([=]() {
		setViewProjectionStereoImpl(view, proj, view2, proj2, fov, singlePassStereo);
	}, true);
}

void hwContext::setViewProjectionStereoImpl(const hwMatrix &view, const hwMatrix &proj, const hwMatrix &view2, const hwMatrix &proj2, float fov, bool singlePassStereo)
{
	if (singlePassStereo == true)
	{
		if (singlePassStereoRenderPass == 0)
		{
			if (g_hw_sdk->SetViewProjection((const gfsdk_float4x4*)&view, (const gfsdk_float4x4*)&proj, GFSDK_HAIR_LEFT_HANDED, fov) != GFSDK_HAIR_RETURN_OK)
			{
				hwLog("GFSDK_HairSDK::SetViewProjection() failed.\n");
			}
		}
		else
		{
			if (g_hw_sdk->SetViewProjection((const gfsdk_float4x4*)&view2, (const gfsdk_float4x4*)&proj2, GFSDK_HAIR_LEFT_HANDED, fov) != GFSDK_HAIR_RETURN_OK)
			{
				hwLog("GFSDK_HairSDK::SetViewProjection() failed.\n");
			}
		}
	}
	else
	{
		// prepare view and projection matrices based on render pass
		if (m_currentVRPass == 0)
		{
			if (g_hw_sdk->SetViewProjection((const gfsdk_float4x4*)&view, (const gfsdk_float4x4*)&proj, GFSDK_HAIR_LEFT_HANDED, fov) != GFSDK_HAIR_RETURN_OK)
			{
				hwLog("GFSDK_HairSDK::SetViewProjection() failed.\n");
			}
		}
		else
		{
			if (g_hw_sdk->SetViewProjection((const gfsdk_float4x4*)&view2, (const gfsdk_float4x4*)&proj2, GFSDK_HAIR_LEFT_HANDED, fov) != GFSDK_HAIR_RETURN_OK)
			{
				hwLog("GFSDK_HairSDK::SetViewProjection() failed.\n");
			}
		}
	}
}

//
void hwContext::setViewProjection(const hwMatrix &view, const hwMatrix &proj, float fov)
{
	// store the matrix locally
	pushDeferredCall([=]() {
		setViewProjectionImpl(view, proj, fov);
	}, false);
}

void hwContext::setViewProjectionImpl(const hwMatrix &view, const hwMatrix &proj, float fov)
{
	// set the view/projection matrix 
	if (g_hw_sdk->SetViewProjection((const gfsdk_float4x4*)&view, (const gfsdk_float4x4*)&proj, GFSDK_HAIR_LEFT_HANDED, fov) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::SetViewProjection() failed.\n");
	}
}

void hwContext::setShaderImpl(hwHShader hs)
{
	if (hs >= m_shaders.size()) { return; }

	auto &v = m_shaders[hs];
	if (v.shader) 
		m_d3dctx->PSSetShader(v.shader, nullptr, 0);
}

void hwContext::setLightsImpl(int num_lights, const hwLightData *lights)
{
    m_cb.num_lights = num_lights;
    std::copy(lights, lights + num_lights, m_cb.lights);
}

// main rendering function
void hwContext::renderImpl(hwHInstance hi)
{
	if (hi >= m_instances.size()) { return; }
	auto &v = m_instances[hi];

	// not needed for now PrepareHairWorksRenderTarget(m_d3dctx, m_d3ddev);

	// update constant buffer
	{
		g_hw_sdk->PrepareShaderConstantBuffer(v.iid, &m_cb.hw);

		D3D11_MAPPED_SUBRESOURCE MappedResource;
		m_d3dctx->Map(m_rs_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
		*((hwConstantBuffer*)MappedResource.pData) = m_cb;
		m_d3dctx->Unmap(m_rs_constant_buffer, 0);

		m_d3dctx->PSSetConstantBuffers(0, 1, &m_rs_constant_buffer);
	}

	// set shader resource views
	{
		ID3D11ShaderResourceView* SRVs[GFSDK_HAIR_NUM_SHADER_RESOUCES];
		g_hw_sdk->GetShaderResources(v.iid, SRVs);
		m_d3dctx->PSSetShaderResources(0, GFSDK_HAIR_NUM_SHADER_RESOUCES, SRVs);
	}

	// set texture 
	instanceSetTextureIntoDevice(v.iid, (hwTextureType) 0);

	// render
	auto settings = GFSDK_HairShaderSettings(true, false);
	if (g_hw_sdk->RenderHairs(v.iid, &settings) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::RenderHairs(%d) failed.\n", hi);
	}

	// render indicators
	g_hw_sdk->RenderVisualization(v.iid);
}

void hwContext::renderShadowImpl(hwHInstance hi)
{
	if (hi >= m_instances.size()) { return; }
	auto &v = m_instances[hi];

	// set shader resource views
	{
		ID3D11ShaderResourceView* SRVs[GFSDK_HAIR_NUM_SHADER_RESOUCES];
		g_hw_sdk->GetShaderResources(v.iid, SRVs);
		m_d3dctx->PSSetShaderResources(0, GFSDK_HAIR_NUM_SHADER_RESOUCES, SRVs);
	}

	auto settings = GFSDK_HairShaderSettings(false, true);
	if (g_hw_sdk->RenderHairs(v.iid, &settings) != GFSDK_HAIR_RETURN_OK)
	{
		hwLog("GFSDK_HairSDK::RenderHairs(%d) failed.\n", hi);
	}	
}

void hwContext::stepSimulationImpl(float dt, bool vrMode, bool singlePassVR)
{
	// step the simulation only once in VR
	if (vrMode)
	{
		if (singlePassVR)
		{
			if (singlePassStereoRenderPass == 0)
			{
				if (g_hw_sdk->StepSimulation(dt) != GFSDK_HAIR_RETURN_OK)
				{
					hwLog("GFSDK_HairSDK::StepSimulation(%f) failed.\n", dt);
				}
			}
		}
		else
		{
			if (m_currentVRPass == 0)
			{
				if (g_hw_sdk->StepSimulation(dt) != GFSDK_HAIR_RETURN_OK)
				{
					hwLog("GFSDK_HairSDK::StepSimulation(%f) failed.\n", dt);
				}
			}
		}
	}
	else
	{ 
		if (g_hw_sdk->StepSimulation(dt) != GFSDK_HAIR_RETURN_OK)
		{
			hwLog("GFSDK_HairSDK::StepSimulation(%f) failed.\n", dt);
		}
	}
}

bool hwContext::IsVREnabled()
{
	return m_VRRendering;
}

void hwContext::flush()
{
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_commands_back = m_commands;
		m_commands.clear();
	}

	m_d3dctx->OMSetDepthStencilState(m_rs_enable_depth, 0);

	// do not do any rendering if we're shutting down
	if (m_shuttingDown > 0 )
	{
		--m_shuttingDown;
	}
	else
	{
		for (auto& c : m_commands_back)
		{
			c();
		}
	}

	m_commands_back.clear();
}

void hwContext::flushVR()
{
	if (m_currentVRPass == 0)
	{
		std::unique_lock<std::mutex> lock(m_mutexVR);
		m_commands_backVR = m_commandsVR;
		m_commandsVR.clear();
	}

	m_d3dctx->OMSetDepthStencilState(m_rs_enable_depth, 0);

	if (m_shuttingDownVR > 0)
	{
		--m_shuttingDownVR;
	}
	else
	{
		for (auto& c : m_commands_backVR)
		{
			c();
		}
	}

	if (m_currentVRPass == 1)
	{
		m_commands_backVR.clear();
	}
	++m_currentVRPass;

	if (m_currentVRPass == 2)
		m_currentVRPass = 0;
}

void hwContext::flushVRSinglePass()
{
	{
		std::unique_lock<std::mutex> lock(m_mutexVR);
		m_commands_backVR = m_commandsVR;
		m_commandsVR.clear();
	}

	m_d3dctx->OMSetDepthStencilState(m_rs_enable_depth, 0);

	// do not do any rendering if we're shutting down
	if (m_shuttingDown > 0)
	{
		--m_shuttingDown;
	}
	else
	{
		// renders 2 eyes in one pass
		for (int cnt = 0; cnt < 2; ++cnt)
		{
			singlePassStereoRenderPass = cnt;

			if (cnt == 0)
				SetViewportLeftEye();
			else
				SetViewportRightEye();

			for (auto& c : m_commands_backVR)
			{
				c();
			}
		}
	}

	m_commands_backVR.clear();
}

//
bool hwContext::addBoneMatricesToBuffer(hwMatrix *matrix, int numMatrix, int &outStartIndex)
{
	// check if this can ever fit in the buffer
	if (numMatrix > NUM_BUFFER_BONES_MATRIX || numMatrix<=0)
		return false;

	//
	if ((boneMatrixStartIndex + numMatrix) > NUM_BUFFER_BONES_MATRIX)
		boneMatrixStartIndex = 0;

	memcpy(m_bonesMatrixBuffer + boneMatrixStartIndex, matrix, sizeof(hwMatrix) * numMatrix);
	outStartIndex = boneMatrixStartIndex;
	boneMatrixStartIndex += numMatrix;
	
	return true;
}

//
void hwContext::SetViewportLeftEye()
{
	// does not need to do anything (at least for now)
}

void hwContext::SetViewportRightEye()
{
	unsigned int numViewports = 1;
	D3D11_VIEWPORT *pVieports = new D3D11_VIEWPORT[1];
	m_d3dctx->RSGetViewports(&numViewports, pVieports);
	D3D11_VIEWPORT finalViewport;
	finalViewport.Height = pVieports[0].Height;
	finalViewport.Width = pVieports[0].Width;
	finalViewport.MaxDepth = pVieports[0].MaxDepth;
	finalViewport.MinDepth = pVieports[0].MinDepth;
	finalViewport.TopLeftX = pVieports[0].Width;
	finalViewport.TopLeftY = pVieports[0].TopLeftY;
	m_d3dctx->RSSetViewports(1, &finalViewport);
}

void hwContext::SetShuttingDownFlag()
{
	m_shuttingDown		= 2;
	m_shuttingDownVR	= 2;
	m_currentVRPass		= 0;
}

//
void hwContext::ResetRenderingPipeline()
{
}

///
bool hwContext::GetUsesReverseZ()
{
	return true;
}

void hwContext::ResetVRPass()
{
	m_currentVRPass = 0;
}



