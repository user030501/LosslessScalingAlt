#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <algorithm>
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
        "capture is active (that takes priority over this synthetic demo). Vendor-specific FSR1/NIS "
        "implementations are not advertised or claimed.");
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
                                    "Integer Scaling"};
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
    const double scaleX = 1920.0 / state.testPatternSource->Width();
    const double scaleY = 1080.0 / state.testPatternSource->Height();
    ImGui::Text("Scaling ratio: %.3fx (X), %.3fx (Y) | Aspect: 16:9 output | Estimated texture memory: %.1f MB",
                scaleX, scaleY, (1920u * 1080u * 4.0) / (1024.0 * 1024.0));

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
                                "Integer Scaling"};
    int algoIndex = static_cast<int>(profile.scalingAlgorithm);
    if (ImGui::Combo("Scaling Algorithm", &algoIndex, algoNames, IM_ARRAYSIZE(algoNames))) {
        profile.scalingAlgorithm = static_cast<ScalingAlgorithm>(algoIndex);
    }

    ImGui::SliderInt("Scale Percentage", &profile.scalePercent, 25, 200, "%d%%");

    int inputW = static_cast<int>(profile.inputResWidth);
    int inputH = static_cast<int>(profile.inputResHeight);
    if (ImGui::InputInt("Input Width (0=auto)", &inputW, 1, 100)) profile.inputResWidth = std::max(0, inputW);
    if (ImGui::InputInt("Input Height (0=auto)", &inputH, 1, 100)) profile.inputResHeight = std::max(0, inputH);
    int outputW = static_cast<int>(profile.targetResWidth);
    int outputH = static_cast<int>(profile.targetResHeight);
    if (ImGui::InputInt("Output Width (0=auto)", &outputW, 1, 100)) profile.targetResWidth = std::max(0, outputW);
    if (ImGui::InputInt("Output Height (0=auto)", &outputH, 1, 100)) profile.targetResHeight = std::max(0, outputH);

    const char* qualityNames[] = {"Performance", "Balanced", "Quality"};
    int qualityIndex = static_cast<int>(profile.scalingQuality);
    if (ImGui::Combo("Scaling Quality", &qualityIndex, qualityNames, IM_ARRAYSIZE(qualityNames))) {
        profile.scalingQuality = static_cast<ScalingQualityPreset>(qualityIndex);
    }
    const char* sharpenNames[] = {"Off", "Low", "Medium", "High"};
    int sharpenIndex = static_cast<int>(profile.sharpenLevel);
    if (ImGui::Combo("Sharpening", &sharpenIndex, sharpenNames, IM_ARRAYSIZE(sharpenNames))) {
        profile.sharpenLevel = static_cast<SharpenLevel>(sharpenIndex);
    }
    const char* aspectNames[] = {"Stretch", "Fit", "Integer Scaling"};
    int aspectIndex = static_cast<int>(profile.aspectMode);
    if (ImGui::Combo("Aspect-Ratio Mode", &aspectIndex, aspectNames, IM_ARRAYSIZE(aspectNames))) {
        profile.aspectMode = static_cast<AspectMode>(aspectIndex);
        profile.preserveAspectRatio = profile.aspectMode != AspectMode::Stretch;
    }

    ImGui::Checkbox("Adaptive Scaling Enabled", &profile.adaptiveScalingEnabled);
    int cooldown = profile.adaptiveScalingCooldownSeconds;
    if (ImGui::SliderInt("Adaptive Cooldown (seconds)", &cooldown, 5, 300)) {
        profile.adaptiveScalingCooldownSeconds = cooldown;
    }
    int preferredQuality = static_cast<int>(profile.preferredScalingQuality);
    if (ImGui::Combo("Preferred Scaling Quality", &preferredQuality, qualityNames, IM_ARRAYSIZE(qualityNames))) {
        profile.preferredScalingQuality = static_cast<ScalingQualityPreset>(preferredQuality);
    }

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
