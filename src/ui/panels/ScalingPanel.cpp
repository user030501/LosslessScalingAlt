#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <string>

namespace lsalt {

namespace {
void DrawLiveScalingSection(AppState& state) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Live Scaling Engine");
    ImGui::TextWrapped(
        "This is a real, working compute-shader scaling pipeline (Nearest/Bilinear/Bicubic/Lanczos/Integer "
        "Scale with real aspect-ratio letterboxing), running on the GPU. This toggle here runs it against "
        "the same synthetic, animated test-pattern source the Frame Generation tab uses, for controlled "
        "testing; the Game Library tab's real window capture also runs through this same engine whenever a "
        "capture is active (that takes priority over this synthetic demo). FSR1/NVIDIA Image Scaling are not "
        "implemented and fall back to Bilinear.");
    ImGui::Separator();

    if (!state.scalingEngine) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Engine failed to construct.");
        return;
    }

    bool liveEnabled = state.scalingLiveEnabled;
    if (ImGui::Checkbox("Enable Live Scaling", &liveEnabled)) {
        state.scalingLiveEnabled = liveEnabled;
    }

    const char* liveAlgoNames[] = {"Nearest Neighbor", "Linear (Bilinear)", "Bicubic", "Lanczos",
                                    "Integer Scaling", "AMD FSR 1.0 (-> Bilinear)", "NVIDIA Image Scaling (-> Bilinear)"};
    int liveAlgoIndex = static_cast<int>(state.scalingEngine->Algorithm());
    if (ImGui::Combo("Live Algorithm", &liveAlgoIndex, liveAlgoNames, IM_ARRAYSIZE(liveAlgoNames))) {
        state.scalingEngine->SetAlgorithm(static_cast<ScalingAlgorithm>(liveAlgoIndex));
    }

    bool liveAspect = state.scalingEngine->PreserveAspectRatio();
    if (ImGui::Checkbox("Live Preserve Aspect Ratio", &liveAspect)) {
        state.scalingEngine->SetPreserveAspectRatio(liveAspect);
    }

    if (!state.testPatternSource) return;

    uint32_t vx = 0, vy = 0, vw = 0, vh = 0;
    state.scalingEngine->Viewport(vx, vy, vw, vh);
    std::string factorSuffix;
    if (state.scalingEngine->Algorithm() == ScalingAlgorithm::IntegerScale) {
        factorSuffix = ", " + std::to_string(state.scalingEngine->IntegerFactor()) + "x integer factor";
    }
    ImGui::Text("%ux%u -> %ux%u (content area %ux%u at (%u,%u)%s)",
                state.testPatternSource->Width(), state.testPatternSource->Height(),
                1920u, 1080u, vw, vh, vx, vy, factorSuffix.c_str());

    if (liveEnabled && state.scalingEngine->OutputSRV()) {
        ImGui::Spacing();
        ImGui::Text("Live Preview (Source | Scaled)");
        const ImVec2 srcThumb(171, 96); // 960x540 at ~1/5.6 scale
        const ImVec2 dstThumb(192, 108); // 1920x1080 at the same relative scale
        if (state.testPatternSource->FrameBSRV()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.testPatternSource->FrameBSRV()), srcThumb);
            ImGui::SameLine();
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(state.scalingEngine->OutputSRV()), dstThumb);
    }
}
} // namespace

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
                            "Not implemented (porting a vendor reference implementation is out of scope); "
                            "the engine falls back to Bilinear whenever this is selected. Setting is still saved.");
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

    DrawLiveScalingSection(state);
}

} // namespace lsalt
