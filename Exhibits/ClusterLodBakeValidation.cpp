/*============================================================================================================================================
                                                     CLUSTERLODBAKEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for the Nanite-like Cluster LOD bake: per-asset final level, material-slot preservation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace
{

[[nodiscard]] bool VerifyCheck(bool Passed, const char* Label) noexcept
{
    if (!Passed)
    {
        std::fprintf(stderr, "[ClusterLodBakeValidation] FAILED: %s\n", Label);
    }
    return Passed;
}

// 20x20 quad grid (800 triangles): left half material slot 3, right half material slot 7
[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildTwoMaterialGrid() noexcept
{
    using namespace Frontier;
    GeometrySpace::PolygonNetwork Network {};
    Network.NetworkTitle = "TwoMaterialGrid";

    constexpr uint32_t Cells = 20u;
    for (uint32_t Y = 0u; Y <= Cells; ++Y)
    {
        for (uint32_t X = 0u; X <= Cells; ++X)
        {
            (void)Network.AppendVertex({ static_cast<float>(X) - 10.0f, static_cast<float>(Y) - 10.0f, 0.0f });
        }
    }
    const uint32_t Stride = Cells + 1u;
    for (uint32_t Y = 0u; Y < Cells; ++Y)
    {
        for (uint32_t X = 0u; X < Cells; ++X)
        {
            const uint32_t V0 = Y * Stride + X;
            const uint32_t Loop[4] = { V0, V0 + 1u, V0 + 1u + Stride, V0 + Stride };
            const uint16_t Material = (X < Cells / 2u) ? 3u : 7u;
            (void)Network.AppendFace(Loop, {}, {}, 0u, Material);
        }
    }
    Network.LinkOppositeEdges();
    Network.RecomputeNormals();
    return Network;
}

// Closed watertight subdivided torus (~2048 triangles) matching GeometryCompilerValidation.mjs
[[nodiscard]] Frontier::GeometrySpace::PolygonNetwork BuildClosedWatertightTorus(
    uint32_t MajorRings = 32u,
    uint32_t MinorRings = 32u,
    float    MajorRadius = 2.4f,
    float    MinorRadius = 0.85f) noexcept
{
    using namespace Frontier;
    GeometrySpace::PolygonNetwork Network {};
    Network.NetworkTitle = "ClosedWatertightTorus";
    constexpr float TwoPi = 6.28318530717958647692f;

    for (uint32_t Maj = 0u; Maj < MajorRings; ++Maj)
    {
        const float Phi  = TwoPi * static_cast<float>(Maj) / static_cast<float>(MajorRings);
        const float CosP = std::cos(Phi);
        const float SinP = std::sin(Phi);
        for (uint32_t Min = 0u; Min < MinorRings; ++Min)
        {
            const float Theta = TwoPi * static_cast<float>(Min) / static_cast<float>(MinorRings);
            const float RingR = MajorRadius + MinorRadius * std::cos(Theta);
            (void)Network.AppendVertex({
                RingR * CosP,
                RingR * SinP,
                MinorRadius * std::sin(Theta),
            });
        }
    }

    for (uint32_t Maj = 0u; Maj < MajorRings; ++Maj)
    {
        const uint32_t NextMaj = (Maj + 1u) % MajorRings;
        for (uint32_t Min = 0u; Min < MinorRings; ++Min)
        {
            const uint32_t NextMin = (Min + 1u) % MinorRings;
            const uint32_t V00 = Maj * MinorRings + Min;
            const uint32_t V10 = NextMaj * MinorRings + Min;
            const uint32_t V11 = NextMaj * MinorRings + NextMin;
            const uint32_t V01 = Maj * MinorRings + NextMin;
            const uint32_t Loop[4] = { V00, V10, V11, V01 };
            (void)Network.AppendFace(Loop, {}, {}, 0u, 1u);
        }
    }
    Network.LinkOppositeEdges();
    Network.RecomputeNormals();
    return Network;
}

struct CutManifoldDiagnostics
{
    uint32_t OpenEdgeCount        = 0u;
    uint32_t NonManifoldEdgeCount = 0u;
    uint32_t TotalEdges           = 0u;
};

[[nodiscard]] CutManifoldDiagnostics InspectActiveCutManifoldEdges(
    const Frontier::SpatialAcceleration::ClusterPyramid& Pyramid,
    std::span<const uint32_t>                            ActiveClusters)
{
    std::unordered_map<uint64_t, uint32_t> EdgeUses;
    for (const uint32_t CIdx : ActiveClusters)
    {
        const auto& Header = Pyramid.Clusters[CIdx];
        for (uint32_t Off = 0u; Off < Header.TriangleCount; ++Off)
        {
            const auto& Tri = Pyramid.Triangles[Header.TriangleStartOffset + Off];
            const uint32_t V[3] = { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 };
            for (std::size_t Side = 0u; Side < 3u; ++Side)
            {
                const uint64_t Lo = std::min(V[Side], V[(Side + 1u) % 3u]);
                const uint64_t Hi = std::max(V[Side], V[(Side + 1u) % 3u]);
                EdgeUses[(Lo << 32u) | Hi] += 1u;
            }
        }
    }
    CutManifoldDiagnostics Diag {};
    Diag.TotalEdges = static_cast<uint32_t>(EdgeUses.size());
    for (const auto& [Key, Uses] : EdgeUses)
    {
        (void)Key;
        if (Uses == 1u)
        {
            ++Diag.OpenEdgeCount;
        }
        else if (Uses > 2u)
        {
            ++Diag.NonManifoldEdgeCount;
        }
    }
    return Diag;
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::SpatialAcceleration;

    const GeometrySpace::PolygonNetwork Grid = BuildTwoMaterialGrid();
    if (!VerifyCheck(Grid.Faces.size() == 400u, "grid built (400 faces)"))
    {
        return 1;
    }

    // 1. Bake deep with material-boundary locking and no runtime cap
    ClusterPyramid Pyramid {};
    ClusterLodBakeConfiguration Bake {};
    Bake.MaximumLevels          = 4u;
    Bake.CoarsestAllowedLevel   = 0xFFFFFFFFu;
    Bake.LockMaterialBoundaries = true;
    if (!VerifyCheck(Pyramid.BuildFromPolygonNetwork(Grid, Bake), "LOD bake succeeded") ||
        !VerifyCheck(Pyramid.HierarchyDepth >= 2u, "LOD bake produced coarser levels"))
    {
        return 1;
    }

    const auto Summaries = Pyramid.SummarizeLevels();
    if (!VerifyCheck(Summaries.size() == Pyramid.HierarchyDepth, "level summaries cover bake depth"))
    {
        return 1;
    }
    for (const auto& Summary : Summaries)
    {
        bool HasMat3 = false;
        bool HasMat7 = false;
        for (const uint16_t Ordinal : Summary.MaterialOrdinals)
        {
            HasMat3 = HasMat3 || (Ordinal == 3u);
            HasMat7 = HasMat7 || (Ordinal == 7u);
            if (!VerifyCheck(Ordinal == 3u || Ordinal == 7u, "no foreign material slot appeared at any LOD level"))
            {
                return 1;
            }
        }
        if (!VerifyCheck(HasMat3 && HasMat7, "both material slots survive at every LOD level"))
        {
            return 1;
        }
    }
    if (!VerifyCheck(
            Summaries.back().TriangleCount < Summaries.front().TriangleCount,
            "coarsest level is actually simplified"))
    {
        return 1;
    }

    // 1b. Material-pure clusters: every cluster at every level holds faces of ONE material slot
    //     only (clusters are never shared between materials), and each triangle keeps the
    //     material of the source face it came from (face identity via SourceFaceIndex).
    for (const auto& Cluster : Pyramid.Clusters)
    {
        for (uint32_t T = 0u; T < Cluster.TriangleCount; ++T)
        {
            const auto& Tri = Pyramid.Triangles[Cluster.TriangleStartOffset + T];
            if (!VerifyCheck(Tri.MaterialOrdinal == Cluster.DominantMaterialOrdinal, "cluster mixes material slots"))
            {
                return 1;
            }
            if (!VerifyCheck(
                    Tri.SourceFaceIndex < Grid.Faces.size() &&
                        Grid.Faces[Tri.SourceFaceIndex].PartGroupOrdinal == Tri.MaterialOrdinal,
                    "triangle lost its source face material slot"))
            {
                return 1;
            }
        }
    }

    // 2. Per-face material assignment: a triangle's material must match the side it sits on
    for (const auto& Tri : Pyramid.Triangles)
    {
        const Mathematics::Axis3D Centroid =
            (Pyramid.Positions[Tri.VertexIndex0] + Pyramid.Positions[Tri.VertexIndex1] + Pyramid.Positions[Tri.VertexIndex2]) *
            (1.0f / 3.0f);
        if (Centroid.X < -0.05f && !VerifyCheck(Tri.MaterialOrdinal == 3u, "left-side triangle kept material slot 3"))
        {
            return 1;
        }
        if (Centroid.X > 0.05f && !VerifyCheck(Tri.MaterialOrdinal == 7u, "right-side triangle kept material slot 7"))
        {
            return 1;
        }
    }

    // 3. Final-level cap: far camera may not select anything coarser than the cap
    ClusterPyramid CappedPyramid {};
    ClusterLodBakeConfiguration CappedBake = Bake;
    CappedBake.CoarsestAllowedLevel        = 0u; // finest only (strict glass-style protection)
    if (!VerifyCheck(CappedPyramid.BuildFromPolygonNetwork(Grid, CappedBake), "capped LOD bake succeeded"))
    {
        return 1;
    }
    const Mathematics::Axis3D FarCamera { 0.0f, 0.0f, 500.0f };
    const auto CappedCut = CappedPyramid.SelectMonotonicCut(FarCamera, 1000.0f, 1.0e6f, false);
    const auto FreeCut   = Pyramid.SelectMonotonicCut(FarCamera, 1000.0f, 1.0e6f, false);
    for (const uint32_t ClusterIdx : CappedCut.ActiveClusterIndices)
    {
        if (!VerifyCheck(CappedPyramid.Clusters[ClusterIdx].HierarchyLevel == 0u, "cap 0 cut selected a coarser cluster"))
        {
            return 1;
        }
    }
    if (!VerifyCheck(
            CappedCut.ActiveTriangleCount > FreeCut.ActiveTriangleCount &&
                FreeCut.ActiveTriangleCount > 0u,
            "uncapped far cut simplifies, capped cut stays finest"))
    {
        return 1;
    }

    // 4. Serialization round-trip keeps the cap & full v3 DAG structure
    const auto Bytes = CappedPyramid.SerializeToBinaryBytes();
    ClusterPyramid Restored {};
    if (!VerifyCheck(Restored.DeserializeFromBinaryBytes(Bytes), "pyramid serialization round-trip") ||
        !VerifyCheck(Restored.CoarsestAllowedLevel == 0u, "coarsest allowed level survived serialization") ||
        !VerifyCheck(Restored.Groups.size() == CappedPyramid.Groups.size(), "DAG groups survived v3 serialization") ||
        !VerifyCheck(Restored.RootClusterIndices == CappedPyramid.RootClusterIndices, "DAG roots survived v3 serialization"))
    {
        return 1;
    }

    // 5. GeometryCompiler parity suite on closed watertight torus (2048 tris -> multi-level DAG):
    //    - Monotone group error & enclosing group spheres
    //    - Exact top-down DAG walk == brute-force per-cluster oracle across 10 camera views
    //    - Zero open edges & zero non-manifold edges on EVERY mixed-level camera cut (seam-free proof!)
    //    - Frustum culling & normal-cone backface culling reduce cluster count deterministically
    const GeometrySpace::PolygonNetwork Torus = BuildClosedWatertightTorus(32u, 32u, 2.4f, 0.85f);
    ClusterPyramid TorusDag {};
    ClusterLodBakeConfiguration TorusBake {};
    TorusBake.MaximumLevels       = 10u;
    TorusBake.TargetGroupClusters = 4u;
    TorusBake.SimplifyKeepRatio   = 0.50f;
    TorusBake.RelativeErrorLimit  = 1.0f;
    if (!VerifyCheck(TorusDag.BuildFromPolygonNetwork(Torus, TorusBake), "closed torus DAG bake succeeded") ||
        !VerifyCheck(TorusDag.HierarchyDepth >= 4u, "closed torus DAG reached >= 4 levels") ||
        !VerifyCheck(!TorusDag.Groups.empty() && !TorusDag.RootClusterIndices.empty(), "closed torus populated DAG groups & roots"))
    {
        return 1;
    }

    for (const ClusterHeaderEntry& Cls : TorusDag.Clusters)
    {
        if (Cls.EnclosingGroupOrdinal > 0u)
        {
            const ClusterGroupEntry& Enclosing = TorusDag.Groups[Cls.EnclosingGroupOrdinal - 1u];
            if (!VerifyCheck(
                    Enclosing.MonotonicErrorMeters + 1.0e-7f >= Cls.FinerSelfErrorMeters,
                    "DAG monotone group error holds on every cluster transition"))
            {
                return 1;
            }
        }
    }

    const Mathematics::Axis3D CameraPoses[10] = {
        {  0.0f,  0.0f,  4.2f },
        {  3.8f,  0.5f,  1.2f },
        { -5.2f,  2.8f,  2.1f },
        {  0.0f, -8.5f,  3.0f },
        {  7.0f,  7.0f,  1.5f },
        { 14.0f, -3.0f,  6.0f },
        { -2.1f,  1.4f,  2.9f },
        {  0.2f,  0.1f, 22.0f },
        { 36.0f,  0.0f,  4.0f },
        {  4.9f, -4.4f, -2.5f },
    };

    bool SawMixedLevelCut = false;
    for (std::size_t PoseIdx = 0u; PoseIdx < 10u; ++PoseIdx)
    {
        const Mathematics::Axis3D CamPos = CameraPoses[PoseIdx];
        for (const float ErrBudget : { 1.25f, 3.5f })
        {
            const auto DagCut = TorusDag.SelectMonotonicCut(CamPos, 720.0f, ErrBudget, false);
            const auto Brute  = TorusDag.SelectExhaustiveReferenceCut(CamPos, 720.0f, ErrBudget);
            if (!VerifyCheck(DagCut.ActiveClusterIndices == Brute, "top-down DAG cut matches exhaustive per-cluster oracle"))
            {
                return 1;
            }
            const auto Manifold = InspectActiveCutManifoldEdges(TorusDag, DagCut.ActiveClusterIndices);
            if (!VerifyCheck(
                    Manifold.OpenEdgeCount == 0u && Manifold.NonManifoldEdgeCount == 0u && Manifold.TotalEdges > 0u,
                    "active DAG cut is 100% watertight (zero cracks, zero T-junctions, zero non-manifold seams)"))
            {
                std::fprintf(
                    stderr,
                    "  pose %zu: open=%u nonManifold=%u activeClusters=%zu\n",
                    PoseIdx, Manifold.OpenEdgeCount, Manifold.NonManifoldEdgeCount, DagCut.ActiveClusterIndices.size());
                return 1;
            }
            uint8_t MinLvl = 255u, MaxLvl = 0u;
            for (const uint32_t CIdx : DagCut.ActiveClusterIndices)
            {
                MinLvl = std::min(MinLvl, TorusDag.Clusters[CIdx].HierarchyLevel);
                MaxLvl = std::max(MaxLvl, TorusDag.Clusters[CIdx].HierarchyLevel);
            }
            if (MaxLvl > MinLvl)
            {
                SawMixedLevelCut = true;
            }
        }
    }
    if (!VerifyCheck(SawMixedLevelCut, "camera sweep exercised mixed-LOD cross-level seam cuts"))
    {
        return 1;
    }

    // 6. Frustum + normal-cone backface culling & forced LOD level checks
    const ClusterFrustumPlane OffscreenPlane { { 1.0f, 0.0f, 0.0f }, -50.0f }; // x >= 50 required -> rejects whole torus at origin
    const auto CulledOffscreen = TorusDag.SelectMonotonicCut(
        { 0.0f, 0.0f, 8.0f }, 720.0f, 1.25f, false, std::span<const ClusterFrustumPlane>(&OffscreenPlane, 1u));
    if (!VerifyCheck(
            CulledOffscreen.ActiveClusterIndices.empty() && CulledOffscreen.CulledFrustumCount > 0u,
            "DAG frustum culling rejects off-frustum root/group spheres without traversing leaves"))
    {
        return 1;
    }

    // Verify normal-cone backface culling on both closed curved torus (64x64 = 8192 tris, 64 L0 clusters) and flat grid:
    const GeometrySpace::PolygonNetwork FineTorus = BuildClosedWatertightTorus(64u, 64u, 2.4f, 0.85f);
    ClusterPyramid FineTorusDag {};
    if (!VerifyCheck(FineTorusDag.BuildFromPolygonNetwork(FineTorus, TorusBake), "64x64 torus DAG bake succeeded"))
    {
        return 1;
    }
    const auto UnculledNear = FineTorusDag.SelectMonotonicCut({ 12.0f, 0.0f, 0.0f }, 720.0f, 0.0f, false);
    const auto ConeCulled   = FineTorusDag.SelectMonotonicCut({ 12.0f, 0.0f, 0.0f }, 720.0f, 0.0f, true);
    const auto GridBackside = Pyramid.SelectMonotonicCut({ 0.0f, 0.0f, -30.0f }, 720.0f, 1.25f, true);
    if (!VerifyCheck(
            ConeCulled.ActiveClusterIndices.size() < UnculledNear.ActiveClusterIndices.size() &&
                ConeCulled.CulledBackfaceCount > 0u &&
                GridBackside.ActiveClusterIndices.empty() &&
                GridBackside.CulledBackfaceCount == Pyramid.SelectMonotonicCut({ 0.0f, 0.0f, -30.0f }, 720.0f, 1.25f, false).ActiveClusterIndices.size(),
            "cluster normal-cone backface culling rejects rear-facing clusters"))
    {
        return 1;
    }

    const auto ForcedL2 = TorusDag.SelectMonotonicCut({ 0.0f, 0.0f, 4.0f }, 720.0f, 1.25f, false, {}, 2);
    for (const uint32_t CIdx : ForcedL2.ActiveClusterIndices)
    {
        if (!VerifyCheck(TorusDag.Clusters[CIdx].HierarchyLevel == 2u, "forced hierarchy level selects exact level 2"))
        {
            return 1;
        }
    }

    const auto TorusSummaries = TorusDag.SummarizeLevels();
    std::printf(
        "[ClusterLodBakeValidation] PASS — Grid (%u lvls, %u->%u tris, materials {3,7}) | Torus DAG (%u lvls, %zu groups, %u->%u tris, 10/10 oracle & watertight cuts).\n",
        Pyramid.HierarchyDepth,
        Summaries.front().TriangleCount,
        Summaries.back().TriangleCount,
        TorusDag.HierarchyDepth,
        TorusDag.Groups.size(),
        TorusSummaries.front().TriangleCount,
        TorusSummaries.back().TriangleCount);
    return 0;
}
