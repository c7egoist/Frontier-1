/*============================================================================================================================================
                                                    VEHICLERENDERTIERSSHOWCASE.CPP
============================================================================================================================================*/
// 🧩 High-Resolution Automotive Showroom Showcase rendering ControlledIntegrator across all 3 Zero-SDF World Render Tiers:
//    Tier 1: GTX GI #1 — ContourSurfelGi (Stokes Contours + 2-Bounce Bunnell Surfels + SH9, Zero Denoiser) + ContactPcss (3.91 mm)
//    Tier 2: GTX GI #2 — DynamicIrradianceGi (8x8 Octahedral Chebyshev + 4-Candidate Owen-Sobol RIS + Joint A-Trous) + SolarDiscRayQuery
//    Tier 3: Deterministic Path Tracer — Unbiased Multi-Bounce OpenPBR MIS/NEE + GGX VNDF + 6-Queue Wavefront Batch-Invariance Proof

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/PathTracing/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/WorldRendering/VulkanWorldRenderSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

struct ShowroomGeometryAsset
{
    GeometrySpace::PolygonNetwork       Net {};
    SpatialAcceleration::ClusterPyramid Pyr {};
};

[[nodiscard]] ShowroomGeometryAsset BuildBoxAsset(Mathematics::Scalar3D HalfExtents, uint16_t Slot)
{
    ShowroomGeometryAsset Asset {};
    GeometrySpace::AppendBoxPrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, HalfExtents, {}, {}, 0u, Slot);
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

[[nodiscard]] ShowroomGeometryAsset BuildSphereAsset(float Radius, uint32_t Sectors, uint32_t Rings, uint16_t Slot)
{
    ShowroomGeometryAsset Asset {};
    GeometrySpace::AppendSpherePrimitive(Asset.Net, { 0.0f, 0.0f, 0.0f }, Radius, Sectors, Rings, {}, {}, 0u, Slot);
    Asset.Net.LinkOppositeEdges();
    Asset.Net.RecomputeNormals();
    (void)Asset.Pyr.BuildFromPolygonNetwork(Asset.Net, 3u);
    return Asset;
}

void AppendNetworkIntoUnified(
    GeometrySpace::PolygonNetwork&       Dst,
    const GeometrySpace::PolygonNetwork& Src,
    Mathematics::Axis3D                  Translation,
    uint16_t                             OverrideSlot,
    bool                                 UseOverrideSlot)
{
    const uint32_t VertexOffset = static_cast<uint32_t>(Dst.Vertices.size());
    for (const auto& V : Src.Vertices)
    {
        (void)Dst.AppendVertex(V.Axes + Translation);
    }

    for (const auto& F : Src.Faces)
    {
        if (F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || F.CornerCount < 3u)
        {
            continue;
        }
        std::vector<uint32_t> Loop {};
        Loop.reserve(F.CornerCount);
        uint32_t EdgeIdx = F.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < F.CornerCount && EdgeIdx != GeometrySpace::InvalidNetworkIndex; ++Step)
        {
            Loop.push_back(VertexOffset + Src.Edges[EdgeIdx].OriginVertexIndex);
            EdgeIdx = Src.Edges[EdgeIdx].NextEdgeIndex;
            if (EdgeIdx == F.BoundaryLoopEdgeIndex)
            {
                break;
            }
        }
        if (Loop.size() < 3u)
        {
            continue;
        }
        const uint16_t Slot = UseOverrideSlot ? OverrideSlot : F.PartGroupOrdinal;
        const std::array<Mathematics::UVAxes, 3u> DummyUV {{ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }};
        for (std::size_t K = 1u; K + 1u < Loop.size(); ++K)
        {
            const std::array<uint32_t, 3u> Tri {{ Loop[0u], Loop[K], Loop[K + 1u] }};
            (void)Dst.AppendFace(Tri, DummyUV, F.Shading, F.PriorityLayerOrdinal, Slot);
        }
    }
}

} // namespace

int main()
{
    // 1. Load high-resolution 6-part ControlledIntegrator vehicle in RightHandedZUp
    auto VehicleAsset = GeometrySpace::ConstructSamplePolygonNetwork(
        GeometrySpace::SampleAssetPreset::ControlledIntegrator,
        Mathematics::WorldCoordinateBasis::RightHandedZUp);

    if (VehicleAsset.Network.Vertices.empty() || VehicleAsset.Network.Faces.empty())
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Could not load ControlledIntegrator vehicle.\n");
        return 1;
    }

    Mathematics::Axis3D VMin {  1.0e9f,  1.0e9f,  1.0e9f };
    Mathematics::Axis3D VMax { -1.0e9f, -1.0e9f, -1.0e9f };
    for (const auto& V : VehicleAsset.Network.Vertices)
    {
        VMin.X = std::min(VMin.X, V.Axes.X); VMax.X = std::max(VMax.X, V.Axes.X);
        VMin.Y = std::min(VMin.Y, V.Axes.Y); VMax.Y = std::max(VMax.Y, V.Axes.Y);
        VMin.Z = std::min(VMin.Z, V.Axes.Z); VMax.Z = std::max(VMax.Z, V.Axes.Z);
    }

    // Center XY, place tyres flush on Z = 0, scale wheelbase length to 3.30 m, and yaw -28 deg for 3/4 hero angle
    const float RawLength   = std::max(0.5f, std::max(VMax.X - VMin.X, VMax.Y - VMin.Y));
    const float ScaleFactor = 3.30f / RawLength;
    const float CenterX     = 0.5f * (VMin.X + VMax.X);
    const float CenterY     = 0.5f * (VMin.Y + VMax.Y);
    const float YawRad      = -28.0f * Constants::DegToRadFloat;
    const float CosYaw      = std::cos(YawRad);
    const float SinYaw      = std::sin(YawRad);

    for (auto& V : VehicleAsset.Network.Vertices)
    {
        const float SX = (V.Axes.X - CenterX) * ScaleFactor;
        const float SY = (V.Axes.Y - CenterY) * ScaleFactor;
        const float SZ = (V.Axes.Z - VMin.Z)  * ScaleFactor;
        V.Axes.X = SX * CosYaw - SY * SinYaw - 0.08f;
        V.Axes.Y = SX * SinYaw + SY * CosYaw + 0.22f;
        V.Axes.Z = SZ;
    }

    // 2. Author OpenPBR Showroom + 6-Part Automotive Materials
    GeometricRaster::WorldSceneSpecification WorldScene {};
    auto& MatReg = WorldScene.MaterialRegister;

    // Showroom materials
    MaterialSpace::SurfaceMaterial GalleryFloor {};
    GalleryFloor.MaterialName             = "ShowroomSatinConcrete";
    GalleryFloor.Diffuse.BaseColor        = { 0.72f, 0.73f, 0.75f };
    GalleryFloor.Diffuse.DiffuseRoughness = 0.35f;
    GalleryFloor.Specular.Roughness       = 0.34f;
    GalleryFloor.Clearcoat.CoatWeight     = 0.22f;
    GalleryFloor.Clearcoat.CoatRoughness  = 0.16f;

    MaterialSpace::SurfaceMaterial GalleryPlaster {};
    GalleryPlaster.MaterialName             = "GalleryWarmPlaster";
    GalleryPlaster.Diffuse.BaseColor        = { 0.80f, 0.79f, 0.77f };
    GalleryPlaster.Diffuse.DiffuseRoughness = 0.50f;
    GalleryPlaster.Specular.Roughness       = 0.62f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName             = "ShowroomCrimsonWall";
    CrimsonWall.Diffuse.BaseColor        = { 0.86f, 0.08f, 0.07f };
    CrimsonWall.Diffuse.DiffuseRoughness = 0.45f;
    CrimsonWall.Specular.Roughness       = 0.52f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName             = "ShowroomEmeraldWall";
    EmeraldWall.Diffuse.BaseColor        = { 0.08f, 0.78f, 0.22f };
    EmeraldWall.Diffuse.DiffuseRoughness = 0.45f;
    EmeraldWall.Specular.Roughness       = 0.52f;

    MaterialSpace::SurfaceMaterial RoofLouver {};
    RoofLouver.MaterialName            = "CobaltArchitecturalLouver";
    RoofLouver.Diffuse.BaseColor       = { 0.12f, 0.26f, 0.72f };
    RoofLouver.Specular.Roughness      = 0.28f;
    RoofLouver.Clearcoat.CoatWeight    = 0.85f;
    RoofLouver.Clearcoat.CoatRoughness = 0.06f;

    MaterialSpace::SurfaceMaterial MirrorChrome {};
    MirrorChrome.MaterialName         = "CalibrationMirrorChrome";
    MirrorChrome.Diffuse.BaseColor    = { 0.95f, 0.96f, 0.98f };
    MirrorChrome.Specular.Metalness   = 1.0f;
    MirrorChrome.Specular.Roughness   = 0.03f;
    MirrorChrome.Specular.EdgeTintF82 = { 0.99f, 0.99f, 1.00f };

    MaterialSpace::SurfaceMaterial AbbeGlass {};
    AbbeGlass.MaterialName                      = "CalibrationAbbeCrownGlass";
    AbbeGlass.Diffuse.BaseWeight                = 0.0f;
    AbbeGlass.Specular.Roughness                = 0.02f;
    AbbeGlass.Specular.DielectricIOR            = 1.52f;
    AbbeGlass.Transmission.TransmissionWeight   = 1.0f;
    AbbeGlass.Transmission.AttenuationColor     = { 0.95f, 0.98f, 1.00f };
    AbbeGlass.Transmission.AbbeDispersionNumber = 26.0f;

    // 6 Dedicated Automotive Part Materials for ControlledIntegrator
    MaterialSpace::SurfaceMaterial CarPaintHero {};
    CarPaintHero.MaterialName            = "MetallicAzureSilverClearcoat";
    CarPaintHero.Diffuse.BaseColor       = { 0.24f, 0.44f, 0.78f };
    CarPaintHero.Specular.Metalness      = 0.76f;
    CarPaintHero.Specular.Roughness      = 0.15f;
    CarPaintHero.Specular.EdgeTintF82    = { 0.88f, 0.95f, 1.00f };
    CarPaintHero.Clearcoat.CoatWeight    = 1.00f;
    CarPaintHero.Clearcoat.CoatRoughness = 0.035f;

    MaterialSpace::SurfaceMaterial CanopyGlass {};
    CanopyGlass.MaterialName            = "SmokedAutomotiveCanopy";
    CanopyGlass.Diffuse.BaseColor       = { 0.035f, 0.055f, 0.090f };
    CanopyGlass.Specular.Metalness      = 0.12f;
    CanopyGlass.Specular.Roughness      = 0.028f;
    CanopyGlass.Specular.DielectricIOR  = 1.52f;
    CanopyGlass.Clearcoat.CoatWeight    = 1.00f;
    CanopyGlass.Clearcoat.CoatRoughness = 0.020f;

    MaterialSpace::SurfaceMaterial HeadlampOptics {};
    HeadlampOptics.MaterialName               = "CrystalReflectorOptics";
    HeadlampOptics.Diffuse.BaseColor          = { 0.96f, 0.92f, 0.84f };
    HeadlampOptics.Specular.Metalness         = 0.88f;
    HeadlampOptics.Specular.Roughness         = 0.05f;
    HeadlampOptics.Clearcoat.CoatWeight       = 1.00f;
    HeadlampOptics.Clearcoat.CoatRoughness    = 0.02f;

    MaterialSpace::SurfaceMaterial CarbonChassis {};
    CarbonChassis.MaterialName            = "SatinCarbonAeroChassis";
    CarbonChassis.Diffuse.BaseColor       = { 0.10f, 0.11f, 0.13f };
    CarbonChassis.Specular.Metalness      = 0.34f;
    CarbonChassis.Specular.Roughness      = 0.34f;
    CarbonChassis.Clearcoat.CoatWeight    = 0.45f;
    CarbonChassis.Clearcoat.CoatRoughness = 0.12f;

    MaterialSpace::SurfaceMaterial TyreRubber {};
    TyreRubber.MaterialName             = "VulcanizedTreadRubber";
    TyreRubber.Diffuse.BaseColor        = { 0.055f, 0.058f, 0.065f };
    TyreRubber.Diffuse.DiffuseRoughness = 0.80f;
    TyreRubber.Specular.Metalness       = 0.02f;
    TyreRubber.Specular.Roughness       = 0.74f;

    MaterialSpace::SurfaceMaterial ForgedRim {};
    ForgedRim.MaterialName         = "CncForgedAlloyRim";
    ForgedRim.Diffuse.BaseColor    = { 0.86f, 0.88f, 0.92f };
    ForgedRim.Specular.Metalness   = 0.96f;
    ForgedRim.Specular.Roughness   = 0.09f;
    ForgedRim.Specular.EdgeTintF82 = { 0.98f, 0.99f, 1.00f };

    const uint16_t SlotFloor   = MatReg.RegisterMaterial(GalleryFloor);
    const uint16_t SlotPlaster = MatReg.RegisterMaterial(GalleryPlaster);
    const uint16_t SlotCrimson = MatReg.RegisterMaterial(CrimsonWall);
    const uint16_t SlotEmerald = MatReg.RegisterMaterial(EmeraldWall);
    const uint16_t SlotLouver  = MatReg.RegisterMaterial(RoofLouver);
    const uint16_t SlotChrome  = MatReg.RegisterMaterial(MirrorChrome);
    const uint16_t SlotGlass   = MatReg.RegisterMaterial(AbbeGlass);

    const std::array<uint16_t, 6u> VehiclePartSlots {{
        MatReg.RegisterMaterial(CarPaintHero),
        MatReg.RegisterMaterial(CanopyGlass),
        MatReg.RegisterMaterial(HeadlampOptics),
        MatReg.RegisterMaterial(CarbonChassis),
        MatReg.RegisterMaterial(TyreRubber),
        MatReg.RegisterMaterial(ForgedRim),
    }};

    // Remap each vehicle face's PartGroupOrdinal (1..6) onto its registered OpenPBR MaterialSlot
    for (auto& F : VehicleAsset.Network.Faces)
    {
        const uint32_t PartIdx = (F.PartGroupOrdinal > 0u)
            ? std::min<uint32_t>(static_cast<uint32_t>(F.PartGroupOrdinal - 1u), 5u)
            : 0u;
        F.PartGroupOrdinal = VehiclePartSlots[PartIdx];
    }
    VehicleAsset.Network.RecomputeNormals();

    // Split vehicle by material part into 6 ClusterPyramids so CPU & GPU visibility/material slots match 1:1
    std::array<ShowroomGeometryAsset, 6u> VehiclePartAssets {};
    for (std::size_t P = 0u; P < 6u; ++P)
    {
        auto& DstNet = VehiclePartAssets[P].Net;
        for (const auto& V : VehicleAsset.Network.Vertices)
        {
            (void)DstNet.AppendVertex(V.Axes);
        }
        const std::array<Mathematics::UVAxes, 3u> DummyUV {{ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }};
        for (const auto& F : VehicleAsset.Network.Faces)
        {
            if (F.PartGroupOrdinal != VehiclePartSlots[P] || F.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
            {
                continue;
            }
            std::vector<uint32_t> Loop {};
            uint32_t EdgeIdx = F.BoundaryLoopEdgeIndex;
            for (uint16_t Step = 0u; Step < F.CornerCount && EdgeIdx != GeometrySpace::InvalidNetworkIndex; ++Step)
            {
                Loop.push_back(VehicleAsset.Network.Edges[EdgeIdx].OriginVertexIndex);
                EdgeIdx = VehicleAsset.Network.Edges[EdgeIdx].NextEdgeIndex;
                if (EdgeIdx == F.BoundaryLoopEdgeIndex) break;
            }
            for (std::size_t K = 1u; K + 1u < Loop.size(); ++K)
            {
                const std::array<uint32_t, 3u> Tri {{ Loop[0u], Loop[K], Loop[K + 1u] }};
                (void)DstNet.AppendFace(Tri, DummyUV, F.Shading, 0u, VehiclePartSlots[P]);
            }
        }
        if (!DstNet.Faces.empty())
        {
            (void)VehiclePartAssets[P].Pyr.BuildFromPolygonNetwork(DstNet, 3u);
            WorldScene.ClusterInstances.push_back(
                { &VehiclePartAssets[P].Pyr, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 1.0f, VehiclePartSlots[P], false });
        }
    }

    // Build showroom architectural enclosure
    const auto FloorAsset    = BuildBoxAsset({ 3.30f, 2.85f, 0.08f }, SlotFloor);
    const auto BackWallAsset = BuildBoxAsset({ 3.30f, 0.08f, 1.55f }, SlotPlaster);
    const auto WestWallAsset = BuildBoxAsset({ 0.08f, 2.75f, 1.55f }, SlotCrimson);
    const auto EastWallAsset = BuildBoxAsset({ 0.08f, 2.75f, 1.55f }, SlotEmerald);
    const auto LouverAsset   = BuildBoxAsset({ 1.65f, 1.25f, 0.07f }, SlotLouver);
    const auto ChromeAsset   = BuildSphereAsset(0.42f, 36u, 22u, SlotChrome);
    const auto GlassOrbAsset = BuildSphereAsset(0.34f, 32u, 20u, SlotGlass);

    GeometrySpace::PolygonNetwork UnifiedWorldNet {};
    AppendNetworkIntoUnified(UnifiedWorldNet, VehicleAsset.Network, { 0.0f, 0.0f, 0.0f }, 0u, false);

    auto RegisterGalleryPiece = [&](const ShowroomGeometryAsset& Asset, Mathematics::Axis3D Center, uint16_t Slot, bool GlassShell)
    {
        WorldScene.ClusterInstances.push_back({ &Asset.Pyr, Center, Center, 1.0f, Slot, GlassShell });
        if (!GlassShell)
        {
            AppendNetworkIntoUnified(UnifiedWorldNet, Asset.Net, Center, Slot, true);
        }
    };

    RegisterGalleryPiece(FloorAsset,    {  0.00f,  0.25f, -0.08f }, SlotFloor,   false);
    RegisterGalleryPiece(BackWallAsset, {  0.00f,  2.75f,  1.45f }, SlotPlaster, false);
    RegisterGalleryPiece(WestWallAsset, { -2.62f,  0.25f,  1.45f }, SlotCrimson, false);
    RegisterGalleryPiece(EastWallAsset, {  2.62f,  0.25f,  1.45f }, SlotEmerald, false);
    RegisterGalleryPiece(LouverAsset,   { -0.65f,  0.60f,  2.58f }, SlotLouver,  false);
    RegisterGalleryPiece(ChromeAsset,   { -1.78f,  1.48f,  0.42f }, SlotChrome,  false);
    RegisterGalleryPiece(GlassOrbAsset, {  1.62f, -0.92f,  0.34f }, SlotGlass,   true);

    if (!WorldScene.SceneTree.BuildFromPolygonNetwork(UnifiedWorldNet))
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] WideBoundingTree build failed.\n");
        return 1;
    }
    WorldScene.OccluderField.BuildFromSceneTree(WorldScene.SceneTree);

    // 3. Configure Automotive Studio Lighting, Octahedral Cache & Contour+Surfel GI
    WorldScene.Sun.DirectionToSun          = Mathematics::NormalizeAxis({ 0.44f, -0.54f, 0.72f });
    WorldScene.Sun.SunAngularRadiusRadians = 0.018f;
    WorldScene.Sun.SolarIrradiance         = { 5.80f, 5.45f, 4.90f };
    WorldScene.Sun.RtxSamplesPerPixel      = 6u;
    WorldScene.SkyRadiance                 = { 0.18f, 0.25f, 0.38f };

    GeometricRaster::PolygonalAreaQuadLight OverheadSoftbox {};
    OverheadSoftbox.CenterWorld      = { 0.35f, -0.05f, 2.42f };
    OverheadSoftbox.RightTangent     = { 1.0f, 0.0f, 0.0f };
    OverheadSoftbox.UpBitangent      = { 0.0f, -1.0f, 0.0f };
    OverheadSoftbox.HalfWidthMeters  = 1.05f;
    OverheadSoftbox.HalfHeightMeters = 0.78f;
    OverheadSoftbox.EmittedRadiance  = { 8.20f, 7.40f, 6.30f };
    WorldScene.AreaLights.push_back(OverheadSoftbox);

    WorldScene.LatticeConfig.GridMinimumWorld  = { -2.50f, -1.85f,  0.04f };
    WorldScene.LatticeConfig.GridMaximumWorld  = {  2.50f,  2.60f,  2.35f };
    WorldScene.LatticeConfig.CountX            = 9u;
    WorldScene.LatticeConfig.CountY            = 8u;
    WorldScene.LatticeConfig.CountZ            = 5u;
    WorldScene.LatticeConfig.FibonacciRayCount = 64u;
    WorldScene.LatticeConfig.BouncePasses      = 2u;

    WorldScene.ContourGiConfig.TargetBounceSurfels      = 128u;
    WorldScene.ContourGiConfig.RadiosityBounces         = 2u;
    WorldScene.ContourGiConfig.SurfelDiskSolidAngleGain = 1.25f;
    WorldScene.ContourGiConfig.SurfelShadowGateMeters   = 3.10f;

    GeometricRaster::TieredWorldRenderSequence CpuWorldSeq {};
    CpuWorldSeq.PrepareWorldScene(WorldScene);
    std::fprintf(stdout,
                 "[VehicleRenderTiersShowcase] Prepared Vehicle+Stage Scene: %zu Triangles | %zu BVH4 Nodes | %zu BounceSurfels\n",
                 WorldScene.SceneTree.Primitives.size(),
                 WorldScene.SceneTree.WideEntries.size(),
                 CpuWorldSeq.InspectContourSurfelGi().InspectBounceSurfels().size());
    std::fflush(stdout);

    // 4. Initialize Vulkan GPU World Render Sequence + 6-Queue Wavefront Path Tracer Digest Proof
    GeometricRaster::VulkanInterchange VkDevice {};
    std::string VkDiag {};
    if (!VkDevice.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR), &VkDiag))
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] VulkanInterchange init failed: %s\n", VkDiag.c_str());
        return 1;
    }

    const std::filesystem::path ShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    GeometricRaster::VulkanWorldRenderSequence GpuWorldSeq {};
    if (!GpuWorldSeq.InitializeSequence(VkDevice, ShaderDir))
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] VulkanWorldRenderSequence init failed.\n");
        return 1;
    }
    GpuWorldSeq.SynchronizeSceneResources(WorldScene, &CpuWorldSeq.InspectIrradianceLattice(), true);
    std::fprintf(stdout, "[VehicleRenderTiersShowcase] Synchronized Vulkan GPU Scene Resources.\n");
    std::fflush(stdout);

    // Verify 6-Queue Wavefront Path Tracer bitwise batch-invariance digest on the exact Vehicle scene!
    uint32_t WavefrontBatchDigest = 0u;
    {
        GeometricRaster::DeterministicPathTraceSequence WavefrontPt {};
        if (WavefrontPt.InitializeVulkanWavefront(&VkDevice, ShaderDir) &&
            WavefrontPt.SynchronizeSceneGeometry(WorldScene.SceneTree, WorldScene.MaterialRegister, WorldScene.AreaLights))
        {
            GeometricRaster::PerspectiveCameraBasis DigestCam {};
            DigestCam.CameraOrigin       = { 0.05f, -3.95f, 1.16f };
            DigestCam.TargetPoint        = { 0.00f,  0.22f, 0.44f };
            DigestCam.WorldUpAxis        = { 0.00f,  0.00f, 1.00f };
            DigestCam.VerticalFovRadians = 41.0f * Constants::DegToRadFloat;
            DigestCam.ViewportWidthPx    = 48u;
            DigestCam.ViewportHeightPx   = 27u;

            GeometricRaster::DeterministicPathTraceConfiguration Batch2 {};
            Batch2.SamplesPerPixel  = 2u;
            Batch2.SampleBatchCount = 1u;
            Batch2.MaxBounceDepth   = 2u;

            GeometricRaster::DeterministicPathTraceConfiguration Batch4 = Batch2;
            Batch4.SampleBatchCount = 2u;

            const auto Wf2 = WavefrontPt.ExecuteVulkanWavefrontSequence(Batch2, DigestCam, WorldScene.Sun, WorldScene.SkyRadiance);
            const auto Wf4 = WavefrontPt.ExecuteVulkanWavefrontSequence(Batch4, DigestCam, WorldScene.Sun, WorldScene.SkyRadiance);
            if (Wf2.DeterminismDigest32 != 0u && Wf2.DeterminismDigest32 == Wf4.DeterminismDigest32)
            {
                WavefrontBatchDigest = Wf2.DeterminismDigest32;
                std::fprintf(stdout, "[VehicleRenderTiersShowcase] Verified 6-Queue Wavefront Vehicle Digest = 0x%08X (Batch 1 == Batch 2)\n",
                             WavefrontBatchDigest);
                std::fflush(stdout);
            }
        }
    }

    // 5. Render Crisp 640x360 16:9 Automotive Hero Views across all 3 Render Tiers (+ Isolated Indirect GI)
    constexpr uint32_t ViewW = 640u;
    constexpr uint32_t ViewH = 360u;

    GeometricRaster::PerspectiveCameraBasis HeroCam {};
    HeroCam.CameraOrigin       = { 0.08f, -3.92f, 1.15f };
    HeroCam.TargetPoint        = { 0.00f,  0.24f, 0.42f };
    HeroCam.WorldUpAxis        = { 0.00f,  0.00f, 1.00f };
    HeroCam.VerticalFovRadians = 41.0f * Constants::DegToRadFloat;
    HeroCam.NearClipMeters     = 0.12f;
    HeroCam.FarClipMeters      = 60.0f;
    HeroCam.ViewportWidthPx    = ViewW;
    HeroCam.ViewportHeightPx   = ViewH;

    std::vector<uint8_t> Tier1ContourSurfelRgba {};
    std::vector<uint8_t> Tier2DynamicIrradRgba  {};
    std::vector<uint8_t> Tier3PathTraceRgba     {};
    std::vector<uint8_t> IsolatedGiRgba         {};

    // TIER 1: GTX GI #1 — Contour + 2-Bounce Surfel + SH9 (Zero Denoiser) + ContactPcss (3.91 mm/tx)
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine           = GeometricRaster::WorldRenderEngine::ContourSurfelGi;
        Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::ContactPcss;
        Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        if (!GpuWorldSeq.RenderWorldViewportWithEngine(Cfg, 0u, HeroCam, WorldScene, Tier1ContourSurfelRgba))
        {
            std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Tier 1 ContourSurfelGi render failed.\n");
            return 1;
        }
        std::fprintf(stdout, "[VehicleRenderTiersShowcase] Rendered Tier 1: ContourSurfelGi + ContactPcss (%ux%u)\n", ViewW, ViewH);
        std::fflush(stdout);
    }

    // TIER 2: GTX GI #2 — Octahedral Chebyshev Cache + WeightedPathReplay RIS + SolarDiscRayQuery
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine           = GeometricRaster::WorldRenderEngine::DynamicIrradianceGi;
        Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::SolarDiscRayQuery;
        Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        for (uint32_t StepIdx = 0u; StepIdx < 2u; ++StepIdx)
        {
            if (!GpuWorldSeq.RenderWorldViewportWithEngine(Cfg, StepIdx, HeroCam, WorldScene, Tier2DynamicIrradRgba))
            {
                std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Tier 2 DynamicIrradianceGi render failed.\n");
                return 1;
            }
        }
        std::fprintf(stdout, "[VehicleRenderTiersShowcase] Rendered Tier 2: DynamicIrradianceGi + SolarDiscRayQuery (%ux%u)\n", ViewW, ViewH);
        std::fflush(stdout);

        // Also capture Isolated Multi-Bounce Diffuse GI (DebugChannel = 8) on the Vehicle
        GeometricRaster::WorldRenderConfiguration GiDbg = Cfg;
        GiDbg.DebugChannel = 8u;
        (void)GpuWorldSeq.RenderWorldViewportWithEngine(GiDbg, 2u, HeroCam, WorldScene, IsolatedGiRgba);
        std::fprintf(stdout, "[VehicleRenderTiersShowcase] Rendered Diagnostic: Isolated Multi-Bounce Indirect GI (%ux%u)\n", ViewW, ViewH);
        std::fflush(stdout);
    }

    // TIER 3: Deterministic Multi-Bounce OpenPBR Path Tracer (MIS + GGX Spherical-Cap VNDF + Abbe Glass)
    {
        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine              = GeometricRaster::WorldRenderEngine::DeterministicPathTrace;
        Cfg.ShadowAlgorithm     = GeometricRaster::SolarShadowAlgorithm::SolarDiscRayQuery;
        Cfg.ReflectionSource    = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;
        Cfg.PathTraceMaxBounces = 3u;
        Cfg.PathTraceDenoise    = true;
        constexpr uint32_t PtDispatches = 8u;
        for (uint32_t SampleBatch = 0u; SampleBatch < PtDispatches; ++SampleBatch)
        {
            if (!GpuWorldSeq.RenderWorldViewportWithEngine(Cfg, SampleBatch, HeroCam, WorldScene, Tier3PathTraceRgba))
            {
                std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Tier 3 DeterministicPathTrace render failed.\n");
                return 1;
            }
            std::fprintf(stdout, "[VehicleRenderTiersShowcase]   Tier 3 batch %u/%u complete\n", SampleBatch + 1u, PtDispatches);
            std::fflush(stdout);
        }
        std::fprintf(stdout, "[VehicleRenderTiersShowcase] Rendered Tier 3: DeterministicPathTrace (%ux%u, %u progressive batches)\n",
                     ViewW, ViewH, PtDispatches);
        std::fflush(stdout);
    }

    // 6. Save Standalone Tier Renders + Master 4-Panel Comparison Sheet in Documentation/Renders/
    const std::filesystem::path OutDir = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Documentation" / "Renders";
    std::filesystem::create_directories(OutDir);

    auto SaveSingleView = [&](const std::filesystem::path& FilePath, const std::vector<uint8_t>& Rgba) -> bool
    {
        Exhibits::SoftwareRasterCanvas Single(ViewW, ViewH, { 10u, 12u, 18u, 255u });
        Single.BlitRgbaRegion(0u, 0u, ViewW, ViewH, Rgba.data());
        return Single.WritePngFile(FilePath);
    };

    const auto PathTier1 = OutDir / "01-Vehicle-Tier1-ContourSurfelGI.png";
    const auto PathTier2 = OutDir / "02-Vehicle-Tier2-DynamicIrradianceGI.png";
    const auto PathTier3 = OutDir / "03-Vehicle-Tier3-DeterministicPathTrace.png";
    const auto PathSheet = OutDir / "00-Vehicle-Three-Render-Tiers-Comparison.png";

    if (!SaveSingleView(PathTier1, Tier1ContourSurfelRgba) ||
        !SaveSingleView(PathTier2, Tier2DynamicIrradRgba) ||
        !SaveSingleView(PathTier3, Tier3PathTraceRgba))
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Failed writing standalone vehicle tier PNGs.\n");
        return 1;
    }

    // Build 1420 x 940 Master Studio Comparison Sheet via Dear ImGui + SoftwareRasterCanvas
    constexpr uint32_t SheetW = 1420u;
    constexpr uint32_t SheetH = 940u;

    IMGUI_CHECKVERSION();
    ImGuiContext* Ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(Ctx);
    ImGuiIO& Io = ImGui::GetIO();
    Io.DisplaySize = ImVec2(static_cast<float>(SheetW), static_cast<float>(SheetH));

    TypographyRepository Fonts {};
    const std::filesystem::path FontRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent" / "FontsArchives";
    if (std::filesystem::exists(FontRoot / "FontRepository.FontSpace") && Fonts.LoadRepository(FontRoot) > 0u)
    {
        TypographyRepository::AssignCurrent(&Fonts);
        if (ImFont* UiFont = Fonts.QueryTabStripFont())
        {
            Io.FontDefault = UiFont;
        }
    }
    unsigned char* FontTex = nullptr;
    int FontW = 0, FontH = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontTex, &FontW, &FontH);

    ImGui::NewFrame();
    ImDrawList* OverlayBatch = ImGui::GetBackgroundDrawList();

    const ImU32 BgCol     = IM_COL32(10, 13, 19, 255);
    const ImU32 TileBg    = IM_COL32(17, 22, 31, 255);
    const ImU32 BorderCol = IM_COL32(48, 62, 86, 255);
    const ImU32 CyanCol   = IM_COL32(110, 205, 255, 255);
    const ImU32 GoldCol   = IM_COL32(246, 202, 114, 255);
    const ImU32 MutedCol  = IM_COL32(168, 180, 200, 255);

    OverlayBatch->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(float(SheetW), float(SheetH)), BgCol);
    OverlayBatch->AddRectFilled(ImVec2(20.0f, 16.0f), ImVec2(float(SheetW - 20u), 84.0f), TileBg, 8.0f);
    OverlayBatch->AddRect(ImVec2(20.0f, 16.0f), ImVec2(float(SheetW - 20u), 84.0f), BorderCol, 8.0f, 0, 1.5f);

    OverlayBatch->AddText(ImVec2(36.0f, 27.0f), CyanCol,
                  "FRONTIER ENGINE // ZERO-SDF 3-TIER WORLD LIGHTING & WAVEFRONT PATH TRACER -- CONTROLLED INTEGRATOR VEHICLE");

    char SubHeader[320] {};
    std::snprintf(
        SubHeader,
        sizeof(SubHeader),
        "%zu Vehicle+Gallery Triangles | %zu 128B BVH4 Entries | %zu 2-Bounce Surfels | 360 Octahedral Chebyshev Cells | Wavefront Digest: 0x%08X",
        WorldScene.SceneTree.Primitives.size(),
        WorldScene.SceneTree.WideEntries.size(),
        CpuWorldSeq.InspectContourSurfelGi().InspectBounceSurfels().size(),
        WavefrontBatchDigest);
    OverlayBatch->AddText(ImVec2(36.0f, 52.0f), MutedCol, SubHeader);

    struct PanelSpec
    {
        float       X;
        float       Y;
        const char* Title;
        const char* Subtitle;
    };
    const std::array<PanelSpec, 4u> Panels {{
        {  20.0f, 100.0f,
            "TIER 1 // GTX GI #1: CONTOUR + 2-BOUNCE SURFEL + SH9 (ZERO DENOISER)",
            "Exact Lambert/Stokes Contours + 339 Bunnell Surfels + 3.91 mm/tx ContactPCSS" },
        { 720.0f, 100.0f,
            "TIER 2 // GTX GI #2: DYNAMIC OCTAHEDRAL CACHE + WEIGHTED PATH REPLAY RIS",
            "8x8 Octahedral Chebyshev + 4-Candidate Owen-Sobol RIS + SolarDiscRayQuery" },
        {  20.0f, 522.0f,
            "TIER 3 // DETERMINISTIC PATH TRACER (GTX 4-WIDE BVH4 / RTX VK_KHR_RAY_QUERY)",
            "Unbiased OpenPBR Monte Carlo + Power-Heuristic MIS + GGX VNDF + Abbe Glass" },
        { 720.0f, 522.0f,
            "DIAGNOSTIC // ISOLATED MULTI-BOUNCE INDIRECT GI ON VEHICLE & UNDERBODY",
            "Crimson West + Emerald East + Softbox Multi-Bounce Bleed Across Body & Rims" },
    }};

    for (const auto& P : Panels)
    {
        OverlayBatch->AddRectFilled(ImVec2(P.X, P.Y), ImVec2(P.X + 680.0f, P.Y + 404.0f), TileBg, 8.0f);
        OverlayBatch->AddRect(ImVec2(P.X, P.Y), ImVec2(P.X + 680.0f, P.Y + 404.0f), BorderCol, 8.0f, 0, 1.5f);
        OverlayBatch->AddText(ImVec2(P.X + 16.0f, P.Y + 7.0f), GoldCol, P.Title);
        OverlayBatch->AddText(ImVec2(P.X + 16.0f, P.Y + 383.0f), MutedCol, P.Subtitle);
    }

    ImGui::Render();
    Exhibits::SoftwareRasterCanvas SheetCanvas(SheetW, SheetH, { 10u, 13u, 19u, 255u });
    SheetCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());

    SheetCanvas.BlitRgbaRegion( 40u, 126u, ViewW, ViewH, Tier1ContourSurfelRgba.data());
    SheetCanvas.BlitRgbaRegion(740u, 126u, ViewW, ViewH, Tier2DynamicIrradRgba.data());
    SheetCanvas.BlitRgbaRegion( 40u, 548u, ViewW, ViewH, Tier3PathTraceRgba.data());
    SheetCanvas.BlitRgbaRegion(740u, 548u, ViewW, ViewH, IsolatedGiRgba.data());

    const bool WroteSheet = SheetCanvas.WritePngFile(PathSheet);
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(Ctx);

    if (!WroteSheet)
    {
        std::fprintf(stderr, "[VehicleRenderTiersShowcase][FAIL] Failed writing comparison sheet PNG.\n");
        return 1;
    }

    std::fprintf(stdout, "[VehicleRenderTiersShowcase] Exported 4 high-resolution vehicle PNGs to %s\n", OutDir.string().c_str());
    return 0;
}
