#pragma once
#include <cstdint>
#include <deque>
#include <windows.h>

namespace lsalt {

class GpuDeviceManager;

// Snapshot of real, measured system metrics. There is no game FPS data
// here yet -- that requires the Capture Engine (Phase 2) to be attached to
// a target process. Showing a fabricated FPS number would violate the
// project's "no fake counters" requirement, so FramePacingStats (below)
// stays empty until Phase 2/3 land.
struct SystemStats {
    double systemCpuPercent = 0.0;   // whole-machine CPU utilization
    double processCpuPercent = 0.0;  // this process only
    uint64_t totalRamBytes = 0;
    uint64_t usedRamBytes = 0;
    uint64_t processRamBytes = 0;    // this process' working set
    uint64_t vramUsedBytes = 0;      // 0 if unavailable
    uint64_t vramBudgetBytes = 0;    // 0 if unavailable
};

// Polls Win32/DXGI performance counters on demand (call Sample() once per
// UI frame or on a timer). Keeps a rolling history per metric for the
// Performance Monitor panel's graphs.
class PerfMonitor {
public:
    explicit PerfMonitor(GpuDeviceManager* gpuManager, size_t historyLength = 240);

    SystemStats Sample();

    const std::deque<float>& CpuHistory() const { return m_cpuHistory; }
    const std::deque<float>& RamHistory() const { return m_ramHistory; }
    const std::deque<float>& VramHistory() const { return m_vramHistory; }

private:
    GpuDeviceManager* m_gpuManager;
    size_t m_historyLength;

    ULARGE_INTEGER m_prevSysIdle{};
    ULARGE_INTEGER m_prevSysKernel{};
    ULARGE_INTEGER m_prevSysUser{};
    ULONGLONG m_prevProcKernel = 0;
    ULONGLONG m_prevProcUser = 0;
    ULONGLONG m_prevSampleTimeMs = 0;
    bool m_hasPrevSample = false;

    std::deque<float> m_cpuHistory;
    std::deque<float> m_ramHistory;
    std::deque<float> m_vramHistory;

    void PushHistory(std::deque<float>& history, float value);
};

} // namespace lsalt
