/*============================================================================================================================================
                                                            WIDETREERAYQUERY.SLANG
============================================================================================================================================*/
// 🧩 Shared 128-byte 4-Wide BVH4 ray-box & Moller-Trumbore intersection routines for GTX compute & RTX software/hybrid fallback

module WideTreeRayQuery;

public struct GpuWideTreeEntry128
{
    public float4 BranchBoundsMinX;
    public float4 BranchBoundsMaxX;
    public float4 BranchBoundsMinY;
    public float4 BranchBoundsMaxY;
    public float4 BranchBoundsMinZ;
    public float4 BranchBoundsMaxZ;
    public uint4  BranchReference;
    public uint4  BranchPrimitiveCount;
};

public struct GpuRayTrianglePrimitive48
{
    public float3 Corner0;
    public uint   SourceFaceIndex;
    public float3 Edge01;
    public uint   PackedMaterialOpaque;
    public float3 Edge02;
    public uint   PrimitiveOrdinal;
};

public struct GpuRayHitSummary
{
    public bool   HitConfirmed;
    public float  HitDistanceMeters;
    public float2 BarycentricsUV;
    public uint   MaterialOrdinal;
    public float3 GeometricNormal;
    public uint   SourceFaceIndex;
    public bool   HitFrontFace;
};

public struct GpuTaperedCapsule32
{
    public float3 JointStart;
    public float  RadiusStart;
    public float3 JointEnd;
    public float  RadiusEnd;
};

public struct GpuOccluderSurfel32
{
    public float3 PositionWorld;
    public float  RadiusMeters;
    public float3 SurfaceNormal;
    public float  SurfaceAreaM2;
};

public float EvaluateCapsuleSignedDistance(float3 WorldPos, GpuTaperedCapsule32 Cap)
{
    float3 Seg   = Cap.JointEnd - Cap.JointStart;
    float3 Rel   = WorldPos - Cap.JointStart;
    float  LenSq = dot(Seg, Seg);
    float  Frac  = (LenSq > 1.0e-8) ? saturate(dot(Rel, Seg) / LenSq) : 0.0;
    float  Rad   = lerp(Cap.RadiusStart, Cap.RadiusEnd, Frac);
    return length(Rel - Seg * Frac) - Rad;
}

public float EvaluateCapsuleRigConeVisibility(
    StructuredBuffer<GpuTaperedCapsule32> Capsules,
    uint                                  CapsuleCount,
    float3                                ReceiverPoint,
    float3                                ReceiverNormal,
    float3                                LightDir,
    float                                 TanHalfAngle,
    float                                 MaxDistM)
{
    if (CapsuleCount == 0u) return 1.0;

    float MinRatio   = 2.0;
    float Travel     = 0.045;
    float ClampedTan = max(0.004, TanHalfAngle);

    for (uint StepIdx = 0u; StepIdx < 36u && Travel < MaxDistM; ++StepIdx)
    {
        float3 SampleP  = ReceiverPoint + LightDir * Travel;
        float  PlaneT   = saturate((dot(SampleP - ReceiverPoint, ReceiverNormal) - 0.015) / 0.085);
        float  PlaneW   = PlaneT * PlaneT * (3.0 - 2.0 * PlaneT);
        float  BestCapD = 1.0e5;
        for (uint CIdx = 0u; CIdx < CapsuleCount; ++CIdx)
        {
            BestCapD = min(BestCapD, EvaluateCapsuleSignedDistance(SampleP, Capsules[CIdx]));
        }
        if (PlaneW > 1.0e-4)
        {
            float ConeR   = max(0.016, Travel * ClampedTan);
            float Ratio   = clamp(BestCapD / ConeR, -1.5, 1.5) * PlaneW + 1.5 * (1.0 - PlaneW);
            float Overlap = max(0.12 - abs(MinRatio - Ratio), 0.0) / 0.12;
            MinRatio      = min(MinRatio, Ratio) - Overlap * Overlap * 0.03;
            if (MinRatio <= -0.96) return 0.0;
        }
        Travel += clamp(abs(BestCapD) * 0.55, 0.025, 0.24);
    }

    float NormVis = saturate((MinRatio + 0.95) / 1.90);
    return NormVis * NormVis * (3.0 - 2.0 * NormVis);
}

public float IntersectTriangleMollerTrumbore(
    float3         Origin,
    float3         Direction,
    float          MinT,
    float          MaxT,
    GpuRayTrianglePrimitive48 Prim,
    out float2     OutUV)
{
    OutUV = float2(0.0);
    float3 PVec = cross(Direction, Prim.Edge02);
    float  Det  = dot(Prim.Edge01, PVec);
    if (abs(Det) <= 1.0e-8) return -1.0;

    float  InvDet = 1.0 / Det;
    float3 TVec   = Origin - Prim.Corner0;
    float  U      = dot(TVec, PVec) * InvDet;
    if (U < 0.0 || U > 1.0) return -1.0;

    float3 QVec = cross(TVec, Prim.Edge01);
    float  V    = dot(Direction, QVec) * InvDet;
    if (V < 0.0 || (U + V) > 1.0) return -1.0;

    float HitT = dot(Prim.Edge02, QVec) * InvDet;
    if (HitT < MinT || HitT > MaxT) return -1.0;

    OutUV = float2(U, V);
    return HitT;
}

public float4 IntersectFourBoxesSlab(
    float3             Origin,
    float3             InvDir,
    float              MinT,
    float              MaxT,
    GpuWideTreeEntry128 Entry)
{
    float4 TX0 = (Entry.BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (Entry.BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (Entry.BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (Entry.BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (Entry.BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (Entry.BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));

    return select(NearT <= FarT, NearT, float4(1.0e30));
}

public GpuRayHitSummary TraceWideBoundingTreeClosest(
    StructuredBuffer<GpuWideTreeEntry128>        WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       UnitDir,
    float                                        MinT,
    float                                        MaxT)
{
    GpuRayHitSummary Result;
    Result.HitConfirmed      = false;
    Result.HitDistanceMeters = MaxT;
    Result.BarycentricsUV    = float2(0.0);
    Result.MaterialOrdinal   = 0u;
    Result.GeometricNormal   = float3(0.0, 0.0, 1.0);
    Result.SourceFaceIndex   = 0xFFFFFFFFu;
    Result.HitFrontFace      = true;

    if (EntryCount == 0u) return Result;

    float3 InvDir = 1.0 / select(abs(UnitDir) > float3(1.0e-8), UnitDir, float3(1.0e-8));
    uint   Stack[32];
    int    StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurrentIdx = Stack[StackTop--];
        if (CurrentIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = WideEntries[CurrentIdx];
        float4 BoxHits = IntersectFourBoxesSlab(Origin, InvDir, MinT, Result.HitDistanceMeters, Entry);

        [unroll]
        for (int Lane = 0; Lane < 4; ++Lane)
        {
            uint Ref = Entry.BranchReference[Lane];
            if (Ref == 0xFFFFFFFFu || BoxHits[Lane] > Result.HitDistanceMeters) continue;

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint PIdx = 0u; PIdx < Count; ++PIdx)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + PIdx];
                    if ((Prim.PackedMaterialOpaque >> 16u) == 0u) continue;
                    float2 HitUV;
                    float HitT = IntersectTriangleMollerTrumbore(Origin, UnitDir, MinT, Result.HitDistanceMeters, Prim, HitUV);
                    if (HitT > 0.0)
                    {
                        float3 RawCross = cross(Prim.Edge01, Prim.Edge02);
                        float  CrossLen = length(RawCross);
                        float3 GeomN    = (CrossLen > 1.0e-12) ? (RawCross / CrossLen) : float3(0.0, 0.0, 1.0);
                        Result.HitConfirmed      = true;
                        Result.HitDistanceMeters = HitT;
                        Result.BarycentricsUV    = HitUV;
                        Result.MaterialOrdinal   = Prim.PackedMaterialOpaque & 0xFFFFu;
                        Result.GeometricNormal   = GeomN;
                        Result.SourceFaceIndex   = Prim.SourceFaceIndex;
                        Result.HitFrontFace      = (dot(UnitDir, GeomN) < 0.0);
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return Result;
}

public bool TraceWideBoundingTreeAnyHit(
    StructuredBuffer<GpuWideTreeEntry128>        WideEntries,
    StructuredBuffer<GpuRayTrianglePrimitive48>  Primitives,
    uint                                         EntryCount,
    float3                                       Origin,
    float3                                       UnitDir,
    float                                        MinT,
    float                                        MaxT)
{
    if (EntryCount == 0u) return false;

    float3 InvDir = 1.0 / select(abs(UnitDir) > float3(1.0e-8), UnitDir, float3(1.0e-8));
    uint   Stack[32];
    int    StackTop = 0;
    Stack[0] = 0u;

    while (StackTop >= 0)
    {
        uint CurrentIdx = Stack[StackTop--];
        if (CurrentIdx >= EntryCount) continue;

        GpuWideTreeEntry128 Entry = WideEntries[CurrentIdx];
        float4 BoxHits = IntersectFourBoxesSlab(Origin, InvDir, MinT, MaxT, Entry);

        [unroll]
        for (int Lane = 0; Lane < 4; ++Lane)
        {
            uint Ref = Entry.BranchReference[Lane];
            if (Ref == 0xFFFFFFFFu || BoxHits[Lane] > MaxT) continue;

            if ((Ref & 0x80000000u) != 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                uint Count = Entry.BranchPrimitiveCount[Lane];
                for (uint PIdx = 0u; PIdx < Count; ++PIdx)
                {
                    GpuRayTrianglePrimitive48 Prim = Primitives[Start + PIdx];
                    if ((Prim.PackedMaterialOpaque >> 16u) == 0u) continue;
                    float2 HitUV;
                    float HitT = IntersectTriangleMollerTrumbore(Origin, UnitDir, MinT, MaxT, Prim, HitUV);
                    if (HitT > 0.0)
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }

    return false;
}
