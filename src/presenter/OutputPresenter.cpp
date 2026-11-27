#include "OutputPresenter.h"
#include "../framegen/ShaderCompiler.h"
#include "../core/Logger.h"
#include "../scaling/ResolutionManager.h"

#include <algorithm>

namespace lsalt {

namespace {
OutputPresenter* g_presenterForWndProc = nullptr;
const wchar_t* kWindowClassName = L"LSAltOutputPresenterWindow";
bool g_classRegistered = false;

LRESULT CALLBACK PresenterWndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_presenterForWndProc) return g_presenterForWndProc->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
} // namespace

OutputPresenter::OutputPresenter() = default;

OutputPresenter::~OutputPresenter() { Close(); }

bool OutputPresenter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const std::wstring& shaderDir) {
    m_device = device;
    m_context = context;
    m_shaderDir = shaderDir;

    m_vs = ShaderCompiler::CompileVertexShader(device, shaderDir + L"\\Presenter.hlsl", "VSMain");
    m_ps = ShaderCompiler::CompilePixelShader(device, shaderDir + L"\\Presenter.hlsl", "PSMain");
    if (!m_vs || !m_ps) {
        Logger::Instance().Error("OutputPresenter", "Failed to compile the presenter blit shaders.");
        return false;
    }

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampDesc, m_sampler.ReleaseAndGetAddressOf()))) {
        Logger::Instance().Error("OutputPresenter", "Failed to create the presenter's sampler state.");
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.ScissorEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, m_rasterizerState.ReleaseAndGetAddressOf()))) {
        Logger::Instance().Error("OutputPresenter", "Failed to create the presenter's rasterizer state.");
        return false;
    }

    m_shadersReady = true;
    for (auto& slot : m_timingSlots) {
        D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP, 0};
        device->CreateQuery(&disjoint, slot.disjoint.GetAddressOf());
        device->CreateQuery(&timestamp, slot.start.GetAddressOf());
        device->CreateQuery(&timestamp, slot.end.GetAddressOf());
    }
    return true;
}

bool OutputPresenter::Open(HINSTANCE hInstance) {
    if (m_hwnd) return true; // already open
    if (!m_shadersReady) {
        Logger::Instance().Error("OutputPresenter", "Open() called before Initialize() succeeded.");
        return false;
    }

    if (!g_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PresenterWndProcThunk;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&wc)) {
            Logger::Instance().Error("OutputPresenter", "RegisterClassExW failed.");
            return false;
        }
        g_classRegistered = true;
    }

    g_presenterForWndProc = this;
    // Borderless (no titlebar/menu) but still resizable: WS_POPUP alone
    // has no resize grips, but combined with WS_THICKFRAME the OS still
    // gives real resize-by-dragging-the-edge behavior without any
    // titlebar/caption being drawn. Alt+F4 still closes it (handled by
    // Windows regardless of style); Esc is also wired as a convenience
    // (see HandleMessage).
    m_hwnd = CreateWindowExW(
        0, kWindowClassName, L"LSAlt Output", WS_POPUP | WS_THICKFRAME | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, nullptr, nullptr, hInstance, nullptr);
    if (!m_hwnd) {
        Logger::Instance().Error("OutputPresenter", "CreateWindowExW failed.");
        return false;
    }

    RECT client{};
    GetClientRect(m_hwnd, &client);
    m_clientWidth = static_cast<uint32_t>(client.right - client.left);
    m_clientHeight = static_cast<uint32_t>(client.bottom - client.top);

    if (!CreateSwapChainResources()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    Logger::Instance().Info("OutputPresenter", "Output window opened.");
    return true;
}

bool OutputPresenter::CreateSwapChainResources() {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) return false;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = std::max(m_clientWidth, 1u);
    desc.Height = std::max(m_clientHeight, 1u);
    desc.BufferCount = 2;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    if (FAILED(factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &desc, nullptr, nullptr,
                                                 m_swapChain.ReleaseAndGetAddressOf()))) {
        Logger::Instance().Error("OutputPresenter", "CreateSwapChainForHwnd failed.");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf()))) {
        return false;
    }
    return true;
}

void OutputPresenter::ReleaseSwapChainResources() {
    m_rtv.Reset();
    m_swapChain.Reset();
}

void OutputPresenter::Close() {
    if (!m_hwnd) return;
    ReleaseSwapChainResources();
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    m_fullscreen = false;
    m_lastSrcWidth = m_lastSrcHeight = 0;
    if (g_presenterForWndProc == this) g_presenterForWndProc = nullptr;
    Logger::Instance().Info("OutputPresenter", "Output window closed.");
}

void OutputPresenter::ResizeSwapChain(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || !m_swapChain) return;
    m_rtv.Reset();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf());

    m_clientWidth = width;
    m_clientHeight = height;
    RecomputeViewport(m_lastSrcWidth, m_lastSrcHeight);
}

void OutputPresenter::RecomputeViewport(uint32_t srcWidth, uint32_t srcHeight) {
    ResolutionRequest request;
    request.inputWidth = srcWidth;
    request.inputHeight = srcHeight;
    request.outputWidth = m_clientWidth;
    request.outputHeight = m_clientHeight;
    request.aspectMode = m_aspectMode;
    const ResolutionResult resolved = ResolutionManager::Resolve(request);
    const float w = resolved.valid ? static_cast<float>(resolved.viewportWidth) : std::max(m_clientWidth, 1u);
    const float h = resolved.valid ? static_cast<float>(resolved.viewportHeight) : std::max(m_clientHeight, 1u);
    const float x = resolved.valid ? static_cast<float>(resolved.viewportX) : 0.0f;
    const float y = resolved.valid ? static_cast<float>(resolved.viewportY) : 0.0f;
    m_viewport = D3D11_VIEWPORT{x, y, std::max(w, 1.0f), std::max(h, 1.0f), 0.0f, 1.0f};
}

void OutputPresenter::ToggleFullscreen() {
    if (!m_hwnd) return;

    if (!m_fullscreen) {
        m_savedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_hwnd, &m_savedPlacement);
        m_savedStyle = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);

        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
        m_fullscreen = true;
    } else {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(m_savedStyle));
        SetWindowPlacement(m_hwnd, &m_savedPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        m_fullscreen = false;
    }
}

bool OutputPresenter::PresentFrame(ID3D11ShaderResourceView* srv, uint32_t srcWidth, uint32_t srcHeight) {
    if (!m_hwnd || !m_swapChain || !m_rtv) return false;

    LARGE_INTEGER cpuStart{}, cpuEnd{}, cpuFreq{};
    QueryPerformanceFrequency(&cpuFreq);
    QueryPerformanceCounter(&cpuStart);

    PresenterTimingSlot& readSlot = m_timingSlots[m_timingReadIndex];
    if (readSlot.pending) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        if (m_context->GetData(readSlot.disjoint.Get(), &disjoint, sizeof(disjoint), 0) == S_OK) {
            UINT64 start = 0, end = 0;
            if (!disjoint.Disjoint && disjoint.Frequency > 0 &&
                m_context->GetData(readSlot.start.Get(), &start, sizeof(start), 0) == S_OK &&
                m_context->GetData(readSlot.end.Get(), &end, sizeof(end), 0) == S_OK) {
                m_lastGpuMs = static_cast<double>(end - start) * 1000.0 /
                              static_cast<double>(disjoint.Frequency);
            } else {
                m_lastGpuMs = -1.0;
            }
            readSlot.pending = false;
            m_timingReadIndex = (m_timingReadIndex + 1) % static_cast<int>(m_timingSlots.size());
        }
    }

    LARGE_INTEGER now{}, freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const double nowSeconds = freq.QuadPart > 0
        ? static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart) : 0.0;
    ++m_presentSequence;
    m_frameScheduler.Submit(m_presentSequence, nowSeconds);
    if (!m_frameScheduler.TakeDueFrame(nowSeconds)) {
        return false;
    }

    if (srcWidth != m_lastSrcWidth || srcHeight != m_lastSrcHeight) {
        m_lastSrcWidth = srcWidth;
        m_lastSrcHeight = srcHeight;
        RecomputeViewport(srcWidth, srcHeight);
    }

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    PresenterTimingSlot& writeSlot = m_timingSlots[m_timingWriteIndex];
    if (!writeSlot.pending) {
        m_context->Begin(writeSlot.disjoint.Get());
        m_context->End(writeSlot.start.Get());
    }

    if (srv && srcWidth > 0 && srcHeight > 0) {
        m_context->RSSetViewports(1, &m_viewport);
        D3D11_RECT scissor{0, 0, static_cast<LONG>(m_clientWidth), static_cast<LONG>(m_clientHeight)};
        m_context->RSSetScissorRects(1, &scissor);
        m_context->RSSetState(m_rasterizerState.Get());
        m_context->IASetInputLayout(nullptr);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(), nullptr, 0);
        m_context->PSSetShader(m_ps.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[1] = {srv};
        m_context->PSSetShaderResources(0, 1, srvs);
        m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        m_context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
        m_context->PSSetShaderResources(0, 1, nullSrv);
    }

    if (!writeSlot.pending) {
        m_context->End(writeSlot.end.Get());
        m_context->End(writeSlot.disjoint.Get());
        writeSlot.pending = true;
        m_timingWriteIndex = (m_timingWriteIndex + 1) % static_cast<int>(m_timingSlots.size());
    }

    const HRESULT hr = m_swapChain->Present(1, 0);
    QueryPerformanceCounter(&cpuEnd);
    if (cpuFreq.QuadPart > 0) {
        m_lastCpuSubmissionMs = static_cast<double>(cpuEnd.QuadPart - cpuStart.QuadPart) * 1000.0 /
                                static_cast<double>(cpuFreq.QuadPart);
    }
    return SUCCEEDED(hr);
}

LRESULT OutputPresenter::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                const uint32_t width = LOWORD(lParam);
                const uint32_t height = HIWORD(lParam);
                if (width > 0 && height > 0) ResizeSwapChain(width, height);
            }
            return 0;
        case WM_EXITSIZEMOVE: {
            RECT client{};
            GetClientRect(hwnd, &client);
            const uint32_t width = static_cast<uint32_t>(std::max(0L, client.right - client.left));
            const uint32_t height = static_cast<uint32_t>(std::max(0L, client.bottom - client.top));
            if (width > 0 && height > 0) ResizeSwapChain(width, height);
            return 0;
        }
        case WM_DPICHANGED:
            // Window placement and client size are authoritative after this
            // message. The next size or exit-resize event also rebinds the
            // swapchain; here we at least recompute the content viewport.
            RecomputeViewport(m_lastSrcWidth, m_lastSrcHeight);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                // Esc always exits fullscreen first (if in it), then a
                // second Esc closes the window -- avoids an accidental
                // single keypress losing the window entirely.
                if (m_fullscreen) {
                    ToggleFullscreen();
                } else {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            } else if (wParam == VK_F11) {
                ToggleFullscreen();
            }
            return 0;
        case WM_CLOSE:
            // Standard WM_CLOSE handling: tear down and destroy right away
            // (Close() releases the swapchain/RTV, then calls
            // DestroyWindow, which synchronously delivers WM_DESTROY to
            // this same WndProc before returning -- handled below, a no-op).
            Close();
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace lsalt
