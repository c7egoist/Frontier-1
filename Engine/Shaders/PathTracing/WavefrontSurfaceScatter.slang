/*============================================================================================================================================
                                                       WAVEFRONTSURFACESCATTER.SLANG
============================================================================================================================================*/
// 🧩 Wavefront Queue 2 (ShadeScatter): Zero-BVH Low-VGPR OpenPBR GGX VNDF + MIS/NEE Solar & Emissive Light Sampling

import DeterministicSampling;

[[vk::binding(0, 0)]] ConstantBuffer<GpuWavefrontUniforms>          PtUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWavefrontSurfaceHit64>    InSurfaceHits;
[[vk::binding(2, 0)]] StructuredBuffer<GpuWavefrontMaterial32>      Materials;
[[vk::binding(3, 0)]] StructuredBuffer<GpuWavefrontEmissiveTri>     EmissiveTris;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuWavefrontBounceRay48>   OutBounceRays;
[[vk::binding(5, 0)]] RWStructuredBuffer<GpuWavefrontShadowRay48>   OutShadowRays;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>                      QueueCounters;

float EvaluateGgxD(float NdotH, float Alpha)
{
    float A2    = Alpha * Alpha;
    float Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
    return A2 / max(1.0e-7, 3.14159265 * Denom * Denom);
}

float EvaluateSmithG1(float NdotX, float Alpha)
{
    float A2 = Alpha * Alpha;
    return (2.0 * NdotX) / max(1.0e-6, NdotX + sqrt(A2 + (1.0 - A2) * NdotX * NdotX));
}

float3 EvaluateSurfaceBsdf(
    GpuWavefrontMaterial32 Mat,
    float3                 UnitN,
    float3                 UnitV,
    float3                 UnitL,
    out float              OutBsdfPdf)
{
    OutBsdfPdf  = 0.0;
    float NdotL = dot(UnitN, UnitL);
    float NdotV = max(1.0e-4, dot(UnitN, UnitV));
    if (NdotL <= 1.0e-5) return float3(0.0);

    float Alpha = clamp(Mat.RoughnessLinear * Mat.RoughnessLinear, 0.004, 1.0);
    float3 Half = normalize(UnitV + UnitL);
    float NdotH = saturate(dot(UnitN, Half));
    float VdotH = saturate(dot(UnitV, Half));

    float3 F0      = lerp(float3(0.04), Mat.BaseAlbedo, Mat.MetallicWeight);
    float  Schlick = pow(1.0 - VdotH, 5.0);
    float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;

    float D     = EvaluateGgxD(NdotH, Alpha);
    float G1V   = EvaluateSmithG1(NdotV, Alpha);
    float G1L   = EvaluateSmithG1(NdotL, Alpha);
    float3 Spec = Fresnel * ((D * G1V * G1L) / max(1.0e-5, 4.0 * NdotV * NdotL));

    float3 DiffAlbedo = Mat.BaseAlbedo * (1.0 - Mat.MetallicWeight);
    float3 Diff       = DiffAlbedo * ((float3(1.0) - Fresnel) * 0.318309886);

    float SpecProb = clamp(0.25 + 0.65 * Mat.MetallicWeight, 0.20, 0.90);
    float PdfSpec  = (D * G1V) / max(1.0e-5, 4.0 * NdotV);
    float PdfDiff  = NdotL * 0.318309886;
    OutBsdfPdf     = lerp(PdfDiff, PdfSpec, SpecProb);
    return Diff + Spec;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void WavefrontSurfaceScatterEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint HitIdx   = DispatchThreadID.x;
    uint HitCount = QueueCounters[0u];
    if (HitIdx >= HitCount) return;

    GpuWavefrontSurfaceHit64 Hit = InSurfaceHits[HitIdx];
    uint SampleIdx               = Hit.SampleBounceKey & 0xFFFFu;
    uint BounceDepth             = Hit.SampleBounceKey >> 16u;
    uint PX                      = Hit.PixelLinearIndex % PtUniforms.ViewportWidth;
    uint PY                      = Hit.PixelLinearIndex / PtUniforms.ViewportWidth;
    uint PixelKey                = (PtUniforms.SharedNetMode != 0u)
        ? 0u
        : AvalancheKey32((PY << 16u) ^ PX ^ 0xA341316Cu);

    uint   MatIdx = min(Hit.MaterialOrdinal, max(1u, PtUniforms.MaterialCount) - 1u);
    GpuWavefrontMaterial32 Mat = Materials[MatIdx];
    float3 UnitN  = Hit.GeometricNormal;
    float3 UnitV  = -Hit.IncomingRayDir;
    if (dot(UnitN, UnitV) < 0.0) UnitN = -UnitN;

    float3 TanU, BitV;
    ConstructOrthonormalBasis(UnitN, TanU, BitV);

    uint BasePair = 1u + BounceDepth * 4u;

    // 1. Next-Event Estimation (NEE): Stratified Solar Disc Shadow Ray
    if (any(PtUniforms.SolarIrradiance > float3(1.0e-5)))
    {
        float2 XiSun = Sobol02Sample(SampleIdx, PixelKey, BasePair + 0u);
        float3 SunU, SunV;
        ConstructOrthonormalBasis(PtUniforms.SunDirection, SunU, SunV);
        float  Rad   = sqrt(XiSun.x) * PtUniforms.TanSunRadius;
        float  Ang   = XiSun.y * 6.28318530718;
        float3 SunL  = normalize(PtUniforms.SunDirection + SunU * (Rad * cos(Ang)) + SunV * (Rad * sin(Ang)));
        float  NdotS = dot(UnitN, SunL);
        if (NdotS > 1.0e-4)
        {
            float BsdfPdf = 0.0;
            float3 Bsdf   = EvaluateSurfaceBsdf(Mat, UnitN, UnitV, SunL, BsdfPdf);
            float3 Contrib = Hit.PathThroughput * Bsdf * PtUniforms.SolarIrradiance * NdotS;
            if (any(Contrib > float3(1.0e-6)))
            {
                uint ShSlot = 0u;
                InterlockedAdd(QueueCounters[2u], 1u, ShSlot);
                GpuWavefrontShadowRay48 ShRay;
                ShRay.OriginWorld       = Hit.HitPositionWorld + UnitN * 2.0e-3;
                ShRay.PixelLinearIndex  = Hit.PixelLinearIndex;
                ShRay.UnitDirection     = SunL;
                ShRay.MaxDistanceM      = 300.0;
                ShRay.UnoccludedContrib = Contrib;
                ShRay.PadTail           = 0.0;
                OutShadowRays[ShSlot]   = ShRay;
            }
        }
    }

    // 2. Next-Event Estimation (NEE): Flux-Stratified Emissive Triangle + Power-Heuristic MIS
    if (PtUniforms.EmissiveCount > 0u)
    {
        float2 XiEmit = Sobol02Sample(SampleIdx, PixelKey, BasePair + 1u);
        float2 XiTri  = Sobol02Sample(SampleIdx, PixelKey, BasePair + 2u);

        uint ChosenIdx = 0u;
        uint Lo = 0u, Hi = PtUniforms.EmissiveCount - 1u;
        while (Lo < Hi)
        {
            uint Mid = (Lo + Hi) >> 1u;
            if (EmissiveTris[Mid].CdfUpper < XiEmit.x) Lo = Mid + 1u;
            else Hi = Mid;
        }
        ChosenIdx = Lo;

        GpuWavefrontEmissiveTri EmitTri = EmissiveTris[ChosenIdx];
        float PrevCdf = (ChosenIdx > 0u) ? EmissiveTris[ChosenIdx - 1u].CdfUpper : 0.0;
        float PickPm  = max(1.0e-6, EmitTri.CdfUpper - PrevCdf);

        float SqrtU  = sqrt(XiTri.x);
        float B0     = 1.0 - SqrtU;
        float B1     = SqrtU * (1.0 - XiTri.y);
        float B2     = SqrtU * XiTri.y;
        float3 EmitP = EmitTri.VertexA * B0 + EmitTri.VertexB * B1 + EmitTri.VertexC * B2;

        float3 ToEmit = EmitP - Hit.HitPositionWorld;
        float  DistSq = dot(ToEmit, ToEmit);
        float  Dist   = sqrt(max(1.0e-6, DistSq));
        float3 UnitL  = ToEmit / Dist;
        float  CosRec = dot(UnitN, UnitL);
        float  CosEm  = (EmitTri.TwoSidedEmit != 0u) ? abs(dot(EmitTri.UnitNormal, -UnitL))
                                                     : dot(EmitTri.UnitNormal, -UnitL);
        if (CosRec > 1.0e-4 && CosEm > 1.0e-4 && Dist > 0.015)
        {
            float LightPdf = (PickPm * DistSq) / max(1.0e-6, EmitTri.AreaM2 * CosEm);
            float BsdfPdf  = 0.0;
            float3 Bsdf    = EvaluateSurfaceBsdf(Mat, UnitN, UnitV, UnitL, BsdfPdf);
            float MisW     = (LightPdf * LightPdf) / max(1.0e-8, LightPdf * LightPdf + BsdfPdf * BsdfPdf);
            float3 Contrib = Hit.PathThroughput * Bsdf * EmitTri.EmittedRad * ((CosRec * MisW) / LightPdf);
            if (any(Contrib > float3(1.0e-6)))
            {
                uint ShSlot = 0u;
                InterlockedAdd(QueueCounters[2u], 1u, ShSlot);
                GpuWavefrontShadowRay48 ShRay;
                ShRay.OriginWorld       = Hit.HitPositionWorld + UnitN * 2.0e-3;
                ShRay.PixelLinearIndex  = Hit.PixelLinearIndex;
                ShRay.UnitDirection     = UnitL;
                ShRay.MaxDistanceM      = max(0.01, Dist - 4.0e-3);
                ShRay.UnoccludedContrib = min(Contrib, float3(64.0));
                ShRay.PadTail           = 0.0;
                OutShadowRays[ShSlot]   = ShRay;
            }
        }
    }

    // 3. Continuation BSDF Bounce Sampling (Dupuy-Benyoub 2023 Spherical-Cap GGX VNDF + Cosine Diffuse)
    if (BounceDepth + 1u >= PtUniforms.MaxBounceDepth) return;

    float2 XiBsdf = Sobol02Sample(SampleIdx, PixelKey, BasePair + 3u);
    float SpecProb = clamp(0.25 + 0.65 * Mat.MetallicWeight, 0.20, 0.90);
    float Alpha    = clamp(Mat.RoughnessLinear * Mat.RoughnessLinear, 0.004, 1.0);

    float3 SampledL;
    if (XiBsdf.x < SpecProb)
    {
        float2 RescaledXi = float2(XiBsdf.x / SpecProb, XiBsdf.y);
        float3 LocalV     = float3(dot(UnitV, TanU), dot(UnitV, BitV), max(1.0e-4, dot(UnitV, UnitN)));
        float3 LocalH     = SampleSphericalCapGgxVndfLocal(LocalV, Alpha, RescaledXi);
        float3 WorldH     = normalize(TanU * LocalH.x + BitV * LocalH.y + UnitN * LocalH.z);
        SampledL          = reflect(-UnitV, WorldH);
    }
    else
    {
        float2 RescaledXi = float2((XiBsdf.x - SpecProb) / max(1.0e-5, 1.0 - SpecProb), XiBsdf.y);
        float  SinT       = sqrt(RescaledXi.x);
        float  CosT       = sqrt(max(0.0, 1.0 - RescaledXi.x));
        float  Phi        = 6.28318530718 * RescaledXi.y;
        SampledL          = normalize(TanU * (SinT * cos(Phi)) + BitV * (SinT * sin(Phi)) + UnitN * CosT);
    }

    float NdotB = dot(UnitN, SampledL);
    if (NdotB <= 1.0e-4) return;

    float  BouncePdf  = 0.0;
    float3 BounceBsdf = EvaluateSurfaceBsdf(Mat, UnitN, UnitV, SampledL, BouncePdf);
    if (BouncePdf <= 1.0e-5) return;

    float3 NextThroughput = Hit.PathThroughput * BounceBsdf * (NdotB / BouncePdf);
    NextThroughput        = min(NextThroughput, float3(12.0));
    if (max(NextThroughput.x, max(NextThroughput.y, NextThroughput.z)) <= 1.0e-4) return;

    uint BounceSlot = 0u;
    InterlockedAdd(QueueCounters[1u], 1u, BounceSlot);
    GpuWavefrontBounceRay48 NextRay;
    NextRay.OriginWorld      = Hit.HitPositionWorld + UnitN * 2.0e-3;
    NextRay.PixelLinearIndex = Hit.PixelLinearIndex;
    NextRay.UnitDirection    = SampledL;
    NextRay.SampleBounceKey  = (SampleIdx & 0xFFFFu) | ((BounceDepth + 1u) << 16u);
    NextRay.PathThroughput   = NextThroughput;
    NextRay.PreviousBsdfPdf  = BouncePdf;
    OutBounceRays[BounceSlot] = NextRay;
}
