#pragma once
#include <cstdint>
#include <deque>
#include <unordered_set>

namespace lsalt {

struct FrameSchedulerStats {
    uint64_t submitted = 0;
    uint64_t presented = 0;
    uint64_t dropped = 0;
    uint64_t duplicate = 0;
    uint64_t late = 0;
    uint64_t queueDepth = 0;
};

class FrameScheduler {
public:
    static constexpr size_t kMaxQueueDepth = 2;

    void Reset();
    void SetRefreshRate(double refreshHz);
    void SetLateTolerance(double seconds);

    bool Submit(uint64_t sequence, double timestampSeconds);
    bool TakeDueFrame(double nowSeconds);
    const FrameSchedulerStats& Stats() const { return m_stats; }
    size_t QueueDepth() const { return m_queue.size(); }

private:
    struct QueuedFrame {
        uint64_t sequence;
        double timestampSeconds;
    };

    std::deque<QueuedFrame> m_queue;
    std::unordered_set<uint64_t> m_recentSequences;
    uint64_t m_lastSequence = 0;
    double m_lastPresentedSeconds = 0.0;
    bool m_hasPresented = false;
    double m_intervalSeconds = 1.0 / 60.0;
    double m_lateToleranceSeconds = 0.004;
    FrameSchedulerStats m_stats;
};

} // namespace lsalt
