#include "PerfMonitor.h"
#include "../gpu/GpuDeviceManager.h"
#include "../core/Logger.h"
#include <psapi.h>
#include <vector>

namespace lsalt {

namespace {
uint64_t ToU64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}
} // namespace

PerfMonitor::PerfMonitor(GpuDeviceManager* gpuManager, size_t historyLength)
    : m_gpuManager(gpuManager), m_historyLength(historyLength) {
    if (PdhOpenQueryW(nullptr, 0, &m_gpuQuery) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(m_gpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &m_gpuUtilCounter) == ERROR_SUCCESS) {
            m_gpuPdhValid = true;
        } else {
            Logger::Instance().Warn("PerfMonitor", "GPU Engine PDH counter unavailable on this system; GPU utilization will read N/A.");
            PdhCloseQuery(m_gpuQuery);
            m_gpuQuery = nullptr;
        }
    }
}

PerfMonitor::~PerfMonitor() {
    if (m_gpuQuery) PdhCloseQuery(m_gpuQuery);
}

void PerfMonitor::PushHistory(std::deque<float>& history, float value) {
    history.push_back(value);
    while (history.size() > m_historyLength) history.pop_front();
}

SystemStats PerfMonitor::Sample() {
    SystemStats stats;

    // --- System-wide CPU usage, from GetSystemTimes deltas ---
    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        ULARGE_INTEGER idle{}, kernel{}, user{};
        idle.QuadPart = ToU64(idleFt);
        kernel.QuadPart = ToU64(kernelFt);
        user.QuadPart = ToU64(userFt);

        if (m_hasPrevSample) {
            const ULONGLONG idleDelta = idle.QuadPart - m_prevSysIdle.QuadPart;
            const ULONGLONG kernelDelta = kernel.QuadPart - m_prevSysKernel.QuadPart;
            const ULONGLONG userDelta = user.QuadPart - m_prevSysUser.QuadPart;
            const ULONGLONG totalDelta = kernelDelta + userDelta; // kernel includes idle on Windows
            if (totalDelta > 0) {
                const ULONGLONG busy = totalDelta - idleDelta;
                stats.systemCpuPercent = (static_cast<double>(busy) / static_cast<double>(totalDelta)) * 100.0;
            }
        }
        m_prevSysIdle = idle;
        m_prevSysKernel = kernel;
        m_prevSysUser = user;
    }

    // --- This process' CPU usage ---
    FILETIME creationFt{}, exitFt{}, procKernelFt{}, procUserFt{};
    if (GetProcessTimes(GetCurrentProcess(), &creationFt, &exitFt, &procKernelFt, &procUserFt)) {
        const ULONGLONG procKernel = ToU64(procKernelFt);
        const ULONGLONG procUser = ToU64(procUserFt);

        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME nowFt{};
        SystemTimeToFileTime(&st, &nowFt);
        const ULONGLONG nowMs = ToU64(nowFt) / 10000ULL;

        if (m_hasPrevSample) {
            const ULONGLONG procDelta = (procKernel - m_prevProcKernel) + (procUser - m_prevProcUser);
            const ULONGLONG wallDeltaMs = nowMs - m_prevSampleTimeMs;
            if (wallDeltaMs > 0) {
                SYSTEM_INFO sysInfo;
                GetSystemInfo(&sysInfo);
                const double procDeltaMs = static_cast<double>(procDelta) / 10000.0;
                stats.processCpuPercent = (procDeltaMs / static_cast<double>(wallDeltaMs)) * 100.0 / sysInfo.dwNumberOfProcessors;
            }
        }
        m_prevProcKernel = procKernel;
        m_prevProcUser = procUser;
        m_prevSampleTimeMs = nowMs;
    }

    m_hasPrevSample = true;

    // --- RAM ---
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        stats.totalRamBytes = memStatus.ullTotalPhys;
        stats.usedRamBytes = memStatus.ullTotalPhys - memStatus.ullAvailPhys;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        stats.processRamBytes = pmc.WorkingSetSize;
    }

    // --- VRAM (real DXGI query on the active adapter) ---
    if (m_gpuManager) {
        uint64_t used = 0, budget = 0;
        if (m_gpuManager->QueryVideoMemoryUsage(m_gpuManager->ActiveAdapterIndex(), used, budget)) {
            stats.vramUsedBytes = used;
            stats.vramBudgetBytes = budget;
        }
    }

    // --- GPU utilization (real PDH "GPU Engine" counter, 3D-engine instances summed system-wide) ---
    if (m_gpuPdhValid && PdhCollectQueryData(m_gpuQuery) == ERROR_SUCCESS) {
        DWORD bufferSize = 0, itemCount = 0;
        PDH_STATUS sizeStatus = PdhGetFormattedCounterArrayW(m_gpuUtilCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (sizeStatus == PDH_MORE_DATA && bufferSize > 0) {
            std::vector<uint8_t> buffer(bufferSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(m_gpuUtilCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS) {
                double sum3D = 0.0;
                for (DWORD i = 0; i < itemCount; ++i) {
                    if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !items[i].szName) continue;
                    std::wstring name(items[i].szName);
                    if (name.find(L"engtype_3D") != std::wstring::npos) {
                        sum3D += items[i].FmtValue.doubleValue;
                    }
                }
                stats.gpuUtilPercent = (sum3D > 100.0) ? 100.0 : sum3D;
                stats.gpuUtilValid = true;
            }
        }
    }

    // --- History for graphs ---
    PushHistory(m_cpuHistory, static_cast<float>(stats.systemCpuPercent));
    if (stats.totalRamBytes > 0) {
        PushHistory(m_ramHistory, static_cast<float>(stats.usedRamBytes) / static_cast<float>(stats.totalRamBytes) * 100.0f);
    }
    if (stats.vramBudgetBytes > 0) {
        PushHistory(m_vramHistory, static_cast<float>(stats.vramUsedBytes) / static_cast<float>(stats.vramBudgetBytes) * 100.0f);
    }
    if (stats.gpuUtilValid) {
        PushHistory(m_gpuUtilHistory, static_cast<float>(stats.gpuUtilPercent));
    }

    return stats;
}

} // namespace lsalt
