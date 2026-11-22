#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <deque>
#include <array>
#include "FrameGenTypes.h"

namespace lsalt {

// CPU-side bookkeeping for the frame generation pipeline: real display
// refresh rate (queried from the OS, not assumed), rolling FPS counters
// measured from actual timestamps, dropped/stalled-source detection, and
// non-blocking GPU dispatch timing via D3D11 timestamp queries. Nothing
// here is simulated -- every number in FrameGenStats comes from a real
// clock or a real GPU query.
class FramePacing {
public:
    static constexpr size_t kMaxQueueDepth = 2; // bounded; never let the queue grow unbounded

    bool Initialize(ID3D11Device* device, HWND windowForRefreshRate);
    void Reset();

    // Call once per real source frame pair (A, B) made available to the engine.
    void RecordSourceFrame();
    // Call once per frame (real or generated) actually handed to a presenter.
    void RecordDisplayedFrame();
    // Call once per interpolated frame produced.
    void RecordGeneratedFrame();

    // Brackets GPU work for timing. Safe to call every frame; internally
    // polls a ring of queries so the render thread is never blocked
    // waiting on the GPU.
    void BeginGpuTiming(ID3D11DeviceContext* context);
    void MarkMotionEstimationDone(ID3D11DeviceContext* context);
    void MarkInterpolationDone(ID3D11DeviceContext* context);

    double DetectedRefreshHz() const { return m_refreshHz; }

    // Phase 4: real per-sample frame-time history for the Performance
    // Monitor's frame-time graph (same lastFrameTimeMs FillStats already
    // computes, just retained instead of discarded).
    const std::deque<float>& FrameTimeHistoryMs() const { return m_frameTimeHistory; }

    // Mutates running avg/min/max tracking (see FillStats), so not const.
    void FillStats(FrameGenStats& stats);
    // Phase 4: lets the UI start a fresh avg/min/max window on demand
    // (e.g. a "Reset Stats" button) without needing a full Reset() of
    // pacing/timing state.
    void ResetRunningFpsStats();

private:
    struct TimingSlot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> tsStart;
        Microsoft::WRL::ComPtr<ID3D11Query> tsAfterMotion;
        Microsoft::WRL::ComPtr<ID3D11Query> tsAfterInterp;
        bool pending = false;
    };

    static double NowSeconds();
    void PollOldestTiming(ID3D11DeviceContext* context);
    double RateInWindow(const std::deque<double>& timestamps, double windowSeconds) const;
    void PruneOld(std::deque<double>& timestamps) const;

    double m_refreshHz = 0.0;

    std::deque<double> m_sourceTimestamps;
    std::deque<double> m_generatedTimestamps;
    std::deque<double> m_displayedTimestamps;

    double m_lastSourceTimestamp = 0.0;
    double m_avgSourceIntervalSec = 0.0;
    uint64_t m_droppedFrames = 0;
    uint64_t m_framesGenerated = 0;

    static constexpr int kTimingSlotCount = 4;
    std::array<TimingSlot, kTimingSlotCount> m_timingSlots;
    int m_timingWriteIndex = 0;
    int m_timingReadIndex = 0;

    double m_lastMotionEstimationMs = 0.0;
    double m_lastInterpolationMs = 0.0;

    uint64_t m_fpsSampleCount = 0;
    double m_fpsSum = 0.0;
    double m_fpsMin = 0.0;
    double m_fpsMax = 0.0;

    std::deque<float> m_frameTimeHistory;
    static constexpr size_t kFrameTimeHistoryLength = 240;
};

} // namespace lsalt
