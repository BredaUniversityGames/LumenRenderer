#pragma once
#include "ShaderBindingTableRecord.h"
#include "../Shaders/CppCommon/LaunchParameters.h"
#include "MemoryBuffer.h"
#include "Camera.h"
#include "PTServiceLocator.h"

#include "Renderer/LumenRenderer.h"

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <Optix/optix_types.h>
#include "Cuda/builtin_types.h"

using GLuint = unsigned;

class OutputBuffer;
class ShaderBindingTableGenerator;

class AccelerationStructure;

namespace Lumen
{
    class ILumenTexture;
    class ILumenPrimitive;
}

class WaveFrontRenderer : public LumenRenderer
{
public:

    struct InitializationData
    {
        uint2 m_Resolution;

    };

    WaveFrontRenderer(const InitializationData& a_InitializationData);
    ~WaveFrontRenderer();



    //template<typename VertexType>
    //OptixTraversableHandle BuildGeometryAccelerationStructure(std::vector<VertexType> a_Vertices, size_t a_Offset)

    template<typename VertexType, typename IndexType = uint32_t>
    std::unique_ptr<AccelerationStructure> BuildGeometryAccelerationStructure(
        std::vector<VertexType> a_Vertices, size_t a_VertexOffset = 0,
        std::vector<IndexType> a_Indices = std::vector<IndexType>(), size_t a_IndexOffset = 0);
    std::unique_ptr<AccelerationStructure> BuildGeometryAccelerationStructure(
        const OptixAccelBuildOptions& a_BuildOptions,
        const OptixBuildInput& a_BuildInput);

    std::unique_ptr<AccelerationStructure> BuildInstanceAccelerationStructure(std::vector<OptixInstance> a_Instances);

    // Creates a cuda texture from the provided raw data and sizes. Only works if the pixel format is uchar4.
    std::shared_ptr<Lumen::ILumenTexture> CreateTexture(void* a_PixelData, uint32_t a_Width, uint32_t a_Height) override;

    std::unique_ptr<Lumen::ILumenPrimitive> CreatePrimitive(PrimitiveData& a_PrimitiveData) override;
    std::unique_ptr<MemoryBuffer> InterleaveVertexData(const PrimitiveData& a_MeshData);

    std::shared_ptr<Lumen::ILumenMesh> CreateMesh(std::vector<std::unique_ptr<Lumen::ILumenPrimitive>>& a_Primitives) override;

    std::shared_ptr<Lumen::ILumenMaterial> CreateMaterial(const MaterialData& a_MaterialData) override;

    std::shared_ptr<Lumen::ILumenScene> CreateScene(SceneData a_SceneData) override;

   

    GLuint TraceFrame();

    Camera m_Camera;

    Lumen::Transform m_TestTransform;

private:

    struct ComputedStackSizes
    {
        int DirectSizeTrace;
        int DirectSizeState;
        int ContinuationSize;
    };



    bool Initialize(const InitializationData& a_InitializationData);

    void InitializeContext();

    void InitializePipelineOptions();

    bool CreatePipeline();

    void CreateOutputBuffer();

    void CreateDataBuffers();

    OptixModule CreateModule(const std::string& a_PtxPath);

    OptixProgramGroup CreateProgramGroup(OptixProgramGroupDesc a_ProgramGroupDesc, const std::string& a_Name);

    void CreateShaderBindingTable();

    ProgramGroupHeader GetProgramGroupHeader(const std::string& a_GroupName) const;

    static void DebugCallback(unsigned int a_Level, const char* a_Tag, const char* a_Message, void* /*extra data provided during context initialization*/);

    static void AccumulateStackSizes(OptixProgramGroup a_ProgramGroup, OptixStackSizes& a_StackSizes);

    static ComputedStackSizes ComputeStackSizes(OptixStackSizes a_StackSizes, int a_TraceDepth, int a_DirectDepth, int a_ContinuationDepth);



    PTServiceLocator m_ServiceLocator;

    OptixDeviceContext              m_DeviceContext;

    OptixPipelineCompileOptions     m_PipelineCompileOptions;
    OptixPipeline                   m_Pipeline;

    std::unique_ptr<ShaderBindingTableGenerator> m_ShaderBindingTableGenerator;


    RecordHandle<RaygenData>    m_RayGenRecord;
    RecordHandle<MissData>      m_MissRecord;
    RecordHandle<HitData>       m_HitRecord;

    std::map<std::string, OptixProgramGroup> m_ProgramGroups;

    std::unique_ptr<OutputBuffer> m_OutputBuffer;
    std::unique_ptr<MemoryBuffer> m_SBTBuffer;

    std::vector<std::unique_ptr<MemoryBuffer>> m_TempBuffers;

    //Data buffers for the wavefront algorithm.

    //ResultBuffer storing the different PixelBuffers as different light channels;
    std::unique_ptr<MemoryBuffer> m_ResultBuffer;
    //3 PixelBuffers for the different channels in the ResultBuffer and 1 PixelBuffer for the merged results.
    std::unique_ptr<MemoryBuffer> m_PixelBuffers[4];
    //2 ray batches, 1 for storing primary rays, other for overwriting secondary rays.
    std::unique_ptr<MemoryBuffer> m_RayBatches[3];
    //2 intersection buffers, 1 for storing primary intersections, other for overwriting secondary intersections.
    std::unique_ptr<MemoryBuffer> m_IntersectionBuffers[2];
    //1 shadow ray batch to overwrite with shadow rays.
    std::unique_ptr<MemoryBuffer> m_ShadowRayBatch;

    std::unique_ptr<class Texture> m_Texture;

    uint2 m_Resolution;
    bool m_Initialized;

};

template <typename VertexType, typename IndexType>
std::unique_ptr<AccelerationStructure> WaveFrontRenderer::BuildGeometryAccelerationStructure(
    std::vector<VertexType> a_Vertices,
    size_t a_VertexOffset, std::vector<IndexType> a_Indices, size_t a_IndexOffset)
{
    // Double check if the IndexType is uint32_t or uint16_t as those are the only supported index formats
    static_assert(std::is_same<IndexType, uint32_t>::value, "The index type needs to be either a 16- or 32-bit unsigned int");

    // Upload the vertex data to the device
    MemoryBuffer vBuffer(a_Vertices.size() * sizeof(VertexType) - a_VertexOffset);
    vBuffer.Write(a_Vertices.data() + a_VertexOffset, a_Vertices.size() * sizeof(VertexType), 0);

    bool hasIndexBuffer = !a_Indices.empty();
    MemoryBuffer iBuffer(a_Indices.size() * sizeof(IndexType) - a_IndexOffset);
    if (hasIndexBuffer) // Upload the index data to the device
        iBuffer.Write(a_Indices.data() + a_IndexOffset, a_Indices.size() * sizeof(IndexType), 0);

    unsigned int flags = OPTIX_GEOMETRY_FLAG_NONE;

    OptixBuildInput buildInput = {};
    buildInput.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    buildInput.triangleArray.indexBuffer = hasIndexBuffer ? *iBuffer : 0;
    buildInput.triangleArray.indexStrideInBytes = static_cast<uint32_t>(a_Indices.size()); // If the buffer is empty, this is 0 and everything is fine in the universe
    if (hasIndexBuffer) // By default, the index format is set to none, so no need for an else statement
        buildInput.triangleArray.indexFormat = sizeof(IndexType) == 2 ? OPTIX_INDICES_FORMAT_UNSIGNED_SHORT3 : OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    buildInput.triangleArray.numVertices = static_cast<uint32_t>(a_Vertices.size());
    buildInput.triangleArray.vertexBuffers = &*vBuffer;
    buildInput.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3; // I doubt we will ever need a different vertex format
    // If the vertex stride is set to 0, it is assumed the vertices are tightly packed and thus the size of the vertex format is taken
    buildInput.triangleArray.vertexStrideInBytes = sizeof(VertexType) <= sizeof(float) * 3 ? 0 : sizeof(VertexType);

    // Extras which are not necessary, but are here for documentation purposes
    buildInput.triangleArray.primitiveIndexOffset = 0; // Defines an offset when accessing the primitive index offset in the hit shaders
    // If the input contains multiple primitives, each with a different Material, we can specify offsets for their SBT records here
    // This could be used as a replacement for the 3-layer acceleration structure we considered earlier
    buildInput.triangleArray.sbtIndexOffsetBuffer = 0;
    buildInput.triangleArray.sbtIndexOffsetSizeInBytes = 0;
    buildInput.triangleArray.sbtIndexOffsetStrideInBytes = 0;
    buildInput.triangleArray.numSbtRecords = 1;
    buildInput.triangleArray.flags = &flags;

    OptixAccelBuildOptions buildOptions = {};
    buildOptions.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE; // Static mesh which is build once, so we build it with FAST_TRACE
    buildOptions.operation = OPTIX_BUILD_OPERATION_BUILD; // Obviously building
    buildOptions.motionOptions = {}; // No motion

    return BuildGeometryAccelerationStructure(buildOptions, buildInput);

}
