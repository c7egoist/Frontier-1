/*============================================================================================================================================
                                                     CONTOURSURFELILLUMINATION.SLANG
============================================================================================================================================*/
// 🧩 GTX GI #1 Vulkan Compute: Denoiser-Free Stokes Spherical-Polygon Emissive Contour + 2-Bounce Surfel Radiosity + Anti-Banded SH9

module ContourSurfelIllumination;

import WideTreeRayQuery;

struct ContourSurfelUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   WideEntryCount;
    uint   CapsuleCount;
    uint   EmissiveTriCount;
    uint   BounceSurfelCount;
    uint   IncludeDirectContours;
    uint   PadZero;
    float3 ShBoundsMin;
    float  ShGridCountX;
    float3 ShCellSpacing;
    float  ShGridCountY;
    float  ShGridCountZ;
    float3 PadTail;
};

struct GpuEmissiveContourTri
{
    float3 VertexA;
    float  AreaM2;
    float3 VertexB;
    uint   ClusterIndex;
    float3 VertexC;
    uint   TwoSidedEmit;
    float3 UnitNormal;
    float  PadScalar;
    float3 EmittedRad;
    float  CdfUpper;
};

struct GpuRadiantBounceSurfel
{
    float3 PositionWorld;
    float  AreaM2;
    float3 UnitNormal;
    float  RadiusMeters;
    float3 DiffuseAlbedo;
    float  SkyVisibility;
    float3 ExitantOverPi;
    float  PadTail;
};

[[vk::binding(0, 0)]] ConstantBuffer<ContourSurfelUniforms>         GiUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneWideTree;
[[vk::binding(2, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   ScenePrimitives;
[[vk::binding(3, 0)]] StructuredBuffer<GpuTaperedCapsule32>         DeformingCapsules;
[[vk::binding(4, 0)]] StructuredBuffer<GpuEmissiveContourTri>       EmissiveTris;
[[vk::binding(5, 0)]] StructuredBuffer<GpuRadiantBounceSurfel>      BounceSurfels;
[[vk::binding(6, 0)]] StructuredBuffer<float4>                      Sh9Coeffs;
[[vk::binding(7, 0)]] Texture2D<float4>                             WorldPositionTex;
[[vk::binding(8, 0)]] Texture2D<float4>                             WorldNormalTex;
[[vk::binding(9, 0)]] [format("rgba16f")] RWTexture2D<float4>       OutIndirectOverPi;

float EvaluateStokesClippedTriFormFactor(float3 RecvP, float3 RecvN, float3 VA, float3 VB, float3 VC)
{
    float3 InVerts[3] = { VA, VB, VC };
    float  Dist[3];
    [unroll]
    for (uint I = 0u; I < 3u; ++I)
    {
        Dist[I] = dot(InVerts[I] - RecvP, RecvN);
    }
    if (Dist[0] <= 0.0 && Dist[1] <= 0.0 && Dist[2] <= 0.0)
    {
        return 0.0;
    }

    float3 Clipped[4];
    uint   PolyCount = 0u;
    [unroll]
    for (uint I = 0u; I < 3u; ++I)
    {
        uint  J   = (I + 1u) % 3u;
        float DA  = Dist[I];
        float DB  = Dist[J];
        bool  InA = (DA >= 0.0);
        bool  InB = (DB >= 0.0);
        if (InA)
        {
            if (PolyCount < 4u) Clipped[PolyCount++] = InVerts[I];
            if (!InB && PolyCount < 4u)
            {
                float T = DA / max(1.0e-7, DA - DB);
                Clipped[PolyCount++] = lerp(InVerts[I], InVerts[J], T);
            }
        }
        else if (InB && PolyCount < 4u)
        {
            float T = DA / max(1.0e-7, DA - DB);
            Clipped[PolyCount++] = lerp(InVerts[I], InVerts[J], T);
        }
    }

    if (PolyCount < 3u) return 0.0;

    float3 Sphere[4];
    for (uint I = 0u; I < PolyCount; ++I)
    {
        Sphere[I] = normalize(Clipped[I] - RecvP);
    }

    float ContourSum = 0.0;
    for (uint I = 0u; I < PolyCount; ++I)
    {
        uint   J         = (I + 1u) % PolyCount;
        float  CosArc    = clamp(dot(Sphere[I], Sphere[J]), -0.999999, 0.999999);
        float3 EdgeCross = cross(Sphere[I], Sphere[J]);
        float  CrossLen  = length(EdgeCross);
        if (CrossLen > 1.0e-7)
        {
            ContourSum += acos(CosArc) * (dot(EdgeCross, RecvN) / CrossLen);
        }
    }
    return saturate(abs(ContourSum) * 0.159154943);
}

float3 EvaluateSh9Cell(uint CellIdx, float3 UnitNormal)
{
    float Basis[9];
    Basis[0] = 0.28209479;
    Basis[1] = 0.48860251 * UnitNormal.y;
    Basis[2] = 0.48860251 * UnitNormal.z;
    Basis[3] = 0.48860251 * UnitNormal.x;
    Basis[4] = 1.09254843 * UnitNormal.x * UnitNormal.y;
    Basis[5] = 1.09254843 * UnitNormal.y * UnitNormal.z;
    Basis[6] = 0.31539157 * (3.0 * UnitNormal.z * UnitNormal.z - 1.0);
    Basis[7] = 1.09254843 * UnitNormal.x * UnitNormal.z;
    Basis[8] = 0.54627422 * (UnitNormal.x * UnitNormal.x - UnitNormal.y * UnitNormal.y);

    float Zonal[9] = { 1.0, 0.66666667, 0.66666667, 0.66666667, 0.25, 0.25, 0.25, 0.25, 0.25 };
    float3 Sum = float3(0.0);
    uint BaseOff = CellIdx * 9u;
    [unroll]
    for (uint B = 0u; B < 9u; ++B)
    {
        Sum += Sh9Coeffs[BaseOff + B].xyz * (Basis[B] * Zonal[B]);
    }
    return max(float3(0.0), Sum);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ContourSurfelComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= GiUniforms.ViewportWidth || DispatchThreadID.y >= GiUniforms.ViewportHeight)
    {
        return;
    }

    float4 PosSample = WorldPositionTex.Load(int3(DispatchThreadID.xy, 0));
    if (PosSample.w <= 0.0)
    {
        OutIndirectOverPi[DispatchThreadID.xy] = float4(0.0);
        return;
    }

    float3 RecvP = PosSample.xyz;
    float3 RecvN = normalize(WorldNormalTex.Load(int3(DispatchThreadID.xy, 0)).xyz);

    // 1. 2-Bounce area-stratified Bunnell disk radiosity
    float3 SurfelBounce = float3(0.0);
    for (uint SIdx = 0u; SIdx < GiUniforms.BounceSurfelCount; ++SIdx)
    {
        GpuRadiantBounceSurfel Surf = BounceSurfels[SIdx];
        float3 Delta  = Surf.PositionWorld - RecvP;
        float  DistSq = dot(Delta, Delta);
        if (DistSq < 0.04) continue;
        float  Dist    = sqrt(DistSq);
        float3 Dir     = Delta / Dist;
        float  CosRecv = dot(RecvN, Dir);
        float  CosEmit = dot(Surf.UnitNormal, -Dir);
        if (CosRecv <= 1.0e-3 || CosEmit <= 1.0e-3) continue;
        float Form = (Surf.AreaM2 * CosRecv * CosEmit) / (3.14159265 * DistSq + Surf.AreaM2);
        SurfelBounce += Surf.ExitantOverPi * Form;
    }

    // 2. Normal-offset Hermite anti-banded SH9 far-field bounce
    uint CountX = max(2u, uint(GiUniforms.ShGridCountX));
    uint CountY = max(2u, uint(GiUniforms.ShGridCountY));
    uint CountZ = max(2u, uint(GiUniforms.ShGridCountZ));
    float BiasM = 0.14 * min(GiUniforms.ShCellSpacing.x, min(GiUniforms.ShCellSpacing.y, GiUniforms.ShCellSpacing.z));
    float3 BiasedP = RecvP + RecvN * BiasM;
    float3 GridCoord = clamp((BiasedP - GiUniforms.ShBoundsMin) / max(float3(1.0e-4), GiUniforms.ShCellSpacing),
                             float3(0.0), float3(float(CountX - 1u), float(CountY - 1u), float(CountZ - 1u)));
    uint3 I0 = min(uint3(CountX - 2u, CountY - 2u, CountZ - 2u), uint3(floor(GridCoord)));
    float3 Frac = GridCoord - float3(I0);
    float3 Herm = Frac * Frac * (3.0 - 2.0 * Frac);

    float3 ShIrr = float3(0.0);
    [unroll]
    for (uint Corner = 0u; Corner < 8u; ++Corner)
    {
        uint3 CIdx = I0 + uint3((Corner & 1u) != 0u ? 1u : 0u,
                                (Corner & 2u) != 0u ? 1u : 0u,
                                (Corner & 4u) != 0u ? 1u : 0u);
        float W = ((Corner & 1u) != 0u ? Herm.x : (1.0 - Herm.x)) *
                  ((Corner & 2u) != 0u ? Herm.y : (1.0 - Herm.y)) *
                  ((Corner & 4u) != 0u ? Herm.z : (1.0 - Herm.z));
        uint FlatCell = (CIdx.z * CountY + CIdx.y) * CountX + CIdx.x;
        ShIrr += EvaluateSh9Cell(FlatCell, RecvN) * W;
    }

    float3 TotalOverPi = SurfelBounce * 0.78 + ShIrr * 0.22;

    // 3. Optional exact Stokes contour direct emissives when isolated GI mode is requested
    if (GiUniforms.IncludeDirectContours != 0u)
    {
        for (uint TIdx = 0u; TIdx < GiUniforms.EmissiveTriCount; ++TIdx)
        {
            GpuEmissiveContourTri Tri = EmissiveTris[TIdx];
            float3 Centroid = (Tri.VertexA + Tri.VertexB + Tri.VertexC) * (1.0 / 3.0);
            if (Tri.TwoSidedEmit == 0u && dot(Tri.UnitNormal, RecvP - Centroid) <= 1.0e-4) continue;
            float FormF = EvaluateStokesClippedTriFormFactor(RecvP, RecvN, Tri.VertexA, Tri.VertexB, Tri.VertexC);
            if (FormF <= 1.0e-6) continue;

            float3 ToEmit  = Centroid - RecvP;
            float  EmitLen = length(ToEmit);
            float3 UnitL   = ToEmit / max(1.0e-4, EmitLen);
            bool Occluded  = TraceWideBoundingTreeAnyHit(
                SceneWideTree, ScenePrimitives, GiUniforms.WideEntryCount, RecvP + RecvN * 2.5e-3, UnitL, 2.0e-3, max(0.02, EmitLen - 0.04));
            if (!Occluded)
            {
                float CapVis = EvaluateCapsuleRigConeVisibility(
                    DeformingCapsules, GiUniforms.CapsuleCount, RecvP, RecvN, UnitL, 0.06, max(0.05, EmitLen));
                TotalOverPi += Tri.EmittedRad * (FormF * CapVis);
            }
        }
    }

    OutIndirectOverPi[DispatchThreadID.xy] = float4(TotalOverPi, 1.0);
}
