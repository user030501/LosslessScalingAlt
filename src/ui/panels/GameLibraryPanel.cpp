#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace lsalt {

namespace {
bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

std::string FormatLastPlayed(int64_t unixSeconds) {
    if (unixSeconds <= 0) return "Never";
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
    if (localtime_s(&local, &t) != 0) return "Unknown";
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
    return buf;
}
} // namespace

void DrawGameLibraryPanel(AppState& state) {
    ImGui::TextWrapped(
        "Real, read-only process detection (no capture, no injection, no memory access beyond a running "
        "process' own executable path -- see docs/ARCHITECTURE.md). Detecting a game and applying its "
        "profile only changes this app's own Frame Generation Engine settings; it does not yet capture or "
        "process that game's actual rendered frames (the Capture Engine remains an unimplemented stub).");
    ImGui::Separator();

    bool autoSwitch = state.gameSession.AutoSwitchEnabled();
    if (ImGui::Checkbox("Automatic Profile Switching", &autoSwitch)) {
        state.gameSession.SetAutoSwitchEnabled(autoSwitch);
        state.config.Get().autoProfileSwitchingEnabled = autoSwitch;
        state.config.Save();
    }
    ImGui::SameLine();
    if (state.gameSession.HasActiveGame()) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "Active: %s", state.gameSession.ActiveGameProfileName().c_str());
    } else {
        ImGui::TextDisabled("No tracked game currently detected as running.");
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add Game", ImVec2(140, 0))) {
        state.profileEditBuffer = GameProfile{};
        state.isAddingNewProfile = true;
        state.activePanel = static_cast<int>(NavPanel::GameProfiles);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(opens the Game Profiles tab's new-profile form)");

    ImGui::Spacing();
    ImGui::Separator();

    const auto& profiles = state.profiles.Profiles();
    for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
        const GameProfile& p = profiles[i];
        ImGui::PushID(i);

        const bool detected = std::any_of(state.lastGameScan.begin(), state.lastGameScan.end(),
                                           [&](const DetectedProcess& d) { return IEquals(d.executableName, p.executableName); });
        const bool isActiveProfile = state.gameSession.HasActiveGame() && state.gameSession.ActiveGameProfileName() == p.displayName;

        ImGui::BeginChild("card", ImVec2(0, 112), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", p.displayName.c_str());
        ImGui::SameLine();
        if (p.isBuiltIn) ImGui::TextDisabled("[built-in]");
        if (isActiveProfile) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "RUNNING (auto-applied)");
        } else if (detected) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "RUNNING");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("Not detected");
        }

        ImGui::TextDisabled("Executable: %s", p.executableName.c_str());
        ImGui::TextDisabled("Install path: %s", p.installPath.empty() ? "(unknown -- not yet seen running, or added by name only)" : p.installPath.c_str());
        ImGui::TextDisabled("Last played: %s", FormatLastPlayed(p.lastPlayedUnixSeconds).c_str());

        if (ImGui::Button("Edit Profile")) {
            state.selectedProfileIndex = i;
            state.isAddingNewProfile = false;
            state.activePanel = static_cast<int>(NavPanel::GameProfiles);
        }
        ImGui::SameLine();
        if (ImGui::Button("Frame Generation")) {
            state.selectedProfileIndex = i;
            state.isAddingNewProfile = false;
            state.activePanel = static_cast<int>(NavPanel::FrameGeneration);
        }
        if (!p.isBuiltIn) {
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                state.profiles.Remove(p.displayName);
                ImGui::EndChild();
                ImGui::PopID();
                break; // the vector we're iterating just changed
            }
        }

        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::PopID();
    }

    if (profiles.empty()) {
        ImGui::TextWrapped("No game profiles yet. Click \"+ Add Game\" above to create one.");
    }
}

} // namespace lsalt
