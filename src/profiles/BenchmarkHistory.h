#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace lsalt {

struct BenchmarkResult {
    int64_t unixSeconds = 0;
    std::string gpuName;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string scalingAlgorithm;
    std::string qualityPreset;
    std::string sharpening;
    std::string frameGenSettings;
    double averageMs = 0.0;
    double medianMs = 0.0;
    double minimumMs = 0.0;
    double maximumMs = 0.0;
    double standardDeviationMs = 0.0;
    double gpuExecutionMs = 0.0;
    double cpuProcessingMs = 0.0;
};

void to_json(nlohmann::json& j, const BenchmarkResult& r);
void from_json(const nlohmann::json& j, BenchmarkResult& r);

class BenchmarkHistory {
public:
    BenchmarkHistory();
    void Add(const BenchmarkResult& result);
    void Clear();
    bool ExportCsv(const std::wstring& path) const;
    const std::vector<BenchmarkResult>& Results() const { return m_results; }
    std::wstring HistoryFilePath() const;

private:
    void Load();
    void Save() const;
    std::vector<BenchmarkResult> m_results;
};

} // namespace lsalt
