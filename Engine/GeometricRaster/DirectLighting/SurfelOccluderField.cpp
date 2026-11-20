/*============================================================================================================================================
                                                         SURFELOCCLUDERFIELD.CPP
============================================================================================================================================*/
// 🧩 Implementation of area-stratified blocker surfel disks + DeformingCapsuleRig C1 quartic-kernel soft shadow transmittance

#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float SmoothHermiteStep(float Edge0, float Edge1, float InputX) noexcept
{
    const float Normalized = std::clamp((InputX - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return Normalized * Normalized * (3.0f - 2.0f * Normalized);
}

} // namespace

void SurfelOccluderField::BuildFromSceneTree(
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig,
    float                                           CellSizeMeters)
{
    Disks.clear();
    if (SceneTree.Primitives.empty())
    {
        return;
    }

    struct EligibleTriangleSample
    {
        Mathematics::Axis3D Centroid;
        Mathematics::Axis3D Normal;
        float               AreaM2;
    };

    std::vector<EligibleTriangleSample> Eligible {};
    Eligible.reserve(SceneTree.Primitives.size() / 2u);

    const bool ExcludeRig = (OptionalCapsuleRig != nullptr && !OptionalCapsuleRig->Capsules.empty());
    Mathematics::Axis3D BoundsMin {  1.0e6f,  1.0e6f,  1.0e6f };
    Mathematics::Axis3D BoundsMax { -1.0e6f, -1.0e6f, -1.0e6f };

    for (const auto& Tri : SceneTree.Primitives)
    {
        if (Tri.OpaqueMask == 0u)
        {
            continue;
        }
        const auto V0 = Tri.Corner0;
        const auto V1 = Tri.Corner0 + Tri.Edge01;
        const auto V2 = Tri.Corner0 + Tri.Edge02;
        const auto Centroid = (V0 + V1 + V2) * (1.0f / 3.0f);

        // Exclude broad studio floor & outer perimeter walls (> 4.6 m) so only true interior blockers populate the surfel field
        if (std::fabs(Centroid.X) > 4.6f || std::fabs(Centroid.Y) > 4.6f || Centroid.Z < 0.03f || Centroid.Z > 3.6f)
        {
            continue;
        }
        if (ExcludeRig && OptionalCapsuleRig->EvaluateMinimumSignedDistance(Centroid) < 0.06f)
        {
            continue;
        }

        const auto  CrossVec = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
        const float CrossLen = Mathematics::EvaluateLength(CrossVec);
        const float AreaM2   = 0.5f * CrossLen;
        if (AreaM2 <= 1.0e-5f)
        {
            continue;
        }

        BoundsMin.X = std::min(BoundsMin.X, Centroid.X);
        BoundsMin.Y = std::min(BoundsMin.Y, Centroid.Y);
        BoundsMin.Z = std::min(BoundsMin.Z, Centroid.Z);
        BoundsMax.X = std::max(BoundsMax.X, Centroid.X);
        BoundsMax.Y = std::max(BoundsMax.Y, Centroid.Y);
        BoundsMax.Z = std::max(BoundsMax.Z, Centroid.Z);

        Eligible.push_back(EligibleTriangleSample {
            Centroid,
            CrossVec * (1.0f / CrossLen),
            AreaM2,
        });
    }

    if (Eligible.empty())
    {
        return;
    }

    const float ClampedCell = std::max(0.10f, CellSizeMeters);
    const int   GridNX      = std::clamp(static_cast<int>(std::ceil((BoundsMax.X - BoundsMin.X + 0.2f) / ClampedCell)), 1, 80);
    const int   GridNY      = std::clamp(static_cast<int>(std::ceil((BoundsMax.Y - BoundsMin.Y + 0.2f) / ClampedCell)), 1, 80);
    const int   GridNZ      = std::clamp(static_cast<int>(std::ceil((BoundsMax.Z - BoundsMin.Z + 0.2f) / ClampedCell)), 1, 36);

    struct CellAccumulator
    {
        Mathematics::Axis3D WeightedPosition {};
        Mathematics::Axis3D WeightedNormal   {};
        float               TotalArea        = 0.0f;
        uint32_t            SampleCount      = 0u;
    };

    std::vector<CellAccumulator> Cells(static_cast<std::size_t>(GridNX) * GridNY * GridNZ);
    for (const auto& Entry : Eligible)
    {
        const int IX = std::clamp(static_cast<int>(std::floor((Entry.Centroid.X - BoundsMin.X) / ClampedCell)), 0, GridNX - 1);
        const int IY = std::clamp(static_cast<int>(std::floor((Entry.Centroid.Y - BoundsMin.Y) / ClampedCell)), 0, GridNY - 1);
        const int IZ = std::clamp(static_cast<int>(std::floor((Entry.Centroid.Z - BoundsMin.Z) / ClampedCell)), 0, GridNZ - 1);
        auto& Cell   = Cells[(static_cast<std::size_t>(IZ) * GridNY + IY) * GridNX + IX];
        Cell.WeightedPosition = Cell.WeightedPosition + Entry.Centroid * Entry.AreaM2;
        Cell.WeightedNormal   = Cell.WeightedNormal   + Entry.Normal   * Entry.AreaM2;
        Cell.TotalArea       += Entry.AreaM2;
        Cell.SampleCount     += 1u;
    }

    Disks.reserve(512u);
    for (const auto& Cell : Cells)
    {
        if (Cell.SampleCount == 0u || Cell.TotalArea <= 1.0e-5f)
        {
            continue;
        }
        OccluderSurfelDisk Disk {};
        Disk.PositionWorld = Cell.WeightedPosition * (1.0f / Cell.TotalArea);
        Disk.SurfaceNormal = Mathematics::NormalizeAxis(Cell.WeightedNormal, { 0.0f, 0.0f, 1.0f });
        Disk.RadiusMeters  = std::clamp(std::sqrt(Cell.TotalArea / Constants::PiFloat) * 1.30f, 0.085f, 0.24f);
        Disk.SurfaceAreaM2 = Cell.TotalArea;
        Disks.push_back(Disk);
    }
}

float SurfelOccluderField::EvaluateSoftDirectionalVisibility(
    const Mathematics::Axis3D&                      ReceiverPoint,
    const Mathematics::Axis3D&                      ReceiverNormal,
    const Mathematics::Axis3D&                      UnitDirectionToLight,
    float                                           TangentLightRadius,
    const SpatialAcceleration::WideBoundingTree*    OptionalSceneTree,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig,
    float*                                          OutBlockerDistM) const noexcept
{
    if (OutBlockerDistM != nullptr)
    {
        *OutBlockerDistM = 0.0f;
    }

    const float NdotL = Mathematics::EvaluateDot(ReceiverNormal, UnitDirectionToLight);
    if (NdotL <= 1.0e-4f)
    {
        return 0.0f;
    }

    // 1. Sub-0.28 m exact BVH4 contact shadow gate
    if (OptionalSceneTree != nullptr && !OptionalSceneTree->WideEntries.empty())
    {
        SpatialAcceleration::RaySegmentQuery ContactRay {};
        ContactRay.Origin          = ReceiverPoint + ReceiverNormal * 2.5e-3f;
        ContactRay.UnitDirection   = UnitDirectionToLight;
        ContactRay.MinimumDistance = 1.5e-3f;
        ContactRay.MaximumDistance = 0.28f;
        const auto ContactHit      = OptionalSceneTree->TraceClosestRay(ContactRay, false);
        if (ContactHit.HitConfirmed)
        {
            if (OutBlockerDistM != nullptr)
            {
                *OutBlockerDistM = ContactHit.HitDistanceMeters;
            }
            return 0.0f;
        }
    }

    // 2. Live deforming articulated bone capsule analytical cone visibility
    float CapsuleVisibility = 1.0f;
    if (OptionalCapsuleRig != nullptr && !OptionalCapsuleRig->Capsules.empty())
    {
        CapsuleVisibility = OptionalCapsuleRig->EvaluateSoftConeVisibility(
            ReceiverPoint, ReceiverNormal, UnitDirectionToLight, TangentLightRadius, 6.5f);
        if (CapsuleVisibility <= 0.002f)
        {
            if (OutBlockerDistM != nullptr)
            {
                *OutBlockerDistM = 0.85f;
            }
            return 0.0f;
        }
    }

    // 3. Smooth C1 quartic-kernel multiplicative blocker disk transmittance
    float DiskTransmittance = 1.0f;
    float NearestBlockerM   = 1.0e5f;
    const float ClampedTan  = std::max(0.0045f, TangentLightRadius);

    for (const auto& Disk : Disks)
    {
        const Mathematics::Axis3D Relative = Disk.PositionWorld - ReceiverPoint;
        const float RayProjection = Mathematics::EvaluateDot(Relative, UnitDirectionToLight);
        if (RayProjection <= 0.14f || RayProjection > 12.0f)
        {
            continue;
        }

        const float PlaneDistance = Mathematics::EvaluateDot(Relative, ReceiverNormal);
        const float PlaneFade     = SmoothHermiteStep(0.02f, 0.14f, PlaneDistance);
        if (PlaneFade <= 1.0e-4f)
        {
            continue;
        }

        const Mathematics::Axis3D Perpendicular = Relative - UnitDirectionToLight * RayProjection;
        const float DistSquared   = Mathematics::EvaluateDot(Perpendicular, Perpendicular);
        const float ConeRadius    = std::max(0.036f, RayProjection * ClampedTan);
        const float EffectiveRad  = Disk.RadiusMeters * 1.15f + ConeRadius * 1.25f;
        const float NormalizedR2  = DistSquared / (EffectiveRad * EffectiveRad);

        if (NormalizedR2 < 1.0f)
        {
            const float FalloffW   = 1.0f - NormalizedR2;
            const float QuarticK   = FalloffW * FalloffW;
            const float ProjectedN = std::max(0.45f, std::fabs(Mathematics::EvaluateDot(Disk.SurfaceNormal, UnitDirectionToLight)));
            const float AreaRatio  = std::min(1.0f, (Disk.RadiusMeters * Disk.RadiusMeters * ProjectedN) / (EffectiveRad * EffectiveRad));
            DiskTransmittance     *= std::max(0.0f, 1.0f - QuarticK * AreaRatio * PlaneFade * 1.65f);
            NearestBlockerM        = std::min(NearestBlockerM, RayProjection);
            if (DiskTransmittance <= 0.002f)
            {
                if (OutBlockerDistM != nullptr)
                {
                    *OutBlockerDistM = NearestBlockerM;
                }
                return 0.0f;
            }
        }
    }

    if (OutBlockerDistM != nullptr && NearestBlockerM < 1.0e4f)
    {
        *OutBlockerDistM = NearestBlockerM;
    }
    return std::min(CapsuleVisibility, DiskTransmittance);
}

} // namespace Frontier::GeometricRaster
