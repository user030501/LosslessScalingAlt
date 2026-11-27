#include "BenchmarkHistory.h"
#include "../core/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>

namespace lsalt {

void to_json(nlohmann::json& j, const BenchmarkResult& r) {
    j = nlohmann::json{
        {"unixSeconds", r.unixSeconds}, {"gpuName", r.gpuName},
        {"width", r.width}, {"height", r.height},
        {"scalingAlgorithm", r.scalingAlgorithm}, {"qualityPreset", r.qualityPreset},
        {"sharpening", r.sharpening}, {"frameGenSettings", r.frameGenSettings},
        {"averageMs", r.averageMs}, {"medianMs", r.medianMs},
        {"minimumMs", r.minimumMs}, {"maximumMs", r.maximumMs},
        {"standardDeviationMs", r.standardDeviationMs},
        {"gpuExecutionMs", r.gpuExecutionMs}, {"cpuProcessingMs", r.cpuProcessingMs},
    };
}

void from_json(const nlohmann::json& j, BenchmarkResult& r) {
    r.unixSeconds = j.value("unixSeconds", int64_t{0});
    r.gpuName = j.value("gpuName", std::string{});
    r.width = j.value("width", uint32_t{0});
    r.height = j.value("height", uint32_t{0});
    r.scalingAlgorithm = j.value("scalingAlgorithm", std::string{"Unknown"});
    r.qualityPreset = j.value("qualityPreset", std::string{"Unknown"});
    r.sharpening = j.value("sharpening", std::string{"Unknown"});
    r.frameGenSettings = j.value("frameGenSettings", std::string{"Unknown"});
    r.averageMs = j.value("averageMs", 0.0);
    r.medianMs = j.value("medianMs", 0.0);
    r.minimumMs = j.value("minimumMs", 0.0);
    r.maximumMs = j.value("maximumMs", 0.0);
    r.standardDeviationMs = j.value("standardDeviationMs", 0.0);
    r.gpuExecutionMs = j.value("gpuExecutionMs", 0.0);
    r.cpuProcessingMs = j.value("cpuProcessingMs", 0.0);
}

namespace {
std::wstring BenchmarkDir() {
    PWSTR path = nullptr;
    std::wstring result = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = std::wstring(path) + L"\\LSAlt\\benchmarks";
        CoTaskMemFree(path);
    }
    std::error_code ec;
    std::filesystem::create_directories(result, ec);
    return result;
}
} // namespace

BenchmarkHistory::BenchmarkHistory() { Load(); }
std::wstring BenchmarkHistory::HistoryFilePath() const { return BenchmarkDir() + L"\\benchmark_history.json"; }

void BenchmarkHistory::Load() {
    std::ifstream file(HistoryFilePath());
    if (!file.is_open()) return;
    try {
        nlohmann::json j;
        file >> j;
        m_results = j.get<std::vector<BenchmarkResult>>();
    } catch (const std::exception& e) {
        Logger::Instance().Error("BenchmarkHistory", std::string("Failed to parse history: ") + e.what());
        m_results.clear();
    }
}

void BenchmarkHistory::Save() const {
    std::ofstream file(HistoryFilePath());
    if (!file.is_open()) {
        Logger::Instance().Error("BenchmarkHistory", "Could not open benchmark history for writing.");
        return;
    }
    nlohmann::json j = m_results;
    file << j.dump(2);
}

void BenchmarkHistory::Add(const BenchmarkResult& result) {
    m_results.push_back(result);
    Save();
}

void BenchmarkHistory::Clear() {
    m_results.clear();
    Save();
}

bool BenchmarkHistory::ExportCsv(const std::wstring& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "unix_seconds,gpu_name,width,height,scaling_algorithm,quality_preset,sharpening,"
            "frame_gen_settings,average_ms,median_ms,minimum_ms,maximum_ms,standard_deviation_ms,"
            "gpu_execution_ms,cpu_processing_ms\n";
    for (const auto& r : m_results) {
        file << r.unixSeconds << ",\"" << r.gpuName << "\"," << r.width << ',' << r.height
             << ",\"" << r.scalingAlgorithm << "\",\"" << r.qualityPreset << "\",\"" << r.sharpening
             << "\",\"" << r.frameGenSettings << "\"," << r.averageMs << ',' << r.medianMs << ','
             << r.minimumMs << ',' << r.maximumMs << ',' << r.standardDeviationMs << ','
             << r.gpuExecutionMs << ',' << r.cpuProcessingMs << '\n';
    }
    return file.good();
}

} // namespace lsalt
