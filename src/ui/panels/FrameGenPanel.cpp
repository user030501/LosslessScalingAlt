#include "Panels.h"
#include "../../App.h"
#include <imgui.h>

namespace lsalt {

void DrawFrameGenPanel(AppState& state) {
    GameProfile* profilePtr = state.profiles.AtIndex(state.selectedProfileIndex);
    if (!profilePtr) {
        ImGui::TextWrapped("No game profiles yet. Add one from the Game Profiles tab first.");
        return;
    }
    GameProfile& profile = *profilePtr;

    ImGui::TextWrapped("Frame generation settings for: %s", profile.displayName.c_str());
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                        "The interpolation engine is not implemented yet (Phase 4). Enabling this "
                        "saves the setting for when it lands; it will NOT duplicate frames as a "
                        "fake substitute.");
    ImGui::Spacing();

    ImGui::Checkbox("Enable Frame Generation", &profile.frameGenEnabled);

    if (!profile.frameGenEnabled) ImGui::BeginDisabled();

    const char* multNames[] = {"2x", "3x (planned)", "4x (planned)"};
    int multIndex = 0;
    switch (profile.frameGenMultiplier) {
        case FrameGenMultiplier::X2: multIndex = 0; break;
        case FrameGenMultiplier::X3: multIndex = 1; break;
        case FrameGenMultiplier::X4: multIndex = 2; break;
        default: multIndex = 0; break;
    }
    if (ImGui::Combo("Multiplier", &multIndex, multNames, IM_ARRAYSIZE(multNames))) {
        profile.frameGenMultiplier = (multIndex == 0) ? FrameGenMultiplier::X2
                                    : (multIndex == 1) ? FrameGenMultiplier::X3
                                                        : FrameGenMultiplier::X4;
    }
    if (multIndex != 0) {
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "3x/4x are architecture placeholders only (see Phase 4 notes).");
    }

    const char* qualityNames[] = {"Performance", "Balanced", "Quality"};
    int qualityIndex = static_cast<int>(profile.frameGenQuality);
    if (ImGui::Combo("Quality Mode", &qualityIndex, qualityNames, IM_ARRAYSIZE(qualityNames))) {
        profile.frameGenQuality = static_cast<FrameGenQualityMode>(qualityIndex);
    }

    if (!profile.frameGenEnabled) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Latency note: frame generation adds a pacing delay to smooth motion. "
                        "It is disabled by default on competitive/latency-sensitive profiles "
                        "(e.g. Valorant) -- enable it deliberately if you want it there.");

    ImGui::Spacing();
    if (ImGui::Button("Save Profile")) {
        state.profiles.AddOrUpdate(profile);
    }
}

} // namespace lsalt
