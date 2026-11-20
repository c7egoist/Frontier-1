/*============================================================================================================================================
                                                     DETERMINISTICPATHTRACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation Exhibit for 6-Queue Wavefront DeterministicPathTraceSequence (Batch Invariance, Softbox Radiometry, CPU + Vulkan GTX/RTX)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/PathTracing/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace Frontier;

namespace
{

struct ValidationTally
{
    uint32_t VerifiedCount = 0u;
    uint32_t RejectedCount = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++VerifiedCount;
            std::fprintf(stdout, "[DeterministicPathTraceValidation][OK]   %s\n", Label);
        }
        else
        {
            ++RejectedCount;
            std::fprintf(stderr, "[DeterministicPathTraceValidation][FAIL] %s\n", Label);
        }
    }
};

} // namespace

int main()
{
    ValidationTally Tally {};

    // 1. Build closed Lambertian test room + overhead emissive quad light
    MaterialSpace::CompactMaterialRegister MatReg {};
    MaterialSpace::SurfaceMaterial MatteWhite {};
    MatteWhite.Diffuse.BaseColor  = { 0.75f, 0.75f, 0.75f };
    MatteWhite.Diffuse.BaseWeight = 1.0f;
    MatteWhite.Specular.Roughness = 1.0f;
    MatteWhite.Specular.Metalness = 0.0f;
    const uint16_t SlotWhite = MatReg.RegisterMaterial(MatteWhite);

    MaterialSpace::SurfaceMaterial MatteCrimson = MatteWhite;
    MatteCrimson.Diffuse.BaseColor = { 0.80f, 0.14f, 0.12f };
    const uint16_t SlotCrimson = MatReg.RegisterMaterial(MatteCrimson);

    GeometrySpace::PolygonNetwork RoomNet {};
    const uint32_t FloorStart = static_cast<uint32_t>(RoomNet.Faces.size());
    GeometrySpace::AppendBoxPrimitive(RoomNet, {  0.0f, 0.0f, -0.80f }, { 2.0f, 2.0f, 0.08f }, {}, {});
    GeometrySpace::AppendBoxPrimitive(RoomNet, {  0.0f, 1.8f,  0.40f }, { 2.0f, 0.08f, 1.25f }, {}, {});
    GeometrySpace::AppendSpherePrimitive(RoomNet, { 0.0f, 0.4f, -0.25f }, 0.42f, 24u, 14u, {}, {});
    for (uint32_t F = FloorStart; F < static_cast<uint32_t>(RoomNet.Faces.size()); ++F)
    {
        RoomNet.Faces[F].PartGroupOrdinal = SlotWhite;
    }
    const uint32_t WallStart = static_cast<uint32_t>(RoomNet.Faces.size());
    GeometrySpace::AppendBoxPrimitive(RoomNet, { -1.8f, 0.0f, 0.40f }, { 0.08f, 2.0f, 1.25f }, {}, {});
    for (uint32_t F = WallStart; F < static_cast<uint32_t>(RoomNet.Faces.size()); ++F)
    {
        RoomNet.Faces[F].PartGroupOrdinal = SlotCrimson;
    }

    SpatialAcceleration::WideBoundingTree SceneTree {};
    Tally.Verify(SceneTree.BuildFromPolygonNetwork(RoomNet),
                 "Built 128-byte WideBoundingTree BVH4 for Wavefront Path Tracer validation");

    GeometricRaster::PolygonalAreaQuadLight Softbox {};
    Softbox.CenterWorld      = { 0.0f, 0.2f, 1.55f };
    Softbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    Softbox.UpBitangent      = { 0.0f, -1.0f, 0.0f };
    Softbox.HalfWidthMeters  = 0.65f;
    Softbox.HalfHeightMeters = 0.65f;
    Softbox.EmittedRadiance  = { 12.0f, 11.2f, 10.0f };
    const std::vector<GeometricRaster::PolygonalAreaQuadLight> AreaLights { Softbox };

    GeometricRaster::SolarDirectionalSource OffSun {};
    OffSun.SolarIrradiance = { 0.0f, 0.0f, 0.0f };
    const Mathematics::LinearRGB SkyRad { 0.08f, 0.10f, 0.14f };

    GeometricRaster::PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = { 0.0f, -2.85f, 0.15f };
    Cam.TargetPoint        = { 0.0f,  0.35f, -0.10f };
    Cam.VerticalFovRadians = 0.72f;
    Cam.ViewportWidthPx    = 48u;
    Cam.ViewportHeightPx   = 32u;

    // 2. CPU Wavefront batch-invariance proof (SampleBatchCount = 1 vs 4 vs 8 -> identical FNV-1a digest!)
    GeometricRaster::DeterministicPathTraceConfiguration Cfg1 {};
    Cfg1.SamplesPerPixel            = 8u;
    Cfg1.SampleBatchCount           = 1u;
    Cfg1.MaxBounceDepth             = 3u;
    Cfg1.EnableAtrousReconstruction = false;

    GeometricRaster::DeterministicPathTraceConfiguration Cfg4 = Cfg1;
    Cfg4.SampleBatchCount = 4u;

    GeometricRaster::DeterministicPathTraceConfiguration Cfg8 = Cfg1;
    Cfg8.SampleBatchCount = 8u;

    const auto Out1 = GeometricRaster::DeterministicPathTraceSequence::EvaluateCpuDeterministicSequence(
        Cfg1, Cam, SceneTree, MatReg, OffSun, AreaLights, SkyRad);
    const auto Out4 = GeometricRaster::DeterministicPathTraceSequence::EvaluateCpuDeterministicSequence(
        Cfg4, Cam, SceneTree, MatReg, OffSun, AreaLights, SkyRad);
    const auto Out8 = GeometricRaster::DeterministicPathTraceSequence::EvaluateCpuDeterministicSequence(
        Cfg8, Cam, SceneTree, MatReg, OffSun, AreaLights, SkyRad);

    Tally.Verify(Out1.DeterminismDigest32 != 0u &&
                     Out1.DeterminismDigest32 == Out4.DeterminismDigest32 &&
                     Out4.DeterminismDigest32 == Out8.DeterminismDigest32,
                 "Split-word fixed-point accumulator guarantees bitwise identical digest across batch sizes 1, 4, 8");

    // 3. Verify Vulkan GTX (WideBoundingTree BVH4) & optional RTX (VK_KHR_ray_query) execution
    GeometricRaster::VulkanInterchange Device {};
    if (Device.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR)))
    {
        GeometricRaster::DeterministicPathTraceSequence GpuPt {};
        const bool Ready = GpuPt.InitializeVulkanWavefront(&Device, std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR));
        Tally.Verify(Ready, "DeterministicPathTraceSequence compiled & loaded 6-queue Wavefront SPIR-V compute shaders");

        if (Ready)
        {
            Tally.Verify(GpuPt.SynchronizeSceneGeometry(SceneTree, MatReg, AreaLights),
                         "SynchronizeSceneGeometry uploaded 128B BVH4 + 48B triangles + materials + emissive contours");

            GeometricRaster::DeterministicPathTraceConfiguration GtxB2 = Cfg4;
            GtxB2.ExecutionTier    = GeometricRaster::PathTraceExecutionTier::GtxWideBvh4Compute;
            GtxB2.SampleBatchCount = 2u;

            GeometricRaster::DeterministicPathTraceConfiguration GtxB8 = GtxB2;
            GtxB8.SampleBatchCount = 8u;

            const auto GpuOut2 = GpuPt.ExecuteVulkanWavefrontSequence(GtxB2, Cam, OffSun, SkyRad);
            const auto GpuOut8 = GpuPt.ExecuteVulkanWavefrontSequence(GtxB8, Cam, OffSun, SkyRad);

            Tally.Verify(GpuOut2.ExecutedTier == GeometricRaster::PathTraceExecutionTier::GtxWideBvh4Compute &&
                             GpuOut2.DeterminismDigest32 == GpuOut8.DeterminismDigest32,
                         "Vulkan GTX 6-queue Wavefront compute achieves exact bitwise batch invariance (batch 2 == batch 8)");

            if (GpuPt.QueryRtxRayQueryReady())
            {
                GeometricRaster::DeterministicPathTraceConfiguration RtxCfg = GtxB8;
                RtxCfg.ExecutionTier = GeometricRaster::PathTraceExecutionTier::RtxHardwareRayQuery;
                const auto RtxOut = GpuPt.ExecuteVulkanWavefrontSequence(RtxCfg, Cam, OffSun, SkyRad);
                Tally.Verify(RtxOut.ExecutedTier == GeometricRaster::PathTraceExecutionTier::RtxHardwareRayQuery &&
                                 RtxOut.DisplayRgba8.size() == 48u * 32u * 4u,
                             "Vulkan RTX VK_KHR_ray_query Wavefront compute executes hardware TLAS/BLAS acceleration");
            }
        }
    }

    std::fprintf(stdout, "[DeterministicPathTraceValidation] Verified=%u Rejected=%u\n", Tally.VerifiedCount, Tally.RejectedCount);
    return (Tally.RejectedCount == 0u) ? 0 : 1;
}
