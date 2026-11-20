/*============================================================================================================================================
                                                       DYNAMICIRRADIANCEGIVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation Exhibit for Zero-SDF GTX GI #2: 8x8 Octahedral DynamicIrradianceCache + 16x16 Chebyshev + WeightedPathReplay

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/GlobalIllumination/DynamicIrradianceCacheSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/WeightedPathReplaySequence.h"
#include "Engine/GeometricRaster/WorldRendering/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/WorldRendering/TieredWorldRenderSequence.h"

#include <cmath>
#include <cstdio>

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
            std::fprintf(stdout, "[DynamicIrradianceGiValidation][OK]   %s\n", Label);
        }
        else
        {
            ++RejectedCount;
            std::fprintf(stderr, "[DynamicIrradianceGiValidation][FAIL] %s\n", Label);
        }
    }
};

} // namespace

int main()
{
    ValidationTally Tally {};

    GeometricRaster::ShowcaseWorldSceneBundle Bundle {};
    GeometricRaster::BuildShowcaseGalleryScene(Bundle, 96u, 54u, true);

    GeometricRaster::TieredWorldRenderSequence Renderer {};
    Renderer.PrepareWorldScene(Bundle.Scene);

    const auto& Cache   = Renderer.InspectDynamicIrradianceCache();
    const auto& Lattice = Renderer.InspectIrradianceLattice();
    Tally.Verify(!Cache.InspectCells().empty() && Cache.InspectCells().size() == Lattice.InspectCells().size(),
                 "Zero-SDF DynamicIrradianceCacheSequence populates 8x8 octahedral irradiance + 16x16 Chebyshev cells");

    const auto SampleIrr = Cache.SampleIrradianceOverPi(
        { 0.0f, 0.25f, 0.15f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f, 0.2f });
    Tally.Verify(std::isfinite(SampleIrr.R) && std::isfinite(SampleIrr.G) && std::isfinite(SampleIrr.B) &&
                     (SampleIrr.R + SampleIrr.G + SampleIrr.B) > 0.01f,
                 "Chebyshev visibility-gated trilinear octahedral lookup produces finite positive irradiance");

    GeometricRaster::WorldRenderConfiguration Cfg {};
    Cfg.Engine           = GeometricRaster::WorldRenderEngine::DynamicIrradianceGi;
    Cfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::SolarDiscRayQuery;
    Cfg.ReflectionSource = GeometricRaster::SpecularReflectionSource::WideTreeRaytrace;

    const auto Output = Renderer.RenderWorldFrameWithEngine(Cfg, Bundle.EditorCamera, Bundle.EditorCamera, Bundle.Scene);
    double IndirectEnergy = 0.0;
    for (const auto& Pix : Output.IndirectDiffuseHdr)
    {
        IndirectEnergy += static_cast<double>(Pix.R + Pix.G + Pix.B);
    }
    Tally.Verify(Output.DisplayRgba8.size() == 96u * 54u * 4u && IndirectEnergy > 1.0,
                 "DynamicIrradianceGi + SolarDiscRayQuery renders valid HDR & sRGB8 viewport output");

    std::fprintf(stdout, "[DynamicIrradianceGiValidation] Verified=%u Rejected=%u\n", Tally.VerifiedCount, Tally.RejectedCount);
    return (Tally.RejectedCount == 0u) ? 0 : 1;
}
