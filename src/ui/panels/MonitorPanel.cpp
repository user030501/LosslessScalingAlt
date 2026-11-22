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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Columns(3, "fps_cols", false);
    ImGui::Text("Original FPS");    ImGui::TextDisabled("N/A (Phase 2)");
    ImGui::NextColumn();
    ImGui::Text("Generated FPS");   ImGui::TextDisabled("N/A (Phase 4)");
    ImGui::NextColumn();
    ImGui::Text("Displayed FPS");   ImGui::TextDisabled("N/A (Phase 2)");
    ImGui::Columns(1);
}

} // namespace lsalt
