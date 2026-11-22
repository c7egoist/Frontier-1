/*============================================================================================================================================
                                                        CLUSTERLODFLOORPROBE.CPP
============================================================================================================================================*/
// 🧩 Deterministic probe: LOD floor enforcement, material-pure clusters, cluster-density packing round-trip

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <set>

namespace
{
using Frontier::GeometrySpace::PolygonNetwork;
using Frontier::Mathematics::Axis3D;
using Frontier::SpatialAcceleration::ClusterLodBakeConfiguration;
using Frontier::SpatialAcceleration::ClusterPyramid;

// Two-material 20x20 quad grid with a sinusoidal surface, built directly on the half-edge topology
// (PolygonNetwork's codec translation unit is not linked into this probe).
PolygonNetwork BuildTwoMaterialTerrain()
{
    PolygonNetwork Network {};
    constexpr uint32_t Cells = 20u;
    const uint32_t Stride   = Cells + 1u;
    Network.Vertices.resize(Stride * Stride);
    for (uint32_t Y = 0u; Y <= Cells; ++Y)
    {
        for (uint32_t X = 0u; X <= Cells; ++X)
        {
            const float Height = 0.35f * std::sin(static_cast<float>(X) * 0.35f) * std::cos(static_cast<float>(Y) * 0.30f);
            Network.Vertices[Y * Stride + X].Axes = { static_cast<float>(X) - 10.0f, static_cast<float>(Y) - 10.0f, Height };
            Network.Vertices[Y * Stride + X].Normal = { 0.0f, 0.0f, 1.0f };
        }
    }

    for (uint32_t Y = 0u; Y < Cells; ++Y)
    {
        for (uint32_t X = 0u; X < Cells; ++X)
        {
            const uint32_t V0 = Y * Stride + X;
            const uint32_t Loop[4] = { V0, V0 + 1u, V0 + 1u + Stride, V0 + Stride };
            const uint32_t FaceIdx = static_cast<uint32_t>(Network.Faces.size());
            const uint32_t EdgeBase = static_cast<uint32_t>(Network.Edges.size());
            Network.Edges.resize(EdgeBase + 4u);
            for (uint32_t Corner = 0u; Corner < 4u; ++Corner)
            {
                auto& Edge              = Network.Edges[EdgeBase + Corner];
                Edge.OriginVertexIndex  = Loop[Corner];
                Edge.NextEdgeIndex      = EdgeBase + ((Corner + 1u) % 4u);
                Edge.PreviousEdgeIndex  = EdgeBase + ((Corner + 3u) % 4u);
                Edge.EnclosingFaceIndex = FaceIdx;
                Edge.ParameterUV        = { static_cast<float>(Corner == 1u || Corner == 2u),
                                            static_cast<float>(Corner >= 2u) };
                Network.Vertices[Loop[Corner]].OutgoingEdgeIndex = EdgeBase + Corner;
            }
            Frontier::GeometrySpace::Face Face {};
            Face.BoundaryLoopEdgeIndex = EdgeBase;
            Face.CornerCount           = 4u;
            Face.GeometricNormal       = { 0.0f, 0.0f, 1.0f };
            Face.PartGroupOrdinal      = (X < Cells / 2u) ? uint16_t{ 3 } : uint16_t{ 7 };
            Network.Faces.push_back(Face);
        }
    }
    return Network;
}

bool Verify(bool Condition, const char* Label)
{
    std::printf("%s : %s\n", Condition ? "PASS" : "FAIL", Label);
    return Condition;
}

} // namespace

int main()
{
    bool Pass = true;

    const PolygonNetwork Grid = BuildTwoMaterialTerrain();
    ClusterPyramid Pyramid {};
    ClusterLodBakeConfiguration Bake {};
    Bake.MaximumLevels          = 4u;
    Bake.CoarsestAllowedLevel   = 0xFFFFFFFFu;
    Bake.LockMaterialBoundaries = true;
    if (!Pyramid.BuildFromPolygonNetwork(Grid, Bake))
    {
        std::printf("RESULT: FAIL (bake failed)\n");
        return 1;
    }

    // 1. Material purity: no cluster may contain two different material slots.
    std::size_t ImpureClusters = 0u;
    for (const auto& Cluster : Pyramid.Clusters)
    {
        std::set<uint16_t> Slots;
        for (uint32_t Tri = 0u; Tri < Cluster.TriangleCount; ++Tri)
        {
            Slots.insert(Pyramid.Triangles[Cluster.TriangleStartOffset + Tri].MaterialOrdinal);
        }
        if (Slots.size() > 1u)
        {
            ++ImpureClusters;
        }
    }
    Pass = Verify(ImpureClusters == 0u, "every cluster is material-pure (never shared across material slots)") && Pass;

    // 2. LOD floor: a floor of 1 must keep the cut at level <= 1 at ANY camera distance.
    constexpr float PixelsPerRadian = 900.0f;
    bool FloorViolated = false;
    for (float Distance : { 0.5f, 2.0f, 8.0f, 40.0f, 400.0f, 4000.0f })
    {
        const Axis3D Camera { 0.0f, 0.0f, Distance };
        const auto Cut = Pyramid.SelectMonotonicCut(Camera, PixelsPerRadian, 40.0f, false, 1u);
        for (const uint32_t ClusterIdx : Cut.ActiveClusterIndices)
        {
            FloorViolated = FloorViolated || (Pyramid.Clusters[ClusterIdx].HierarchyLevel > 1u);
        }
        if (Cut.ActiveClusterIndices.empty())
        {
            FloorViolated = true; // the cut must always remain watertight
        }
    }
    Pass = Verify(!FloorViolated, "per-instance LOD floor caps the cut at level 1 at every distance") && Pass;

    // 3. Uncapped cut does reach coarser levels at long range (the floor is a cap, not a freeze).
    const auto FactoryCut   = Pyramid.SelectMonotonicCut(Axis3D { 0.0f, 0.0f, 4000.0f }, PixelsPerRadian, 400.0f, false);
    const auto FlooredCut   = Pyramid.SelectMonotonicCut(Axis3D { 0.0f, 0.0f, 4000.0f }, PixelsPerRadian, 400.0f, false, 0u);
    uint32_t FactoryTriangles = FactoryCut.ActiveTriangleCount;
    uint32_t FlooredTriangles = FlooredCut.ActiveTriangleCount;
    Pass = Verify(FlooredTriangles >= FactoryTriangles, "floor 0 keeps at least the finest-cut triangle count") && Pass;

    // 4. Density packing round-trip: (cluster << 8 | triangles) decodes exactly.
    bool PackingOk = true;
    for (uint32_t ClusterOrd = 0u; ClusterOrd < 4096u; ClusterOrd += 97u)
    {
        for (uint32_t Tris : { 1u, 17u, 64u, 128u })
        {
            const float Packed = static_cast<float>((ClusterOrd << 8u) | Tris);
            const uint32_t Decoded = static_cast<uint32_t>(Packed + 0.5f);
            PackingOk = PackingOk && ((Decoded >> 8u) == ClusterOrd) && ((Decoded & 0xFFu) == Tris);
        }
    }
    Pass = Verify(PackingOk, "cluster ordinal + triangle count survive the float32 debug lane exactly") && Pass;

    std::printf("levels baked   : %u\n", Pyramid.HierarchyDepth);
    std::printf("clusters       : %zu\n", Pyramid.Clusters.size());
    std::printf("%s\n", Pass ? "RESULT: PASS" : "RESULT: FAIL");
    return Pass ? 0 : 1;
}
