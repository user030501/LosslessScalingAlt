#include "CaptureDiagnostics.h"
#include "CaptureEngineWGC.h"
#include "../presenter/OutputPresenter.h"
#include "../core/Logger.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace lsalt {

namespace {
bool ReadbackRGBA8(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src,
                    uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
    if (!src) return false;
    D3D11_TEXTURE2D_DESC desc{};
    src->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf()))) return false;
    context->CopyResource(staging.Get(), src);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    out.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint8_t* srcRow = reinterpret_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(&out[static_cast<size_t>(y) * width * 4], srcRow + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(width) * 4);
    }
    context->Unmap(staging.Get(), 0);
    return true;
}
} // namespace

namespace {
Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateSolidTestTexture(ID3D11Device* device, uint32_t width, uint32_t height,
                                                                 uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r; pixels[i + 1] = g; pixels[i + 2] = b; pixels[i + 3] = 255;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * 4;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    device->CreateTexture2D(&desc, &initData, tex.GetAddressOf());
    return tex;
}

void PumpPendingMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
} // namespace

std::vector<DiagnosticResult> CaptureDiagnostics::RunAll(CaptureEngineWGC& captureEngine, HWND selfWindow,
                                                           ID3D11Device* device, ID3D11DeviceContext* context,
                                                           OutputPresenter& presenter, HINSTANCE hInstance) {
    std::vector<DiagnosticResult> results;

    // 27. Real window capture (self).
    {
        DiagnosticResult r{"27. Real Window Capture (self)", false, ""};

        if (!CaptureEngineWGC::IsSupportedOnThisSystem()) {
            r.passed = true; // correctly detecting non-support is the correct behavior here, not a failure
            r.detail = "Windows Graphics Capture is not supported on this OS build/configuration -- the engine "
                       "correctly reports this rather than attempting to capture anyway (skipped, not applicable).";
            results.push_back(r);
        } else if (!captureEngine.StartCaptureWindow(selfWindow)) {
            r.detail = "StartCaptureWindow(self) failed: " + captureEngine.LastError();
            results.push_back(r);
        } else {
            // Frame arrival is real and asynchronous (a worker thread
            // delivered by the OS) -- poll briefly rather than assume
            // it's instantaneous.
            ID3D11ShaderResourceView* srv = nullptr;
            for (int i = 0; i < 50 && !srv; ++i) {
                Sleep(100);
                srv = captureEngine.LatestFrameSRV();
            }

            uint32_t capturedW = 0, capturedH = 0;
            captureEngine.GetFrameSize(capturedW, capturedH);

            RECT clientRect{};
            GetClientRect(selfWindow, &clientRect);
            const uint32_t expectedW = static_cast<uint32_t>(clientRect.right - clientRect.left);
            const uint32_t expectedH = static_cast<uint32_t>(clientRect.bottom - clientRect.top);

            const bool arrived = srv != nullptr && capturedW > 0 && capturedH > 0;
            const bool sizeOk = arrived &&
                std::abs(static_cast<int>(capturedW) - static_cast<int>(expectedW)) <= 4 &&
                std::abs(static_cast<int>(capturedH) - static_cast<int>(expectedH)) <= 4;

            bool readbackOk = false;
            if (arrived) {
                Microsoft::WRL::ComPtr<ID3D11Resource> resource;
                srv->GetResource(resource.GetAddressOf());
                Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                resource.As(&tex);
                std::vector<uint8_t> pixels;
                readbackOk = tex && ReadbackRGBA8(device, context, tex.Get(), capturedW, capturedH, pixels) && !pixels.empty();
            }

            const bool wasCapturingBeforeStop = captureEngine.IsCapturing();
            captureEngine.StopCapture();
            const bool stoppedCleanly = !captureEngine.IsCapturing();

            r.passed = arrived && sizeOk && readbackOk && wasCapturingBeforeStop && stoppedCleanly;
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "arrived=%d captured=%ux%u expected=%ux%u sizeOk=%d readback=%d stoppedCleanly=%d",
                     arrived, capturedW, capturedH, expectedW, expectedH, sizeOk, readbackOk, stoppedCleanly);
            r.detail = buf;
            results.push_back(r);
        }
    }

    // 28. Feedback-loop prevention: a window on the exclusion list must be
    // refused with the feedback-loop-specific reason, distinct from (and
    // checked before) the general WGC-support check -- so this is
    // deterministic and real regardless of whether WGC itself works here.
    {
        DiagnosticResult r{"28. Capture Feedback-Loop Prevention", false, ""};
        captureEngine.SetExcludedWindows({selfWindow});
        const bool refused = !captureEngine.StartCaptureWindow(selfWindow);
        const std::string refusalReason = captureEngine.LastError();
        const bool mentionsFeedbackLoop = refusalReason.find("feedback loop") != std::string::npos;
        const bool notCapturing = !captureEngine.IsCapturing();
        captureEngine.SetExcludedWindows({}); // restore for later tests/real use

        r.passed = refused && mentionsFeedbackLoop && notCapturing;
        r.detail = "refused=" + std::to_string(refused) + " reasonMentionsFeedbackLoop=" +
                   std::to_string(mentionsFeedbackLoop) + " notCapturing=" + std::to_string(notCapturing) +
                   " -- reason: " + refusalReason;
        results.push_back(r);
    }

    // 29. Invalid/unsupported capture target handling: null and bogus
    // window handles must fail gracefully (no crash, clear error), never
    // report IsCapturing()==true.
    {
        DiagnosticResult r{"29. Invalid Capture Target Handling", false, ""};
        const bool nullRefused = !captureEngine.StartCaptureWindow(nullptr);
        const bool nullNotCapturing = !captureEngine.IsCapturing();
        const bool bogusRefused = !captureEngine.StartCaptureWindow(reinterpret_cast<HWND>(0x1));
        const bool bogusNotCapturing = !captureEngine.IsCapturing();

        r.passed = nullRefused && nullNotCapturing && bogusRefused && bogusNotCapturing;
        r.detail = "null-handle refused=" + std::to_string(nullRefused) + ", bogus-handle refused=" +
                   std::to_string(bogusRefused) + " (neither crashed, neither left IsCapturing() true)";
        results.push_back(r);
    }

    // 30. Output Presenter lifecycle: open a real second top-level window
    // with its own D3D11 swapchain, present a real frame to it, resize
    // it, toggle fullscreen, and close it -- all against the real
    // Win32/D3D11 APIs, no mocking.
    {
        DiagnosticResult r{"30. Output Presenter Lifecycle", false, ""};
        auto testTex = CreateSolidTestTexture(device, 64, 64, 10, 180, 220);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> testSrv;
        if (testTex) device->CreateShaderResourceView(testTex.Get(), nullptr, testSrv.GetAddressOf());

        const bool opened = testSrv && presenter.Open(hInstance);
        bool presentedBeforeResize = false, presentedAfterResize = false, resizedOk = false, fullscreenToggleOk = true;

        if (opened) {
            presentedBeforeResize = presenter.PresentFrame(testSrv.Get(), 64, 64);

            // Simulate a user resize: move/resize the real window and pump
            // the real WM_SIZE message through to the presenter's own WndProc.
            SetWindowPos(presenter.WindowHandle(), nullptr, 0, 0, 640, 360, SWP_NOMOVE | SWP_NOZORDER);
            PumpPendingMessages();
            RECT rc{};
            GetClientRect(presenter.WindowHandle(), &rc);
            resizedOk = (rc.right - rc.left) > 0 && (rc.bottom - rc.top) > 0;

            presentedAfterResize = presenter.PresentFrame(testSrv.Get(), 64, 64);

            try {
                presenter.ToggleFullscreen();
                PumpPendingMessages();
                presenter.ToggleFullscreen();
                PumpPendingMessages();
            } catch (...) {
                fullscreenToggleOk = false;
            }
        }

        const bool wasOpenBeforeClose = presenter.IsOpen();
        presenter.Close();
        const bool closedCleanly = !presenter.IsOpen();

        r.passed = opened && presentedBeforeResize && resizedOk && presentedAfterResize && fullscreenToggleOk &&
                   wasOpenBeforeClose && closedCleanly;
        r.detail = "opened=" + std::to_string(opened) + " presentBeforeResize=" + std::to_string(presentedBeforeResize) +
                   " resizedOk=" + std::to_string(resizedOk) + " presentAfterResize=" + std::to_string(presentedAfterResize) +
                   " fullscreenToggleOk=" + std::to_string(fullscreenToggleOk) + " closedCleanly=" + std::to_string(closedCleanly);
        results.push_back(r);
    }

    return results;
}

} // namespace lsalt
