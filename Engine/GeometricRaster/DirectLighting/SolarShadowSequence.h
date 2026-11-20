/*============================================================================================================================================
                                                            SOLARSHADOWSEQUENCE.H
============================================================================================================================================*/
// 🧩 Top-2 Direct Solar Shadows (Zero 3D SDF):
//    1. ContactPcss       — 2-Cascade (3.91 mm/tx) PCSS + 0.28 m BVH4 Contact Gate + DeformingCapsuleRig + Plane-Aware Filter
//    2. SolarDiscRayQuery — Owen-Sobol / Vogel Solar Disc BVH4 / RTX RayQuery + SurfelOccluderField + DeformingCapsuleRig

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"
#include "Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.h"
#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DirectShadowTier : uint8_t
{
    GtxContactClipSdfCone    = 0u, // [-] - Compatibility alias mapped to ContactPcss
    RtxDeterministicSolarRay = 1u, // [-] - Compatibility alias mapped to SolarDiscRayQuery
};

enum class SolarShadowAlgorithm : uint8_t
{
    ContactPcss       = 0u, // [-] - Shadow #1: 2-Cascade PCSS (3.91 mm/tx) + 0.28 m BVH4 Contact + DeformingCapsuleRig + Plane-Aware Filter
    SolarDiscRayQuery = 1u, // [-] - Shadow #2: Deterministic Solar Disc BVH4 / RTX RayQuery + SurfelOccluderField + DeformingCapsuleRig
    PcfBilinear       = 2u, // [-] - Lightweight Low-tier 3x3 Tent-Weighted Orthographic PCF fallback

    // Compatibility aliases for existing workspaces & exhibits
    WideTreeRayQuery  = SolarDiscRayQuery,
    PcssPlaneAware    = ContactPcss,
    ShadowRayMarch    = ContactPcss,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OmnidirectionalPointLight
{
    Mathematics::Axis3D    PositionWorld { 0.0f, 0.0f, 2.2f };  // [m]     - Point light world position
    Mathematics::LinearRGB Color         { 1.0f, 0.94f, 0.86f };// [-]     - Normalized linear emitter tint
    float                  Intensity     = 12.0f;               // [cd]    - Luminous intensity scale
    float                  RangeMeters   = 12.0f;               // [m]     - Smooth window cutoff distance
    float                  RadiusMeters  = 0.12f;               // [m]     - Finite emitter sphere radius
};

struct SolarDirectionalSource
{
    Mathematics::Axis3D    DirectionToSun           { -0.45f, -0.52f, 0.725f }; // [-]     - Unit vector pointing toward solar disc
    Mathematics::LinearRGB SolarIrradiance          { 4.20f,   3.98f, 3.65f  }; // [W/m^2] - Direct perpendicular solar irradiance
    float                  SunAngularRadiusRadians  = 0.028f;                   // [rad]   - Physical 0.00465 (0.53 deg) .. studio soft sun
    uint32_t               RtxSamplesPerPixel       = 8u;                       // [count] - Stratified solar disc rays per pixel
    bool                   PreferSurfelSoftBlockers = false;                    // [-]     - Use denoiser-free SurfelOccluderField cone when available
};

struct OrthographicShadowCascade
{
    Mathematics::Axis3D CenterWorld        {};      // [m]   - Cascade world anchor
    Mathematics::Axis3D LightRightAxis     {};      // [-]   - Unit cascade +U axis
    Mathematics::Axis3D LightUpAxis        {};      // [-]   - Unit cascade +V axis
    Mathematics::Axis3D LightForwardAxis   {};      // [-]   - Unit light propagation direction (-DirectionToSun)
    float               HalfSpanMeters     = 2.0f;  // [m]   - Orthographic half-width (2.0 m -> 4.0 m / 1024 = 3.906 mm/tx!)
    float               DepthExtentMeters  = 24.0f; // [m]   - +/- orthographic Z capture slab
    uint32_t            ResolutionTexels   = 1024u; // [px]  - Shadow depth grid width & height
    std::vector<float>  ClosestLightDepth  {};      // [m]   - Closest blocker distance from cascade near slab
};

struct SolarShadowFieldResult
{
    uint32_t           WidthPx               = 0u;   // [px]  - Viewport width
    uint32_t           HeightPx              = 0u;   // [px]  - Viewport height
    std::vector<float> RawVisibility         {};     // [0-1] - Unfiltered solar visibility
    std::vector<float> FilteredVisibility    {};     // [0-1] - Coplanar dilated + 3-step plane-aware filtered visibility
    std::vector<float> BlockerDistanceMeters {};     // [m]   - Nearest/harmonic occluder distance (0 = sunlit / contact)
    float              CascadeZeroTexelMm    = 0.0f; // [mm]  - Physical texel footprint of Cascade 0 (~3.91 mm)
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR SHADOW SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class SolarShadowSequence
{
public:
    // Builds Cascade 0 (4.0 m span @ 1024 -> 3.91 mm/tx) and Cascade 1 (16.0 m span @ 1024 -> 15.6 mm/tx) from scene geometry
    static void BuildTwoCascadeContactClips(
        const SolarDirectionalSource&                Sun,
        Mathematics::Axis3D                          FocusCenterWorld,
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        OrthographicShadowCascade&                   OutFineCascade,
        OrthographicShadowCascade&                   OutCoarseCascade);

    // Evaluates Top-2 Solar Shadow Algorithm across primary surface samples (Zero 3D SDF!)
    [[nodiscard]] static SolarShadowFieldResult EvaluateSolarShadowFieldWithAlgorithm(
        SolarShadowAlgorithm                            Algorithm,
        const SolarDirectionalSource&                   Sun,
        uint32_t                                        ViewportWidth,
        uint32_t                                        ViewportHeight,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const OrthographicShadowCascade*                OptionalFineClip   = nullptr,
        const OrthographicShadowCascade*                OptionalCoarseClip = nullptr,
        const SurfelOccluderField*                      OptionalSurfels    = nullptr,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr);

    // Compatibility wrapper mapping DirectShadowTier -> SolarShadowAlgorithm
    [[nodiscard]] static SolarShadowFieldResult EvaluateSolarShadowField(
        DirectShadowTier                                Tier,
        const SolarDirectionalSource&                   Sun,
        uint32_t                                        ViewportWidth,
        uint32_t                                        ViewportHeight,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const void*                                     UnusedLegacySdf    = nullptr,
        const OrthographicShadowCascade*                OptionalFineClip   = nullptr,
        const OrthographicShadowCascade*                OptionalCoarseClip = nullptr);

    // Executes Coplanar Blocker Dilation + 3-Step Plane-Aware Continuous-Sigma À-Trous Edge-Preserving Shadow Filter
    static void ApplyPlaneAwarePenumbraFilter(
        uint32_t                                       ViewportWidth,
        uint32_t                                       ViewportHeight,
        float                                          TanSunAngularRadius,
        std::span<const AnalyticResolvedSurfaceSample> PrimarySamples,
        SolarShadowFieldResult&                        InOutField);
};

} // namespace Frontier::GeometricRaster
