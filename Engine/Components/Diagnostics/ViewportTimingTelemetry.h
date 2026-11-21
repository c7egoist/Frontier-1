/*============================================================================================================================================
                                                        FRAMETIMINGTELEMETRY.H
============================================================================================================================================*/
// 🧩 Per-stage CPU/GPU timing breakdown with shutdown-only persistence path for Frontier diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportTimingBreakdown
{
    uint64_t    FrameIndex          = 0ull;  // [idx] - Monotonic desktop frame index
    uint32_t    RenderWidthPx       = 0u;    // [px]  - Active world render width
    uint32_t    RenderHeightPx      = 0u;    // [px]  - Active world render height
    uint32_t    EngineMode          = 1u;    // [idx] - 0=MatCap, 1=OpenPBR
    uint32_t    ShadowAlgorithm     = 4u;    // [idx] - 0=ContactPCSS, 1=PlanePCSS, 2=SMRT, 3=PCF, 4=RayQuery
    uint32_t    QualityTier         = 2u;    // [idx] - 0=Low..4=Ultra
    bool        HardwareGpuActive   = false; // [-]   - True when running on hardware GPU (discrete/iGPU)
    bool        GpuTimestampsValid  = false; // [-]   - True when VkQueryPool timestamps populated GPU pass times
    bool        WorldDispatched     = false; // [-]   - True when world compute pipeline executed this frame
    std::string AdapterName         { "Unknown" }; // [-] - Active Vulkan adapter name
    std::string TimingLogPath       { "SessionOutput/ThoughtLog.md" }; // [-] - Shutdown-only ledger report path

    // End-to-end host frame stages [ms]
    float TotalFrameMs              = 0.0f;  // [ms] - Full frame wall-clock delta
    float DesktopPollAndUiMs        = 0.0f;  // [ms] - GLFW poll + ImGui frame build
    float EditorTickMs              = 0.0f;  // [ms] - Scene/outliner tick before render
    float SceneSyncGpuMs            = 0.0f;  // [ms] - GPU scene upload & shadow cascade sync
    float GizmoOverlayMs            = 0.0f;  // [ms] - 3D Transform gizmo raster & composite
    float SwapchainPresentMs        = 0.0f;  // [ms] - ImGui draw + texture upload + vkQueuePresentKHR

    // Vulkan world compute GPU hardware pass breakdown [ms]
    float GpuTotalPassMs            = 0.0f;  // [ms] - VkQueryPool total GPU time across world passes
    float GpuSkyLutMs               = 0.0f;  // [ms] - SkyView transmittance LUT compute pass
    float GpuCloudsMs               = 0.0f;  // [ms] - Downsampled volumetric cloud deck compute pass
    float GpuPrimaryTraceMs         = 0.0f;  // [ms] - Primary raytrace/shade/shadow pass
    float GpuDenoiseMs              = 0.0f;  // [ms] - Spatial shadow denoise & composite passes
    float GpuReadbackCopyMs         = 0.0f;  // [ms] - VRAM -> host-cached staging copy
    float GpuHostSubmitWaitMs       = 0.0f;  // [ms] - CPU uniform pack, cmd encode, timeline wait & memcpy
};

[[nodiscard]] inline const char* QueryEngineShortLabel(uint32_t EngineMode) noexcept
{
    switch (EngineMode)
    {
    case 0u: return "MatCap";
    case 1u: return "OpenPBR Direct";
    case 2u: return "GTX GDF GI";
    case 3u: return "HighEnd BVH4";
    case 4u: return "PT Realtime";
    case 5u: return "PT Final";
    default: return "GTX GDF GI";
    }
}

[[nodiscard]] inline const char* QueryShadowShortLabel(uint32_t ShadowAlg) noexcept
{
    switch (ShadowAlg)
    {
    case 0u: return "ContactPCSS";
    case 1u: return "PlanePCSS";
    case 2u: return "SMRT";
    case 3u: return "PCF3x3";
    case 4u: return "RayQuery";
    default: return "Unknown";
    }
}

} // namespace Frontier
