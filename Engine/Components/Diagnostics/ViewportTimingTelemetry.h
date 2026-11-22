/*============================================================================================================================================
                                                        FRAMETIMINGTELEMETRY.H
============================================================================================================================================*/
// 🧩 Per-stage CPU/GPU timing breakdown and debug timing log utilities for Frontier diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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
    std::string TimingLogPath       { "FrontierTiming.log" }; // [-] - Debug log file path

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

class TimingLogBufferedWriter
{
public:
    TimingLogBufferedWriter() = default;
    ~TimingLogBufferedWriter() noexcept
    {
        Flush();
    }

    void Initialize(const std::filesystem::path& LogPath, std::string_view AdapterName, bool HwGpu)
    {
        if (Stream.is_open())
        {
            Stream.close();
        }
        ActivePath = LogPath;
        Stream.open(ActivePath, std::ios::out | std::ios::trunc);
        if (!Stream)
        {
            return;
        }
        Buffer.clear();
        Buffer.reserve(65536u);
        Buffer += "# Frontier Per-Stage Timing Log\n";
        Buffer += "# Adapter: ";
        Buffer.append(AdapterName.data(), AdapterName.size());
        Buffer += " | HardwareGPU: ";
        Buffer += (HwGpu ? "YES\n" : "NO\n");
        Buffer += "# Columns: Frame | FPS | TotalMs | Engine | Shadow | Res | WorldGpuTotalMs | SkyLutMs | CloudsMs | TraceMs | DenoiseMs | CopyMs | HostWaitMs | SceneSyncMs | TickMs | GizmoMs | UiBuildMs | PresentMs\n";
        Flush();
    }

    void Append(const std::filesystem::path& LogPath, std::string_view FormattedLine)
    {
        if (!Stream.is_open() || ActivePath != LogPath)
        {
            ActivePath = LogPath;
            Stream.open(ActivePath, std::ios::out | std::ios::app);
        }
        if (!Stream)
        {
            return;
        }
        Buffer.append(FormattedLine.data(), FormattedLine.size());
        if (Buffer.size() >= 32768u)
        {
            Flush();
        }
    }

    void Flush() noexcept
    {
        if (Stream.is_open() && !Buffer.empty())
        {
            Stream.write(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
            Stream.flush();
            Buffer.clear();
        }
    }

private:
    std::filesystem::path ActivePath {};
    std::ofstream         Stream     {};
    std::string           Buffer     {};
};

[[nodiscard]] inline TimingLogBufferedWriter& AccessTimingLogWriter() noexcept
{
    static TimingLogBufferedWriter Writer {};
    return Writer;
}

inline void InitializeTimingLogFile(const std::filesystem::path& LogPath, std::string_view AdapterName, bool HwGpu)
{
    AccessTimingLogWriter().Initialize(LogPath, AdapterName, HwGpu);
}

inline void FlushTimingLogFile() noexcept
{
    AccessTimingLogWriter().Flush();
}

inline void AppendTimingLogLine(const std::filesystem::path& LogPath, const ViewportTimingBreakdown& T, const char* ReasonTag = "SAMPLE")
{
    const float Fps = (T.TotalFrameMs > 0.01f) ? (1000.0f / T.TotalFrameMs) : 0.0f;
    char Line[512]{};
    const int Written = std::snprintf(
        Line,
        sizeof(Line),
        "[%s] F=%llu | %5.1f fps | Total=%6.2f ms | %s/%s Q%u | %ux%u | "
        "GPU(Tot=%5.2f Sky=%4.2f Cld=%4.2f Trc=%5.2f Den=%4.2f Cpy=%4.2f HostWait=%5.2f) | "
        "SceneSync=%5.2f Tick=%4.2f Gizmo=%4.2f UI=%4.2f Present=%5.2f\n",
        ReasonTag != nullptr ? ReasonTag : "SAMPLE",
        static_cast<unsigned long long>(T.FrameIndex),
        Fps,
        T.TotalFrameMs,
        QueryEngineShortLabel(T.EngineMode),
        QueryShadowShortLabel(T.ShadowAlgorithm),
        T.QualityTier,
        T.RenderWidthPx,
        T.RenderHeightPx,
        T.GpuTotalPassMs,
        T.GpuSkyLutMs,
        T.GpuCloudsMs,
        T.GpuPrimaryTraceMs,
        T.GpuDenoiseMs,
        T.GpuReadbackCopyMs,
        T.GpuHostSubmitWaitMs,
        T.SceneSyncGpuMs,
        T.EditorTickMs,
        T.GizmoOverlayMs,
        T.DesktopPollAndUiMs,
        T.SwapchainPresentMs);
    if (Written > 0)
    {
        AccessTimingLogWriter().Append(
            LogPath,
            std::string_view(Line, static_cast<std::size_t>(std::min<int>(Written, static_cast<int>(sizeof(Line) - 1)))));
    }
}

} // namespace Frontier
