#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace lsalt {

// One finished monitoring session's summary. Every field is a running
// average/peak/total of values this app already measures in real time
// (PerfMonitor, FrameGenStats) -- nothing here is estimated or invented
// after the fact; see PerformanceHistory::RecordSample.
struct PerformanceSessionRecord {
    std::string gameName;
    int64_t startUnixSeconds = 0;
    double durationSeconds = 0.0;
    double avgSourceFps = 0.0;
    double avgDisplayedFps = 0.0;
    double avgFrameTimeMs = 0.0;
    uint64_t peakVramUsedBytes = 0;
    double avgGpuUtilPercent = 0.0;
    bool gpuUtilEverValid = false;
    uint64_t droppedFrames = 0;
};

void to_json(nlohmann::json& j, const PerformanceSessionRecord& r);
void from_json(const nlohmann::json& j, PerformanceSessionRecord& r);

// Phase 4: tracks one monitoring session at a time -- begun when the Live
// Frame Generation Engine is turned on (that's what actually produces real
// FrameGenStats today; see App::RenderFrame), labeled with whatever game
// GameSessionController currently has detected if any, else a label making
// clear it's the synthetic test source -- and appends a summarized record
// to disk when the session ends. History survives app restarts.
class PerformanceHistory {
public:
    PerformanceHistory();

    void BeginSession(const std::string& gameName);
    // Call once per Performance Monitor sample while a session is active
    // (SessionActive()). No-op otherwise.
    void RecordSample(double sourceFps, double displayedFps, double frameTimeMs, uint64_t vramUsedBytes,
                       double gpuUtilPercent, bool gpuUtilValid, uint64_t droppedFrames);
    void EndSession(); // no-op if no session is active

    bool SessionActive() const { return m_active; }
    double CurrentSessionDurationSeconds() const;
    const std::string& CurrentSessionGameName() const { return m_gameName; }

    const std::vector<PerformanceSessionRecord>& Records() const { return m_records; }
    void ClearHistory();
    // Returns false (and logs why) if the file couldn't be written --
    // never silently "succeeds" without a real file on disk.
    bool ExportCsv(const std::wstring& path) const;

    std::wstring HistoryFilePath() const;

private:
    void Load();
    void Save() const;
    static double NowSeconds();

    std::vector<PerformanceSessionRecord> m_records;

    bool m_active = false;
    std::string m_gameName;
    double m_startSeconds = 0.0;
    int64_t m_startUnixSeconds = 0;

    uint64_t m_sampleCount = 0;
    double m_sumSourceFps = 0.0;
    double m_sumDisplayedFps = 0.0;
    double m_sumFrameTimeMs = 0.0;
    double m_sumGpuUtil = 0.0;
    uint64_t m_gpuUtilSampleCount = 0;
    uint64_t m_peakVram = 0;
    uint64_t m_baselineDroppedFrames = 0;
    uint64_t m_lastDroppedFrames = 0;
};

} // namespace lsalt
