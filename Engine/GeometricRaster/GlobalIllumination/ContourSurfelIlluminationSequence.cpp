/*============================================================================================================================================
                                                   CONTOURSURFELILLUMINATIONSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Implementation of GTX GI #1: Stokes Spherical-Polygon Emissive Contour + 2-Bounce Surfel Radiosity + Anti-Banded SH9 Field

#include "Engine/GeometricRaster/GlobalIllumination/ContourSurfelIlluminationSequence.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float ComputeLinearLuminance(const Mathematics::LinearRGB& Color) noexcept
{
    return std::max(0.0f, 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B);
}

[[nodiscard]] float SmoothCubicStep(float UnitT) noexcept
{
    const float Clamped = std::clamp(UnitT, 0.0f, 1.0f);
    return Clamped * Clamped * (3.0f - 2.0f * Clamped);
}

[[nodiscard]] float DecodeFp16Scalar(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfBits & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfBits >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfBits & 0x03FFu;
    uint32_t Bits32     = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u) Bits32 = Sign;
        else
        {
            const float Sub = static_cast<float>(Mant) * (1.0f / 16777216.0f);
            return (Sign != 0u) ? -Sub : Sub;
        }
    }
    else if (Exp == 31u) Bits32 = Sign | 0x7F800000u | (Mant << 13u);
    else                 Bits32 = Sign | ((Exp + 112u) << 23u) | (Mant << 13u);
    float Decoded = 0.0f;
    std::memcpy(&Decoded, &Bits32, sizeof(float));
    return Decoded;
}

[[nodiscard]] Mathematics::LinearRGB UnpackSlabDiffuseReflectance(const MaterialSpace::BounceSlabEntry& Slab) noexcept
{
    return {
        std::clamp(DecodeFp16Scalar(static_cast<uint16_t>(Slab.DiffuseAlbedoRgHalf & 0xFFFFu)), 0.02f, 0.96f),
        std::clamp(DecodeFp16Scalar(static_cast<uint16_t>((Slab.DiffuseAlbedoRgHalf >> 16u) & 0xFFFFu)), 0.02f, 0.96f),
        std::clamp(DecodeFp16Scalar(static_cast<uint16_t>(Slab.DiffuseAlbedoBAndRoughnessHalf & 0xFFFFu)), 0.02f, 0.96f),
    };
}

// Real Order-2 Spherical Harmonics basis Y_00 .. Y_22
void EvaluateSh9Basis(const Mathematics::Axis3D& UnitDir, std::array<float, 9u>& OutBasis) noexcept
{
    OutBasis[0u] = 0.28209479f;
    OutBasis[1u] = 0.48860251f * UnitDir.Y;
    OutBasis[2u] = 0.48860251f * UnitDir.Z;
    OutBasis[3u] = 0.48860251f * UnitDir.X;
    OutBasis[4u] = 1.09254843f * UnitDir.X * UnitDir.Y;
    OutBasis[5u] = 1.09254843f * UnitDir.Y * UnitDir.Z;
    OutBasis[6u] = 0.31539157f * (3.0f * UnitDir.Z * UnitDir.Z - 1.0f);
    OutBasis[7u] = 1.09254843f * UnitDir.X * UnitDir.Z;
    OutBasis[8u] = 0.54627422f * (UnitDir.X * UnitDir.X - UnitDir.Y * UnitDir.Y);
}

// Ramamoorthi & Hanrahan clamped-cosine convolution / pi (returns E(N)/pi directly!)
[[nodiscard]] Mathematics::LinearRGB ReconstructSh9IrradianceOverPi(
    const SphericalHarmonics9Cell& Cell,
    const Mathematics::Axis3D&     UnitNormal) noexcept
{
    std::array<float, 9u> Basis {};
    EvaluateSh9Basis(UnitNormal, Basis);

    // Clamped-cosine zonal harmonics A_l / pi: A0/pi = 1.0, A1/pi = 2/3, A2/pi = 1/4
    constexpr std::array<float, 9u> ZonalOverPi {{
        1.0f,
        0.66666667f, 0.66666667f, 0.66666667f,
        0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
    }};

    Mathematics::LinearRGB Sum {};
    for (uint32_t Band = 0u; Band < 9u; ++Band)
    {
        const float Weight = Basis[Band] * ZonalOverPi[Band];
        Sum.R += Cell.Coeffs[Band].R * Weight;
        Sum.G += Cell.Coeffs[Band].G * Weight;
        Sum.B += Cell.Coeffs[Band].B * Weight;
    }
    return {
        std::max(0.0f, Sum.R),
        std::max(0.0f, Sum.G),
        std::max(0.0f, Sum.B),
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                  STOKES SPHERICAL-POLYGON CONTOUR FORM FACTOR
//------------------------------------------------------------------------------------------------------------------------

float ContourSurfelIlluminationSequence::EvaluateClippedTriangleFormFactor(
    const Mathematics::Axis3D& ReceiverPoint,
    const Mathematics::Axis3D& ReceiverNormal,
    const Mathematics::Axis3D& TriVertexA,
    const Mathematics::Axis3D& TriVertexB,
    const Mathematics::Axis3D& TriVertexC) noexcept
{
    std::array<Mathematics::Axis3D, 4u> ClippedPoly {};
    uint32_t ClippedCount = 0u;

    const std::array<Mathematics::Axis3D, 3u> InputVerts {{ TriVertexA, TriVertexB, TriVertexC }};
    std::array<float, 3u> HorizonDist {};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        HorizonDist[Idx] = Mathematics::EvaluateDot(InputVerts[Idx] - ReceiverPoint, ReceiverNormal);
    }
    if (HorizonDist[0u] <= 0.0f && HorizonDist[1u] <= 0.0f && HorizonDist[2u] <= 0.0f)
    {
        return 0.0f;
    }

    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const uint32_t NextIdx = (Idx + 1u) % 3u;
        const float DistA = HorizonDist[Idx];
        const float DistB = HorizonDist[NextIdx];
        const bool  AboveA = (DistA >= 0.0f);
        const bool  AboveB = (DistB >= 0.0f);
        if (AboveA)
        {
            ClippedPoly[ClippedCount++] = InputVerts[Idx];
            if (!AboveB && ClippedCount < 4u)
            {
                const float BlendT = DistA / std::max(1.0e-7f, DistA - DistB);
                ClippedPoly[ClippedCount++] = InputVerts[Idx] + (InputVerts[NextIdx] - InputVerts[Idx]) * BlendT;
            }
        }
        else if (AboveB && ClippedCount < 4u)
        {
            const float BlendT = DistA / std::max(1.0e-7f, DistA - DistB);
            ClippedPoly[ClippedCount++] = InputVerts[Idx] + (InputVerts[NextIdx] - InputVerts[Idx]) * BlendT;
        }
    }

    if (ClippedCount < 3u)
    {
        return 0.0f;
    }

    std::array<Mathematics::Axis3D, 4u> UnitSphere {};
    for (uint32_t Idx = 0u; Idx < ClippedCount; ++Idx)
    {
        UnitSphere[Idx] = Mathematics::NormalizeAxis(ClippedPoly[Idx] - ReceiverPoint, ReceiverNormal);
    }

    float ContourSum = 0.0f;
    for (uint32_t Idx = 0u; Idx < ClippedCount; ++Idx)
    {
        const uint32_t NextIdx = (Idx + 1u) % ClippedCount;
        const float CosArc = std::clamp(Mathematics::EvaluateDot(UnitSphere[Idx], UnitSphere[NextIdx]), -0.999999f, 0.999999f);
        const Mathematics::Axis3D EdgeCross = Mathematics::EvaluateCross(UnitSphere[Idx], UnitSphere[NextIdx]);
        const float CrossLen = Mathematics::EvaluateLength(EdgeCross);
        if (CrossLen > 1.0e-7f)
        {
            const float ArcAngle = std::acos(CosArc);
            ContourSum += ArcAngle * (Mathematics::EvaluateDot(EdgeCross, ReceiverNormal) / CrossLen);
        }
    }

    return std::clamp(std::fabs(ContourSum) * (0.5f / Constants::PiFloat), 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  SCENE PREPARATION (CLUSTERS + 2-BOUNCE SURFELS + SH9)
//------------------------------------------------------------------------------------------------------------------------

void ContourSurfelIlluminationSequence::PrepareSceneIllumination(
    const ContourSurfelConfiguration&               Config,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
    const SolarDirectionalSource&                   Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    std::span<const OmnidirectionalPointLight>      PointLights,
    Mathematics::LinearRGB                          SkyRadiance,
    const SurfelOccluderField*                      OptionalOccluders,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig)
{
    ActiveConfig = Config;
    EmissiveTriangles.clear();
    EmissiveClusters.clear();
    BounceSurfels.clear();
    Sh9Grid.clear();

    const uint32_t MatSlotCount = MaterialRegister.QuerySlotCount();
    double TotalEmissiveFlux    = 0.0;

    Mathematics::Axis3D SceneMin {  1.0e6f,  1.0e6f,  1.0e6f };
    Mathematics::Axis3D SceneMax { -1.0e6f, -1.0e6f, -1.0e6f };

    // 1. Extract emissive triangles from authored OpenPBR materials + PolygonalAreaQuadLights
    for (const auto& Tri : SceneTree.Primitives)
    {
        const auto V0 = Tri.Corner0;
        const auto V1 = Tri.Corner0 + Tri.Edge01;
        const auto V2 = Tri.Corner0 + Tri.Edge02;
        const auto Centroid = (V0 + V1 + V2) * (1.0f / 3.0f);
        SceneMin.X = std::min(SceneMin.X, Centroid.X);
        SceneMin.Y = std::min(SceneMin.Y, Centroid.Y);
        SceneMin.Z = std::min(SceneMin.Z, Centroid.Z);
        SceneMax.X = std::max(SceneMax.X, Centroid.X);
        SceneMax.Y = std::max(SceneMax.Y, Centroid.Y);
        SceneMax.Z = std::max(SceneMax.Z, Centroid.Z);

        if (Tri.MaterialOrdinal >= MatSlotCount)
        {
            continue;
        }
        const auto& Mat = MaterialRegister.InspectAuthoredMaterial(Tri.MaterialOrdinal);
        const auto  Em  = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        const float Lum = ComputeLinearLuminance(Em);
        if (Lum <= 1.0e-4f)
        {
            continue;
        }

        const auto  CrossVec = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float TwiceA   = Mathematics::EvaluateLength(CrossVec);
        if (TwiceA <= 1.0e-7f)
        {
            continue;
        }

        EmissiveContourTriangle Entry {};
        Entry.VertexA      = V0;
        Entry.VertexB      = V1;
        Entry.VertexC      = V2;
        Entry.AreaM2       = 0.5f * TwiceA;
        Entry.UnitNormal   = CrossVec * (1.0f / TwiceA);
        Entry.EmittedRad   = Em;
        Entry.TwoSidedEmit = 0u;
        TotalEmissiveFlux += static_cast<double>(Lum) * static_cast<double>(Entry.AreaM2);
        EmissiveTriangles.push_back(Entry);
    }

    for (const auto& Quad : AreaLights)
    {
        const float Lum = ComputeLinearLuminance(Quad.EmittedRadiance);
        if (Lum <= 1.0e-4f)
        {
            continue;
        }
        const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Quad.RightTangent, { 1.0f, 0.0f, 0.0f }) * Quad.HalfWidthMeters;
        const Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Quad.UpBitangent,  { 0.0f, 1.0f, 0.0f }) * Quad.HalfHeightMeters;
        const Mathematics::Axis3D Nrm   = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(Quad.RightTangent, Quad.UpBitangent), { 0.0f, 0.0f, -1.0f });

        const Mathematics::Axis3D Q0 = Quad.CenterWorld - Right - Up;
        const Mathematics::Axis3D Q1 = Quad.CenterWorld + Right - Up;
        const Mathematics::Axis3D Q2 = Quad.CenterWorld + Right + Up;
        const Mathematics::Axis3D Q3 = Quad.CenterWorld - Right + Up;
        const float TriArea = 2.0f * Quad.HalfWidthMeters * Quad.HalfHeightMeters;

        for (const auto& Verts : { std::array<Mathematics::Axis3D, 3u>{ Q0, Q1, Q2 },
                                   std::array<Mathematics::Axis3D, 3u>{ Q0, Q2, Q3 } })
        {
            EmissiveContourTriangle Entry {};
            Entry.VertexA      = Verts[0u];
            Entry.VertexB      = Verts[1u];
            Entry.VertexC      = Verts[2u];
            Entry.AreaM2       = TriArea;
            Entry.UnitNormal   = Nrm;
            Entry.EmittedRad   = Quad.EmittedRadiance;
            Entry.TwoSidedEmit = Quad.TwoSidedEmission ? 1u : 0u;
            TotalEmissiveFlux += static_cast<double>(Lum) * static_cast<double>(TriArea);
            EmissiveTriangles.push_back(Entry);
        }
    }

    // Group emissive triangles into up to 8 spatial-normal clusters for fast soft-penumbra visibility gating
    if (!EmissiveTriangles.empty())
    {
        std::array<std::vector<uint32_t>, 8u> Buckets {};
        float MinY = 1.0e9f, MaxY = -1.0e9f;
        for (const auto& E : EmissiveTriangles)
        {
            const float CY = (E.VertexA.Y + E.VertexB.Y + E.VertexC.Y) * (1.0f / 3.0f);
            MinY = std::min(MinY, CY);
            MaxY = std::max(MaxY, CY);
        }
        const float SpanY = std::max(0.05f, MaxY - MinY);

        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(EmissiveTriangles.size()); ++Idx)
        {
            const auto& E  = EmissiveTriangles[Idx];
            const float CY = (E.VertexA.Y + E.VertexB.Y + E.VertexC.Y) * (1.0f / 3.0f);
            uint32_t Bucket = 0u;
            if (E.TwoSidedEmit != 0u || E.UnitNormal.Z < -0.65f)
            {
                Bucket = (CY < 0.5f * (MinY + MaxY)) ? 6u : 7u;
            }
            else
            {
                const uint32_t Side = (E.UnitNormal.X >= 0.0f) ? 0u : 3u;
                const uint32_t Band = std::min(2u, static_cast<uint32_t>(std::clamp((CY - MinY) / SpanY, 0.0f, 1.0f) * 3.0f));
                Bucket = Side + Band;
            }
            Buckets[Bucket].push_back(Idx);
        }

        std::vector<EmissiveContourTriangle> Reordered {};
        Reordered.reserve(EmissiveTriangles.size());
        for (uint32_t BIdx = 0u; BIdx < 8u; ++BIdx)
        {
            if (Buckets[BIdx].empty()) continue;

            EmissiveClusterSummary Cluster {};
            Cluster.FirstTriIdx = static_cast<uint32_t>(Reordered.size());
            Cluster.TriCount    = static_cast<uint32_t>(Buckets[BIdx].size());

            Mathematics::Axis3D    WeightedPos {};
            Mathematics::Axis3D    WeightedNrm {};
            Mathematics::LinearRGB WeightedRad {};
            float                  TotalWeight = 0.0f;

            for (uint32_t TriIdx : Buckets[BIdx])
            {
                EmissiveContourTriangle Copy = EmissiveTriangles[TriIdx];
                Copy.ClusterIndex            = static_cast<uint32_t>(EmissiveClusters.size());
                const Mathematics::Axis3D C  = (Copy.VertexA + Copy.VertexB + Copy.VertexC) * (1.0f / 3.0f);
                const float W                = std::max(1.0e-6f, Copy.AreaM2 * ComputeLinearLuminance(Copy.EmittedRad));
                WeightedPos                  = WeightedPos + C * W;
                WeightedNrm                  = WeightedNrm + Copy.UnitNormal * W;
                WeightedRad                  = WeightedRad + Copy.EmittedRad * Copy.AreaM2;
                Cluster.TotalAreaM2         += Copy.AreaM2;
                TotalWeight                 += W;
                Reordered.push_back(Copy);
            }

            Cluster.CenterWorld  = WeightedPos * (1.0f / std::max(1.0e-7f, TotalWeight));
            Cluster.MeanNormal   = Mathematics::NormalizeAxis(WeightedNrm, { 0.0f, 0.0f, 1.0f });
            Cluster.MeanRadiance = WeightedRad * (1.0f / std::max(1.0e-7f, Cluster.TotalAreaM2));

            float MaxR2 = 0.01f;
            for (uint32_t K = Cluster.FirstTriIdx; K < Cluster.FirstTriIdx + Cluster.TriCount; ++K)
            {
                for (const auto& V : { Reordered[K].VertexA, Reordered[K].VertexB, Reordered[K].VertexC })
                {
                    MaxR2 = std::max(MaxR2, Mathematics::EvaluateLengthSquared(V - Cluster.CenterWorld));
                }
            }
            Cluster.BoundRadiusM = std::sqrt(MaxR2);
            EmissiveClusters.push_back(Cluster);
        }
        EmissiveTriangles.swap(Reordered);

        const double InvFlux = (TotalEmissiveFlux > 1.0e-12) ? (1.0 / TotalEmissiveFlux) : 1.0;
        double Cumulative = 0.0;
        for (auto& E : EmissiveTriangles)
        {
            Cumulative += static_cast<double>(ComputeLinearLuminance(E.EmittedRad)) * static_cast<double>(E.AreaM2);
            E.CdfUpper  = static_cast<float>(std::clamp(Cumulative * InvFlux, 0.0, 1.0));
        }
    }

    // 2. Build area-stratified diffuse bounce surfels directly from non-emissive opaque scene geometry
    if (SceneMax.X > SceneMin.X)
    {
        ShBoundsMin = { std::max(-12.0f, SceneMin.X + 0.08f), std::max(-12.0f, SceneMin.Y + 0.08f), std::max(-1.0f, SceneMin.Z + 0.06f) };
        ShBoundsMax = { std::min( 12.0f, SceneMax.X - 0.08f), std::min( 12.0f, SceneMax.Y - 0.08f), std::min( 8.0f, SceneMax.Z - 0.06f) };
        if (ShBoundsMax.X - ShBoundsMin.X < 1.0f) { ShBoundsMin.X -= 2.0f; ShBoundsMax.X += 2.0f; }
        if (ShBoundsMax.Y - ShBoundsMin.Y < 1.0f) { ShBoundsMin.Y -= 2.0f; ShBoundsMax.Y += 2.0f; }
        if (ShBoundsMax.Z - ShBoundsMin.Z < 0.8f) { ShBoundsMin.Z  = 0.05f; ShBoundsMax.Z  = 3.0f; }
    }

    if (!SceneTree.Primitives.empty())
    {
        constexpr int NX = 14, NY = 12, NZ = 6;
        struct PatchCellAccum
        {
            Mathematics::Axis3D    PosSum {};
            Mathematics::Axis3D    NrmSum {};
            Mathematics::LinearRGB AlbSum {};
            float                  Area   = 0.0f;
            uint32_t               Count  = 0u;
        };
        // Separate normal-hemisphere buckets (6 cardinal directions) prevent opposing walls/pillars from cancelling normals!
        std::vector<PatchCellAccum> PatchCells(static_cast<std::size_t>(NX) * NY * NZ * 6u);
        const Mathematics::Axis3D Span = ShBoundsMax - ShBoundsMin;

        for (const auto& Tri : SceneTree.Primitives)
        {
            if (Tri.OpaqueMask == 0u) continue;
            const auto  CrossVec = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float TwiceA   = Mathematics::EvaluateLength(CrossVec);
            const float Area     = 0.5f * TwiceA;
            if (Area <= 1.0e-5f) continue;

            Mathematics::LinearRGB Alb { 0.58f, 0.58f, 0.58f };
            if (Tri.MaterialOrdinal < BounceSummaries.size())
            {
                Alb = UnpackSlabDiffuseReflectance(BounceSummaries[Tri.MaterialOrdinal]);
            }
            if (Tri.MaterialOrdinal < MatSlotCount)
            {
                const auto& Mat = MaterialRegister.InspectAuthoredMaterial(Tri.MaterialOrdinal);
                if (ComputeLinearLuminance(MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight)) > 0.05f)
                {
                    continue;
                }
            }

            const Mathematics::Axis3D Nrm      = CrossVec * (1.0f / TwiceA);
            const Mathematics::Axis3D Centroid = (Tri.Corner0 * 3.0f + Tri.Edge01 + Tri.Edge02) * (1.0f / 3.0f);
            if (std::fabs(Centroid.X) > 12.5f || std::fabs(Centroid.Y) > 12.5f || Centroid.Z > 8.5f) continue;

            const int IX = std::clamp(static_cast<int>(std::floor(((Centroid.X - ShBoundsMin.X) / std::max(0.5f, Span.X)) * NX)), 0, NX - 1);
            const int IY = std::clamp(static_cast<int>(std::floor(((Centroid.Y - ShBoundsMin.Y) / std::max(0.5f, Span.Y)) * NY)), 0, NY - 1);
            const int IZ = std::clamp(static_cast<int>(std::floor(((Centroid.Z - ShBoundsMin.Z) / std::max(0.5f, Span.Z)) * NZ)), 0, NZ - 1);

            const float AX = std::fabs(Nrm.X), AY = std::fabs(Nrm.Y), AZ = std::fabs(Nrm.Z);
            uint32_t FaceAxis = 0u;
            if (AZ >= AX && AZ >= AY)      FaceAxis = (Nrm.Z >= 0.0f) ? 4u : 5u;
            else if (AX >= AY)             FaceAxis = (Nrm.X >= 0.0f) ? 0u : 1u;
            else                           FaceAxis = (Nrm.Y >= 0.0f) ? 2u : 3u;

            auto& Bucket  = PatchCells[((static_cast<std::size_t>(IZ) * NY + IY) * NX + IX) * 6u + FaceAxis];
            Bucket.PosSum = Bucket.PosSum + Centroid * Area;
            Bucket.NrmSum = Bucket.NrmSum + Nrm * Area;
            Bucket.AlbSum = Bucket.AlbSum + Alb * Area;
            Bucket.Area  += Area;
            Bucket.Count += 1u;
        }

        BounceSurfels.reserve(Config.TargetBounceSurfels);
        for (const auto& Bucket : PatchCells)
        {
            if (Bucket.Count == 0u || Bucket.Area <= 1.0e-4f) continue;
            RadiantBounceSurfel Surfel {};
            Surfel.PositionWorld = Bucket.PosSum * (1.0f / Bucket.Area);
            Surfel.UnitNormal    = Mathematics::NormalizeAxis(Bucket.NrmSum, { 0.0f, 0.0f, 1.0f });
            Surfel.DiffuseAlbedo = Bucket.AlbSum * (1.0f / Bucket.Area);
            Surfel.AreaM2        = std::min(Bucket.Area, 2.25f);
            Surfel.RadiusMeters  = std::clamp(std::sqrt(Surfel.AreaM2 / Constants::PiFloat), 0.08f, 0.85f);

            // Cache 4-ray hemisphere sky openness
            float OpenSky = 0.0f;
            if (Surfel.UnitNormal.Z > -0.35f)
            {
                for (uint32_t R = 0u; R < 4u; ++R)
                {
                    const auto Xi   = Sobol02Sample(R, 0x91E10DA5u, 2u);
                    const float Phi = Xi.Y * Constants::TauFloat;
                    const float ST  = std::sqrt(Xi.X * 0.55f);
                    const float CT  = std::sqrt(1.0f - ST * ST);
                    const Mathematics::Axis3D SkyDir = Mathematics::NormalizeAxis(
                        { ST * std::cos(Phi), ST * std::sin(Phi), CT }, { 0.0f, 0.0f, 1.0f });
                    if (Mathematics::EvaluateDot(SkyDir, Surfel.UnitNormal) <= 0.05f) continue;
                    SpatialAcceleration::RaySegmentQuery SkyQ {};
                    SkyQ.Origin          = Surfel.PositionWorld + Surfel.UnitNormal * 0.02f;
                    SkyQ.UnitDirection   = SkyDir;
                    SkyQ.MinimumDistance = 0.01f;
                    SkyQ.MaximumDistance = 40.0f;
                    if (!SceneTree.QueryRayOccluded(SkyQ)) OpenSky += 0.25f;
                }
            }
            Surfel.SkyVisibility = OpenSky;
            BounceSurfels.push_back(Surfel);
        }
    }

    // 3. First-bounce illumination onto BounceSurfels (Sun + Sky + Stokes Emissive Contours + PointLights)
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float SunIrrLum            = ComputeLinearLuminance(Sun.SolarIrradiance);

    for (auto& Surfel : BounceSurfels)
    {
        Mathematics::LinearRGB IncidentOverPi = EvaluateDirectEmissiveContoursOverPi(
            Surfel.PositionWorld, Surfel.UnitNormal, SceneTree, OptionalOccluders, OptionalCapsuleRig);

        // Solar direct onto surfel
        if (SunIrrLum > 1.0e-4f)
        {
            const float NdotSun = Mathematics::EvaluateDot(Surfel.UnitNormal, SunDir);
            if (NdotSun > 1.0e-4f)
            {
                SpatialAcceleration::RaySegmentQuery SunRay {};
                SunRay.Origin          = Surfel.PositionWorld + Surfel.UnitNormal * 0.015f;
                SunRay.UnitDirection   = SunDir;
                SunRay.MinimumDistance = 0.005f;
                SunRay.MaximumDistance = 120.0f;
                if (!SceneTree.QueryRayOccluded(SunRay))
                {
                    const float Scale = NdotSun * static_cast<float>(Constants::InversePi);
                    IncidentOverPi.R += Sun.SolarIrradiance.R * Scale;
                    IncidentOverPi.G += Sun.SolarIrradiance.G * Scale;
                    IncidentOverPi.B += Sun.SolarIrradiance.B * Scale;
                }
            }
        }

        // Sky dome irradiance/pi onto surfel
        if (Surfel.SkyVisibility > 0.0f)
        {
            IncidentOverPi.R += SkyRadiance.R * Surfel.SkyVisibility;
            IncidentOverPi.G += SkyRadiance.G * Surfel.SkyVisibility;
            IncidentOverPi.B += SkyRadiance.B * Surfel.SkyVisibility;
        }

        // Point lights onto surfel
        for (const auto& Pt : PointLights)
        {
            const Mathematics::Axis3D Delta = Pt.PositionWorld - Surfel.PositionWorld;
            const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
            const float Dist  = std::sqrt(std::max(1.0e-4f, Dist2));
            if (Dist >= Pt.RangeMeters) continue;
            const Mathematics::Axis3D LDir = Delta * (1.0f / Dist);
            const float NdotL = Mathematics::EvaluateDot(Surfel.UnitNormal, LDir);
            if (NdotL <= 1.0e-4f) continue;

            SpatialAcceleration::RaySegmentQuery PtQ {};
            PtQ.Origin          = Surfel.PositionWorld + Surfel.UnitNormal * 0.015f;
            PtQ.UnitDirection   = LDir;
            PtQ.MinimumDistance = 0.005f;
            PtQ.MaximumDistance = std::max(0.02f, Dist - Pt.RadiusMeters);
            if (!SceneTree.QueryRayOccluded(PtQ))
            {
                const float Win   = std::max(0.0f, 1.0f - (Dist * Dist) / std::max(0.25f, Pt.RangeMeters * Pt.RangeMeters));
                const float Atten = (Pt.Intensity * Win * Win * NdotL * static_cast<float>(Constants::InversePi)) /
                                    std::max(0.20f, Dist2 + Pt.RadiusMeters * Pt.RadiusMeters);
                IncidentOverPi.R += Pt.Color.R * Atten;
                IncidentOverPi.G += Pt.Color.G * Atten;
                IncidentOverPi.B += Pt.Color.B * Atten;
            }
        }

        Surfel.ExitantOverPi = {
            IncidentOverPi.R * Surfel.DiffuseAlbedo.R,
            IncidentOverPi.G * Surfel.DiffuseAlbedo.G,
            IncidentOverPi.B * Surfel.DiffuseAlbedo.B,
        };
    }

    // 4. Multi-bounce diffuse radiosity relaxation (2nd & 3rd bounce color bleed!)
    const std::vector<RadiantBounceSurfel> DirectSurfels = BounceSurfels;
    const uint32_t Bounces = std::clamp(Config.RadiosityBounces, 1u, 3u);
    for (uint32_t BounceIdx = 1u; BounceIdx < Bounces; ++BounceIdx)
    {
        std::vector<Mathematics::LinearRGB> UpdatedExitant(BounceSurfels.size());
        for (std::size_t TargetIdx = 0u; TargetIdx < BounceSurfels.size(); ++TargetIdx)
        {
            const auto& Target = BounceSurfels[TargetIdx];
            const auto  Bleed  = EvaluateNearBounceSurfelsOverPi(Target.PositionWorld, Target.UnitNormal);
            UpdatedExitant[TargetIdx] = {
                DirectSurfels[TargetIdx].ExitantOverPi.R + Bleed.R * Target.DiffuseAlbedo.R,
                DirectSurfels[TargetIdx].ExitantOverPi.G + Bleed.G * Target.DiffuseAlbedo.G,
                DirectSurfels[TargetIdx].ExitantOverPi.B + Bleed.B * Target.DiffuseAlbedo.B,
            };
        }
        for (std::size_t TargetIdx = 0u; TargetIdx < BounceSurfels.size(); ++TargetIdx)
        {
            BounceSurfels[TargetIdx].ExitantOverPi = UpdatedExitant[TargetIdx];
        }
    }

    // 5. Project far-field surfel + sky radiance into anti-banded Order-2 SH9 grid
    ActiveConfig.ShGridCountX = std::max(2u, Config.ShGridCountX);
    ActiveConfig.ShGridCountY = std::max(2u, Config.ShGridCountY);
    ActiveConfig.ShGridCountZ = std::max(2u, Config.ShGridCountZ);
    ShCellSpacing = {
        (ShBoundsMax.X - ShBoundsMin.X) / static_cast<float>(ActiveConfig.ShGridCountX - 1u),
        (ShBoundsMax.Y - ShBoundsMin.Y) / static_cast<float>(ActiveConfig.ShGridCountY - 1u),
        (ShBoundsMax.Z - ShBoundsMin.Z) / static_cast<float>(ActiveConfig.ShGridCountZ - 1u),
    };

    const std::size_t TotalShCells = static_cast<std::size_t>(ActiveConfig.ShGridCountX) *
                                     ActiveConfig.ShGridCountY * ActiveConfig.ShGridCountZ;
    Sh9Grid.assign(TotalShCells, SphericalHarmonics9Cell{});

    for (uint32_t IZ = 0u; IZ < ActiveConfig.ShGridCountZ; ++IZ)
    {
        for (uint32_t IY = 0u; IY < ActiveConfig.ShGridCountY; ++IY)
        {
            for (uint32_t IX = 0u; IX < ActiveConfig.ShGridCountX; ++IX)
            {
                const Mathematics::Axis3D CellPos {
                    ShBoundsMin.X + static_cast<float>(IX) * ShCellSpacing.X,
                    ShBoundsMin.Y + static_cast<float>(IY) * ShCellSpacing.Y,
                    ShBoundsMin.Z + static_cast<float>(IZ) * ShCellSpacing.Z,
                };
                auto& ShCell = Sh9Grid[(static_cast<std::size_t>(IZ) * ActiveConfig.ShGridCountY + IY) * ActiveConfig.ShGridCountX + IX];

                for (const auto& Surfel : BounceSurfels)
                {
                    const Mathematics::Axis3D Delta = Surfel.PositionWorld - CellPos;
                    const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
                    if (DistSq < 0.09f) continue;
                    const float Dist = std::sqrt(DistSq);
                    const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
                    const float CosEmit = Mathematics::EvaluateDot(Surfel.UnitNormal, Dir * -1.0f);
                    if (CosEmit <= 1.0e-3f) continue;

                    const float SolidAngle = (Surfel.AreaM2 * CosEmit) / (DistSq + Surfel.AreaM2 * static_cast<float>(Constants::InversePi));
                    std::array<float, 9u> Basis {};
                    EvaluateSh9Basis(Dir, Basis);
                    for (uint32_t Band = 0u; Band < 9u; ++Band)
                    {
                        const float Scale = SolidAngle * Basis[Band] * 0.35f;
                        ShCell.Coeffs[Band].R += Surfel.ExitantOverPi.R * Scale;
                        ShCell.Coeffs[Band].G += Surfel.ExitantOverPi.G * Scale;
                        ShCell.Coeffs[Band].B += Surfel.ExitantOverPi.B * Scale;
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PER-PIXEL IRRADIANCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

Mathematics::LinearRGB ContourSurfelIlluminationSequence::EvaluateDirectEmissiveContoursOverPi(
    const Mathematics::Axis3D&                      ReceiverPoint,
    const Mathematics::Axis3D&                      ReceiverNormal,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SurfelOccluderField*                      OptionalOccluders,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig) const noexcept
{
    if (EmissiveTriangles.empty() || EmissiveClusters.empty())
    {
        return {};
    }

    Mathematics::LinearRGB TotalOverPi {};
    for (const auto& Cluster : EmissiveClusters)
    {
        const Mathematics::Axis3D ToCluster = Cluster.CenterWorld - ReceiverPoint;
        const float ClusterDist = Mathematics::EvaluateLength(ToCluster);
        if (ClusterDist <= 1.0e-4f) continue;

        const Mathematics::Axis3D UnitToCluster = ToCluster * (1.0f / ClusterDist);
        float Visibility = 1.0f;
        const float TanCluster = std::clamp(Cluster.BoundRadiusM / std::max(0.25f, ClusterDist), 0.02f, 0.22f);

        if (OptionalOccluders != nullptr && !OptionalOccluders->Disks.empty())
        {
            Visibility = OptionalOccluders->EvaluateSoftDirectionalVisibility(
                ReceiverPoint, ReceiverNormal, UnitToCluster, TanCluster, &SceneTree, OptionalCapsuleRig, nullptr);
        }
        else
        {
            SpatialAcceleration::RaySegmentQuery VisRay {};
            VisRay.Origin          = ReceiverPoint + ReceiverNormal * 2.5e-3f;
            VisRay.UnitDirection   = UnitToCluster;
            VisRay.MinimumDistance = 2.0e-3f;
            VisRay.MaximumDistance = std::max(0.02f, ClusterDist - Cluster.BoundRadiusM * 0.65f);
            Visibility = SceneTree.QueryRayOccluded(VisRay) ? 0.0f : 1.0f;
            if (Visibility > 0.0f && OptionalCapsuleRig != nullptr && !OptionalCapsuleRig->Capsules.empty())
            {
                Visibility *= OptionalCapsuleRig->EvaluateSoftConeVisibility(
                    ReceiverPoint, ReceiverNormal, UnitToCluster, TanCluster, VisRay.MaximumDistance);
            }
        }

        if (Visibility <= 1.0e-4f)
        {
            continue;
        }

        for (uint32_t K = Cluster.FirstTriIdx; K < Cluster.FirstTriIdx + Cluster.TriCount; ++K)
        {
            const auto& Tri = EmissiveTriangles[K];
            const Mathematics::Axis3D Centroid = (Tri.VertexA + Tri.VertexB + Tri.VertexC) * (1.0f / 3.0f);
            if (Tri.TwoSidedEmit == 0u &&
                Mathematics::EvaluateDot(Tri.UnitNormal, ReceiverPoint - Centroid) <= 1.0e-4f)
            {
                continue;
            }

            const float FormFactor = EvaluateClippedTriangleFormFactor(
                ReceiverPoint, ReceiverNormal, Tri.VertexA, Tri.VertexB, Tri.VertexC);
            if (FormFactor > 1.0e-6f)
            {
                const float Weight = FormFactor * Visibility;
                TotalOverPi.R += Tri.EmittedRad.R * Weight;
                TotalOverPi.G += Tri.EmittedRad.G * Weight;
                TotalOverPi.B += Tri.EmittedRad.B * Weight;
            }
        }
    }

    return TotalOverPi;
}

Mathematics::LinearRGB ContourSurfelIlluminationSequence::EvaluateNearBounceSurfelsOverPi(
    const Mathematics::Axis3D& ReceiverPoint,
    const Mathematics::Axis3D& ReceiverNormal) const noexcept
{
    Mathematics::LinearRGB Bounce {};
    for (const auto& Surfel : BounceSurfels)
    {
        const Mathematics::Axis3D Delta = Surfel.PositionWorld - ReceiverPoint;
        const float DistSq = Mathematics::EvaluateLengthSquared(Delta);
        if (DistSq < 0.04f) continue;

        const float Dist = std::sqrt(DistSq);
        const Mathematics::Axis3D Dir = Delta * (1.0f / Dist);
        const float CosRecv = Mathematics::EvaluateDot(ReceiverNormal, Dir);
        const float CosEmit = Mathematics::EvaluateDot(Surfel.UnitNormal, Dir * -1.0f);
        if (CosRecv <= 1.0e-3f || CosEmit <= 1.0e-3f) continue;

        // Singularity-free Bunnell/Wallace finite-disk form factor
        const float DiskForm = (Surfel.AreaM2 * CosRecv * CosEmit) / (Constants::PiFloat * DistSq + Surfel.AreaM2);
        Bounce.R += Surfel.ExitantOverPi.R * DiskForm;
        Bounce.G += Surfel.ExitantOverPi.G * DiskForm;
        Bounce.B += Surfel.ExitantOverPi.B * DiskForm;
    }
    return Bounce;
}

Mathematics::LinearRGB ContourSurfelIlluminationSequence::SampleSh9IrradianceOverPi(
    const Mathematics::Axis3D& WorldPoint,
    const Mathematics::Axis3D& UnitNormal) const noexcept
{
    if (Sh9Grid.empty() || ActiveConfig.ShGridCountX < 2u)
    {
        return {};
    }

    // Normal-offset Hermite anti-banding (eliminates cell-plane derivative kinks!)
    const float NormalBias = 0.14f * std::min({ ShCellSpacing.X, ShCellSpacing.Y, ShCellSpacing.Z });
    const Mathematics::Axis3D Biased = WorldPoint + UnitNormal * NormalBias;

    const float FX = std::clamp((Biased.X - ShBoundsMin.X) / std::max(1.0e-4f, ShCellSpacing.X),
                                0.0f, static_cast<float>(ActiveConfig.ShGridCountX - 1u));
    const float FY = std::clamp((Biased.Y - ShBoundsMin.Y) / std::max(1.0e-4f, ShCellSpacing.Y),
                                0.0f, static_cast<float>(ActiveConfig.ShGridCountY - 1u));
    const float FZ = std::clamp((Biased.Z - ShBoundsMin.Z) / std::max(1.0e-4f, ShCellSpacing.Z),
                                0.0f, static_cast<float>(ActiveConfig.ShGridCountZ - 1u));

    const uint32_t IX0 = std::min(ActiveConfig.ShGridCountX - 2u, static_cast<uint32_t>(std::floor(FX)));
    const uint32_t IY0 = std::min(ActiveConfig.ShGridCountY - 2u, static_cast<uint32_t>(std::floor(FY)));
    const uint32_t IZ0 = std::min(ActiveConfig.ShGridCountZ - 2u, static_cast<uint32_t>(std::floor(FZ)));
    const float    TX  = SmoothCubicStep(FX - static_cast<float>(IX0));
    const float    TY  = SmoothCubicStep(FY - static_cast<float>(IY0));
    const float    TZ  = SmoothCubicStep(FZ - static_cast<float>(IZ0));

    Mathematics::LinearRGB Blended {};
    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const uint32_t CX = IX0 + ((Corner & 1u) != 0u ? 1u : 0u);
        const uint32_t CY = IY0 + ((Corner & 2u) != 0u ? 1u : 0u);
        const uint32_t CZ = IZ0 + ((Corner & 4u) != 0u ? 1u : 0u);
        const float Weight = ((Corner & 1u) != 0u ? TX : (1.0f - TX)) *
                             ((Corner & 2u) != 0u ? TY : (1.0f - TY)) *
                             ((Corner & 4u) != 0u ? TZ : (1.0f - TZ));

        const std::size_t CellIdx = (static_cast<std::size_t>(CZ) * ActiveConfig.ShGridCountY + CY) * ActiveConfig.ShGridCountX + CX;
        const auto CellIrr = ReconstructSh9IrradianceOverPi(Sh9Grid[CellIdx], UnitNormal);
        Blended = Blended + CellIrr * Weight;
    }
    return Blended;
}

Mathematics::LinearRGB ContourSurfelIlluminationSequence::EvaluateIrradianceOverPi(
    const Mathematics::Axis3D&                      ReceiverPoint,
    const Mathematics::Axis3D&                      ReceiverNormal,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SurfelOccluderField*                      OptionalOccluders,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig) const noexcept
{
    const Mathematics::LinearRGB NearSurfelBounce = EvaluateNearBounceSurfelsOverPi(ReceiverPoint, ReceiverNormal);
    const Mathematics::LinearRGB FarSh9Bounce     = SampleSh9IrradianceOverPi(ReceiverPoint, ReceiverNormal);
    Mathematics::LinearRGB TotalOverPi            = NearSurfelBounce * 0.78f + FarSh9Bounce * 0.22f;

    if (ActiveConfig.IncludeDirectContoursInGi)
    {
        TotalOverPi = TotalOverPi + EvaluateDirectEmissiveContoursOverPi(
            ReceiverPoint, ReceiverNormal, SceneTree, OptionalOccluders, OptionalCapsuleRig);
    }
    return TotalOverPi;
}

std::vector<Mathematics::LinearRGB> ContourSurfelIlluminationSequence::EvaluateViewportIndirectOverPi(
    uint32_t                                        ViewportWidth,
    uint32_t                                        ViewportHeight,
    std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SurfelOccluderField*                      OptionalOccluders,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig) const
{
    const std::size_t TotalPixels = static_cast<std::size_t>(ViewportWidth) * ViewportHeight;
    std::vector<Mathematics::LinearRGB> Output(TotalPixels, { 0.0f, 0.0f, 0.0f });
    if (TotalPixels == 0u || PrimarySamples.size() < TotalPixels)
    {
        return Output;
    }

    const uint32_t WorkerCount = std::min(ViewportHeight, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> Workers {};
    Workers.reserve(WorkerCount);
    const uint32_t RowsPerWorker = (ViewportHeight + WorkerCount - 1u) / WorkerCount;

    for (uint32_t WIdx = 0u; WIdx < WorkerCount; ++WIdx)
    {
        const uint32_t RowBegin = WIdx * RowsPerWorker;
        const uint32_t RowEnd   = std::min(ViewportHeight, RowBegin + RowsPerWorker);
        if (RowBegin >= RowEnd) continue;

        Workers.emplace_back([&, RowBegin, RowEnd]()
        {
            for (uint32_t PY = RowBegin; PY < RowEnd; ++PY)
            {
                for (uint32_t PX = 0u; PX < ViewportWidth; ++PX)
                {
                    const std::size_t Off = static_cast<std::size_t>(PY) * ViewportWidth + PX;
                    const auto& Hit       = PrimarySamples[Off];
                    if (!Hit.ValidHit) continue;
                    Output[Off] = EvaluateIrradianceOverPi(
                        Hit.WorldPosition, Hit.TangentFrame.NormalZ, SceneTree, OptionalOccluders, OptionalCapsuleRig);
                }
            }
        });
    }
    for (auto& Worker : Workers) Worker.join();
    return Output;
}

} // namespace Frontier::GeometricRaster
