#include "Panels.h"
#include "../../App.h"
#include <imgui.h>

namespace lsalt {

void DrawAboutPanel(AppState&) {
    ImGui::Text("LSAlt (working title) - v0.1.0 (Phase 1)");
    ImGui::Separator();
    ImGui::TextWrapped(
        "An original, independently implemented Windows utility for scaling and "
        "frame-generating game output. It is not affiliated with, derived from, or "
        "endorsed by Lossless Scaling or Bloody Advert; no proprietary source, "
        "branding, icons or other copyrighted assets from that or any other product "
        "are used here.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Implementation status");
    ImGui::BulletText("Phase 1 - App shell, dark-themed UI, config/profile persistence, real GPU/CPU/RAM/VRAM telemetry: DONE");
    ImGui::BulletText("Phase 2 - Game capture & window detection: NOT STARTED");
    ImGui::BulletText("Phase 3 - Image scaling engine (shaders): NOT STARTED");
    ImGui::BulletText("Phase 4 - Frame generation (motion-based interpolation): NOT STARTED");
    ImGui::BulletText("Phase 5 - GPU acceleration & perf tuning pass: NOT STARTED");
    ImGui::BulletText("Phase 6 - Expanded profiles & monitoring polish: NOT STARTED");
    ImGui::BulletText("Phase 7 - Testing, debugging, packaging: NOT STARTED");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Target hardware: Intel Core i5-8250U, NVIDIA GeForce MX150 2GB, "
                        "Intel UHD Graphics 620, 16GB RAM.");
}

} // namespace lsalt
