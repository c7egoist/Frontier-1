/*============================================================================================================================================
                                                           CONTACTCLIPSHADOW.SLANG
============================================================================================================================================*/
// 🧩 Shadow #1 (ContactPcss): 2-Cascade (3.91 mm / 15.6 mm) Orthographic PCSS + 0.28 m BVH4 Contact Gate + Live Tapered Capsules (Zero 3D SDF)

module ContactClipShadow;

import WideTreeRayQuery;

struct ContactClipUniforms
{
    float3 FineCascadeCenter;
    float  FineHalfSpanMeters;
    float3 CoarseCascadeCenter;
    float  CoarseHalfSpanMeters;
    float3 LightRight;
    float  DepthExtentMeters;
    float3 LightUp;
    float  TanSunAngularRadius;
    float3 LightForward;
    uint   CascadeResolution;
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   WideEntryCount;
    uint   CapsuleCount;
};

[[vk::binding(0, 0)]] ConstantBuffer<ContactClipUniforms>          ClipUniforms;
[[vk::binding(1, 0)]] Texture2D<float>                             FineCascadeDepth;
[[vk::binding(2, 0)]] Texture2D<float>                             CoarseCascadeDepth;
[[vk::binding(3, 0)]] StructuredBuffer<GpuWideTreeEntry128>        SceneWideTree;
[[vk::binding(4, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>  ScenePrimitives;
[[vk::binding(5, 0)]] StructuredBuffer<GpuTaperedCapsule32>        DeformingCapsules;
[[vk::binding(6, 0)]] Texture2D<float4>                            WorldPositionTex;
[[vk::binding(7, 0)]] Texture2D<float4>                            WorldNormalTex;
[[vk::binding(8, 0)]] RWTexture2D<float2>                          OutShadowBlocker;

float2 SampleCascadePcss16(
    Texture2D<float> CascadeTex,
    float3           BiasedWorldPos,
    float3           CascadeCenter,
    float            HalfSpanM,
    float            SlopeScale,
    float            BaseRotRad,
    out bool         OutInside)
{
    OutInside = false;
    float3 Rel    = BiasedWorldPos - CascadeCenter;
    float  LocalU = dot(Rel, ClipUniforms.LightRight);
    float  LocalV = dot(Rel, ClipUniforms.LightUp);
    float  LocalZ = dot(Rel, ClipUniforms.LightForward);
    if (abs(LocalU) > HalfSpanM * 0.96 || abs(LocalV) > HalfSpanM * 0.96)
    {
        return float2(1.0, 0.0);
    }

    OutInside      = true;
    float ResF     = float(ClipUniforms.CascadeResolution);
    float InvSpan2 = 1.0 / max(1.0e-4, 2.0 * HalfSpanM);
    float TexelM   = (2.0 * HalfSpanM) / ResF;
    float2 TexC    = float2((LocalU + HalfSpanM) * InvSpan2 * ResF,
                            (HalfSpanM - LocalV) * InvSpan2 * ResF);
    float RecvD    = LocalZ + ClipUniforms.DepthExtentMeters;
    float BiasM    = TexelM * (1.45 + 0.65 * clamp(SlopeScale, 0.0, 3.0));

    // Step 1: 8-tap Vogel blocker search
    float BlkSum = 0.0;
    uint  BlkCnt = 0u;
    float SearchTexels = clamp((0.65 * ClipUniforms.TanSunAngularRadius) / max(1.0e-4, TexelM), 1.5, 14.0);
    [unroll]
    for (uint Tap = 0u; Tap < 8u; ++Tap)
    {
        float Rad = sqrt((float(Tap) + 0.5) / 8.0) * SearchTexels;
        float Ang = float(Tap) * 2.39996323 + BaseRotRad;
        int2  TC  = clamp(int2(TexC + float2(cos(Ang), sin(Ang)) * Rad), int2(0), int2(int(ClipUniforms.CascadeResolution) - 1));
        float Df  = RecvD - CascadeTex.Load(int3(TC, 0));
        if (Df > BiasM)
        {
            BlkSum += Df;
            ++BlkCnt;
        }
    }

    if (BlkCnt == 0u)
    {
        return float2(1.0, 0.0);
    }

    float AvgBlockerM   = BlkSum / float(BlkCnt);
    float PenumbraTexel = clamp((AvgBlockerM * ClipUniforms.TanSunAngularRadius) / max(1.0e-4, TexelM), 0.85, 18.0);

    // Step 2: 16-tap Vogel PCSS filter
    float LitSum = 0.0;
    [unroll]
    for (uint Tap = 0u; Tap < 16u; ++Tap)
    {
        float Rad = sqrt((float(Tap) + 0.5) / 16.0) * PenumbraTexel;
        float Ang = float(Tap) * 2.39996323 + BaseRotRad;
        int2  TC  = clamp(int2(TexC + float2(cos(Ang), sin(Ang)) * Rad), int2(0), int2(int(ClipUniforms.CascadeResolution) - 1));
        float Df  = RecvD - CascadeTex.Load(int3(TC, 0));
        if (Df <= BiasM + Rad * TexelM * 0.20)
        {
            LitSum += 1.0;
        }
    }

    return float2(LitSum * 0.0625, AvgBlockerM);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ContactClipComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= ClipUniforms.ViewportWidth || DispatchThreadID.y >= ClipUniforms.ViewportHeight)
    {
        return;
    }

    float4 PosSample = WorldPositionTex.Load(int3(DispatchThreadID.xy, 0));
    float3 Nrm       = normalize(WorldNormalTex.Load(int3(DispatchThreadID.xy, 0)).xyz);
    if (PosSample.w <= 0.0)
    {
        OutShadowBlocker[DispatchThreadID.xy] = float2(1.0, 0.0);
        return;
    }

    float3 SunDir = -ClipUniforms.LightForward;
    float  NdotL  = dot(Nrm, SunDir);
    if (NdotL <= 1.0e-5)
    {
        OutShadowBlocker[DispatchThreadID.xy] = float2(0.0, 0.0);
        return;
    }

    float IgnDot  = 0.06711056 * float(DispatchThreadID.x) + 0.00583715 * float(DispatchThreadID.y);
    float BaseRot = frac(52.9829189 * frac(IgnDot)) * 6.28318530718;
    float SlopeS  = sqrt(max(0.0, 1.0 - NdotL * NdotL)) / max(0.15, NdotL);
    float3 Biased = PosSample.xyz + Nrm * 3.5e-3;

    bool   Inside  = false;
    float2 ClipRes = SampleCascadePcss16(
        FineCascadeDepth, Biased, ClipUniforms.FineCascadeCenter, ClipUniforms.FineHalfSpanMeters, SlopeS, BaseRot, Inside);
    if (!Inside)
    {
        ClipRes = SampleCascadePcss16(
            CoarseCascadeDepth, Biased, ClipUniforms.CoarseCascadeCenter, ClipUniforms.CoarseHalfSpanMeters, SlopeS, BaseRot, Inside);
    }

    float FinalVis = Inside ? ClipRes.x : 1.0;
    float BlockerD = ClipRes.y;

    // 0.28 m exact BVH4 contact shadow gate
    GpuRayHitSummary ContactHit = TraceWideBoundingTreeClosest(
        SceneWideTree, ScenePrimitives, ClipUniforms.WideEntryCount, PosSample.xyz + Nrm * 1.8e-3, SunDir, 1.0e-3, 0.28);
    if (ContactHit.HitConfirmed)
    {
        float Fade = saturate(ContactHit.HitDistanceMeters / 0.28);
        FinalVis   = min(FinalVis, Fade * Fade);
        BlockerD   = (BlockerD > 0.0) ? min(BlockerD, ContactHit.HitDistanceMeters) : ContactHit.HitDistanceMeters;
    }
    else if (!Inside)
    {
        bool FarBlocked = TraceWideBoundingTreeAnyHit(
            SceneWideTree, ScenePrimitives, ClipUniforms.WideEntryCount, PosSample.xyz + Nrm * 2.0e-3, SunDir, 1.0e-3, 150.0);
        FinalVis = FarBlocked ? 0.0 : 1.0;
    }

    // Live deforming character bone capsules
    if (ClipUniforms.CapsuleCount > 0u && FinalVis > 1.0e-3)
    {
        float CapVis = EvaluateCapsuleRigConeVisibility(
            DeformingCapsules, ClipUniforms.CapsuleCount, PosSample.xyz, Nrm, SunDir, ClipUniforms.TanSunAngularRadius, 6.5);
        if (CapVis < FinalVis)
        {
            FinalVis = CapVis;
            if (BlockerD <= 0.0) BlockerD = 0.85;
        }
    }

    OutShadowBlocker[DispatchThreadID.xy] = float2(FinalVis, BlockerD);
}
