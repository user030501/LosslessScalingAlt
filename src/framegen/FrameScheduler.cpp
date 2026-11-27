#include "FrameScheduler.h"
#include <algorithm>

namespace lsalt {

void FrameScheduler::Reset() {
    m_queue.clear();
    m_recentSequences.clear();
    m_lastSequence = 0;
    m_lastPresentedSeconds = 0.0;
    m_hasPresented = false;
    m_stats = FrameSchedulerStats{};
}

void FrameScheduler::SetRefreshRate(double refreshHz) {
    if (refreshHz >= 20.0 && refreshHz <= 360.0) {
        m_intervalSeconds = 1.0 / refreshHz;
    }
}

void FrameScheduler::SetLateTolerance(double seconds) {
    m_lateToleranceSeconds = std::max(0.0, seconds);
}

bool FrameScheduler::Submit(uint64_t sequence, double timestampSeconds) {
    ++m_stats.submitted;

    if (m_recentSequences.find(sequence) != m_recentSequences.end()) {
        ++m_stats.duplicate;
        return false;
    }

    if (m_queue.size() >= kMaxQueueDepth) {
        m_queue.pop_front();
        ++m_stats.dropped;
    }

    m_queue.push_back(QueuedFrame{sequence, timestampSeconds});
    m_recentSequences.insert(sequence);
    if (m_recentSequences.size() > 256) {
        m_recentSequences.erase(m_recentSequences.begin());
    }
    m_lastSequence = sequence;
    m_stats.queueDepth = m_queue.size();
    return true;
}

bool FrameScheduler::TakeDueFrame(double nowSeconds) {
    if (m_queue.empty()) return false;

    if (!m_hasPresented ||
        nowSeconds - m_lastPresentedSeconds >= m_intervalSeconds - 0.00025) {
        const QueuedFrame frame = m_queue.front();
        m_queue.pop_front();

        if (m_hasPresented &&
            nowSeconds - m_lastPresentedSeconds > m_intervalSeconds + m_lateToleranceSeconds) {
            ++m_stats.late;
        }

        m_lastPresentedSeconds = nowSeconds;
        m_hasPresented = true;
        ++m_stats.presented;
        m_stats.queueDepth = m_queue.size();
        return true;
    }
    return false;
}

} // namespace lsalt
