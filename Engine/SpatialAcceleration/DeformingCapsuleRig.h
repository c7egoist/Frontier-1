/*============================================================================================================================================
                                                          DEFORMINGCAPSULERIG.H
============================================================================================================================================*/
// 🧩 Analytic 32-byte tapered bone capsules for sub-0.1 ms articulated character pose updates, ray occlusion, & soft solar cones

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 32-byte GPU-aligned analytic tapered bone capsule (matches GpuTaperedCapsule32 in WideTreeRayQuery.slang)
struct alignas(16) TaperedBoneCapsule
{
    Mathematics::Axis3D JointStart  {};      // [m] - Proximal joint world coordinate
    float               RadiusStart = 0.06f; // [m] - Proximal capsule radius
    Mathematics::Axis3D JointEnd    {};      // [m] - Distal joint world coordinate
    float               RadiusEnd   = 0.06f; // [m] - Distal capsule radius

    [[nodiscard]] float EvaluateSignedDistance(const Mathematics::Axis3D& WorldPoint) const noexcept;
};
static_assert(sizeof(TaperedBoneCapsule) == 32u, "TaperedBoneCapsule must remain 32 bytes for GPU StructuredBuffer parity.");

struct DeformingCapsuleRig
{
    std::vector<TaperedBoneCapsule> Capsules        {};
    Mathematics::Axis3D             RootCenterWorld {};
    float                           BoundingRadiusM = 2.15f;

    [[nodiscard]] float EvaluateMinimumSignedDistance(const Mathematics::Axis3D& WorldPoint) const noexcept;

    [[nodiscard]] bool QueryRayOccluded(
        const Mathematics::Axis3D& Origin,
        const Mathematics::Axis3D& UnitDirection,
        float                      MinimumDistance,
        float                      MaximumDistance) const noexcept;

    // Evaluates smooth analytical solar or area-light cone visibility [0..1] across all live bone capsules
    [[nodiscard]] float EvaluateSoftConeVisibility(
        const Mathematics::Axis3D& ReceiverPoint,
        const Mathematics::Axis3D& ReceiverNormal,
        const Mathematics::Axis3D& UnitDirectionToLight,
        float                      TangentConeHalfAngle,
        float                      MaxDistanceMeters = 6.5f) const noexcept;

    // Updates 10-bone humanoid pose in place (< 0.1 ms, zero mesh voxelization) for PosePhase in [0, 1]
    void UpdateHumanoidPose(
        float                      PosePhase,
        const Mathematics::Axis3D& RootWorld) noexcept;

    // Appends watertight tapered limb geometry matching the current capsule pose into OutNetwork
    void AppendRigPolygonGeometry(
        GeometrySpace::PolygonNetwork& OutNetwork,
        uint16_t                       PrimaryMaterialSlot,
        uint16_t                       AccentMaterialSlot,
        uint32_t                       RadialSides = 10u) const;
};

} // namespace Frontier::SpatialAcceleration
