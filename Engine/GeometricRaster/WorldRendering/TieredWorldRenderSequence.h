/*============================================================================================================================================
                                                         TIEREDWORLDRENDERSEQUENCE.H
============================================================================================================================================*/
// 🧩 Unified 5-Engine Zero-SDF World Render Sequence (MatCap, OpenPbrDirect, DynamicIrradianceGi, ContourSurfelGi, DeterministicPathTrace)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/ContourSurfelIlluminationSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/DynamicIrradianceCacheSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/WeightedPathReplaySequence.h"
#include "Engine/GeometricRaster/PathTracing/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/WorldRendering/SpecularReflectionSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SimulationSpace/FluidSpace/FluidSimulationSequence.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
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

enum class WorldRenderTier : uint8_t
{
    Tier1GtxCalibrated    = 0u, // [-] - Compatibility alias -> ContourSurfelGi + ContactPcss + WideTreeRaytrace
    Tier2RtxDeterministic = 1u, // [-] - Compatibility alias -> DynamicIrradianceGi + SolarDiscRayQuery + WideTreeRaytrace
};

enum class WorldRenderEngine : uint8_t
{
    MatCap                 = 0u, // [-] - Studio MatCap Normal/Roughness Shading (Zero GI, Zero Shadow Maps)
    OpenPbrDirect          = 1u, // [-] - Direct OpenPBR + LTC Area Lights + Top-2 Solar Shadows + BVH4/Sky Reflections
    DynamicIrradianceGi    = 2u, // [-] - GTX GI #2: 8x8 Octahedral Cache + 16x16 Chebyshev + 4-Candidate Owen-Sobol RIS
    ContourSurfelGi        = 3u, // [-] - GTX GI #1: Stokes Spherical-Polygon Contour + 2-Bounce Surfels + SH9 (Zero Denoiser)
    DeterministicPathTrace = 4u, // [-] - 6-Queue Lock-Free Owen-Sobol (0,2) Wavefront Path Tracer (GTX BVH4 & RTX RayQuery)

    // Compatibility aliases for existing workspaces & validation exhibits
    DdgiRaytracing         = DynamicIrradianceGi,
    SdfGlobalIllumination  = ContourSurfelGi,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldRenderConfiguration
{
    WorldRenderEngine        Engine                    = WorldRenderEngine::ContourSurfelGi;
    SolarShadowAlgorithm     ShadowAlgorithm           = SolarShadowAlgorithm::ContactPcss;
    SpecularReflectionSource ReflectionSource          = SpecularReflectionSource::WideTreeRaytrace;
    uint32_t                 DebugChannel              = 0u;    // [0..17] - 0=Lit..15=SpecRefl, 16=BounceSurfelGi, 17=CapsuleVisibility
    uint32_t                 PathTraceSamples          = 16u;   // [spp]   - Samples per pixel for DeterministicPathTrace
    uint32_t                 PathTraceMaxBounces       = 5u;    // [cnt]   - Maximum bounce depth for DeterministicPathTrace
    bool                     PathTraceDenoise          = true;  // [-]     - 3-step albedo/normal/depth A-Trous reconstruction
    bool                     SdfGiBilateral            = true;  // [-]     - Optional spatial filter toggle
    bool                     GlobalIlluminationEnabled = true;  // [-]     - Render setting toggle for indirect GI / bounces
    bool                     ReflectionsEnabled        = true;  // [-]     - Render setting toggle for specular reflections
    bool                     RaytracingEnabled         = true;  // [-]     - Render setting toggle for hardware/BVH raytracing
    uint32_t                 QualityTier               = 1u;    // [0..2]  - 0=Medium, 1=High, 2=Epic
};

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>                           ClusterInstances {};
    MaterialSpace::CompactMaterialRegister                      MaterialRegister {};
    std::vector<MaterialSpace::BounceSlabEntry>                 BounceSummaries  {};
    SpatialAcceleration::WideBoundingTree                       SceneTree        {};
    SpatialAcceleration::DeformingCapsuleRig                    CapsuleRig       {};
    SurfelOccluderField                                         OccluderField    {};
    std::vector<OmnidirectionalPointLight>                      PointLights      {};
    SolarDirectionalSource                                      Sun              {};
    std::vector<PolygonalAreaQuadLight>                         AreaLights       {};
    IrradianceLatticeConfiguration                              LatticeConfig    {};
    ContourSurfelConfiguration                                  ContourGiConfig  {};
    Mathematics::LinearRGB                                      SkyRadiance      { 0.15f, 0.21f, 0.32f };
    WorldAtmosphereConfiguration                                Atmosphere       {};
    const SimulationSpace::FluidSpace::FluidSimulationSequence* OptionalFluid    = nullptr;
};

struct WorldRenderOutput
{
    uint32_t                            WidthPx            = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx           = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier         = WorldRenderTier::Tier2RtxDeterministic;
    WorldRenderEngine                   ActiveEngine       = WorldRenderEngine::DynamicIrradianceGi;
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr {};     // [cd/m2] - Final Direct + Diffuse GI + Specular + Glass HDR
    std::vector<Mathematics::LinearRGB> IndirectDiffuseHdr {};     // [cd/m2] - Isolated diffuse global illumination buffer
    std::vector<uint8_t>                DisplayRgba8       {};     // [rgba8] - ACES filmic + sRGB8 viewport output
    TwoPhaseCullReport                  VisibilityCull     {};     // [-]     - Cluster culling telemetry
    DirectLightingOutput                DirectLighting     {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularGlass      {};     // [-]     - Reflection & glass refraction telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TieredWorldRenderSequence
{
public:
    static void CompileBounceSummaries(WorldSceneSpecification& InOutScene);

    void PrepareWorldScene(WorldSceneSpecification& InOutScene);

    void SynchronizeWorldAtmosphere(WorldSceneSpecification& InOutScene);

    void PrepareRtxIrradianceLatticeOnly(WorldSceneSpecification& InOutScene);

    void AssignIrradianceLattice(IrradianceLatticeSequence&& BakedLattice) noexcept;

    [[nodiscard]] WorldRenderOutput RenderWorldFrameWithEngine(
        const WorldRenderConfiguration& Config,
        const PerspectiveCameraBasis&   CurrentCamera,
        const PerspectiveCameraBasis&   PreviousCamera,
        const WorldSceneSpecification&  PreparedScene);

    [[nodiscard]] WorldRenderOutput RenderWorldFrame(
        WorldRenderTier                Tier,
        const PerspectiveCameraBasis&  CurrentCamera,
        const PerspectiveCameraBasis&  PreviousCamera,
        const WorldSceneSpecification& PreparedScene);

    [[nodiscard]] const IrradianceLatticeSequence&         InspectIrradianceLattice()      const noexcept { return RtxLattice; }
    [[nodiscard]] const DynamicIrradianceCacheSequence&    InspectDynamicIrradianceCache() const noexcept { return IrradianceCache; }
    [[nodiscard]] const ContourSurfelIlluminationSequence& InspectContourSurfelGi()        const noexcept { return ContourSurfelGi; }
    [[nodiscard]] const AtmosphericSkySequence&            InspectAtmosphericSky()         const noexcept { return SkySequence; }

private:
    OrthographicShadowCascade            FineContactClip      {};
    OrthographicShadowCascade            CoarseContactClip    {};
    IrradianceLatticeSequence            RtxLattice           {};
    DynamicIrradianceCacheSequence       IrradianceCache      {};
    ContourSurfelIlluminationSequence    ContourSurfelGi      {};
    AtmosphericSkySequence               SkySequence          {};
    DepthPyramidSequence                 PreviousHiZ          {};
    std::vector<ReservoirSampleState>    PreviousReservoirs   {};
    PerspectiveCameraBasis               PreviousReplayCamera {};
    uint32_t                             ReplaySequenceIndex  = 0u;
};

} // namespace Frontier::GeometricRaster
