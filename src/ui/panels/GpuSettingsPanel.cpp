#include "Panels.h"
#include "../../App.h"
#include <imgui.h>
#include <string>

namespace lsalt {

namespace {
const char* VendorName(uint32_t vendorId) {
    switch (vendorId) {
        case kVendorNVIDIA: return "NVIDIA";
        case kVendorAMD:    return "AMD";
        case kVendorIntel:  return "Intel";
        default:             return "Unknown";
    }
}
} // namespace

void DrawGpuSettingsPanel(AppState& state) {
    ImGui::TextWrapped("Detected GPUs on this system (via DXGI). Choose which one should do the "
                        "scaling/frame-generation work once those engines are implemented.");
    ImGui::Separator();

    AppConfig& cfg = state.config.Get();

    const int autoWouldPick = state.gpu.SelectBestAdapterIndex();

    bool autoSelected = cfg.preferredAdapterIndex < 0;
    if (ImGui::RadioButton("Auto (recommended)", autoSelected)) {
        cfg.preferredAdapterIndex = -1;
        state.config.Save();
    }
    if (autoSelected) {
        ImGui::Indent();
        if (autoWouldPick >= 0) {
            ImGui::TextDisabled("Auto picks by real measured capability: compute shaders + typed UAV store "
                                 "required, then most dedicated VRAM as the tie-breaker (see below).");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                                "No enumerated adapter passed capability scoring; falling back to DXGI's default.");
        }
        ImGui::Unindent();
    }

    for (const auto& adapter : state.gpu.Adapters()) {
        ImGui::Spacing();
        bool selected = (cfg.preferredAdapterIndex == adapter.index);
        std::string label = std::string(VendorName(adapter.vendorId)) + " - " +
                             std::string(adapter.description.begin(), adapter.description.end());
        if (ImGui::RadioButton(label.c_str(), selected)) {
            cfg.preferredAdapterIndex = adapter.index;
            state.config.Save();
        }
        ImGui::Indent();
        ImGui::TextDisabled("Dedicated VRAM: %.0f MB | D3D11: %s | Compute shaders: %s | Typed UAV store: %s",
                             adapter.dedicatedVideoMemoryBytes / (1024.0 * 1024.0),
                             adapter.supportsD3D11 ? "yes" : "no",
                             adapter.supportsComputeShaders ? "yes" : "no",
                             adapter.supportsTypedUavStore ? "yes" : "no");
        ImGui::TextDisabled("Active now: %s%s",
                             (adapter.index == state.gpu.ActiveAdapterIndex()) ? "yes" : "no",
                             (autoSelected && adapter.index == autoWouldPick) ? "  (what Auto picks)" : "");
        ImGui::Unindent();
    }

    if (state.gpu.Adapters().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No hardware adapters detected.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Note: the app's own UI currently renders on the adapter active at launch "
                        "(shown as \"Active now\"). Switching the preference here takes effect the "
                        "next time the app starts; per-process GPU hand-off for the capture/scale "
                        "pipeline itself is implemented in later phases.");
}

} // namespace lsalt
