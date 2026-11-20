/*============================================================================================================================================
                                                   DETERMINISTICPATHTRACESEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Unified Bit-Exact Owen-Sobol (0,2) 6-Queue Wavefront Path Tracer for BOTH GTX (WideBoundingTree BVH4) & RTX (VK_KHR_ray_query)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/PathTracing/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier::GeometricRaster
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           SHARED FIXED-POINT & SHADING HELPERS
//------------------------------------------------------------------------------------------------------------------------

void AccumulateFixedWordsCpu(
    std::span<uint32_t>    FixedWords,
    uint32_t               PixelIdx,
    Mathematics::LinearRGB Contrib) noexcept
{
    const float Channels[3] = {
        std::clamp(Contrib.R, 0.0f, 32768.0f),
        std::clamp(Contrib.G, 0.0f, 32768.0f),
        std::clamp(Contrib.B, 0.0f, 32768.0f),
    };
    const std::size_t PixelWordOffset = static_cast<std::size_t>(PixelIdx) * 6u;
    for (uint32_t Ch = 0u; Ch < 3u; ++Ch)
    {
        const float Val   = Channels[Ch];
        uint32_t    Whole = static_cast<uint32_t>(std::floor(Val));
        uint32_t    Frac  = static_cast<uint32_t>(std::floor((Val - static_cast<float>(Whole)) * 16777216.0f + 0.5f));
        Whole            += (Frac >> 24u);
        Frac             &= 0x00FFFFFFu;

        const uint32_t WordLo = ((Whole & 0xFFu) << 24u) | Frac;
        const uint32_t WordHi = (Whole >> 8u);
        if (WordLo == 0u && WordHi == 0u)
        {
            continue;
        }
        const uint32_t PrevLo                      = FixedWords[PixelWordOffset + Ch * 2u + 0u];
        const uint32_t SumLo                       = PrevLo + WordLo;
        FixedWords[PixelWordOffset + Ch * 2u + 0u] = SumLo;
        const uint32_t Carry                       = (SumLo < PrevLo) ? 1u : 0u;
        FixedWords[PixelWordOffset + Ch * 2u + 1u] += (WordHi + Carry);
    }
}

[[nodiscard]] float DecodeFixedWordsCpu(uint32_t WordLo, uint32_t WordHi, float InvSamples) noexcept
{
    const float WholeLo = static_cast<float>(WordLo >> 24u);
    const float Frac24  = static_cast<float>(WordLo & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    const float WholeHi = static_cast<float>(WordHi) * 256.0f;
    return (WholeHi + WholeLo + Frac24) * InvSamples;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateWavefrontSkyCpu(
    Mathematics::Axis3D    UnitDir,
    Mathematics::LinearRGB AmbientSky) noexcept
{
    const float ElevationBlend = std::clamp(UnitDir.Z * 0.5f + 0.5f, 0.0f, 1.0f);
    const Mathematics::LinearRGB UpperSky      { AmbientSky.R * 1.25f, AmbientSky.G * 1.25f, AmbientSky.B * 1.25f };
    const Mathematics::LinearRGB EquatorialSky { AmbientSky.R * 0.92f, AmbientSky.G * 0.83f, AmbientSky.B * 0.72f };
    const Mathematics::LinearRGB GroundSky     { AmbientSky.R * 0.22f, AmbientSky.G * 0.22f, AmbientSky.B * 0.22f };
    if (UnitDir.Z >= 0.0f)
    {
        const float T = std::pow(std::clamp(UnitDir.Z, 0.0f, 1.0f), 0.65f);
        return {
            EquatorialSky.R + (UpperSky.R - EquatorialSky.R) * T,
            EquatorialSky.G + (UpperSky.G - EquatorialSky.G) * T,
            EquatorialSky.B + (UpperSky.B - EquatorialSky.B) * T,
        };
    }
    const float T = std::clamp(ElevationBlend * 2.0f, 0.0f, 1.0f);
    return {
        GroundSky.R + (EquatorialSky.R - GroundSky.R) * T,
        GroundSky.G + (EquatorialSky.G - GroundSky.G) * T,
        GroundSky.B + (EquatorialSky.B - GroundSky.B) * T,
    };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateWavefrontBsdfCpu(
    const WavefrontGpuMaterial32& Mat,
    Mathematics::Axis3D           UnitN,
    Mathematics::Axis3D           UnitV,
    Mathematics::Axis3D           UnitL,
    float&                        OutBsdfPdf) noexcept
{
    OutBsdfPdf        = 0.0f;
    const float NdotL = Mathematics::EvaluateDot(UnitN, UnitL);
    const float NdotV = std::max(1.0e-4f, Mathematics::EvaluateDot(UnitN, UnitV));
    if (NdotL <= 1.0e-5f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float Alpha = std::clamp(Mat.RoughnessLinear * Mat.RoughnessLinear, 0.004f, 1.0f);
    const Mathematics::Axis3D Half = Mathematics::NormalizeAxis(
        { UnitV.X + UnitL.X, UnitV.Y + UnitL.Y, UnitV.Z + UnitL.Z }, UnitN);
    const float NdotH = std::clamp(Mathematics::EvaluateDot(UnitN, Half), 0.0f, 1.0f);
    const float VdotH = std::clamp(Mathematics::EvaluateDot(UnitV, Half), 0.0f, 1.0f);

    const float Metallic = std::clamp(Mat.MetallicWeight, 0.0f, 1.0f);
    const float F0R      = 0.04f + (Mat.SurfaceAlbedo.R - 0.04f) * Metallic;
    const float F0G      = 0.04f + (Mat.SurfaceAlbedo.G - 0.04f) * Metallic;
    const float F0B      = 0.04f + (Mat.SurfaceAlbedo.B - 0.04f) * Metallic;
    const float Schlick  = std::pow(1.0f - VdotH, 5.0f);
    const float FrR      = F0R + (1.0f - F0R) * Schlick;
    const float FrG      = F0G + (1.0f - F0G) * Schlick;
    const float FrB      = F0B + (1.0f - F0B) * Schlick;

    const float A2    = Alpha * Alpha;
    const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
    const float D     = A2 / std::max(1.0e-7f, Constants::PiFloat * Denom * Denom);
    const float G1V   = (2.0f * NdotV) / std::max(1.0e-6f, NdotV + std::sqrt(A2 + (1.0f - A2) * NdotV * NdotV));
    const float G1L   = (2.0f * NdotL) / std::max(1.0e-6f, NdotL + std::sqrt(A2 + (1.0f - A2) * NdotL * NdotL));
    const float SpecS = (D * G1V * G1L) / std::max(1.0e-5f, 4.0f * NdotV * NdotL);

    const float DiffScale = (1.0f - Metallic) * static_cast<float>(Constants::InversePi);
    const float SpecProb  = std::clamp(0.25f + 0.65f * Metallic, 0.20f, 0.90f);
    const float PdfSpec   = (D * G1V) / std::max(1.0e-5f, 4.0f * NdotV);
    const float PdfDiff   = NdotL * static_cast<float>(Constants::InversePi);
    OutBsdfPdf            = PdfDiff + (PdfSpec - PdfDiff) * SpecProb;

    return {
        Mat.SurfaceAlbedo.R * DiffScale * (1.0f - FrR) + FrR * SpecS,
        Mat.SurfaceAlbedo.G * DiffScale * (1.0f - FrG) + FrG * SpecS,
        Mat.SurfaceAlbedo.B * DiffScale * (1.0f - FrB) + FrB * SpecS,
    };
}

void ExtractWavefrontMaterialBuffers(
    const SpatialAcceleration::WideBoundingTree&  SceneTree,
    const MaterialSpace::CompactMaterialRegister& MaterialRegister,
    std::span<const PolygonalAreaQuadLight>       AreaLights,
    std::vector<WavefrontGpuMaterial32>&          OutMaterials,
    std::vector<EmissiveContourTriangle>&         OutEmissives)
{
    const uint32_t SlotCount = std::max(1u, MaterialRegister.QuerySlotCount());
    OutMaterials.assign(SlotCount, WavefrontGpuMaterial32 {});
    for (uint32_t Idx = 0u; Idx < MaterialRegister.QuerySlotCount(); ++Idx)
    {
        const auto& Authored = MaterialRegister.InspectAuthoredMaterial(static_cast<uint16_t>(Idx));
        auto& Dst            = OutMaterials[Idx];
        Dst.SurfaceAlbedo = {
            std::clamp(Authored.Diffuse.BaseColor.R * Authored.Diffuse.BaseWeight, 0.02f, 0.98f),
            std::clamp(Authored.Diffuse.BaseColor.G * Authored.Diffuse.BaseWeight, 0.02f, 0.98f),
            std::clamp(Authored.Diffuse.BaseColor.B * Authored.Diffuse.BaseWeight, 0.02f, 0.98f),
        };
        Dst.RoughnessLinear = std::clamp(Authored.Specular.Roughness, 0.04f, 1.0f);
        Dst.MetallicWeight  = std::clamp(Authored.Specular.Metalness, 0.0f, 1.0f);
        Dst.EmittedRadiance = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Authored.PorosityLight);
    }

    OutEmissives.clear();
    for (const auto& Prim : SceneTree.Primitives)
    {
        const uint32_t MatIdx = std::min<uint32_t>(Prim.MaterialOrdinal, SlotCount - 1u);
        const auto& EmitRad   = OutMaterials[MatIdx].EmittedRadiance;
        const float Lum       = 0.2126f * EmitRad.R + 0.7152f * EmitRad.G + 0.0722f * EmitRad.B;
        if (Lum <= 1.0e-4f)
        {
            continue;
        }
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
        const float DoubleArea          = Mathematics::EvaluateLength(Cross);
        if (DoubleArea <= 1.0e-6f)
        {
            continue;
        }
        EmissiveContourTriangle Tri {};
        Tri.VertexA      = Prim.Corner0;
        Tri.VertexB      = { Prim.Corner0.X + Prim.Edge01.X, Prim.Corner0.Y + Prim.Edge01.Y, Prim.Corner0.Z + Prim.Edge01.Z };
        Tri.VertexC      = { Prim.Corner0.X + Prim.Edge02.X, Prim.Corner0.Y + Prim.Edge02.Y, Prim.Corner0.Z + Prim.Edge02.Z };
        Tri.AreaM2       = 0.5f * DoubleArea;
        Tri.UnitNormal   = { Cross.X / DoubleArea, Cross.Y / DoubleArea, Cross.Z / DoubleArea };
        Tri.EmittedRad   = EmitRad;
        Tri.TwoSidedEmit = 1u;
        OutEmissives.push_back(Tri);
    }

    for (const auto& Quad : AreaLights)
    {
        const float Lum = 0.2126f * Quad.EmittedRadiance.R + 0.7152f * Quad.EmittedRadiance.G + 0.0722f * Quad.EmittedRadiance.B;
        if (Lum <= 1.0e-4f || Quad.HalfWidthMeters <= 1.0e-4f || Quad.HalfHeightMeters <= 1.0e-4f)
        {
            continue;
        }
        const Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Quad.RightTangent, { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Quad.UpBitangent,  { 0.0f, 1.0f, 0.0f });
        const Mathematics::Axis3D QuadN = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Up), { 0.0f, 0.0f, -1.0f });
        const Mathematics::Axis3D RX { Right.X * Quad.HalfWidthMeters, Right.Y * Quad.HalfWidthMeters, Right.Z * Quad.HalfWidthMeters };
        const Mathematics::Axis3D UY { Up.X * Quad.HalfHeightMeters,   Up.Y * Quad.HalfHeightMeters,   Up.Z * Quad.HalfHeightMeters   };

        const Mathematics::Axis3D C0 { Quad.CenterWorld.X - RX.X - UY.X, Quad.CenterWorld.Y - RX.Y - UY.Y, Quad.CenterWorld.Z - RX.Z - UY.Z };
        const Mathematics::Axis3D C1 { Quad.CenterWorld.X + RX.X - UY.X, Quad.CenterWorld.Y + RX.Y - UY.Y, Quad.CenterWorld.Z + RX.Z - UY.Z };
        const Mathematics::Axis3D C2 { Quad.CenterWorld.X + RX.X + UY.X, Quad.CenterWorld.Y + RX.Y + UY.Y, Quad.CenterWorld.Z + RX.Z + UY.Z };
        const Mathematics::Axis3D C3 { Quad.CenterWorld.X - RX.X + UY.X, Quad.CenterWorld.Y - RX.Y + UY.Y, Quad.CenterWorld.Z - RX.Z + UY.Z };
        const float TriArea = 2.0f * Quad.HalfWidthMeters * Quad.HalfHeightMeters;

        EmissiveContourTriangle T0 {};
        T0.VertexA      = C0;
        T0.VertexB      = C1;
        T0.VertexC      = C2;
        T0.AreaM2       = TriArea;
        T0.UnitNormal   = QuadN;
        T0.EmittedRad   = Quad.EmittedRadiance;
        T0.TwoSidedEmit = Quad.TwoSidedEmission ? 1u : 0u;
        OutEmissives.push_back(T0);

        EmissiveContourTriangle T1 = T0;
        T1.VertexB = C2;
        T1.VertexC = C3;
        OutEmissives.push_back(T1);
    }

    float TotalFlux = 0.0f;
    for (const auto& Tri : OutEmissives)
    {
        const float Lum = 0.2126f * Tri.EmittedRad.R + 0.7152f * Tri.EmittedRad.G + 0.0722f * Tri.EmittedRad.B;
        TotalFlux += std::max(1.0e-5f, Tri.AreaM2 * Lum);
    }
    float AccumFlux = 0.0f;
    for (auto& Tri : OutEmissives)
    {
        const float Lum = 0.2126f * Tri.EmittedRad.R + 0.7152f * Tri.EmittedRad.G + 0.0722f * Tri.EmittedRad.B;
        AccumFlux      += std::max(1.0e-5f, Tri.AreaM2 * Lum);
        Tri.CdfUpper    = (TotalFlux > 1.0e-6f) ? std::min(1.0f, AccumFlux / TotalFlux) : 1.0f;
    }
    if (!OutEmissives.empty())
    {
        OutEmissives.back().CdfUpper = 1.0f;
    }
}

void EncodeAcesDisplayBuffer(
    std::span<const Mathematics::LinearRGB> LinearHdr,
    std::vector<uint8_t>&                   OutRgba8)
{
    auto AcesCurve = [](float X) noexcept -> uint8_t
    {
        const float V = std::max(0.0f, X);
        const float Mapped = std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
        const float Srgb   = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::round(Srgb * 255.0f), 0.0f, 255.0f));
    };
    OutRgba8.resize(LinearHdr.size() * 4u);
    for (std::size_t Idx = 0u; Idx < LinearHdr.size(); ++Idx)
    {
        OutRgba8[Idx * 4u + 0u] = AcesCurve(LinearHdr[Idx].R);
        OutRgba8[Idx * 4u + 1u] = AcesCurve(LinearHdr[Idx].G);
        OutRgba8[Idx * 4u + 2u] = AcesCurve(LinearHdr[Idx].B);
        OutRgba8[Idx * 4u + 3u] = 255u;
    }
}

[[nodiscard]] DeviceBufferResource AllocateHostVisibleWavefrontBuffer(
    VulkanInterchange& Device,
    uint64_t           ByteSize,
    VkBufferUsageFlags UsageMask,
    uint64_t           Alignment = 0ull)
{
    return Device.AllocateBuffer(
        ByteSize,
        UsageMask,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        Alignment);
}

[[nodiscard]] DeviceBufferResource AllocateDeviceLocalWavefrontBuffer(
    VulkanInterchange& Device,
    uint64_t           ByteSize,
    VkBufferUsageFlags UsageMask,
    uint64_t           Alignment = 0ull)
{
    return Device.AllocateBuffer(
        ByteSize,
        UsageMask,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        0u,
        Alignment);
}

void SubmitWavefrontSingleTimeCommands(VulkanInterchange& Device, VkCommandBuffer Cmd)
{
    const uint64_t Tick = Device.DispatchCommandBufferAsync(Cmd);
    (void)Device.AwaitTimelineTick(Tick, 60000000000ull);
    Device.FreeCommandBuffer(Cmd);
}

bool ConstructComputeProgramFromSpirv(
    VulkanInterchange&           Device,
    const std::filesystem::path& SpvPath,
    const char*                  EntryPoint,
    VkPipelineLayout             ProgramLayout,
    VkPipeline&                  OutProgram)
{
    VkShaderModule ShaderUnit = Device.LoadShaderBinary(SpvPath);
    if (ShaderUnit == VK_NULL_HANDLE)
    {
        return false;
    }

    VkComputePipelineCreateInfo CreateSpec {};
    CreateSpec.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    CreateSpec.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    CreateSpec.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    CreateSpec.stage.module       = ShaderUnit;
    CreateSpec.stage.pName        = EntryPoint;
    CreateSpec.layout             = ProgramLayout;

    const VkResult Res = vkCreateComputePipelines(
        Device.AccessDevice(), VK_NULL_HANDLE, 1u, &CreateSpec, nullptr, &OutProgram);
    vkDestroyShaderModule(Device.AccessDevice(), ShaderUnit, nullptr);
    return (Res == VK_SUCCESS);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      DETERMINISTIC PATH TRACE SEQUENCE METHODS
//------------------------------------------------------------------------------------------------------------------------

DeterministicPathTraceSequence::~DeterministicPathTraceSequence()
{
    TeardownVulkanWavefront();
}

uint32_t DeterministicPathTraceSequence::ComputeRgbaDeterminismDigest(std::span<const uint8_t> DisplayRgba8) noexcept
{
    uint32_t Digest = 2166136261u;
    for (const uint8_t Byte : DisplayRgba8)
    {
        Digest ^= static_cast<uint32_t>(Byte);
        Digest *= 16777619u;
    }
    return Digest;
}

DeterministicPathTraceResult DeterministicPathTraceSequence::EvaluateCpuDeterministicSequence(
    const DeterministicPathTraceConfiguration&      Config,
    const PerspectiveCameraBasis&                   Camera,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    const SolarDirectionalSource&                   Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    Mathematics::LinearRGB                          SkyRadiance,
    const SpatialAcceleration::DeformingCapsuleRig* OptionalCapsuleRig)
{
    const auto StartClock = std::chrono::steady_clock::now();
    DeterministicPathTraceResult Result {};
    Result.WidthPx      = std::max(1u, Camera.ViewportWidthPx);
    Result.HeightPx     = std::max(1u, Camera.ViewportHeightPx);
    Result.ExecutedTier = PathTraceExecutionTier::GtxWideBvh4Compute;

    const uint32_t TotalPixels  = Result.WidthPx * Result.HeightPx;
    const uint32_t TotalSamples = std::max(1u, Config.SamplesPerPixel);
    const uint32_t BatchSamples = std::max(1u, std::min(TotalSamples, Config.SampleBatchCount));
    const uint32_t MaxBounces   = std::max(1u, Config.MaxBounceDepth);

    std::vector<WavefrontGpuMaterial32>  Materials {};
    std::vector<EmissiveContourTriangle> Emissives {};
    ExtractWavefrontMaterialBuffers(SceneTree, MaterialRegister, AreaLights, Materials, Emissives);

    std::vector<uint32_t>              FixedAccumWords(static_cast<std::size_t>(TotalPixels) * 6u, 0u);
    std::vector<WavefrontSurfaceHit64> SurfaceHitsCurr {};
    std::vector<WavefrontSurfaceHit64> SurfaceHitsNext {};
    std::vector<WavefrontBounceRay48>  BounceRays {};
    std::vector<WavefrontShadowRay48>  ShadowRays {};

    const auto CamBasis              = ComputeCameraBasis(Camera);
    const float TanHalfFov           = std::tan(0.5f * Camera.VerticalFovRadians);
    const float Aspect               = static_cast<float>(Result.WidthPx) / static_cast<float>(Result.HeightPx);
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.35f, -0.45f, 0.82f });
    const float TanSunRadius         = std::tan(std::max(0.0045f, Sun.SunAngularRadiusRadians));

    auto AnyHitOccluded = [&](Mathematics::Axis3D Origin, Mathematics::Axis3D Dir, float MinT, float MaxT) -> bool
    {
        SpatialAcceleration::RaySegmentQuery Seg {};
        Seg.Origin          = Origin;
        Seg.MinimumDistance = MinT;
        Seg.UnitDirection   = Dir;
        Seg.MaximumDistance = MaxT;
        if (SceneTree.QueryRayOccluded(Seg))
        {
            return true;
        }
        if (OptionalCapsuleRig != nullptr && !OptionalCapsuleRig->Capsules.empty())
        {
            return OptionalCapsuleRig->EvaluateSoftConeVisibility(Origin, { 0.0f, 0.0f, 1.0f }, Dir, 0.005f, MaxT) < 0.5f;
        }
        return false;
    };

    for (uint32_t SampleStart = 0u; SampleStart < TotalSamples; SampleStart += BatchSamples)
    {
        const uint32_t CurBatch = std::min(BatchSamples, TotalSamples - SampleStart);
        SurfaceHitsCurr.clear();
        SurfaceHitsCurr.reserve(static_cast<std::size_t>(TotalPixels) * CurBatch);

        for (uint32_t PixIdx = 0u; PixIdx < TotalPixels; ++PixIdx)
        {
            const uint32_t PX = PixIdx % Result.WidthPx;
            const uint32_t PY = PixIdx / Result.WidthPx;
            const uint32_t PixelKey = Config.SharedQuadratureNet
                ? 0u
                : AvalancheKey32((PY << 16u) ^ PX ^ 0xA341316Cu);

            for (uint32_t LocalS = 0u; LocalS < CurBatch; ++LocalS)
            {
                const uint32_t SampleIdx = SampleStart + LocalS;
                const auto SubJ = Sobol02Sample(SampleIdx, PixelKey, 0u);
                const float U = ((static_cast<float>(PX) + SubJ.X) / static_cast<float>(Result.WidthPx)) * 2.0f - 1.0f;
                const float V = 1.0f - ((static_cast<float>(PY) + SubJ.Y) / static_cast<float>(Result.HeightPx)) * 2.0f;

                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    {
                        CamBasis.ForwardAxis.X + CamBasis.RightAxis.X * (U * Aspect * TanHalfFov) + CamBasis.UpAxis.X * (V * TanHalfFov),
                        CamBasis.ForwardAxis.Y + CamBasis.RightAxis.Y * (U * Aspect * TanHalfFov) + CamBasis.UpAxis.Y * (V * TanHalfFov),
                        CamBasis.ForwardAxis.Z + CamBasis.RightAxis.Z * (U * Aspect * TanHalfFov) + CamBasis.UpAxis.Z * (V * TanHalfFov),
                    },
                    CamBasis.ForwardAxis);

                SpatialAcceleration::RaySegmentQuery PrimarySeg {};
                PrimarySeg.Origin          = Camera.CameraOrigin;
                PrimarySeg.MinimumDistance = 1.0e-3f;
                PrimarySeg.UnitDirection   = RayDir;
                PrimarySeg.MaximumDistance = 500.0f;

                const auto Hit = SceneTree.TraceClosestRay(PrimarySeg);
                if (!Hit.HitConfirmed)
                {
                    auto Sky = EvaluateWavefrontSkyCpu(RayDir, SkyRadiance);
                    if (Mathematics::EvaluateDot(RayDir, SunDir) >= std::cos(std::max(0.0045f, TanSunRadius)))
                    {
                        const float SolidA = std::max(1.0e-5f, Constants::PiFloat * TanSunRadius * TanSunRadius);
                        Sky.R += std::min(64.0f, Sun.SolarIrradiance.R / SolidA);
                        Sky.G += std::min(64.0f, Sun.SolarIrradiance.G / SolidA);
                        Sky.B += std::min(64.0f, Sun.SolarIrradiance.B / SolidA);
                    }
                    AccumulateFixedWordsCpu(FixedAccumWords, PixIdx, Sky);
                    continue;
                }

                const uint32_t MatIdx = std::min<uint32_t>(Hit.MaterialOrdinal, static_cast<uint32_t>(Materials.size() - 1u));
                const auto& Mat       = Materials[MatIdx];
                if (Mat.EmittedRadiance.R > 1.0e-5f || Mat.EmittedRadiance.G > 1.0e-5f || Mat.EmittedRadiance.B > 1.0e-5f)
                {
                    AccumulateFixedWordsCpu(FixedAccumWords, PixIdx, Mat.EmittedRadiance);
                }

                const bool FrontFace            = (Mathematics::EvaluateDot(RayDir, Hit.GeometricNormal) < 0.0f);
                const Mathematics::Axis3D OrientedN = FrontFace
                    ? Hit.GeometricNormal
                    : Mathematics::Axis3D { -Hit.GeometricNormal.X, -Hit.GeometricNormal.Y, -Hit.GeometricNormal.Z };

                WavefrontSurfaceHit64 HitEntry {};
                HitEntry.HitPositionWorld = {
                    Camera.CameraOrigin.X + RayDir.X * Hit.HitDistanceMeters,
                    Camera.CameraOrigin.Y + RayDir.Y * Hit.HitDistanceMeters,
                    Camera.CameraOrigin.Z + RayDir.Z * Hit.HitDistanceMeters,
                };
                HitEntry.PixelLinearIndex = PixIdx;
                HitEntry.GeometricNormal  = OrientedN;
                HitEntry.MaterialOrdinal  = MatIdx;
                HitEntry.IncomingRayDir   = RayDir;
                HitEntry.SampleBounceKey  = (SampleIdx & 0xFFFFu);
                HitEntry.PathThroughput   = { 1.0f, 1.0f, 1.0f };
                HitEntry.HitDistanceM     = Hit.HitDistanceMeters;
                SurfaceHitsCurr.push_back(HitEntry);
            }
        }

        for (uint32_t Depth = 0u; Depth < MaxBounces && !SurfaceHitsCurr.empty(); ++Depth)
        {
            BounceRays.clear();
            ShadowRays.clear();
            SurfaceHitsNext.clear();

            for (const auto& Hit : SurfaceHitsCurr)
            {
                const uint32_t SampleIdx = Hit.SampleBounceKey & 0xFFFFu;
                const uint32_t PX        = Hit.PixelLinearIndex % Result.WidthPx;
                const uint32_t PY        = Hit.PixelLinearIndex / Result.WidthPx;
                const uint32_t PixelKey  = Config.SharedQuadratureNet
                    ? 0u
                    : AvalancheKey32((PY << 16u) ^ PX ^ 0xA341316Cu);

                const auto& Mat = Materials[std::min<uint32_t>(Hit.MaterialOrdinal, static_cast<uint32_t>(Materials.size() - 1u))];
                Mathematics::Axis3D UnitN = Hit.GeometricNormal;
                const Mathematics::Axis3D UnitV { -Hit.IncomingRayDir.X, -Hit.IncomingRayDir.Y, -Hit.IncomingRayDir.Z };
                if (Mathematics::EvaluateDot(UnitN, UnitV) < 0.0f)
                {
                    UnitN = { -UnitN.X, -UnitN.Y, -UnitN.Z };
                }

                Mathematics::Axis3D TanU {}, BitV {};
                BuildDuffBasis(UnitN, TanU, BitV);
                const uint32_t QuadratureOffset = 1u + Depth * 4u;

                if (Sun.SolarIrradiance.R > 1.0e-5f || Sun.SolarIrradiance.G > 1.0e-5f || Sun.SolarIrradiance.B > 1.0e-5f)
                {
                    const auto XiSun = Sobol02Sample(SampleIdx, PixelKey, QuadratureOffset + 0u);
                    Mathematics::Axis3D SunU {}, SunV {};
                    BuildDuffBasis(SunDir, SunU, SunV);
                    const float Rad = std::sqrt(XiSun.X) * TanSunRadius;
                    const float Ang = XiSun.Y * Constants::TauFloat;
                    const Mathematics::Axis3D SunL = Mathematics::NormalizeAxis(
                        {
                            SunDir.X + SunU.X * (Rad * std::cos(Ang)) + SunV.X * (Rad * std::sin(Ang)),
                            SunDir.Y + SunU.Y * (Rad * std::cos(Ang)) + SunV.Y * (Rad * std::sin(Ang)),
                            SunDir.Z + SunU.Z * (Rad * std::cos(Ang)) + SunV.Z * (Rad * std::sin(Ang)),
                        },
                        SunDir);
                    const float NdotS = Mathematics::EvaluateDot(UnitN, SunL);
                    if (NdotS > 1.0e-4f)
                    {
                        float BsdfPdf = 0.0f;
                        const auto Bsdf = EvaluateWavefrontBsdfCpu(Mat, UnitN, UnitV, SunL, BsdfPdf);
                        WavefrontShadowRay48 Sh {};
                        Sh.OriginWorld = {
                            Hit.HitPositionWorld.X + UnitN.X * 2.0e-3f,
                            Hit.HitPositionWorld.Y + UnitN.Y * 2.0e-3f,
                            Hit.HitPositionWorld.Z + UnitN.Z * 2.0e-3f,
                        };
                        Sh.PixelLinearIndex  = Hit.PixelLinearIndex;
                        Sh.UnitDirection     = SunL;
                        Sh.MaxDistanceM      = 300.0f;
                        Sh.UnoccludedContrib = {
                            Hit.PathThroughput.R * Bsdf.R * Sun.SolarIrradiance.R * NdotS,
                            Hit.PathThroughput.G * Bsdf.G * Sun.SolarIrradiance.G * NdotS,
                            Hit.PathThroughput.B * Bsdf.B * Sun.SolarIrradiance.B * NdotS,
                        };
                        ShadowRays.push_back(Sh);
                    }
                }

                if (!Emissives.empty())
                {
                    const auto XiEmit = Sobol02Sample(SampleIdx, PixelKey, QuadratureOffset + 1u);
                    const auto XiTri  = Sobol02Sample(SampleIdx, PixelKey, QuadratureOffset + 2u);
                    auto It = std::lower_bound(
                        Emissives.begin(), Emissives.end(), XiEmit.X,
                        [](const EmissiveContourTriangle& T, float Key) { return T.CdfUpper < Key; });
                    const std::size_t ChosenIdx = (It == Emissives.end())
                        ? (Emissives.size() - 1u)
                        : static_cast<std::size_t>(std::distance(Emissives.begin(), It));
                    const auto& EmitTri = Emissives[ChosenIdx];
                    const float PrevCdf = (ChosenIdx > 0u) ? Emissives[ChosenIdx - 1u].CdfUpper : 0.0f;
                    const float PickPm  = std::max(1.0e-6f, EmitTri.CdfUpper - PrevCdf);

                    const float SqrtU = std::sqrt(XiTri.X);
                    const float B0    = 1.0f - SqrtU;
                    const float B1    = SqrtU * (1.0f - XiTri.Y);
                    const float B2    = SqrtU * XiTri.Y;
                    const Mathematics::Axis3D EmitP {
                        EmitTri.VertexA.X * B0 + EmitTri.VertexB.X * B1 + EmitTri.VertexC.X * B2,
                        EmitTri.VertexA.Y * B0 + EmitTri.VertexB.Y * B1 + EmitTri.VertexC.Y * B2,
                        EmitTri.VertexA.Z * B0 + EmitTri.VertexB.Z * B1 + EmitTri.VertexC.Z * B2,
                    };
                    const Mathematics::Axis3D ToEmit {
                        EmitP.X - Hit.HitPositionWorld.X,
                        EmitP.Y - Hit.HitPositionWorld.Y,
                        EmitP.Z - Hit.HitPositionWorld.Z,
                    };
                    const float DistSq = Mathematics::EvaluateDot(ToEmit, ToEmit);
                    const float Dist   = std::sqrt(std::max(1.0e-6f, DistSq));
                    const Mathematics::Axis3D UnitL { ToEmit.X / Dist, ToEmit.Y / Dist, ToEmit.Z / Dist };
                    const float CosRec = Mathematics::EvaluateDot(UnitN, UnitL);
                    const float RawEm  = -Mathematics::EvaluateDot(EmitTri.UnitNormal, UnitL);
                    const float CosEm  = (EmitTri.TwoSidedEmit != 0u) ? std::abs(RawEm) : RawEm;

                    if (CosRec > 1.0e-4f && CosEm > 1.0e-4f && Dist > 0.015f)
                    {
                        const float LightPdf = (PickPm * DistSq) / std::max(1.0e-6f, EmitTri.AreaM2 * CosEm);
                        float BsdfPdf = 0.0f;
                        const auto Bsdf  = EvaluateWavefrontBsdfCpu(Mat, UnitN, UnitV, UnitL, BsdfPdf);
                        const float MisW  = PowerHeuristicMis(LightPdf, BsdfPdf);
                        const float Scale = (CosRec * MisW) / LightPdf;
                        WavefrontShadowRay48 Sh {};
                        Sh.OriginWorld = {
                            Hit.HitPositionWorld.X + UnitN.X * 2.0e-3f,
                            Hit.HitPositionWorld.Y + UnitN.Y * 2.0e-3f,
                            Hit.HitPositionWorld.Z + UnitN.Z * 2.0e-3f,
                        };
                        Sh.PixelLinearIndex  = Hit.PixelLinearIndex;
                        Sh.UnitDirection     = UnitL;
                        Sh.MaxDistanceM      = std::max(0.01f, Dist - 4.0e-3f);
                        Sh.UnoccludedContrib = {
                            std::min(64.0f, Hit.PathThroughput.R * Bsdf.R * EmitTri.EmittedRad.R * Scale),
                            std::min(64.0f, Hit.PathThroughput.G * Bsdf.G * EmitTri.EmittedRad.G * Scale),
                            std::min(64.0f, Hit.PathThroughput.B * Bsdf.B * EmitTri.EmittedRad.B * Scale),
                        };
                        ShadowRays.push_back(Sh);
                    }
                }

                if (Depth + 1u >= MaxBounces)
                {
                    continue;
                }
                const auto XiBsdf    = Sobol02Sample(SampleIdx, PixelKey, QuadratureOffset + 3u);
                const float SpecProb = std::clamp(0.25f + 0.65f * Mat.MetallicWeight, 0.20f, 0.90f);
                const float Alpha    = std::clamp(Mat.RoughnessLinear * Mat.RoughnessLinear, 0.004f, 1.0f);
                Mathematics::Axis3D SampledL {};
                if (XiBsdf.X < SpecProb)
                {
                    const Mathematics::Axis3D LocalWo {
                        Mathematics::EvaluateDot(UnitV, TanU),
                        Mathematics::EvaluateDot(UnitV, BitV),
                        std::max(1.0e-4f, Mathematics::EvaluateDot(UnitV, UnitN)),
                    };
                    const auto LocalWi = SampleSphericalCapGgxVndfLocal(LocalWo, Alpha, Alpha, XiBsdf.X / SpecProb, XiBsdf.Y);
                    SampledL = Mathematics::NormalizeAxis(
                        {
                            TanU.X * LocalWi.X + BitV.X * LocalWi.Y + UnitN.X * LocalWi.Z,
                            TanU.Y * LocalWi.X + BitV.Y * LocalWi.Y + UnitN.Y * LocalWi.Z,
                            TanU.Z * LocalWi.X + BitV.Z * LocalWi.Y + UnitN.Z * LocalWi.Z,
                        },
                        UnitN);
                }
                else
                {
                    const float UDiff = (XiBsdf.X - SpecProb) / std::max(1.0e-5f, 1.0f - SpecProb);
                    const float SinT  = std::sqrt(std::max(0.0f, UDiff));
                    const float CosT  = std::sqrt(std::max(0.0f, 1.0f - UDiff));
                    const float Phi   = Constants::TauFloat * XiBsdf.Y;
                    SampledL = Mathematics::NormalizeAxis(
                        {
                            TanU.X * (SinT * std::cos(Phi)) + BitV.X * (SinT * std::sin(Phi)) + UnitN.X * CosT,
                            TanU.Y * (SinT * std::cos(Phi)) + BitV.Y * (SinT * std::sin(Phi)) + UnitN.Y * CosT,
                            TanU.Z * (SinT * std::cos(Phi)) + BitV.Z * (SinT * std::sin(Phi)) + UnitN.Z * CosT,
                        },
                        UnitN);
                }

                const float NdotB = Mathematics::EvaluateDot(UnitN, SampledL);
                if (NdotB <= 1.0e-4f)
                {
                    continue;
                }
                float BouncePdf = 0.0f;
                const auto BounceBsdf = EvaluateWavefrontBsdfCpu(Mat, UnitN, UnitV, SampledL, BouncePdf);
                if (BouncePdf <= 1.0e-5f)
                {
                    continue;
                }
                const float Weight = NdotB / BouncePdf;
                WavefrontBounceRay48 Next {};
                Next.OriginWorld = {
                    Hit.HitPositionWorld.X + UnitN.X * 2.0e-3f,
                    Hit.HitPositionWorld.Y + UnitN.Y * 2.0e-3f,
                    Hit.HitPositionWorld.Z + UnitN.Z * 2.0e-3f,
                };
                Next.PixelLinearIndex = Hit.PixelLinearIndex;
                Next.UnitDirection    = SampledL;
                Next.SampleBounceKey  = (SampleIdx & 0xFFFFu) | ((Depth + 1u) << 16u);
                Next.PathThroughput   = {
                    std::min(12.0f, Hit.PathThroughput.R * BounceBsdf.R * Weight),
                    std::min(12.0f, Hit.PathThroughput.G * BounceBsdf.G * Weight),
                    std::min(12.0f, Hit.PathThroughput.B * BounceBsdf.B * Weight),
                };
                Next.PreviousBsdfPdf  = BouncePdf;
                BounceRays.push_back(Next);
            }

            for (const auto& Sh : ShadowRays)
            {
                if (!AnyHitOccluded(Sh.OriginWorld, Sh.UnitDirection, 1.5e-3f, Sh.MaxDistanceM))
                {
                    AccumulateFixedWordsCpu(FixedAccumWords, Sh.PixelLinearIndex, Sh.UnoccludedContrib);
                }
            }

            for (const auto& Ray : BounceRays)
            {
                SpatialAcceleration::RaySegmentQuery BounceSeg {};
                BounceSeg.Origin          = Ray.OriginWorld;
                BounceSeg.MinimumDistance = 1.5e-3f;
                BounceSeg.UnitDirection   = Ray.UnitDirection;
                BounceSeg.MaximumDistance = 300.0f;

                const auto Hit = SceneTree.TraceClosestRay(BounceSeg);
                if (!Hit.HitConfirmed)
                {
                    const auto Sky = EvaluateWavefrontSkyCpu(Ray.UnitDirection, SkyRadiance);
                    AccumulateFixedWordsCpu(
                        FixedAccumWords,
                        Ray.PixelLinearIndex,
                        { Ray.PathThroughput.R * Sky.R, Ray.PathThroughput.G * Sky.G, Ray.PathThroughput.B * Sky.B });
                    continue;
                }

                const uint32_t MatIdx = std::min<uint32_t>(Hit.MaterialOrdinal, static_cast<uint32_t>(Materials.size() - 1u));
                const auto& Mat       = Materials[MatIdx];
                if (Mat.EmittedRadiance.R > 1.0e-5f || Mat.EmittedRadiance.G > 1.0e-5f || Mat.EmittedRadiance.B > 1.0e-5f)
                {
                    const float MisW = Emissives.empty() ? 1.0f : 0.35f;
                    AccumulateFixedWordsCpu(
                        FixedAccumWords,
                        Ray.PixelLinearIndex,
                        {
                            Ray.PathThroughput.R * Mat.EmittedRadiance.R * MisW,
                            Ray.PathThroughput.G * Mat.EmittedRadiance.G * MisW,
                            Ray.PathThroughput.B * Mat.EmittedRadiance.B * MisW,
                        });
                }

                const bool FrontFace            = (Mathematics::EvaluateDot(Ray.UnitDirection, Hit.GeometricNormal) < 0.0f);
                const Mathematics::Axis3D OrientedN = FrontFace
                    ? Hit.GeometricNormal
                    : Mathematics::Axis3D { -Hit.GeometricNormal.X, -Hit.GeometricNormal.Y, -Hit.GeometricNormal.Z };
                WavefrontSurfaceHit64 HitEntry {};
                HitEntry.HitPositionWorld = {
                    Ray.OriginWorld.X + Ray.UnitDirection.X * Hit.HitDistanceMeters,
                    Ray.OriginWorld.Y + Ray.UnitDirection.Y * Hit.HitDistanceMeters,
                    Ray.OriginWorld.Z + Ray.UnitDirection.Z * Hit.HitDistanceMeters,
                };
                HitEntry.PixelLinearIndex = Ray.PixelLinearIndex;
                HitEntry.GeometricNormal  = OrientedN;
                HitEntry.MaterialOrdinal  = MatIdx;
                HitEntry.IncomingRayDir   = Ray.UnitDirection;
                HitEntry.SampleBounceKey  = Ray.SampleBounceKey;
                HitEntry.PathThroughput   = Ray.PathThroughput;
                HitEntry.HitDistanceM     = Hit.HitDistanceMeters;
                SurfaceHitsNext.push_back(HitEntry);
            }

            SurfaceHitsCurr.swap(SurfaceHitsNext);
        }
    }

    Result.LinearHdr.resize(TotalPixels);
    const float InvSamples = 1.0f / static_cast<float>(TotalSamples);
    for (uint32_t PixIdx = 0u; PixIdx < TotalPixels; ++PixIdx)
    {
        const std::size_t Off = static_cast<std::size_t>(PixIdx) * 6u;
        Result.LinearHdr[PixIdx] = {
            DecodeFixedWordsCpu(FixedAccumWords[Off + 0u], FixedAccumWords[Off + 1u], InvSamples),
            DecodeFixedWordsCpu(FixedAccumWords[Off + 2u], FixedAccumWords[Off + 3u], InvSamples),
            DecodeFixedWordsCpu(FixedAccumWords[Off + 4u], FixedAccumWords[Off + 5u], InvSamples),
        };
    }

    EncodeAcesDisplayBuffer(Result.LinearHdr, Result.DisplayRgba8);
    Result.DeterminismDigest32 = ComputeRgbaDeterminismDigest(Result.DisplayRgba8);

    const auto EndClock        = std::chrono::steady_clock::now();
    Result.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
    return Result;
}

bool DeterministicPathTraceSequence::InitializeVulkanWavefront(
    VulkanInterchange*           Device,
    const std::filesystem::path& CompiledShaderDirectory)
{
    TeardownVulkanWavefront();
    if (Device == nullptr || !Device->Ready())
    {
        return false;
    }
    BoundDevice    = Device;
    VkDevice VkDev = BoundDevice->AccessDevice();

    auto ConstructSetLayout = [&](std::span<const VkDescriptorType> Types) -> VkDescriptorSetLayout
    {
        std::vector<VkDescriptorSetLayoutBinding> Slots(Types.size());
        for (std::size_t I = 0u; I < Types.size(); ++I)
        {
            Slots[I].binding         = static_cast<uint32_t>(I);
            Slots[I].descriptorType  = Types[I];
            Slots[I].descriptorCount = 1u;
            Slots[I].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo Spec {};
        Spec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        Spec.bindingCount = static_cast<uint32_t>(Slots.size());
        Spec.pBindings    = Slots.data();
        VkDescriptorSetLayout Out = VK_NULL_HANDLE;
        vkCreateDescriptorSetLayout(VkDev, &Spec, nullptr, &Out);
        return Out;
    };

    auto ConstructProgramLayout = [&](VkDescriptorSetLayout SetLayout) -> VkPipelineLayout
    {
        VkPipelineLayoutCreateInfo Spec {};
        Spec.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        Spec.setLayoutCount = 1u;
        Spec.pSetLayouts    = &SetLayout;
        VkPipelineLayout Out = VK_NULL_HANDLE;
        vkCreatePipelineLayout(VkDev, &Spec, nullptr, &Out);
        return Out;
    };

    constexpr std::array<VkDescriptorType, 9> PrimaryGtxTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    constexpr std::array<VkDescriptorType, 7> ScatterTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    constexpr std::array<VkDescriptorType, 9> BounceGtxTypes = PrimaryGtxTypes;
    constexpr std::array<VkDescriptorType, 7> FinalizeTypes  = ScatterTypes;

    PrimaryGtxLayout        = ConstructSetLayout(PrimaryGtxTypes);
    ScatterLayout           = ConstructSetLayout(ScatterTypes);
    BounceGtxLayout         = ConstructSetLayout(BounceGtxTypes);
    FinalizeLayout          = ConstructSetLayout(FinalizeTypes);

    PrimaryGtxProgramLayout = ConstructProgramLayout(PrimaryGtxLayout);
    ScatterProgramLayout    = ConstructProgramLayout(ScatterLayout);
    BounceGtxProgramLayout  = ConstructProgramLayout(BounceGtxLayout);
    FinalizeProgramLayout   = ConstructProgramLayout(FinalizeLayout);

    GtxProgramsLoaded =
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontPrimaryTrace_GTX.comp.spv", "WavefrontPrimaryTraceEntry", PrimaryGtxProgramLayout, PrimaryGtxProgram) &&
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontSurfaceScatter.comp.spv",    "WavefrontSurfaceScatterEntry", ScatterProgramLayout, ScatterProgram) &&
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontBounceTrace_GTX.comp.spv",  "WavefrontBounceClosestEntry", BounceGtxProgramLayout, BounceClosestGtxProgram) &&
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontShadowAnyHit_GTX.comp.spv", "WavefrontShadowAnyHitEntry",  BounceGtxProgramLayout, ShadowAnyHitGtxProgram) &&
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontFinalizeSample.comp.spv",   "WavefrontFinalizeSampleEntry", FinalizeProgramLayout, FinalizeProgram) &&
        ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontAtrousStep.comp.spv",       "WavefrontAtrousStepEntry",    FinalizeProgramLayout, AtrousProgram);

    if (!GtxProgramsLoaded)
    {
        TeardownVulkanWavefront();
        return false;
    }

    if (BoundDevice->QueryHardwareRayQuerySupported())
    {
        constexpr std::array<VkDescriptorType, 9> RtxTypes = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        };
        PrimaryRtxLayout        = ConstructSetLayout(RtxTypes);
        BounceRtxLayout         = ConstructSetLayout(RtxTypes);
        PrimaryRtxProgramLayout = ConstructProgramLayout(PrimaryRtxLayout);
        BounceRtxProgramLayout  = ConstructProgramLayout(BounceRtxLayout);

        RtxProgramsLoaded =
            ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontPrimaryTrace_RTX.comp.spv", "WavefrontPrimaryTraceEntry", PrimaryRtxProgramLayout, PrimaryRtxProgram) &&
            ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontBounceTrace_RTX.comp.spv",  "WavefrontBounceClosestEntry", BounceRtxProgramLayout, BounceClosestRtxProgram) &&
            ConstructComputeProgramFromSpirv(*BoundDevice, CompiledShaderDirectory / "WavefrontShadowAnyHit_RTX.comp.spv", "WavefrontShadowAnyHitEntry",  BounceRtxProgramLayout, ShadowAnyHitRtxProgram);
    }

    std::array<VkDescriptorPoolSize, 3> RegistrySizes {};
    RegistrySizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 12u };
    RegistrySizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64u };
    RegistrySizes[2] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4u };

    VkDescriptorPoolCreateInfo RegistrySpec {};
    RegistrySpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    RegistrySpec.maxSets       = 12u;
    RegistrySpec.poolSizeCount = RtxProgramsLoaded ? 3u : 2u;
    RegistrySpec.pPoolSizes    = RegistrySizes.data();
    if (vkCreateDescriptorPool(VkDev, &RegistrySpec, nullptr, &DescriptorRegistry) != VK_SUCCESS)
    {
        TeardownVulkanWavefront();
        return false;
    }

    auto AllocSet = [&](VkDescriptorSetLayout Layout) -> VkDescriptorSet
    {
        VkDescriptorSetAllocateInfo Alloc {};
        Alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        Alloc.descriptorPool     = DescriptorRegistry;
        Alloc.descriptorSetCount = 1u;
        Alloc.pSetLayouts        = &Layout;
        VkDescriptorSet Set = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(VkDev, &Alloc, &Set);
        return Set;
    };

    PrimaryGtxSet   = AllocSet(PrimaryGtxLayout);
    ScatterSet      = AllocSet(ScatterLayout);
    BounceGtxSet    = AllocSet(BounceGtxLayout);
    FinalizeSetPing = AllocSet(FinalizeLayout);
    FinalizeSetPong = AllocSet(FinalizeLayout);
    if (RtxProgramsLoaded)
    {
        PrimaryRtxSet = AllocSet(PrimaryRtxLayout);
        BounceRtxSet  = AllocSet(BounceRtxLayout);
    }

    UniformBuffer       = AllocateHostVisibleWavefrontBuffer(*BoundDevice, sizeof(WavefrontPathTraceUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    QueueCountersBuffer = AllocateHostVisibleWavefrontBuffer(*BoundDevice, sizeof(uint32_t) * 4u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return true;
}

void DeterministicPathTraceSequence::ReleaseSceneBuffers() noexcept
{
    if (BoundDevice == nullptr)
    {
        return;
    }
    VkDevice VkDev = BoundDevice->AccessDevice();
    auto DestroyAccel = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(VkDev, "vkDestroyAccelerationStructureKHR"));
    if (DestroyAccel != nullptr)
    {
        if (TopLevelAccel != VK_NULL_HANDLE)
        {
            DestroyAccel(VkDev, TopLevelAccel, nullptr);
            TopLevelAccel = VK_NULL_HANDLE;
        }
        if (BottomLevelAccel != VK_NULL_HANDLE)
        {
            DestroyAccel(VkDev, BottomLevelAccel, nullptr);
            BottomLevelAccel = VK_NULL_HANDLE;
        }
    }
    BoundDevice->ReleaseBuffer(TlasStorageBuffer);
    BoundDevice->ReleaseBuffer(BlasStorageBuffer);
    BoundDevice->ReleaseBuffer(RtxVertexBuffer);
    BoundDevice->ReleaseBuffer(WideEntryBuffer);
    BoundDevice->ReleaseBuffer(PrimitiveBuffer);
    BoundDevice->ReleaseBuffer(MaterialBuffer);
    BoundDevice->ReleaseBuffer(EmissiveBuffer);
    HardwareAccelReady = false;
}

void DeterministicPathTraceSequence::TeardownVulkanWavefront() noexcept
{
    if (BoundDevice == nullptr)
    {
        return;
    }
    if (BoundDevice->Ready()) { vkDeviceWaitIdle(BoundDevice->AccessDevice()); }
    ReleaseSceneBuffers();
    BoundDevice->ReleaseBuffer(UniformBuffer);
    BoundDevice->ReleaseBuffer(SurfaceHitQueueA);
    BoundDevice->ReleaseBuffer(SurfaceHitQueueB);
    BoundDevice->ReleaseBuffer(BounceRayQueue);
    BoundDevice->ReleaseBuffer(ShadowRayQueue);
    BoundDevice->ReleaseBuffer(QueueCountersBuffer);
    BoundDevice->ReleaseBuffer(FixedAccumBuffer);
    BoundDevice->ReleaseBuffer(PrimaryAlbedoDepthBuffer);
    BoundDevice->ReleaseBuffer(PrimaryNormalBuffer);
    BoundDevice->ReleaseBuffer(DemodPingBuffer);
    BoundDevice->ReleaseBuffer(DemodPongBuffer);
    BoundDevice->ReleaseBuffer(FinalHdrBuffer);

    VkDevice VkDev = BoundDevice->AccessDevice();
    auto DestroyProg = [&](VkPipeline& P) { if (P != VK_NULL_HANDLE) { vkDestroyPipeline(VkDev, P, nullptr); P = VK_NULL_HANDLE; } };
    auto DestroyLay  = [&](VkPipelineLayout& L) { if (L != VK_NULL_HANDLE) { vkDestroyPipelineLayout(VkDev, L, nullptr); L = VK_NULL_HANDLE; } };
    auto DestroySetL = [&](VkDescriptorSetLayout& S) { if (S != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(VkDev, S, nullptr); S = VK_NULL_HANDLE; } };

    DestroyProg(PrimaryGtxProgram);
    DestroyProg(PrimaryRtxProgram);
    DestroyProg(ScatterProgram);
    DestroyProg(BounceClosestGtxProgram);
    DestroyProg(ShadowAnyHitGtxProgram);
    DestroyProg(BounceClosestRtxProgram);
    DestroyProg(ShadowAnyHitRtxProgram);
    DestroyProg(FinalizeProgram);
    DestroyProg(AtrousProgram);

    DestroyLay(PrimaryGtxProgramLayout);
    DestroyLay(PrimaryRtxProgramLayout);
    DestroyLay(ScatterProgramLayout);
    DestroyLay(BounceGtxProgramLayout);
    DestroyLay(BounceRtxProgramLayout);
    DestroyLay(FinalizeProgramLayout);

    DestroySetL(PrimaryGtxLayout);
    DestroySetL(PrimaryRtxLayout);
    DestroySetL(ScatterLayout);
    DestroySetL(BounceGtxLayout);
    DestroySetL(BounceRtxLayout);
    DestroySetL(FinalizeLayout);

    if (DescriptorRegistry != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(VkDev, DescriptorRegistry, nullptr);
        DescriptorRegistry = VK_NULL_HANDLE;
    }

    BoundDevice            = nullptr;
    GtxProgramsLoaded      = false;
    RtxProgramsLoaded      = false;
    HardwareAccelReady     = false;
    AllocatedPixelCapacity = 0u;
}

bool DeterministicPathTraceSequence::BuildHardwareRayQueryAccel(const SpatialAcceleration::WideBoundingTree& SceneTree)
{
    if (!RtxProgramsLoaded || BoundDevice == nullptr || !BoundDevice->QueryHardwareRayQuerySupported())
    {
        return false;
    }
    const auto& Prims = SceneTree.Primitives;
    if (Prims.empty())
    {
        return false;
    }

    VkDevice VkDev = BoundDevice->AccessDevice();
    auto GetBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(VkDev, "vkGetAccelerationStructureBuildSizesKHR"));
    auto CreateAccel = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(VkDev, "vkCreateAccelerationStructureKHR"));
    auto CmdBuildAccel = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(VkDev, "vkCmdBuildAccelerationStructuresKHR"));
    auto GetAccelAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(VkDev, "vkGetAccelerationStructureDeviceAddressKHR"));

    if (!GetBuildSizes || !CreateAccel || !CmdBuildAccel || !GetAccelAddress)
    {
        return false;
    }

    std::vector<float> Vertices(Prims.size() * 9u);
    for (std::size_t I = 0u; I < Prims.size(); ++I)
    {
        const auto& P = Prims[I];
        Vertices[I * 9u + 0u] = P.Corner0.X;
        Vertices[I * 9u + 1u] = P.Corner0.Y;
        Vertices[I * 9u + 2u] = P.Corner0.Z;
        Vertices[I * 9u + 3u] = P.Corner0.X + P.Edge01.X;
        Vertices[I * 9u + 4u] = P.Corner0.Y + P.Edge01.Y;
        Vertices[I * 9u + 5u] = P.Corner0.Z + P.Edge01.Z;
        Vertices[I * 9u + 6u] = P.Corner0.X + P.Edge02.X;
        Vertices[I * 9u + 7u] = P.Corner0.Y + P.Edge02.Y;
        Vertices[I * 9u + 8u] = P.Corner0.Z + P.Edge02.Z;
    }

    RtxVertexBuffer = AllocateHostVisibleWavefrontBuffer(*BoundDevice, 
        Vertices.size() * sizeof(float),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    std::memcpy(RtxVertexBuffer.HostAddress, Vertices.data(), Vertices.size() * sizeof(float));

    VkAccelerationStructureGeometryTrianglesDataKHR TriSpec {};
    TriSpec.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    TriSpec.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    TriSpec.vertexData.deviceAddress = BoundDevice->QueryBufferDeviceAddress(RtxVertexBuffer.BufferHandle);
    TriSpec.vertexStride = sizeof(float) * 3u;
    TriSpec.maxVertex    = static_cast<uint32_t>(Prims.size() * 3u - 1u);
    TriSpec.indexType    = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureGeometryKHR BlasGeom {};
    BlasGeom.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    BlasGeom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    BlasGeom.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;
    BlasGeom.geometry.triangles = TriSpec;

    const uint32_t PrimCount = static_cast<uint32_t>(Prims.size());
    VkAccelerationStructureBuildGeometryInfoKHR BlasBuild {};
    BlasBuild.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    BlasBuild.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    BlasBuild.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    BlasBuild.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BlasBuild.geometryCount = 1u;
    BlasBuild.pGeometries   = &BlasGeom;

    VkAccelerationStructureBuildSizesInfoKHR BlasSizes {};
    BlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    GetBuildSizes(VkDev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &BlasBuild, &PrimCount, &BlasSizes);

    BlasStorageBuffer = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, 
        BlasSizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    DeviceBufferResource BlasScratch = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, 
        std::max<VkDeviceSize>(256u, BlasSizes.buildScratchSize),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR BlasCreate {};
    BlasCreate.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    BlasCreate.buffer = BlasStorageBuffer.BufferHandle;
    BlasCreate.size   = BlasSizes.accelerationStructureSize;
    BlasCreate.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (CreateAccel(VkDev, &BlasCreate, nullptr, &BottomLevelAccel) != VK_SUCCESS)
    {
        BoundDevice->ReleaseBuffer(BlasScratch);
        return false;
    }

    BlasBuild.dstAccelerationStructure  = BottomLevelAccel;
    BlasBuild.scratchData.deviceAddress = BoundDevice->QueryBufferDeviceAddress(BlasScratch.BufferHandle);

    VkAccelerationStructureBuildRangeInfoKHR BlasRange {};
    BlasRange.primitiveCount = PrimCount;
    const VkAccelerationStructureBuildRangeInfoKHR* BlasRangePtr = &BlasRange;

    VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
    CmdBuildAccel(Cmd, 1u, &BlasBuild, &BlasRangePtr);
    SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
    BoundDevice->ReleaseBuffer(BlasScratch);

    VkAccelerationStructureDeviceAddressInfoKHR AddrQuery {};
    AddrQuery.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    AddrQuery.accelerationStructure = BottomLevelAccel;
    const VkDeviceAddress BlasAddress = GetAccelAddress(VkDev, &AddrQuery);

    VkAccelerationStructureInstanceKHR Instance {};
    Instance.transform.matrix[0][0] = 1.0f;
    Instance.transform.matrix[1][1] = 1.0f;
    Instance.transform.matrix[2][2] = 1.0f;
    Instance.mask                   = 0xFFu;
    Instance.flags                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    Instance.accelerationStructureReference = BlasAddress;

    DeviceBufferResource InstBuf = AllocateHostVisibleWavefrontBuffer(*BoundDevice, 
        sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    std::memcpy(InstBuf.HostAddress, &Instance, sizeof(Instance));

    VkAccelerationStructureGeometryInstancesDataKHR InstSpec {};
    InstSpec.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    InstSpec.data.deviceAddress = BoundDevice->QueryBufferDeviceAddress(InstBuf.BufferHandle);

    VkAccelerationStructureGeometryKHR TlasGeom {};
    TlasGeom.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    TlasGeom.geometryType       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    TlasGeom.geometry.instances = InstSpec;

    const uint32_t InstanceCount = 1u;
    VkAccelerationStructureBuildGeometryInfoKHR TlasBuild {};
    TlasBuild.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    TlasBuild.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TlasBuild.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    TlasBuild.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    TlasBuild.geometryCount = 1u;
    TlasBuild.pGeometries   = &TlasGeom;

    VkAccelerationStructureBuildSizesInfoKHR TlasSizes {};
    TlasSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    GetBuildSizes(VkDev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TlasBuild, &InstanceCount, &TlasSizes);

    TlasStorageBuffer = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, 
        TlasSizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    DeviceBufferResource TlasScratch = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, 
        std::max<VkDeviceSize>(256u, TlasSizes.buildScratchSize),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR TlasCreate {};
    TlasCreate.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    TlasCreate.buffer = TlasStorageBuffer.BufferHandle;
    TlasCreate.size   = TlasSizes.accelerationStructureSize;
    TlasCreate.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (CreateAccel(VkDev, &TlasCreate, nullptr, &TopLevelAccel) != VK_SUCCESS)
    {
        BoundDevice->ReleaseBuffer(TlasScratch);
        BoundDevice->ReleaseBuffer(InstBuf);
        return false;
    }

    TlasBuild.dstAccelerationStructure  = TopLevelAccel;
    TlasBuild.scratchData.deviceAddress = BoundDevice->QueryBufferDeviceAddress(TlasScratch.BufferHandle);

    VkAccelerationStructureBuildRangeInfoKHR TlasRange {};
    TlasRange.primitiveCount = 1u;
    const VkAccelerationStructureBuildRangeInfoKHR* TlasRangePtr = &TlasRange;

    Cmd = BoundDevice->BeginSingleTimeCommands();
    CmdBuildAccel(Cmd, 1u, &TlasBuild, &TlasRangePtr);
    SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);

    BoundDevice->ReleaseBuffer(TlasScratch);
    BoundDevice->ReleaseBuffer(InstBuf);
    return true;
}

bool DeterministicPathTraceSequence::SynchronizeSceneGeometry(
    const SpatialAcceleration::WideBoundingTree&  SceneTree,
    const MaterialSpace::CompactMaterialRegister& MaterialRegister,
    std::span<const PolygonalAreaQuadLight>       AreaLights)
{
    if (!GtxProgramsLoaded || BoundDevice == nullptr)
    {
        return false;
    }
    ReleaseSceneBuffers();

    UploadedWideEntryCount = static_cast<uint32_t>(SceneTree.WideEntries.size());
    UploadedPrimitiveCount = static_cast<uint32_t>(SceneTree.Primitives.size());

    std::vector<WavefrontGpuMaterial32>  Materials {};
    std::vector<EmissiveContourTriangle> Emissives {};
    ExtractWavefrontMaterialBuffers(SceneTree, MaterialRegister, AreaLights, Materials, Emissives);
    UploadedMaterialCount = static_cast<uint32_t>(Materials.size());
    UploadedEmissiveCount = static_cast<uint32_t>(Emissives.size());

    auto UploadStorage = [&](const void* SrcBuffer, std::size_t ByteCount) -> DeviceBufferResource
    {
        const std::size_t SafeBytes = std::max<std::size_t>(64u, ByteCount);
        DeviceBufferResource Buf    = AllocateHostVisibleWavefrontBuffer(*BoundDevice, SafeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (ByteCount > 0u && SrcBuffer != nullptr)
        {
            std::memcpy(Buf.HostAddress, SrcBuffer, ByteCount);
        }
        else
        {
            std::memset(Buf.HostAddress, 0, SafeBytes);
        }
        return Buf;
    };

    WideEntryBuffer = UploadStorage(SceneTree.WideEntries.data(), SceneTree.WideEntries.size() * sizeof(SpatialAcceleration::WideTreeEntry));
    PrimitiveBuffer = UploadStorage(SceneTree.Primitives.data(), SceneTree.Primitives.size() * sizeof(SpatialAcceleration::RayTrianglePrimitive));
    MaterialBuffer  = UploadStorage(Materials.data(), Materials.size() * sizeof(WavefrontGpuMaterial32));
    EmissiveBuffer  = UploadStorage(Emissives.data(), Emissives.size() * sizeof(EmissiveContourTriangle));

    HardwareAccelReady = BuildHardwareRayQueryAccel(SceneTree);
    return true;
}

DeterministicPathTraceResult DeterministicPathTraceSequence::ExecuteVulkanWavefrontSequence(
    const DeterministicPathTraceConfiguration& Config,
    const PerspectiveCameraBasis&              Camera,
    const SolarDirectionalSource&              Sun,
    Mathematics::LinearRGB                     SkyRadiance)
{
    const auto StartClock = std::chrono::steady_clock::now();
    DeterministicPathTraceResult Result {};
    Result.WidthPx  = std::max(1u, Camera.ViewportWidthPx);
    Result.HeightPx = std::max(1u, Camera.ViewportHeightPx);
    if (!GtxProgramsLoaded || BoundDevice == nullptr || UploadedPrimitiveCount == 0u)
    {
        return Result;
    }

    const bool UseRtx = (Config.ExecutionTier == PathTraceExecutionTier::RtxHardwareRayQuery && QueryRtxRayQueryReady()) ||
                        (Config.ExecutionTier == PathTraceExecutionTier::AutoSelectOptimal   && QueryRtxRayQueryReady());
    Result.ExecutedTier = UseRtx
        ? PathTraceExecutionTier::RtxHardwareRayQuery
        : PathTraceExecutionTier::GtxWideBvh4Compute;

    const uint32_t TotalPixels  = Result.WidthPx * Result.HeightPx;
    const uint32_t TotalSamples = std::max(1u, Config.SamplesPerPixel);
    const uint32_t BatchSamples = std::max(1u, std::min(TotalSamples, Config.SampleBatchCount));
    const uint32_t MaxBounces   = std::max(1u, Config.MaxBounceDepth);
    const uint32_t MaxQueueCap  = TotalPixels * BatchSamples;

    if (MaxQueueCap > AllocatedPixelCapacity)
    {
        BoundDevice->ReleaseBuffer(SurfaceHitQueueA);
        BoundDevice->ReleaseBuffer(SurfaceHitQueueB);
        BoundDevice->ReleaseBuffer(BounceRayQueue);
        BoundDevice->ReleaseBuffer(ShadowRayQueue);
        BoundDevice->ReleaseBuffer(FixedAccumBuffer);
        BoundDevice->ReleaseBuffer(PrimaryAlbedoDepthBuffer);
        BoundDevice->ReleaseBuffer(PrimaryNormalBuffer);
        BoundDevice->ReleaseBuffer(DemodPingBuffer);
        BoundDevice->ReleaseBuffer(DemodPongBuffer);
        BoundDevice->ReleaseBuffer(FinalHdrBuffer);

        SurfaceHitQueueA         = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(WavefrontSurfaceHit64) * MaxQueueCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        SurfaceHitQueueB         = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(WavefrontSurfaceHit64) * MaxQueueCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        BounceRayQueue           = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(WavefrontBounceRay48)  * MaxQueueCap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        ShadowRayQueue           = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(WavefrontShadowRay48)  * MaxQueueCap * 2u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        FixedAccumBuffer         = AllocateHostVisibleWavefrontBuffer(*BoundDevice, sizeof(uint32_t) * 6u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        PrimaryAlbedoDepthBuffer = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(float) * 4u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        PrimaryNormalBuffer      = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(float) * 4u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        DemodPingBuffer          = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(float) * 4u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        DemodPongBuffer          = AllocateDeviceLocalWavefrontBuffer(*BoundDevice, sizeof(float) * 4u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        FinalHdrBuffer           = AllocateHostVisibleWavefrontBuffer(*BoundDevice, sizeof(float) * 4u * TotalPixels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        AllocatedPixelCapacity   = MaxQueueCap;
    }

    std::memset(FixedAccumBuffer.HostAddress, 0, sizeof(uint32_t) * 6u * TotalPixels);

    const auto CamBasis              = ComputeCameraBasis(Camera);
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.35f, -0.45f, 0.82f });

    WavefrontPathTraceUniforms Ubo {};
    Ubo.CameraOrigin     = Camera.CameraOrigin;
    Ubo.TanHalfVertFov   = std::tan(0.5f * Camera.VerticalFovRadians);
    Ubo.CameraForward    = CamBasis.ForwardAxis;
    Ubo.AspectRatio      = static_cast<float>(Result.WidthPx) / static_cast<float>(Result.HeightPx);
    Ubo.CameraRight      = CamBasis.RightAxis;
    Ubo.ViewportWidth    = Result.WidthPx;
    Ubo.CameraUp         = CamBasis.UpAxis;
    Ubo.ViewportHeight   = Result.HeightPx;
    Ubo.SunDirection     = SunDir;
    Ubo.TanSunRadius     = std::tan(std::max(0.0045f, Sun.SunAngularRadiusRadians));
    Ubo.SolarIrradiance  = Sun.SolarIrradiance;
    Ubo.WideEntryCount   = UploadedWideEntryCount;
    Ubo.SkyRadiance      = SkyRadiance;
    Ubo.PrimitiveCount   = UploadedPrimitiveCount;
    Ubo.MaterialCount    = UploadedMaterialCount;
    Ubo.EmissiveCount    = UploadedEmissiveCount;
    Ubo.TotalSamples     = TotalSamples;
    Ubo.MaxBounceDepth   = MaxBounces;
    Ubo.SharedNetMode    = Config.SharedQuadratureNet ? 1u : 0u;
    Ubo.TilePixelOffset  = 0u;
    Ubo.TilePixelCount   = TotalPixels;

    VkDevice VkDev = BoundDevice->AccessDevice();
    auto WriteBufferDesc = [&](VkDescriptorSet Set, uint32_t SlotIdx, VkDescriptorType Type, VkBuffer Handle)
    {
        VkDescriptorBufferInfo BufSpec { Handle, 0u, VK_WHOLE_SIZE };
        VkWriteDescriptorSet Write {};
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet          = Set;
        Write.dstBinding      = SlotIdx;
        Write.descriptorCount = 1u;
        Write.descriptorType  = Type;
        Write.pBufferInfo     = &BufSpec;
        vkUpdateDescriptorSets(VkDev, 1u, &Write, 0u, nullptr);
    };
    auto WriteTlasDesc = [&](VkDescriptorSet Set, uint32_t SlotIdx)
    {
        VkWriteDescriptorSetAccelerationStructureKHR AsSpec {};
        AsSpec.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        AsSpec.accelerationStructureCount = 1u;
        AsSpec.pAccelerationStructures    = &TopLevelAccel;
        VkWriteDescriptorSet Write {};
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.pNext           = &AsSpec;
        Write.dstSet          = Set;
        Write.dstBinding      = SlotIdx;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(VkDev, 1u, &Write, 0u, nullptr);
    };

    VkDescriptorSet ActivePrimarySet = UseRtx ? PrimaryRtxSet : PrimaryGtxSet;
    VkDescriptorSet ActiveBounceSet  = UseRtx ? BounceRtxSet  : BounceGtxSet;

    WriteBufferDesc(ActivePrimarySet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle);
    if (UseRtx) WriteTlasDesc(ActivePrimarySet, 1u);
    else        WriteBufferDesc(ActivePrimarySet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, WideEntryBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimitiveBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SurfaceHitQueueA.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, QueueCountersBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FixedAccumBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimaryAlbedoDepthBuffer.BufferHandle);
    WriteBufferDesc(ActivePrimarySet, 8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimaryNormalBuffer.BufferHandle);

    auto AssignIterationQueues = [&](VkBuffer InHits, VkBuffer OutHits)
    {
        WriteBufferDesc(ScatterSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle);
        WriteBufferDesc(ScatterSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InHits);
        WriteBufferDesc(ScatterSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialBuffer.BufferHandle);
        WriteBufferDesc(ScatterSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, EmissiveBuffer.BufferHandle);
        WriteBufferDesc(ScatterSet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BounceRayQueue.BufferHandle);
        WriteBufferDesc(ScatterSet, 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadowRayQueue.BufferHandle);
        WriteBufferDesc(ScatterSet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, QueueCountersBuffer.BufferHandle);

        WriteBufferDesc(ActiveBounceSet, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle);
        if (UseRtx) WriteTlasDesc(ActiveBounceSet, 1u);
        else        WriteBufferDesc(ActiveBounceSet, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, WideEntryBuffer.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimitiveBuffer.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaterialBuffer.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BounceRayQueue.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShadowRayQueue.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutHits);
        WriteBufferDesc(ActiveBounceSet, 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, QueueCountersBuffer.BufferHandle);
        WriteBufferDesc(ActiveBounceSet, 8u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FixedAccumBuffer.BufferHandle);
    };

    VkMemoryBarrier ComputeBarrier {};
    ComputeBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    auto* CountersPtr = static_cast<uint32_t*>(QueueCountersBuffer.HostAddress);

    for (uint32_t SampleStart = 0u; SampleStart < TotalSamples; SampleStart += BatchSamples)
    {
        const uint32_t CurBatch = std::min(BatchSamples, TotalSamples - SampleStart);
        Ubo.SampleOffset     = SampleStart;
        Ubo.SampleBatchCount = CurBatch;
        std::memcpy(UniformBuffer.HostAddress, &Ubo, sizeof(Ubo));

        CountersPtr[0] = 0u;
        CountersPtr[1] = 0u;
        CountersPtr[2] = 0u;
        CountersPtr[3] = 0u;

        {
            VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, UseRtx ? PrimaryRtxProgram : PrimaryGtxProgram);
            vkCmdBindDescriptorSets(
                Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                UseRtx ? PrimaryRtxProgramLayout : PrimaryGtxProgramLayout,
                0u, 1u, &ActivePrimarySet, 0u, nullptr);
            vkCmdDispatch(Cmd, (TotalPixels * CurBatch + 63u) / 64u, 1u, 1u);
            SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
        }

        for (uint32_t Depth = 0u; Depth < MaxBounces; ++Depth)
        {
            const uint32_t HitCount = CountersPtr[0];
            if (HitCount == 0u)
            {
                break;
            }
            CountersPtr[1] = 0u;
            CountersPtr[2] = 0u;

            VkBuffer InHits  = (Depth % 2u == 0u) ? SurfaceHitQueueA.BufferHandle : SurfaceHitQueueB.BufferHandle;
            VkBuffer OutHits = (Depth % 2u == 0u) ? SurfaceHitQueueB.BufferHandle : SurfaceHitQueueA.BufferHandle;
            AssignIterationQueues(InHits, OutHits);

            {
                VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ScatterProgram);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ScatterProgramLayout, 0u, 1u, &ScatterSet, 0u, nullptr);
                vkCmdDispatch(Cmd, (HitCount + 63u) / 64u, 1u, 1u);
                SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
            }

            const uint32_t BounceCount = CountersPtr[1];
            const uint32_t ShadowCount = CountersPtr[2];
            CountersPtr[0] = 0u;

            if (ShadowCount > 0u || BounceCount > 0u)
            {
                VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
                VkPipelineLayout BounceLay = UseRtx ? BounceRtxProgramLayout : BounceGtxProgramLayout;
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, BounceLay, 0u, 1u, &ActiveBounceSet, 0u, nullptr);

                if (ShadowCount > 0u)
                {
                    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, UseRtx ? ShadowAnyHitRtxProgram : ShadowAnyHitGtxProgram);
                    vkCmdDispatch(Cmd, (ShadowCount + 63u) / 64u, 1u, 1u);
                    vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &ComputeBarrier, 0u, nullptr, 0u, nullptr);
                }
                if (BounceCount > 0u)
                {
                    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, UseRtx ? BounceClosestRtxProgram : BounceClosestGtxProgram);
                    vkCmdDispatch(Cmd, (BounceCount + 63u) / 64u, 1u, 1u);
                }
                SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
            }
        }
    }

    auto AssignFinalizeSet = [&](VkDescriptorSet Set, VkBuffer InDemod, VkBuffer OutDemod)
    {
        WriteBufferDesc(Set, 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffer.BufferHandle);
        WriteBufferDesc(Set, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FixedAccumBuffer.BufferHandle);
        WriteBufferDesc(Set, 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimaryAlbedoDepthBuffer.BufferHandle);
        WriteBufferDesc(Set, 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PrimaryNormalBuffer.BufferHandle);
        WriteBufferDesc(Set, 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, InDemod);
        WriteBufferDesc(Set, 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, OutDemod);
        WriteBufferDesc(Set, 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FinalHdrBuffer.BufferHandle);
    };
    AssignFinalizeSet(FinalizeSetPing, DemodPongBuffer.BufferHandle, DemodPingBuffer.BufferHandle);
    AssignFinalizeSet(FinalizeSetPong, DemodPingBuffer.BufferHandle, DemodPongBuffer.BufferHandle);

    const uint32_t GroupsX = (Result.WidthPx  + 7u) / 8u;
    const uint32_t GroupsY = (Result.HeightPx + 7u) / 8u;
    {
        VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, FinalizeProgram);
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, FinalizeProgramLayout, 0u, 1u, &FinalizeSetPing, 0u, nullptr);
        vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
        SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
    }

    if (Config.EnableAtrousReconstruction)
    {
        constexpr std::array<uint32_t, 3> StepWidths = { 1u, 2u, 4u };
        for (std::size_t Step = 0u; Step < StepWidths.size(); ++Step)
        {
            Ubo.AtrousStepWidth = StepWidths[Step];
            std::memcpy(UniformBuffer.HostAddress, &Ubo, sizeof(Ubo));
            VkDescriptorSet StepSet = (Step % 2u == 0u) ? FinalizeSetPong : FinalizeSetPing;
            VkCommandBuffer Cmd = BoundDevice->BeginSingleTimeCommands();
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, AtrousProgram);
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, FinalizeProgramLayout, 0u, 1u, &StepSet, 0u, nullptr);
            vkCmdDispatch(Cmd, GroupsX, GroupsY, 1u);
            SubmitWavefrontSingleTimeCommands(*BoundDevice, Cmd);
        }
    }

    Result.LinearHdr.resize(TotalPixels);
    const auto* HdrPtr = static_cast<const float*>(FinalHdrBuffer.HostAddress);
    for (uint32_t PixIdx = 0u; PixIdx < TotalPixels; ++PixIdx)
    {
        Result.LinearHdr[PixIdx] = {
            HdrPtr[PixIdx * 4u + 0u],
            HdrPtr[PixIdx * 4u + 1u],
            HdrPtr[PixIdx * 4u + 2u],
        };
    }

    EncodeAcesDisplayBuffer(Result.LinearHdr, Result.DisplayRgba8);
    Result.DeterminismDigest32 = ComputeRgbaDeterminismDigest(Result.DisplayRgba8);

    const auto EndClock        = std::chrono::steady_clock::now();
    Result.ElapsedMilliseconds = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
    return Result;
}

} // namespace Frontier::GeometricRaster
