/*============================================================================================================================================
                                                    CONTOURSURFELILLUMINATIONSEQUENCE.H
============================================================================================================================================*/
// 🧩 GTX GI #1 (Denoiser-Free): Analytic Stokes Spherical-Polygon Emissive Contour + 2-Bounce Stratified Surfel Radiosity + Anti-Banded SH9

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) EmissiveContourTriangle
{
    Mathematics::Axis3D    VertexA       {};      // [m]       - World triangle vertex A
    float                  AreaM2        = 0.0f;  // [m2]      - Triangle surface area
    Mathematics::Axis3D    VertexB       {};      // [m]       - World triangle vertex B
    uint32_t               ClusterIndex  = 0u;    // [idx]     - Owning EmissiveClusterSummary index
    Mathematics::Axis3D    VertexC       {};      // [m]       - World triangle vertex C
    uint32_t               TwoSidedEmit  = 0u;    // [0/1]     - 1 if emitting from both sides
    Mathematics::Axis3D    UnitNormal    {};      // [-]       - Outward geometric luminaire normal
    float                  PadScalar     = 0.0f;  // [-]       - 16B alignment padding
    Mathematics::LinearRGB EmittedRad    {};      // [W/sr/m2] - Emitted surface radiance
    float                  CdfUpper      = 1.0f;  // [0..1]    - Flux-weighted cumulative distribution
};

struct EmissiveClusterSummary
{
    Mathematics::Axis3D    CenterWorld   {};      // [m]       - Flux-weighted cluster center
    float                  BoundRadiusM  = 0.15f; // [m]       - Bounding sphere radius
    Mathematics::Axis3D    MeanNormal    {};      // [-]       - Flux-weighted luminaire normal
    float                  TotalAreaM2   = 0.0f;  // [m2]      - Summed cluster area
    Mathematics::LinearRGB MeanRadiance  {};      // [W/sr/m2] - Area-averaged radiance
    uint32_t               FirstTriIdx   = 0u;    // [idx]     - Start in EmissiveTriangles
    uint32_t               TriCount      = 0u;    // [cnt]     - Triangle count in cluster
};

struct alignas(16) RadiantBounceSurfel
{
    Mathematics::Axis3D    PositionWorld {};      // [m]       - Stratified surface patch center
    float                  AreaM2        = 0.10f; // [m2]      - Surface patch area
    Mathematics::Axis3D    UnitNormal    {};      // [-]       - Outward surface normal
    float                  RadiusMeters  = 0.18f; // [m]       - Equivalent disk radius
    Mathematics::LinearRGB DiffuseAlbedo {};      // [0..1]    - Base diffuse reflectance
    float                  SkyVisibility = 1.0f;  // [0..1]    - Cached hemisphere sky openness
    Mathematics::LinearRGB ExitantOverPi {};      // [W/sr/m2] - Multi-bounce diffuse exitant radiance E*rho/pi
    float                  PadTail       = 0.0f;  // [-]       - 16B alignment padding
};

struct SphericalHarmonics9Cell
{
    std::array<Mathematics::LinearRGB, 9u> Coeffs {}; // [-] - Order-2 real SH9 irradiance/pi coefficients
};

struct ContourSurfelConfiguration
{
    uint32_t TargetBounceSurfels      = 320u; // [cnt] - Stratified diffuse surface patches across scene
    uint32_t RadiosityBounces         = 2u;   // [cnt] - Multi-bounce surfel-to-surfel relaxation steps
    uint32_t ShGridCountX             = 11u;  // [cnt] - Anti-banded SH9 ambient field X resolution
    uint32_t ShGridCountY             = 9u;   // [cnt] - Anti-banded SH9 ambient field Y resolution
    uint32_t ShGridCountZ             = 5u;   // [cnt] - Anti-banded SH9 ambient field Z resolution
    float    SurfelDiskSolidAngleGain = 1.0f; // [-]   - Bunnell disk form-factor gain
    float    SurfelShadowGateMeters   = 2.6f; // [m]   - Near-field BVH4 shadow gate distance
    bool     IncludeDirectContoursInGi = false; // [-] - True when DirectLighting skips emissive triangles
};

//------------------------------------------------------------------------------------------------------------------------
//                                        CONTOUR SURFEL ILLUMINATION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class ContourSurfelIlluminationSequence
{
public:
    // Exact closed-form Lambert/Stokes spherical-polygon form factor F = (1/pi) * integral_Tri max(0, n.w) dw in [0..1]
    [[nodiscard]] static float EvaluateClippedTriangleFormFactor(
        const Mathematics::Axis3D& ReceiverPoint,
        const Mathematics::Axis3D& ReceiverNormal,
        const Mathematics::Axis3D& TriVertexA,
        const Mathematics::Axis3D& TriVertexB,
        const Mathematics::Axis3D& TriVertexC) noexcept;

    // Prepares emissive contour clusters, 2-bounce area-stratified surfels, & anti-banded SH9 diffuse field (Zero 3D SDF!)
    void PrepareSceneIllumination(
        const ContourSurfelConfiguration&               Config,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
        std::span<const MaterialSpace::BounceSlabEntry> BounceSummaries,
        const SolarDirectionalSource&                   Sun,
        std::span<const PolygonalAreaQuadLight>         AreaLights,
        std::span<const OmnidirectionalPointLight>      PointLights,
        Mathematics::LinearRGB                          SkyRadiance,
        const SurfelOccluderField*                      OptionalOccluders  = nullptr,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr);

    // Evaluates denoiser-free 1/pi diffuse irradiance E(N)/pi at arbitrary world surface point
    [[nodiscard]] Mathematics::LinearRGB EvaluateIrradianceOverPi(
        const Mathematics::Axis3D&                      ReceiverPoint,
        const Mathematics::Axis3D&                      ReceiverNormal,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SurfelOccluderField*                      OptionalOccluders  = nullptr,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr) const noexcept;

    // Evaluates full viewport indirect diffuse E(N)/pi buffer in parallel across scanlines
    [[nodiscard]] std::vector<Mathematics::LinearRGB> EvaluateViewportIndirectOverPi(
        uint32_t                                        ViewportWidth,
        uint32_t                                        ViewportHeight,
        std::span<const AnalyticResolvedSurfaceSample>  PrimarySamples,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SurfelOccluderField*                      OptionalOccluders  = nullptr,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig = nullptr) const;

    [[nodiscard]] std::span<const EmissiveContourTriangle> InspectEmissiveTriangles() const noexcept { return EmissiveTriangles; }
    [[nodiscard]] std::span<const EmissiveClusterSummary>  InspectEmissiveClusters()  const noexcept { return EmissiveClusters; }
    [[nodiscard]] std::span<const RadiantBounceSurfel>     InspectBounceSurfels()     const noexcept { return BounceSurfels; }
    [[nodiscard]] std::span<const SphericalHarmonics9Cell> InspectSh9Cells()          const noexcept { return Sh9Grid; }

private:
    [[nodiscard]] Mathematics::LinearRGB EvaluateDirectEmissiveContoursOverPi(
        const Mathematics::Axis3D&                      ReceiverPoint,
        const Mathematics::Axis3D&                      ReceiverNormal,
        const SpatialAcceleration::WideBoundingTree&    SceneTree,
        const SurfelOccluderField*                      OptionalOccluders,
        const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB EvaluateNearBounceSurfelsOverPi(
        const Mathematics::Axis3D& ReceiverPoint,
        const Mathematics::Axis3D& ReceiverNormal) const noexcept;

    [[nodiscard]] Mathematics::LinearRGB SampleSh9IrradianceOverPi(
        const Mathematics::Axis3D& WorldPoint,
        const Mathematics::Axis3D& UnitNormal) const noexcept;

    ContourSurfelConfiguration           ActiveConfig      {};
    Mathematics::Axis3D                  ShBoundsMin       { -6.0f, -5.0f, 0.0f };
    Mathematics::Axis3D                  ShBoundsMax       {  6.0f,  5.0f, 3.4f };
    Mathematics::Axis3D                  ShCellSpacing     {  1.0f,  1.0f, 1.0f };
    std::vector<EmissiveContourTriangle> EmissiveTriangles {};
    std::vector<EmissiveClusterSummary>  EmissiveClusters  {};
    std::vector<RadiantBounceSurfel>     BounceSurfels     {};
    std::vector<SphericalHarmonics9Cell> Sh9Grid           {};
};

} // namespace Frontier::GeometricRaster
