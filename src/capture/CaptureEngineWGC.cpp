#include "CaptureEngineWGC.h"
#include "../core/Logger.h"

#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <dxgi1_2.h>
#include <algorithm>

namespace lsalt {

namespace winrtWGC = winrt::Windows::Graphics::Capture;
namespace winrtD3D = winrt::Windows::Graphics::DirectX;
namespace winrtD3D11 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {
double NowSeconds() {
    static const double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}

std::string NarrowFromWide(const std::wstring& w) {
    if (w.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string GetWindowTitleUtf8(HWND hwnd) {
    wchar_t buf[256] = {};
    GetWindowTextW(hwnd, buf, 256);
    return NarrowFromWide(buf);
}

// Standard WGC <-> D3D11 interop bridge (the same pattern every public
// Microsoft WGC sample uses): wraps our existing ID3D11Device as a WinRT
// IDirect3DDevice so GraphicsCaptureItem APIs will accept it, and unwraps
// a captured IDirect3DSurface back into the real ID3D11Texture2D behind
// it. No data copy happens in this bridge itself -- it is the same GPU
// resource, just accessed through two different API surfaces.
winrtD3D11::IDirect3DDevice WrapD3DDevice(ID3D11Device* device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())));
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
    return inspectable.as<winrtD3D11::IDirect3DDevice>();
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> UnwrapD3DSurface(const winrtD3D11::IDirect3DSurface& surface) {
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(tex.GetAddressOf())));
    return tex;
}

winrtWGC::GraphicsCaptureItem CreateCaptureItemForWindow(HWND hwnd) {
    auto interopFactory = winrt::get_activation_factory<winrtWGC::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::com_ptr<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem> abiItem;
    winrt::check_hresult(interopFactory->CreateForWindow(
        hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), abiItem.put_void()));
    return abiItem.as<winrtWGC::GraphicsCaptureItem>();
}
} // namespace

struct CaptureEngineWGC::Impl {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    winrtD3D11::IDirect3DDevice winrtDevice{nullptr};

    winrtWGC::GraphicsCaptureItem item{nullptr};
    winrtWGC::Direct3D11CaptureFramePool framePool{nullptr};
    winrtWGC::GraphicsCaptureSession session{nullptr};
    winrtWGC::GraphicsCaptureItem::Closed_revoker closedRevoker;
    winrtWGC::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrivedRevoker;

    std::mutex frameMutex;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latestTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> latestSrv;
    uint32_t frameWidth = 0, frameHeight = 0;

    bool capturing = false;
    bool comInitializedHere = false;
    std::string lastError;
    std::string targetWindowTitle;
    HWND targetWindow = nullptr;
    std::vector<HWND> excludedWindows;
    double lastFrameArrivalSeconds = 0.0;

    void EnsureOutputTexture(uint32_t width, uint32_t height) {
        if (width == frameWidth && height == frameHeight && latestTexture) return;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf()))) {
            Logger::Instance().Error("CaptureEngineWGC", "Failed to (re)allocate the captured-frame output texture.");
            return;
        }
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) {
            Logger::Instance().Error("CaptureEngineWGC", "Failed to create an SRV for the captured-frame output texture.");
            return;
        }

        latestTexture = tex;
        latestSrv = srv;
        frameWidth = width;
        frameHeight = height;
    }

    void OnFrameArrived(const winrtWGC::Direct3D11CaptureFramePool& pool) {
        auto frame = pool.TryGetNextFrame();
        if (!frame) return;

        const auto contentSize = frame.ContentSize();
        const uint32_t newW = static_cast<uint32_t>(std::max(1, contentSize.Width));
        const uint32_t newH = static_cast<uint32_t>(std::max(1, contentSize.Height));

        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> contextOwned;
        contextOwned.Attach(context);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> capturedTex;
        try {
            capturedTex = UnwrapD3DSurface(frame.Surface());
        } catch (const winrt::hresult_error& e) {
            lastError = "Failed to unwrap a captured frame: " + NarrowFromWide(std::wstring(e.message()));
            return;
        }

        std::lock_guard<std::mutex> lock(frameMutex);

        if (newW != frameWidth || newH != frameHeight) {
            // The window was resized: the frame pool must be recreated at
            // the new size, matching every public WGC sample's pattern.
            try {
                framePool.Recreate(winrtDevice, winrtD3D::DirectXPixelFormat::R8G8B8A8UIntNormalized, 2, contentSize);
            } catch (const winrt::hresult_error& e) {
                lastError = "Failed to recreate the capture frame pool after a resize: " + NarrowFromWide(std::wstring(e.message()));
            }
            EnsureOutputTexture(newW, newH);
        }

        if (latestTexture) {
            contextOwned->CopyResource(latestTexture.Get(), capturedTex.Get());
        }
        lastFrameArrivalSeconds = NowSeconds();
    }
};

CaptureEngineWGC::CaptureEngineWGC() : m_impl(std::make_unique<Impl>()) {
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_impl->comInitializedHere = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        Logger::Instance().Warn("CaptureEngineWGC", "CoInitializeEx failed, hr=" + std::to_string(hr));
    }
}

CaptureEngineWGC::~CaptureEngineWGC() {
    StopCapture();
    if (m_impl->comInitializedHere) ::CoUninitialize();
}

bool CaptureEngineWGC::IsSupportedOnThisSystem() {
    // Actually cached, as the header promises: this is called from UI
    // draw code every single rendered frame while the Game Library panel
    // is visible, and the uncached version was a real, measured bug --
    // repeated real WinRT activation-factory calls every frame, which
    // showed up as a continuous warning-log spam and real per-frame
    // overhead. Support can't change within a single process's lifetime,
    // so computing it once is both correct and necessary.
    static const bool kSupported = [] {
        try {
            return winrtWGC::GraphicsCaptureSession::IsSupported();
        } catch (const winrt::hresult_error& e) {
            Logger::Instance().Warn("CaptureEngineWGC", "IsSupported() threw hr=" + std::to_string(static_cast<int32_t>(e.code())) +
                                                             " msg=" + NarrowFromWide(std::wstring(e.message())));
            return false;
        } catch (...) {
            Logger::Instance().Warn("CaptureEngineWGC", "IsSupported() threw a non-WinRT exception.");
            return false;
        }
    }();
    return kSupported;
}

bool CaptureEngineWGC::Initialize(ID3D11Device* device) {
    m_impl->device = device;
    try {
        m_impl->winrtDevice = WrapD3DDevice(device);
    } catch (const winrt::hresult_error& e) {
        m_impl->lastError = "Failed to wrap the D3D11 device for WGC: " + NarrowFromWide(std::wstring(e.message()));
        Logger::Instance().Error("CaptureEngineWGC", m_impl->lastError);
        return false;
    }
    return true;
}

bool CaptureEngineWGC::StartCaptureWindow(HWND targetWindow) {
    StopCapture();

    // Checked before (and independent of) WGC support: there's no reason
    // to probe WinRT activation for a target we're going to refuse
    // regardless, and it keeps this safety check exercisable by a test
    // even in environments where WGC itself isn't available (see
    // CaptureDiagnostics).
    if (!targetWindow || !IsWindow(targetWindow)) {
        m_impl->lastError = "Invalid target window.";
        return false;
    }
    if (std::find(m_impl->excludedWindows.begin(), m_impl->excludedWindows.end(), targetWindow) !=
        m_impl->excludedWindows.end()) {
        m_impl->lastError = "Refusing to capture this app's own window(s) -- that would create a feedback loop "
                             "(captured -> processed -> presented -> captured again, compounding every tick).";
        Logger::Instance().Warn("CaptureEngineWGC", m_impl->lastError);
        return false;
    }

    if (!IsSupportedOnThisSystem()) {
        m_impl->lastError = "Windows Graphics Capture is not supported on this OS build/configuration.";
        Logger::Instance().Error("CaptureEngineWGC", m_impl->lastError);
        return false;
    }

    try {
        m_impl->item = CreateCaptureItemForWindow(targetWindow);
    } catch (const winrt::hresult_error& e) {
        m_impl->lastError = "Failed to create a capture item for that window: " + NarrowFromWide(std::wstring(e.message()));
        Logger::Instance().Error("CaptureEngineWGC", m_impl->lastError);
        return false;
    }

    m_impl->targetWindow = targetWindow;
    m_impl->targetWindowTitle = GetWindowTitleUtf8(targetWindow);

    const auto size = m_impl->item.Size();
    {
        std::lock_guard<std::mutex> lock(m_impl->frameMutex);
        m_impl->EnsureOutputTexture(static_cast<uint32_t>(std::max(1, size.Width)),
                                     static_cast<uint32_t>(std::max(1, size.Height)));
    }

    try {
        m_impl->framePool = winrtWGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_impl->winrtDevice, winrtD3D::DirectXPixelFormat::R8G8B8A8UIntNormalized, 2, size);
        m_impl->session = m_impl->framePool.CreateCaptureSession(m_impl->item);

        try {
            m_impl->session.IsCursorCaptureEnabled(false);
        } catch (...) {
            // Older Windows builds don't expose this property; capturing
            // the cursor too is cosmetic, not a functional failure.
        }

        m_impl->frameArrivedRevoker = m_impl->framePool.FrameArrived(
            winrt::auto_revoke, [this](const winrtWGC::Direct3D11CaptureFramePool& pool, const winrt::Windows::Foundation::IInspectable&) {
                m_impl->OnFrameArrived(pool);
            });
        m_impl->closedRevoker = m_impl->item.Closed(
            winrt::auto_revoke, [this](const winrtWGC::GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&) {
                Logger::Instance().Info("CaptureEngineWGC", "Capture target window was closed; stopping capture.");
                m_impl->lastError = "Target window was closed.";
                m_impl->capturing = false;
            });

        m_impl->session.StartCapture();
    } catch (const winrt::hresult_error& e) {
        m_impl->lastError = "Failed to start capture: " + NarrowFromWide(std::wstring(e.message()));
        Logger::Instance().Error("CaptureEngineWGC", m_impl->lastError);
        return false;
    }

    m_impl->capturing = true;
    m_impl->lastError.clear();
    m_impl->lastFrameArrivalSeconds = 0.0;
    Logger::Instance().Info("CaptureEngineWGC", "Started capturing window: " + m_impl->targetWindowTitle);
    return true;
}

void CaptureEngineWGC::StopCapture() {
    if (!m_impl->capturing && !m_impl->session && !m_impl->framePool) return;

    m_impl->frameArrivedRevoker.revoke();
    m_impl->closedRevoker.revoke();

    if (m_impl->session) {
        try { m_impl->session.Close(); } catch (...) {}
        m_impl->session = nullptr;
    }
    if (m_impl->framePool) {
        try { m_impl->framePool.Close(); } catch (...) {}
        m_impl->framePool = nullptr;
    }
    m_impl->item = nullptr;
    m_impl->capturing = false;
}

bool CaptureEngineWGC::IsCapturing() const { return m_impl->capturing; }

ID3D11ShaderResourceView* CaptureEngineWGC::LatestFrameSRV() {
    std::lock_guard<std::mutex> lock(m_impl->frameMutex);
    return m_impl->latestSrv.Get();
}

void CaptureEngineWGC::GetFrameSize(uint32_t& width, uint32_t& height) const {
    std::lock_guard<std::mutex> lock(m_impl->frameMutex);
    width = m_impl->frameWidth;
    height = m_impl->frameHeight;
}

std::string CaptureEngineWGC::LastError() const { return m_impl->lastError; }
std::string CaptureEngineWGC::TargetWindowTitle() const { return m_impl->targetWindowTitle; }

void CaptureEngineWGC::SetExcludedWindows(std::vector<HWND> excluded) {
    m_impl->excludedWindows = std::move(excluded);
}

double CaptureEngineWGC::SecondsSinceLastFrame() const {
    std::lock_guard<std::mutex> lock(m_impl->frameMutex);
    if (!m_impl->capturing || m_impl->lastFrameArrivalSeconds <= 0.0) return 0.0;
    return NowSeconds() - m_impl->lastFrameArrivalSeconds;
}

} // namespace lsalt
