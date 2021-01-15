#include "WaveFrontRenderer.h"
#include "PTMesh.h"
#include "PTScene.h"
#include "AccelerationStructure.h"
#include "Material.h"
#include "Texture.h"
#include "MemoryBuffer.h"
#include "OutputBuffer.h"
#include "ShaderBindingTableGen.h"
#include "../Shaders/CppCommon/LumenPTConsts.h"
#include "../Shaders/CppCommon/WaveFrontKernels.cuh"

#include <cstdio>
#include <fstream>
#include <sstream>
#include "Cuda/cuda.h"
#include "Cuda/cuda_runtime.h"
#include "Optix/optix_stubs.h"
#include <glm/gtx/compatibility.hpp>


#include "PTMesh.h"
#include "PTPrimitive.h"

void CheckOptixRes(const OptixResult& a_res)
{
    if(a_res != OPTIX_SUCCESS)
    {
        std::string errorName = optixGetErrorName(a_res);
        std::string errorMessage = optixGetErrorString(a_res);

        std::fprintf(
            stderr, 
            "Optix error occured: %s \n Description: %s", 
            errorName.c_str(), 
            errorMessage.c_str());
    }
}

#if defined(OPTIX_NOCHECK)
#define CHECKOPTIXRESULT(x)
#elif defined(OPTIX_CHECK) || defined(_DEBUG)
#define CHECKOPTIXRESULT(x)\
    CheckOptixRes(x);
#endif

WaveFrontRenderer::WaveFrontRenderer(const InitializationData& a_InitializationData)
    :
m_DeviceContext(nullptr),
m_PipelineCompileOptions({}),
m_Pipeline(nullptr),
m_RayGenRecord({}),
m_MissRecord({}),
m_HitRecord({}),
m_Texture(nullptr),
m_ProgramGroups({}),
m_OutputBuffer(nullptr),
m_SBTBuffer(nullptr),
m_Resolution({}),
m_Initialized(false)
{

    m_Initialized = Initialize(a_InitializationData);
    if(!m_Initialized)
    {
        std::fprintf(stderr, "Initialization of wavefront renderer unsuccessful");
        return;
    }

    m_ShaderBindingTableGenerator = std::make_unique<ShaderBindingTableGenerator>();

    m_Texture = std::make_unique<Texture>(LumenPTConsts::gs_AssetDirectory + "debugTex.jpg");

    m_ServiceLocator.m_SBTGenerator = m_ShaderBindingTableGenerator.get();
    m_ServiceLocator.m_Renderer = this;

    CreateShaderBindingTable();


}

WaveFrontRenderer::~WaveFrontRenderer()
{}

bool WaveFrontRenderer::Initialize(const InitializationData& a_InitializationData)
{
    bool success = true;

    m_Resolution = a_InitializationData.m_Resolution;

    InitializeContext();
    InitializePipelineOptions();
    success &= CreatePipeline();
    CreateOutputBuffer();

    return success;

}

void WaveFrontRenderer::InitializeContext()
{

    cudaFree(0);
    CUcontext cu_ctx = 0;
    CHECKOPTIXRESULT(optixInit());
    OptixDeviceContextOptions options = {};
    options.logCallbackFunction = &WaveFrontRenderer::DebugCallback;
    options.logCallbackLevel = 4;
    CHECKOPTIXRESULT(optixDeviceContextCreate(cu_ctx, &options, &m_DeviceContext));

}

void WaveFrontRenderer::InitializePipelineOptions()
{

    m_PipelineCompileOptions = {};
    m_PipelineCompileOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
    m_PipelineCompileOptions.numAttributeValues = 2;
    m_PipelineCompileOptions.numPayloadValues = 4;
    m_PipelineCompileOptions.exceptionFlags = OPTIX_EXCEPTION_FLAG_DEBUG;
    m_PipelineCompileOptions.usesPrimitiveTypeFlags = 0;
    m_PipelineCompileOptions.usesMotionBlur = false;
    m_PipelineCompileOptions.pipelineLaunchParamsVariableName = "params";

}

//TODO: implement necessary Optix shaders for wavefront algorithm
//Shaders:
// - RayGen: trace rays from ray definitions in ray batch, different methods for radiance or shadow.
// - Miss: is this shader necessary, miss result by default in intersection buffer?
// - ClosestHit: radiance trace, report intersection into intersection buffer.
// - AnyHit: shadow trace, report if any hit in between certain max distance.
bool WaveFrontRenderer::CreatePipeline()
{

    //OptixModule shaderModule = CreateModule(LumenPTConsts::gs_ShaderPathBase + "WaveFrontShaders.ptx");
    OptixModule shaderModule = CreateModule(LumenPTConsts::gs_ShaderPathBase + "draw_solid_color.ptx");

    if (shaderModule == nullptr) { return false; }

    OptixProgramGroupDesc rgGroupDesc = {};
    rgGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    //rgGroupDesc.raygen.entryFunctionName = "__raygen__WaveFrontGenRays";
    rgGroupDesc.raygen.entryFunctionName = "__raygen__draw_solid_color";
    rgGroupDesc.raygen.module = shaderModule;

    OptixProgramGroup rayGenProgram = CreateProgramGroup(rgGroupDesc, "RayGen");

    OptixProgramGroupDesc msGroupDesc = {};
    msGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    //msGroupDesc.miss.entryFunctionName = "__miss__WaveFrontMiss";
    msGroupDesc.miss.entryFunctionName = "__miss__MissShader";
    msGroupDesc.miss.module = shaderModule;

    OptixProgramGroup missProgram = CreateProgramGroup(msGroupDesc, "Miss");

    OptixProgramGroupDesc htGroupDesc = {};
    htGroupDesc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
   //htGroupDesc.hitgroup.entryFunctionNameCH = "__closesthit__WaveFrontClosestHit";
   htGroupDesc.hitgroup.entryFunctionNameCH = "__closesthit__HitShader";
   //htGroupDesc.hitgroup.entryFunctionNameAH = "__anyhit__WaveFrontAnyHit";
    htGroupDesc.hitgroup.moduleCH = shaderModule;
    //htGroupDesc.hitgroup.moduleAH = shaderModule;

    OptixProgramGroup hitProgram = CreateProgramGroup(htGroupDesc, "Hit");

    OptixPipelineLinkOptions pipelineLinkOptions = {};
    pipelineLinkOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_FULL;
    pipelineLinkOptions.maxTraceDepth = 3; //TODO: potentially add this as a init param.

    OptixProgramGroup programGroups[] = { rayGenProgram, missProgram, hitProgram };

    char log[2048];
    auto logSize = sizeof(log);

    OptixResult error{};

    CHECKOPTIXRESULT(error = optixPipelineCreate(
        m_DeviceContext, 
        &m_PipelineCompileOptions, 
        &pipelineLinkOptions, 
        programGroups, 
        sizeof(programGroups) / sizeof(OptixProgramGroup), 
        log, 
        &logSize, 
        &m_Pipeline));

    if (error) { return false; }

    OptixStackSizes stackSizes = {};

    AccumulateStackSizes(rayGenProgram, stackSizes);
    AccumulateStackSizes(missProgram, stackSizes);
    AccumulateStackSizes(hitProgram, stackSizes);

    auto finalSizes = ComputeStackSizes(stackSizes, 1, 0, 0);

    CHECKOPTIXRESULT(error = optixPipelineSetStackSize(
        m_Pipeline, 
        finalSizes.DirectSizeTrace, 
        finalSizes.DirectSizeState, 
        finalSizes.ContinuationSize, 
        3));

    if (error) { return false; }

    return true;

}

OptixModule WaveFrontRenderer::CreateModule(const std::string& a_PtxPath)
{

    OptixModuleCompileOptions moduleOptions = {};
    moduleOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_LINEINFO;
    moduleOptions.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    moduleOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;

    std::ifstream stream;
    stream.open(a_PtxPath);

    if(!stream.is_open())
    {
        return nullptr;
    }

    std::stringstream stringStream;
    stringStream << stream.rdbuf();
    std::string source = stringStream.str();

    char log[2048];
    auto logSize = sizeof(log);
    OptixModule module;

    CHECKOPTIXRESULT(optixModuleCreateFromPTX(
        m_DeviceContext, 
        &moduleOptions, 
        &m_PipelineCompileOptions, 
        source.c_str(), 
        source.size(), 
        log, 
        &logSize, 
        &module));

    return module;

}

OptixProgramGroup WaveFrontRenderer::CreateProgramGroup(OptixProgramGroupDesc a_ProgramGroupDesc, const std::string& a_Name)
{

    OptixProgramGroupOptions programGroupOptions = {};

    char log[2048];
    auto logSize = sizeof(log);

    OptixProgramGroup programGroup;

    CHECKOPTIXRESULT(optixProgramGroupCreate(
        m_DeviceContext, 
        &a_ProgramGroupDesc, 
        1, 
        &programGroupOptions, 
        log, 
        &logSize, 
        &programGroup));

    m_ProgramGroups.emplace(a_Name, programGroup);

    return programGroup;

}

std::unique_ptr<AccelerationStructure> WaveFrontRenderer::BuildGeometryAccelerationStructure(
    const OptixAccelBuildOptions& a_BuildOptions, 
    const OptixBuildInput& a_BuildInput)
{

    // Let Optix compute how much memory the output buffer and the temporary buffer need to have, then create these buffers
    OptixAccelBufferSizes sizes;
    CHECKOPTIXRESULT(optixAccelComputeMemoryUsage(m_DeviceContext, &a_BuildOptions, &a_BuildInput, 1, &sizes));
    MemoryBuffer tempBuffer(sizes.tempSizeInBytes);
    std::unique_ptr<MemoryBuffer> resultBuffer = std::make_unique<MemoryBuffer>(sizes.outputSizeInBytes);

    OptixTraversableHandle handle;
    // It is possible to have functionality similar to DX12 Command lists with cuda streams, though it is not required.
    // Just worth mentioning if we want that functionality.
    CheckOptixRes(optixAccelBuild(m_DeviceContext, 0, &a_BuildOptions, &a_BuildInput, 1, *tempBuffer, sizes.tempSizeInBytes,
        **resultBuffer, sizes.outputSizeInBytes, &handle, nullptr, 0));

    // Needs to return the resulting memory buffer and the traversable handle
    return std::make_unique<AccelerationStructure>(handle, std::move(resultBuffer));

}

std::unique_ptr<AccelerationStructure> WaveFrontRenderer::BuildInstanceAccelerationStructure(std::vector<OptixInstance> a_Instances)
{


    auto instanceBuffer = MemoryBuffer(a_Instances.size() * sizeof(OptixInstance));
    instanceBuffer.Write(a_Instances.data(), a_Instances.size() * sizeof(OptixInstance), 0);
    OptixBuildInput buildInput = {};

    assert(*instanceBuffer % OPTIX_INSTANCE_BYTE_ALIGNMENT == 0);

    buildInput.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    buildInput.instanceArray.instances = *instanceBuffer;
    buildInput.instanceArray.numInstances = static_cast<uint32_t>(a_Instances.size());
    buildInput.instanceArray.aabbs = 0;
    buildInput.instanceArray.numAabbs = 0;

    OptixAccelBuildOptions buildOptions = {};
    // Based on research, it is more efficient to continuously be rebuilding most instance acceleration structures rather than to update them
    // This is because updating an acceleration structure reduces its quality, thus lowering the ray traversal speed through it
    // while rebuilding will preserve this quality. Since instance acceleration structures are cheap to build, it is faster to rebuild them than deal
    // with the lower traversal speed
    buildOptions.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    buildOptions.operation = OPTIX_BUILD_OPERATION_BUILD;
    buildOptions.motionOptions.numKeys = 0; // No motion

    OptixAccelBufferSizes sizes;
    CHECKOPTIXRESULT(optixAccelComputeMemoryUsage(
        m_DeviceContext,
        &buildOptions,
        &buildInput,
        1,
        &sizes));

    auto tempBuffer = MemoryBuffer(sizes.tempSizeInBytes);
    auto resultBuffer = std::make_unique<MemoryBuffer>(sizes.outputSizeInBytes);

    OptixTraversableHandle handle;
    CHECKOPTIXRESULT(optixAccelBuild(
        m_DeviceContext,
        0,
        &buildOptions,
        &buildInput,
        1,
        *tempBuffer,
        sizes.tempSizeInBytes,
        **resultBuffer,
        sizes.outputSizeInBytes,
        &handle,
        nullptr,
        0));

    return std::make_unique<AccelerationStructure>(handle, std::move(resultBuffer));;
    
}

//TODO: adjust output buffer to the necessary output buffer received
//from CUDA.
void WaveFrontRenderer::CreateOutputBuffer()
{

    m_OutputBuffer = std::make_unique<::OutputBuffer>(m_Resolution.x, m_Resolution.y);

}

void WaveFrontRenderer::CreateDataBuffers()
{

    const unsigned numPixels = static_cast<unsigned>(m_Resolution.x) * static_cast<unsigned>(m_Resolution.y);

    const unsigned raysPerPixel = 1;
    const unsigned shadowRaysPerPixel = 1;
    const unsigned maxDepth = 3;

    //Allocate pixel buffers.
    for(auto& pixelBuffer : m_PixelBuffers)
    {
        pixelBuffer = std::make_unique<MemoryBuffer>(sizeof(PixelBuffer) + numPixels * sizeof(float3));
        pixelBuffer->Write(numPixels, 0);
    }

    //Allocate result buffer.
    m_ResultBuffer = std::make_unique<MemoryBuffer>(sizeof(ResultBuffer));

    const unsigned int numOutputChannels = ResultBuffer::s_NumOutputChannels;
    PixelBuffer* pixelBufferHandles[numOutputChannels];

    for(unsigned i = 0; i < numOutputChannels; ++i)
    {
        if(i < _countof(m_PixelBuffers))
        {
            pixelBufferHandles[i] = m_PixelBuffers[i]->GetDevicePtr<PixelBuffer>();
        }
    }

    //Initialize result buffer with pixel buffer handles.
    m_ResultBuffer->Write(pixelBufferHandles, 0);

    //Allocate and initialize ray batches.
    for(auto& rayBatch : m_RayBatches)
    {
        rayBatch = std::make_unique<MemoryBuffer>(sizeof(RayBatch) + numPixels * raysPerPixel * sizeof(RayData));
        rayBatch->Write(numPixels, 0);
        rayBatch->Write(raysPerPixel, sizeof(RayBatch::m_NumPixels));
    }

    for(auto& intersectionBuffer : m_IntersectionBuffers)
    {
        intersectionBuffer = std::make_unique<MemoryBuffer>(sizeof(IntersectionBuffer) + numPixels * raysPerPixel * sizeof(IntersectionData));
        intersectionBuffer->Write(numPixels, 0);
        intersectionBuffer->Write(raysPerPixel, sizeof(IntersectionBuffer::m_NumPixels));
    }

    m_ShadowRayBatch = std::make_unique<MemoryBuffer>(sizeof(ShadowRayBatch) + maxDepth * numPixels * shadowRaysPerPixel * sizeof(ShadowRayData));
    m_ShadowRayBatch->Write(maxDepth, 0);
    m_ShadowRayBatch->Write(numPixels, sizeof(ShadowRayBatch::m_MaxDepth));
    m_ShadowRayBatch->Write(shadowRaysPerPixel, sizeof(ShadowRayBatch::m_MaxDepth) + sizeof(ShadowRayBatch::m_NumPixels));

}


void WaveFrontRenderer::DebugCallback(unsigned a_Level, const char* a_Tag, const char* a_Message, void*)
{

    std::printf("%u::%s:: %s\n\n", a_Level, a_Tag, a_Message);

}

void WaveFrontRenderer::AccumulateStackSizes(OptixProgramGroup a_ProgramGroup, OptixStackSizes& a_StackSizes)
{

    OptixStackSizes localSizes;
    optixProgramGroupGetStackSize(a_ProgramGroup, &localSizes);
    a_StackSizes.cssRG = std::max(a_StackSizes.cssRG, localSizes.cssRG);
    a_StackSizes.cssMS = std::max(a_StackSizes.cssMS, localSizes.cssMS);
    a_StackSizes.cssIS = std::max(a_StackSizes.cssIS, localSizes.cssIS);
    a_StackSizes.cssAH = std::max(a_StackSizes.cssAH, localSizes.cssAH);
    a_StackSizes.cssCH = std::max(a_StackSizes.cssCH, localSizes.cssCH);
    a_StackSizes.cssCC = std::max(a_StackSizes.cssCC, localSizes.cssCC);
    a_StackSizes.dssDC = std::max(a_StackSizes.dssDC, localSizes.dssDC);

}

//TODO: replace data structures with the data structures necessary
//in this rendering pipeline. (Wavefront data structures)
void WaveFrontRenderer::CreateShaderBindingTable()
{

    m_RayGenRecord = m_ShaderBindingTableGenerator->SetRayGen<RaygenData>();

    auto& rayGenRecord = m_RayGenRecord.GetRecord();
    rayGenRecord.m_Header = GetProgramGroupHeader("RayGen");
    rayGenRecord.m_Data.m_Color = { 0.4f, 0.5f, 0.2f };

    m_MissRecord = m_ShaderBindingTableGenerator->AddMiss<MissData>();

    auto& missRecord = m_MissRecord.GetRecord();
    missRecord.m_Header = GetProgramGroupHeader("Miss");
    missRecord.m_Data.m_Num = 2;
    missRecord.m_Data.m_Color = { 0.f, 0.f, 1.f };

    m_HitRecord = m_ShaderBindingTableGenerator->AddHitGroup<HitData>();

    auto& hitRecord = m_HitRecord.GetRecord();
    hitRecord.m_Header = GetProgramGroupHeader("Hit");
    hitRecord.m_Data.m_TextureObject = **m_Texture;

}

ProgramGroupHeader WaveFrontRenderer::GetProgramGroupHeader(const std::string& a_GroupName) const
{

    assert(m_ProgramGroups.count(a_GroupName) != 0);

    ProgramGroupHeader header;

    optixSbtRecordPackHeader(m_ProgramGroups.at(a_GroupName), &header);

    return header;

}

GLuint WaveFrontRenderer::TraceFrame()
{

    std::vector<float3> vert = {
        {0.5f, 0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}
    };

    std::vector<Vertex> verti = std::vector<Vertex>(3);
    verti[0].m_Position = vert[0];
    verti[0].m_Normal = { 1.0f, 0.0f, 0.0f };
    verti[1].m_Position = vert[1];
    verti[1].m_Normal = { 0.0f, 1.0f, 0.0f };
    verti[2].m_Position = vert[2];
    verti[2].m_Normal = { 0.0f, 0.0f, 1.0f };

    MemoryBuffer vertexBuffer(verti.size() * sizeof(Vertex));
    vertexBuffer.Write(verti.data(), verti.size() * sizeof(Vertex), 0);

    LaunchParameters params = {};
    OptixShaderBindingTable sbt = m_ShaderBindingTableGenerator->GetTableDesc();

    params.m_Image = m_OutputBuffer->GetDevicePointer();
    params.m_Handle = static_cast<PTScene&>(*m_Scene).GetSceneAccelerationStructure();
    params.m_ImageWidth = m_Resolution.x;
    params.m_ImageHeight = m_Resolution.y;
    params.m_VertexBuffer = vertexBuffer.GetDevicePtr<Vertex>();

    m_Camera.SetAspectRatio(static_cast<float>(m_Resolution.x) / static_cast<float>(m_Resolution.y));
    glm::vec3 eye, U, V, W;
    m_Camera.GetVectorData(eye, U, V, W);
    params.eye = make_float3(eye.x, eye.y, eye.z);
    params.U = make_float3(U.x, U.y, U.z);
    params.V = make_float3(V.x, V.y, V.z);
    params.W = make_float3(W.x, W.y, W.z);

    MemoryBuffer devBuffer(sizeof(params));
    devBuffer.Write(params);

    static float f = 0.f;
    f += 1.f / 60.f;

    m_MissRecord.GetRecord().m_Data.m_Color = { 0.f, sinf(f), 0.f };


    cudaDeviceSynchronize();
    auto error = cudaGetLastError();

    optixLaunch(
        m_Pipeline, 
        0, 
        *devBuffer, 
        devBuffer.GetSize(), 
        &sbt, 
        m_Resolution.x, 
        m_Resolution.y, 
        1);

    return m_OutputBuffer->GetTexture();

}

WaveFrontRenderer::ComputedStackSizes WaveFrontRenderer::ComputeStackSizes(
    OptixStackSizes a_StackSizes, 
    int a_TraceDepth, 
    int a_DirectDepth, 
    int a_ContinuationDepth)
{

    ComputedStackSizes sizes;
    sizes.DirectSizeState = a_StackSizes.dssDC * a_DirectDepth;
    sizes.DirectSizeTrace = a_StackSizes.dssDC * a_DirectDepth;

    unsigned int cssCCTree = a_ContinuationDepth * a_StackSizes.cssCC;

    unsigned int cssCHOrMSPlusCCTree = std::max(a_StackSizes.cssCH, a_StackSizes.cssMS) + cssCCTree;

    sizes.ContinuationSize =
        a_StackSizes.cssRG + cssCCTree +
        (std::max(a_TraceDepth, 1) - 1) * cssCHOrMSPlusCCTree +
        std::min(a_TraceDepth, 1) * std::max(cssCHOrMSPlusCCTree, a_StackSizes.cssIS + a_StackSizes.cssAH);

    return sizes;

}

std::unique_ptr<MemoryBuffer> WaveFrontRenderer::InterleaveVertexData(const PrimitiveData& a_MeshData)
{
    std::vector<Vertex> vertices;

    for (size_t i = 0; i < a_MeshData.m_Positions.Size(); i++)
    {
        auto& v = vertices.emplace_back();
        v.m_Position = make_float3(a_MeshData.m_Positions[i].x, a_MeshData.m_Positions[i].y, a_MeshData.m_Positions[i].z);
        if (!a_MeshData.m_TexCoords.Empty())
            v.m_UVCoord = make_float2(a_MeshData.m_TexCoords[i].x, a_MeshData.m_TexCoords[i].y);
        if (!a_MeshData.m_Normals.Empty())
            v.m_Normal = make_float3(a_MeshData.m_Normals[i].x, a_MeshData.m_Normals[i].y, a_MeshData.m_Normals[i].z);
    }
    return std::make_unique<MemoryBuffer>(vertices);
}

std::shared_ptr<Lumen::ILumenTexture> WaveFrontRenderer::CreateTexture(void* a_PixelData, uint32_t a_Width, uint32_t a_Height)
{

    static cudaChannelFormatDesc formatDesc = cudaCreateChannelDesc<uchar4>();
    return std::make_shared<Texture>(a_PixelData, formatDesc, a_Width, a_Height);

}

std::unique_ptr<Lumen::ILumenPrimitive> WaveFrontRenderer::CreatePrimitive(PrimitiveData& a_PrimitiveData)
{
    auto vertexBuffer = InterleaveVertexData(a_PrimitiveData);
    cudaDeviceSynchronize();
    auto err = cudaGetLastError();

    std::vector<uint32_t> correctedIndices;

    if (a_PrimitiveData.m_IndexSize != 4)
    {
        VectorView<uint16_t, uint8_t> indexView(a_PrimitiveData.m_IndexBinary);

        for (size_t i = 0; i < indexView.Size(); i++)
        {
            correctedIndices.push_back(indexView[i]);
        }
    }

    std::unique_ptr<MemoryBuffer> indexBuffer = std::make_unique<MemoryBuffer>(correctedIndices);

    unsigned int geomFlags = OPTIX_GEOMETRY_FLAG_NONE;

    OptixAccelBuildOptions buildOptions = {};
    buildOptions.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    buildOptions.operation = OPTIX_BUILD_OPERATION_BUILD;
    buildOptions.motionOptions = {};

    OptixBuildInput buildInput = {};
    buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    buildInput.triangleArray.indexBuffer = **indexBuffer;
    buildInput.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    buildInput.triangleArray.indexStrideInBytes = 0;
    buildInput.triangleArray.numIndexTriplets = correctedIndices.size() / 3;
    buildInput.triangleArray.vertexBuffers = &**vertexBuffer;
    buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    buildInput.triangleArray.vertexStrideInBytes = sizeof(Vertex);
    buildInput.triangleArray.numVertices = a_PrimitiveData.m_Positions.Size();
    buildInput.triangleArray.numSbtRecords = 1;
    buildInput.triangleArray.flags = &geomFlags;

    auto gAccel = BuildGeometryAccelerationStructure(buildOptions, buildInput);

    auto prim = std::make_unique<PTPrimitive>(std::move(vertexBuffer), std::move(indexBuffer), std::move(gAccel));

    prim->m_Material = a_PrimitiveData.m_Material;

    prim->m_RecordHandle = m_ShaderBindingTableGenerator->AddHitGroup<DevicePrimitive>();
    auto& rec = prim->m_RecordHandle.GetRecord();
    rec.m_Header = GetProgramGroupHeader("Hit");
    rec.m_Data.m_VertexBuffer = prim->m_VertBuffer->GetDevicePtr<Vertex>();
    rec.m_Data.m_IndexBuffer = prim->m_IndexBuffer->GetDevicePtr<unsigned int>();
    rec.m_Data.m_Material = static_cast<Material*>(prim->m_Material.get())->GetDeviceMaterial();

    return prim;
}

std::shared_ptr<Lumen::ILumenMesh> WaveFrontRenderer::CreateMesh(
    std::vector<std::unique_ptr<Lumen::ILumenPrimitive>>& a_Primitives)
{
    auto mesh = std::make_shared<PTMesh>(a_Primitives, m_ServiceLocator);
    return mesh;
}

std::shared_ptr<Lumen::ILumenMaterial> WaveFrontRenderer::CreateMaterial(const MaterialData& a_MaterialData)
{

    auto mat = std::make_shared<Material>();
    mat->SetDiffuseColor(a_MaterialData.m_DiffuseColor);
    mat->SetDiffuseTexture(a_MaterialData.m_DiffuseTexture);

    return mat;

}

std::shared_ptr<Lumen::ILumenScene> WaveFrontRenderer::CreateScene(SceneData a_SceneData)
{
    return std::make_shared<PTScene>(a_SceneData, m_ServiceLocator);
}