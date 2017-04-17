#pragma once

struct hwShaderData
{
    hwHShader handle;
    int ref_count;
    ID3D11PixelShader *shader;
    std::string path;

    hwShaderData() : handle(hwNullHandle), ref_count(0), shader(nullptr) {}
    void invalidate() { ref_count = 0; shader = nullptr; path.clear(); }
    operator bool() const { return shader != nullptr; }
};

struct hwAssetData
{
    hwHAsset handle;
    int ref_count;
    hwAssetID aid;
    std::string path;
    hwConversionSettings settings;

    hwAssetData() : handle(hwNullHandle), aid(hwNullAssetID), ref_count(0) {}
    void invalidate() { ref_count = 0; aid = hwNullAssetID; path.clear(); }
    operator bool() const { return aid != hwNullAssetID; }
};

struct hwInstanceData
{
    hwHInstance handle;
    hwInstanceID iid;
    hwHAsset hasset;
    bool cast_shadow;
    bool receive_shadow;

    hwInstanceData() : handle(hwNullHandle), iid(hwNullInstanceID), hasset(hwNullHandle), cast_shadow(false), receive_shadow(false) {}
    void invalidate() { iid = hwNullInstanceID; hasset = hwNullAssetID; cast_shadow = false; receive_shadow = false; }
    operator bool() const { return iid != hwNullInstanceID; }
};

enum hwELightType
{
    hwELightType_Directional,
    hwELightType_Point,
};

struct hwLightData
{
    hwELightType type; int pad[3];
    hwFloat4 position; // w: range
    hwFloat4 direction;
    hwFloat4 color;

    hwLightData()
        : type(hwELightType_Directional)
        , position({ 0.0f, 0.0f, 0.0f, 0.0f })
        , direction({ 0.0f, 0.0f, 0.0f, 0.0f })
        , color({ 1.0f, 1.0f, 1.0f, 1.0 })
    {}
};

struct hwConstantBuffer
{
    int num_lights; int pad0[3];
    hwLightData lights[hwMaxLights];
    GFSDK_HairShaderConstantBuffer hw;

    hwConstantBuffer() : num_lights(0) {}
};



class hwContext
{
public:
    static hwSDK* loadSDK();
    static void   unloadSDK();


public:
    hwContext();
    ~hwContext();
    bool valid() const;

    bool initialize(hwDevice *d3d_device);
    void finalize();
    void move(hwContext &from);

    hwHShader       shaderLoadFromFile(const std::string &path);
    void            shaderRelease(hwHShader hs);
    void            shaderReload(hwHShader hs);

    hwHAsset        assetLoadFromFile(const std::string &path, const hwConversionSettings *conv);
    void            assetRelease(hwHAsset ha);
    void            assetReload(hwHAsset ha);
    int             assetGetNumBones(hwHAsset ha) const;
    const char*     assetGetBoneName(hwHAsset ha, int nth) const;
    void            assetGetBoneIndices(hwHAsset ha, hwFloat4 &o_indices) const;
    void            assetGetBoneWeights(hwHAsset ha, hwFloat4 &o_weight) const;
    void            assetGetBindPose(hwHAsset ha, int nth, hwMatrix &o_mat);
    void            assetGetDefaultDescriptor(hwHAsset ha, hwHairDescriptor &o_desc) const;


	// New - WayGate
	const char*     assetGetTextureName(hwHAsset ha, int textureType) const;
	void EnableVRRendering(bool enabled);
	void SetShuttingDownFlag();


    hwHInstance     instanceCreate(hwHAsset ha);
    void            instanceRelease(hwHInstance hi);
    void            instanceGetBounds(hwHInstance hi, hwFloat3 &o_min, hwFloat3 &o_max) const;
    void            instanceGetDescriptor(hwHInstance hi, hwHairDescriptor &desc) const;
    void            instanceSetDescriptor(hwHInstance hi, const hwHairDescriptor &desc);
    void            instanceSetTexture(hwHInstance hi, hwTextureType type, hwTexture *tex);
	void			instanceSetTextureIntoDevice(hwHInstance hi, hwTextureType type);
    void            instanceUpdateSkinningMatrices(hwHInstance hi, int num_bones, hwMatrix *matrices);
	void			instanceUpdateSkinningMatricesAsync(hwHInstance hi, int num_bones, hwMatrix *matrices, bool vrMode);
    void            instanceUpdateSkinningDQs(hwHInstance hi, int num_bones, hwDQuaternion *dqs);
	

    void beginScene(bool vrMode);
    void endScene(bool vrMode);
    void setViewProjection(const hwMatrix &view, const hwMatrix &proj, float fov);
	void setViewProjectionStereo(const hwMatrix &view, const hwMatrix &proj, const hwMatrix &view2, const hwMatrix &proj2, float fov, bool singlePassStereo);
    void setRenderTarget(hwTexture *framebuffer, hwTexture *depthbuffer, bool vrMode);
    void setShader(hwHShader hs, bool vrMode);
    void setLights(int num_lights, const hwLightData *lights, bool vrMode);
	void render(hwHInstance hi, bool vrMode);
    void renderShadow(hwHInstance hi, bool vrMode);
    void stepSimulation(float dt, bool vrMode, bool singlePassVR);
    void flush();
	void flushVR();
	void flushVRSinglePass();
	void ResetVRPass();

private:
    hwShaderData&   newShaderData();
    hwAssetData&    newAssetData();
    hwInstanceData& newInstanceData();

    typedef std::function<void()> DeferredCall;
	void pushDeferredCall(const DeferredCall &c, bool useVRQueue = false);
    void setViewProjectionImpl(const hwMatrix &view, const hwMatrix &proj, float fov);
	void setViewProjectionStereoImpl(const hwMatrix &view, const hwMatrix &proj, const hwMatrix &view2, const hwMatrix &proj2, float fov, bool singlePassStereo);
    void setRenderTargetImpl(hwTexture *framebuffer, hwTexture *depthbuffer);
    void setShaderImpl(hwHShader hs);
    void setLightsImpl(int num_lights, const hwLightData *lights);
    void renderImpl(hwHInstance hi);
    void renderShadowImpl(hwHInstance hi);
    void stepSimulationImpl(float dt, bool vrMode, bool singlePassVR);
	void getSamplerLinear(ID3D11Device* pd3dDevice , ID3D11SamplerState** ppTexSampler);
    hwSRV* getSRV(hwTexture *tex);
    hwRTV* getRTV(hwTexture *tex);

	// new stuff WayGate
	void InitVRVariables();
	bool GetUsesReverseZ();
	bool IsVREnabled();
	void StoreMatrixLocally(const hwMatrix &view, const hwMatrix &proj, float fov, int eyeSlot = 0);

	void SetViewportLeftEye();
	void SetViewportRightEye();
	void ResetRenderingPipeline();
	bool addBoneMatricesToBuffer(hwMatrix *matrix, int numMatrix, int &outStartIndex);
	void instanceUpdateSkinningMatricesAsyncImpl(hwInstanceID instance, int matrixIndex, int numMatrix);

	int  m_currentVRPass;
	bool m_VRRendering;

	hwMatrix m_view0;
	hwMatrix m_proj0;
	float fov0;

	hwMatrix m_view1;
	hwMatrix m_proj1;
	float fov1;

	int m_shuttingDown;
	int m_shuttingDownVR;

	int singlePassStereoRenderPass;

	// skinning matrix ring buffer
	static const  int NUM_BUFFER_BONES_MATRIX = 1000;
	hwMatrix m_bonesMatrixBuffer[NUM_BUFFER_BONES_MATRIX];
	int boneMatrixStartIndex	= 0;

	// End new stuff from WayGate 

private:
    typedef std::vector<hwShaderData>       ShaderCont;
    typedef std::vector<hwAssetData>        AssetCont;
    typedef std::vector<hwInstanceData>     InstanceCont;
    typedef std::map<hwTexture*, hwSRV*>    SRVTable;
    typedef std::map<hwTexture*, hwRTV*>    RTVTable;
    typedef std::vector<DeferredCall>       DeferredCalls;

    std::mutex              m_mutex;
	std::mutex              m_mutexVR;

    ID3D11Device            *m_d3ddev = nullptr;
    ID3D11DeviceContext     *m_d3dctx = nullptr;
    ShaderCont              m_shaders;
    AssetCont               m_assets;
    InstanceCont            m_instances;
    SRVTable                m_srvtable;
    RTVTable                m_rtvtable;
    DeferredCalls           m_commands;
    DeferredCalls           m_commands_back;

	DeferredCalls           m_commandsVR;
	DeferredCalls           m_commands_backVR;

    ID3D11DepthStencilState *m_rs_enable_depth = nullptr;
    ID3D11Buffer            *m_rs_constant_buffer = nullptr;

    hwConstantBuffer        m_cb;

	// New Stuff from Carlo
	ID3D11Buffer* m_VB; 
	ID3D11Buffer* m_CB; 
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;
	ID3D11InputLayout* m_InputLayout;
	ID3D11RasterizerState* m_RasterState;
	ID3D11BlendState* m_BlendState;
	ID3D11DepthStencilState* m_DepthState;
	ID3D11SamplerState *m_HairShaderSampler;
	// End new stuff from Carlo 

};
