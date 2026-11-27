#include "Panels.h"
#include "../../App.h"
#include "../../framegen/DiagnosticTests.h"
#include <imgui.h>
#include <string>

namespace lsalt {

namespace {
void DrawLiveEngineSection(AppState& state) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Live Frame Generation Engine");
    ImGui::TextWrapped(
        "This is a real, working motion-compensated interpolation pipeline (block-matching motion "
        "estimation + forward/backward warping + occlusion-aware blending) running on the GPU. Below, this "
        "toggle runs it against a synthetic, deterministic test-pattern source for controlled testing; to "
        "run it against a real captured window instead, use the Game Library tab's \"Capture\" button -- "
        "while a real capture is active, enabling Live Engine here runs this same pipeline on those real "
        "captured (and scaled) frames instead of the synthetic source.");
    ImGui::Separator();

    if (!state.frameGenEngine) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Engine failed to construct.");
        return;
    }

    const FrameGenCapabilities& caps = state.frameGenEngine->Capabilities();
    if (!caps.computeShadersSupported) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Unsupported on this GPU/driver:");
        ImGui::TextWrapped("%s", caps.unsupportedReason.c_str());
        return;
    }

    bool liveEnabled = state.frameGenLiveEnabled;
    if (ImGui::Checkbox("Enable Live Engine", &liveEnabled)) {
        state.frameGenLiveEnabled = liveEnabled;
        state.frameGenEngine->SetEnabled(liveEnabled);
    }

    ImGui::SameLine();
    {
        const int activeIdx = state.gpu.ActiveAdapterIndex();
        std::string gpuLabel = "Auto (see GPU Settings)";
        if (activeIdx >= 0 && activeIdx < static_cast<int>(state.gpu.Adapters().size())) {
            const auto& desc = state.gpu.Adapters()[activeIdx].description;
            gpuLabel = std::string(desc.begin(), desc.end());
        }
        ImGui::TextDisabled("Processing GPU: %s (shares the app's UI device)", gpuLabel.c_str());
    }

    if (!state.testPatternSource) return;

    int scenarioIndex = static_cast<int>(state.testPatternSource->Scenario());
    const char* scenarioNames[static_cast<int>(TestScenario::Count)];
    for (int i = 0; i < static_cast<int>(TestScenario::Count); ++i) scenarioNames[i] = ToString(static_cast<TestScenario>(i));
    if (ImGui::Combo("Test Scenario", &scenarioIndex, scenarioNames, static_cast<int>(TestScenario::Count))) {
        state.testPatternSource->SetScenario(static_cast<TestScenario>(scenarioIndex));
    }

    int qualityIndex = static_cast<int>(state.frameGenEngine->QualityMode());
    const char* qualityNames[] = {"Performance", "Balanced", "Quality"};
    if (ImGui::Combo("Live Quality Mode", &qualityIndex, qualityNames, IM_ARRAYSIZE(qualityNames))) {
        state.frameGenEngine->SetQualityMode(static_cast<FrameGenQualityMode>(qualityIndex));
    }

    float srcFps = static_cast<float>(state.testPatternSource->SourceFps());
    if (ImGui::SliderFloat("Simulated Source FPS", &srcFps, 5.0f, 60.0f, "%.0f")) {
        state.testPatternSource->SetSourceFps(srcFps);
    }

    if (state.dynamicPerfManager) {
        bool autoPerf = state.dynamicPerfManager->AutoEnabled();
        if (ImGui::Checkbox("Auto Performance Management", &autoPerf)) {
            state.dynamicPerfManager->SetAutoEnabled(autoPerf);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Automatically steps the quality mode above down under sustained GPU overload "
                "(measured GPU dispatch time, GPU utilization, or the thermal fallback signal -- "
                "see Performance Monitor) and back up once headroom has been stable for a while. "
                "Never changes resolution. Manually picking a quality mode here still works while "
                "this is on; it is just liable to be changed again under real sustained load.");
        }
        bool adaptiveScaling = state.dynamicPerfManager->AdaptiveScalingEnabled();
        if (ImGui::Checkbox("Adaptive Scaling (opt-in)", &adaptiveScaling)) {
            state.dynamicPerfManager->SetAdaptiveScalingEnabled(adaptiveScaling);
            if (!adaptiveScaling && state.scalingEngine) {
                state.dynamicPerfManager->RestoreScaling(*state.scalingEngine);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Under sustained overload, temporarily lowers scaling quality/sharpening or "
                "expensive sampling and restores them gradually. Desktop resolution is never changed.");
        }
    }

    const FrameGenStats stats = state.frameGenEngine->GetStats();
    ImGui::Spacing();
    ImGui::Text("Status: %s", stats.statusText.c_str());
    if (!stats.autoAdjustReason.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "%s", stats.autoAdjustReason.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Run Diagnostic Tests (Phase 8 suite)")) {
        const bool wasEnabled = state.frameGenLiveEnabled;
        const TestScenario savedScenario = state.testPatternSource->Scenario();
        state.frameGenEngine->SetEnabled(true);
        state.lastDiagnosticsResults = DiagnosticTests::RunAll(*state.frameGenEngine, *state.testPatternSource,
                                                                 state.gpu.Device().Get(), state.gpu.Context().Get());
        state.testPatternSource->SetScenario(savedScenario);
        state.frameGenEngine->SetEnabled(wasEnabled);
    }

    if (!state.lastDiagnosticsResults.empty()) {
        ImGui::Spacing();
        if (ImGui::BeginTable("diag_results", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Test");
            ImGui::TableSetupColumn("Result");
            ImGui::TableHeadersRow();
            for (const auto& r : state.lastDiagnosticsResults) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(r.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(r.passed ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                    "%s", r.passed ? "PASS" : "FAIL");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.detail.c_str());
            }
            ImGui::EndTable();
        }
    }

    if (liveEnabled && state.frameGenEngine->IsEnabled()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Live Preview (Frame A | Generated A.5 | Frame B)");
        const ImVec2 thumb(220, 124);
        if (state.testPatternSource->FrameASRV()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.testPatternSource->FrameASRV()), thumb);
            ImGui::SameLine();
        }
        if (state.frameGenEngine->GeneratedOutputSRV()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.frameGenEngine->GeneratedOutputSRV()), thumb);
            ImGui::SameLine();
        }
        if (state.testPatternSource->FrameBSRV()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(state.testPatternSource->FrameBSRV()), thumb);
        }
    }
}
} // namespace

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
                        "The interpolation engine itself is implemented (real motion-compensated "
                        "warping, not frame duplication -- try it live below). This setting is applied "
                        "automatically if this game is auto-detected running (Game Library tab); actually "
                        "capturing and processing its frames still requires manually starting capture on "
                        "it from the Game Library tab.");
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

    DrawLiveEngineSection(state);
}

} // namespace lsalt
