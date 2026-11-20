/*============================================================================================================================================
                                                          WAVEFRONTQUEUELAYOUT.H
============================================================================================================================================*/
// 🧩 GPU-aligned SoA ray/hit/shadow queue entries & 64-bit split-word fixed-point (2^24 scale) carry-propagated accumulator layout

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double   WavefrontFixedScale      = 16777216.0; // [2^24] - 5.96e-8 radiance precision, associative atomicAdd
inline constexpr double   WavefrontInverseFixed    = 1.0 / 16777216.0;
inline constexpr uint32_t WavefrontMaxTileCapacity = 65536u;     // [px]   - L2-resident Wavefront queue capacity per tile batch

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 48-byte continuation bounce ray entry enqueued by SurfaceScatter and consumed by BounceTrace
struct alignas(16) WavefrontBounceRay48
{
    Mathematics::Axis3D    OriginWorld      {};      // [m]   - Biased surface continuation origin
    uint32_t               PixelLinearIndex = 0u;    // [idx] - Viewport pixel index (Y * Width + X)
    Mathematics::Axis3D    UnitDirection    {};      // [-]   - Sampled continuation ray direction
    uint32_t               SampleBounceKey  = 0u;    // [bit] - Low 16b = SampleIdx, High 16b = BounceDepth
    Mathematics::LinearRGB PathThroughput   {};      // [-]   - Cumulative BSDF * cos / pdf weight
    float                  PreviousBsdfPdf  = 0.0f;  // [1/sr]- BSDF sampling PDF for MIS on emissive hit
};
static_assert(sizeof(WavefrontBounceRay48) == 48u, "WavefrontBounceRay48 must remain 48 bytes.");

// 64-byte resolved surface hit entry enqueued by PrimaryTrace / BounceTrace and consumed by SurfaceScatter
struct alignas(16) WavefrontSurfaceHit64
{
    Mathematics::Axis3D    HitPositionWorld {};      // [m]   - Exact triangle intersection position
    uint32_t               PixelLinearIndex = 0u;    // [idx] - Viewport pixel index
    Mathematics::Axis3D    GeometricNormal  {};      // [-]   - Outward face normal oriented against ray
    uint32_t               MaterialOrdinal  = 0u;    // [idx] - Material slot index
    Mathematics::Axis3D    IncomingRayDir   {};      // [-]   - Incident unit ray direction
    uint32_t               SampleBounceKey  = 0u;    // [bit] - Low 16b = SampleIdx, High 16b = BounceDepth
    Mathematics::LinearRGB PathThroughput   {};      // [-]   - Throughput arriving at this surface hit
    float                  HitDistanceM     = 0.0f;  // [m]   - Ray travel distance
};
static_assert(sizeof(WavefrontSurfaceHit64) == 64u, "WavefrontSurfaceHit64 must remain 64 bytes.");

// 48-byte NEE shadow ray entry enqueued by SurfaceScatter and consumed by ShadowTrace
struct alignas(16) WavefrontShadowRay48
{
    Mathematics::Axis3D    OriginWorld       {};      // [m]     - Biased shadow ray origin
    uint32_t               PixelLinearIndex  = 0u;    // [idx]   - Viewport pixel index
    Mathematics::Axis3D    UnitDirection     {};      // [-]     - Unit direction toward sampled luminaire
    float                  MaxDistanceM      = 100.0f;// [m]     - Distance to luminaire minus epsilon
    Mathematics::LinearRGB UnoccludedContrib {};      // [cd/m2] - Pre-weighted throughput * BSDF * Le * MIS / pdf
    float                  PadTail           = 0.0f;  // [-]     - 16B alignment padding
};
static_assert(sizeof(WavefrontShadowRay48) == 48u, "WavefrontShadowRay48 must remain 48 bytes.");

// Queue counters buffer [0]=ActiveHits, [1]=NextBounceRays, [2]=ActiveShadows, [3]=CompletedSamples
struct alignas(16) WavefrontQueueCounters
{
    uint32_t SurfaceHitCount = 0u;
    uint32_t BounceRayCount  = 0u;
    uint32_t ShadowRayCount  = 0u;
    uint32_t ReservedSlot    = 0u;
};
static_assert(sizeof(WavefrontQueueCounters) == 16u, "WavefrontQueueCounters must remain 16 bytes.");

// Compact 32-byte GPU material entry for WavefrontSurfaceScatter
struct alignas(16) WavefrontGpuMaterial32
{
    Mathematics::LinearRGB SurfaceAlbedo   { 0.6f, 0.6f, 0.6f };
    float                  RoughnessLinear = 0.35f;
    Mathematics::LinearRGB EmittedRadiance { 0.0f, 0.0f, 0.0f };
    float                  MetallicWeight  = 0.0f;
};
static_assert(sizeof(WavefrontGpuMaterial32) == 32u, "WavefrontGpuMaterial32 must remain 32 bytes.");

// Uniform block shared across all Wavefront compute dispatches
struct alignas(16) WavefrontPathTraceUniforms
{
    Mathematics::Axis3D    CameraOrigin     {};
    float                  TanHalfVertFov   = 0.50f;
    Mathematics::Axis3D    CameraForward    {};
    float                  AspectRatio      = 1.777778f;
    Mathematics::Axis3D    CameraRight      {};
    uint32_t               ViewportWidth    = 320u;
    Mathematics::Axis3D    CameraUp         {};
    uint32_t               ViewportHeight   = 180u;
    Mathematics::Axis3D    SunDirection     { -0.45f, -0.52f, 0.725f };
    float                  TanSunRadius     = 0.028f;
    Mathematics::LinearRGB SolarIrradiance  { 4.20f, 3.98f, 3.65f };
    uint32_t               WideEntryCount   = 0u;
    Mathematics::LinearRGB SkyRadiance      { 0.16f, 0.22f, 0.34f };
    uint32_t               PrimitiveCount   = 0u;
    uint32_t               MaterialCount    = 0u;
    uint32_t               EmissiveCount    = 0u;
    uint32_t               CapsuleCount     = 0u;
    uint32_t               TotalSamples     = 8u;
    uint32_t               SampleOffset     = 0u;
    uint32_t               SampleBatchCount = 1u;
    uint32_t               MaxBounceDepth   = 4u;
    uint32_t               ActiveBounceIdx  = 0u;
    uint32_t               SharedNetMode    = 0u;
    uint32_t               TilePixelOffset  = 0u;
    uint32_t               TilePixelCount   = 0u;
    uint32_t               AtrousStepWidth  = 1u;
};
static_assert(sizeof(WavefrontPathTraceUniforms) == 160u, "WavefrontPathTraceUniforms must remain 160 bytes.");

} // namespace Frontier::GeometricRaster
