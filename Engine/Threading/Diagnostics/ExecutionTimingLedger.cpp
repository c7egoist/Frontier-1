/*============================================================================================================================================
                                                    EXECUTIONTIMINGLEDGER.CPP
============================================================================================================================================*/
// 🧩 Implementation of the background-fed in-memory timing ledger and single shutdown persistence pass

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Threading/Diagnostics/ExecutionTimingLedger.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Frontier::Threading
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SINGLETON ACCESS
//------------------------------------------------------------------------------------------------------------------------

ExecutionTimingLedger& ExecutionTimingLedger::Instance() noexcept
{
    static ExecutionTimingLedger Singleton {};
    return Singleton;
}

ExecutionTimingLedger::~ExecutionTimingLedger() noexcept
{
    PersistAndStop();
}

void ExecutionTimingLedger::ConfigureOutputPath(std::filesystem::path OutputPath) noexcept
{
    std::lock_guard<std::mutex> Guard(QueueMutex);
    if (!OutputPath.empty())
    {
        ConfiguredPath = std::move(OutputPath);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTAKE & WORKER
//------------------------------------------------------------------------------------------------------------------------

void ExecutionTimingLedger::EnsureWorker() noexcept
{
    std::lock_guard<std::mutex> Guard(QueueMutex);
    if (Running)
    {
        return;
    }
    Running = true;
    Accepting = true;
    try
    {
        Worker = std::thread([this]() { WorkerLoop(); });
    }
    catch (...)
    {
        // 📝 On thread-creation failure the intake path still buffers in RAM for shutdown persistence.
        Running = false;
    }
}

void ExecutionTimingLedger::AppendTiming(std::string_view Label, float DurationMs, uint64_t FrameIndex) noexcept
{
    Enqueue(Label, {}, DurationMs, FrameIndex);
}

void ExecutionTimingLedger::AppendNote(std::string_view Label, std::string_view Detail, uint64_t FrameIndex) noexcept
{
    Enqueue(Label, Detail, 0.0f, FrameIndex);
}

void ExecutionTimingLedger::Enqueue(
    std::string_view Label,
    std::string_view Detail,
    float            DurationMs,
    uint64_t         FrameIndex) noexcept
{
    std::unique_lock<std::mutex> Lock(QueueMutex);
    if (!Accepting)
    {
        return;
    }
    LedgerEntry Entry {};
    Entry.Sequence   = NextSequence++;
    Entry.FrameIndex = FrameIndex;
    Entry.Stamp      = std::chrono::steady_clock::now();
    Entry.DurationMs = DurationMs;
    Entry.Label.assign(Label.data(), Label.size());
    Entry.Detail.assign(Detail.data(), Detail.size());
    Pending.push_back(std::move(Entry));
    Lock.unlock();
    EnsureWorker();
    QueueWake.notify_one();
}

void ExecutionTimingLedger::DrainPending() noexcept
{
    std::lock_guard<std::mutex> Guard(QueueMutex);
    while (!Pending.empty())
    {
        Recorded.push_back(std::move(Pending.front()));
        Pending.pop_front();
    }
}

void ExecutionTimingLedger::WorkerLoop() noexcept
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> Lock(QueueMutex);
            QueueWake.wait(Lock, [this]() { return !Pending.empty() || !Accepting; });
            if (Pending.empty() && !Accepting)
            {
                return;
            }
        }
        DrainPending();
        std::this_thread::yield();
    }
}

void ExecutionTimingLedger::PersistAndStop(std::filesystem::path OverridePath) noexcept
{
    {
        std::lock_guard<std::mutex> Guard(QueueMutex);
        if (!OverridePath.empty())
        {
            ConfiguredPath = std::move(OverridePath);
        }
        Accepting = false;
    }
    QueueWake.notify_all();
    if (Worker.joinable())
    {
        Worker.join();
    }
    DrainPending();

    std::lock_guard<std::mutex> Guard(QueueMutex);
    Aggregates.clear();
    for (const LedgerEntry& Entry : Recorded)
    {
        if (Entry.Detail.empty())
        {
            AggregateEntry& Target = Aggregates[Entry.Label];
            const double Sample = static_cast<double>(Entry.DurationMs);
            if (Target.Count == 0ull)
            {
                Target.MinimumMs = Sample;
                Target.MaximumMs = Sample;
            }
            else
            {
                Target.MinimumMs = std::min(Target.MinimumMs, Sample);
                Target.MaximumMs = std::max(Target.MaximumMs, Sample);
            }
            Target.TotalMs += Sample;
            ++Target.Count;
        }
    }
    WriteReport(ConfiguredPath);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SHUTDOWN REPORT
//------------------------------------------------------------------------------------------------------------------------

void ExecutionTimingLedger::WriteReport(const std::filesystem::path& OutputPath) const noexcept
{
    if (OutputPath.empty())
    {
        return;
    }
    std::error_code DirectoryError {};
    if (OutputPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputPath.parent_path(), DirectoryError);
    }
    std::ofstream Stream(OutputPath, std::ios::binary | std::ios::trunc);
    if (!Stream.is_open())
    {
        return;
    }

    Stream << "# Frontier Execution Timing Ledger\n\n";
    Stream << "Persisted once during application shutdown. All samples were held in RAM while running.\n\n";
    Stream << "| Stage | Samples | Total ms | Mean ms | Min ms | Max ms |\n";
    Stream << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    Stream << std::fixed << std::setprecision(4);
    for (const auto& [Label, Summary] : Aggregates)
    {
        const double Mean = (Summary.Count > 0ull) ? (Summary.TotalMs / static_cast<double>(Summary.Count)) : 0.0;
        Stream << "| " << Label << " | " << Summary.Count << " | " << Summary.TotalMs
               << " | " << Mean << " | " << Summary.MinimumMs << " | " << Summary.MaximumMs << " |\n";
    }

    Stream << "\n## Intake Sequence\n\n";
    Stream << "| Ordinal | Frame | Stage | Duration ms | Note |\n";
    Stream << "| ---: | ---: | --- | ---: | --- |\n";
    for (const LedgerEntry& Entry : Recorded)
    {
        std::string Escaped = Entry.Detail;
        for (char& Character : Escaped)
        {
            if (Character == '|')
            {
                Character = '/';
            }
            else if (Character == '\n' || Character == '\r')
            {
                Character = ' ';
            }
        }
        Stream << "| " << Entry.Sequence << " | " << Entry.FrameIndex << " | " << Entry.Label
               << " | " << static_cast<double>(Entry.DurationMs) << " | " << Escaped << " |\n";
    }
}

std::size_t ExecutionTimingLedger::QueryBufferedCount() const noexcept
{
    std::lock_guard<std::mutex> Guard(QueueMutex);
    return Recorded.size() + Pending.size();
}

bool ExecutionTimingLedger::QueryWorkerActive() const noexcept
{
    std::lock_guard<std::mutex> Guard(QueueMutex);
    return Running && Accepting;
}

} // namespace Frontier::Threading
