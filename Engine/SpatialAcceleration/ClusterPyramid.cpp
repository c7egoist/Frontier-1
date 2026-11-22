/*============================================================================================================================================
                                                              CLUSTERPYRAMID.CPP
============================================================================================================================================*/
// 🧩 128-triangle recursive median cluster DAG with shared-edge group clustering, FREE/BORDER/LOCKED QEM collapse, and top-down DAG cut

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace Frontier::SpatialAcceleration
{

namespace
{

constexpr uint32_t ClusterBinaryMagic   = 0x31595043u; // 'CPY1'
constexpr uint32_t ClusterBinaryVersion = 4u; // v4 appends per-material coarsest-allowed LOD caps (MaterialCoarsestAllowedLevels)

enum class SimplificationVertexConstraint : uint8_t
{
    FreeInterior    = 0u, // [-] - May collapse onto any incident vertex
    OpenSheetBorder = 1u, // [-] - True open sheet border: may only slide along single-use border edges
    LockedBoundary  = 2u, // [-] - Cross-group / material / UV-seam / non-manifold vertex: never moves
};

constexpr std::size_t QuadricCoeffCount = 11u; // 10 symmetric plane quadric coefficients + accumulated area weight

//------------------------------------------------------------------------------------------------------------------------
//                                       HOARE QUICKSELECT SPATIAL MEDIAN BISECTION
//------------------------------------------------------------------------------------------------------------------------

void PartitionSelectNth(
    std::vector<uint32_t>&   OrderedIds,
    std::span<const float>   AxisCoordinates,
    int32_t                  LowIdx,
    int32_t                  HighIdx,
    int32_t                  TargetNth) noexcept
{
    while (LowIdx < HighIdx)
    {
        const float Pivot = AxisCoordinates[OrderedIds[static_cast<std::size_t>((LowIdx + HighIdx) >> 1)]];
        int32_t Left  = LowIdx;
        int32_t Right = HighIdx;
        while (Left <= Right)
        {
            while (AxisCoordinates[OrderedIds[static_cast<std::size_t>(Left)]] < Pivot)
            {
                ++Left;
            }
            while (AxisCoordinates[OrderedIds[static_cast<std::size_t>(Right)]] > Pivot)
            {
                --Right;
            }
            if (Left <= Right)
            {
                std::swap(OrderedIds[static_cast<std::size_t>(Left)], OrderedIds[static_cast<std::size_t>(Right)]);
                ++Left;
                --Right;
            }
        }
        if (TargetNth <= Right)
        {
            HighIdx = Right;
        }
        else if (TargetNth >= Left)
        {
            LowIdx = Left;
        }
        else
        {
            return;
        }
    }
}

[[nodiscard]] uint32_t ExpandBits10To30(uint32_t Coord10) noexcept
{
    uint32_t Spread = Coord10 & 0x000003FFu;
    Spread = (Spread | (Spread << 16u)) & 0x030000FFu;
    Spread = (Spread | (Spread << 8u))  & 0x0300F00Fu;
    Spread = (Spread | (Spread << 4u))  & 0x030C30C3u;
    Spread = (Spread | (Spread << 2u))  & 0x09249249u;
    return Spread;
}

[[nodiscard]] uint32_t EncodeMorton30(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D SceneMin,
    Mathematics::Axis3D InvSceneExtent) noexcept
{
    const float NormX = std::clamp((Point.X - SceneMin.X) * InvSceneExtent.X, 0.0f, 1.0f);
    const float NormY = std::clamp((Point.Y - SceneMin.Y) * InvSceneExtent.Y, 0.0f, 1.0f);
    const float NormZ = std::clamp((Point.Z - SceneMin.Z) * InvSceneExtent.Z, 0.0f, 1.0f);
    const auto QuantX = static_cast<uint32_t>(NormX * 1023.0f);
    const auto QuantY = static_cast<uint32_t>(NormY * 1023.0f);
    const auto QuantZ = static_cast<uint32_t>(NormZ * 1023.0f);
    return (ExpandBits10To30(QuantX) << 2u) | (ExpandBits10To30(QuantY) << 1u) | ExpandBits10To30(QuantZ);
}

template <typename EmitCallback>
void SplitTrianglesSpatially(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> SourceTriangles,
    uint32_t                              ClusterLimit,
    EmitCallback&&                        EmitSpan)
{
    const uint32_t Count = static_cast<uint32_t>(SourceTriangles.size());
    if (Count == 0u)
    {
        return;
    }

    Mathematics::Axis3D BoundMin { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D BoundMax { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    std::vector<Mathematics::Axis3D> Centroids(Count);
    for (uint32_t Idx = 0u; Idx < Count; ++Idx)
    {
        const ClusterTriangleEntry& Tri = SourceTriangles[Idx];
        const Mathematics::Axis3D Centroid =
            (Positions[Tri.VertexIndex0] + Positions[Tri.VertexIndex1] + Positions[Tri.VertexIndex2]) * (1.0f / 3.0f);
        Centroids[Idx] = Centroid;
        BoundMin.X = std::min(BoundMin.X, Centroid.X);
        BoundMin.Y = std::min(BoundMin.Y, Centroid.Y);
        BoundMin.Z = std::min(BoundMin.Z, Centroid.Z);
        BoundMax.X = std::max(BoundMax.X, Centroid.X);
        BoundMax.Y = std::max(BoundMax.Y, Centroid.Y);
        BoundMax.Z = std::max(BoundMax.Z, Centroid.Z);
    }

    // Pre-order ids along a 30-bit Morton Z-order curve so ties and spatial octants stay compact across all 3 axes!
    const Mathematics::Axis3D InvSpan {
        1.0f / std::max(1.0e-5f, BoundMax.X - BoundMin.X),
        1.0f / std::max(1.0e-5f, BoundMax.Y - BoundMin.Y),
        1.0f / std::max(1.0e-5f, BoundMax.Z - BoundMin.Z),
    };
    struct MortonSeed { uint32_t Code = 0u; uint32_t OrigIdx = 0u; };
    std::vector<MortonSeed> Seeds(Count);
    for (uint32_t Idx = 0u; Idx < Count; ++Idx)
    {
        Seeds[Idx] = { EncodeMorton30(Centroids[Idx], BoundMin, InvSpan), Idx };
    }
    std::sort(Seeds.begin(), Seeds.end(), [](const MortonSeed& Left, const MortonSeed& Right) noexcept
    {
        return (Left.Code != Right.Code) ? (Left.Code < Right.Code) : (Left.OrigIdx < Right.OrigIdx);
    });

    std::vector<uint32_t> Ids(Count);
    std::array<std::vector<float>, 3u> Axes {{
        std::vector<float>(Count),
        std::vector<float>(Count),
        std::vector<float>(Count),
    }};
    for (uint32_t Idx = 0u; Idx < Count; ++Idx)
    {
        const uint32_t Orig = Seeds[Idx].OrigIdx;
        Ids[Idx]      = Orig;
        Axes[0u][Orig] = Centroids[Orig].X;
        Axes[1u][Orig] = Centroids[Orig].Y;
        Axes[2u][Orig] = Centroids[Orig].Z;
    }

    // Balanced spatial KD median split when cluster count is >= 16; pure 3D Z-order octant chunks below 16
    // so small assets (e.g. spheres with ~20 clusters) keep tight 3D octant bounding spheres rather than wide 2D wedges!
    if (Count <= ClusterLimit * 24u)
    {
        for (uint32_t Start = 0u; Start < Count; Start += ClusterLimit)
        {
            const uint32_t ChunkLen = std::min(ClusterLimit, Count - Start);
            EmitSpan(std::span<const uint32_t>(Ids.data() + Start, ChunkLen));
        }
        return;
    }

    struct SplitRange
    {
        uint32_t Start = 0u;
        uint32_t End   = 0u;
    };
    std::vector<SplitRange> Stack;
    Stack.reserve(64u);
    Stack.push_back({ 0u, Count });

    while (!Stack.empty())
    {
        const SplitRange Range = Stack.back();
        Stack.pop_back();
        const uint32_t SpanSize = Range.End - Range.Start;
        if (SpanSize <= ClusterLimit)
        {
            EmitSpan(std::span<const uint32_t>(Ids.data() + Range.Start, SpanSize));
            continue;
        }

        std::size_t BestAxis   = 0u;
        float       BestExtent = -1.0f;
        for (std::size_t AxisIdx = 0u; AxisIdx < 3u; ++AxisIdx)
        {
            float Low  = std::numeric_limits<float>::max();
            float High = std::numeric_limits<float>::lowest();
            const auto& CoordArray = Axes[AxisIdx];
            for (uint32_t Scan = Range.Start; Scan < Range.End; ++Scan)
            {
                const float Coord = CoordArray[Ids[Scan]];
                Low  = std::min(Low, Coord);
                High = std::max(High, Coord);
            }
            if (High - Low > BestExtent)
            {
                BestExtent = High - Low;
                BestAxis   = AxisIdx;
            }
        }

        const uint32_t Mid = (Range.Start + Range.End) >> 1u;
        PartitionSelectNth(
            Ids,
            Axes[BestAxis],
            static_cast<int32_t>(Range.Start),
            static_cast<int32_t>(Range.End - 1u),
            static_cast<int32_t>(Mid));

        // Push upper half first so lower half pops first (deterministic spatial ordering)
        Stack.push_back({ Mid, Range.End });
        Stack.push_back({ Range.Start, Mid });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CLUSTER BOUNDS, SPHERE & NORMAL CONE
//------------------------------------------------------------------------------------------------------------------------

void PopulateClusterBoundsNormalCone(
    ClusterHeaderEntry&                   Header,
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> ClusterTris) noexcept
{
    if (ClusterTris.empty())
    {
        return;
    }

    Mathematics::Axis3D MinPt { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D MaxPt { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    Mathematics::Axis3D NormalSum { 0.0f, 0.0f, 0.0f };
    std::vector<Mathematics::Axis3D> TriNormals;
    TriNormals.reserve(ClusterTris.size());

    std::unordered_map<uint16_t, uint32_t> MaterialVotes;

    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        const std::array<uint32_t, 3u> Indices {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
        for (const uint32_t VIdx : Indices)
        {
            const Mathematics::Axis3D Pos = Positions[VIdx];
            MinPt.X = std::min(MinPt.X, Pos.X);
            MinPt.Y = std::min(MinPt.Y, Pos.Y);
            MinPt.Z = std::min(MinPt.Z, Pos.Z);
            MaxPt.X = std::max(MaxPt.X, Pos.X);
            MaxPt.Y = std::max(MaxPt.Y, Pos.Y);
            MaxPt.Z = std::max(MaxPt.Z, Pos.Z);
        }

        const Mathematics::Axis3D P0 = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1 = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2 = Positions[Tri.VertexIndex2];
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(P1 - P0, P2 - P0);
        const float Len = Mathematics::EvaluateLength(Cross);
        if (Len > 1.0e-12f)
        {
            const Mathematics::Axis3D UnitN = Cross * (1.0f / Len);
            NormalSum = NormalSum + UnitN;
            TriNormals.push_back(UnitN);
        }
        ++MaterialVotes[Tri.MaterialOrdinal];
    }

    Header.BoundsMinimum = MinPt;
    Header.BoundsMaximum = MaxPt;
    Header.SphereCenter  = (MinPt + MaxPt) * 0.5f;

    float MaxRadiusSq = 0.0f;
    for (const ClusterTriangleEntry& Tri : ClusterTris)
    {
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex0] - Header.SphereCenter));
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex1] - Header.SphereCenter));
        MaxRadiusSq = std::max(MaxRadiusSq, Mathematics::EvaluateLengthSquared(Positions[Tri.VertexIndex2] - Header.SphereCenter));
    }
    Header.SphereRadiusMeters = std::sqrt(std::max(1.0e-12f, MaxRadiusSq));

    uint32_t TopVotes = 0u;
    for (const auto& [MatOrd, Votes] : MaterialVotes)
    {
        if (Votes > TopVotes)
        {
            TopVotes = Votes;
            Header.DominantMaterialOrdinal = static_cast<uint8_t>(std::min<uint16_t>(MatOrd, 255u));
        }
    }

    Header.NormalConeDirection = { 0.0f, 1.0f, 0.0f };
    Header.NormalConeSinCutoff = 2.0f; // 2.0 = spans hemisphere, disable cone backface culling
    if (!TriNormals.empty() && Mathematics::EvaluateLengthSquared(NormalSum) > 1.0e-12f)
    {
        Header.NormalConeDirection = Mathematics::NormalizeAxis(NormalSum);
        float LeastDot = 1.0f;
        for (const Mathematics::Axis3D& UnitN : TriNormals)
        {
            LeastDot = std::min(LeastDot, Mathematics::EvaluateDot(Header.NormalConeDirection, UnitN));
        }
        if (LeastDot > 0.10f)
        {
            Header.NormalConeSinCutoff = std::sqrt(std::max(0.0f, 1.0f - LeastDot * LeastDot));
        }
    }
}

[[nodiscard]] double ComputeTriangleSpanSurfaceArea(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> SpanTris) noexcept
{
    double TotalArea = 0.0;
    for (const ClusterTriangleEntry& Tri : SpanTris)
    {
        const Mathematics::Axis3D P0 = Positions[Tri.VertexIndex0];
        const Mathematics::Axis3D P1 = Positions[Tri.VertexIndex1];
        const Mathematics::Axis3D P2 = Positions[Tri.VertexIndex2];
        TotalArea += 0.5 * static_cast<double>(Mathematics::EvaluateLength(Mathematics::EvaluateCross(P1 - P0, P2 - P0)));
    }
    return TotalArea;
}

//------------------------------------------------------------------------------------------------------------------------
//                      MIN-HEAP QUADRIC EDGE-COLLAPSE SIMPLIFIER (GEOMETRYCOMPILER PARITY)
//------------------------------------------------------------------------------------------------------------------------

struct CollapseHeapCandidate
{
    double   PriorityCost = 0.0;
    uint32_t FromLocal    = 0u;
    uint32_t ToLocal      = 0u;
    int32_t  StampFrom    = 0;
    int32_t  StampTo      = 0;
};

class CollapsePriorityQueue
{
public:
    void Reserve(std::size_t Capacity)
    {
        Entries.reserve(Capacity);
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return Entries.empty();
    }

    void Push(const CollapseHeapCandidate& Candidate)
    {
        std::size_t Index = Entries.size();
        Entries.push_back(Candidate);
        while (Index > 0u)
        {
            const std::size_t Upper = (Index - 1u) >> 1u;
            if (Entries[Upper].PriorityCost <= Entries[Index].PriorityCost)
            {
                break;
            }
            std::swap(Entries[Index], Entries[Upper]);
            Index = Upper;
        }
    }

    [[nodiscard]] CollapseHeapCandidate PopMin() noexcept
    {
        const CollapseHeapCandidate Top = Entries[0u];
        const std::size_t Last = Entries.size() - 1u;
        Entries[0u] = Entries[Last];
        Entries.pop_back();

        std::size_t Index = 0u;
        for (;;)
        {
            const std::size_t Left  = Index * 2u + 1u;
            const std::size_t Right = Left + 1u;
            std::size_t Smallest    = Index;
            if (Left < Last && Entries[Left].PriorityCost < Entries[Smallest].PriorityCost)
            {
                Smallest = Left;
            }
            if (Right < Last && Entries[Right].PriorityCost < Entries[Smallest].PriorityCost)
            {
                Smallest = Right;
            }
            if (Smallest == Index)
            {
                break;
            }
            std::swap(Entries[Index], Entries[Smallest]);
            Index = Smallest;
        }
        return Top;
    }

private:
    std::vector<CollapseHeapCandidate> Entries {};
};

struct GroupSimplificationOutput
{
    std::vector<ClusterTriangleEntry> SurvivingTriangles {};
    float                             EstimatedRmsError  = 0.0f;
    uint32_t                          ExecutedCollapses  = 0u;
};

template <typename ConstraintLookupFn>
[[nodiscard]] GroupSimplificationOutput SimplifyClusterGroupQuadric(
    std::span<const Mathematics::Axis3D>  Positions,
    std::span<const ClusterTriangleEntry> InputTriangles,
    ConstraintLookupFn&&                  QueryConstraint,
    uint32_t                              TargetTriangleCount,
    double                                MaxAllowedErrorMeters)
{
    GroupSimplificationOutput Result {};
    const uint32_t TriCount = static_cast<uint32_t>(InputTriangles.size());
    if (TriCount <= TargetTriangleCount)
    {
        Result.SurvivingTriangles.assign(InputTriangles.begin(), InputTriangles.end());
        return Result;
    }

    // 1. Remap global vertex indices to compact local [0..LocalVertexCount) indices
    std::unordered_map<uint32_t, uint32_t> LocalOfGlobal;
    LocalOfGlobal.reserve(TriCount * 2u);
    std::vector<uint32_t> GlobalOfLocal;
    GlobalOfLocal.reserve(TriCount * 2u);

    std::vector<uint32_t> LocalTriCorners(static_cast<std::size_t>(TriCount) * 3u, 0u);
    std::vector<ClusterTriangleEntry> WorkingTris(InputTriangles.begin(), InputTriangles.end());

    const auto MapVertex = [&](uint32_t GlobalV) -> uint32_t
    {
        const auto Found = LocalOfGlobal.find(GlobalV);
        if (Found != LocalOfGlobal.end())
        {
            return Found->second;
        }
        const uint32_t LocalIdx = static_cast<uint32_t>(GlobalOfLocal.size());
        GlobalOfLocal.push_back(GlobalV);
        LocalOfGlobal.emplace(GlobalV, LocalIdx);
        return LocalIdx;
    };

    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        LocalTriCorners[static_cast<std::size_t>(T) * 3u + 0u] = MapVertex(InputTriangles[T].VertexIndex0);
        LocalTriCorners[static_cast<std::size_t>(T) * 3u + 1u] = MapVertex(InputTriangles[T].VertexIndex1);
        LocalTriCorners[static_cast<std::size_t>(T) * 3u + 2u] = MapVertex(InputTriangles[T].VertexIndex2);
    }

    const uint32_t LocalVertexCount = static_cast<uint32_t>(GlobalOfLocal.size());
    std::vector<double> PosX(LocalVertexCount, 0.0);
    std::vector<double> PosY(LocalVertexCount, 0.0);
    std::vector<double> PosZ(LocalVertexCount, 0.0);
    std::vector<SimplificationVertexConstraint> VertexKinds(LocalVertexCount, SimplificationVertexConstraint::FreeInterior);

    for (uint32_t L = 0u; L < LocalVertexCount; ++L)
    {
        const uint32_t GlobalV = GlobalOfLocal[L];
        PosX[L] = static_cast<double>(Positions[GlobalV].X);
        PosY[L] = static_cast<double>(Positions[GlobalV].Y);
        PosZ[L] = static_cast<double>(Positions[GlobalV].Z);
        VertexKinds[L] = QueryConstraint(GlobalV);
    }

    std::vector<double> Quadrics(static_cast<std::size_t>(LocalVertexCount) * QuadricCoeffCount, 0.0);
    std::vector<std::vector<uint32_t>> VertexFans(LocalVertexCount);
    std::vector<uint8_t> TriAlive(TriCount, 1u);
    uint32_t AliveCount = TriCount;

    const auto AddPlaneQuadric = [&](uint32_t LocalV, double Nx, double Ny, double Nz, double OffsetD, double AreaWeight) noexcept
    {
        double* Q = Quadrics.data() + static_cast<std::size_t>(LocalV) * QuadricCoeffCount;
        Q[0u]  += Nx * Nx * AreaWeight;
        Q[1u]  += Nx * Ny * AreaWeight;
        Q[2u]  += Nx * Nz * AreaWeight;
        Q[3u]  += Nx * OffsetD * AreaWeight;
        Q[4u]  += Ny * Ny * AreaWeight;
        Q[5u]  += Ny * Nz * AreaWeight;
        Q[6u]  += Ny * OffsetD * AreaWeight;
        Q[7u]  += Nz * Nz * AreaWeight;
        Q[8u]  += Nz * OffsetD * AreaWeight;
        Q[9u]  += OffsetD * OffsetD * AreaWeight;
        Q[10u] += AreaWeight;
    };

    const auto PackEdgeKey = [](uint32_t A, uint32_t B) noexcept -> uint64_t
    {
        const uint64_t Lo = std::min(A, B);
        const uint64_t Hi = std::max(A, B);
        return (Lo << 32u) | Hi;
    };

    std::unordered_map<uint64_t, uint32_t> LocalEdgeUse;
    LocalEdgeUse.reserve(static_cast<std::size_t>(TriCount) * 3u);
    std::vector<uint64_t> UniqueEdgeKeys;
    UniqueEdgeKeys.reserve(static_cast<std::size_t>(TriCount) * 2u);

    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        const uint32_t A = LocalTriCorners[static_cast<std::size_t>(T) * 3u + 0u];
        const uint32_t B = LocalTriCorners[static_cast<std::size_t>(T) * 3u + 1u];
        const uint32_t C = LocalTriCorners[static_cast<std::size_t>(T) * 3u + 2u];
        if (A == B || B == C || A == C)
        {
            TriAlive[T] = 0u;
            --AliveCount;
            continue;
        }
        VertexFans[A].push_back(T);
        VertexFans[B].push_back(T);
        VertexFans[C].push_back(T);

        for (const uint64_t EKey : { PackEdgeKey(A, B), PackEdgeKey(B, C), PackEdgeKey(C, A) })
        {
            const auto [Iter, Inserted] = LocalEdgeUse.emplace(EKey, 1u);
            if (Inserted)
            {
                UniqueEdgeKeys.push_back(EKey);
            }
            else
            {
                Iter->second += 1u;
            }
        }
    }

    // 2. Accumulate area-weighted face quadrics + 4*area perpendicular open-edge border quadrics
    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        if (TriAlive[T] == 0u)
        {
            continue;
        }
        const std::array<uint32_t, 3u> V {{
            LocalTriCorners[static_cast<std::size_t>(T) * 3u + 0u],
            LocalTriCorners[static_cast<std::size_t>(T) * 3u + 1u],
            LocalTriCorners[static_cast<std::size_t>(T) * 3u + 2u],
        }};
        const double Ux = PosX[V[1u]] - PosX[V[0u]];
        const double Uy = PosY[V[1u]] - PosY[V[0u]];
        const double Uz = PosZ[V[1u]] - PosZ[V[0u]];
        const double Wx = PosX[V[2u]] - PosX[V[0u]];
        const double Wy = PosY[V[2u]] - PosY[V[0u]];
        const double Wz = PosZ[V[2u]] - PosZ[V[0u]];

        double Nx = Uy * Wz - Uz * Wy;
        double Ny = Uz * Wx - Ux * Wz;
        double Nz = Ux * Wy - Uy * Wx;
        const double CrossLen = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (CrossLen < 1.0e-30)
        {
            continue;
        }
        Nx /= CrossLen;
        Ny /= CrossLen;
        Nz /= CrossLen;
        const double Area    = CrossLen * 0.5;
        const double OffsetD = -(Nx * PosX[V[0u]] + Ny * PosY[V[0u]] + Nz * PosZ[V[0u]]);

        AddPlaneQuadric(V[0u], Nx, Ny, Nz, OffsetD, Area);
        AddPlaneQuadric(V[1u], Nx, Ny, Nz, OffsetD, Area);
        AddPlaneQuadric(V[2u], Nx, Ny, Nz, OffsetD, Area);

        for (std::size_t Side = 0u; Side < 3u; ++Side)
        {
            const uint32_t EA = V[Side];
            const uint32_t EB = V[(Side + 1u) % 3u];
            if (LocalEdgeUse[PackEdgeKey(EA, EB)] != 1u)
            {
                continue;
            }
            const double Ex = PosX[EB] - PosX[EA];
            const double Ey = PosY[EB] - PosY[EA];
            const double Ez = PosZ[EB] - PosZ[EA];
            double Bx = Ey * Nz - Ez * Ny;
            double By = Ez * Nx - Ex * Nz;
            double Bz = Ex * Ny - Ey * Nx;
            const double BorderLen = std::sqrt(Bx * Bx + By * By + Bz * Bz);
            if (BorderLen < 1.0e-30)
            {
                continue;
            }
            Bx /= BorderLen;
            By /= BorderLen;
            Bz /= BorderLen;
            const double BorderD      = -(Bx * PosX[EA] + By * PosY[EA] + Bz * PosZ[EA]);
            const double BorderWeight = Area * 4.0;
            AddPlaneQuadric(EA, Bx, By, Bz, BorderD, BorderWeight);
            AddPlaneQuadric(EB, Bx, By, Bz, BorderD, BorderWeight);
        }
    }

    std::vector<int32_t> Stamps(LocalVertexCount, 0);
    std::vector<int32_t> Marks(LocalVertexCount, 0);
    int32_t MarkCounter = 0;

    CollapsePriorityQueue Queue {};
    Queue.Reserve(UniqueEdgeKeys.size() * 2u);

    const auto EvaluateCollapseCost = [&](uint32_t FromU, uint32_t ToV) noexcept -> double
    {
        const double* QA = Quadrics.data() + static_cast<std::size_t>(FromU) * QuadricCoeffCount;
        const double* QB = Quadrics.data() + static_cast<std::size_t>(ToV) * QuadricCoeffCount;
        const double X = PosX[ToV];
        const double Y = PosY[ToV];
        const double Z = PosZ[ToV];
        const double Q0 = QA[0u] + QB[0u];
        const double Q1 = QA[1u] + QB[1u];
        const double Q2 = QA[2u] + QB[2u];
        const double Q3 = QA[3u] + QB[3u];
        const double Q4 = QA[4u] + QB[4u];
        const double Q5 = QA[5u] + QB[5u];
        const double Q6 = QA[6u] + QB[6u];
        const double Q7 = QA[7u] + QB[7u];
        const double Q8 = QA[8u] + QB[8u];
        const double Q9 = QA[9u] + QB[9u];
        const double Cost =
            X * X * Q0 + 2.0 * X * Y * Q1 + 2.0 * X * Z * Q2 + 2.0 * X * Q3 +
            Y * Y * Q4 + 2.0 * Y * Z * Q5 + 2.0 * Y * Q6 +
            Z * Z * Q7 + 2.0 * Z * Q8 +
            Q9;
        return (Cost > 0.0) ? Cost : 0.0;
    };

    const auto CountSharedTriangles = [&](uint32_t U, uint32_t V) noexcept -> uint32_t
    {
        uint32_t Shared = 0u;
        for (const uint32_t TriIdx : VertexFans[U])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            if (LocalTriCorners[Off] == V || LocalTriCorners[Off + 1u] == V || LocalTriCorners[Off + 2u] == V)
            {
                ++Shared;
            }
        }
        return Shared;
    };

    const auto ProposeDirectedCollapse = [&](uint32_t FromU, uint32_t ToV)
    {
        if (VertexKinds[FromU] == SimplificationVertexConstraint::LockedBoundary)
        {
            return;
        }
        if (VertexKinds[FromU] == SimplificationVertexConstraint::OpenSheetBorder &&
            (VertexKinds[ToV] == SimplificationVertexConstraint::FreeInterior || CountSharedTriangles(FromU, ToV) != 1u))
        {
            return;
        }
        const double RawCost = EvaluateCollapseCost(FromU, ToV);
        const double Dx = PosX[FromU] - PosX[ToV];
        const double Dy = PosY[FromU] - PosY[ToV];
        const double Dz = PosZ[FromU] - PosZ[ToV];
        const double CombinedWeight =
            Quadrics[static_cast<std::size_t>(FromU) * QuadricCoeffCount + 10u] +
            Quadrics[static_cast<std::size_t>(ToV) * QuadricCoeffCount + 10u];
        const double Priority = RawCost + 1.0e-4 * (Dx * Dx + Dy * Dy + Dz * Dz) * CombinedWeight;
        Queue.Push({ Priority, FromU, ToV, Stamps[FromU], Stamps[ToV] });
    };

    const auto ProposeAroundVertex = [&](uint32_t CenterV)
    {
        ++MarkCounter;
        Marks[CenterV] = MarkCounter;
        for (const uint32_t TriIdx : VertexFans[CenterV])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            for (std::size_t K = 0u; K < 3u; ++K)
            {
                const uint32_t PeerW = LocalTriCorners[Off + K];
                if (Marks[PeerW] == MarkCounter)
                {
                    continue;
                }
                Marks[PeerW] = MarkCounter;
                ProposeDirectedCollapse(CenterV, PeerW);
                ProposeDirectedCollapse(PeerW, CenterV);
            }
        }
    };

    std::sort(UniqueEdgeKeys.begin(), UniqueEdgeKeys.end());
    for (const uint64_t EKey : UniqueEdgeKeys)
    {
        const uint32_t A = static_cast<uint32_t>(EKey >> 32u);
        const uint32_t B = static_cast<uint32_t>(EKey & 0xFFFFFFFFu);
        ProposeDirectedCollapse(A, B);
        ProposeDirectedCollapse(B, A);
    }

    const auto VerifyLinkCondition = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        ++MarkCounter;
        const int32_t CurMark = MarkCounter;
        uint32_t Shared = 0u;
        uint32_t Common = 0u;
        for (const uint32_t TriIdx : VertexFans[U])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            bool TouchesV = false;
            for (std::size_t K = 0u; K < 3u; ++K)
            {
                const uint32_t W = LocalTriCorners[Off + K];
                if (W == V)
                {
                    TouchesV = true;
                }
                else if (W != U)
                {
                    Marks[W] = CurMark;
                }
            }
            if (TouchesV)
            {
                ++Shared;
            }
        }
        for (const uint32_t TriIdx : VertexFans[V])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            for (std::size_t K = 0u; K < 3u; ++K)
            {
                const uint32_t W = LocalTriCorners[Off + K];
                if (W != U && W != V && Marks[W] == CurMark)
                {
                    ++Common;
                    Marks[W] = 0;
                }
            }
        }
        return Shared > 0u && Shared <= 2u && Common == Shared;
    };

    const auto VerifyNoLockedBridge = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        if (VertexKinds[V] != SimplificationVertexConstraint::LockedBoundary)
        {
            return true;
        }
        ++MarkCounter;
        const int32_t CurMark = MarkCounter;
        for (const uint32_t TriIdx : VertexFans[V])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            Marks[LocalTriCorners[Off + 0u]] = CurMark;
            Marks[LocalTriCorners[Off + 1u]] = CurMark;
            Marks[LocalTriCorners[Off + 2u]] = CurMark;
        }
        for (const uint32_t TriIdx : VertexFans[U])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            for (std::size_t K = 0u; K < 3u; ++K)
            {
                const uint32_t W = LocalTriCorners[Off + K];
                if (VertexKinds[W] == SimplificationVertexConstraint::LockedBoundary && Marks[W] != CurMark)
                {
                    return false;
                }
            }
        }
        return true;
    };

    const auto VerifyOrientationPreserved = [&](uint32_t U, uint32_t V) noexcept -> bool
    {
        for (const uint32_t TriIdx : VertexFans[U])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            const uint32_t A = LocalTriCorners[Off + 0u];
            const uint32_t B = LocalTriCorners[Off + 1u];
            const uint32_t C = LocalTriCorners[Off + 2u];
            if (A == V || B == V || C == V)
            {
                continue;
            }
            const double Ux = PosX[B] - PosX[A];
            const double Uy = PosY[B] - PosY[A];
            const double Uz = PosZ[B] - PosZ[A];
            const double Wx = PosX[C] - PosX[A];
            const double Wy = PosY[C] - PosY[A];
            const double Wz = PosZ[C] - PosZ[A];
            const double Ox = Uy * Wz - Uz * Wy;
            const double Oy = Uz * Wx - Ux * Wz;
            const double Oz = Ux * Wy - Uy * Wx;

            const uint32_t A2 = (A == U) ? V : A;
            const uint32_t B2 = (B == U) ? V : B;
            const uint32_t C2 = (C == U) ? V : C;
            const double Sx = PosX[B2] - PosX[A2];
            const double Sy = PosY[B2] - PosY[A2];
            const double Sz = PosZ[B2] - PosZ[A2];
            const double Tx = PosX[C2] - PosX[A2];
            const double Ty = PosY[C2] - PosY[A2];
            const double Tz = PosZ[C2] - PosZ[A2];
            const double Mx = Sy * Tz - Sz * Ty;
            const double My = Sz * Tx - Sx * Tz;
            const double Mz = Sx * Ty - Sy * Tx;

            const double BeforeSq = Ox * Ox + Oy * Oy + Oz * Oz;
            const double AfterSq  = Mx * Mx + My * My + Mz * Mz;
            const double DotN     = Ox * Mx + Oy * My + Oz * Mz;
            if (AfterSq <= BeforeSq * 1.0e-8 || DotN <= 0.25 * std::sqrt(BeforeSq * AfterSq))
            {
                return false;
            }
        }
        return true;
    };

    const double MaxErrorSq = MaxAllowedErrorMeters * MaxAllowedErrorMeters;
    double WorstSquaredError = 0.0;

    // 3. Pop minimum-cost collapses until TargetTriangleCount or error/border ceiling
    while (AliveCount > TargetTriangleCount && !Queue.Empty())
    {
        const CollapseHeapCandidate Top = Queue.PopMin();
        const uint32_t U = Top.FromLocal;
        const uint32_t V = Top.ToLocal;
        if (Stamps[U] != Top.StampFrom || Stamps[V] != Top.StampTo)
        {
            continue;
        }
        if (VertexKinds[U] == SimplificationVertexConstraint::OpenSheetBorder && CountSharedTriangles(U, V) != 1u)
        {
            continue;
        }
        if (!VerifyLinkCondition(U, V) || !VerifyNoLockedBridge(U, V) || !VerifyOrientationPreserved(U, V))
        {
            continue;
        }

        const double RawCost = EvaluateCollapseCost(U, V);
        const double Weight  =
            Quadrics[static_cast<std::size_t>(U) * QuadricCoeffCount + 10u] +
            Quadrics[static_cast<std::size_t>(V) * QuadricCoeffCount + 10u];
        const double SquaredError = (Weight > 0.0) ? (RawCost / Weight) : 0.0;
        if (SquaredError > MaxErrorSq)
        {
            continue;
        }
        // Conservative collapse bound: max of area-normalized quadric RMS error and chord displacement
        // floor so small high-curvature surfaces (e.g. close foreground spheres) retain tight L0 octant clusters!
        const double Dx = PosX[U] - PosX[V];
        const double Dy = PosY[U] - PosY[V];
        const double Dz = PosZ[U] - PosZ[V];
        const double ChordFloorSq = 0.0256 * (Dx * Dx + Dy * Dy + Dz * Dz);
        WorstSquaredError = std::max({ WorstSquaredError, 4.0 * SquaredError, ChordFloorSq });

        // Capture representative corner attributes at V from a shared (U, V) triangle so UVs & smooth normals transfer cleanly
        Mathematics::UVAxes TransferUV {};
        uint32_t            TransferNormal  = 0u;
        uint32_t            TransferTangent = 0u;
        bool                TransferReady   = false;
        for (const uint32_t TriIdx : VertexFans[U])
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            const ClusterTriangleEntry& SharedTri = WorkingTris[TriIdx];
            if (LocalTriCorners[Off + 0u] == V)
            {
                TransferUV      = SharedTri.CornerUV0;
                TransferNormal  = SharedTri.PackedNormal0;
                TransferTangent = SharedTri.PackedTangent0;
                TransferReady   = true;
                break;
            }
            if (LocalTriCorners[Off + 1u] == V)
            {
                TransferUV      = SharedTri.CornerUV1;
                TransferNormal  = SharedTri.PackedNormal1;
                TransferTangent = SharedTri.PackedTangent0;
                TransferReady   = true;
                break;
            }
            if (LocalTriCorners[Off + 2u] == V)
            {
                TransferUV      = SharedTri.CornerUV2;
                TransferNormal  = SharedTri.PackedNormal2;
                TransferTangent = SharedTri.PackedTangent0;
                TransferReady   = true;
                break;
            }
        }

        const uint32_t GlobalTargetV = GlobalOfLocal[V];
        auto& FanU = VertexFans[U];
        auto& FanV = VertexFans[V];
        for (const uint32_t TriIdx : FanU)
        {
            const std::size_t Off = static_cast<std::size_t>(TriIdx) * 3u;
            if (LocalTriCorners[Off + 0u] == V || LocalTriCorners[Off + 1u] == V || LocalTriCorners[Off + 2u] == V)
            {
                TriAlive[TriIdx] = 0u;
                --AliveCount;
                for (std::size_t K = 0u; K < 3u; ++K)
                {
                    const uint32_t W = LocalTriCorners[Off + K];
                    if (W == U)
                    {
                        continue;
                    }
                    auto& FanW = VertexFans[W];
                    const auto FoundT = std::find(FanW.begin(), FanW.end(), TriIdx);
                    if (FoundT != FanW.end())
                    {
                        *FoundT = FanW.back();
                        FanW.pop_back();
                    }
                }
            }
            else
            {
                ClusterTriangleEntry& Surviving = WorkingTris[TriIdx];
                if (LocalTriCorners[Off + 0u] == U)
                {
                    LocalTriCorners[Off + 0u] = V;
                    Surviving.VertexIndex0    = GlobalTargetV;
                    if (TransferReady)
                    {
                        Surviving.CornerUV0      = TransferUV;
                        Surviving.PackedNormal0  = TransferNormal;
                        Surviving.PackedTangent0 = TransferTangent;
                    }
                }
                else if (LocalTriCorners[Off + 1u] == U)
                {
                    LocalTriCorners[Off + 1u] = V;
                    Surviving.VertexIndex1    = GlobalTargetV;
                    if (TransferReady)
                    {
                        Surviving.CornerUV1     = TransferUV;
                        Surviving.PackedNormal1 = TransferNormal;
                    }
                }
                else
                {
                    LocalTriCorners[Off + 2u] = V;
                    Surviving.VertexIndex2    = GlobalTargetV;
                    if (TransferReady)
                    {
                        Surviving.CornerUV2     = TransferUV;
                        Surviving.PackedNormal2 = TransferNormal;
                    }
                }
                FanV.push_back(TriIdx);
            }
        }
        FanU.clear();

        for (std::size_t K = 0u; K < QuadricCoeffCount; ++K)
        {
            Quadrics[static_cast<std::size_t>(V) * QuadricCoeffCount + K] +=
                Quadrics[static_cast<std::size_t>(U) * QuadricCoeffCount + K];
        }
        ++Stamps[U];
        ++Stamps[V];
        ++Result.ExecutedCollapses;
        ProposeAroundVertex(V);
    }

    Result.SurvivingTriangles.reserve(AliveCount);
    for (uint32_t T = 0u; T < TriCount; ++T)
    {
        if (TriAlive[T] != 0u)
        {
            Result.SurvivingTriangles.push_back(WorkingTris[T]);
        }
    }
    Result.EstimatedRmsError = static_cast<float>(std::sqrt(std::max(0.0, WorstSquaredError)));
    return Result;
}

[[nodiscard]] bool TestSphereOutsideFrustum(
    std::span<const ClusterFrustumPlane> Planes,
    Mathematics::Axis3D                  SphereCenter,
    float                                SphereRadius) noexcept
{
    for (const ClusterFrustumPlane& Plane : Planes)
    {
        const float SignedDist =
            Mathematics::EvaluateDot(Plane.UnitNormal, SphereCenter) + Plane.PlaneOffset;
        if (SignedDist < -SphereRadius)
        {
            return true;
        }
    }
    return false;
}

template <typename PodType>
void AppendPodBytes(std::vector<std::byte>& OutBytes, const PodType& SourceEntry)
{
    const std::size_t Offset = OutBytes.size();
    OutBytes.resize(Offset + sizeof(PodType));
    std::memcpy(OutBytes.data() + Offset, &SourceEntry, sizeof(PodType));
}

template <typename PodType>
bool ReadPodBytes(std::span<const std::byte>& Cursor, PodType& OutEntry) noexcept
{
    if (Cursor.size() < sizeof(PodType))
    {
        return false;
    }
    std::memcpy(&OutEntry, Cursor.data(), sizeof(PodType));
    Cursor = Cursor.subspan(sizeof(PodType));
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 CLUSTER PYRAMID BUILDER
//------------------------------------------------------------------------------------------------------------------------

void ClusterPyramid::Reset() noexcept
{
    Positions.clear();
    Triangles.clear();
    Clusters.clear();
    Groups.clear();
    GroupFinerClusterIndices.clear();
    RootClusterIndices.clear();
    HierarchyDepth           = 0u;
    FinestClusterCount       = 0u;
    CoarsestAllowedLevel     = 0xFFFFFFFFu;
    MaterialBoundariesLocked = true;
    ReductionStalled         = false;
    MaterialCoarsestAllowedLevels.fill(0xFFFFFFFFu);
}

bool ClusterPyramid::BuildFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    uint32_t                             MaxHierarchyLevels)
{
    ClusterLodBakeConfiguration BakeConfiguration {};
    BakeConfiguration.MaximumLevels = MaxHierarchyLevels;
    return BuildFromPolygonNetwork(Network, BakeConfiguration);
}

bool ClusterPyramid::BuildFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    const ClusterLodBakeConfiguration&   BakeConfiguration)
{
    Reset();
    CoarsestAllowedLevel     = BakeConfiguration.CoarsestAllowedLevel;
    MaterialBoundariesLocked = BakeConfiguration.LockMaterialBoundaries;
    if (Network.Vertices.empty() || Network.Faces.empty())
    {
        return false;
    }

    Mathematics::Axis3D SceneMin { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    Mathematics::Axis3D SceneMax { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    for (const GeometrySpace::Vertex& Vert : Network.Vertices)
    {
        SceneMin.X = std::min(SceneMin.X, Vert.Axes.X);
        SceneMin.Y = std::min(SceneMin.Y, Vert.Axes.Y);
        SceneMin.Z = std::min(SceneMin.Z, Vert.Axes.Z);
        SceneMax.X = std::max(SceneMax.X, Vert.Axes.X);
        SceneMax.Y = std::max(SceneMax.Y, Vert.Axes.Y);
        SceneMax.Z = std::max(SceneMax.Z, Vert.Axes.Z);
    }

    // Protect authored UV seams & sharp creases when opposite edges are linked on the network.
    // Open borders are NOT hard-locked here — GeometryCompiler classifies true open borders as
    // OpenSheetBorder (4*area perpendicular plane quadric + slide along single-use border edges)!
    bool AnyLinkedOpposite = false;
    for (const GeometrySpace::DirectedEdge& Edge : Network.Edges)
    {
        AnyLinkedOpposite = AnyLinkedOpposite || (Edge.OppositeEdgeIndex != GeometrySpace::InvalidNetworkIndex);
    }

    std::vector<uint8_t> GlobalSeamLockedVertices(Network.Vertices.size(), 0u);
    if (AnyLinkedOpposite)
    {
        for (const GeometrySpace::DirectedEdge& Edge : Network.Edges)
        {
            const bool SeamOrCrease =
                GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSeamCut) ||
                GeometrySpace::QueryEdgeAttributeEnabled(Edge.AttributeMask, GeometrySpace::EdgeAttributeMask::EnableSharpCrease);
            if (SeamOrCrease && Edge.OriginVertexIndex < GlobalSeamLockedVertices.size())
            {
                GlobalSeamLockedVertices[Edge.OriginVertexIndex] = 1u;
                if (Edge.NextEdgeIndex < Network.Edges.size())
                {
                    const uint32_t DestV = Network.Edges[Edge.NextEdgeIndex].OriginVertexIndex;
                    if (DestV < GlobalSeamLockedVertices.size())
                    {
                        GlobalSeamLockedVertices[DestV] = 1u;
                    }
                }
            }
        }
    }

    // Weld vertices by quantized position so triangle-soup assets (e.g. ControlVehicle) share
    // manifold connectivity across faces while per-corner normals, UVs, and material slots ride on triangles.
    std::vector<uint32_t> WeldRemap(Network.Vertices.size(), 0u);
    {
        struct WeldKey
        {
            int32_t X = 0;
            int32_t Y = 0;
            int32_t Z = 0;
            [[nodiscard]] bool operator==(const WeldKey& Rhs) const noexcept { return X == Rhs.X && Y == Rhs.Y && Z == Rhs.Z; }
        };
        struct WeldKeyHash
        {
            [[nodiscard]] std::size_t operator()(const WeldKey& Key) const noexcept
            {
                const uint32_t UX = static_cast<uint32_t>(Key.X);
                const uint32_t UY = static_cast<uint32_t>(Key.Y);
                const uint32_t UZ = static_cast<uint32_t>(Key.Z);
                return static_cast<std::size_t>((UX * 2654435761u) ^ (UY * 40503u) ^ (UZ * 73856093u));
            }
        };
        std::unordered_map<WeldKey, uint32_t, WeldKeyHash> WeldRegistry;
        WeldRegistry.reserve(Network.Vertices.size());
        std::vector<Mathematics::Axis3D> WeldedPositions;
        WeldedPositions.reserve(Network.Vertices.size());
        std::vector<uint8_t> WeldedLocks;
        WeldedLocks.reserve(Network.Vertices.size());

        const float MaxExtent      = std::max({ SceneMax.X - SceneMin.X, SceneMax.Y - SceneMin.Y, SceneMax.Z - SceneMin.Z, 1.0e-3f });
        const float WeldInvQuantum = 1.0f / std::max(5.0e-5f, MaxExtent * 4.0e-5f);
        for (std::size_t VIdx = 0u; VIdx < Network.Vertices.size(); ++VIdx)
        {
            const Mathematics::Axis3D Pos = Network.Vertices[VIdx].Axes;
            const WeldKey Key {
                static_cast<int32_t>(std::lround(Pos.X * WeldInvQuantum)),
                static_cast<int32_t>(std::lround(Pos.Y * WeldInvQuantum)),
                static_cast<int32_t>(std::lround(Pos.Z * WeldInvQuantum)),
            };
            const auto Found = WeldRegistry.find(Key);
            uint32_t WeldedIdx = 0u;
            if (Found == WeldRegistry.end())
            {
                WeldedIdx = static_cast<uint32_t>(WeldedPositions.size());
                WeldRegistry.emplace(Key, WeldedIdx);
                WeldedPositions.push_back(Pos);
                WeldedLocks.push_back(0u);
            }
            else
            {
                WeldedIdx = Found->second;
            }
            WeldRemap[VIdx] = WeldedIdx;
            if (GlobalSeamLockedVertices[VIdx] != 0u)
            {
                WeldedLocks[WeldedIdx] = 1u;
            }
        }
        Positions                = std::move(WeldedPositions);
        GlobalSeamLockedVertices = std::move(WeldedLocks);
    }

    const auto CornerTangents = GeometrySpace::EvaluateContinuousCornerTangents(Network);

    std::vector<ClusterTriangleEntry> SourceTriangles;
    SourceTriangles.reserve(Network.Faces.size() * 2u);

    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Network.Faces.size()); ++FaceIdx)
    {
        const GeometrySpace::Face& CurrentFace = Network.Faces[FaceIdx];
        if (CurrentFace.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex || CurrentFace.CornerCount < 3u)
        {
            continue;
        }

        std::vector<uint32_t> LoopEdges;
        LoopEdges.reserve(CurrentFace.CornerCount);
        uint32_t Walk = CurrentFace.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < CurrentFace.CornerCount && Walk < Network.Edges.size(); ++Step)
        {
            LoopEdges.push_back(Walk);
            Walk = Network.Edges[Walk].NextEdgeIndex;
        }

        for (std::size_t Fan = 1u; Fan + 1u < LoopEdges.size(); ++Fan)
        {
            const uint32_t E0 = LoopEdges[0u];
            const uint32_t E1 = LoopEdges[Fan];
            const uint32_t E2 = LoopEdges[Fan + 1u];

            const uint32_t V0 = WeldRemap[Network.Edges[E0].OriginVertexIndex];
            const uint32_t V1 = WeldRemap[Network.Edges[E1].OriginVertexIndex];
            const uint32_t V2 = WeldRemap[Network.Edges[E2].OriginVertexIndex];
            if (V0 == V1 || V1 == V2 || V2 == V0)
            {
                continue;
            }

            ClusterTriangleEntry Tri {};
            Tri.VertexIndex0      = V0;
            Tri.VertexIndex1      = V1;
            Tri.VertexIndex2      = V2;
            Tri.SourceFaceIndex   = FaceIdx;
            Tri.CornerUV0         = Network.Edges[E0].ParameterUV;
            Tri.CornerUV1         = Network.Edges[E1].ParameterUV;
            Tri.CornerUV2         = Network.Edges[E2].ParameterUV;
            Tri.PackedNormal0     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E0].NormalAxis);
            Tri.PackedNormal1     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E1].NormalAxis);
            Tri.PackedNormal2     = GeometrySpace::PackOctahedralDirection32(CornerTangents[E2].NormalAxis);
            Tri.PackedTangent0    = GeometrySpace::PackOctahedralDirection32(CornerTangents[E0].TangentAxis);
            Tri.MaterialOrdinal   = CurrentFace.PartGroupOrdinal;
            Tri.TangentSignScaled = (CornerTangents[E0].HandednessSign >= 0.0f) ? int16_t{ 1 } : int16_t{ -1 };
            SourceTriangles.push_back(Tri);
        }
    }

    if (SourceTriangles.empty())
    {
        return false;
    }

    // Material-boundary locks: any vertex shared by triangles of different material slots is locked
    // across all levels so multi-material borders remain crack-free and never bleed across slots.
    if (BakeConfiguration.LockMaterialBoundaries)
    {
        std::vector<int32_t> SeenMaterial(Positions.size(), -1);
        for (const ClusterTriangleEntry& Tri : SourceTriangles)
        {
            for (const uint32_t VIdx : { Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 })
            {
                if (SeenMaterial[VIdx] < 0)
                {
                    SeenMaterial[VIdx] = static_cast<int32_t>(Tri.MaterialOrdinal);
                }
                else if (SeenMaterial[VIdx] != static_cast<int32_t>(Tri.MaterialOrdinal))
                {
                    GlobalSeamLockedVertices[VIdx] = 1u;
                }
            }
        }
    }

    // Helper: append one <=128-triangle cluster and compute its tight bounding sphere + normal cone
    const auto EmitClusterFromSpan = [&](std::span<const ClusterTriangleEntry> SpanTris, uint32_t Level, uint32_t OriginGroupOrd) -> uint32_t
    {
        const uint32_t TriOffset = static_cast<uint32_t>(Triangles.size());
        Triangles.insert(Triangles.end(), SpanTris.begin(), SpanTris.end());

        ClusterHeaderEntry Header {};
        Header.TriangleStartOffset         = TriOffset;
        Header.TriangleCount               = static_cast<uint16_t>(SpanTris.size());
        Header.HierarchyLevel              = static_cast<uint8_t>(Level);
        Header.FinerSelfErrorMeters        = (OriginGroupOrd > 0u) ? Groups[OriginGroupOrd - 1u].MonotonicErrorMeters : 0.0f;
        Header.CoarserThresholdErrorMeters = 1.0e30f;
        Header.OriginGroupOrdinal          = OriginGroupOrd;
        Header.EnclosingGroupOrdinal       = 0u;
        Header.ClusterOrdinal              = static_cast<uint32_t>(Clusters.size());

        PopulateClusterBoundsNormalCone(
            Header,
            Positions,
            std::span<const ClusterTriangleEntry>(Triangles.data() + TriOffset, SpanTris.size()));

        Clusters.push_back(Header);
        return Header.ClusterOrdinal;
    };

    const auto SplitSpanIntoClusters = [&](
        std::span<const ClusterTriangleEntry> SpanTris,
        uint32_t                              Level,
        uint32_t                              OriginGroupOrd,
        std::vector<uint32_t>&                OutClusterIndices)
    {
        if (SpanTris.empty())
        {
            return;
        }
        if (SpanTris.size() <= MaxTrianglesPerCluster)
        {
            OutClusterIndices.push_back(EmitClusterFromSpan(SpanTris, Level, OriginGroupOrd));
            return;
        }
        std::vector<ClusterTriangleEntry> PartitionTris;
        PartitionTris.reserve(MaxTrianglesPerCluster);
        SplitTrianglesSpatially(Positions, SpanTris, MaxTrianglesPerCluster, [&](std::span<const uint32_t> PartIds)
        {
            PartitionTris.clear();
            for (const uint32_t Id : PartIds)
            {
                PartitionTris.push_back(SpanTris[Id]);
            }
            OutClusterIndices.push_back(EmitClusterFromSpan(PartitionTris, Level, OriginGroupOrd));
        });
    };

    // Partition Level-0 source triangles per material bucket using Hoare quickselect spatial bisection
    // so most clusters hold a full 128 spatially coherent triangles while staying 100% material-pure.
    std::sort(SourceTriangles.begin(), SourceTriangles.end(), [](const ClusterTriangleEntry& Left, const ClusterTriangleEntry& Right) noexcept
    {
        return Left.MaterialOrdinal < Right.MaterialOrdinal;
    });

    std::vector<uint32_t> CurrentLevelClusters;
    for (std::size_t BucketStart = 0u; BucketStart < SourceTriangles.size();)
    {
        std::size_t BucketEnd = BucketStart + 1u;
        while (BucketEnd < SourceTriangles.size() &&
               SourceTriangles[BucketEnd].MaterialOrdinal == SourceTriangles[BucketStart].MaterialOrdinal)
        {
            ++BucketEnd;
        }
        SplitSpanIntoClusters(
            std::span<const ClusterTriangleEntry>(SourceTriangles.data() + BucketStart, BucketEnd - BucketStart),
            0u,
            0u,
            CurrentLevelClusters);
        BucketStart = BucketEnd;
    }

    FinestClusterCount = static_cast<uint32_t>(CurrentLevelClusters.size());
    HierarchyDepth     = 1u;

    const uint32_t MaxLevels       = std::clamp(BakeConfiguration.MaximumLevels, 1u, 24u);
    const uint32_t GroupClusterCap = std::clamp(BakeConfiguration.TargetGroupClusters, 2u, 16u);
    const float    KeepRatio       = std::clamp(BakeConfiguration.SimplifyKeepRatio, 0.10f, 0.90f);
    const double   RelErrorLimit   = static_cast<double>(std::max(0.05f, BakeConfiguration.RelativeErrorLimit));

    const auto PackGlobalEdgeKey = [](uint32_t VA, uint32_t VB) noexcept -> uint64_t
    {
        const uint64_t Lo = std::min(VA, VB);
        const uint64_t Hi = std::max(VA, VB);
        return (Lo << 32u) | Hi;
    };

    //--------------------------------------------------------------------------------------------------------------------
    //             ITERATIVE SHARED-EDGE CLUSTER GROUPING + DAG LEVEL CONSTRUCTION (GEOMETRYCOMPILER PARITY)
    //--------------------------------------------------------------------------------------------------------------------

    for (uint32_t Level = 1u; Level < MaxLevels && CurrentLevelClusters.size() > 1u; ++Level)
    {
        // 1. Edge ownership across the active level:
        //    Owner >= 0 : seen once so far by cluster Owner (stays >=0 iff true open border across the level)
        //    Owner == -1: seen exactly twice across this level (manifold interior edge)
        //    Owner == -2: seen 3+ times across this level (non-manifold -> always LockedBoundary)
        std::unordered_map<uint64_t, int32_t> LevelEdgeOwner;
        LevelEdgeOwner.reserve(CurrentLevelClusters.size() * MaxTrianglesPerCluster * 2u);

        std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> SharedBorderWeights;
        SharedBorderWeights.reserve(CurrentLevelClusters.size());
        for (const uint32_t ClusterId : CurrentLevelClusters)
        {
            SharedBorderWeights[ClusterId] = {};
        }

        for (const uint32_t ClusterId : CurrentLevelClusters)
        {
            const ClusterHeaderEntry& Header = Clusters[ClusterId];
            const uint8_t ClusterMat = Header.DominantMaterialOrdinal;
            for (uint32_t Off = 0u; Off < Header.TriangleCount; ++Off)
            {
                const ClusterTriangleEntry& Tri = Triangles[Header.TriangleStartOffset + Off];
                const std::array<uint32_t, 3u> V {{ Tri.VertexIndex0, Tri.VertexIndex1, Tri.VertexIndex2 }};
                for (std::size_t Side = 0u; Side < 3u; ++Side)
                {
                    const uint64_t EKey = PackGlobalEdgeKey(V[Side], V[(Side + 1u) % 3u]);
                    const auto Found = LevelEdgeOwner.find(EKey);
                    if (Found == LevelEdgeOwner.end())
                    {
                        LevelEdgeOwner.emplace(EKey, static_cast<int32_t>(ClusterId));
                    }
                    else if (Found->second >= 0)
                    {
                        const uint32_t OtherCluster = static_cast<uint32_t>(Found->second);
                        Found->second = -1;
                        if (OtherCluster != ClusterId &&
                            Clusters[OtherCluster].DominantMaterialOrdinal == ClusterMat)
                        {
                            SharedBorderWeights[OtherCluster][ClusterId] += 1u;
                            SharedBorderWeights[ClusterId][OtherCluster] += 1u;
                        }
                    }
                    else
                    {
                        Found->second = -2;
                    }
                }
            }
        }

        // 2. Greedy shared-edge grouping: grow each group up to GroupClusterCap by picking the unassigned
        //    same-material cluster sharing the most border edges with the current group frontier!
        std::unordered_map<uint32_t, std::size_t> AssignedGroupOf;
        AssignedGroupOf.reserve(CurrentLevelClusters.size());
        std::vector<std::vector<uint32_t>> CandidateGroups;
        CandidateGroups.reserve((CurrentLevelClusters.size() + GroupClusterCap - 1u) / GroupClusterCap);

        for (const uint32_t SeedCluster : CurrentLevelClusters)
        {
            if (AssignedGroupOf.find(SeedCluster) != AssignedGroupOf.end())
            {
                continue;
            }
            const std::size_t GroupIdx = CandidateGroups.size();
            std::vector<uint32_t> GroupClusters;
            GroupClusters.reserve(GroupClusterCap);
            GroupClusters.push_back(SeedCluster);
            AssignedGroupOf.emplace(SeedCluster, GroupIdx);

            std::unordered_map<uint32_t, uint32_t> FrontierWeights;
            const auto AbsorbCluster = [&](uint32_t AbsorbedId)
            {
                for (const auto& [PeerId, SharedEdges] : SharedBorderWeights[AbsorbedId])
                {
                    if (AssignedGroupOf.find(PeerId) == AssignedGroupOf.end())
                    {
                        FrontierWeights[PeerId] += SharedEdges;
                    }
                }
            };
            AbsorbCluster(SeedCluster);

            while (GroupClusters.size() < GroupClusterCap && !FrontierWeights.empty())
            {
                uint32_t BestPeer   = GeometrySpace::InvalidNetworkIndex;
                uint32_t BestWeight = 0u;
                for (const auto& [PeerId, Weight] : FrontierWeights)
                {
                    if (Weight > BestWeight || (Weight == BestWeight && PeerId < BestPeer))
                    {
                        BestWeight = Weight;
                        BestPeer   = PeerId;
                    }
                }
                if (BestPeer == GeometrySpace::InvalidNetworkIndex)
                {
                    break;
                }
                FrontierWeights.erase(BestPeer);
                AssignedGroupOf.emplace(BestPeer, GroupIdx);
                GroupClusters.push_back(BestPeer);
                AbsorbCluster(BestPeer);
            }
            CandidateGroups.push_back(std::move(GroupClusters));
        }

        // Fold leftover singleton clusters into the neighbouring same-material group they share the most
        // border edges with (up to GroupClusterCap * 2), matching GeometryCompiler::buildClusterHierarchy!
        for (std::size_t GIdx = 0u; GIdx < CandidateGroups.size(); ++GIdx)
        {
            if (CandidateGroups[GIdx].size() != 1u)
            {
                continue;
            }
            const uint32_t LoneCluster = CandidateGroups[GIdx][0u];
            std::size_t BestTargetGroup = CandidateGroups.size();
            uint32_t    BestShared      = 0u;
            for (const auto& [PeerId, SharedEdges] : SharedBorderWeights[LoneCluster])
            {
                const std::size_t TargetGroup = AssignedGroupOf[PeerId];
                if (TargetGroup != GIdx &&
                    !CandidateGroups[TargetGroup].empty() &&
                    CandidateGroups[TargetGroup].size() < static_cast<std::size_t>(GroupClusterCap) * 2u &&
                    SharedEdges > BestShared)
                {
                    BestShared      = SharedEdges;
                    BestTargetGroup = TargetGroup;
                }
            }
            if (BestTargetGroup < CandidateGroups.size())
            {
                CandidateGroups[BestTargetGroup].push_back(LoneCluster);
                AssignedGroupOf[LoneCluster] = BestTargetGroup;
                CandidateGroups[GIdx].clear();
            }
        }

        // Disconnected components of the same material (e.g. 4 separate vehicle tyres/rims) share zero edges;
        // pair nearby spatial singletons of the same material only when within safe local reach so individual
        // curved components still simplify internally without bridging distant parts.
        for (std::size_t GIdx = 0u; GIdx < CandidateGroups.size(); ++GIdx)
        {
            if (CandidateGroups[GIdx].size() != 1u)
            {
                continue;
            }
            const uint32_t AnchorId = CandidateGroups[GIdx][0u];
            const ClusterHeaderEntry& Anchor = Clusters[AnchorId];
            std::size_t BestNearGroup = CandidateGroups.size();
            float       BestDist      = std::numeric_limits<float>::max();
            for (std::size_t OtherG = 0u; OtherG < CandidateGroups.size(); ++OtherG)
            {
                if (OtherG == GIdx || CandidateGroups[OtherG].empty() ||
                    CandidateGroups[OtherG].size() >= GroupClusterCap)
                {
                    continue;
                }
                const ClusterHeaderEntry& Peer = Clusters[CandidateGroups[OtherG][0u]];
                if (Peer.DominantMaterialOrdinal != Anchor.DominantMaterialOrdinal)
                {
                    continue;
                }
                const float Dist = Mathematics::EvaluateDistance(Anchor.SphereCenter, Peer.SphereCenter);
                const float MaxReach = (Anchor.SphereRadiusMeters + Peer.SphereRadiusMeters) * 1.35f;
                if (Dist <= MaxReach && Dist < BestDist)
                {
                    BestDist      = Dist;
                    BestNearGroup = OtherG;
                }
            }
            if (BestNearGroup < CandidateGroups.size())
            {
                CandidateGroups[BestNearGroup].push_back(AnchorId);
                AssignedGroupOf[AnchorId] = BestNearGroup;
                CandidateGroups[GIdx].clear();
            }
        }

        // 3. Simplify each group, build its monotone (Error, EnclosingSphere) DAG entry, and split into next-level clusters
        std::vector<uint32_t> NextLevelClusters;
        uint32_t LevelTrianglesAfter  = 0u;
        uint32_t LevelTrianglesBefore = 0u;
        bool     AnyGroupReduced      = false;

        for (const std::vector<uint32_t>& GroupClusters : CandidateGroups)
        {
            if (GroupClusters.empty())
            {
                continue;
            }

            std::size_t TotalMergedTris = 0u;
            for (const uint32_t CId : GroupClusters)
            {
                TotalMergedTris += Clusters[CId].TriangleCount;
            }

            std::vector<ClusterTriangleEntry> MergedTris;
            MergedTris.reserve(TotalMergedTris);
            for (const uint32_t CId : GroupClusters)
            {
                const ClusterHeaderEntry& Finer = Clusters[CId];
                MergedTris.insert(
                    MergedTris.end(),
                    Triangles.begin() + Finer.TriangleStartOffset,
                    Triangles.begin() + Finer.TriangleStartOffset + Finer.TriangleCount);
            }

            // Classify vertices inside this group vs. level-wide edge ownership:
            //   LocalUses > 2 or LevelOwner == -2 -> LockedBoundary (non-manifold)
            //   LocalUses == 1 and LevelOwner >= 0 -> OpenSheetBorder (true open sheet outline)
            //   LocalUses == 1 and LevelOwner == -1 -> LockedBoundary (shared with another group this level!)
            //   LocalUses == 2 -> FreeInterior (unless touched by global UV-seam / material lock)
            std::unordered_map<uint64_t, uint32_t> GroupLocalEdgeUse;
            GroupLocalEdgeUse.reserve(MergedTris.size() * 3u);
            for (const ClusterTriangleEntry& Tri : MergedTris)
            {
                GroupLocalEdgeUse[PackGlobalEdgeKey(Tri.VertexIndex0, Tri.VertexIndex1)] += 1u;
                GroupLocalEdgeUse[PackGlobalEdgeKey(Tri.VertexIndex1, Tri.VertexIndex2)] += 1u;
                GroupLocalEdgeUse[PackGlobalEdgeKey(Tri.VertexIndex2, Tri.VertexIndex0)] += 1u;
            }

            std::unordered_map<uint32_t, SimplificationVertexConstraint> GroupVertexKinds;
            GroupVertexKinds.reserve(MergedTris.size() * 2u);
            const auto PromoteConstraint = [&](uint32_t GlobalV, SimplificationVertexConstraint Kind)
            {
                const auto Found = GroupVertexKinds.find(GlobalV);
                if (Found == GroupVertexKinds.end() || static_cast<uint8_t>(Found->second) < static_cast<uint8_t>(Kind))
                {
                    GroupVertexKinds[GlobalV] = Kind;
                }
            };

            for (const auto& [EKey, LocalUses] : GroupLocalEdgeUse)
            {
                const int32_t Owner = LevelEdgeOwner[EKey];
                SimplificationVertexConstraint EdgeKind = SimplificationVertexConstraint::FreeInterior;
                if (Owner == -2 || LocalUses > 2u)
                {
                    EdgeKind = SimplificationVertexConstraint::LockedBoundary;
                }
                else if (LocalUses == 1u)
                {
                    EdgeKind = (Owner >= 0)
                        ? SimplificationVertexConstraint::OpenSheetBorder
                        : SimplificationVertexConstraint::LockedBoundary;
                }
                if (EdgeKind != SimplificationVertexConstraint::FreeInterior)
                {
                    const uint32_t VA = static_cast<uint32_t>(EKey >> 32u);
                    const uint32_t VB = static_cast<uint32_t>(EKey & 0xFFFFFFFFu);
                    PromoteConstraint(VA, EdgeKind);
                    PromoteConstraint(VB, EdgeKind);
                }
            }

            // Compute monotone enclosing sphere over every consumed cluster's LOD sphere + max finer group error
            float GroupMonotoneError = 0.0f;
            Mathematics::Axis3D CenterSum { 0.0f, 0.0f, 0.0f };
            struct SphereSample
            {
                Mathematics::Axis3D Center {};
                float               Radius = 0.0f;
            };
            std::vector<SphereSample> FinerLodSpheres;
            FinerLodSpheres.reserve(GroupClusters.size());

            for (const uint32_t CId : GroupClusters)
            {
                const ClusterHeaderEntry& Finer = Clusters[CId];
                if (Finer.OriginGroupOrdinal > 0u)
                {
                    const ClusterGroupEntry& PrevGroup = Groups[Finer.OriginGroupOrdinal - 1u];
                    GroupMonotoneError = std::max(GroupMonotoneError, PrevGroup.MonotonicErrorMeters);
                    FinerLodSpheres.push_back({ PrevGroup.SphereCenter, PrevGroup.SphereRadiusMeters });
                    CenterSum = CenterSum + PrevGroup.SphereCenter;
                }
                else
                {
                    FinerLodSpheres.push_back({ Finer.SphereCenter, Finer.SphereRadiusMeters });
                    CenterSum = CenterSum + Finer.SphereCenter;
                }
            }

            const Mathematics::Axis3D GroupCenter = CenterSum * (1.0f / static_cast<float>(FinerLodSpheres.size()));
            float GroupRadius = 0.0f;
            for (const SphereSample& Sph : FinerLodSpheres)
            {
                GroupRadius = std::max(GroupRadius, Mathematics::EvaluateDistance(Sph.Center, GroupCenter) + Sph.Radius);
            }

            const uint32_t BeforeCount = static_cast<uint32_t>(MergedTris.size());
            const uint32_t TargetCount = std::max(1u, static_cast<uint32_t>(std::floor(static_cast<float>(BeforeCount) * KeepRatio)));
            const double   GroupArea   = ComputeTriangleSpanSurfaceArea(Positions, MergedTris);
            const double   ErrorCeiling = RelErrorLimit * std::sqrt(std::max(1.0e-16, GroupArea / static_cast<double>(TargetCount)));

            const GroupSimplificationOutput Simplified = SimplifyClusterGroupQuadric(
                Positions,
                MergedTris,
                [&](uint32_t GlobalV) noexcept -> SimplificationVertexConstraint
                {
                    if (GlobalV < GlobalSeamLockedVertices.size() && GlobalSeamLockedVertices[GlobalV] != 0u)
                    {
                        return SimplificationVertexConstraint::LockedBoundary;
                    }
                    const auto Found = GroupVertexKinds.find(GlobalV);
                    return (Found != GroupVertexKinds.end()) ? Found->second : SimplificationVertexConstraint::FreeInterior;
                },
                TargetCount,
                ErrorCeiling);

            LevelTrianglesBefore += BeforeCount;

            if (Simplified.SurvivingTriangles.empty() || Simplified.SurvivingTriangles.size() >= MergedTris.size())
            {
                // Unreduced group remains active for potential pairing or becomes a DAG root
                for (const uint32_t CId : GroupClusters)
                {
                    NextLevelClusters.push_back(CId);
                    LevelTrianglesAfter += Clusters[CId].TriangleCount;
                }
                continue;
            }

            // Strictly monotone error so coarsening transitions are strictly nested across the DAG
            GroupMonotoneError = std::max(GroupMonotoneError + 1.0e-5f, std::max(GroupMonotoneError, Simplified.EstimatedRmsError));

            const uint32_t FinerStartOffset = static_cast<uint32_t>(GroupFinerClusterIndices.size());
            for (const uint32_t CId : GroupClusters)
            {
                GroupFinerClusterIndices.push_back(CId);
            }

            ClusterGroupEntry GroupRecord {};
            GroupRecord.SphereCenter         = GroupCenter;
            GroupRecord.SphereRadiusMeters   = GroupRadius * 1.0001f; // Float32 enclosure guard matching GeometryCompiler
            GroupRecord.MonotonicErrorMeters = GroupMonotoneError;
            GroupRecord.FinerClusterStart    = FinerStartOffset;
            GroupRecord.FinerClusterCount    = static_cast<uint16_t>(GroupClusters.size());
            GroupRecord.HierarchyLevel       = static_cast<uint8_t>(Level);
            GroupRecord.MaterialOrdinal      = Clusters[GroupClusters[0u]].DominantMaterialOrdinal;
            Groups.push_back(GroupRecord);

            const uint32_t NewOneBasedGroupOrd = static_cast<uint32_t>(Groups.size());
            for (const uint32_t CId : GroupClusters)
            {
                Clusters[CId].EnclosingGroupOrdinal       = NewOneBasedGroupOrd;
                Clusters[CId].CoarserThresholdErrorMeters = GroupMonotoneError;
            }

            SplitSpanIntoClusters(Simplified.SurvivingTriangles, Level, NewOneBasedGroupOrd, NextLevelClusters);
            LevelTrianglesAfter += static_cast<uint32_t>(Simplified.SurvivingTriangles.size());
            AnyGroupReduced = true;
        }

        if (!AnyGroupReduced)
        {
            break;
        }

        CurrentLevelClusters = std::move(NextLevelClusters);
        ++HierarchyDepth;

        // Stop when locked seams/borders dominate and a full pass reduces fewer than 8% of triangles
        if (static_cast<double>(LevelTrianglesAfter) > static_cast<double>(LevelTrianglesBefore) * 0.92)
        {
            ReductionStalled = (CurrentLevelClusters.size() > 1u);
            break;
        }
    }

    // Populate DAG root clusters: every cluster that was never consumed by a coarser group
    RootClusterIndices.clear();
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Clusters.size()); ++Idx)
    {
        if (Clusters[Idx].EnclosingGroupOrdinal == 0u)
        {
            RootClusterIndices.push_back(Idx);
        }
    }

    return !Clusters.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                  RUNTIME CRACK-FREE TOP-DOWN DAG CUT & ORACLE
//------------------------------------------------------------------------------------------------------------------------

float ClusterPyramid::EvaluateGroupProjectedPixelError(
    uint32_t            OneBasedGroupOrdinal,
    Mathematics::Axis3D CameraLocalPosition,
    float               PixelsPerRadian,
    float               NearClipDistanceMeters) const noexcept
{
    if (OneBasedGroupOrdinal == 0u || OneBasedGroupOrdinal > Groups.size())
    {
        return 0.0f;
    }
    const ClusterGroupEntry& Group = Groups[OneBasedGroupOrdinal - 1u];
    const float Dist = Mathematics::EvaluateDistance(Group.SphereCenter, CameraLocalPosition) - Group.SphereRadiusMeters;
    const float SafeNear = std::max(1.0e-5f, NearClipDistanceMeters);
    return (Group.MonotonicErrorMeters * std::max(1.0f, PixelsPerRadian)) / std::max(Dist, SafeNear);
}

ClusterCutSelection ClusterPyramid::SelectMonotonicCut(
    Mathematics::Axis3D                  CameraLocalPosition,
    float                                PixelsPerRadian,
    float                                MaxAllowedPixelError,
    bool                                 EnableNormalConeBackfaceCull,
    std::span<const ClusterFrustumPlane> LocalFrustumPlanes,
    int32_t                              ForcedHierarchyLevel,
    float                                NearClipDistanceMeters) const
{
    ClusterCutSelection Selection {};
    if (Clusters.empty())
    {
        return Selection;
    }

    const float SafeFocal = std::max(1.0f, PixelsPerRadian);
    const float Threshold = std::max(0.0f, MaxAllowedPixelError);
    const float SafeNear  = std::max(1.0e-5f, NearClipDistanceMeters);

    const auto EmitAcceptedCluster = [&](uint32_t ClusterIdx)
    {
        const ClusterHeaderEntry& Cluster = Clusters[ClusterIdx];
        if (!LocalFrustumPlanes.empty() &&
            TestSphereOutsideFrustum(LocalFrustumPlanes, Cluster.SphereCenter, Cluster.SphereRadiusMeters))
        {
            ++Selection.CulledFrustumCount;
            return;
        }
        if (EnableNormalConeBackfaceCull && Cluster.NormalConeSinCutoff < 1.5f)
        {
            const Mathematics::Axis3D ToCluster = Cluster.SphereCenter - CameraLocalPosition;
            const float Dist = Mathematics::EvaluateLength(ToCluster);
            if (Mathematics::EvaluateDot(ToCluster, Cluster.NormalConeDirection) >=
                Cluster.NormalConeSinCutoff * Dist + Cluster.SphereRadiusMeters)
            {
                ++Selection.CulledBackfaceCount;
                return;
            }
        }
        Selection.ActiveClusterIndices.push_back(ClusterIdx);
        Selection.ActiveTriangleCount += Cluster.TriangleCount;
    };

    // Explicit forced hierarchy level inspection (GeometryCompiler parity)
    if (ForcedHierarchyLevel >= 0)
    {
        const uint32_t ClampedLevel = std::min(
            static_cast<uint32_t>(ForcedHierarchyLevel),
            (HierarchyDepth > 0u) ? (HierarchyDepth - 1u) : 0u);
        for (uint32_t CIdx = 0u; CIdx < static_cast<uint32_t>(Clusters.size()); ++CIdx)
        {
            if (Clusters[CIdx].HierarchyLevel == ClampedLevel)
            {
                ++Selection.TestedGroupCount;
                EmitAcceptedCluster(CIdx);
            }
        }
        return Selection;
    }

    // Legacy v1/v2 deserialized payload fallback when DAG Groups were not stored in the container
    if (Groups.empty() && RootClusterIndices.empty())
    {
        std::unordered_map<uint32_t, float> LegacyGroupDist;
        for (const ClusterHeaderEntry& Cluster : Clusters)
        {
            if (Cluster.EnclosingGroupOrdinal == 0u)
            {
                continue;
            }
            const float RawDist = std::max(
                SafeNear,
                Mathematics::EvaluateDistance(CameraLocalPosition, Cluster.SphereCenter) - Cluster.SphereRadiusMeters);
            const auto Found = LegacyGroupDist.find(Cluster.EnclosingGroupOrdinal);
            if (Found == LegacyGroupDist.end() || RawDist < Found->second)
            {
                LegacyGroupDist[Cluster.EnclosingGroupOrdinal] = RawDist;
            }
        }
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Clusters.size()); ++Idx)
        {
            const ClusterHeaderEntry& Cluster = Clusters[Idx];
            const uint32_t EffectiveCap = std::min(CoarsestAllowedLevel, MaterialCoarsestAllowedLevels[Cluster.DominantMaterialOrdinal]);
            if (Cluster.HierarchyLevel > EffectiveCap)
            {
                continue;
            }
            const float SelfDist = std::max(
                SafeNear,
                Mathematics::EvaluateDistance(CameraLocalPosition, Cluster.SphereCenter) - Cluster.SphereRadiusMeters);
            const float GroupDist = (Cluster.EnclosingGroupOrdinal != 0u) ? LegacyGroupDist[Cluster.EnclosingGroupOrdinal] : SelfDist;
            const bool CoarserCapped =
                Cluster.EnclosingGroupOrdinal != 0u &&
                (static_cast<uint32_t>(Cluster.HierarchyLevel) + 1u > EffectiveCap);
            const float ProjSelf    = (Cluster.FinerSelfErrorMeters * SafeFocal) / SelfDist;
            const float ProjCoarser = (CoarserCapped || Cluster.CoarserThresholdErrorMeters >= 1.0e20f)
                ? 1.0e30f
                : ((Cluster.CoarserThresholdErrorMeters * SafeFocal) / GroupDist);
            if (ProjSelf <= Threshold && ProjCoarser > Threshold)
            {
                EmitAcceptedCluster(Idx);
            }
        }
        return Selection;
    }

    // Shared single-group coarseness test: true when group exceeds pixel threshold OR exceeds per-asset/per-material level cap
    const auto QueryGroupTooCoarse = [&](uint32_t ZeroBasedGroupIdx) noexcept -> bool
    {
        const ClusterGroupEntry& Group = Groups[ZeroBasedGroupIdx];
        const uint32_t EffectiveCap = std::min(CoarsestAllowedLevel, MaterialCoarsestAllowedLevels[Group.MaterialOrdinal]);
        if (Group.HierarchyLevel > EffectiveCap)
        {
            return true;
        }
        const float Dist = Mathematics::EvaluateDistance(Group.SphereCenter, CameraLocalPosition) - Group.SphereRadiusMeters;
        return (Group.MonotonicErrorMeters * SafeFocal) > Threshold * std::max(Dist, SafeNear);
    };

    // Top-down DAG walk starting at RootClusterIndices (GeometryCompiler::selectClusters parity):
    //   VisitedGroupState[g] ==  1 : group already expanded (too coarse)
    //   VisitedGroupState[g] == -1 : group already accepted (detailed enough; emit all peer clusters from g)
    std::vector<int8_t>   VisitedGroupMarker(Groups.size(), 0);
    std::vector<uint32_t> ExpansionStack;
    ExpansionStack.reserve(Groups.size());

    const auto ReachCluster = [&](uint32_t ClusterIdx)
    {
        ++Selection.TestedGroupCount;
        const ClusterHeaderEntry& Cluster = Clusters[ClusterIdx];
        if (Cluster.OriginGroupOrdinal == 0u)
        {
            EmitAcceptedCluster(ClusterIdx);
            return;
        }
        const uint32_t GroupIdx = Cluster.OriginGroupOrdinal - 1u;
        const int8_t Marker = VisitedGroupMarker[GroupIdx];
        if (Marker == 1)
        {
            return; // Already expanded into finer clusters
        }
        if (Marker == -1)
        {
            EmitAcceptedCluster(ClusterIdx);
            return;
        }
        if (QueryGroupTooCoarse(GroupIdx))
        {
            VisitedGroupMarker[GroupIdx] = 1;
            const ClusterGroupEntry& Group = Groups[GroupIdx];
            if (!LocalFrustumPlanes.empty() &&
                TestSphereOutsideFrustum(LocalFrustumPlanes, Group.SphereCenter, Group.SphereRadiusMeters))
            {
                ++Selection.CulledFrustumCount;
                return; // Entire DAG subtree enclosed by Group.Sphere is outside the view frustum!
            }
            ExpansionStack.push_back(GroupIdx);
        }
        else
        {
            VisitedGroupMarker[GroupIdx] = -1;
            EmitAcceptedCluster(ClusterIdx);
        }
    };

    for (const uint32_t RootIdx : RootClusterIndices)
    {
        ReachCluster(RootIdx);
    }
    while (!ExpansionStack.empty())
    {
        const uint32_t GroupIdx = ExpansionStack.back();
        ExpansionStack.pop_back();
        const ClusterGroupEntry& Group = Groups[GroupIdx];
        for (uint32_t Off = 0u; Off < Group.FinerClusterCount; ++Off)
        {
            ReachCluster(GroupFinerClusterIndices[Group.FinerClusterStart + Off]);
        }
    }

    // Keep deterministic ascending cluster order so cut signatures are invariant to stack pop order
    std::sort(Selection.ActiveClusterIndices.begin(), Selection.ActiveClusterIndices.end());
    return Selection;
}

std::vector<uint32_t> ClusterPyramid::SelectExhaustiveReferenceCut(
    Mathematics::Axis3D CameraLocalPosition,
    float               PixelsPerRadian,
    float               MaxAllowedPixelError,
    float               NearClipDistanceMeters) const
{
    std::vector<uint32_t> Picked;
    if (Clusters.empty())
    {
        return Picked;
    }
    const float SafeFocal = std::max(1.0f, PixelsPerRadian);
    const float Threshold = std::max(0.0f, MaxAllowedPixelError);
    const float SafeNear  = std::max(1.0e-5f, NearClipDistanceMeters);

    const auto QueryGroupTooCoarse = [&](uint32_t OneBasedGroupOrd) noexcept -> bool
    {
        const ClusterGroupEntry& Group = Groups[OneBasedGroupOrd - 1u];
        const uint32_t EffectiveCap = std::min(CoarsestAllowedLevel, MaterialCoarsestAllowedLevels[Group.MaterialOrdinal]);
        if (Group.HierarchyLevel > EffectiveCap)
        {
            return true;
        }
        const float Dist = Mathematics::EvaluateDistance(Group.SphereCenter, CameraLocalPosition) - Group.SphereRadiusMeters;
        return (Group.MonotonicErrorMeters * SafeFocal) > Threshold * std::max(Dist, SafeNear);
    };

    for (uint32_t CIdx = 0u; CIdx < static_cast<uint32_t>(Clusters.size()); ++CIdx)
    {
        const ClusterHeaderEntry& Cluster = Clusters[CIdx];
        const bool OwnTooCoarse    = (Cluster.OriginGroupOrdinal > 0u) && QueryGroupTooCoarse(Cluster.OriginGroupOrdinal);
        const bool CoarserTooCoarse = (Cluster.EnclosingGroupOrdinal == 0u) || QueryGroupTooCoarse(Cluster.EnclosingGroupOrdinal);
        if (!OwnTooCoarse && CoarserTooCoarse)
        {
            Picked.push_back(CIdx);
        }
    }
    return Picked;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LEVEL SUMMARIES & SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<ClusterLodLevelSummary> ClusterPyramid::SummarizeLevels() const
{
    std::vector<ClusterLodLevelSummary> Summaries;
    Summaries.resize(HierarchyDepth);
    for (uint32_t Level = 0u; Level < HierarchyDepth; ++Level)
    {
        Summaries[Level].Level = Level;
    }
    for (const ClusterGroupEntry& Group : Groups)
    {
        if (Group.HierarchyLevel < Summaries.size())
        {
            ++Summaries[Group.HierarchyLevel].GroupCount;
        }
    }
    for (const ClusterHeaderEntry& Cluster : Clusters)
    {
        if (Cluster.HierarchyLevel >= Summaries.size())
        {
            continue;
        }
        ClusterLodLevelSummary& Summary = Summaries[Cluster.HierarchyLevel];
        ++Summary.ClusterCount;
        Summary.TriangleCount += Cluster.TriangleCount;
        Summary.MaximumErrorMeters = std::max(Summary.MaximumErrorMeters, Cluster.FinerSelfErrorMeters);
        for (uint32_t TriIdx = 0u; TriIdx < Cluster.TriangleCount; ++TriIdx)
        {
            const uint16_t Ordinal = Triangles[Cluster.TriangleStartOffset + TriIdx].MaterialOrdinal;
            bool Known = false;
            for (const uint16_t Existing : Summary.MaterialOrdinals)
            {
                Known = Known || (Existing == Ordinal);
            }
            if (!Known)
            {
                Summary.MaterialOrdinals.push_back(Ordinal);
            }
        }
    }
    return Summaries;
}

std::vector<std::byte> ClusterPyramid::SerializeToBinaryBytes() const
{
    std::vector<std::byte> Bytes;
    Bytes.reserve(
        48u +
        Positions.size() * sizeof(Mathematics::Axis3D) +
        Triangles.size() * sizeof(ClusterTriangleEntry) +
        Clusters.size() * sizeof(ClusterHeaderEntry) +
        Groups.size() * sizeof(ClusterGroupEntry) +
        (GroupFinerClusterIndices.size() + RootClusterIndices.size()) * sizeof(uint32_t) +
        MaterialCoarsestAllowedLevels.size() * sizeof(uint32_t));

    AppendPodBytes(Bytes, ClusterBinaryMagic);
    AppendPodBytes(Bytes, ClusterBinaryVersion);
    AppendPodBytes(Bytes, HierarchyDepth);
    AppendPodBytes(Bytes, FinestClusterCount);
    const auto PosCount   = static_cast<uint32_t>(Positions.size());
    const auto TriCount   = static_cast<uint32_t>(Triangles.size());
    const auto ClsCount   = static_cast<uint32_t>(Clusters.size());
    const auto GrpCount   = static_cast<uint32_t>(Groups.size());
    const auto FinerCount = static_cast<uint32_t>(GroupFinerClusterIndices.size());
    const auto RootCount  = static_cast<uint32_t>(RootClusterIndices.size());
    AppendPodBytes(Bytes, PosCount);
    AppendPodBytes(Bytes, TriCount);
    AppendPodBytes(Bytes, ClsCount);
    AppendPodBytes(Bytes, CoarsestAllowedLevel);

    for (const auto& Pos : Positions)
    {
        AppendPodBytes(Bytes, Pos);
    }
    for (const auto& Tri : Triangles)
    {
        AppendPodBytes(Bytes, Tri);
    }
    for (const auto& Cls : Clusters)
    {
        AppendPodBytes(Bytes, Cls);
    }

    // v3 DAG trailer
    AppendPodBytes(Bytes, GrpCount);
    AppendPodBytes(Bytes, FinerCount);
    AppendPodBytes(Bytes, RootCount);
    for (const auto& Grp : Groups)
    {
        AppendPodBytes(Bytes, Grp);
    }
    for (const uint32_t FinerIdx : GroupFinerClusterIndices)
    {
        AppendPodBytes(Bytes, FinerIdx);
    }
    for (const uint32_t RootIdx : RootClusterIndices)
    {
        AppendPodBytes(Bytes, RootIdx);
    }

    // v4 per-material coarsest-allowed LOD cap trailer
    const auto MatCapCount = static_cast<uint32_t>(MaterialCoarsestAllowedLevels.size());
    AppendPodBytes(Bytes, MatCapCount);
    for (const uint32_t CapLevel : MaterialCoarsestAllowedLevels)
    {
        AppendPodBytes(Bytes, CapLevel);
    }
    return Bytes;
}

bool ClusterPyramid::DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes)
{
    Reset();
    std::span<const std::byte> Cursor = RawBytes;
    uint32_t Magic = 0u, Version = 0u, PosCount = 0u, TriCount = 0u, ClsCount = 0u;
    if (!ReadPodBytes(Cursor, Magic) || !ReadPodBytes(Cursor, Version) ||
        Magic != ClusterBinaryMagic || (Version < 1u || Version > ClusterBinaryVersion) ||
        !ReadPodBytes(Cursor, HierarchyDepth) || !ReadPodBytes(Cursor, FinestClusterCount) ||
        !ReadPodBytes(Cursor, PosCount) || !ReadPodBytes(Cursor, TriCount) || !ReadPodBytes(Cursor, ClsCount))
    {
        return false;
    }
    if (Version >= 2u && !ReadPodBytes(Cursor, CoarsestAllowedLevel))
    {
        Reset();
        return false;
    }

    const std::size_t BasePayloadBytes =
        static_cast<std::size_t>(PosCount) * sizeof(Mathematics::Axis3D) +
        static_cast<std::size_t>(TriCount) * sizeof(ClusterTriangleEntry) +
        static_cast<std::size_t>(ClsCount) * sizeof(ClusterHeaderEntry);
    if (Cursor.size() < BasePayloadBytes)
    {
        Reset();
        return false;
    }

    Positions.resize(PosCount);
    Triangles.resize(TriCount);
    Clusters.resize(ClsCount);
    for (uint32_t Idx = 0u; Idx < PosCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Positions[Idx]);
    }
    for (uint32_t Idx = 0u; Idx < TriCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Triangles[Idx]);
    }
    for (uint32_t Idx = 0u; Idx < ClsCount; ++Idx)
    {
        (void)ReadPodBytes(Cursor, Clusters[Idx]);
    }

    if (Version >= 3u)
    {
        uint32_t GrpCount = 0u, FinerCount = 0u, RootCount = 0u;
        if (!ReadPodBytes(Cursor, GrpCount) ||
            !ReadPodBytes(Cursor, FinerCount) ||
            !ReadPodBytes(Cursor, RootCount))
        {
            Reset();
            return false;
        }
        const std::size_t DagBytes =
            static_cast<std::size_t>(GrpCount) * sizeof(ClusterGroupEntry) +
            static_cast<std::size_t>(FinerCount + RootCount) * sizeof(uint32_t);
        if (Cursor.size() < DagBytes)
        {
            Reset();
            return false;
        }
        Groups.resize(GrpCount);
        GroupFinerClusterIndices.resize(FinerCount);
        RootClusterIndices.resize(RootCount);
        for (uint32_t Idx = 0u; Idx < GrpCount; ++Idx)
        {
            (void)ReadPodBytes(Cursor, Groups[Idx]);
        }
        for (uint32_t Idx = 0u; Idx < FinerCount; ++Idx)
        {
            (void)ReadPodBytes(Cursor, GroupFinerClusterIndices[Idx]);
        }
        for (uint32_t Idx = 0u; Idx < RootCount; ++Idx)
        {
            (void)ReadPodBytes(Cursor, RootClusterIndices[Idx]);
        }
    }

    // v4 per-material coarsest-allowed LOD cap trailer
    if (Version < 4u)
    {
        Reset();
        return false;
    }
    uint32_t MatCapCount = 0u;
    if (!ReadPodBytes(Cursor, MatCapCount) ||
        Cursor.size() < static_cast<std::size_t>(MatCapCount) * sizeof(uint32_t))
    {
        Reset();
        return false;
    }
    const uint32_t CopyCount = std::min<uint32_t>(
        MatCapCount,
        static_cast<uint32_t>(MaterialCoarsestAllowedLevels.size()));
    for (uint32_t Idx = 0u; Idx < MatCapCount; ++Idx)
    {
        uint32_t CapVal = 0xFFFFFFFFu;
        (void)ReadPodBytes(Cursor, CapVal);
        if (Idx < CopyCount)
        {
            MaterialCoarsestAllowedLevels[Idx] = CapVal;
        }
    }
    return true;
}

} // namespace Frontier::SpatialAcceleration
