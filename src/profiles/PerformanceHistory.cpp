#include "PerformanceHistory.h"
#include "../core/Logger.h"

#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <ctime>

namespace lsalt {

void to_json(nlohmann::json& j, const PerformanceSessionRecord& r) {
    j = nlohmann::json{
        {"gameName", r.gameName},
        {"startUnixSeconds", r.startUnixSeconds},
        {"durationSeconds", r.durationSeconds},
        {"avgSourceFps", r.avgSourceFps},
        {"avgDisplayedFps", r.avgDisplayedFps},
        {"avgFrameTimeMs", r.avgFrameTimeMs},
        {"peakVramUsedBytes", r.peakVramUsedBytes},
        {"avgGpuUtilPercent", r.avgGpuUtilPercent},
        {"gpuUtilEverValid", r.gpuUtilEverValid},
        {"droppedFrames", r.droppedFrames},
    };
}

void from_json(const nlohmann::json& j, PerformanceSessionRecord& r) {
    r.gameName = j.value("gameName", std::string{});
    r.startUnixSeconds = j.value("startUnixSeconds", static_cast<int64_t>(0));
    r.durationSeconds = j.value("durationSeconds", 0.0);
    r.avgSourceFps = j.value("avgSourceFps", 0.0);
    r.avgDisplayedFps = j.value("avgDisplayedFps", 0.0);
    r.avgFrameTimeMs = j.value("avgFrameTimeMs", 0.0);
    r.peakVramUsedBytes = j.value("peakVramUsedBytes", static_cast<uint64_t>(0));
    r.avgGpuUtilPercent = j.value("avgGpuUtilPercent", 0.0);
    r.gpuUtilEverValid = j.value("gpuUtilEverValid", false);
    r.droppedFrames = j.value("droppedFrames", static_cast<uint64_t>(0));
}

namespace {
std::wstring HistoryDir() {
    PWSTR path = nullptr;
    std::wstring result = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = std::wstring(path) + L"\\LSAlt\\history";
        CoTaskMemFree(path);
    }
    std::error_code ec;
    std::filesystem::create_directories(result, ec);
    return result;
}
} // namespace

PerformanceHistory::PerformanceHistory() { Load(); }

std::wstring PerformanceHistory::HistoryFilePath() const { return HistoryDir() + L"\\sessions.json"; }

double PerformanceHistory::NowSeconds() {
    static const double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}

void PerformanceHistory::Load() {
    std::ifstream file(HistoryFilePath());
    if (!file.is_open()) return;
    try {
        nlohmann::json j;
        file >> j;
        m_records = j.get<std::vector<PerformanceSessionRecord>>();
    } catch (const std::exception& e) {
        Logger::Instance().Error("PerformanceHistory", std::string("Failed to parse sessions.json: ") + e.what());
        m_records.clear();
    }
}

void PerformanceHistory::Save() const {
    std::ofstream file(HistoryFilePath());
    if (!file.is_open()) {
        Logger::Instance().Error("PerformanceHistory", "Could not open sessions.json for writing.");
        return;
    }
    nlohmann::json j = m_records;
    file << j.dump(2);
}

void PerformanceHistory::BeginSession(const std::string& gameName) {
    if (m_active) EndSession();
    m_active = true;
    m_gameName = gameName;
    m_startSeconds = NowSeconds();
    m_startUnixSeconds = static_cast<int64_t>(std::time(nullptr));
    m_sampleCount = 0;
    m_sumSourceFps = m_sumDisplayedFps = m_sumFrameTimeMs = m_sumGpuUtil = 0.0;
    m_gpuUtilSampleCount = 0;
    m_peakVram = 0;
    m_baselineDroppedFrames = 0;
    m_lastDroppedFrames = 0;
    Logger::Instance().Info("PerformanceHistory", "Session started: " + gameName);
}

void PerformanceHistory::RecordSample(double sourceFps, double displayedFps, double frameTimeMs, uint64_t vramUsedBytes,
                                        double gpuUtilPercent, bool gpuUtilValid, uint64_t droppedFrames) {
    if (!m_active) return;

    if (m_sampleCount == 0) m_baselineDroppedFrames = droppedFrames;
    m_lastDroppedFrames = droppedFrames;

    m_sumSourceFps += sourceFps;
    m_sumDisplayedFps += displayedFps;
    m_sumFrameTimeMs += frameTimeMs;
    ++m_sampleCount;

    m_peakVram = std::max(m_peakVram, vramUsedBytes);

    if (gpuUtilValid) {
        m_sumGpuUtil += gpuUtilPercent;
        ++m_gpuUtilSampleCount;
    }
}

void PerformanceHistory::EndSession() {
    if (!m_active) return;
    m_active = false;

    PerformanceSessionRecord r;
    r.gameName = m_gameName;
    r.startUnixSeconds = m_startUnixSeconds;
    r.durationSeconds = NowSeconds() - m_startSeconds;
    if (m_sampleCount > 0) {
        r.avgSourceFps = m_sumSourceFps / static_cast<double>(m_sampleCount);
        r.avgDisplayedFps = m_sumDisplayedFps / static_cast<double>(m_sampleCount);
        r.avgFrameTimeMs = m_sumFrameTimeMs / static_cast<double>(m_sampleCount);
    }
    r.peakVramUsedBytes = m_peakVram;
    if (m_gpuUtilSampleCount > 0) {
        r.avgGpuUtilPercent = m_sumGpuUtil / static_cast<double>(m_gpuUtilSampleCount);
        r.gpuUtilEverValid = true;
    }
    r.droppedFrames = (m_lastDroppedFrames >= m_baselineDroppedFrames) ? (m_lastDroppedFrames - m_baselineDroppedFrames) : 0;

    // Sessions with essentially no samples (e.g. toggled on and off within
    // the same frame) aren't meaningful history -- skip rather than
    // clutter the record with a near-zero-duration entry.
    if (m_sampleCount > 0 && r.durationSeconds > 0.5) {
        m_records.push_back(r);
        Save();
        Logger::Instance().Info("PerformanceHistory",
            "Session ended: " + r.gameName + " (" + std::to_string(r.durationSeconds) + "s, " +
            std::to_string(m_sampleCount) + " samples).");
    }
}

double PerformanceHistory::CurrentSessionDurationSeconds() const {
    return m_active ? (NowSeconds() - m_startSeconds) : 0.0;
}

void PerformanceHistory::ClearHistory() {
    m_records.clear();
    Save();
    Logger::Instance().Info("PerformanceHistory", "Performance history cleared.");
}

bool PerformanceHistory::ExportCsv(const std::wstring& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("PerformanceHistory", "Could not open CSV export path for writing.");
        return false;
    }
    file << "game_name,start_unix_seconds,duration_seconds,avg_source_fps,avg_displayed_fps,avg_frame_time_ms,"
            "peak_vram_mb,avg_gpu_util_percent,dropped_frames\n";
    for (const auto& r : m_records) {
        file << '"' << r.gameName << "\"," << r.startUnixSeconds << ',' << r.durationSeconds << ','
             << r.avgSourceFps << ',' << r.avgDisplayedFps << ',' << r.avgFrameTimeMs << ','
             << (r.peakVramUsedBytes / (1024.0 * 1024.0)) << ','
             << (r.gpuUtilEverValid ? std::to_string(r.avgGpuUtilPercent) : std::string("N/A")) << ','
             << r.droppedFrames << '\n';
    }
    return file.good();
}

} // namespace lsalt
