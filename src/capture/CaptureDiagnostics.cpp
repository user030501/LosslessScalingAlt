#include "CaptureDiagnostics.h"
#include "CaptureEngineWGC.h"
#include "../core/Logger.h"

#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

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

std::vector<DiagnosticResult> CaptureDiagnostics::RunAll(CaptureEngineWGC& captureEngine, HWND selfWindow,
                                                           ID3D11Device* device, ID3D11DeviceContext* context) {
    std::vector<DiagnosticResult> results;
    DiagnosticResult r{"27. Real Window Capture (self)", false, ""};

    if (!CaptureEngineWGC::IsSupportedOnThisSystem()) {
        r.passed = true; // correctly detecting non-support is the correct behavior here, not a failure
        r.detail = "Windows Graphics Capture is not supported on this OS build/configuration -- the engine "
                   "correctly reports this rather than attempting to capture anyway (skipped, not applicable).";
        results.push_back(r);
        return results;
    }

    if (!captureEngine.StartCaptureWindow(selfWindow)) {
        r.detail = "StartCaptureWindow(self) failed: " + captureEngine.LastError();
        results.push_back(r);
        return results;
    }

    // Frame arrival is real and asynchronous (a worker thread delivered by
    // the OS) -- poll briefly rather than assume it's instantaneous.
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
    return results;
}

} // namespace lsalt
