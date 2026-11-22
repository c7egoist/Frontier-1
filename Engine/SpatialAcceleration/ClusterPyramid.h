/*============================================================================================================================================
                                                               CLUSTERPYRAMID.H
============================================================================================================================================*/
// 🧩 128-triangle hierarchical cluster LOD DAG with shared-edge group clustering, FREE/BORDER/LOCKED QEM collapse, and top-down DAG cut

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxTrianglesPerCluster = 128u; // [count] - Hardware/compute visibility dispatch granularity

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ClusterTriangleEntry
{
    uint32_t            VertexIndex0      = 0u;   // [idx] - First PolygonNetwork or simplified vertex index
    uint32_t            VertexIndex1      = 0u;   // [idx] - Second PolygonNetwork or simplified vertex index
    uint32_t            VertexIndex2      = 0u;   // [idx] - Third PolygonNetwork or simplified vertex index
    uint32_t            SourceFaceIndex   = 0u;   // [idx] - Originating PolygonNetwork face index
    Mathematics::UVAxes CornerUV0         {};     // [uv]  - Parameterized UV at corner 0
    Mathematics::UVAxes CornerUV1         {};     // [uv]  - Parameterized UV at corner 1
    Mathematics::UVAxes CornerUV2         {};     // [uv]  - Parameterized UV at corner 2
    uint32_t            PackedNormal0     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 0
    uint32_t            PackedNormal1     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 1
    uint32_t            PackedNormal2     = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner normal 2
    uint32_t            PackedTangent0    = 0u;   // [oct] - 32-bit SNORM16x2 octahedral corner tangent 0
    uint16_t            MaterialOrdinal   = 0u;   // [idx] - PartGroupOrdinal / MaterialSlot assignment
    int16_t             TangentSignScaled = 1;    // [-]   - +1 or -1 bitangent handedness sign
};

struct alignas(64) ClusterHeaderEntry
{
    Mathematics::Axis3D SphereCenter                {};    // [m]     - Tight bounding sphere center (12 B)
    float               SphereRadiusMeters          = 0.0f; // [m]    - Tight bounding sphere radius (4 B)
    Mathematics::Axis3D BoundsMinimum               {};    // [m]     - World/local AABB lower corner (12 B)
    uint32_t            TriangleStartOffset         = 0u;  // [idx]   - Offset into PyramidTriangles (4 B)
    Mathematics::Axis3D BoundsMaximum               {};    // [m]     - World/local AABB upper corner (12 B)
    uint16_t            TriangleCount               = 0u;  // [count] - Triangles in this cluster (<= 128) (2 B)
    uint8_t             HierarchyLevel              = 0u;  // [level] - 0 = finest source triangles; >0 = simplified LOD (1 B)
    uint8_t             DominantMaterialOrdinal     = 0u;  // [idx]   - Primary material slot inside cluster (1 B)
    Mathematics::Axis3D NormalConeDirection         { 0.0f, 1.0f, 0.0f }; // [-] - Mean unit cluster normal axis (12 B)
    float               NormalConeSinCutoff         = 2.0f; // [-]    - Backface cull sin(theta) threshold; >=1.5 disables (4 B)
    float               FinerSelfErrorMeters        = 0.0f; // [m]    - Geometric error of producing group (0 at L0)
    float               CoarserThresholdErrorMeters = 1.0e30f; // [m] - Monotonic error of consuming coarser group
    uint32_t            OriginGroupOrdinal          = 0u;   // [idx]  - 1-based group that produced this cluster (0 at L0)
    uint32_t            ReservedFieldAlpha          = 0u;   // [-]    - Explicit pad to 128 B alignment multiple
    uint64_t            ReservedAlignmentPaddingB   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingC   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingD   = 0ull; // [-]    - Explicit pad
    uint64_t            ReservedAlignmentPaddingE   = 0ull; // [-]    - Explicit pad
    uint32_t            EnclosingGroupOrdinal       = 0u;   // [idx]  - 1-based coarser merge group that consumed this cluster (0 = root)
    uint32_t            ClusterOrdinal              = 0u;   // [idx]  - Self index in Clusters array
};

static_assert(sizeof(ClusterHeaderEntry) == 128u, "ClusterHeaderEntry must occupy exactly 128 aligned bytes.");

struct ClusterGroupEntry
{
    Mathematics::Axis3D SphereCenter       {};    // [m]   - Monotone bounding sphere enclosing all consumed LOD spheres
    float               SphereRadiusMeters = 0.0f; // [m]  - Monotone bounding sphere radius (* 1.0001f float32 slack)
    float               MonotonicErrorMeters = 0.0f; // [m] - Shared monotone group error
    uint32_t            FinerClusterStart  = 0u;  // [idx] - Start offset into GroupFinerClusterIndices
    uint16_t            FinerClusterCount  = 0u;  // [cnt] - Number of finer clusters consumed by this group
    uint8_t             HierarchyLevel     = 0u;  // [lvl] - Coarser level produced by this group
    uint8_t             MaterialOrdinal    = 0u;  // [idx] - Material slot shared by all members of this group
};

static_assert(sizeof(ClusterGroupEntry) == 28u, "ClusterGroupEntry must occupy 28 compact bytes.");

struct ClusterFrustumPlane
{
    Mathematics::Axis3D UnitNormal  { 0.0f, 0.0f, 1.0f }; // [-] - Inward-pointing frustum plane normal (ax+by+cz+d >= -r is inside)
    float               PlaneOffset = 0.0f;               // [m] - Signed plane offset d in instance local space
};

struct ClusterCutSelection
{
    std::vector<uint32_t> ActiveClusterIndices {}; // [idx]   - Crack-free active cluster ordinals for current camera
    uint32_t              ActiveTriangleCount  = 0u; // [cnt] - Sum of triangles across selected clusters
    uint32_t              CulledBackfaceCount  = 0u; // [cnt] - Clusters rejected by normal-cone backface test
    uint32_t              CulledFrustumCount   = 0u; // [cnt] - Clusters / DAG branches rejected by frustum planes
    uint32_t              TestedGroupCount     = 0u; // [cnt] - DAG group / root cluster tests executed
};

// Per-asset LOD bake recipe (GeometryCompiler parity): how deep to bake, group arity, target ratio, error ceiling, and level cap.
struct ClusterLodBakeConfiguration
{
    uint32_t MaximumLevels          = 24u;         // [lvl] - Bake depth incl. level 0 (clamped 1..24)
    uint32_t TargetGroupClusters    = 4u;          // [cnt] - Target neighbouring clusters merged per group (2..16)
    float    SimplifyKeepRatio      = 0.50f;       // [-]   - Target fraction of triangles kept per group (0.1..0.9)
    float    RelativeErrorLimit     = 1.0f;        // [-]   - Max collapse error in multiples of sqrt(groupArea / targetTris)
    uint32_t CoarsestAllowedLevel   = 0xFFFFFFFFu; // [lvl] - Last selectable LOD level (0 = finest only)
    bool     LockMaterialBoundaries = true;        // [-]   - Never collapse vertices shared by faces of different material slots
};

struct ClusterLodLevelSummary
{
    uint32_t              Level               = 0u; // [lvl] - Hierarchy level (0 = finest)
    uint32_t              ClusterCount        = 0u; // [cnt] - Clusters at this level
    uint32_t              GroupCount          = 0u; // [cnt] - Simplification groups producing this level
    uint32_t              TriangleCount       = 0u; // [cnt] - Triangles at this level
    float                 MaximumErrorMeters  = 0.0f; // [m] - Worst FinerSelfError at this level
    std::vector<uint16_t> MaterialOrdinals    {};   // [idx] - Distinct per-face material slots present at this level
};

class ClusterPyramid
{
public:
    std::vector<Mathematics::Axis3D>  Positions                {}; // [m]   - Shared vertex positions across all LOD levels
    std::vector<ClusterTriangleEntry> Triangles                {}; // [-]   - Contiguous clustered triangles across all levels
    std::vector<ClusterHeaderEntry>   Clusters                 {}; // [-]   - 128 B GPU-aligned cluster descriptors
    std::vector<ClusterGroupEntry>    Groups                   {}; // [-]   - DAG group descriptors (0-indexed; ordinals are 1-based)
    std::vector<uint32_t>             GroupFinerClusterIndices {}; // [idx] - Flattened finer cluster indices per group
    std::vector<uint32_t>             RootClusterIndices       {}; // [idx] - Top-most coarsest DAG root cluster indices
    uint32_t                          HierarchyDepth           = 0u; // [cnt] - Number of built LOD levels (1..N)
    uint32_t                          FinestClusterCount       = 0u; // [cnt] - Level-0 cluster count
    uint32_t                          CoarsestAllowedLevel     = 0xFFFFFFFFu; // [lvl] - Runtime cut cap from the bake recipe
    bool                              MaterialBoundariesLocked = true; // [-] - Bake kept per-face material slots intact
    bool                              ReductionStalled         = false; // [-] - True when locked borders halted further reduction

    void Reset() noexcept;

    // Partitions PolygonNetwork via recursive median spatial bisection and builds a shared-edge cluster DAG.
    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        uint32_t                             MaxHierarchyLevels = 8u);

    // Bake with a full per-asset recipe (bake depth, group size, simplify ratio, runtime coarsest level, material locking).
    bool BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        const ClusterLodBakeConfiguration&   BakeConfiguration);

    // Sets the coarsest LOD level SelectMonotonicCut may ever activate (per asset, e.g. glass).
    void ConfigureCoarsestAllowedLevel(uint32_t Level) noexcept { CoarsestAllowedLevel = Level; }

    // Sets a per-material-slot coarsest LOD level floor (e.g. keeping thin windshield/window glass at LOD <= 1).
    void ConfigureMaterialCoarsestAllowedLevel(uint8_t MaterialOrdinal, uint32_t MaxLevel) noexcept
    {
        MaterialCoarsestAllowedLevels[MaterialOrdinal] = MaxLevel;
    }

    std::array<uint32_t, 256u> MaterialCoarsestAllowedLevels = []() constexpr
    {
        std::array<uint32_t, 256u> LevelCaps {};
        LevelCaps.fill(0xFFFFFFFFu);
        return LevelCaps;
    }();

    // Per-level bake report: clusters, groups, triangles, worst error, and the material slots still present.
    [[nodiscard]] std::vector<ClusterLodLevelSummary> SummarizeLevels() const;

    // Selects a crack-free monotonic LOD cut via top-down DAG walk from RootClusterIndices (GeometryCompiler parity).
    [[nodiscard]] ClusterCutSelection SelectMonotonicCut(
        Mathematics::Axis3D                    CameraLocalPosition,
        float                                  PixelsPerRadian,
        float                                  MaxAllowedPixelError = 1.25f,
        bool                                   EnableNormalConeBackfaceCull = false,
        std::span<const ClusterFrustumPlane>   LocalFrustumPlanes = {},
        int32_t                                ForcedHierarchyLevel = -1,
        float                                  NearClipDistanceMeters = 1.0e-3f) const;

    // Flat per-cluster reference cut evaluating identical groupTooCoarse(OriginGroup) / groupTooCoarse(EnclosingGroup) rules.
    [[nodiscard]] std::vector<uint32_t> SelectExhaustiveReferenceCut(
        Mathematics::Axis3D CameraLocalPosition,
        float               PixelsPerRadian,
        float               MaxAllowedPixelError = 1.25f,
        float               NearClipDistanceMeters = 1.0e-3f) const;

    // Projected pixel error for a 1-based group ordinal (0 returns 0.0f).
    [[nodiscard]] float EvaluateGroupProjectedPixelError(
        uint32_t            OneBasedGroupOrdinal,
        Mathematics::Axis3D CameraLocalPosition,
        float               PixelsPerRadian,
        float               NearClipDistanceMeters = 1.0e-3f) const noexcept;

    [[nodiscard]] std::vector<std::byte> SerializeToBinaryBytes() const;
    [[nodiscard]] bool                   DeserializeFromBinaryBytes(std::span<const std::byte> RawBytes);
};

} // namespace Frontier::SpatialAcceleration
