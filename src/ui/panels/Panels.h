#pragma once

namespace lsalt {
struct AppState;

void DrawHomePanel(AppState& state);
void DrawScalingPanel(AppState& state);
void DrawFrameGenPanel(AppState& state);
void DrawProfilesPanel(AppState& state);
void DrawMonitorPanel(AppState& state);
void DrawGpuSettingsPanel(AppState& state);
void DrawAdvancedPanel(AppState& state);
void DrawAboutPanel(AppState& state);

} // namespace lsalt
