#include "BenchmarkRunner.h"
#include "../profiles/GameProfile.h"
#include "../framegen/FramePacing.h"
#include "../core/Logger.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>

namespace lsalt {

// ==================== SyntheticSource ====================
SyntheticSource::SyntheticSource(float targetFps)
    : m_targetFps(targetFps) {}

void SyntheticSource::Start() {
    if (m_running.exchange(true)) return; // already running
    m_thread = std::thread(&SyntheticSource::ThreadLoop, this);
}

void SyntheticSource::Stop() {
    if (!m_running.exchange(false)) return; // not running
    if (m_thread.joinable())
        m_thread.join();
}

double SyntheticSource::GetLastFrameTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastFrameTime;
}

void SyntheticSource::ThreadLoop() {
    const double framePeriod = 1.0 / static_cast<double>(m_targetFps);
    while (m_running) {
        auto start = std::chrono::high_resolution_clock::now();
        // Simulate rendering work – very light sleep.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastFrameTime = elapsed.count();
        }
        double remaining = framePeriod - elapsed.count();
        if (remaining > 0)
            std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
    }
}

// ==================== BenchmarkRunner ====================
BenchmarkRunner::BenchmarkRunner()
    : m_source(60.f) {}

BenchmarkRunner::~BenchmarkRunner() {
    Stop();
}

// Helper to compute statistics – testable.
BenchmarkRunner::Stats BenchmarkRunner::ComputeStats(const std::vector<double>& samples) {
    Stats s;
    if (samples.empty()) {
        s.averageMs = s.medianMs = s.minMs = s.maxMs = s.stddevMs = -1.0;
        return s;
    }
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    s.averageMs = sum / samples.size();
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    s.medianMs = sorted[sorted.size() / 2];
    s.minMs = sorted.front();
    s.maxMs = sorted.back();
    double var = 0.0;
    for (double v : samples) {
        double d = v - s.averageMs;
        var += d * d;
    }
    s.stddevMs = std::sqrt(var / samples.size());
    return s;
}

void BenchmarkRunner::Start(const GameProfile& profile, const Config& cfg) {
    if (IsRunning()) return; // prevent concurrent runs
    m_config = cfg;
    m_currentIteration = 0;
    m_running = true;
    m_state.store(BenchmarkState::Running);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimes.clear();
    }
    float targetFps = static_cast<float>(profile.fpsLimit > 0 ? profile.fpsLimit : 60);
    m_source = SyntheticSource(targetFps);
    m_thread = std::thread(&BenchmarkRunner::RunLoop, this, profile);
}

void BenchmarkRunner::Stop() {
    if (!IsRunning()) return;
    // Signal cancellation.
    m_running = false;
    m_source.Stop();
    if (m_thread.joinable())
        m_thread.join();
    // If the loop didn't set Completed, treat as Cancelled.
    if (m_state.load() != BenchmarkState::Completed)
        m_state.store(BenchmarkState::Cancelled);
    ComputeResult();
    if (m_state.load() == BenchmarkState::Completed) {
        try {
            m_history.Add(m_result);
        } catch (const std::exception& e) {
            Logger::Instance().Error("BenchmarkRunner", std::string("Failed to save benchmark: ") + e.what());
        }
    }
}

bool BenchmarkRunner::IsRunning() const {
    return m_running.load();
}

void BenchmarkRunner::RunLoop(const GameProfile& /*profile*/) {
    try {
        int total = m_config.warmupIterations + m_config.benchmarkIterations;
        for (int i = 0; i < total && m_running; ++i) {
            m_currentIteration = i + 1; // 1‑based for UI
            m_source.Start();
            int frames = 0;
            while (frames < m_config.framesPerIteration && m_running) {
                double ft = m_source.GetLastFrameTime();
                if (ft > 0.0) {
                    if (i >= m_config.warmupIterations) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_frameTimes.push_back(ft * 1000.0); // ms
                    }
                    ++frames;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            m_source.Stop();
        }
        if (m_running) {
            m_state.store(BenchmarkState::Completed);
        }
    } catch (const std::exception&) {
        m_state.store(BenchmarkState::Failed);
    }
    m_running = false;
}

void BenchmarkRunner::ComputeResult() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.empty()) {
        m_result.averageMs = m_result.medianMs = -1.0;
        return;
    }
    Stats stats = ComputeStats(m_frameTimes);
    m_result.averageMs = stats.averageMs;
    m_result.medianMs = stats.medianMs;
    m_result.minimumMs = stats.minMs;
    m_result.maximumMs = stats.maxMs;
    m_result.standardDeviationMs = stats.stddevMs;
    // Populate placeholder fields.
    m_result.unixSeconds = static_cast<int64_t>(std::time(nullptr));
    m_result.gpuName = "Synthetic";
    m_result.width = 1920;
    m_result.height = 1080;
    m_result.scalingAlgorithm = "Synthetic";
    m_result.qualityPreset = "Synthetic";
    m_result.sharpening = "None";
    m_result.frameGenSettings = "Synthetic";
    m_result.gpuExecutionMs = -1.0;
    m_result.cpuProcessingMs = -1.0;
}

} // namespace lsalt