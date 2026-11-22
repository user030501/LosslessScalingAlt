#include "Panels.h"
#include "../../App.h"
#include <imgui.h>

namespace lsalt {

void DrawScalingPanel(AppState& state) {
    GameProfile* profilePtr = state.profiles.AtIndex(state.selectedProfileIndex);
    if (!profilePtr) {
        ImGui::TextWrapped("No game profiles yet. Add one from the Game Profiles tab first.");
        return;
    }
    GameProfile& profile = *profilePtr;

    ImGui::TextWrapped("Scaling settings for: %s", profile.displayName.c_str());
    ImGui::Separator();

    const char* algoNames[] = {"Nearest Neighbor", "Linear (Bilinear)", "Bicubic", "Lanczos",
                                "Integer Scaling", "AMD FSR 1.0 (spatial)", "NVIDIA Image Scaling"};
    int algoIndex = static_cast<int>(profile.scalingAlgorithm);
    if (ImGui::Combo("Scaling Algorithm", &algoIndex, algoNames, IM_ARRAYSIZE(algoNames))) {
        profile.scalingAlgorithm = static_cast<ScalingAlgorithm>(algoIndex);
    }

    if (profile.scalingAlgorithm == ScalingAlgorithm::FSR1 || profile.scalingAlgorithm == ScalingAlgorithm::NIS) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                            "Shader implementation lands in Phase 3. Setting is saved now; it has no effect yet.");
    }

    ImGui::SliderInt("Scale Percentage", &profile.scalePercent, 25, 200, "%d%%");
    ImGui::Checkbox("Preserve Aspect Ratio", &profile.preserveAspectRatio);

    ImGui::Spacing();
    ImGui::Text("Window Mode");
    const char* modeNames[] = {"Fullscreen", "Borderless Window", "Windowed"};
    int modeIndex = static_cast<int>(profile.windowMode);
    if (ImGui::Combo("##windowmode", &modeIndex, modeNames, IM_ARRAYSIZE(modeNames))) {
        profile.windowMode = static_cast<WindowMode>(modeIndex);
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save Profile")) {
        state.profiles.AddOrUpdate(profile);
    }
}

} // namespace lsalt
