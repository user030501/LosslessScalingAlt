#pragma once

#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include "../profiles/BenchmarkHistory.h"
#include "../profiles/GameProfile.h"

namespace lsalt {

// Simple synthetic source placeholder – generates timestamps at a target FPS.
class SyntheticSource {
public:
    SyntheticSource(float targetFps = 60.f);
    void Start();
    void Stop();
    // Returns duration (seconds) of the last generated frame.
    double GetLastFrameTime() const;
private:
    float m_targetFps;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::mutex m_mutex;
    double m_lastFrameTime{0.0};
    void ThreadLoop();
};

// BenchmarkRunner drives a SyntheticSource, gathers timestamps via FramePacing,
// computes statistics and stores the result in BenchmarkHistory.

// Execution state of the benchmark runner.
enum class BenchmarkState {
    Idle,
    Running,
    Completed,
    Cancelled,
    Failed
};

// Duplicate definition removed
class BenchmarkRunner {
public:
    struct Config {
        int warmupIterations = 1;      // how many warm‑up runs (discarded)
        int benchmarkIterations = 5;  // number of measured runs
        int framesPerIteration = 100; // synthetic frames per iteration
    };

    BenchmarkRunner();
    ~BenchmarkRunner();

    // Begins an asynchronous benchmark run with the given profile settings and configuration.
    void Start(const GameProfile& profile, const Config& cfg = Config());
    // Requests stop – blocks until the run is finished and result persisted.
    void Stop();
    bool IsRunning() const;

    // UI helpers – expose current progress and the latest computed result.
    int CurrentIteration() const { return m_currentIteration; }
    int TotalIterations() const { return m_config.benchmarkIterations; }
    int WarmupIterations() const { return m_config.warmupIterations; }
    const BenchmarkResult& LatestResult() const { return m_result; }

private:
    void RunLoop(const GameProfile& profile);
    void ComputeResult();

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    SyntheticSource m_source;
    std::vector<double> m_frameTimes; // aggregated across measured iterations
    BenchmarkResult m_result;
    BenchmarkHistory m_history;
    std::mutex m_mutex;
    Config m_config;
    int m_currentIteration{0}; // 0‑based, includes warm‑up + measured
};

} // namespace lsalt
