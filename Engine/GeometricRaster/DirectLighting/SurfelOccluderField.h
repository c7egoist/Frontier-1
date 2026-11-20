/*============================================================================================================================================
                                                          SURFELOCCLUDERFIELD.H
============================================================================================================================================*/
// 🧩 Area-stratified blocker surfel disks + live DeformingCapsuleRig C1 quartic-kernel soft shadow transmittance (Zero 3D SDF)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 32-byte GPU-aligned area-stratified blocker surfel disk
struct alignas(16) OccluderSurfelDisk
{
    Mathematics::Axis3D PositionWorld {};       // [m] - Stratified surface cluster centroid
    float               RadiusMeters  = 0.16f;  // [m] - Effective circular blocker radius
    Mathematics::Axis3D SurfaceNormal {};       // [-] - Area-weighted unit surface normal
    float               SurfaceAreaM2 = 0.04f;  // [m2]- Clustered triangle surface area
};
static_assert(sizeof(OccluderSurfelDisk) == 32u, "OccluderSurfelDisk must remain 32 bytes for GPU StructuredBuffer parity.");

class SurfelOccluderField
{
public:
    std::vector<OccluderSurfelDisk> Disks {};

    // Clusters interior occluding scene geometry into ~250-512 area-stratified blocker disks (excludes outer walls & live capsule rig)
    void BuildFromSceneTree(
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr,
        float                                           CellSizeMeters     = 0.16f);

    // Evaluates denoiser-free C1 quartic-kernel blocker transmittance + 0.28 m BVH4 contact gate + DeformingCapsuleRig cone [0..1]
    [[nodiscard]] float EvaluateSoftDirectionalVisibility(
        const Mathematics::Axis3D&                      ReceiverPoint,
        const Mathematics::Axis3D&                      ReceiverNormal,
        const Mathematics::Axis3D&                      UnitDirectionToLight,
        float                                           TangentLightRadius,
        const SpatialAcceleration::WideBoundingTree*    OptionalSceneTree  = nullptr,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr,
        float*                                          OutBlockerDistM    = nullptr) const noexcept;
};

} // namespace Frontier::GeometricRaster
