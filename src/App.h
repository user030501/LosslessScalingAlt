#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <memory>
#include <string>

#include "core/ConfigManager.h"
#include "profiles/ProfileManager.h"
#include "gpu/GpuDeviceManager.h"
#include "monitor/PerfMonitor.h"
#include "profiles/GameProfile.h"

namespace lsalt {

// Everything the UI panels need, gathered in one place so each panel
// function takes a single AppState& instead of a long parameter list.
struct AppState {
    ConfigManager config;
    ProfileManager profiles;
    GpuDeviceManager gpu;
    std::unique_ptr<PerfMonitor> perf;

    SystemStats lastStats;

    int selectedProfileIndex = 0;
    bool scalingActive = false; // UI-only flag; Start/Stop do not yet drive a real engine (Phase 2/3/4)
    std::string statusMessage = "Idle. Capture/Scaling/Frame-Generation engines are not implemented yet (Phase 2-4).";

    // Scratch buffer used by the "add/edit profile" form in ProfilesPanel.
    GameProfile profileEditBuffer;
    bool isAddingNewProfile = false;

    int activePanel = 0; // index into the left-nav sections
};

// Owns the Win32 window, D3D11 swapchain and the ImGui frame lifecycle.
// This is the "Phase 1 shell": a real, running, resizable application
// window with a working dark-themed UI, real GPU enumeration and real
// system performance counters. It does not yet capture or process any
// game -- that is Phase 2 onward.
class App {
public:
    bool Initialize(HINSTANCE hInstance);
    int Run();
    void Shutdown();

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateSwapChain();
    void ResizeSwapChain(UINT width, UINT height);
    void RenderFrame();

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

    AppState m_state;
    bool m_running = false;
};

} // namespace lsalt
