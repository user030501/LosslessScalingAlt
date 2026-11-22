#include "Panels.h"
#include "../../App.h"
#include "../../core/Logger.h"
#include <imgui.h>

namespace lsalt {

void DrawHomePanel(AppState& state) {
    ImGui::TextWrapped("An original scaling + frame generation utility for Windows games. "
                        "Not affiliated with or derived from any commercial product.");
    ImGui::Separator();
    ImGui::Spacing();

    const GameProfile* activeProfile =
        (state.selectedProfileIndex >= 0 &&
         state.selectedProfileIndex < static_cast<int>(state.profiles.Profiles().size()))
            ? &state.profiles.Profiles()[state.selectedProfileIndex]
            : nullptr;

    ImGui::Text("Active profile:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s",
                        activeProfile ? activeProfile->displayName.c_str() : "(none selected)");

    ImGui::Spacing();

    ImVec4 statusColor = state.scalingActive ? ImVec4(0.3f, 0.85f, 0.4f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    ImGui::TextColored(statusColor, state.scalingActive ? "RUNNING" : "STOPPED");

    ImGui::Spacing();
    ImGui::Spacing();

    const bool canStart = activeProfile != nullptr && !state.scalingActive;
    if (!canStart) ImGui::BeginDisabled();
    if (ImGui::Button("Start Scaling", ImVec2(160, 36))) {
        state.scalingActive = true;
        state.statusMessage = "Marked as running, but no frames are being captured or processed yet: "
                               "the Capture Engine (Phase 2), Scaling Engine (Phase 3) and Frame "
                               "Generation Engine (Phase 4) are not implemented in this build.";
        Logger::Instance().Info("UI", "Start Scaling pressed (no-op until Phase 2-4 land).");
    }
    if (!canStart) ImGui::EndDisabled();

    ImGui::SameLine();

    if (!state.scalingActive) ImGui::BeginDisabled();
    if (ImGui::Button("Stop Scaling", ImVec2(160, 36))) {
        state.scalingActive = false;
        state.statusMessage = "Stopped.";
        Logger::Instance().Info("UI", "Stop Scaling pressed.");
    }
    if (!state.scalingActive) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("%s", state.statusMessage.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("System snapshot (real, measured):");
    ImGui::BulletText("CPU: %.1f%% system / %.1f%% this process", state.lastStats.systemCpuPercent, state.lastStats.processCpuPercent);
    ImGui::BulletText("RAM: %.1f / %.1f GB",
                       state.lastStats.usedRamBytes / (1024.0 * 1024.0 * 1024.0),
                       state.lastStats.totalRamBytes / (1024.0 * 1024.0 * 1024.0));
    if (state.lastStats.vramBudgetBytes > 0) {
        ImGui::BulletText("VRAM (active adapter): %.0f / %.0f MB",
                           state.lastStats.vramUsedBytes / (1024.0 * 1024.0),
                           state.lastStats.vramBudgetBytes / (1024.0 * 1024.0));
    } else {
        ImGui::BulletText("VRAM: unavailable on this adapter/driver");
    }
}

} // namespace lsalt
