/*============================================================================================================================================
                                                        WAVEFRONTPRIMARYTRACE.SLANG
============================================================================================================================================*/
// 🧩 Wavefront Queue 1: Coherent 8x8 Tile Primary Owen-Sobol Ray Generation + Closest-Hit Trace (GTX BVH4 Compute & RTX RayQuery)

import WideTreeRayQuery;
import DeterministicSampling;

#ifndef FRONTIER_USE_HW_RAY_QUERY
#define FRONTIER_USE_HW_RAY_QUERY 0
#endif

[[vk::binding(0, 0)]] ConstantBuffer<GpuWavefrontUniforms>           PtUniforms;
#if FRONTIER_USE_HW_RAY_QUERY == 1
[[vk::binding(1, 0)]] RaytracingAccelerationStructure                TopLevelAccel;
#else
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>          WideEntries;
#endif
[[vk::binding(2, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>    Primitives;
[[vk::binding(3, 0)]] StructuredBuffer<GpuWavefrontMaterial32>       Materials;
[[vk::binding(4, 0)]] RWStructuredBuffer<GpuWavefrontSurfaceHit64>   OutSurfaceHits;
[[vk::binding(5, 0)]] RWStructuredBuffer<uint>                       QueueCounters;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>                       FixedAccumWords;
[[vk::binding(7, 0)]] RWStructuredBuffer<float4>                     PrimaryAlbedoDepth;
[[vk::binding(8, 0)]] RWStructuredBuffer<float4>                     PrimaryNormalBuffer;

GpuRayHitSummary TracePrimarySceneClosest(float3 Origin, float3 UnitDir, float MinT, float MaxT)
{
#if FRONTIER_USE_HW_RAY_QUERY == 1
    GpuRayHitSummary Res;
    Res.HitConfirmed      = false;
    Res.HitDistanceMeters = MaxT;
    Res.BarycentricsUV    = float2(0.0);
    Res.MaterialOrdinal   = 0u;
    Res.GeometricNormal   = float3(0.0, 0.0, 1.0);
    Res.SourceFaceIndex   = 0xFFFFFFFFu;
    Res.HitFrontFace      = true;

    RayDesc Desc;
    Desc.Origin    = Origin;
    Desc.Direction = UnitDir;
    Desc.TMin      = MinT;
    Desc.TMax      = MaxT;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> Q;
    Q.TraceRayInline(TopLevelAccel, RAY_FLAG_FORCE_OPAQUE, 0xFF, Desc);
    while (Q.Proceed()) {}
    if (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        uint PrimIdx = Q.CommittedPrimitiveIndex();
        GpuRayTrianglePrimitive48 Prim = Primitives[PrimIdx];
        float3 RawCross = cross(Prim.Edge01, Prim.Edge02);
        float  CrossLen = length(RawCross);
        float3 GeomN    = (CrossLen > 1.0e-12) ? (RawCross / CrossLen) : float3(0.0, 0.0, 1.0);
        Res.HitConfirmed      = true;
        Res.HitDistanceMeters = Q.CommittedRayT();
        Res.BarycentricsUV    = Q.CommittedTriangleBarycentrics();
        Res.MaterialOrdinal   = Prim.PackedMaterialOpaque & 0xFFFFu;
        Res.GeometricNormal   = GeomN;
        Res.SourceFaceIndex   = Prim.SourceFaceIndex;
        Res.HitFrontFace      = (dot(UnitDir, GeomN) < 0.0);
    }
    return Res;
#else
    return TraceWideBoundingTreeClosest(
        WideEntries, Primitives, PtUniforms.WideEntryCount, Origin, UnitDir, MinT, MaxT);
#endif
}

float3 EvaluateAnalyticSkyEnvironment(float3 UnitDir)
{
    float HorizonT = saturate(UnitDir.z * 0.5 + 0.5);
    float3 Zenith  = PtUniforms.SkyRadiance * 1.25;
    float3 Horizon = PtUniforms.SkyRadiance * float3(1.18, 1.06, 0.92) * 0.78;
    float3 Ground  = PtUniforms.SkyRadiance * 0.22;
    return (UnitDir.z >= 0.0) ? lerp(Horizon, Zenith, pow(saturate(UnitDir.z), 0.65))
                              : lerp(Ground, Horizon, HorizonT * 2.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void WavefrontPrimaryTraceEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint WorkIdx = DispatchThreadID.x;
    uint TotalWork = PtUniforms.TilePixelCount * PtUniforms.SampleBatchCount;
    if (WorkIdx >= TotalWork) return;

    uint LocalPix  = WorkIdx / PtUniforms.SampleBatchCount;
    uint LocalSmp  = WorkIdx % PtUniforms.SampleBatchCount;
    uint SampleIdx = PtUniforms.SampleOffset + LocalSmp;
    uint PixIdx    = PtUniforms.TilePixelOffset + LocalPix;
    uint TotalPix  = PtUniforms.ViewportWidth * PtUniforms.ViewportHeight;
    if (PixIdx >= TotalPix) return;

    uint PX = PixIdx % PtUniforms.ViewportWidth;
    uint PY = PixIdx / PtUniforms.ViewportWidth;

    uint PixelKey = (PtUniforms.SharedNetMode != 0u)
        ? 0u
        : AvalancheKey32((PY << 16u) ^ PX ^ 0xA341316Cu);
    float2 SubJitter = Sobol02Sample(SampleIdx, PixelKey, 0u);

    float U = ((float(PX) + SubJitter.x) / float(PtUniforms.ViewportWidth)) * 2.0 - 1.0;
    float V = 1.0 - ((float(PY) + SubJitter.y) / float(PtUniforms.ViewportHeight)) * 2.0;

    float3 RayDir = normalize(
        PtUniforms.CameraForward +
        PtUniforms.CameraRight * (U * PtUniforms.AspectRatio * PtUniforms.TanHalfVertFov) +
        PtUniforms.CameraUp    * (V * PtUniforms.TanHalfVertFov));

    GpuRayHitSummary Hit = TracePrimarySceneClosest(PtUniforms.CameraOrigin, RayDir, 1.0e-3, 500.0);
    if (!Hit.HitConfirmed)
    {
        float3 SkyRad = EvaluateAnalyticSkyEnvironment(RayDir);
        float  CosSun = dot(RayDir, PtUniforms.SunDirection);
        float  CosLim = cos(max(0.0045, PtUniforms.TanSunRadius));
        if (CosSun >= CosLim)
        {
            float SolidAngle = max(1.0e-5, 3.14159265 * PtUniforms.TanSunRadius * PtUniforms.TanSunRadius);
            SkyRad += min(float3(64.0), PtUniforms.SolarIrradiance / SolidAngle);
        }
        AccumulateDeterministicRadiance(FixedAccumWords, PixIdx, SkyRad);
        if (SampleIdx == 0u)
        {
            PrimaryAlbedoDepth[PixIdx]  = float4(1.0, 1.0, 1.0, 1.0e4);
            PrimaryNormalBuffer[PixIdx] = float4(-RayDir, 0.0);
        }
        return;
    }

    float3 OrientedN = Hit.HitFrontFace ? Hit.GeometricNormal : (-Hit.GeometricNormal);
    uint   MatIdx    = min(Hit.MaterialOrdinal, max(1u, PtUniforms.MaterialCount) - 1u);
    GpuWavefrontMaterial32 Mat = Materials[MatIdx];

    if (SampleIdx == 0u)
    {
        PrimaryAlbedoDepth[PixIdx]  = float4(max(float3(0.04), Mat.BaseAlbedo), Hit.HitDistanceMeters);
        PrimaryNormalBuffer[PixIdx] = float4(OrientedN, 1.0);
    }

    if (any(Mat.EmittedRadiance > float3(1.0e-5)))
    {
        AccumulateDeterministicRadiance(FixedAccumWords, PixIdx, Mat.EmittedRadiance);
    }

    uint Slot = 0u;
    InterlockedAdd(QueueCounters[0u], 1u, Slot);
    GpuWavefrontSurfaceHit64 Item;
    Item.HitPositionWorld = PtUniforms.CameraOrigin + RayDir * Hit.HitDistanceMeters;
    Item.PixelLinearIndex = PixIdx;
    Item.GeometricNormal  = OrientedN;
    Item.MaterialOrdinal  = MatIdx;
    Item.IncomingRayDir   = RayDir;
    Item.SampleBounceKey  = (SampleIdx & 0xFFFFu);
    Item.PathThroughput   = float3(1.0, 1.0, 1.0);
    Item.HitDistanceM     = Hit.HitDistanceMeters;
    OutSurfaceHits[Slot]  = Item;
}
