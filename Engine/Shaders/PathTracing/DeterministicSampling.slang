/*============================================================================================================================================
                                                        DETERMINISTICSAMPLING.SLANG
============================================================================================================================================*/
// 🧩 Shared Laine-Karras Owen-Scrambled Sobol (0,2) Quadrature, Dupuy-Benyoub (2023) GGX VNDF & Split-Word 64-Bit Fixed-Point Accumulator

module DeterministicSampling;

public struct GpuWavefrontUniforms
{
    public float3 CameraOrigin;
    public float  TanHalfVertFov;
    public float3 CameraForward;
    public float  AspectRatio;
    public float3 CameraRight;
    public uint   ViewportWidth;
    public float3 CameraUp;
    public uint   ViewportHeight;
    public float3 SunDirection;
    public float  TanSunRadius;
    public float3 SolarIrradiance;
    public uint   WideEntryCount;
    public float3 SkyRadiance;
    public uint   PrimitiveCount;
    public uint   MaterialCount;
    public uint   EmissiveCount;
    public uint   CapsuleCount;
    public uint   TotalSamples;
    public uint   SampleOffset;
    public uint   SampleBatchCount;
    public uint   MaxBounceDepth;
    public uint   ActiveBounceIdx;
    public uint   SharedNetMode;
    public uint   TilePixelOffset;
    public uint   TilePixelCount;
    public uint   AtrousStepWidth;
};

public struct GpuWavefrontBounceRay48
{
    public float3 OriginWorld;
    public uint   PixelLinearIndex;
    public float3 UnitDirection;
    public uint   SampleBounceKey;
    public float3 PathThroughput;
    public float  PreviousBsdfPdf;
};

public struct GpuWavefrontSurfaceHit64
{
    public float3 HitPositionWorld;
    public uint   PixelLinearIndex;
    public float3 GeometricNormal;
    public uint   MaterialOrdinal;
    public float3 IncomingRayDir;
    public uint   SampleBounceKey;
    public float3 PathThroughput;
    public float  HitDistanceM;
};

public struct GpuWavefrontShadowRay48
{
    public float3 OriginWorld;
    public uint   PixelLinearIndex;
    public float3 UnitDirection;
    public float  MaxDistanceM;
    public float3 UnoccludedContrib;
    public float  PadTail;
};

public struct GpuWavefrontMaterial32
{
    public float3 BaseAlbedo;
    public float  RoughnessLinear;
    public float3 EmittedRadiance;
    public float  MetallicWeight;
};

public struct GpuWavefrontEmissiveTri
{
    public float3 VertexA;
    public float  AreaM2;
    public float3 VertexB;
    public uint   ClusterIndex;
    public float3 VertexC;
    public uint   TwoSidedEmit;
    public float3 UnitNormal;
    public float  PadScalar;
    public float3 EmittedRad;
    public float  CdfUpper;
};

public uint ReverseBits32(uint InputBits)
{
    uint X = ((InputBits & 0x55555555u) << 1u) | ((InputBits >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

public uint AvalancheKey32(uint InputKey)
{
    uint Mixed = InputKey;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

public uint LaineKarrasPermutation(uint InputBits, uint ScrambleKey)
{
    uint Permuted = InputBits + ScrambleKey;
    Permuted ^= Permuted * 0x6C50B47Cu;
    Permuted ^= Permuted * 0xB82F1E52u;
    Permuted ^= Permuted * 0xC7AFE638u;
    Permuted ^= Permuted * 0x8D22F6E6u;
    return Permuted;
}

public uint NestedDigitPermutation(uint InputBits, uint ScrambleKey)
{
    return ReverseBits32(LaineKarrasPermutation(ReverseBits32(InputBits), ScrambleKey));
}

public uint SobolSecondDimension(uint SampleIdx)
{
    uint Bits = SampleIdx;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

public float2 Sobol02Sample(uint SampleIndex, uint PixelKey, uint PairIndex)
{
    uint PairKey       = AvalancheKey32(PairIndex * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    uint ShuffledIndex = NestedDigitPermutation(SampleIndex, PairKey);
    uint FirstDigits   = NestedDigitPermutation(ReverseBits32(ShuffledIndex), AvalancheKey32(PairKey ^ 0xA511E9B3u));
    uint SecondDigits  = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey32(PairKey ^ 0x63D83595u));
    float Inv24        = 1.0 / 16777216.0;
    return float2(float(FirstDigits >> 8u) * Inv24, float(SecondDigits >> 8u) * Inv24);
}

public void ConstructOrthonormalBasis(float3 UnitN, out float3 OutTanU, out float3 OutBitV)
{
    float SignZ  = (UnitN.z >= 0.0) ? 1.0 : -1.0;
    float ScaleA = -1.0 / (SignZ + UnitN.z);
    float CrossB = UnitN.x * UnitN.y * ScaleA;
    OutTanU      = float3(1.0 + SignZ * UnitN.x * UnitN.x * ScaleA, SignZ * CrossB, -SignZ * UnitN.x);
    OutBitV      = float3(CrossB, SignZ + UnitN.y * UnitN.y * ScaleA, -UnitN.y);
}

// Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF Sampler
public float3 SampleSphericalCapGgxVndfLocal(float3 LocalViewWi, float AlphaRoughness, float2 Xi)
{
    float Alpha = clamp(AlphaRoughness, 0.004, 1.0);
    float3 VStd = normalize(float3(LocalViewWi.x * Alpha, LocalViewWi.y * Alpha, max(1.0e-5, LocalViewWi.z)));

    float Phi    = 6.28318530718 * Xi.x;
    float ZCap   = fma(1.0 - Xi.y, 1.0 + VStd.z, -VStd.z);
    float SinCap = sqrt(saturate(1.0 - ZCap * ZCap));
    float3 CapDir = float3(SinCap * cos(Phi), SinCap * sin(Phi), ZCap);
    float3 HalfStd = CapDir + VStd;
    return normalize(float3(HalfStd.x * Alpha, HalfStd.y * Alpha, max(1.0e-5, HalfStd.z)));
}

// Lock-free 64-bit split-word unsigned fixed-point (2^24 scale) carry-propagated atomicAdd
// Strictly associative & commutative across arbitrary GPU thread/warp execution order!
public void AccumulateDeterministicRadiance(
    RWStructuredBuffer<uint> FixedAccumWords,
    uint                     PixelIndex,
    float3                   LinearContribution)
{
    float3 Clamped = clamp(LinearContribution, float3(0.0), float3(32768.0));
    uint   BaseOff = PixelIndex * 6u;
    [unroll]
    for (uint Ch = 0u; Ch < 3u; ++Ch)
    {
        float Val   = Clamped[Ch];
        uint  Whole = uint(floor(Val));
        uint  Frac  = uint(floor((Val - float(Whole)) * 16777216.0 + 0.5));
        Whole      += (Frac >> 24u);
        Frac       &= 0x00FFFFFFu;

        uint WordLo = ((Whole & 0xFFu) << 24u) | Frac;
        uint WordHi = (Whole >> 8u);
        if (WordLo == 0u && WordHi == 0u) continue;

        uint PrevLo = 0u;
        InterlockedAdd(FixedAccumWords[BaseOff + Ch * 2u + 0u], WordLo, PrevLo);
        uint Carry = ((PrevLo + WordLo) < PrevLo) ? 1u : 0u;
        uint AddHi = WordHi + Carry;
        if (AddHi != 0u)
        {
            uint UnusedHi = 0u;
            InterlockedAdd(FixedAccumWords[BaseOff + Ch * 2u + 1u], AddHi, UnusedHi);
        }
    }
}
