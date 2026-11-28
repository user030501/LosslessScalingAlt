// BenchmarkPanel.cpp
// UI panel for Phase 11 benchmark functionality.

#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <string>
#include <vector>

namespace lsalt {

static const std::vector<std::string> kScalingAlgorithms = {
    "Nearest",
    "Bilinear",
    "Bicubic",
    "Lanczos",
    "Synthetic",
};
static const std::vector<std::string> kQualityPresets = {
    "Low",
    "Medium",
    "High",
    "Ultra",
    "Synthetic",
};
static const std::vector<std::string> kSharpeningOptions = {
    "None",
    "Low",
    "Medium",
    "High",
    "Synthetic",
};
static const std::vector<std::string> kFrameGenOptions = {
    "Off",
    "On",
    "Synthetic",
};

void DrawBenchmarkPanel(AppState& state) {
    ImGui::Separator();
    ImGui::Text("Benchmark Runner");
    ImGui::Spacing();

    bool running = state.benchmarkRunner.IsRunning();

    // Store configuration locally while not running
    static BenchmarkRunner::Config cfg;
    if (!running) {
        ImGui::InputInt("Warm‑up iterations", &cfg.warmupIterations);
        ImGui::InputInt("Benchmark iterations", &cfg.benchmarkIterations);
        ImGui::InputInt("Frames per iteration", &cfg.framesPerIteration);
        // Simple combo boxes – values stored locally then applied on start.
        static int curScaling = 0;
        ImGui::Combo("Scaling algorithm", &curScaling, [] (void* data, int idx, const char** out_text) {
            const auto* vec = static_cast<const std::vector<std::string>*>(data);
            *out_text = (*vec)[idx].c_str();
            return true;
        }, (void*)&kScalingAlgorithms, (int)kScalingAlgorithms.size());
        static int curPreset = 2; // default Medium
        ImGui::Combo("Quality preset", &curPreset, [] (void* data, int idx, const char** out_text) {
            const auto* vec = static_cast<const std::vector<std::string>*>(data);
            *out_text = (*vec)[idx].c_str();
            return true;
        }, (void*)&kQualityPresets, (int)kQualityPresets.size());
        static int curSharpen = 0;
        ImGui::Combo("Sharpening", &curSharpen, [] (void* data, int idx, const char** out_text) {
            const auto* vec = static_cast<const std::vector<std::string>*>(data);
            *out_text = (*vec)[idx].c_str();
            return true;
        }, (void*)&kSharpeningOptions, (int)kSharpeningOptions.size());
        static int curFrameGen = 0;
        ImGui::Combo("Frame generation", &curFrameGen, [] (void* data, int idx, const char** out_text) {
            const auto* vec = static_cast<const std::vector<std::string>*>(data);
            *out_text = (*vec)[idx].c_str();
            return true;
        }, (void*)&kFrameGenOptions, (int)kFrameGenOptions.size());
    }

    // Start / Cancel buttons
    if (!running) {
        if (ImGui::Button("Start Benchmark", ImVec2(-1, 0))) {
            // Use the selected profile (fallback to first if none)
            GameProfile* profile = state.profiles.AtIndex(state.selectedProfileIndex);
            if (!profile) profile = &state.profiles.Profiles().front();
            state.benchmarkRunner.Start(*profile, cfg);
        }
    } else {
        if (ImGui::Button("Cancel Benchmark", ImVec2(-1, 0))) {
            state.benchmarkRunner.Stop();
            // Copy the latest result into the global history for UI display
            state.benchmarkHistory.Add(state.benchmarkRunner.LatestResult());
        }
    }

    ImGui::Separator();
    // Live status display
    ImGui::TextWrapped("Status: %s", running ? "Running" : "Idle");
    if (running) {
        ImGui::Text("Current iteration: %d / %d", state.benchmarkRunner.CurrentIteration(), state.benchmarkRunner.TotalIterations());
    }
    const auto& res = state.benchmarkRunner.LatestResult();
    auto fmt = [] (double v) { return v >= 0.0 ? std::to_string(v) : std::string("N/A"); };
    ImGui::Text("Average ms: %s", fmt(res.averageMs).c_str());
    ImGui::Text("Median ms: %s", fmt(res.medianMs).c_str());
    ImGui::Text("Min ms: %s", fmt(res.minimumMs).c_str());
    ImGui::Text("Max ms: %s", fmt(res.maximumMs).c_str());
    ImGui::Text("StdDev ms: %s", fmt(res.standardDeviationMs).c_str());
    ImGui::Text("GPU exec ms: %s", fmt(res.gpuExecutionMs).c_str());
    ImGui::Text("CPU proc ms: %s", fmt(res.cpuProcessingMs).c_str());

    // History list
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Benchmark History", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Clear History", ImVec2(-1, 0))) {
            state.benchmarkHistory.Clear();
        }
        if (ImGui::Button("Export CSV", ImVec2(-1, 0))) {
            std::wstring path = L"benchmark_history.csv";
            if (!state.benchmarkHistory.ExportCsv(path)) {
                state.statusMessage = "Failed to export CSV";
            } else {
                state.statusMessage = "CSV exported to benchmark_history.csv";
            }
        }
        const auto& results = state.benchmarkHistory.Results();
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            ImGui::BulletText("%zu: %s %ux%u – avg %.2f ms", i+1, r.gpuName.c_str(), r.width, r.height, r.averageMs);
        }
    }
}


} // namespace lsalt
