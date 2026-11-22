#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <vector>
#include <deque>
#include <string>
#include <cstdio>

namespace lsalt {

namespace {
void PlotHistory(const char* label, const std::deque<float>& history, const char* overlay) {
    if (history.empty()) {
        ImGui::TextDisabled("%s: no data yet", label);
        return;
    }
    std::vector<float> buf(history.begin(), history.end());
    ImGui::Text("%s", label);
    ImGui::PlotLines(("##" + std::string(label)).c_str(), buf.data(), static_cast<int>(buf.size()),
                      0, overlay, 0.0f, 100.0f, ImVec2(-1, 80));
}
} // namespace

void DrawMonitorPanel(AppState& state) {
    ImGui::TextWrapped("Real, measured system metrics (not simulated). Per-game FPS, frame time "
                        "and \"generated FPS\" fields stay blank until the Capture/Scaling/"
                        "Frame-Generation engines (Phase 2-4) are attached to a running game.");
    ImGui::Separator();

    char cpuOverlay[64];
    snprintf(cpuOverlay, sizeof(cpuOverlay), "%.1f%%", state.lastStats.systemCpuPercent);
    PlotHistory("CPU Utilization", state.perf->CpuHistory(), cpuOverlay);

    const double ramPercent = state.lastStats.totalRamBytes > 0
        ? (static_cast<double>(state.lastStats.usedRamBytes) / state.lastStats.totalRamBytes) * 100.0
        : 0.0;
    char ramOverlay[64];
    snprintf(ramOverlay, sizeof(ramOverlay), "%.1f%%", ramPercent);
    PlotHistory("RAM Utilization", state.perf->RamHistory(), ramOverlay);

    const double vramPercent = state.lastStats.vramBudgetBytes > 0
        ? (static_cast<double>(state.lastStats.vramUsedBytes) / state.lastStats.vramBudgetBytes) * 100.0
        : 0.0;
    char vramOverlay[64];
    snprintf(vramOverlay, sizeof(vramOverlay), "%.1f%%", vramPercent);
    PlotHistory("VRAM Utilization (active adapter)", state.perf->VramHistory(), vramOverlay);

    if (state.lastStats.gpuUtilValid) {
        char gpuOverlay[64];
        snprintf(gpuOverlay, sizeof(gpuOverlay), "%.1f%%", state.lastStats.gpuUtilPercent);
        PlotHistory("GPU Utilization (3D engine, system-wide)", state.perf->GpuUtilHistory(), gpuOverlay);
    } else {
        ImGui::TextDisabled("GPU Utilization: N/A (the \"GPU Engine\" PDH counter is not available on this system)");
    }

    ImGui::Spacing();
    ImGui::Separator();

    const bool liveRunning = state.frameGenEngine && state.frameGenEngine->IsEnabled();
    const FrameGenStats fgStats = state.frameGenEngine ? state.frameGenEngine->GetStats() : FrameGenStats{};

    ImGui::TextWrapped(liveRunning
        ? "Frame Generation live-engine metrics below are real, measured numbers from the synthetic "
          "test-pattern source (see the Frame Generation tab) -- there is still no real game attached "
          "(Capture Engine is Phase 2, not implemented)."
        : "Per-game FPS, frame time and \"generated FPS\" stay blank until the Capture Engine (Phase 2) "
          "is attached to a running game. Enable the Frame Generation tab's Live Engine for real "
          "synthetic-source metrics in the meantime.");

    ImGui::Columns(3, "fps_cols", false);
    ImGui::Text("Source (Original) FPS");
    if (liveRunning) ImGui::Text("%.1f", fgStats.sourceFps); else ImGui::TextDisabled("N/A (Phase 2)");
    ImGui::NextColumn();
    ImGui::Text("Generated FPS");
    if (liveRunning) ImGui::Text("%.1f", fgStats.generatedFps); else ImGui::TextDisabled("N/A (disabled)");
    ImGui::NextColumn();
    ImGui::Text("Displayed FPS");
    if (liveRunning) ImGui::Text("%.1f", fgStats.displayedFps); else ImGui::TextDisabled("N/A (Phase 2)");
    ImGui::Columns(1);

    if (liveRunning) {
        ImGui::Spacing();
        ImGui::Columns(3, "fg_cols", false);
        ImGui::Text("Frame Time");         ImGui::Text("%.2f ms", fgStats.lastFrameTimeMs);
        ImGui::NextColumn();
        ImGui::Text("Motion Est. GPU Time"); ImGui::Text("%.2f ms", fgStats.motionEstimationGpuMs);
        ImGui::NextColumn();
        ImGui::Text("Interpolation GPU Time"); ImGui::Text("%.2f ms", fgStats.interpolationGpuMs);
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Columns(3, "fg_cols2", false);
        ImGui::Text("Frames Generated");   ImGui::Text("%llu", static_cast<unsigned long long>(fgStats.framesGenerated));
        ImGui::NextColumn();
        ImGui::Text("Dropped Frames");     ImGui::Text("%llu", static_cast<unsigned long long>(fgStats.droppedFrames));
        ImGui::NextColumn();
        ImGui::Text("Detected Refresh Rate");
        const double hz = state.frameGenEngine->DetectedRefreshHz();
        if (hz > 0.0) ImGui::Text("%.0f Hz", hz); else ImGui::TextDisabled("unknown");
        ImGui::Columns(1);
    }
}

} // namespace lsalt
