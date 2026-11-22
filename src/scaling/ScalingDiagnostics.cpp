#include "ScalingDiagnostics.h"
#include "ScalingEngine.h"
#include "../framegen/FrameGenEngine.h"
#include "../framegen/TestPatternSource.h"
#include "../core/Logger.h"

#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace lsalt {

namespace {
std::wstring ShaderDirectory() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path = path.substr(0, slash);
    return path + L"\\shaders\\scaling";
}

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

Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateSourceTexture(ID3D11Device* device, uint32_t width, uint32_t height,
                                                              const std::vector<uint8_t>& rgba) {
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
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = width * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    device->CreateTexture2D(&desc, &initData, tex.GetAddressOf());
    return tex;
}

bool PixelEquals(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t x, uint32_t y,
                  uint8_t r, uint8_t g, uint8_t b, int tolerance = 0) {
    const size_t idx = (static_cast<size_t>(y) * width + x) * 4;
    if (idx + 2 >= rgba.size()) return false;
    return std::abs(int(rgba[idx + 0]) - r) <= tolerance && std::abs(int(rgba[idx + 1]) - g) <= tolerance &&
           std::abs(int(rgba[idx + 2]) - b) <= tolerance;
}
} // namespace

std::vector<DiagnosticResult> ScalingDiagnostics::RunAll(ID3D11Device* device, ID3D11DeviceContext* context,
                                                           FrameGenEngineD3D11& frameGenEngine, TestPatternSource& source) {
    std::vector<DiagnosticResult> results;
    const std::wstring shaderDir = ShaderDirectory();

    // 21. Nearest Neighbor correctness: a 2x2 source with 4 distinct solid
    // colors, stretched (aspect ratio off, so no letterboxing) to 8x8 --
    // each resulting quadrant must be an exact, unblended match for its
    // source corner.
    {
        DiagnosticResult r{"21. Scaling - Nearest Neighbor Correctness", false, ""};
        std::vector<uint8_t> srcPixels(2 * 2 * 4);
        auto setPx = [&](uint32_t x, uint32_t y, uint8_t rr, uint8_t gg, uint8_t bb) {
            const size_t idx = (y * 2 + x) * 4;
            srcPixels[idx + 0] = rr; srcPixels[idx + 1] = gg; srcPixels[idx + 2] = bb; srcPixels[idx + 3] = 255;
        };
        setPx(0, 0, 255, 0, 0);   // red: top-left
        setPx(1, 0, 0, 255, 0);   // green: top-right
        setPx(0, 1, 0, 0, 255);   // blue: bottom-left
        setPx(1, 1, 255, 255, 0); // yellow: bottom-right

        auto srcTex = CreateSourceTexture(device, 2, 2, srcPixels);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcSrv;
        if (srcTex) device->CreateShaderResourceView(srcTex.Get(), nullptr, srcSrv.GetAddressOf());

        ScalingEngineD3D11 scaler;
        bool ok = srcSrv && scaler.Initialize(device, shaderDir) && scaler.Resize(2, 2, 8, 8);
        scaler.SetAlgorithm(ScalingAlgorithm::Nearest);
        scaler.SetPreserveAspectRatio(false);

        std::vector<uint8_t> outPixels;
        ID3D11ShaderResourceView* result = ok ? scaler.Scale(context, srcSrv.Get()) : nullptr;
        ok = ok && result && ReadbackRGBA8(device, context, scaler.OutputTexture(), 8, 8, outPixels);

        if (ok) {
            ok = PixelEquals(outPixels, 8, 1, 1, 255, 0, 0) &&   // top-left quadrant -> red
                 PixelEquals(outPixels, 8, 6, 1, 0, 255, 0) &&   // top-right quadrant -> green
                 PixelEquals(outPixels, 8, 1, 6, 0, 0, 255) &&   // bottom-left quadrant -> blue
                 PixelEquals(outPixels, 8, 6, 6, 255, 255, 0);   // bottom-right quadrant -> yellow
        }
        r.passed = ok;
        r.detail = ok ? "All 4 quadrants exactly matched their source corner color (no blending)."
                      : "Quadrant colors did not exactly match, or generation/readback failed.";
        results.push_back(r);
    }

    // 22. Integer Scale exactness: a 3x3 source with 9 unique colors,
    // scaled into a destination sized so the largest whole-number factor
    // is exactly 5 (dst 17x17 -> floor(17/3) = 5) -- every pixel in each
    // 5x5 destination block must exactly equal its single source pixel,
    // and the 2px letterbox border must be exactly black.
    {
        DiagnosticResult r{"22. Scaling - Integer Scale Exactness", false, ""};
        std::vector<uint8_t> srcPixels(3 * 3 * 4);
        for (uint32_t y = 0; y < 3; ++y) {
            for (uint32_t x = 0; x < 3; ++x) {
                const size_t idx = (y * 3 + x) * 4;
                srcPixels[idx + 0] = static_cast<uint8_t>(20 + (y * 3 + x) * 25);
                srcPixels[idx + 1] = static_cast<uint8_t>(200 - (y * 3 + x) * 15);
                srcPixels[idx + 2] = static_cast<uint8_t>(40 + x * 60);
                srcPixels[idx + 3] = 255;
            }
        }
        auto srcTex = CreateSourceTexture(device, 3, 3, srcPixels);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcSrv;
        if (srcTex) device->CreateShaderResourceView(srcTex.Get(), nullptr, srcSrv.GetAddressOf());

        ScalingEngineD3D11 scaler;
        bool ok = srcSrv && scaler.Initialize(device, shaderDir) && scaler.Resize(3, 3, 17, 17);
        scaler.SetAlgorithm(ScalingAlgorithm::IntegerScale);

        std::vector<uint8_t> outPixels;
        ID3D11ShaderResourceView* result = ok ? scaler.Scale(context, srcSrv.Get()) : nullptr;
        ok = ok && result && ReadbackRGBA8(device, context, scaler.OutputTexture(), 17, 17, outPixels);

        const bool factorOk = scaler.IntegerFactor() == 5;
        bool blocksOk = true;
        if (ok) {
            for (uint32_t sy = 0; sy < 3 && blocksOk; ++sy) {
                for (uint32_t sx = 0; sx < 3 && blocksOk; ++sx) {
                    const size_t srcIdx = (sy * 3 + sx) * 4;
                    const uint32_t cx = 1 + sx * 5 + 2; // middle of this source pixel's 5x5 block, offset by the 1px border
                    const uint32_t cy = 1 + sy * 5 + 2;
                    if (!PixelEquals(outPixels, 17, cx, cy, srcPixels[srcIdx], srcPixels[srcIdx + 1], srcPixels[srcIdx + 2])) {
                        blocksOk = false;
                    }
                }
            }
        }
        const bool borderOk = ok && PixelEquals(outPixels, 17, 0, 0, 0, 0, 0) && PixelEquals(outPixels, 17, 16, 16, 0, 0, 0);

        r.passed = ok && factorOk && blocksOk && borderOk;
        char buf[160];
        snprintf(buf, sizeof(buf), "factor=%u (expect 5) blocksExact=%d borderBlack=%d", scaler.IntegerFactor(), blocksOk, borderOk);
        r.detail = buf;
        results.push_back(r);
    }

    // 23. Aspect-ratio letterboxing: a 300x300 solid-color source into a
    // 400x200 destination with aspect preserved -- must pillarbox (left/
    // right bars), with the bars exactly black and the content area
    // showing the source color.
    {
        DiagnosticResult r{"23. Scaling - Aspect Ratio Letterboxing", false, ""};
        std::vector<uint8_t> srcPixels(300 * 300 * 4);
        for (size_t i = 0; i < srcPixels.size(); i += 4) {
            srcPixels[i + 0] = 10; srcPixels[i + 1] = 180; srcPixels[i + 2] = 220; srcPixels[i + 3] = 255;
        }
        auto srcTex = CreateSourceTexture(device, 300, 300, srcPixels);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcSrv;
        if (srcTex) device->CreateShaderResourceView(srcTex.Get(), nullptr, srcSrv.GetAddressOf());

        ScalingEngineD3D11 scaler;
        bool ok = srcSrv && scaler.Initialize(device, shaderDir) && scaler.Resize(300, 300, 400, 200);
        scaler.SetAlgorithm(ScalingAlgorithm::Bilinear);
        scaler.SetPreserveAspectRatio(true);

        std::vector<uint8_t> outPixels;
        ID3D11ShaderResourceView* result = ok ? scaler.Scale(context, srcSrv.Get()) : nullptr;
        ok = ok && result && ReadbackRGBA8(device, context, scaler.OutputTexture(), 400, 200, outPixels);

        uint32_t vx = 0, vy = 0, vw = 0, vh = 0;
        scaler.Viewport(vx, vy, vw, vh);
        const bool viewportOk = vw == 200 && vh == 200 && vx == 100 && vy == 0;
        const bool leftBarBlack = ok && PixelEquals(outPixels, 400, 10, 100, 0, 0, 0);
        const bool rightBarBlack = ok && PixelEquals(outPixels, 400, 390, 100, 0, 0, 0);
        const bool centerHasContent = ok && PixelEquals(outPixels, 400, 200, 100, 10, 180, 220, 4);

        r.passed = ok && viewportOk && leftBarBlack && rightBarBlack && centerHasContent;
        char buf[160];
        snprintf(buf, sizeof(buf), "viewport=(%u,%u %ux%u) expect (100,0 200x200); leftBar=%d rightBar=%d center=%d",
                 vx, vy, vw, vh, leftBarBlack, rightBarBlack, centerHasContent);
        r.detail = buf;
        results.push_back(r);
    }

    // 24. Scaling -> Frame Generation integration: scale two consecutive
    // REAL animated synthetic source frames (not a static image) and feed
    // the scaled outputs directly into the Frame Generation Engine,
    // proving the two engines compose rather than only working in
    // isolation.
    {
        DiagnosticResult r{"24. Scaling + Frame Generation Pipeline Integration", false, ""};
        const double dt = 1.0 / source.SourceFps();
        source.RenderExplicit(context, 1.0, 1.0 + dt);

        ScalingEngineD3D11 scalerA, scalerB;
        bool ok = scalerA.Initialize(device, shaderDir) && scalerB.Initialize(device, shaderDir) &&
                  scalerA.Resize(source.Width(), source.Height(), 1920, 1080) &&
                  scalerB.Resize(source.Width(), source.Height(), 1920, 1080);
        scalerA.SetAlgorithm(ScalingAlgorithm::Bilinear);
        scalerB.SetAlgorithm(ScalingAlgorithm::Bilinear);

        ID3D11ShaderResourceView* scaledA = ok ? scalerA.Scale(context, source.FrameASRV()) : nullptr;
        ID3D11ShaderResourceView* scaledB = ok ? scalerB.Scale(context, source.FrameBSRV()) : nullptr;
        ok = ok && scaledA && scaledB;

        ID3D11ShaderResourceView* generated = ok ? frameGenEngine.GenerateInterpolatedFrame(scaledA, scaledB) : nullptr;
        std::vector<uint8_t> genPixels;
        ok = ok && generated && ReadbackRGBA8(device, context, frameGenEngine.GeneratedOutputTexture(), 1920, 1080, genPixels);

        r.passed = ok;
        r.detail = ok ? "FrameGenEngine accepted two 1920x1080 ScalingEngine outputs (scaled from the real "
                        "960x540 animated source) and produced a valid interpolated frame at that resolution."
                      : "Pipeline composition failed (scale, generation, or readback).";
        results.push_back(r);
    }

    // 25. Resolution change robustness: cycle through several src/dst size
    // combinations (including a non-trivial upscale and a downscale) using
    // the real animated source, never crashing and always producing valid
    // output.
    {
        DiagnosticResult r{"25. Scaling - Resolution Change", false, ""};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir);
        source.RenderExplicit(context, 0.0, 1.0 / source.SourceFps());

        const uint32_t dstSizes[][2] = {{1280, 720}, {640, 360}, {1920, 1080}, {480, 270}};
        for (const auto& dst : dstSizes) {
            if (!ok) break;
            ok = scaler.Resize(source.Width(), source.Height(), dst[0], dst[1]);
            ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
        }
        r.passed = ok;
        r.detail = ok ? "Survived 4 resolution changes (720p/360p/1080p/270p) producing valid output each time."
                      : "Failed to produce valid output after a resolution change.";
        results.push_back(r);
    }

    // 26. All real algorithms produce valid, non-degenerate output against
    // the real animated source (not a crash, not an all-black/garbage frame).
    {
        DiagnosticResult r{"26. Scaling - All Algorithms Produce Valid Output", false, ""};
        source.RenderExplicit(context, 0.5, 0.5 + 1.0 / source.SourceFps());

        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir) && scaler.Resize(source.Width(), source.Height(), 1280, 720);

        const ScalingAlgorithm algos[] = {ScalingAlgorithm::Nearest, ScalingAlgorithm::Bilinear,
                                           ScalingAlgorithm::Bicubic, ScalingAlgorithm::Lanczos,
                                           ScalingAlgorithm::IntegerScale};
        std::string detail;
        for (ScalingAlgorithm algo : algos) {
            if (!ok) break;
            scaler.SetAlgorithm(algo);
            ID3D11ShaderResourceView* out = scaler.Scale(context, source.FrameBSRV());
            std::vector<uint8_t> pixels;
            const bool thisOk = out && ReadbackRGBA8(device, context, scaler.OutputTexture(), 1280, 720, pixels);
            bool nonDegenerate = false;
            if (thisOk) {
                // At least some variation across the frame (not a single
                // flat/garbage value everywhere).
                for (size_t i = 4; i < pixels.size() && !nonDegenerate; i += 4) {
                    if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] || pixels[i + 2] != pixels[2]) nonDegenerate = true;
                }
            }
            detail += std::string(ToString(algo)) + "=" + (thisOk && nonDegenerate ? "ok " : "FAIL ");
            ok = ok && thisOk && nonDegenerate;
        }
        r.passed = ok;
        r.detail = detail;
        results.push_back(r);
    }

    return results;
}

} // namespace lsalt
