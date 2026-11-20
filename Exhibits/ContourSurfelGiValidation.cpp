/*============================================================================================================================================
                                                        CONTOURSURFELGIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation Exhibit for Zero-SDF GTX GI #1: Stokes Spherical-Polygon Contour + 2-Bounce Surfels + SH9 + DeformingCapsuleRig

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h"
#include "Engine/GeometricRaster/GlobalIllumination/ContourSurfelIlluminationSequence.h"
#include "Engine/GeometricRaster/WorldRendering/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/SpatialAcceleration/DeformingCapsuleRig.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Frontier;

namespace
{

struct ValidationTally
{
    uint32_t VerifiedCount = 0u;
    uint32_t RejectedCount = 0u;

    void Verify(bool Condition, const char* Label)
    {
        if (Condition)
        {
            ++VerifiedCount;
            std::fprintf(stdout, "[ContourSurfelGiValidation][OK]   %s\n", Label);
        }
        else
        {
            ++RejectedCount;
            std::fprintf(stderr, "[ContourSurfelGiValidation][FAIL] %s\n", Label);
        }
    }
};

} // namespace

int main()
{
    ValidationTally Tally {};

    // 1. Analytic Stokes spherical-polygon contour integral test (unit horizontal emissive triangle above origin)
    {
        GeometricRaster::EmissiveContourTriangle EmitTri {};
        EmitTri.VertexA      = { -0.5f, -0.5f, 1.0f };
        EmitTri.VertexB      = {  0.5f, -0.5f, 1.0f };
        EmitTri.VertexC      = { -0.5f,  0.5f, 1.0f };
        EmitTri.UnitNormal   = {  0.0f,  0.0f, -1.0f };
        EmitTri.EmittedRad   = { 10.0f, 10.0f, 10.0f };
        EmitTri.AreaM2       = 0.5f;
        EmitTri.TwoSidedEmit = false;

        const float FormFactor = GeometricRaster::ContourSurfelIlluminationSequence::EvaluateClippedTriangleFormFactor(
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f },
            EmitTri.VertexA,
            EmitTri.VertexB,
            EmitTri.VertexC);
        Tally.Verify(FormFactor > 0.08f && FormFactor < 0.25f,
                     "Stokes spherical-polygon contour integral returns bounded positive cosine form factor");

        // Backfacing receiver sees zero flux
        const float BackForm = GeometricRaster::ContourSurfelIlluminationSequence::EvaluateClippedTriangleFormFactor(
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f },
            EmitTri.VertexA,
            EmitTri.VertexB,
            EmitTri.VertexC);
        Tally.Verify(BackForm == 0.0f,
                     "Upper-hemisphere Sutherland-Hodgman clip rejects lower-hemisphere contour completely");
    }

    // 2. DeformingCapsuleRig live articulated pose + soft shadow cone
    {
        SpatialAcceleration::DeformingCapsuleRig Rig {};
        Rig.UpdateHumanoidPose(0.35f, { 0.0f, 0.0f, -0.60f });
        Tally.Verify(Rig.Capsules.size() == 10u,
                     "DeformingCapsuleRig populates 10 articulated 32-byte tapered bone capsules");

        const float OccludedVis = Rig.EvaluateSoftConeVisibility(
            { 0.0f, 0.0f, -0.60f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
            0.03f,
            10.0f);
        const float UnoccludedVis = Rig.EvaluateSoftConeVisibility(
            { 3.5f, 3.5f, -0.60f },
            { 0.0f, 0.0f, 1.0f },
            { 0.0f, 0.0f, 1.0f },
            0.03f,
            10.0f);
        Tally.Verify(OccludedVis < 0.55f && UnoccludedVis > 0.99f,
                     "DeformingCapsuleRig attenuates soft solar cone under character while leaving clear receivers unoccluded");
    }

    // 3. Full Showcase Gallery scene: SurfelOccluderField + 2-Bounce ContourSurfelGi + Engine 3 viewport render
    {
        GeometricRaster::ShowcaseWorldSceneBundle Bundle {};
        GeometricRaster::BuildShowcaseGalleryScene(Bundle, 96u, 54u, true);
        Tally.Verify(!Bundle.Scene.OccluderField.Disks.empty(),
                     "ShowcaseGalleryScene builds Zero-SDF SurfelOccluderField blocker disks");

        GeometricRaster::TieredWorldRenderSequence Renderer {};
        Renderer.PrepareWorldScene(Bundle.Scene);
        Tally.Verify(!Renderer.InspectContourSurfelGi().InspectBounceSurfels().empty(),
                     "PrepareWorldScene builds 2-bounce area-stratified Bunnell surfels for ContourSurfelGi");

        GeometricRaster::WorldRenderConfiguration Cfg {};
        Cfg.Engine           = GeometricRaster::WorldRenderEngine::ContourSurfelGi;
        Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::ContactPcss;
        Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;

        const auto Output = Renderer.RenderWorldFrameWithEngine(Cfg, Bundle.EditorCamera, Bundle.EditorCamera, Bundle.Scene);
        double IndirectSum = 0.0;
        for (const auto& Irr : Output.IndirectDiffuseHdr)
        {
            IndirectSum += static_cast<double>(Irr.R + Irr.G + Irr.B);
        }
        Tally.Verify(Output.DisplayRgba8.size() == 96u * 54u * 4u && IndirectSum > 1.0,
                     "ContourSurfelGi renders denoiser-free indirect diffuse global illumination across showcase viewport");
    }

    std::fprintf(stdout, "[ContourSurfelGiValidation] Verified=%u Rejected=%u\n", Tally.VerifiedCount, Tally.RejectedCount);
    return (Tally.RejectedCount == 0u) ? 0 : 1;
}
