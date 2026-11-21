/*============================================================================================================================================
                                                      EXECUTIONTIMINGLEDGER.H
============================================================================================================================================*/
// 🧩 Thread-fed in-memory timing and note ledger that persists exactly once during application shutdown

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ExecutionTimingLedger
{
public:
    static ExecutionTimingLedger& Instance() noexcept;

    void ConfigureOutputPath(std::filesystem::path OutputPath) noexcept;
    void AppendTiming(std::string_view Label, float DurationMs, uint64_t FrameIndex = 0ull) noexcept;
    void AppendNote(std::string_view Label, std::string_view Detail, uint64_t FrameIndex = 0ull) noexcept;
    void PersistAndStop(std::filesystem::path OverridePath = {}) noexcept;

    [[nodiscard]] std::size_t QueryBufferedCount() const noexcept;
    [[nodiscard]] bool QueryWorkerActive() const noexcept;

    ExecutionTimingLedger(const ExecutionTimingLedger&)            = delete;
    ExecutionTimingLedger& operator=(const ExecutionTimingLedger&) = delete;

private:
    struct LedgerEntry
    {
        uint64_t                                  Sequence    = 0ull;  // [idx] - Monotonic intake ordinal
        uint64_t                                  FrameIndex  = 0ull;  // [idx] - Optional frame affiliation
        std::chrono::steady_clock::time_point     Stamp       {};      // [s]   - Intake instant on the calling thread
        std::string                               Label       {};      // [-]   - Stage or note identifier
        std::string                               Detail      {};      // [-]   - Optional note payload
        float                                     DurationMs  = 0.0f;  // [ms]  - Measured elapsed duration (0 for notes)
    };

    struct AggregateEntry
    {
        uint64_t Count    = 0ull;  // [count] - Samples folded into this label
        double   TotalMs  = 0.0;   // [ms]    - Sum of all durations
        double   MinimumMs = 0.0;  // [ms]    - Fastest observed sample
        double   MaximumMs = 0.0;  // [ms]    - Slowest observed sample
    };

    ExecutionTimingLedger() noexcept = default;
    ~ExecutionTimingLedger() noexcept;

    void EnsureWorker() noexcept;
    void Enqueue(
        std::string_view Label,
        std::string_view Detail,
        float            DurationMs,
        uint64_t         FrameIndex) noexcept;
    void DrainPending() noexcept;
    void WorkerLoop() noexcept;
    void WriteReport(const std::filesystem::path& OutputPath) const noexcept;

    mutable std::mutex      QueueMutex        {};
    std::condition_variable QueueWake         {};
    std::deque<LedgerEntry> Pending            {};
    std::vector<LedgerEntry> Recorded          {}; // [-] - Complete in-RAM history until shutdown
    std::map<std::string, AggregateEntry> Aggregates {};
    std::filesystem::path   ConfiguredPath     { "SessionOutput/ThoughtLog.md" };
    std::thread             Worker             {};
    uint64_t                NextSequence       = 0ull;
    bool                    Accepting          = true;
    bool                    Running            = false;
};

} // namespace Frontier::Threading
