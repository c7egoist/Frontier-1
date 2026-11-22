/*============================================================================================================================================
                                            VIEWPORTCACHEPICKINGVALIDATION.CPP
============================================================================================================================================*/
// End-to-end validation of the viewport stutter, disk cache, and exact picking work:
//  1) Derived disk asset cache (.cpy1 / .wbvh4 / .sdv1): cold bake timings vs millisecond warm loads
//  2) ClusterPyramid 'CPY1' v4 per-material coarsest-allowed LOD caps survive serialization
//  3) Zero-SAH-rebuild motion: repeated identical renders never re-run the cluster cut BVH build,
//     rigid translation re-uses the unchanged per-asset cluster-cut BLAS through the two-level cache
//  4) Static GDF clipmap stability while DynamicClipmapScrolling is disabled
//  5) Volumetric cloud compute dispatch skipped while camera/sun/cloud/wind signature is unchanged
//  6) Exact local BVH4 BLAS ray picking (strictly tighter than a coarse world AABB) plus LMB marquee box selection

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace
{

using namespace Frontier;
using namespace Frontier::GeometricRaster;

struct ValidationChecklist
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++Passed;
            std::printf("[ViewportCachePickingValidation][PASS] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[ViewportCachePickingValidation][FAIL] %s\n", Label);
        }
    }
};

class Stopwatch
{
public:
    void Begin() noexcept { Started = std::chrono::steady_clock::now(); }

    [[nodiscard]] double ElapsedMilliseconds() const noexcept
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
    }

private:
    std::chrono::steady_clock::time_point Started {};
};

// Rebuilds the exact same primary-camera ray the viewport pickers trace (NormalizedU/V in [0, 1]).
void ConstructCameraRay(
    const PerspectiveCameraBasis& Cam,
    float                         NormalizedU,
    float                         NormalizedV,
    Mathematics::Axis3D&          OutOrigin,
    Mathematics::Axis3D&          OutDirection) noexcept
{
    OutOrigin = Cam.CameraOrigin;
    const Mathematics::Axis3D Fwd   = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });

    const float Aspect  = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    const float TanHalf = std::tan(std::clamp(Cam.VerticalFovRadians, 0.10f, 2.60f) * 0.5f);
    const float NdcX    = (NormalizedU * 2.0f - 1.0f) * Aspect * TanHalf;
    const float NdcY    = (1.0f - NormalizedV * 2.0f) * TanHalf;

    OutDirection = Mathematics::NormalizeAxis(Fwd + Right * NdcX + Up * NdcY, Fwd);
}

// Coarse world-AABB test retained only as the reference the exact BLAS picker must beat.
bool IntersectReferenceAabb(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D Dir,
    Mathematics::Axis3D Center,
    Mathematics::Scalar3D HalfExtents) noexcept
{
    float TMin = 0.01f;
    float TMax = 1.0e6f;
    const float Orig[3] = { Origin.X, Origin.Y, Origin.Z };
    const float RayD[3] = { Dir.X, Dir.Y, Dir.Z };
    const float BMin[3] = { Center.X - HalfExtents.X, Center.Y - HalfExtents.Y, Center.Z - HalfExtents.Z };
    const float BMax[3] = { Center.X + HalfExtents.X, Center.Y + HalfExtents.Y, Center.Z + HalfExtents.Z };
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::fabs(RayD[Axis]) < 1.0e-6f)
        {
            if (Orig[Axis] < BMin[Axis] || Orig[Axis] > BMax[Axis])
            {
                return false;
            }
            continue;
        }
        const float InvD = 1.0f / RayD[Axis];
        float       T1   = (BMin[Axis] - Orig[Axis]) * InvD;
        float       T2   = (BMax[Axis] - Orig[Axis]) * InvD;
        if (T1 > T2)
        {
            std::swap(T1, T2);
        }
        TMin = std::max(TMin, T1);
        TMax = std::min(TMax, T2);
        if (TMin > TMax)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const ShowcaseWorldAssetEntry* FindVehicleEntry(const ShowcaseWorldSceneBundle& Bundle) noexcept
{
    for (const ShowcaseWorldAssetEntry& Obj : Bundle.Objects)
    {
        if (Obj.ClusterInstanceIndex == 1u)
        {
            return &Obj;
        }
    }
    return Bundle.Objects.size() >= 2u ? &Bundle.Objects[1u] : nullptr;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;

    ValidationChecklist Checklist {};
    constexpr uint32_t ViewW = 320u;
    constexpr uint32_t ViewH = 200u;

    //--------------------------------------------------------------------------------------------------------------------
    // VALIDATION 1: Derived disk asset cache - cold bake vs warm load
    //--------------------------------------------------------------------------------------------------------------------
    std::error_code Ec {};
    const std::filesystem::path CacheDir  = QueryShowcaseAssetCacheDirectory();
    const std::filesystem::path StashDir  = CacheDir.parent_path() / "AssetCachePreviousStash";
    std::filesystem::remove_all(StashDir, Ec);
    bool CacheStashed = false;
    if (std::filesystem::exists(CacheDir))
    {
        std::filesystem::rename(CacheDir, StashDir, Ec);
        CacheStashed = !Ec;
    }
    std::printf("[ViewportCachePickingValidation] Derived asset cache: %s (previous cache stashed: %s)\n",
        CacheDir.generic_string().c_str(), CacheStashed ? "yes" : "no");

    Stopwatch ColdClock {};
    ResetShowcaseAssetCacheStats();
    ColdClock.Begin();
    ShowcaseWorldSceneBundle ColdBundle {};
    BuildVehicleClearSkyDefaultScene(ColdBundle, ViewW, ViewH);
    const double ColdMilliseconds = ColdClock.ElapsedMilliseconds();
    const ShowcaseAssetCacheStats ColdStats = QueryShowcaseAssetCacheStats();
    std::printf(
        "[ViewportCachePickingValidation] COLD scene build: %.1f ms | .cpy1 bakes=%u | .wbvh4 builds=%u | .sdv1 bakes=%u\n",
        ColdMilliseconds, ColdStats.PyramidBakeCount, ColdStats.BlasBakeCount, ColdStats.SdfBakeCount);

    Stopwatch WarmClock {};
    ResetShowcaseAssetCacheStats();
    WarmClock.Begin();
    ShowcaseWorldSceneBundle WarmBundle {};
    BuildVehicleClearSkyDefaultScene(WarmBundle, ViewW, ViewH);
    const double WarmMilliseconds = WarmClock.ElapsedMilliseconds();
    const ShowcaseAssetCacheStats WarmStats = QueryShowcaseAssetCacheStats();
    std::printf(
        "[ViewportCachePickingValidation] WARM scene build: %.1f ms | .cpy1 hits=%u bakes=%u | .wbvh4 hits=%u builds=%u | .sdv1 hits=%u bakes=%u\n",
        WarmMilliseconds,
        WarmStats.PyramidDiskHitCount, WarmStats.PyramidBakeCount,
        WarmStats.BlasDiskHitCount, WarmStats.BlasBakeCount,
        WarmStats.SdfDiskHitCount, WarmStats.SdfBakeCount);

    Checklist.Verify(ColdStats.PyramidBakeCount > 0u, "Cold build executed the Nanite cluster pyramid bake");
    Checklist.Verify(ColdStats.BlasBakeCount > 0u, "Cold build executed the local BVH4 SAH bake");
    Checklist.Verify(WarmStats.PyramidBakeCount == 0u, "Warm build loaded every ClusterPyramid from .cpy1 (zero QEM bakes)");
    Checklist.Verify(WarmStats.BlasBakeCount == 0u, "Warm build loaded every local BLAS from .wbvh4 (zero SAH builds)");
    Checklist.Verify(WarmStats.SdfBakeCount == 0u, "Warm build loaded every local SDF from .sdv1 (zero voxelisation passes)");
    Checklist.Verify(
        WarmStats.PyramidDiskHitCount > 0u && WarmStats.BlasDiskHitCount > 0u && WarmStats.SdfDiskHitCount > 0u,
        "Warm build reports disk hits for .cpy1, .wbvh4, and .sdv1 simultaneously");
    Checklist.Verify(
        WarmMilliseconds < ColdMilliseconds * 0.80,
        "Warm scene build is strictly faster than the cold bake");
    std::printf(
        "[ViewportCachePickingValidation] Warm speedup: %.2fx (%.1f ms -> %.1f ms)\n",
        WarmMilliseconds > 0.01 ? ColdMilliseconds / WarmMilliseconds : 0.0,
        ColdMilliseconds, WarmMilliseconds);

    // Discard the stash: the cache produced by this run is the canonical one.
    std::filesystem::remove_all(StashDir, Ec);

    ShowcaseWorldSceneBundle Bundle {};
    BuildVehicleClearSkyDefaultScene(Bundle, ViewW, ViewH);
    OutlinerPanel Outliner {};
    RegisterShowcaseGalleryInOutliner(Outliner, Bundle, true);

    const ShowcaseWorldAssetEntry* VehicleObj = FindVehicleEntry(Bundle);
    Checklist.Verify(VehicleObj != nullptr && VehicleObj->ClusterInstanceIndex < Bundle.Scene.ClusterInstances.size(),
        "ControlVehicle asset entry resolved");
    if (VehicleObj == nullptr || VehicleObj->ClusterInstanceIndex >= Bundle.Scene.ClusterInstances.size())
    {
        return 1;
    }
    const ShowcaseWorldAssetEntry& VehicleRef = *VehicleObj;

    //--------------------------------------------------------------------------------------------------------------------
    // VALIDATION 2: ClusterPyramid CPY1 v4 per-material LOD caps
    //--------------------------------------------------------------------------------------------------------------------
    {
        const RigidClusterInstance& Inst = Bundle.Scene.ClusterInstances[VehicleRef.ClusterInstanceIndex];
        SpatialAcceleration::ClusterPyramid Source = *Inst.PyramidAsset;
        Source.ConfigureMaterialCoarsestAllowedLevel(3u, 1u);
        Source.ConfigureMaterialCoarsestAllowedLevel(7u, 2u);
        const std::vector<std::byte> Encoded = Source.SerializeToBinaryBytes();

        uint32_t Magic = 0u;
        uint32_t Version = 0u;
        if (Encoded.size() >= 8u)
        {
            std::memcpy(&Magic, Encoded.data(), sizeof(uint32_t));
            std::memcpy(&Version, Encoded.data() + sizeof(uint32_t), sizeof(uint32_t));
        }
        Checklist.Verify(Magic == 0x31595043u, "ClusterPyramid 'CPY1' magic present in encoded bytes");
        Checklist.Verify(Version == 4u, "ClusterPyramid binary version is 4 (per-material LOD cap trailer)");

        SpatialAcceleration::ClusterPyramid RoundTrip {};
        const bool Decoded = RoundTrip.DeserializeFromBinaryBytes(Encoded);
        Checklist.Verify(Decoded && RoundTrip.Clusters.size() == Source.Clusters.size(),
            "ClusterPyramid v4 round-trip restores the cluster DAG");
        Checklist.Verify(RoundTrip.MaterialCoarsestAllowedLevels[3u] == 1u &&
            RoundTrip.MaterialCoarsestAllowedLevels[7u] == 2u,
            "ClusterPyramid v4 persists per-material coarsest-allowed LOD caps");
        bool AllCapsPreserved = true;
        uint32_t CappedSlotCount = 0u;
        for (uint32_t Slot = 0u; Slot < 256u; ++Slot)
        {
            AllCapsPreserved = AllCapsPreserved &&
                (RoundTrip.MaterialCoarsestAllowedLevels[Slot] == Source.MaterialCoarsestAllowedLevels[Slot]);
            CappedSlotCount += (RoundTrip.MaterialCoarsestAllowedLevels[Slot] != 0xFFFFFFFFu) ? 1u : 0u;
        }
        Checklist.Verify(AllCapsPreserved,
            "ClusterPyramid v4 round-trip preserves all 256 per-material cap slots byte-for-byte");
        Checklist.Verify(CappedSlotCount >= 2u,
            "ClusterPyramid v4 round-trip keeps the capped material slots capped");
        std::printf(
            "[ViewportCachePickingValidation] CPY1 v%u: %.1f KB | material 3 cap=%u, material 7 cap=%u, material 4 cap=0x%08X\n",
            Version, static_cast<double>(Encoded.size()) / 1024.0,
            RoundTrip.MaterialCoarsestAllowedLevels[3u],
            RoundTrip.MaterialCoarsestAllowedLevels[7u],
            RoundTrip.MaterialCoarsestAllowedLevels[4u]);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // VALIDATION 3: Exact BVH4 BLAS picking strictly tighter than the coarse world AABB
    //--------------------------------------------------------------------------------------------------------------------
    {
        const PerspectiveCameraBasis Cam = Bundle.EditorCamera;

        float CenterU = 0.5f;
        float CenterV = 0.5f;
        {
            Mathematics::Axis3D ToCenter = VehicleRef.CenterWorld - Cam.CameraOrigin;
            Mathematics::Axis3D Fwd      = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
            Mathematics::Axis3D Right    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
            Mathematics::Axis3D Up       = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });
            const float Depth = Mathematics::EvaluateDot(ToCenter, Fwd);
            if (Depth > 1.0e-3f)
            {
                const float Aspect  = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
                const float TanHalf = std::tan(std::clamp(Cam.VerticalFovRadians, 0.10f, 2.60f) * 0.5f);
                const float NdcX = Mathematics::EvaluateDot(ToCenter, Right) / (Depth * std::max(1.0e-5f, Aspect * TanHalf));
                const float NdcY = Mathematics::EvaluateDot(ToCenter, Up) / (Depth * std::max(1.0e-5f, TanHalf));
                CenterU = std::clamp(0.5f * (NdcX + 1.0f), 0.0f, 1.0f);
                CenterV = std::clamp(0.5f * (1.0f - NdcY), 0.0f, 1.0f);
            }
        }

        const OutlinerSlotToken CenterPick = PickShowcaseGalleryEntryAtUv(Outliner, Bundle, CenterU, CenterV);
        Checklist.Verify(CenterPick == VehicleRef.SlotToken,
            "Exact BLAS pick returns the ControlVehicle at its projected center");
        std::printf("[ViewportCachePickingValidation] Projected vehicle center UV = (%.3f, %.3f) -> token %llu\n",
            CenterU, CenterV, static_cast<unsigned long long>(CenterPick.Identifier));

        const OutlinerSlotToken SkyPick = PickShowcaseGalleryEntryAtUv(Outliner, Bundle, 0.015f, 0.015f);
        Checklist.Verify(!SkyPick.Valid(), "Exact BLAS pick returns no token for empty sky pixels");

        const Mathematics::Scalar3D HalfExt {
            std::max(0.04f, 0.5f * VehicleRef.DimensionsMeters.X),
            std::max(0.04f, 0.5f * VehicleRef.DimensionsMeters.Y),
            std::max(0.04f, 0.5f * VehicleRef.DimensionsMeters.Z),
        };

        uint32_t AabbHitSamples       = 0u;
        uint32_t AabbMissedByBlas     = 0u;
        uint32_t BlasVehicleHits      = 0u;
        float    DivergenceU          = 0.0f;
        float    DivergenceV          = 0.0f;
        constexpr uint32_t GridU = 96u;
        constexpr uint32_t GridV = 64u;
        for (uint32_t GV = 0u; GV < GridV; ++GV)
        {
            for (uint32_t GU = 0u; GU < GridU; ++GU)
            {
                const float U = (static_cast<float>(GU) + 0.5f) / static_cast<float>(GridU);
                const float V = (static_cast<float>(GV) + 0.5f) / static_cast<float>(GridV);
                Mathematics::Axis3D RayOrigin {};
                Mathematics::Axis3D RayDir {};
                ConstructCameraRay(Cam, U, V, RayOrigin, RayDir);
                if (!IntersectReferenceAabb(RayOrigin, RayDir, VehicleRef.CenterWorld, HalfExt))
                {
                    continue;
                }
                ++AabbHitSamples;
                const OutlinerSlotToken Pick = PickShowcaseGalleryEntryAtUv(Outliner, Bundle, U, V);
                if (Pick == VehicleRef.SlotToken)
                {
                    ++BlasVehicleHits;
                }
                else if (AabbMissedByBlas == 0u)
                {
                    ++AabbMissedByBlas;
                    DivergenceU = U;
                    DivergenceV = V;
                }
                else
                {
                    ++AabbMissedByBlas;
                }
            }
        }
        std::printf(
            "[ViewportCachePickingValidation] Pick grid %ux%u: world-AABB rays=%u | exact BLAS vehicle hits=%u | AABB-only false hits=%u (first at UV %.3f, %.3f)\n",
            GridU, GridV, AabbHitSamples, BlasVehicleHits, AabbMissedByBlas, DivergenceU, DivergenceV);
        Checklist.Verify(AabbHitSamples > 0u, "Reference world-AABB sweep intersects the ControlVehicle volume");
        Checklist.Verify(BlasVehicleHits > 0u, "Exact BLAS picking hits the ControlVehicle across the silhouette sweep");
        Checklist.Verify(AabbMissedByBlas > 0u,
            "Exact BLAS picking rejects rays that only touch the coarse world AABB (strictly tighter than AABB)");
        const double AabbOnlyFraction = AabbHitSamples > 0u
            ? static_cast<double>(AabbMissedByBlas) / static_cast<double>(AabbHitSamples)
            : 0.0;
        Checklist.Verify(AabbOnlyFraction > 0.10,
            "More than 10% of coarse AABB hits are rejected by the exact mesh picker");

        //----------------------------------------------------------------------------------------------------------------
        // VALIDATION 4: LMB marquee box selection
        //----------------------------------------------------------------------------------------------------------------
        const std::vector<OutlinerSlotToken> WideBox = PickShowcaseGalleryEntriesInUvBox(Outliner, Bundle, 0.02f, 0.02f, 0.98f, 0.98f);
        bool WideBoxHasVehicle = false;
        for (const OutlinerSlotToken Token : WideBox)
        {
            WideBoxHasVehicle = WideBoxHasVehicle || (Token == VehicleRef.SlotToken);
        }
        Checklist.Verify(WideBoxHasVehicle, "Marquee box selection sweeps the ControlVehicle");

        const std::vector<OutlinerSlotToken> NarrowBox = PickShowcaseGalleryEntriesInUvBox(Outliner, Bundle, 0.0f, 0.0f, 0.03f, 0.03f);
        Checklist.Verify(NarrowBox.empty(), "Marquee box selection returns no tokens for an empty sky corner");
        std::printf(
            "[ViewportCachePickingValidation] Marquee: full-view box=%zu tokens (vehicle included=%s) | sky corner box=%zu tokens\n",
            WideBox.size(), WideBoxHasVehicle ? "yes" : "no", NarrowBox.size());

        // Every marquee token must be a visible Showcase object (no phantom or hidden selections).
        bool AllTokensVisible = true;
        for (const OutlinerSlotToken Token : WideBox)
        {
            AllTokensVisible = AllTokensVisible && Token.Valid() && Outliner.QueryEffectiveVisibility(Token);
        }
        Checklist.Verify(AllTokensVisible, "Every marquee token is a visible Showcase slot");
    }

    //--------------------------------------------------------------------------------------------------------------------
    // VALIDATION 5: GPU cache stability - cut signature, per-asset BLAS reuse, static clipmap, cloud dispatch skip
    //--------------------------------------------------------------------------------------------------------------------
    std::string VkDiagnostic {};
    VulkanInterchange VkDevice {};
    if (!VkDevice.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR), &VkDiagnostic))
    {
        std::printf("[ViewportCachePickingValidation][SKIP] No Vulkan ICD available (%s)\n", VkDiagnostic.c_str());
        std::printf("[ViewportCachePickingValidation] Summary: %u passed, %u failed\n", Checklist.Passed, Checklist.Failed);
        return Checklist.Failed == 0u ? 0 : 1;
    }

    VulkanWorldRenderSequence GpuSeq {};
    const std::filesystem::path CompiledShaderDir(FRONTIER_COMPILED_SHADER_DIR);
    if (!GpuSeq.InitializeSequence(VkDevice, CompiledShaderDir))
    {
        std::fprintf(stderr, "[ViewportCachePickingValidation][FAIL] VulkanWorldRenderSequence initialization failed\n");
        VkDevice.TeardownInterchange();
        return 1;
    }
    Checklist.Verify(true, "VulkanWorldRenderSequence initialized");

    // Enable the volumetric cloud deck through its Outliner row so later live syncs keep it visible,
    // then tune the deck so the signature-skip path is actually exercised.
    if (OutlinerRecordEntry* CloudsRow = Outliner.AccessEntry(Bundle.VolumetricCloudsToken))
    {
        CloudsRow->VisibilityEnabled = true;
    }
    (void)SynchronizeShowcaseGalleryFromOutliner(Outliner, Bundle);
    Bundle.Scene.Atmosphere.Clouds.CloudsEnabled            = true;
    Bundle.Scene.Atmosphere.Clouds.CloudFraction            = 0.55f;
    Bundle.Scene.Atmosphere.Clouds.WindSpeedMetersPerSecond = 2.0f;
    Bundle.Scene.Atmosphere.Clouds.FollowWindEnabled        = true;
    GpuSeq.SynchronizeSceneResources(Bundle.Scene, true);

    const auto RenderStep = [&](const PerspectiveCameraBasis& Cam) -> bool
    {
        WorldRenderConfiguration Config {};
        Config.Engine           = WorldRenderEngine::RealTimeGiGtxLowEnd;
        Config.NaniteLodEnabled = true;
        Config.NanitePixelError = 1.25f;
        Config.DebugChannel     = 0u;
        std::vector<uint8_t> Rgba {};
        return GpuSeq.RenderWorldViewportWithEngine(Config, 0u, Cam, Bundle.Scene, Rgba) &&
            Rgba.size() == static_cast<std::size_t>(ViewW) * ViewH * 4u;
    };

    PerspectiveCameraBasis CameraA = Bundle.EditorCamera;
    CameraA.ViewportWidthPx  = ViewW;
    CameraA.ViewportHeightPx = ViewH;

    PerspectiveCameraBasis CameraB = CameraA;
    CameraB.CameraOrigin = { CameraA.CameraOrigin.X + 2.4f, CameraA.CameraOrigin.Y - 1.1f, CameraA.CameraOrigin.Z + 1.3f };

    const bool StepOneOk = RenderStep(CameraA);
    Checklist.Verify(StepOneOk, "GPU viewport rendered the first step");
    const uint32_t CloudAfterOne   = GpuSeq.QueryCloudLayerDispatchCount();
    const uint32_t BuildsAfterOne  = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t RebuildsAfterOne = GpuSeq.QueryNaniteLodStats().GeometryRebuildCount;
    const auto SdfStatsFirst       = GpuSeq.QueryGlobalDistanceFieldStats();

    const bool StepTwoOk = RenderStep(CameraA);
    Checklist.Verify(StepTwoOk, "GPU viewport rendered the identical repeat step");
    const uint32_t CloudAfterTwo    = GpuSeq.QueryCloudLayerDispatchCount();
    const uint32_t BuildsAfterTwo   = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t RebuildsAfterTwo = GpuSeq.QueryNaniteLodStats().GeometryRebuildCount;

    std::printf(
        "[ViewportCachePickingValidation] Cloud dispatches: step1=%u | identical repeat=%u | BLAS builds: %u -> %u | cut rebuilds: %u -> %u\n",
        CloudAfterOne, CloudAfterTwo, BuildsAfterOne, BuildsAfterTwo, RebuildsAfterOne, RebuildsAfterTwo);

    Checklist.Verify(CloudAfterOne >= 1u, "Volumetric cloud compute dispatched on the first step");
    Checklist.Verify(CloudAfterTwo == CloudAfterOne,
        "Volumetric cloud compute SKIPPED on an identical camera/sun/cloud/wind step (CachedCloudLayerSignature)");
    Checklist.Verify(BuildsAfterTwo == BuildsAfterOne,
        "Identical repeat step re-used the cached cluster-cut BVH (zero new SAH builds)");
    Checklist.Verify(RebuildsAfterTwo == RebuildsAfterOne,
        "Identical repeat step re-used the cached Nanite cut (zero geometry rebuilds)");

    // Static clipmap: moving only the camera must not scroll the GDF cascades by default.
    const bool StepThreeOk = RenderStep(CameraB);
    Checklist.Verify(StepThreeOk, "GPU viewport rendered a camera-moved step");
    const auto SdfStatsSecond      = GpuSeq.QueryGlobalDistanceFieldStats();
    const uint32_t CloudAfterMove  = GpuSeq.QueryCloudLayerDispatchCount();
    Checklist.Verify(
        std::fabs(SdfStatsSecond.FineCascadeCenterWorld[0] - SdfStatsFirst.FineCascadeCenterWorld[0]) < 1.0e-4f &&
        std::fabs(SdfStatsSecond.FineCascadeCenterWorld[1] - SdfStatsFirst.FineCascadeCenterWorld[1]) < 1.0e-4f &&
        std::fabs(SdfStatsSecond.FineCascadeCenterWorld[2] - SdfStatsFirst.FineCascadeCenterWorld[2]) < 1.0e-4f,
        "Static GDF fine cascade stays pinned while DynamicClipmapScrolling is disabled");
    Checklist.Verify(CloudAfterMove > CloudAfterTwo,
        "Camera motion invalidates the cloud signature and dispatches the cloud compute again");
    std::printf(
        "[ViewportCachePickingValidation] Fine cascade center: (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f) after camera move\n",
        SdfStatsFirst.FineCascadeCenterWorld[0], SdfStatsFirst.FineCascadeCenterWorld[1], SdfStatsFirst.FineCascadeCenterWorld[2],
        SdfStatsSecond.FineCascadeCenterWorld[0], SdfStatsSecond.FineCascadeCenterWorld[1], SdfStatsSecond.FineCascadeCenterWorld[2]);

    // Rigid gizmo translation: the unmoved floor asset must reuse its cached per-asset BLAS.
    const uint32_t BuildsBeforeMove  = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t ReuseBeforeMove   = GpuSeq.QueryAssetCutBlasReuseCount();
    bool NeedsCpuRebuild = false;
    if (OutlinerRecordEntry* VehicleRow = Outliner.AccessEntry(VehicleRef.SlotToken))
    {
        VehicleRow->PositionX += 0.35f;
    }
    (void)SynchronizeShowcaseGallerySceneLive(Outliner, Bundle, NeedsCpuRebuild, true);
    Checklist.Verify(!NeedsCpuRebuild, "Rigid gizmo translation defers the full CPU scene BVH rebuild for the GPU path");

    const bool StepFourOk = RenderStep(CameraB);
    Checklist.Verify(StepFourOk, "GPU viewport rendered the rigid-translated step");
    const uint32_t BuildsAfterMove = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t ReuseAfterMove  = GpuSeq.QueryAssetCutBlasReuseCount();
    std::printf(
        "[ViewportCachePickingValidation] Rigid translation: new BLAS builds=%u (delta) | cached BLAS reuses=%u (delta)\n",
        BuildsAfterMove - BuildsBeforeMove, ReuseAfterMove - ReuseBeforeMove);
    Checklist.Verify(BuildsAfterMove - BuildsBeforeMove <= 1u,
        "Rigid translation rebuilds at most the moved asset BLAS (two-level AssetCutBlasCache)");
    Checklist.Verify(ReuseAfterMove > ReuseBeforeMove,
        "Unmoved assets re-use their cached per-asset cluster-cut BLAS after rigid motion");

    // Material-only edit: zero SAH rebuilds through SynchronizeMaterialEntriesOnly.
    // The first authored colour promotes the instance to a dedicated material override (a genuine
    // cut-signature change), so the zero-rebuild assertion is made on the follow-up colour edit.
    const bool ColorAssigned = AssignShowcaseEntryBaseColor(
        Outliner, Bundle, VehicleRef.SlotToken, ColorRgba{ 42u, 96u, 210u, 255u }, "ValidationBluePaint");
    Checklist.Verify(ColorAssigned, "Zero-rebuild material edit assigned a new base colour to the ControlVehicle");
    GpuSeq.SynchronizeMaterialEntriesOnly(Bundle.Scene);
    const bool StepFiveOk = RenderStep(CameraB);
    Checklist.Verify(StepFiveOk, "GPU viewport rendered the material-edited step");

    const uint32_t BuildsBeforeMaterial  = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t RebuildsBeforeMaterial = GpuSeq.QueryNaniteLodStats().GeometryRebuildCount;
    const bool ColorReassigned = AssignShowcaseEntryBaseColor(
        Outliner, Bundle, VehicleRef.SlotToken, ColorRgba{ 210u, 64u, 32u, 255u }, "ValidationOrangePaint");
    Checklist.Verify(ColorReassigned, "Follow-up colour edit re-authored the dedicated material slot");
    GpuSeq.SynchronizeMaterialEntriesOnly(Bundle.Scene);
    const bool StepSixOk = RenderStep(CameraB);
    Checklist.Verify(StepSixOk, "GPU viewport rendered the re-coloured step");
    const uint32_t BuildsAfterMaterial = GpuSeq.QueryAssetCutBlasBuildCount();
    const uint32_t RebuildsAfterMaterial = GpuSeq.QueryNaniteLodStats().GeometryRebuildCount;
    Checklist.Verify(BuildsAfterMaterial == BuildsBeforeMaterial,
        "Material-only synchronisation triggers zero new cluster-cut BLAS builds");
    Checklist.Verify(RebuildsAfterMaterial == RebuildsBeforeMaterial,
        "Material-only synchronisation keeps the Nanite cut signature intact");
    std::printf(
        "[ViewportCachePickingValidation] Material-only edit: BLAS builds %u -> %u | cut rebuilds %u -> %u\n",
        BuildsBeforeMaterial, BuildsAfterMaterial, RebuildsBeforeMaterial, RebuildsAfterMaterial);

    // Explicit bake triggers must be intrusive when deliberately requested.
    const uint32_t CloudBeforeRefresh = GpuSeq.QueryCloudLayerDispatchCount();
    GpuSeq.RequestStaticGiBake();
    GpuSeq.RequestSkyCloudCacheRefresh();
    const bool StepSevenOk = RenderStep(CameraB);
    Checklist.Verify(StepSevenOk, "GPU viewport rendered the explicitly re-baked step");
    const uint32_t CloudAfterBake = GpuSeq.QueryCloudLayerDispatchCount();
    std::printf(
        "[ViewportCachePickingValidation] Cloud dispatches: before refresh=%u | after refresh=%u\n",
        CloudBeforeRefresh, CloudAfterBake);
    Checklist.Verify(CloudAfterBake > CloudBeforeRefresh,
        "RequestSkyCloudCacheRefresh forces the cloud compute dispatch even for an unchanged step");

    GpuSeq.TeardownSequence();
    VkDevice.TeardownInterchange();

    std::printf("[ViewportCachePickingValidation] Summary: %u passed, %u failed\n", Checklist.Passed, Checklist.Failed);
    return Checklist.Failed == 0u ? 0 : 1;
}
