#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "GameInput.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "BufferManager.h"

//Demo Application Includes
#include "Camera.h"
#include "CameraController.h"
#include "Model.h"
#include "PostEffects.h"

//Include Shaders
#include "CompiledShaders/ForwardVS.h"
#include "CompiledShaders/ForwardPS.h"
#include "CompiledShaders/GBufferVS.h"
#include "CompiledShaders/GBufferPS.h"
#include "CompiledShaders/SampleGenerationVS.h"
#include "CompiledShaders/SampleGenerationPS.h"
#include "CompiledShaders/SampleVisualizationVS.h"
#include "CompiledShaders/SampleVisualizationGS.h"
#include "CompiledShaders/SampleVisualizationPS.h"
#include "CompiledShaders/Hit.h"
#include "CompiledShaders/Miss.h"
#include "CompiledShaders/RayGeneration.h"
#include "CompiledShaders/AccumulationPass.h"
#include "CompiledShaders/WorldHashTable.h"
#include "CompiledShaders/FinalVisualizationPass.h"
#include "CompiledShaders/ClearBuffersPass.h"

//Include Core Engine Shaders - MinMax Technique
#include "../Core/CompiledShaders/BlurCS.h"

//Ray Tracing Includes
#include "DescriptorHeapStack.h"
#include "RayTracingDispatchRayInputs.h"
#include "ShaderHelpers.h"
#include "AccelerationStructureBuffer.h"

//Disable define of max (collides with std::max)
#undef max

using namespace GameCore;
using namespace Graphics;

__declspec(align(16)) struct SharedConstants
{
	Matrix4 worldViewProjectionMat;
	Matrix4 viewProjectionInverseMat;
	Matrix4 viewInverseMat;
	Matrix4 worldMat;
	Vector4 viewVector;
	uint32_t voxelConnectivity;
	uint32_t level;
    uint32_t maxLevels;
	uint32_t cellSize;
    uint32_t localizedLOD;
};

__declspec(align(16)) struct RayTracingConstants
{
	Matrix4 viewProjectionInverseMat;
	uint32_t frameCount;
};

__declspec(align(16)) struct PointSampleData
{
	uint32_t seed;
    uint32_t prevSeed;
	float x;
	float y;
	float z;
};

__declspec(align(16)) struct HashTableData
{
	uint32_t key;
	uint32_t value;
	uint32_t count;
};

__declspec(align(16)) struct HashTableConstants
{
	uint32_t worldHashTableElementCount;
	uint32_t accumulationHashTableElementCount;
	uint32_t worldHashTableValueFractionalBits;
	uint32_t worldHashTableCountFractionalBits;
	uint32_t accumulationHashTableValueFractionalBits;
};

class ImplicitPointDemo : public GameCore::IGameApp
{
public:

    ImplicitPointDemo()
    {}

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:
    //--- GENERAL RENDERING MEMBERS ---
    RootSignature m_RootSignature;

	//--- MESH RENDERING MEMBERS ---
	GraphicsPSO m_MeshPSO;

    //--- FILL GBUFFER MEMBERS ---
    DepthBuffer m_DepthBuffer;
    ColorBuffer m_WorldNormalBuffer;
    ColorBuffer m_WorldTangentBuffer;
    ColorBuffer m_WorldBitangentBuffer;
    GraphicsPSO m_GBufferPSO;

    //--- FULLSCREEN SAMPLE GENERATION MEMBERS ---
    uint32_t m_VoxelConnectivity = 26; //Either: 0, 7, 18 or 26 
    uint32_t m_MaxLevels = 10;
    uint32_t m_Level = 10; //[0, m_MaxLevels]
    uint32_t m_CellSize = 2560;

    StructuredBuffer m_FullscreenVertexBuffer;
    ByteAddressBuffer m_FullscreenIndexBuffer;
    StructuredBuffer m_SampleGenerationBuffer;
    GraphicsPSO m_SampleGenerationPSO;

    //--- FULLSCREEN SAMPLE VISUALIZATION MEMBERS ---
    GraphicsPSO m_SampleVisualizationPSO;

    //--- HASHTABLE MEMBERS ---
    const HashTableConstants m_HashTableConstants =
    {
        8388608, //World Hash Table Element Count -> 8388608 * 12 bytes = +- 100Mb pre-allocated video memory
        2073600, //Accumulation Hash Table Element Count = 1920 * 1080 pixels
        31,      //World Hash Table - Amount bits Fractional Part "Value"
        11,      //World Hash Table - Amount bits Fractional Part "Count"
        11       //Accumulation Hash Table - Amount bits Fractional Part "Value"
    };

    //--- ACCUMULATION MEMBERS ---
    StructuredBuffer m_AccumulationHashTable; //Accumulation Buffer
    RootSignature m_AccumulationRootSignature;
    ComputePSO m_AccumulationPSO;

    //--- WORLD HASH TABLE MEMBERS ---
    StructuredBuffer m_WorldHashTable; //Permanent World Buffer
    RootSignature m_WorldHashTableRootSignature;
    ComputePSO m_WorldHashTablePSO;

    //--- FINAL VISUALIZATION MEMBERS ---
    ColorBuffer m_FinalVisualizationBuffer;
    RootSignature m_FinalVisualizationRootSignature;
    ComputePSO m_FinalVisualizationPSO;

    //--- CLEAR ACCUMULATION MEMBERS ---
    RootSignature m_ClearBuffersRootSignature;
    ComputePSO m_ClearBuffersPSO;

    //--- RAY TRACING MEMBERS ---
    uint32_t m_FrameCount = 0;
    CComPtr<ID3D12Device5> m_pRaytracingDevice;
    CComPtr<ID3D12StateObject> m_pRayTracingPSO;
    CComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;
    std::vector<CComPtr<ID3D12Resource>> m_BottomLevelAccelerationStructures = {};

    std::unique_ptr<DescriptorHeapStack> m_pRaytracingDescriptorHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_AmbientOcclusionOutputGPUHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_SRVTableGPUHandle;

    ColorBuffer m_AmbientOcclusionOutputBuffer;
    AccelerationStructureBuffer m_AccelerationStructureBuffer;
    ByteAddressBuffer m_DynamicConstantBuffer;

    RootSignature m_RayTracingGlobalRootSignature;
    RootSignature m_RayTracingLocalRootSignature;

    LPCWSTR const m_ExportNameRayGenerationShader = L"RayGen";
    LPCWSTR const m_ExportNameClosestHitShader = L"Hit";
    LPCWSTR const m_ExportNameMissShader = L"Miss";
    LPCWSTR const m_ExportNameHitGroup = L"HitGroup";
    RayTracingDispatchRayInputs m_RayTracingInputs;

    //--- LOCALIZED LOD MEMBERS ---
    ColorBuffer m_BlurredAOBuffer;
    StructuredBuffer m_LODBuffer;
    RootSignature m_GaussianBlurRootSignature;
    ComputePSO m_GaussianBlurPSO;

	//--- DEMO SPECIFIC MEMBERS ---
	Camera m_MainCamera;
	std::auto_ptr<CameraController> m_CameraController;
	Model m_SceneModel;

    //--- BOOLEANS FOR DEBUGGING & TESTING
    enum class LODMode : uint32_t
    {
        FixedLOD    = 0,
        DistanceLOD = 1,
        MinMaxLOD   = 2
    };
    bool m_SSAOEnabled = true;
    bool m_VisualizeSamples = false;
    bool m_StopAccumulating = false;
    bool m_VisualizeFinalPass = true;
    bool m_VisualizeRayTracing = false;
    bool m_TechniqueEnabled = true;
    LODMode m_LODMode = LODMode::FixedLOD;

    //--- PRIVATE FUNCTIONS ---
    void InitializeRayTracing();
    void CreateAccelerationStructures();
    void CreateRayTracingPipelineStateObject();
    void CreateShaderBindingTable();
    void CreateShaderViews();

    void SetDefaultRenderingPipeline(GraphicsContext& gfxContext);
    void DrawScene(GraphicsContext& gfxContext);
    void ForwardRenderScenePass(GraphicsContext& gfxContext);
    void GBufferPass(GraphicsContext& gfxContext);
    void RayTracingPass(GraphicsContext& gfxContext);
    void AOGaussianBlurPass(GraphicsContext& gfxContext);
    void SampleGenerationPass(GraphicsContext& gfxContext);
    void SampleVisualizationPass(GraphicsContext& gfxContext);
    void AccumulationPass(GraphicsContext& gfxContext);
    void WorldHashTablePass(GraphicsContext& gfxContext);
    void FinalVisualizationPass(GraphicsContext& gfxContext);
    void ClearBuffersPass(GraphicsContext& gfxContext);
};

CREATE_APPLICATION( ImplicitPointDemo )

void ImplicitPointDemo::Startup( void )
{
    //---- DEMO SETUP ----
    //General Camera setup
    m_MainCamera.SetZRange(1.0f, 10000.f);

#define BistroInterior
#if defined(BistroInterior)
    //Default camera settings
    m_MainCamera.SetPosition(Vector3(-11.805f, 298.791f, -115.986f));
    m_MainCamera.SetLookDirection(Vector3(0.937597f, -0.186109f, -0.293727f), Vector3(0.177598f, 0.982529f, -0.0556372f));

    //Close-up cabin
    //m_MainCamera.SetPosition(Vector3(1002.4f, 225.482f, -145.828f));
    //m_MainCamera.SetLookDirection(Vector3(0.469577f, -0.205169f, -0.858721f), Vector3(0.0984369f, 0.978727f, -0.180013f));

    ASSERT(m_SceneModel.Load("Models/bistro-interior.h3d"), "Failed to load scene models!");
    ASSERT(m_SceneModel.m_Header.meshCount > 0, "Scene model doesn't contain meshes!");
    m_MaxLevels = 9;
#elif defined(Dragon)
	m_MainCamera.SetPosition(Vector3(-141.809f, 282.313f, 256.845f));
	m_MainCamera.SetLookDirection(Vector3(0.37324f, -0.593874f, -0.712745f), Vector3(0.275502f, 0.804558f, -0.526104f));
	ASSERT(m_SceneModel.Load("Models/dragon.h3d"), "Failed to load scene models!");
	ASSERT(m_SceneModel.m_Header.meshCount > 0, "Scene model doesn't contain meshes!");
#elif defined(Hairball)
	m_MainCamera.SetPosition(Vector3(-8.37427f, 150.595f, 133.763f));
	m_MainCamera.SetLookDirection(Vector3(0.0445527f, -0.633293f, -0.772629f), Vector3(0.0364575f, 0.773913f, -0.632242f));
	ASSERT(m_SceneModel.Load("Models/hairball.h3d"), "Failed to load scene models!");
	ASSERT(m_SceneModel.m_Header.meshCount > 0, "Scene model doesn't contain meshes!");
#elif defined(BistroExterior)
	//m_MainCamera.SetPosition(Vector3(-1675.45f, 491.903f, -495.006f));
	//m_MainCamera.SetLookDirection(Vector3(0.938374f, -0.230119f, 0.257876f), Vector3(0.221893f, 0.973163f, 0.0609787f));

	//m_MainCamera.SetPosition(Vector3(-890.747f, 314.691f, 692.823f));
	//m_MainCamera.SetLookDirection(Vector3(0.836575f, -0.0462768f, -0.545603f), Vector3(0.0387644f, 0.998929f, -0.0252759f));

    //Close-up cycle
	m_MainCamera.SetPosition(Vector3(-680.566f, 110.841f, 106.817f));
	m_MainCamera.SetLookDirection(Vector3(0.974446f, -0.103681f, -0.199261f), Vector3(0.101579f, 0.994611f, -0.0207715f));

	ASSERT(m_SceneModel.Load("Models/bistro-exterior.h3d"), "Failed to load scene models!");
	ASSERT(m_SceneModel.m_Header.meshCount > 0, "Scene model doesn't contain meshes!");
    m_MaxLevels = 11;
#else
    exit(-1);
#endif

    //Make and reset camera controller
    m_CameraController.reset(new CameraController(m_MainCamera, Vector3(kYUnitVector)));

    //--- SETUP ROOT SIGNATURE ---
    m_RootSignature.Reset(3, 0);
    m_RootSignature[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL);
    m_RootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 5, D3D12_SHADER_VISIBILITY_PIXEL); //Depth + Normal Buffer + Tangent + Bitangent + AO Buffer
    m_RootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 2); //PointSampleBuffer + LODBuffer
    m_RootSignature.Finalize(L"ImplicitPointDemo_RasterizationRootSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    //--- SETUP MESH DRAWING PIPELINE STATE OBJECT ---
	D3D12_INPUT_ELEMENT_DESC const vertexMeshLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	}; //Mesh format has this vertex layout, so cannot change if using H3D mesh format
    m_MeshPSO.SetRootSignature(m_RootSignature);
    m_MeshPSO.SetRasterizerState(RasterizerDefault);
    m_MeshPSO.SetBlendState(BlendDisable);
    m_MeshPSO.SetDepthStencilState(DepthStateReadWrite);
    m_MeshPSO.SetInputLayout(_countof(vertexMeshLayout), vertexMeshLayout);
    m_MeshPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_MeshPSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
    m_MeshPSO.SetVertexShader(g_pForwardVS, sizeof(g_pForwardVS));
    m_MeshPSO.SetPixelShader(g_pForwardPS, sizeof(g_pForwardPS));
    m_MeshPSO.Finalize();

    //--- SETUP GBUFFER FILL PSO ---
    uint32_t const widthBuffer = g_SceneColorBuffer.GetWidth();
    uint32_t const heightBuffer = g_SceneColorBuffer.GetHeight();
    m_WorldNormalBuffer.Create(L"World Normal Buffer", widthBuffer, heightBuffer, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    m_WorldTangentBuffer.Create(L"World Tangent Buffer", widthBuffer, heightBuffer, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    m_WorldBitangentBuffer.Create(L"World Bitangent Buffer", widthBuffer, heightBuffer, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
    m_DepthBuffer.Create(L"Depth Buffer", widthBuffer, heightBuffer, DXGI_FORMAT_D32_FLOAT);

	m_GBufferPSO.SetRootSignature(m_RootSignature);
	m_GBufferPSO.SetRasterizerState(RasterizerDefault);
	m_GBufferPSO.SetBlendState(BlendDisable);
	m_GBufferPSO.SetDepthStencilState(DepthStateReadWrite);
	m_GBufferPSO.SetInputLayout(_countof(vertexMeshLayout), vertexMeshLayout);
	m_GBufferPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    DXGI_FORMAT rtFormats[3] = { m_WorldNormalBuffer.GetFormat(), m_WorldTangentBuffer.GetFormat(), m_WorldBitangentBuffer.GetFormat() };
    m_GBufferPSO.SetRenderTargetFormats(3, rtFormats, m_DepthBuffer.GetFormat());
	m_GBufferPSO.SetVertexShader(g_pGBufferVS, sizeof(g_pGBufferVS));
	m_GBufferPSO.SetPixelShader(g_pGBufferPS, sizeof(g_pGBufferPS));
	m_GBufferPSO.Finalize();

    //--- SETUP FULLSCREEN SAMPLE GENERATION
	D3D12_INPUT_ELEMENT_DESC const vertexSampleGenerationLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
    m_SampleGenerationPSO.SetRootSignature(m_RootSignature);
	m_SampleGenerationPSO.SetRasterizerState(RasterizerDefault);
	m_SampleGenerationPSO.SetBlendState(BlendDisable);
	m_SampleGenerationPSO.SetDepthStencilState(DepthStateDisabled);
	m_SampleGenerationPSO.SetInputLayout(_countof(vertexSampleGenerationLayout), vertexSampleGenerationLayout);
	m_SampleGenerationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_SampleGenerationPSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
	m_SampleGenerationPSO.SetVertexShader(g_pSampleGenerationVS, sizeof(g_pSampleGenerationVS));
	m_SampleGenerationPSO.SetPixelShader(g_pSampleGenerationPS, sizeof(g_pSampleGenerationPS));
	m_SampleGenerationPSO.Finalize();

	std::vector<Math::Vector3> const vertices = {
	    Math::Vector3(-1.f, 1.f, 0.f),
	    Math::Vector3(-1.f, -1.f, 0.f),
	    Math::Vector3(1.f, -1.f, 0.f),
	    Math::Vector3(1.f, 1.f, 0.f)};
	std::vector<uint16_t> const indices = { 0,1,2,0,2,3 };
	m_FullscreenVertexBuffer.Create(L"FullScreenVertexBuffer", (uint32_t)vertices.size(), sizeof(Math::Vector3), vertices.data());
	m_FullscreenIndexBuffer.Create(L"FullScreenIndexBuffer", (uint32_t)indices.size(), sizeof(uint16_t), indices.data());

    uint32_t const sampleElementSize = 20; //sizeof(PointSampleData) -> without padding
    uint32_t const sampleNumElements = m_DepthBuffer.GetWidth() * m_DepthBuffer.GetHeight();
    m_SampleGenerationBuffer.Create(L"SampleBuffer", sampleNumElements, sampleElementSize);
    m_LODBuffer.Create(L"LOD Buffer", sampleNumElements, sizeof(uint32_t) * 2); //Stores uint2

    //--- SETUP POINT VISUALIZATION ---
	D3D12_INPUT_ELEMENT_DESC const vertexPointVisualizationLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	}; //for VertexID hack!
    m_SampleVisualizationPSO.SetRootSignature(m_RootSignature);
    m_SampleVisualizationPSO.SetRasterizerState(RasterizerDefault);
    m_SampleVisualizationPSO.SetBlendState(BlendDisable);
    m_SampleVisualizationPSO.SetDepthStencilState(DepthStateDisabled); //DepthStateReadWrite
    m_SampleVisualizationPSO.SetInputLayout(_countof(vertexPointVisualizationLayout), vertexPointVisualizationLayout);
    m_SampleVisualizationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
    m_SampleVisualizationPSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
    m_SampleVisualizationPSO.SetVertexShader(g_pSampleVisualizationVS, sizeof(g_pSampleVisualizationVS));
    m_SampleVisualizationPSO.SetGeometryShader(g_pSampleVisualizationGS, sizeof(g_pSampleVisualizationGS));
    m_SampleVisualizationPSO.SetPixelShader(g_pSampleVisualizationPS, sizeof(g_pSampleVisualizationPS));
    m_SampleVisualizationPSO.Finalize();

    //--- ACCUMULATION ---
    m_AccumulationRootSignature.Reset(3, 0);
    m_AccumulationRootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, D3D12_SHADER_VISIBILITY_ALL); //AO + Samples
    m_AccumulationRootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, D3D12_SHADER_VISIBILITY_ALL); //Accumulation 2D HashTable
    m_AccumulationRootSignature[2].InitAsConstantBuffer(0);
    m_AccumulationRootSignature.Finalize(L"AccumulationRootSignature", D3D12_ROOT_SIGNATURE_FLAG_NONE);

    uint32_t const hashtableElementSize = 12; //sizeof(HashTableData)
    m_AccumulationHashTable.Create(L"HashTable", m_HashTableConstants.accumulationHashTableElementCount, hashtableElementSize);

    m_AccumulationPSO.SetRootSignature(m_AccumulationRootSignature);
    m_AccumulationPSO.SetComputeShader(g_pAccumulationPass, sizeof(g_pAccumulationPass));
    m_AccumulationPSO.Finalize();

    //--- WORLD HASH TABLE ---
	m_WorldHashTableRootSignature.Reset(3, 0);
    m_WorldHashTableRootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2, D3D12_SHADER_VISIBILITY_ALL); //Samples + Accumulation 2D HashTable
    m_WorldHashTableRootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1); //World HashTable
    m_WorldHashTableRootSignature[2].InitAsConstantBuffer(0);
    m_WorldHashTableRootSignature.Finalize(L"WorldHashTableRootSignature", D3D12_ROOT_SIGNATURE_FLAG_NONE);

	m_WorldHashTable.Create(L"HashTable", m_HashTableConstants.worldHashTableElementCount, hashtableElementSize);

	m_WorldHashTablePSO.SetRootSignature(m_WorldHashTableRootSignature);
    m_WorldHashTablePSO.SetComputeShader(g_pWorldHashTable, sizeof(g_pWorldHashTable));
    m_WorldHashTablePSO.Finalize();

    //--- FINAL VISUALIZATION ---
    m_FinalVisualizationRootSignature.Reset(6, 0);
    m_FinalVisualizationRootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4); //Samples + World HashTable + LOD Buffer + Depth Buffer
    m_FinalVisualizationRootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1); //Output Buffer 
    m_FinalVisualizationRootSignature[2].InitAsConstantBuffer(0);
    m_FinalVisualizationRootSignature[3].InitAsConstantBuffer(1);
    m_FinalVisualizationRootSignature[4].InitAsConstantBuffer(2);
    m_FinalVisualizationRootSignature[5].InitAsConstantBuffer(3);
    m_FinalVisualizationRootSignature.Finalize(L"FinalVisualizationRootSignature");

    m_FinalVisualizationBuffer.Create(L"FinalVisualizationBuffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 1, g_SceneColorBuffer.GetFormat());

    m_FinalVisualizationPSO.SetRootSignature(m_FinalVisualizationRootSignature);
    m_FinalVisualizationPSO.SetComputeShader(g_pFinalVisualizationPass, sizeof(g_pFinalVisualizationPass));
    m_FinalVisualizationPSO.Finalize();

    //--- CLEAR BUFFERS ---
    m_ClearBuffersRootSignature.Reset(2, 0);
    m_ClearBuffersRootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2); //Accumulation Buffer + LOD Buffer
    m_ClearBuffersRootSignature[1].InitAsConstantBuffer(0);
    m_ClearBuffersRootSignature.Finalize(L"ClearBuffersRootSignature");

    m_ClearBuffersPSO.SetRootSignature(m_ClearBuffersRootSignature);
    m_ClearBuffersPSO.SetComputeShader(g_pClearBuffersPass, sizeof(g_pClearBuffersPass));
    m_ClearBuffersPSO.Finalize();

	//--- RAY TRACING SETUP ---
	InitializeRayTracing();

    //--- LOCALIZED LOD ---
    m_GaussianBlurRootSignature.Reset(4, 2);
    m_GaussianBlurRootSignature.InitStaticSampler(0, SamplerLinearClampDesc);
    m_GaussianBlurRootSignature.InitStaticSampler(1, SamplerLinearBorderDesc);
    m_GaussianBlurRootSignature[0].InitAsConstants(0, 4);
    m_GaussianBlurRootSignature[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4);
    m_GaussianBlurRootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 4);
    m_GaussianBlurRootSignature[3].InitAsConstantBuffer(1);
    m_GaussianBlurRootSignature.Finalize(L"GaussianBlurRootSignature", D3D12_ROOT_SIGNATURE_FLAG_NONE);

	m_BlurredAOBuffer.Create(L"BlurredAOBuffer", m_AmbientOcclusionOutputBuffer.GetWidth(), m_AmbientOcclusionOutputBuffer.GetHeight(),
		1, m_AmbientOcclusionOutputBuffer.GetFormat());

	m_GaussianBlurPSO.SetRootSignature(m_GaussianBlurRootSignature);
    m_GaussianBlurPSO.SetComputeShader(g_pBlurCS, sizeof(g_pBlurCS));
    m_GaussianBlurPSO.Finalize();
}

void ImplicitPointDemo::Cleanup( void )
{
	//--- MESH DRAWING ---
	m_MeshPSO.DestroyAll();

    //--- LOCALIZED LOD ---
    m_GaussianBlurPSO.DestroyAll();
    m_GaussianBlurRootSignature.DestroyAll();
    m_BlurredAOBuffer.Destroy();

    //--- RAY TRACING ---
    for (auto& blas : m_BottomLevelAccelerationStructures)
        blas.Release();
    m_BottomLevelAccelerationStructures.clear();
    m_TopLevelAccelerationStructure.Release();
    m_AccelerationStructureBuffer.Destroy();
    m_AmbientOcclusionOutputBuffer.Destroy();
    m_DynamicConstantBuffer.Destroy();
    m_pRayTracingPSO.Release();
    m_pRaytracingDescriptorHeap.reset();
    m_RayTracingGlobalRootSignature.DestroyAll();
    m_RayTracingLocalRootSignature.DestroyAll();
    m_pRaytracingDevice.Release();

    //--- ACCUMULATION ---
    m_AccumulationPSO.DestroyAll();
    m_AccumulationRootSignature.DestroyAll();
    m_AccumulationHashTable.Destroy();

    //--- WORLD HASH TABLE ---
    m_WorldHashTablePSO.DestroyAll();
    m_WorldHashTableRootSignature.DestroyAll();
    m_WorldHashTable.Destroy();

    //--- FINAL VISUALIZATION ---
    m_FinalVisualizationPSO.DestroyAll();
    m_FinalVisualizationRootSignature.DestroyAll();
    m_FinalVisualizationBuffer.Destroy();

    //--- CLEAR ACCUMULATION ---
    m_ClearBuffersPSO.DestroyAll();
    m_ClearBuffersRootSignature.DestroyAll();

    //--- GBUFFER ---
    m_GBufferPSO.DestroyAll();
    m_WorldNormalBuffer.Destroy();
    m_WorldTangentBuffer.Destroy();
    m_WorldBitangentBuffer.Destroy();
    m_DepthBuffer.Destroy();

    //--- FULLSCREEN POINT GENERATION ---
    m_SampleGenerationPSO.DestroyAll();
    m_SampleGenerationBuffer.Destroy();
    m_LODBuffer.Destroy();
    m_FullscreenIndexBuffer.Destroy();
    m_FullscreenVertexBuffer.Destroy();

    //--- SAMPLE VISUALIZATION ---
    m_SampleVisualizationPSO.DestroyAll();

    //--- SHARED ---
    m_SceneModel.Clear();
    m_RootSignature.DestroyAll();

    //--- DEMO ---
    m_CameraController.reset();
}

void ImplicitPointDemo::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    //Update Camera's
    m_CameraController->Update(deltaT);

    //Update Debug Drawing
    if (GameInput::IsFirstReleased(GameInput::kKey_t))
        m_VisualizeSamples = !m_VisualizeSamples;

    //Enable/Disable RayTracing
    if (GameInput::IsFirstReleased(GameInput::kKey_r))
        m_VisualizeRayTracing = !m_VisualizeRayTracing;

    //Enable/Disable Final Visualization
    if (GameInput::IsFirstReleased(GameInput::kKey_y))
        m_VisualizeFinalPass = !m_VisualizeFinalPass;

    //Debug - Enable/Disable Accumulation Pass
    if (GameInput::IsFirstReleased(GameInput::kKey_u))
        m_StopAccumulating = !m_StopAccumulating;

    //Enable/Disable Entire Technique
    if (GameInput::IsFirstReleased(GameInput::kKey_p))
        m_TechniqueEnabled = !m_TechniqueEnabled;
}

void ImplicitPointDemo::RenderScene( void )
{
	//Disable Post Processing Effects
	PostEffects::EnableHDR = false;
	PostEffects::EnableAdaptation = false;
	PostEffects::BloomEnable = false;

    //Begin Graphics Context
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    //Render Passes
    SetDefaultRenderingPipeline(gfxContext);
    ForwardRenderScenePass(gfxContext);
    if (m_TechniqueEnabled)
    {
        GBufferPass(gfxContext);
        RayTracingPass(gfxContext);
        if (m_LODMode == LODMode::MinMaxLOD)
            AOGaussianBlurPass(gfxContext);
        SetDefaultRenderingPipeline(gfxContext); //Reset to default due to RT context!
		SampleGenerationPass(gfxContext);
		if (m_VisualizeSamples)
			SampleVisualizationPass(gfxContext);
        if (!m_StopAccumulating)
        {
            AccumulationPass(gfxContext);
            WorldHashTablePass(gfxContext);
        }
        if (m_VisualizeFinalPass)
            FinalVisualizationPass(gfxContext);

        if (!m_StopAccumulating)
            ClearBuffersPass(gfxContext);
    }

    //--- END ---
    gfxContext.Finish();
}

void ImplicitPointDemo::InitializeRayTracing()
{
    //DXR Functional Specs: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
    //Get ID3D12Device5 interface - Necessary for Ray Tracing support
    HRESULT result = g_Device->QueryInterface(IID_PPV_ARGS(&m_pRaytracingDevice));
    if (result != S_OK)
        return;

    //Create Descriptor Heap - Using custom class as some functionality is lacking in GPUBuffer
    uint32_t const numDescriptors = 200; //TODO: optimize to more precise number?
    uint32_t const nodeMask = 0;
    m_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(
        new DescriptorHeapStack(*g_Device, numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nodeMask));

    //Create Buffers
    m_AmbientOcclusionOutputBuffer.Create(L"AO Buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 1, g_SceneColorBuffer.GetFormat());
    m_DynamicConstantBuffer.Create(L"RT Dynamic Constant Buffer", 1, sizeof(RayTracingConstants));

    //Create Root Signatures - Both Local and Global are needed.
    m_RayTracingGlobalRootSignature.Reset(4, 0);
    m_RayTracingGlobalRootSignature[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1); //Output
    m_RayTracingGlobalRootSignature[1].InitAsBufferSRV(0); // BVH
    m_RayTracingGlobalRootSignature[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); //Depth & World Normal
    m_RayTracingGlobalRootSignature[3].InitAsConstantBuffer(0); //Constant Buffer
    m_RayTracingGlobalRootSignature.Finalize(L"ImplicitPointDemo_RayTracingGlobalRootSignature", D3D12_ROOT_SIGNATURE_FLAG_NONE);

    m_RayTracingLocalRootSignature.Reset(0, 0);
    m_RayTracingLocalRootSignature.Finalize(L"ImplicitPointDemo_RayTracingLocalRootSignature", D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

    CreateRayTracingPipelineStateObject();
    CreateAccelerationStructures();
    CreateShaderBindingTable();
    CreateShaderViews();
}

void ImplicitPointDemo::CreateAccelerationStructures()
{
    uint32_t const numBottomLevels = 1;
    uint64_t scratchBufferSizeNeeded = 0; //Either determined by TLAS or BLAS - biggest value needed

#pragma region TLAS
    //Only one TLAS structure
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelAccelerationStructureDesc.Inputs;
    topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelInputs.NumDescs = numBottomLevels; //Hard coded, but if we support instances for dynamic update, a valid/up-to-date number needs to be pushed...
    topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; //Adjust this flag later to support dynamic update!
    topLevelInputs.pGeometryDescs = nullptr;
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    m_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
    scratchBufferSizeNeeded = std::max(scratchBufferSizeNeeded, topLevelPrebuildInfo.ScratchDataSizeInBytes);
#pragma endregion

#pragma region GEOMETRY
    //Multiple geometry descriptors based on amount of meshes
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(m_SceneModel.m_Header.meshCount);
    for (uint32_t i = 0; i < m_SceneModel.m_Header.meshCount; ++i)
    {
        const Model::Mesh& pMesh = m_SceneModel.m_pMesh[i];

        D3D12_RAYTRACING_GEOMETRY_DESC& desc = geometryDescs[i];
        desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangleDesc = desc.Triangles;
        triangleDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; //POSITION attribute format
        triangleDesc.VertexCount = pMesh.vertexCount;
        triangleDesc.VertexBuffer.StartAddress = m_SceneModel.m_VertexBuffer->GetGPUVirtualAddress()
            + (pMesh.vertexDataByteOffset + pMesh.attrib[Model::attrib_position].offset); //Start + (offset one vertex + offset of position attribute (0 in our case)
        triangleDesc.VertexBuffer.StrideInBytes = pMesh.vertexStride;
        triangleDesc.IndexFormat = DXGI_FORMAT_R16_UINT;
        triangleDesc.IndexCount = pMesh.indexCount;
        triangleDesc.IndexBuffer = m_SceneModel.m_IndexBuffer->GetGPUVirtualAddress() + pMesh.indexDataByteOffset;
        triangleDesc.Transform3x4 = 0; //No transformation matrix pushed
    }
#pragma endregion

#pragma region BLAS
    //Multiple BLAS structures
    std::vector<uint64_t> bottomLevelAccelerationStructureSize(numBottomLevels);
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> bottomLevelAccelerationStructureDescs(numBottomLevels);
    for (uint32_t i = 0; i < numBottomLevels; ++i)
    {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& bottomLevelAccelerationStructureDesc = bottomLevelAccelerationStructureDescs[i];
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelAccelerationStructureDesc.Inputs;
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.NumDescs = m_SceneModel.m_Header.meshCount;
        bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelInputs.pGeometryDescs = &geometryDescs[i];
        m_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);

        bottomLevelAccelerationStructureSize[i] = bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes;
        scratchBufferSizeNeeded = std::max(scratchBufferSizeNeeded, bottomLevelPrebuildInfo.ScratchDataSizeInBytes);
    }
#pragma endregion

#pragma region CREATE_RESOURCES_AND_INSTANCES_IN_MEMORY
    //Create scratch buffer (for temporary data storage) - Used by TLAS
    ByteAddressBuffer scratchBuffer = {};
    scratchBuffer.Create(L"Acceleration Structure Scratch Buffer", (uint32_t)scratchBufferSizeNeeded, 1);

    //Allocate and Set the TLAS GPU Memory/Resource
    D3D12_HEAP_PROPERTIES const defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC const topLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g_Device->CreateCommittedResource(
        &defaultHeapDesc,
        D3D12_HEAP_FLAG_NONE,
        &topLevelDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        IID_PPV_ARGS(&m_TopLevelAccelerationStructure));
    topLevelAccelerationStructureDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure->GetGPUVirtualAddress(); //Set address of allocated GPU Memory
    topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer->GetGPUVirtualAddress();

    //Create BLAS Instances
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(numBottomLevels);
    m_BottomLevelAccelerationStructures.resize(numBottomLevels);
    //For every BLAS descriptor, make an instance (resource)
    for (uint32_t i = 0; i < bottomLevelAccelerationStructureDescs.size(); ++i)
    {
        CComPtr<ID3D12Resource>& bottomLevelStructure = m_BottomLevelAccelerationStructures[i]; //TODO: remove/change?

        //Allocate and Set the BLAS GPU Memory/Resource
        CD3DX12_RESOURCE_DESC bottomLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(bottomLevelAccelerationStructureSize[i], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		g_Device->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bottomLevelDesc,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr,
			IID_PPV_ARGS(&bottomLevelStructure));
        bottomLevelAccelerationStructureDescs[i].DestAccelerationStructureData = bottomLevelStructure->GetGPUVirtualAddress(); //Set address of allocated GPU Memory
        bottomLevelAccelerationStructureDescs[i].ScratchAccelerationStructureData = scratchBuffer->GetGPUVirtualAddress();

        //Allocate and Create Instance of BLAS
        //TODO: use ByteAddressBuffer instead!!
        //m_BLAS[i].Create(L"BLAS_Structure", uint32_t(bottomLevelDesc.Width), sizeof(uint32_t)); //numElements ok??
        uint32_t const descriptorIndex = m_pRaytracingDescriptorHeap->AllocateBufferUav(*bottomLevelStructure); //TODO: change with ByteAddressBufferer?

        D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc = instanceDescs[i];
        //Identity Matrix
        ZeroMemory(instanceDesc.Transform, sizeof(instanceDesc.Transform));
        instanceDesc.Transform[0][0] = 1.f;
        instanceDesc.Transform[1][1] = 1.f;
        instanceDesc.Transform[2][2] = 1.f;
        //Set other params
        instanceDesc.AccelerationStructure = bottomLevelStructure->GetGPUVirtualAddress(); //m_BLAS[i]->GetGPUVirtualAddress(); ///TODO: change?
        instanceDesc.Flags = 0;
        instanceDesc.InstanceID = 0;
        instanceDesc.InstanceMask = 1;
        instanceDesc.InstanceContributionToHitGroupIndex = i;
    }

    //Create Buffer with Derived Views of Instances
    m_AccelerationStructureBuffer.Create(L"Instance Data Buffer", numBottomLevels, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), instanceDescs.data());
    topLevelInputs.InstanceDescs = m_AccelerationStructureBuffer->GetGPUVirtualAddress();
    //topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; //Already set right... See beginning TLAS region
#pragma endregion

#pragma region BUILD_BVH_AND_WAIT_FOR_COMPLETION
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Create Acceleration Structure");
    ID3D12GraphicsCommandList* pCommandList = gfxContext.GetCommandList();
    CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
    pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

    ID3D12DescriptorHeap* descriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() }; //Can we remove the heap if we use ByteAddressBuffer instead?
    pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(descriptorHeaps), descriptorHeaps);

    //Build BLAS
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    for (uint32_t i = 0; i < bottomLevelAccelerationStructureDescs.size(); ++i)
    {
        pRaytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelAccelerationStructureDescs[i], 0, nullptr);
    }
    pCommandList->ResourceBarrier(1, &uavBarrier);

    //Build TLAS
    pRaytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
    gfxContext.Finish(true); //Wait for completion!
#pragma endregion
}

void ImplicitPointDemo::CreateRayTracingPipelineStateObject()
{
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

    //Ray Generation Shader
    CD3DX12_DXIL_LIBRARY_SUBOBJECT* const rayGenSubObject = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE rayGenDxil = CD3DX12_SHADER_BYTECODE((void*)g_pRayGeneration, ARRAYSIZE(g_pRayGeneration));
    rayGenSubObject->SetDXILLibrary(&rayGenDxil);
    rayGenSubObject->DefineExport(m_ExportNameRayGenerationShader);

    //Hit Shader
	CD3DX12_DXIL_LIBRARY_SUBOBJECT* const hitSubObject = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE hitDxil = CD3DX12_SHADER_BYTECODE((void*)g_pHit, ARRAYSIZE(g_pHit));
    hitSubObject->SetDXILLibrary(&hitDxil);
    hitSubObject->DefineExport(m_ExportNameClosestHitShader);

    //Miss Shader
	CD3DX12_DXIL_LIBRARY_SUBOBJECT* const missSubObject = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE missDxil = CD3DX12_SHADER_BYTECODE((void*)g_pMiss, ARRAYSIZE(g_pMiss));
    missSubObject->SetDXILLibrary(&missDxil);
    missSubObject->DefineExport(m_ExportNameMissShader);

    //Hit Group
    CD3DX12_HIT_GROUP_SUBOBJECT* const hitGroupSubObject = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroupSubObject->SetClosestHitShaderImport(m_ExportNameClosestHitShader);
    hitGroupSubObject->SetHitGroupExport(m_ExportNameHitGroup);
    hitGroupSubObject->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    //Shader Config
    CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* const shaderConfigSubObject = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    uint32_t const payLoadSize = 8; //RayPayload = bool + float = 8bytes
    uint32_t const attributeSize = 8; //BuiltInTriangleIntersectionAttributes = float2 = 8bytes
    shaderConfigSubObject->Config(payLoadSize, attributeSize);

    //Local Root Signature
    CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* const localRootSignatureSubObject = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    localRootSignatureSubObject->SetRootSignature(m_RayTracingLocalRootSignature.GetSignature());

    //Global Root Signature
    CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* const globalRootSignatureSubObject = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignatureSubObject->SetRootSignature(m_RayTracingGlobalRootSignature.GetSignature());

    //Shader Association - Uses local root signature
    CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* const rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
    rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignatureSubObject);
    rootSignatureAssociation->AddExport(m_ExportNameRayGenerationShader);

    //Pipeline config
    CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* const pipelineConfigSubObject = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t const maxTraceRecursionDepth = 1;
    pipelineConfigSubObject->Config(maxTraceRecursionDepth);

    //Create State Object
    HRESULT result = m_pRaytracingDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_pRayTracingPSO));
	if (FAILED(result))
	{
		throw std::logic_error("Failed to create Raytracing State Object");
	}
}

void ImplicitPointDemo::CreateShaderBindingTable()
{
    #define ALIGN(alignment, num) ((((num) + alignment - 1) / alignment) * alignment)

	uint32_t const shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    uint32_t const shaderRecordSizeInBytes = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, shaderIdentifierSize);

	std::vector<byte> pHitShaderTable(shaderRecordSizeInBytes * m_SceneModel.m_Header.meshCount);

	auto GetShaderTable = [=](const Model&, ID3D12StateObject* pPSO, byte* pShaderTable)
	{
		ID3D12StateObjectProperties* stateObjectProperties = nullptr;
		HRESULT result = pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties));
		if (FAILED(result))
		{
			throw std::logic_error("Failed to create Shader Binding Table");
		}
		void* pHitGroupIdentifierData = stateObjectProperties->GetShaderIdentifier(m_ExportNameHitGroup);
		for (UINT i = 0; i < m_SceneModel.m_Header.meshCount; i++)
		{
			byte* pShaderRecord = i * shaderRecordSizeInBytes + pShaderTable;
			memcpy(pShaderRecord, pHitGroupIdentifierData, shaderIdentifierSize);
            //Add material ID's to shader table if you want to be able to acquire material information in shader
		}
        stateObjectProperties->Release();
	};

	GetShaderTable(m_SceneModel, m_pRayTracingPSO.p, pHitShaderTable.data());
    m_RayTracingInputs = RayTracingDispatchRayInputs(m_pRayTracingPSO.p,
        pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), 
        m_ExportNameRayGenerationShader, m_ExportNameMissShader);
}

void ImplicitPointDemo::CreateShaderViews()
{
	//Output UAV
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	uint32_t uavDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	g_Device->CopyDescriptorsSimple(1, uavHandle, m_AmbientOcclusionOutputBuffer.GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_AmbientOcclusionOutputGPUHandle = m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

    //Input SRV (depth, normal)
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
    uint32_t srvDescriptorIndex;
    m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
    g_Device->CopyDescriptorsSimple(1, srvHandle, m_DepthBuffer.GetDepthSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_SRVTableGPUHandle = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);
    uint32_t unused;
    m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
    g_Device->CopyDescriptorsSimple(1, srvHandle, m_WorldNormalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ImplicitPointDemo::SetDefaultRenderingPipeline(GraphicsContext& gfxContext)
{
	gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

	gfxContext.SetRootSignature(m_RootSignature);
	gfxContext.SetViewportAndScissor(0, 0, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());

	//Set constant buffer
    SharedConstants constants = {};
	constants.worldMat = Matrix4(kIdentity);
	constants.viewProjectionInverseMat = Invert(m_MainCamera.GetViewProjMatrix());
	constants.viewInverseMat = Invert(m_MainCamera.GetViewMatrix());
	constants.worldViewProjectionMat = m_MainCamera.GetViewProjMatrix() * constants.worldMat;
	constants.viewVector = Vector4(m_MainCamera.GetForwardVec(), 0.f);
	constants.voxelConnectivity = m_VoxelConnectivity;
	constants.level = m_Level;
	constants.cellSize = m_CellSize;
    constants.localizedLOD = static_cast<uint32_t>(m_LODMode);
    constants.maxLevels = m_MaxLevels;
	gfxContext.SetDynamicConstantBufferView(0, sizeof(SharedConstants), &constants);
}

void ImplicitPointDemo::DrawScene(GraphicsContext& gfxContext)
{
	gfxContext.SetIndexBuffer(m_SceneModel.m_IndexBuffer.IndexBufferView());
	gfxContext.SetVertexBuffer(0, m_SceneModel.m_VertexBuffer.VertexBufferView());
	for (uint32_t meshIndex = 0; meshIndex < m_SceneModel.m_Header.meshCount; ++meshIndex)
	{
		const Model::Mesh& mesh = m_SceneModel.m_pMesh[meshIndex];
		uint32_t const indexCount = mesh.indexCount;
		uint32_t const startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
		uint32_t const baseVertex = mesh.vertexDataByteOffset / m_SceneModel.m_VertexStride;

		gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
	}
}

void ImplicitPointDemo::ForwardRenderScenePass(GraphicsContext& gfxContext)
{
    //Set Render Targets
	gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	gfxContext.ClearColor(g_SceneColorBuffer);
	gfxContext.ClearDepth(g_SceneDepthBuffer);
	gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());

	//Set the scene model drawing state
	gfxContext.SetPipelineState(m_MeshPSO);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DrawScene(gfxContext);
}

void ImplicitPointDemo::GBufferPass(GraphicsContext& gfxContext)
{
    //Set G-Buffer as Render Targets
	gfxContext.TransitionResource(m_WorldNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.TransitionResource(m_WorldTangentBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.TransitionResource(m_WorldBitangentBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.TransitionResource(m_DepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	gfxContext.ClearColor(m_WorldNormalBuffer);
    gfxContext.ClearColor(m_WorldTangentBuffer);
    gfxContext.ClearColor(m_WorldBitangentBuffer);
	gfxContext.ClearDepth(m_DepthBuffer);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv[3] = { m_WorldNormalBuffer.GetRTV(), m_WorldTangentBuffer.GetRTV(), m_WorldBitangentBuffer.GetRTV() };
    gfxContext.SetRenderTargets(3, rtv, m_DepthBuffer.GetDSV());

	gfxContext.SetPipelineState(m_GBufferPSO);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DrawScene(gfxContext);

    //Transition GBuffer for Reading - SRV
    gfxContext.TransitionResource(m_WorldNormalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
    gfxContext.TransitionResource(m_WorldTangentBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
    gfxContext.TransitionResource(m_WorldBitangentBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
	gfxContext.TransitionResource(m_DepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

    //Reset to Default Application Render Targets - State of these didn't change!
	gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
}

void ImplicitPointDemo::RayTracingPass(GraphicsContext& gfxContext)
{
    ComputeContext& computeContext = gfxContext.GetComputeContext();

    //Transition GBuffer
    computeContext.TransitionResource(m_AmbientOcclusionOutputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    computeContext.ClearUAV(m_AmbientOcclusionOutputBuffer);

    //Get the RT command list version
    ID3D12GraphicsCommandList* const pCommandList = computeContext.GetCommandList();
    CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
    pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

    //Set Descriptor Heap using RT command list
    ID3D12DescriptorHeap* pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };
    pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

    pCommandList->SetComputeRootSignature(m_RayTracingGlobalRootSignature.GetSignature());
    pCommandList->SetComputeRootDescriptorTable(0, m_AmbientOcclusionOutputGPUHandle); //Output UAV
    pCommandList->SetComputeRootShaderResourceView(1, m_TopLevelAccelerationStructure->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(2, m_SRVTableGPUHandle); //Depth Buffer & World Normal Buffer

	RayTracingConstants rtConstants = {};
	rtConstants.viewProjectionInverseMat = Invert(m_MainCamera.GetViewProjMatrix());
    rtConstants.frameCount = m_FrameCount;
    gfxContext.WriteBuffer(m_DynamicConstantBuffer, 0, &rtConstants, sizeof(RayTracingConstants));
    gfxContext.TransitionResource(m_DynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    pCommandList->SetComputeRootConstantBufferView(3, m_DynamicConstantBuffer->GetGPUVirtualAddress()); //Constant Buffer

    //Dispatch the rays = start raytracing
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = m_RayTracingInputs.GetDispatchRayDesc(g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());
    pRaytracingCommandList->SetPipelineState1(m_pRayTracingPSO.p);
    pRaytracingCommandList->DispatchRays(&dispatchDesc);

    //Update framecount
    ++m_FrameCount;

    if (m_VisualizeRayTracing)
    {
        //Transition Resources
        gfxContext.TransitionResource(m_AmbientOcclusionOutputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
        gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_COPY_DEST, true);

        //Copy raytrace output to the back-buffer
        pCommandList->CopyResource(g_SceneColorBuffer.GetResource(), m_AmbientOcclusionOutputBuffer.GetResource());

        //Transition the back-buffer back to a render target
        gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    }

    gfxContext.Flush(true);
}

void ImplicitPointDemo::AOGaussianBlurPass(GraphicsContext& gfxContext)
{
	ComputeContext& computeContext = gfxContext.GetComputeContext();

	//Set the shader
	computeContext.SetRootSignature(m_GaussianBlurRootSignature);
	computeContext.SetPipelineState(m_GaussianBlurPSO);

	//Set the shader constants
	uint32_t bufferWidth = m_AmbientOcclusionOutputBuffer.GetWidth();
	uint32_t bufferHeight = m_AmbientOcclusionOutputBuffer.GetHeight();
	computeContext.SetConstants(0, 1.0f / bufferWidth, 1.0f / bufferHeight, 0);

	//Set the input textures and output UAV
	computeContext.TransitionResource(m_BlurredAOBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	computeContext.SetDynamicDescriptor(1, 0, m_BlurredAOBuffer.GetUAV());
	D3D12_CPU_DESCRIPTOR_HANDLE SRVs[1] = { m_AmbientOcclusionOutputBuffer.GetSRV() };
	computeContext.SetDynamicDescriptors(2, 0, 1, SRVs);

	//Dispatch the compute shader with default 8x8 thread groups
	computeContext.Dispatch2D(bufferWidth, bufferHeight);

	computeContext.TransitionResource(m_BlurredAOBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gfxContext.Flush(true);
}

void ImplicitPointDemo::SampleGenerationPass(GraphicsContext& gfxContext)
{
	gfxContext.TransitionResource(m_SampleGenerationBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(m_LODBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	gfxContext.TransitionResource(m_BlurredAOBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	gfxContext.SetPipelineState(m_SampleGenerationPSO);

	D3D12_CPU_DESCRIPTOR_HANDLE const srvHandles[5] = 
        { m_DepthBuffer.GetDepthSRV(), m_WorldNormalBuffer.GetSRV(), m_WorldTangentBuffer.GetSRV(), m_WorldBitangentBuffer.GetSRV(), m_BlurredAOBuffer.GetSRV() };
    D3D12_CPU_DESCRIPTOR_HANDLE const uavHandles[2] = { m_SampleGenerationBuffer.GetUAV(), m_LODBuffer.GetUAV() };
	gfxContext.SetDynamicDescriptors(1, 0, 5, srvHandles);
    gfxContext.SetDynamicDescriptors(2, 0, 2, uavHandles);

	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gfxContext.SetIndexBuffer(m_FullscreenIndexBuffer.IndexBufferView());
	gfxContext.SetVertexBuffer(0, m_FullscreenVertexBuffer.VertexBufferView());

	gfxContext.DrawIndexed(6);
}

void ImplicitPointDemo::SampleVisualizationPass(GraphicsContext& gfxContext)
{
	gfxContext.SetPipelineState(m_SampleVisualizationPSO);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	gfxContext.Draw(m_DepthBuffer.GetWidth() * m_DepthBuffer.GetHeight());
}

void ImplicitPointDemo::AccumulationPass(GraphicsContext& gfxContext)
{
	ComputeContext& computeContext = gfxContext.GetComputeContext();

	computeContext.Flush(true); //Make sure all previous data is saved in V-RAM, and wait for completion!

	computeContext.TransitionResource(m_AmbientOcclusionOutputBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	computeContext.TransitionResource(m_SampleGenerationBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    computeContext.TransitionResource(m_AccumulationHashTable, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	computeContext.SetRootSignature(m_AccumulationRootSignature);
	computeContext.SetPipelineState(m_AccumulationPSO);

	D3D12_CPU_DESCRIPTOR_HANDLE const handles[2] = { m_AmbientOcclusionOutputBuffer.GetSRV(), m_SampleGenerationBuffer.GetSRV() };
	computeContext.SetDynamicDescriptors(0, 0, 2, handles);
	computeContext.SetDynamicDescriptor(1, 0, m_AccumulationHashTable.GetUAV());
	computeContext.SetDynamicConstantBufferView(2, sizeof(HashTableConstants), &m_HashTableConstants);

	computeContext.Dispatch2D(1920, 1080);
}

void ImplicitPointDemo::WorldHashTablePass(GraphicsContext& gfxContext)
{
    ComputeContext& computeContext = gfxContext.GetComputeContext();

    computeContext.Flush(true); //Make sure all previous data is saved in V-RAM, and wait for completion!

    computeContext.TransitionResource(m_SampleGenerationBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    computeContext.TransitionResource(m_AccumulationHashTable, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    computeContext.TransitionResource(m_WorldHashTable, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	computeContext.SetRootSignature(m_WorldHashTableRootSignature);
	computeContext.SetPipelineState(m_WorldHashTablePSO);

    D3D12_CPU_DESCRIPTOR_HANDLE const handles[2] = { m_SampleGenerationBuffer.GetSRV(), m_AccumulationHashTable.GetSRV() };
    computeContext.SetDynamicDescriptors(0, 0, 2, handles);
    computeContext.SetDynamicDescriptor(1, 0, m_WorldHashTable.GetUAV());
    computeContext.SetDynamicConstantBufferView(2, sizeof(HashTableConstants), &m_HashTableConstants);

    computeContext.Dispatch2D(1920, 1080);
}

void ImplicitPointDemo::FinalVisualizationPass(GraphicsContext& gfxContext)
{
	ComputeContext& computeContext = gfxContext.GetComputeContext();

	computeContext.Flush(true); //Make sure all previous data is saved in V-RAM, and wait for completion!

	computeContext.TransitionResource(m_SampleGenerationBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	computeContext.TransitionResource(m_WorldHashTable, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    computeContext.TransitionResource(m_LODBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
    computeContext.TransitionResource(m_FinalVisualizationBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	computeContext.SetRootSignature(m_FinalVisualizationRootSignature);
	computeContext.SetPipelineState(m_FinalVisualizationPSO);

	D3D12_CPU_DESCRIPTOR_HANDLE const srvHandles[4] = 
        { m_SampleGenerationBuffer.GetSRV(), m_WorldHashTable.GetSRV(), m_LODBuffer.GetSRV(), m_DepthBuffer.GetDepthSRV() };
	computeContext.SetDynamicDescriptors(0, 0, 4, srvHandles);
    computeContext.SetDynamicDescriptor(1, 0, m_FinalVisualizationBuffer.GetUAV());
	computeContext.SetDynamicConstantBufferView(2, sizeof(HashTableConstants), &m_HashTableConstants);
    __declspec(align(16)) struct LocalConstantBuffer
    {
        Matrix4 viewProjectionInverse;
        Matrix4 viewInverse;
        Vector4 viewVector;
    } constantBuffer;
    constantBuffer.viewProjectionInverse = Invert(m_MainCamera.GetViewProjMatrix());
    constantBuffer.viewInverse = Invert(m_MainCamera.GetViewMatrix());
    constantBuffer.viewVector = Vector4(m_MainCamera.GetForwardVec(), 0.f);;
    computeContext.SetDynamicConstantBufferView(3, sizeof(LocalConstantBuffer), &constantBuffer);
    __declspec(align(16)) struct Modes
    {
        uint32_t maxLevels;
        uint32_t lodMode;
    } modes;
    modes.maxLevels = m_MaxLevels;
    modes.lodMode = static_cast<uint32_t>(m_LODMode);
    computeContext.SetDynamicConstantBufferView(4, sizeof(Modes), &modes);
    __declspec(align(16)) struct DemoConstants
    {
        uint32_t discreteCellSize;
        uint32_t voxelConnectivity;
    } demoConstants;
    demoConstants.discreteCellSize = m_CellSize;
    demoConstants.voxelConnectivity = m_VoxelConnectivity;
    computeContext.SetDynamicConstantBufferView(5, sizeof(DemoConstants), &demoConstants);

    computeContext.Dispatch2D(1920, 1080);
    computeContext.Flush(true); //Make sure all previous data is saved in V-RAM, and wait for completion!

    //Copy to RenderTarget...
	//Transition Resources
    computeContext.TransitionResource(m_FinalVisualizationBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
    computeContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_COPY_DEST, true);

	//Copy raytrace output to the back-buffer
    computeContext.GetCommandList()->CopyResource(g_SceneColorBuffer.GetResource(), m_FinalVisualizationBuffer.GetResource());

	//Transition the back-buffer back to a render target
    computeContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
}

void ImplicitPointDemo::ClearBuffersPass(GraphicsContext& gfxContext)
{
    ComputeContext& computeContext = gfxContext.GetComputeContext();

    computeContext.TransitionResource(m_AccumulationHashTable, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    computeContext.TransitionResource(m_LODBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	computeContext.SetRootSignature(m_ClearBuffersRootSignature);
	computeContext.SetPipelineState(m_ClearBuffersPSO);

	D3D12_CPU_DESCRIPTOR_HANDLE const uavHandles[2] = { m_AccumulationHashTable.GetUAV(), m_LODBuffer.GetUAV() };
	computeContext.SetDynamicDescriptors(0, 0, 2, uavHandles);
	computeContext.SetDynamicConstantBufferView(1, sizeof(HashTableConstants), &m_HashTableConstants);

    computeContext.Dispatch2D(1920, 1080);
}