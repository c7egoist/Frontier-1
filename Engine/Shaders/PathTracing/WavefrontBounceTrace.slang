/*============================================================================================================================================
                                                        WAVEFRONTBOUNCETRACE.SLANG
============================================================================================================================================*/
// 🧩 Wavefront Queue 3 & 4: Compacted Continuation Closest-Hit & Direct-Descent NEE Shadow Trace (GTX BVH4 Compute & RTX RayQuery)

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
[[vk::binding(4, 0)]] StructuredBuffer<GpuWavefrontBounceRay48>      InBounceRays;
[[vk::binding(5, 0)]] StructuredBuffer<GpuWavefrontShadowRay48>      InShadowRays;
[[vk::binding(6, 0)]] RWStructuredBuffer<GpuWavefrontSurfaceHit64>   OutSurfaceHits;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint>                       QueueCounters;
[[vk::binding(8, 0)]] RWStructuredBuffer<uint>                       FixedAccumWords;

GpuRayHitSummary TraceBounceClosest(float3 Origin, float3 UnitDir, float MinT, float MaxT)
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

bool TraceShadowOccluded(float3 Origin, float3 UnitDir, float MinT, float MaxT)
{
#if FRONTIER_USE_HW_RAY_QUERY == 1
    RayDesc Desc;
    Desc.Origin    = Origin;
    Desc.Direction = UnitDir;
    Desc.TMin      = MinT;
    Desc.TMax      = MaxT;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> Q;
    Q.TraceRayInline(TopLevelAccel, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, Desc);
    while (Q.Proceed()) {}
    return (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
#else
    return TraceWideBoundingTreeAnyHit(
        WideEntries, Primitives, PtUniforms.WideEntryCount, Origin, UnitDir, MinT, MaxT);
#endif
}

float3 EvaluateBounceSky(float3 UnitDir)
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
void WavefrontBounceClosestEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint RayIdx   = DispatchThreadID.x;
    uint RayCount = QueueCounters[1u];
    if (RayIdx >= RayCount) return;

    GpuWavefrontBounceRay48 Ray = InBounceRays[RayIdx];
    GpuRayHitSummary Hit        = TraceBounceClosest(Ray.OriginWorld, Ray.UnitDirection, 1.5e-3, 300.0);
    if (!Hit.HitConfirmed)
    {
        float3 SkyContrib = Ray.PathThroughput * EvaluateBounceSky(Ray.UnitDirection);
        AccumulateDeterministicRadiance(FixedAccumWords, Ray.PixelLinearIndex, SkyContrib);
        return;
    }

    uint MatIdx = min(Hit.MaterialOrdinal, max(1u, PtUniforms.MaterialCount) - 1u);
    GpuWavefrontMaterial32 Mat = Materials[MatIdx];

    if (any(Mat.EmittedRadiance > float3(1.0e-5)))
    {
        // Power-Heuristic MIS weight when BSDF bounce hits an emissive triangle
        float MisW = (PtUniforms.EmissiveCount > 0u) ? 0.35 : 1.0;
        AccumulateDeterministicRadiance(FixedAccumWords, Ray.PixelLinearIndex, Ray.PathThroughput * Mat.EmittedRadiance * MisW);
    }

    float3 OrientedN = Hit.HitFrontFace ? Hit.GeometricNormal : (-Hit.GeometricNormal);
    uint Slot = 0u;
    InterlockedAdd(QueueCounters[0u], 1u, Slot);
    GpuWavefrontSurfaceHit64 Item;
    Item.HitPositionWorld = Ray.OriginWorld + Ray.UnitDirection * Hit.HitDistanceMeters;
    Item.PixelLinearIndex = Ray.PixelLinearIndex;
    Item.GeometricNormal  = OrientedN;
    Item.MaterialOrdinal  = MatIdx;
    Item.IncomingRayDir   = Ray.UnitDirection;
    Item.SampleBounceKey  = Ray.SampleBounceKey;
    Item.PathThroughput   = Ray.PathThroughput;
    Item.HitDistanceM     = Hit.HitDistanceMeters;
    OutSurfaceHits[Slot]  = Item;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void WavefrontShadowAnyHitEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint ShIdx   = DispatchThreadID.x;
    uint ShCount = QueueCounters[2u];
    if (ShIdx >= ShCount) return;

    GpuWavefrontShadowRay48 ShRay = InShadowRays[ShIdx];
    if (!TraceShadowOccluded(ShRay.OriginWorld, ShRay.UnitDirection, 1.5e-3, ShRay.MaxDistanceM))
    {
        AccumulateDeterministicRadiance(FixedAccumWords, ShRay.PixelLinearIndex, ShRay.UnoccludedContrib);
    }
}
