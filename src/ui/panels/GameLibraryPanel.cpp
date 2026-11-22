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

namespace {
const DetectedProcess* FindDetected(AppState& state, const GameProfile& p) {
    for (const auto& d : state.lastGameScan) {
        if (IEquals(d.executableName, p.executableName)) return &d;
    }
    return nullptr;
}
} // namespace

void DrawGameLibraryPanel(AppState& state) {
    ImGui::TextWrapped(
        "Real, read-only process detection (no injection, no memory access beyond a running process' own "
        "executable path) plus real window capture via Windows Graphics Capture -- the same OS mechanism "
        "behind Xbox Game Bar/OBS/Teams screen share. No anti-cheat is touched or bypassed.");
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Real Window Capture");
    if (state.captureEngine && state.captureEngine->IsCapturing()) {
        uint32_t cw = 0, ch = 0;
        state.captureEngine->GetFrameSize(cw, ch);
        ImGui::Text("Capturing: %s (%ux%u)", state.captureEngine->TargetWindowTitle().c_str(), cw, ch);
        if (ImGui::Button("Stop Capture")) state.captureEngine->StopCapture();
        ImGui::SliderFloat("Capture Upscale %", &state.captureScalePercent, 50.0f, 300.0f, "%.0f%%");
        if (state.captureEngine->LatestFrameSRV()) {
            ImGui::Text("Live Preview (real captured window)");
            ImGui::Image(reinterpret_cast<ImTextureID>(state.captureEngine->LatestFrameSRV()), ImVec2(320, 180));
        }
        if (state.scalingEngine && state.scalingEngine->OutputSRV()) {
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Scaled Output");
            ImGui::Image(reinterpret_cast<ImTextureID>(state.scalingEngine->OutputSRV()), ImVec2(320, 180));
            ImGui::EndGroup();
        }
        ImGui::TextWrapped("Enable Live Engine on the Frame Generation tab to also run real motion-compensated "
                            "interpolation on these real captured+scaled frames.");
    } else if (!CaptureEngineWGC::IsSupportedOnThisSystem()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Windows Graphics Capture is not supported on this OS build.");
    } else {
        ImGui::TextDisabled("Not capturing. Click \"Capture\" on a RUNNING game card below, or capture this app's "
                             "own window to try it immediately.");
        if (ImGui::Button("Capture This App's Own Window (demo)") && state.captureEngine) {
            state.captureEngine->StartCaptureWindow(GetForegroundWindow());
        }
        if (state.captureEngine && !state.captureEngine->LastError().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Last capture error: %s", state.captureEngine->LastError().c_str());
        }
    }

    ImGui::Spacing();
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

        const DetectedProcess* detectedProc = FindDetected(state, p);
        const bool detected = detectedProc != nullptr;
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
        if (detected && detectedProc->windowHandle && state.captureEngine && CaptureEngineWGC::IsSupportedOnThisSystem()) {
            ImGui::SameLine();
            if (ImGui::Button("Capture")) {
                state.captureEngine->StartCaptureWindow(detectedProc->windowHandle);
            }
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
