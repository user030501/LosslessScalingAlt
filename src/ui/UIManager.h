#pragma once
#include <windows.h>
#include <d3d11.h>

namespace lsalt {
struct AppState;

// Owns the ImGui context + Win32/D3D11 backends and draws the main window
// layout: a left-hand navigation sidebar (Home, Scaling, Frame Generation,
// Game Profiles, Performance Monitor, GPU Settings, Advanced Settings,
// About) and the selected panel's content on the right.
class UIManager {
public:
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    void NewFrame();
    void Draw(AppState& state);
    void Render();

    static void ApplyDarkTheme();

private:
    bool m_initialized = false;
};

} // namespace lsalt
