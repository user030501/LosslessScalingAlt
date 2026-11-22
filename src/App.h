#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>

#include "core/ConfigManager.h"
#include "profiles/ProfileManager.h"
#include "gpu/GpuDeviceManager.h"
#include "monitor/PerfMonitor.h"
#include "profiles/GameProfile.h"
#include "framegen/FrameGenEngine.h"
#include "framegen/TestPatternSource.h"
#include "framegen/DiagnosticTests.h"
#include "framegen/ThermalMonitor.h"
#include "framegen/DynamicPerformanceManager.h"

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

    // Phase 4: a real Frame Generation Engine, exercised against a
    // synthetic test-pattern source since the Capture Engine (Phase 2)
    // doesn't exist yet. See framegen/TestPatternSource.h.
    std::unique_ptr<FrameGenEngineD3D11> frameGenEngine;
    std::unique_ptr<TestPatternSource> testPatternSource;
    bool frameGenLiveEnabled = false;
    std::vector<DiagnosticResult> lastDiagnosticsResults;

    // Phase 3: thermal-aware signal + closed-loop quality auto-adjustment.
    // See ThermalMonitor.h / DynamicPerformanceManager.h for what is and
    // isn't real here (CPU temperature is best-effort; GPU temperature is
    // never available; both have a utilization/trend-based fallback).
    std::unique_ptr<ThermalMonitor> thermalMonitor;
    std::unique_ptr<DynamicPerformanceManager> dynamicPerfManager;
    ThermalSample lastThermalSample;
    std::string lastAutoPerfReason; // most recent automatic change, for the UI
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
    // Headlessly runs DiagnosticTests' 10 scenarios against the live
    // FrameGenEngine + TestPatternSource and logs a pass/fail report.
    // Returns 0 if every scenario passed, 1 otherwise (or if the engine
    // isn't available on this GPU/driver). Does not enter the message loop.
    int RunSelfTest();
    void Shutdown();

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateSwapChain();
    void ResizeSwapChain(UINT width, UINT height);
    void RenderFrame();

    // Phase 3 stability: real D3D11 device-loss recovery. Triggered when
    // Present() reports DXGI_ERROR_DEVICE_REMOVED/RESET (a real, recoverable
    // failure mode on the laptop GPUs this targets, under thermal/driver
    // stress). Tears down and recreates the device, swap chain, UI render
    // backend and Frame Generation Engine; returns false if recovery itself
    // fails, in which case the app shuts down rather than running on a
    // broken device. Live frame generation is deliberately left disabled
    // after a successful recovery so the user re-enables it knowingly.
    bool AttemptDeviceLossRecovery();

    static constexpr uint32_t kTestSourceWidth = 960;
    static constexpr uint32_t kTestSourceHeight = 540;

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

    AppState m_state;
    bool m_running = false;
    LARGE_INTEGER m_qpcFrequency{};
    LARGE_INTEGER m_lastFrameQpc{};
};

} // namespace lsalt
