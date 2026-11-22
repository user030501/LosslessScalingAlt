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

    bool autoSelected = cfg.preferredAdapterIndex < 0;
    if (ImGui::RadioButton("Auto (recommended)", autoSelected)) {
        cfg.preferredAdapterIndex = -1;
        state.config.Save();
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
        ImGui::TextDisabled("Dedicated VRAM: %.0f MB | D3D11: %s | Active now: %s",
                             adapter.dedicatedVideoMemoryBytes / (1024.0 * 1024.0),
                             adapter.supportsD3D11 ? "yes" : "no",
                             (adapter.index == state.gpu.ActiveAdapterIndex()) ? "yes" : "no");
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
