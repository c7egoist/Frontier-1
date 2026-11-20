/*============================================================================================================================================
                                                        WAVEFRONTATROUSFILTER.SLANG
============================================================================================================================================*/
// 🧩 Wavefront Queue 5 & Reconstruction: Split-Word 64-Bit Fixed-Point HDR Resolve + Albedo-Demodulated Edge-Stopping A-Trous Filter

import DeterministicSampling;

[[vk::binding(0, 0)]] ConstantBuffer<GpuWavefrontUniforms> PtUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<uint>               FixedAccumWords;
[[vk::binding(2, 0)]] StructuredBuffer<float4>             PrimaryAlbedoDepth;
[[vk::binding(3, 0)]] StructuredBuffer<float4>             PrimaryNormalBuffer;
[[vk::binding(4, 0)]] StructuredBuffer<float4>             InDemodRadiance;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>           OutDemodRadiance;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4>           OutFinalLinearHdr;

float DecodeFixedPointChannel(uint WordLo, uint WordHi, float InvTotalSamples)
{
    float WholeLo = float(WordLo >> 24u);
    float Frac24  = float(WordLo & 0x00FFFFFFu) * (1.0 / 16777216.0);
    float WholeHi = float(WordHi) * 256.0;
    return (WholeHi + WholeLo + Frac24) * InvTotalSamples;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void WavefrontFinalizeSampleEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= PtUniforms.ViewportWidth || DispatchThreadID.y >= PtUniforms.ViewportHeight)
    {
        return;
    }

    uint PixIdx = DispatchThreadID.y * PtUniforms.ViewportWidth + DispatchThreadID.x;
    uint Base   = PixIdx * 6u;
    float InvS  = 1.0 / float(max(1u, PtUniforms.TotalSamples));

    float3 LinearHdr = float3(
        DecodeFixedPointChannel(FixedAccumWords[Base + 0u], FixedAccumWords[Base + 1u], InvS),
        DecodeFixedPointChannel(FixedAccumWords[Base + 2u], FixedAccumWords[Base + 3u], InvS),
        DecodeFixedPointChannel(FixedAccumWords[Base + 4u], FixedAccumWords[Base + 5u], InvS));

    OutFinalLinearHdr[PixIdx] = float4(LinearHdr, 1.0);

    float4 AlbDepth = PrimaryAlbedoDepth[PixIdx];
    float3 SafeAlb  = max(float3(0.04), AlbDepth.xyz);
    OutDemodRadiance[PixIdx] = float4(LinearHdr / SafeAlb, AlbDepth.w);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void WavefrontAtrousStepEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= PtUniforms.ViewportWidth || DispatchThreadID.y >= PtUniforms.ViewportHeight)
    {
        return;
    }

    int2   CenterCoord = int2(DispatchThreadID.xy);
    uint   CenterIdx   = DispatchThreadID.y * PtUniforms.ViewportWidth + DispatchThreadID.x;
    float4 CenterAlbD  = PrimaryAlbedoDepth[CenterIdx];
    float4 CenterNrm   = PrimaryNormalBuffer[CenterIdx];
    float4 CenterDemod = InDemodRadiance[CenterIdx];

    if (CenterNrm.w <= 0.0 || CenterAlbD.w >= 5.0e3)
    {
        OutDemodRadiance[CenterIdx]  = CenterDemod;
        OutFinalLinearHdr[CenterIdx] = float4(CenterDemod.xyz * max(float3(0.04), CenterAlbD.xyz), 1.0);
        return;
    }

    int   StepW = max(1, int(PtUniforms.AtrousStepWidth));
    float K1D[3] = { 1.0, 0.6666667, 0.25 };
    float3 SumDemod = float3(0.0);
    float  SumW     = 0.0;
    float  CenterL  = dot(CenterDemod.xyz, float3(0.2126, 0.7152, 0.0722));

    [unroll]
    for (int DY = -2; DY <= 2; ++DY)
    {
        int NY = clamp(CenterCoord.y + DY * StepW, 0, int(PtUniforms.ViewportHeight) - 1);
        float KY = K1D[abs(DY)];
        [unroll]
        for (int DX = -2; DX <= 2; ++DX)
        {
            int NX = clamp(CenterCoord.x + DX * StepW, 0, int(PtUniforms.ViewportWidth) - 1);
            uint NIdx = uint(NY) * PtUniforms.ViewportWidth + uint(NX);

            float4 NeighAlbD  = PrimaryAlbedoDepth[NIdx];
            float4 NeighNrm   = PrimaryNormalBuffer[NIdx];
            if (NeighNrm.w <= 0.0) continue;

            float4 NeighDemod = InDemodRadiance[NIdx];
            float  RelDepth   = abs(NeighAlbD.w - CenterAlbD.w) / max(0.08, CenterAlbD.w * 0.06);
            float  WDepth     = exp(-RelDepth * RelDepth);
            float  WNrm       = pow(saturate(dot(CenterNrm.xyz, NeighNrm.xyz)), 32.0);
            float  NeighL     = dot(NeighDemod.xyz, float3(0.2126, 0.7152, 0.0722));
            float  WLum       = exp(-abs(NeighL - CenterL) / max(0.35, CenterL * 0.85 + 0.25));
            float  Weight     = K1D[abs(DX)] * KY * WDepth * WNrm * WLum;

            SumDemod += NeighDemod.xyz * Weight;
            SumW     += Weight;
        }
    }

    float3 FilteredDemod = (SumW > 1.0e-6) ? (SumDemod / SumW) : CenterDemod.xyz;
    OutDemodRadiance[CenterIdx]  = float4(FilteredDemod, CenterAlbD.w);
    OutFinalLinearHdr[CenterIdx] = float4(FilteredDemod * max(float3(0.04), CenterAlbD.xyz), 1.0);
}
