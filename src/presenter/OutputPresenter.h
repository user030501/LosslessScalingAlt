#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <string>
#include "../core/Types.h"

namespace lsalt {

// Phase 6: a dedicated, borderless-but-resizable output window that shows
// the processed pipeline's final frame (scaled, and motion-interpolated
// when Live Engine is on) -- a real second top-level window with its own
// D3D11 swapchain on the SAME device as the rest of the app (no cross-
// device copy), presented via a one-draw-call fullscreen-triangle blit
// (no CPU readback, no intermediate texture copy beyond what Scaling/
// FrameGen already produce).
//
// Feedback-loop safety: this window is registered with CaptureEngineWGC's
// exclusion list (see App::Initialize/OpenOutputPresenter) so a user can't
// accidentally capture this window itself -- that would feed its own
// processed output back into the pipeline, compounding every tick.
class OutputPresenter {
public:
    OutputPresenter();
    ~OutputPresenter();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const std::wstring& shaderDir);

    // Creates (if needed) and shows the output window. False on failure
    // (logged) -- e.g. shader compile failure or swapchain creation failure.
    bool Open(HINSTANCE hInstance);
    // Destroys the output window and releases its swapchain/RTV. Safe to
    // call when not open.
    void Close();
    bool IsOpen() const { return m_hwnd != nullptr; }

    void SetPreserveAspectRatio(bool preserve) { m_preserveAspect = preserve; }
    void SetAspectMode(AspectMode mode) { m_aspectMode = mode; m_preserveAspect = mode != AspectMode::Stretch; }
    bool PreserveAspectRatio() const { return m_preserveAspect; }
    void ToggleFullscreen();
    bool IsFullscreen() const { return m_fullscreen; }

    // Call once per App::RenderFrame tick with the latest pipeline output
    // to show. srv may be null (nothing new yet -- the backbuffer is
    // cleared to black and left that way, never shows stale garbage).
    // Returns true if it actually presented a frame (a real
    // IDXGISwapChain::Present call happened) so the caller can tie real
    // "displayed" FPS measurement to it.
    bool PresentFrame(ID3D11ShaderResourceView* srv, uint32_t srcWidth, uint32_t srcHeight);

    HWND WindowHandle() const { return m_hwnd; }

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool CreateSwapChainResources();
    void ReleaseSwapChainResources();
    void ResizeSwapChain(uint32_t width, uint32_t height);
    void RecomputeViewport(uint32_t srcWidth, uint32_t srcHeight);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    std::wstring m_shaderDir;
    bool m_shadersReady = false;

    HWND m_hwnd = nullptr;
    bool m_fullscreen = false;
    bool m_preserveAspect = true;
    AspectMode m_aspectMode = AspectMode::Fit;
    WINDOWPLACEMENT m_savedPlacement{};
    LONG_PTR m_savedStyle = 0;

    uint32_t m_clientWidth = 0, m_clientHeight = 0;
    uint32_t m_lastSrcWidth = 0, m_lastSrcHeight = 0;
    D3D11_VIEWPORT m_viewport{};
};

} // namespace lsalt
