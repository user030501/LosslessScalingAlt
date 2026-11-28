#include "BenchmarkRunner.h"
#include "../profiles/GameProfile.h"
#include "../framegen/FramePacing.h"
#include "../core/Logger.h"
#include <algorithm>
#include <numeric>
#include <chrono>
#include <ctime>

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

void BenchmarkRunner::Start(const GameProfile& profile, const Config& cfg) {
    if (IsRunning()) return; // prevent concurrent runs
    m_config = cfg;
    m_currentIteration = 0;
    m_running = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimes.clear();
    }
    // Use profile's FPS limit if provided, else default 60.
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
    // Only compute results if we have at least one measured iteration.
    ComputeResult();
    try {
        m_history.Add(m_result);
    } catch (const std::exception& e) {
        Logger::Instance().Error("BenchmarkRunner", std::string("Failed to save benchmark: ") + e.what());
    }
}

bool BenchmarkRunner::IsRunning() const {
    return m_running.load();
}

void BenchmarkRunner::RunLoop(const GameProfile& /*profile*/) {
    // Iterate warm‑up + measured passes.
    int total = m_config.warmupIterations + m_config.benchmarkIterations;
    for (int i = 0; i < total && m_running; ++i) {
        m_currentIteration = i + 1; // 1‑based for UI display
        // Reset synthetic source for each iteration.
        m_source.Start();
        int frames = 0;
        while (frames < m_config.framesPerIteration && m_running) {
            double ft = m_source.GetLastFrameTime();
            if (ft > 0.0) {
                // Record only for measured iterations (skip warm‑up).
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
    // Ensure flag cleared – the outer Stop() will also set it, but in case Stop() was not called.
    m_running = false;
}

void BenchmarkRunner::ComputeResult() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.empty()) {
        // No measured data – mark as invalid.
        m_result.averageMs = m_result.medianMs = -1.0;
        return;
    }
    double sum = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0);
    m_result.averageMs = sum / m_frameTimes.size();
    std::vector<double> sorted = m_frameTimes;
    std::sort(sorted.begin(), sorted.end());
    m_result.medianMs = sorted[sorted.size() / 2];
    m_result.minimumMs = sorted.front();
    m_result.maximumMs = sorted.back();
    double var = 0.0;
    for (double v : m_frameTimes) {
        double d = v - m_result.averageMs;
        var += d * d;
    }
    m_result.standardDeviationMs = std::sqrt(var / m_frameTimes.size());

    // Populate placeholder fields – real implementation will fill real GPU/CPU timing.
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

    : m_source(60.f) {}

BenchmarkRunner::~BenchmarkRunner() {
    Stop();
}

void BenchmarkRunner::Start(const GameProfile& profile) {
    if (IsRunning()) return;
    m_running = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimes.clear();
    }
    // Use profile's FPS limit if provided, else default 60.
    float targetFps = static_cast<float>(profile.fpsLimit > 0 ? profile.fpsLimit : 60);
    m_source = SyntheticSource(targetFps);
    m_thread = std::thread(&BenchmarkRunner::RunLoop, this, profile);
}

void BenchmarkRunner::Stop() {
    if (!IsRunning()) return;
    // Signal the loop to finish.
    m_running = false;
    m_source.Stop();
    if (m_thread.joinable())
        m_thread.join();
    ComputeResult();
    // Persist the result.
    try {
        m_history.Add(m_result);
    } catch (const std::exception& e) {
        Logger::Instance().Error("BenchmarkRunner", std::string("Failed to save benchmark: ") + e.what());
    }
}

bool BenchmarkRunner::IsRunning() const {
    return m_running.load();
}

void BenchmarkRunner::RunLoop(const GameProfile& /*profile*/) {
    m_source.Start();
    while (m_running) {
        double ft = m_source.GetLastFrameTime();
        if (ft > 0.0) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frameTimes.push_back(ft * 1000.0); // ms
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_source.Stop();
}

void BenchmarkRunner::ComputeResult() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.empty()) {
        // No data – mark as invalid.
        m_result.averageMs = m_result.medianMs = -1.0;
        return;
    }
    double sum = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0);
    m_result.averageMs = sum / m_frameTimes.size();
    std::vector<double> sorted = m_frameTimes;
    std::sort(sorted.begin(), sorted.end());
    m_result.medianMs = sorted[sorted.size() / 2];
    m_result.minimumMs = sorted.front();
    m_result.maximumMs = sorted.back();
    double var = 0.0;
    for (double v : m_frameTimes) {
        double d = v - m_result.averageMs;
        var += d * d;
    }
    m_result.standardDeviationMs = std::sqrt(var / m_frameTimes.size());

    // Populate the remaining fields with sane defaults – real implementation will replace them.
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
