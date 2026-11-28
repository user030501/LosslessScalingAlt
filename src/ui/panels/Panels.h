#pragma once

namespace lsalt {
struct AppState;

void DrawHomePanel(AppState& state);
void DrawScalingPanel(AppState& state);
void DrawFrameGenPanel(AppState& state);
void DrawGameLibraryPanel(AppState& state);
void DrawProfilesPanel(AppState& state);
void DrawMonitorPanel(AppState& state);
void DrawGpuSettingsPanel(AppState& state);
void DrawAdvancedPanel(AppState& state);
// Added benchmark panel declaration
void DrawBenchmarkPanel(AppState& state);

// Indices into UIManager's nav list (kNavItems) -- named so code that
// needs to jump to a specific panel (e.g. the Game Library's "Add Game"
// button landing on the Game Profiles editor) doesn't hardcode a magic
// number that silently goes stale if the nav list is reordered.
enum class NavPanel : int {
    Home = 0,
    Scaling,
    FrameGeneration,
    GameLibrary,
    GameProfiles,
    PerformanceMonitor,
    GpuSettings,
    AdvancedSettings,
    Benchmark,
    About,
};
    Home = 0,
    Scaling,
    FrameGeneration,
    GameLibrary,
    GameProfiles,
    PerformanceMonitor,
    GpuSettings,
    AdvancedSettings,
    About,
};

} // namespace lsalt
