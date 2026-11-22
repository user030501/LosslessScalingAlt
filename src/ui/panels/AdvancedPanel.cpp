#include "Panels.h"
#include "../../App.h"
#include "../../core/Logger.h"
#include <imgui.h>

namespace lsalt {

void DrawAdvancedPanel(AppState& state) {
    AppConfig& cfg = state.config.Get();

    ImGui::Text("Global Defaults");
    ImGui::Separator();
    ImGui::Checkbox("V-Sync", &cfg.vsync);
    ImGui::InputInt("FPS Limit (0 = unlimited)", &cfg.fpsLimit);
    if (cfg.fpsLimit < 0) cfg.fpsLimit = 0;
    ImGui::Checkbox("Low GPU Overhead Mode", &cfg.lowGpuOverheadMode);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reduces capture/present queue depth once the Capture Engine (Phase 2) "
                           "is implemented. Recommended for the MX150/UHD 620 hardware class.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Diagnostics");
    const char* logLevels[] = {"Debug", "Info", "Warning", "Error"};
    int levelIndex = static_cast<int>(cfg.logLevel);
    if (ImGui::Combo("Log Level", &levelIndex, logLevels, IM_ARRAYSIZE(logLevels))) {
        cfg.logLevel = static_cast<LogLevel>(levelIndex);
        Logger::Instance().SetLevel(cfg.logLevel);
    }

    std::wstring logPath = Logger::Instance().GetLogFilePath();
    std::string logPathNarrow(logPath.begin(), logPath.end());
    ImGui::TextDisabled("Log file: %s", logPathNarrow.c_str());

    std::wstring cfgPath = state.config.ConfigFilePath();
    std::string cfgPathNarrow(cfgPath.begin(), cfgPath.end());
    ImGui::TextDisabled("Config file: %s", cfgPathNarrow.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save Settings", ImVec2(160, 0))) {
        state.config.Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Default Settings", ImVec2(220, 0))) {
        state.config.ResetToDefaults();
        Logger::Instance().SetLevel(state.config.Get().logLevel);
    }
}

} // namespace lsalt
