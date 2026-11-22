#include "FramePacing.h"
#include "../core/Logger.h"
#include <algorithm>

namespace lsalt {

namespace {
double QpcFrequency() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(freq.QuadPart);
}
} // namespace

double FramePacing::NowSeconds() {
    static const double freq = QpcFrequency();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / freq;
}

bool FramePacing::Initialize(ID3D11Device* device, HWND windowForRefreshRate) {
    m_refreshHz = 0.0;
    if (windowForRefreshRate) {
        HMONITOR monitor = MonitorFromWindow(windowForRefreshRate, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            DEVMODEW devMode{};
            devMode.dmSize = sizeof(devMode);
            if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
                if (devMode.dmDisplayFrequency > 1) {
                    m_refreshHz = static_cast<double>(devMode.dmDisplayFrequency);
                }
            }
        }
    }

    for (auto& slot : m_timingSlots) {
        D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC tsDesc{D3D11_QUERY_TIMESTAMP, 0};
        device->CreateQuery(&disjointDesc, slot.disjoint.ReleaseAndGetAddressOf());
        device->CreateQuery(&tsDesc, slot.tsStart.ReleaseAndGetAddressOf());
        device->CreateQuery(&tsDesc, slot.tsAfterMotion.ReleaseAndGetAddressOf());
        device->CreateQuery(&tsDesc, slot.tsAfterInterp.ReleaseAndGetAddressOf());
        slot.pending = false;
    }

    Reset();
    return true;
}

void FramePacing::Reset() {
    m_sourceTimestamps.clear();
    m_generatedTimestamps.clear();
    m_displayedTimestamps.clear();
    m_lastSourceTimestamp = 0.0;
    m_avgSourceIntervalSec = 0.0;
    m_droppedFrames = 0;
    m_framesGenerated = 0;
    m_lastMotionEstimationMs = 0.0;
    m_lastInterpolationMs = 0.0;
}

void FramePacing::PruneOld(std::deque<double>& timestamps) const {
    const double now = NowSeconds();
    while (!timestamps.empty() && (now - timestamps.front()) > 2.0) {
        timestamps.pop_front();
    }
}

double FramePacing::RateInWindow(const std::deque<double>& timestamps, double windowSeconds) const {
    const double now = NowSeconds();
    size_t count = 0;
    for (auto it = timestamps.rbegin(); it != timestamps.rend(); ++it) {
        if (now - *it > windowSeconds) break;
        ++count;
    }
    return static_cast<double>(count) / windowSeconds;
}

void FramePacing::RecordSourceFrame() {
    const double now = NowSeconds();
    if (m_lastSourceTimestamp > 0.0) {
        const double gap = now - m_lastSourceTimestamp;
        if (m_avgSourceIntervalSec <= 0.0) {
            m_avgSourceIntervalSec = gap;
        } else {
            if (gap > m_avgSourceIntervalSec * 1.8) {
                ++m_droppedFrames;
            }
            m_avgSourceIntervalSec = m_avgSourceIntervalSec * 0.9 + gap * 0.1;
        }
    }
    m_lastSourceTimestamp = now;
    m_sourceTimestamps.push_back(now);
    PruneOld(m_sourceTimestamps);
}

void FramePacing::RecordDisplayedFrame() {
    m_displayedTimestamps.push_back(NowSeconds());
    PruneOld(m_displayedTimestamps);
}

void FramePacing::RecordGeneratedFrame() {
    ++m_framesGenerated;
    m_generatedTimestamps.push_back(NowSeconds());
    PruneOld(m_generatedTimestamps);
}

void FramePacing::BeginGpuTiming(ID3D11DeviceContext* context) {
    PollOldestTiming(context);

    TimingSlot& slot = m_timingSlots[m_timingWriteIndex];
    if (slot.pending || !slot.disjoint) return; // ring full or not initialized; skip this frame's timing

    context->Begin(slot.disjoint.Get());
    context->End(slot.tsStart.Get());
}

void FramePacing::MarkMotionEstimationDone(ID3D11DeviceContext* context) {
    TimingSlot& slot = m_timingSlots[m_timingWriteIndex];
    if (slot.pending || !slot.tsAfterMotion) return;
    context->End(slot.tsAfterMotion.Get());
}

void FramePacing::MarkInterpolationDone(ID3D11DeviceContext* context) {
    TimingSlot& slot = m_timingSlots[m_timingWriteIndex];
    if (slot.pending || !slot.tsAfterInterp) return;
    context->End(slot.tsAfterInterp.Get());
    context->End(slot.disjoint.Get());
    slot.pending = true;
    m_timingWriteIndex = (m_timingWriteIndex + 1) % kTimingSlotCount;
}

void FramePacing::PollOldestTiming(ID3D11DeviceContext* context) {
    TimingSlot& slot = m_timingSlots[m_timingReadIndex];
    if (!slot.pending) return;

    // Non-blocking poll: GetData with no flags returns S_FALSE if the GPU
    // hasn't finished the query range yet, which we simply try again next
    // frame rather than stalling the caller.
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    HRESULT hr = context->GetData(slot.disjoint.Get(), &disjointData, sizeof(disjointData), 0);
    if (hr != S_OK) return;

    UINT64 tStart = 0, tAfterMotion = 0, tAfterInterp = 0;
    bool ok = context->GetData(slot.tsStart.Get(), &tStart, sizeof(tStart), 0) == S_OK &&
              context->GetData(slot.tsAfterMotion.Get(), &tAfterMotion, sizeof(tAfterMotion), 0) == S_OK &&
              context->GetData(slot.tsAfterInterp.Get(), &tAfterInterp, sizeof(tAfterInterp), 0) == S_OK;

    if (ok && !disjointData.Disjoint && disjointData.Frequency > 0) {
        const double freq = static_cast<double>(disjointData.Frequency);
        m_lastMotionEstimationMs = static_cast<double>(tAfterMotion - tStart) / freq * 1000.0;
        m_lastInterpolationMs = static_cast<double>(tAfterInterp - tAfterMotion) / freq * 1000.0;
    }

    slot.pending = false;
    m_timingReadIndex = (m_timingReadIndex + 1) % kTimingSlotCount;
}

void FramePacing::ResetRunningFpsStats() {
    m_fpsSampleCount = 0;
    m_fpsSum = 0.0;
    m_fpsMin = 0.0;
    m_fpsMax = 0.0;
}

void FramePacing::FillStats(FrameGenStats& stats) {
    stats.sourceFps = RateInWindow(m_sourceTimestamps, 1.0);
    stats.generatedFps = RateInWindow(m_generatedTimestamps, 1.0);
    stats.displayedFps = RateInWindow(m_displayedTimestamps, 1.0);

    // Phase 5 fix: this used to be derived from displayedFps (1000/displayedFps),
    // which was itself a derived (3x generatedFps) number -- a double
    // derivation masquerading as a measurement. Now that displayedFps is
    // an independent real measurement of THIS APP'S OWN present cadence
    // (see FrameGenTypes.h), deriving frame time from it would make "Frame
    // Time" track vsync/UI refresh rate instead of actual content
    // production rate. Base it on generatedFps (falling back to
    // sourceFps) instead -- the real interval between successive pieces
    // of new content, independent of how often this app's own window
    // happens to present.
    const double contentFps = (stats.generatedFps > 0.0) ? stats.generatedFps : stats.sourceFps;
    stats.lastFrameTimeMs = contentFps > 0.0 ? 1000.0 / contentFps : 0.0;
    stats.motionEstimationGpuMs = m_lastMotionEstimationMs;
    stats.interpolationGpuMs = m_lastInterpolationMs;
    stats.totalGpuMs = m_lastMotionEstimationMs + m_lastInterpolationMs;
    stats.framesGenerated = m_framesGenerated;
    stats.droppedFrames = m_droppedFrames;

    // Real GPU submission backlog: how many of the timing ring's slots are
    // still waiting on the GPU to finish (PollOldestTiming hasn't been able
    // to read them back yet). Phase 2 left this field wired to a setter
    // nothing ever called, which made it a permanent, misleading 0; this is
    // the actual bounded in-flight-work count instead.
    size_t pending = 0;
    for (const auto& slot : m_timingSlots) {
        if (slot.pending) ++pending;
    }
    stats.queueDepth = pending;

    // Running avg/min/max over samples where something was actually being
    // displayed -- skipping zero-fps idle samples so these reflect "while
    // running", not a mix dominated by idle periods (GetStats() is called
    // every UI frame regardless of whether Live Engine is on).
    if (stats.displayedFps > 0.01) {
        if (m_fpsSampleCount == 0) {
            m_fpsMin = m_fpsMax = stats.displayedFps;
        } else {
            m_fpsMin = std::min(m_fpsMin, stats.displayedFps);
            m_fpsMax = std::max(m_fpsMax, stats.displayedFps);
        }
        m_fpsSum += stats.displayedFps;
        ++m_fpsSampleCount;

        m_frameTimeHistory.push_back(static_cast<float>(stats.lastFrameTimeMs));
        while (m_frameTimeHistory.size() > kFrameTimeHistoryLength) m_frameTimeHistory.pop_front();
    }
    stats.avgDisplayedFpsRunning = (m_fpsSampleCount > 0) ? (m_fpsSum / static_cast<double>(m_fpsSampleCount)) : 0.0;
    stats.minDisplayedFpsRunning = m_fpsMin;
    stats.maxDisplayedFpsRunning = m_fpsMax;
}

} // namespace lsalt
