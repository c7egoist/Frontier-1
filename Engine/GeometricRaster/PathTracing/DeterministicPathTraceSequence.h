/*============================================================================================================================================
                                                    DETERMINISTICPATHTRACESEQUENCE.H
============================================================================================================================================*/
// 🧩 Unified Bit-Exact Owen-Sobol (0,2) 6-Queue Wavefront Path Tracer for BOTH GTX (WideBoundingTree BVH4) & RTX (VK_KHR_ray_query)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/ContourSurfelIlluminationSequence.h"
#include "Engine/GeometricRaster/PathTracing/WavefrontQueueLayout.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class PathTraceExecutionTier : uint8_t
{
    AutoSelectOptimal   = 0u, // [-] - Uses RTX VK_KHR_ray_query when supported, otherwise GTX 4-Wide BVH4 compute
    GtxWideBvh4Compute  = 1u, // [-] - Forces stackless-ordered 128 B 4-Wide WideBoundingTree compute query
    RtxHardwareRayQuery = 2u, // [-] - Requests inline Vulkan VK_KHR_ray_query hardware BLAS/TLAS acceleration
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DeterministicPathTraceConfiguration
{
    uint32_t               SamplesPerPixel            = 8u;    // [spp] - Total Owen-Sobol (0,2) samples per pixel
    uint32_t               SampleBatchCount           = 2u;    // [cnt] - Samples dispatched per Wavefront batch
    uint32_t               MaxBounceDepth             = 4u;    // [cnt] - Maximum path transport depth
    bool                   SharedQuadratureNet        = false; // [-]   - True = zero-grain shared (0,m,2) net across pixels
    bool                   EnableAtrousReconstruction = false; // [-]   - Optional 3-step albedo/normal/depth A-Trous filter
    PathTraceExecutionTier ExecutionTier              = PathTraceExecutionTier::AutoSelectOptimal;
};

struct DeterministicPathTraceResult
{
    uint32_t                            WidthPx             = 0u;
    uint32_t                            HeightPx            = 0u;
    std::vector<Mathematics::LinearRGB> LinearHdr           {};
    std::vector<uint8_t>                DisplayRgba8        {};
    uint32_t                            DeterminismDigest32 = 0u;
    PathTraceExecutionTier              ExecutedTier        = PathTraceExecutionTier::GtxWideBvh4Compute;
    double                              ElapsedMilliseconds = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                           DETERMINISTIC PATH TRACE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DeterministicPathTraceSequence
{
public:
    DeterministicPathTraceSequence() = default;
    ~DeterministicPathTraceSequence();

    DeterministicPathTraceSequence(const DeterministicPathTraceSequence&)            = delete;
    DeterministicPathTraceSequence& operator=(const DeterministicPathTraceSequence&) = delete;

    // Computes bit-exact FNV-1a 32-bit digest over DisplayRgba8 (verifies DistinctDigests == 1 across batch sizes)
    [[nodiscard]] static uint32_t ComputeRgbaDeterminismDigest(std::span<const uint8_t> DisplayRgba8) noexcept;

    // Executes bit-exact CPU Wavefront Path Tracer with identical 64-bit split-word fixed-point accumulation
    [[nodiscard]] static DeterministicPathTraceResult EvaluateCpuDeterministicSequence(
        const DeterministicPathTraceConfiguration&      Config,
        const PerspectiveCameraBasis&                   Camera,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        const SolarDirectionalSource&                   Sun,
        std::span<const PolygonalAreaQuadLight>         AreaLights,
        Mathematics::LinearRGB                          SkyRadiance,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr);

    // Initializes Vulkan 6-Queue Wavefront compute programs (_GTX.comp.spv & optional _RTX.comp.spv)
    [[nodiscard]] bool InitializeVulkanWavefront(
        VulkanInterchange*           Device,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownVulkanWavefront() noexcept;

    [[nodiscard]] bool Ready() const noexcept { return GtxProgramsLoaded; }
    [[nodiscard]] bool QueryRtxRayQueryReady() const noexcept { return RtxProgramsLoaded && HardwareAccelReady; }

    // Uploads WideBoundingTree 128 B entries + 48 B triangles + emissives + optional Vulkan BLAS/TLAS for RTX RayQuery
    [[nodiscard]] bool SynchronizeSceneGeometry(
        const SpatialAcceleration::WideBoundingTree&  SceneTree,
        const MaterialSpace::CompactMaterialRegister& MaterialRegister,
        std::span<const PolygonalAreaQuadLight>       AreaLights);

    // Executes 6-Queue Lock-Free Wavefront Path Tracer on Vulkan GPU (GTX BVH4 or RTX RayQuery)
    [[nodiscard]] DeterministicPathTraceResult ExecuteVulkanWavefrontSequence(
        const DeterministicPathTraceConfiguration& Config,
        const PerspectiveCameraBasis&              Camera,
        const SolarDirectionalSource&              Sun,
        Mathematics::LinearRGB                     SkyRadiance);

private:
    [[nodiscard]] bool BuildHardwareRayQueryAccel(const SpatialAcceleration::WideBoundingTree& SceneTree);
    void               ReleaseSceneBuffers() noexcept;

    VulkanInterchange*         BoundDevice              = nullptr;
    VkDescriptorPool           DescriptorRegistry       = VK_NULL_HANDLE;

    VkDescriptorSetLayout      PrimaryGtxLayout         = VK_NULL_HANDLE;
    VkPipelineLayout           PrimaryGtxProgramLayout  = VK_NULL_HANDLE;
    VkPipeline                 PrimaryGtxProgram        = VK_NULL_HANDLE;
    VkDescriptorSet            PrimaryGtxSet            = VK_NULL_HANDLE;

    VkDescriptorSetLayout      PrimaryRtxLayout         = VK_NULL_HANDLE;
    VkPipelineLayout           PrimaryRtxProgramLayout  = VK_NULL_HANDLE;
    VkPipeline                 PrimaryRtxProgram        = VK_NULL_HANDLE;
    VkDescriptorSet            PrimaryRtxSet            = VK_NULL_HANDLE;

    VkDescriptorSetLayout      ScatterLayout            = VK_NULL_HANDLE;
    VkPipelineLayout           ScatterProgramLayout     = VK_NULL_HANDLE;
    VkPipeline                 ScatterProgram           = VK_NULL_HANDLE;
    VkDescriptorSet            ScatterSet               = VK_NULL_HANDLE;

    VkDescriptorSetLayout      BounceGtxLayout          = VK_NULL_HANDLE;
    VkPipelineLayout           BounceGtxProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                 BounceClosestGtxProgram  = VK_NULL_HANDLE;
    VkPipeline                 ShadowAnyHitGtxProgram   = VK_NULL_HANDLE;
    VkDescriptorSet            BounceGtxSet             = VK_NULL_HANDLE;

    VkDescriptorSetLayout      BounceRtxLayout          = VK_NULL_HANDLE;
    VkPipelineLayout           BounceRtxProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                 BounceClosestRtxProgram  = VK_NULL_HANDLE;
    VkPipeline                 ShadowAnyHitRtxProgram   = VK_NULL_HANDLE;
    VkDescriptorSet            BounceRtxSet             = VK_NULL_HANDLE;

    VkDescriptorSetLayout      FinalizeLayout           = VK_NULL_HANDLE;
    VkPipelineLayout           FinalizeProgramLayout    = VK_NULL_HANDLE;
    VkPipeline                 FinalizeProgram          = VK_NULL_HANDLE;
    VkPipeline                 AtrousProgram            = VK_NULL_HANDLE;
    VkDescriptorSet            FinalizeSetPing          = VK_NULL_HANDLE;
    VkDescriptorSet            FinalizeSetPong          = VK_NULL_HANDLE;

    DeviceBufferResource       UniformBuffer            {};
    DeviceBufferResource       WideEntryBuffer          {};
    DeviceBufferResource       PrimitiveBuffer          {};
    DeviceBufferResource       MaterialBuffer           {};
    DeviceBufferResource       EmissiveBuffer           {};
    DeviceBufferResource       SurfaceHitQueueA         {};
    DeviceBufferResource       SurfaceHitQueueB         {};
    DeviceBufferResource       BounceRayQueue           {};
    DeviceBufferResource       ShadowRayQueue           {};
    DeviceBufferResource       QueueCountersBuffer      {};
    DeviceBufferResource       FixedAccumBuffer         {};
    DeviceBufferResource       PrimaryAlbedoDepthBuffer {};
    DeviceBufferResource       PrimaryNormalBuffer      {};
    DeviceBufferResource       DemodPingBuffer          {};
    DeviceBufferResource       DemodPongBuffer          {};
    DeviceBufferResource       FinalHdrBuffer           {};

    DeviceBufferResource       RtxVertexBuffer          {};
    DeviceBufferResource       BlasStorageBuffer        {};
    DeviceBufferResource       TlasStorageBuffer        {};
    VkAccelerationStructureKHR BottomLevelAccel         = VK_NULL_HANDLE;
    VkAccelerationStructureKHR TopLevelAccel            = VK_NULL_HANDLE;

    uint32_t                   UploadedWideEntryCount   = 0u;
    uint32_t                   UploadedPrimitiveCount   = 0u;
    uint32_t                   UploadedMaterialCount    = 0u;
    uint32_t                   UploadedEmissiveCount    = 0u;
    uint32_t                   AllocatedPixelCapacity   = 0u;
    bool                       GtxProgramsLoaded        = false;
    bool                       RtxProgramsLoaded        = false;
    bool                       HardwareAccelReady       = false;
};

} // namespace Frontier::GeometricRaster
