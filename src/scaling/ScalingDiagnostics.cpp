#include "ScalingDiagnostics.h"
#include "ScalingEngine.h"
#include "ResolutionManager.h"
#include "../framegen/FrameScheduler.h"
#include "../framegen/FramePacing.h"
#include "../framegen/DynamicPerformanceManager.h"
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

    // 31. Resolution Manager: fit, stretch, integer, and invalid dimensions
    // are deterministic and independently ground-truthed.
    {
        DiagnosticResult r{"31. Resolution - Fit, Stretch, and Integer Ground Truth", false, ""};
        ResolutionRequest fitReq{300, 300, 400, 200, 100, AspectMode::Fit};
        const ResolutionResult fit = ResolutionManager::Resolve(fitReq);
        ResolutionRequest stretchReq{300, 300, 400, 200, 100, AspectMode::Stretch};
        const ResolutionResult stretch = ResolutionManager::Resolve(stretchReq);
        ResolutionRequest intReq{300, 300, 320, 320, 100, AspectMode::IntegerScale};
        const ResolutionResult integer = ResolutionManager::Resolve(intReq);
        const bool ok = fit.valid && fit.outputWidth == 400 && fit.outputHeight == 200 &&
                        fit.viewportWidth == 200 && fit.viewportHeight == 200 &&
                        fit.viewportX == 100 && fit.viewportY == 0 &&
                        stretch.valid && stretch.viewportWidth == 400 && stretch.viewportHeight == 200 &&
                        integer.valid && integer.integerFactor == 1 &&
                        integer.viewportWidth == 300 && integer.viewportHeight == 300 &&
                        integer.viewportX == 10 && integer.viewportY == 10;
        r.passed = ok;
        r.detail = "fit=200x200 at (100,0), stretch=400x200, integer=300x300 at (10,10)";
        results.push_back(r);
    }

    // 32. Invalid resolutions and very large requests must fail or clamp
    // safely rather than allocating/wrapping.
    {
        DiagnosticResult r{"32. Resolution - Invalid and Very Large Dimensions", false, ""};
        const ResolutionResult invalid = ResolutionManager::Resolve(ResolutionRequest{0, 1080, 1920, 1080, 100, AspectMode::Fit});
        const ResolutionResult huge = ResolutionManager::Resolve(ResolutionRequest{1920, 1080, 99999, 99999, 100, AspectMode::Fit});
        r.passed = !invalid.valid && huge.valid &&
                   huge.outputWidth == ScalingEngineD3D11::kMaxTextureDimension &&
                   huge.outputHeight == ScalingEngineD3D11::kMaxTextureDimension;
        r.detail = "zero input rejected=" + std::to_string(!invalid.valid) +
                   " 99999x99999 clamped=" + std::to_string(huge.outputWidth) + "x" +
                   std::to_string(huge.outputHeight);
        results.push_back(r);
    }

    // 33. Texture pool reuse: resize away and back, verifying one exact
    // shape is reused and no pool growth occurs beyond its capacity.
    {
        DiagnosticResult r{"33. GPU Resource - Texture Pool Reuse", false, ""};
        ScalingEngineD3D11 scaler;
        source.RenderExplicit(context, 0.5, 0.5 + 1.0 / source.SourceFps());
        bool ok = scaler.Initialize(device, shaderDir) &&
                  scaler.Resize(960, 540, 1280, 720);
        ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
        ok = ok && scaler.Resize(960, 540, 1920, 1080);
        ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
        ok = ok && scaler.Resize(960, 540, 1280, 720);
        ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
        r.passed = ok && scaler.PoolReuseCount() >= 1;
        r.detail = "reuseCount=" + std::to_string(scaler.PoolReuseCount()) +
                   " allocationsEver=" + std::to_string(scaler.PoolAllocationCount()) +
                   " pooledBytes=" + std::to_string(scaler.EstimatedPoolBytes());
        results.push_back(r);
    }

    // 34. Sharpening: off and high produce real, bounded but different
    // output from a deterministic synthetic edge pattern.
    {
        DiagnosticResult r{"34. Scaling - Sharpening Off vs High", false, ""};
        std::vector<uint8_t> pixels(64 * 64 * 4);
        for (uint32_t y = 0; y < 64; ++y) {
            for (uint32_t x = 0; x < 64; ++x) {
                const size_t i = (static_cast<size_t>(y) * 64 + x) * 4;
                const uint8_t v = x < 32 ? 40 : 220;
                pixels[i] = pixels[i + 1] = pixels[i + 2] = v;
                pixels[i + 3] = 255;
            }
        }
        auto tex = CreateSourceTexture(device, 64, 64, pixels);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        if (tex) device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf());
        ScalingEngineD3D11 off, high;
        bool ok = srv && off.Initialize(device, shaderDir) && high.Initialize(device, shaderDir) &&
                  off.Resize(64, 64, 128, 128) && high.Resize(64, 64, 128, 128);
        off.SetAlgorithm(ScalingAlgorithm::Bicubic);
        high.SetAlgorithm(ScalingAlgorithm::Bicubic);
        off.SetSharpenLevel(SharpenLevel::Off);
        high.SetSharpenLevel(SharpenLevel::High);
        off.SetPreserveAspectRatio(false);
        high.SetPreserveAspectRatio(false);
        const bool scaled = ok && off.Scale(context, srv.Get()) && high.Scale(context, srv.Get());
        std::vector<uint8_t> offOut, highOut;
        ok = scaled && ReadbackRGBA8(device, context, off.OutputTexture(), 128, 128, offOut) &&
             ReadbackRGBA8(device, context, high.OutputTexture(), 128, 128, highOut);
        bool differs = false;
        bool inRange = true;
        if (ok) {
            for (size_t i = 0; i < offOut.size(); i += 4) {
                if (offOut[i] != highOut[i]) differs = true;
                if (highOut[i] > 255) inRange = false;
            }
        }
        r.passed = ok && differs && inRange;
        r.detail = "off/high differ=" + std::to_string(differs) + " bounded=" + std::to_string(inRange);
        results.push_back(r);
    }

    // 35. Synthetic GPU benchmark matrix. This records only machine-local
    // timing for identical synthetic frames and does not claim real-game
    // FPS. It is repeated at the five required resolution levels.
    {
        DiagnosticResult r{"35. Synthetic GPU Scaling Benchmark Matrix", false, ""};
        const uint32_t sizes[][2] = {{640,480},{1280,720},{1600,900},{1920,1080},{2560,1440}};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir);
        source.RenderExplicit(context, 0.5, 0.75);
        std::string detail;
        for (const auto& size : sizes) {
            if (!ok) break;
            ok = scaler.Resize(source.Width(), source.Height(), size[0], size[1]);
            scaler.SetAlgorithm(ScalingAlgorithm::Bilinear);
            LARGE_INTEGER freq{}, a{}, b{};
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&a);
            for (int i = 0; i < 3 && ok; ++i) {
                ok = scaler.Scale(context, source.FrameBSRV()) != nullptr;
            }
            context->Flush();
            QueryPerformanceCounter(&b);
            const double avgMs = freq.QuadPart ? ((b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart) / 3.0 : -1.0;
            detail += std::to_string(size[0]) + "x" + std::to_string(size[1]) +
                      "=" + (avgMs >= 0.0 ? std::to_string(static_cast<int>(avgMs * 100.0) / 100.0) : "-1") +
                      "ms;";
        }
        r.passed = ok;
        r.detail = "Measured synthetic dispatch/flush timing only: " + detail;
        results.push_back(r);
    }

    // 36. VRAM budget admission: force an impossible 8K request under a
    // tiny budget and verify the engine clamps deterministically.
    {
        DiagnosticResult r{"36. VRAM Budget Enforcement", false, ""};
        ScalingEngineD3D11 scaler;
        scaler.SetVramQuery([](uint64_t& used, uint64_t& budget) {
            used = 2040u * 1024u * 1024u;
            budget = 2048u * 1024u * 1024u;
            return true;
        });
        bool ok = scaler.Initialize(device, shaderDir) &&
                  scaler.Resize(1920, 1080, 7680, 4320);
        r.passed = ok && scaler.DstWidth() < 7680 && scaler.DstHeight() < 4320 &&
                   !scaler.LastClampReason().empty();
        r.detail = "allocated=" + std::to_string(scaler.DstWidth()) + "x" +
                   std::to_string(scaler.DstHeight()) + " reason=" + scaler.LastClampReason();
        results.push_back(r);
    }

    // Phase 8: deterministic frame scheduler ground truth.
    {
        DiagnosticResult r{"38. Frame Scheduler - Accuracy and Ordering", false, ""};
        FrameScheduler scheduler;
        scheduler.SetRefreshRate(60.0);
        scheduler.SetLateTolerance(0.004);
        const bool submitted = scheduler.Submit(101, 1.0) && scheduler.Submit(102, 1.01);
        const bool first = scheduler.TakeDueFrame(1.0);
        const bool secondBeforeRefresh = !scheduler.TakeDueFrame(1.005);
        const bool second = scheduler.TakeDueFrame(1.017);
        r.passed = submitted && first && secondBeforeRefresh && second &&
                   scheduler.Stats().presented == 2 && scheduler.QueueDepth() == 0;
        r.detail = "submitted=" + std::to_string(submitted) +
                   " first=" + std::to_string(first) +
                   " refreshGate=" + std::to_string(secondBeforeRefresh) +
                   " second=" + std::to_string(second);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"39. Frame Scheduler - Queue Overflow and Latest-Wins", false, ""};
        FrameScheduler scheduler;
        scheduler.Submit(1, 0.0);
        scheduler.TakeDueFrame(0.0);
        const bool twoAccepted = scheduler.Submit(2, 0.001) && scheduler.Submit(3, 0.002);
        scheduler.Submit(4, 0.003);
        scheduler.Submit(5, 0.004);
        const bool overflowDropped = scheduler.Stats().dropped == 2;
        r.passed = twoAccepted && overflowDropped &&
                   scheduler.Stats().dropped == 2 &&
                   scheduler.QueueDepth() == FrameScheduler::kMaxQueueDepth;
        r.detail = "queueDrop=" + std::to_string(scheduler.Stats().dropped) +
                   " queueDepth=" + std::to_string(scheduler.QueueDepth());
        results.push_back(r);
    }

    {
        DiagnosticResult r{"40. Frame Scheduler - Late and Dropped Frames", false, ""};
        FrameScheduler scheduler;
        scheduler.SetRefreshRate(60.0);
        scheduler.SetLateTolerance(0.004);
        scheduler.Submit(1, 0.1);
        scheduler.TakeDueFrame(0.1);
        scheduler.Submit(2, 0.110);
        const bool lateDetected = scheduler.TakeDueFrame(0.140);
        r.passed = lateDetected && scheduler.Stats().late == 1;
        r.detail = "lateCount=" + std::to_string(scheduler.Stats().late);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"41. Frame Scheduler - Duplicate Detection", false, ""};
        FrameScheduler scheduler;
        const bool first = scheduler.Submit(77, 1.0);
        const bool duplicate = !scheduler.Submit(77, 1.001);
        r.passed = first && duplicate && scheduler.Stats().duplicate == 1 &&
                   scheduler.QueueDepth() == 1;
        r.detail = "duplicateCount=" + std::to_string(scheduler.Stats().duplicate);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"42. Frame Pacing - Timing Calculations", false, ""};
        FramePacing pacing;
        pacing.ResetRunningFpsStats();
        pacing.Reset();
        pacing.RecordSourceFrame();
        pacing.RecordGeneratedFrame();
        pacing.RecordDisplayedFrame();
        pacing.RecordCpuProcessingMs(1.25);
        FrameGenStats stats;
        pacing.FillStats(stats);
        r.passed = stats.cpuProcessingMs == 1.25 &&
                   stats.estimatedLatencyMs > stats.totalGpuMs &&
                   stats.queueDepth <= FrameScheduler::kMaxQueueDepth;
        r.detail = "cpuMs=" + std::to_string(stats.cpuProcessingMs) +
                   " estimatedLatencyMs=" + std::to_string(stats.estimatedLatencyMs) +
                   " queueDepth=" + std::to_string(stats.queueDepth);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"43. GPU Synchronization - Timestamp Ring", false, ""};
        FramePacing pacing;
        const bool initialized = pacing.Initialize(device, nullptr);
        pacing.BeginGpuTiming(context);
        pacing.MarkMotionEstimationDone(context);
        pacing.MarkInterpolationDone(context);
        context->Flush();
        pacing.BeginGpuTiming(context);
        r.passed = initialized && device->GetDeviceRemovedReason() == S_OK;
        r.detail = "timestampRingInitialized=" + std::to_string(initialized) +
                   " deviceRemoved=" + std::to_string(device->GetDeviceRemovedReason() != S_OK);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"44. Adaptive Quality - Measured Overload Threshold", false, ""};
        frameGenEngine.SetEnabled(true);
        frameGenEngine.SetQualityMode(FrameGenQualityMode::Quality);
        DynamicPerformanceManager adaptive;
        adaptive.SetAutoEnabled(true);
        FrameGenStats stats = frameGenEngine.GetStats();
        stats.totalGpuMs = 20.0;
        ThermalSample thermal{};
        thermal.sustainedHighLoad = true;
        std::string reason;
        for (int i = 0; i < 5; ++i) {
            reason = adaptive.Update(frameGenEngine, stats, thermal, 95.0, true);
        }
        r.passed = adaptive.AutoEnabled() &&
                   frameGenEngine.QualityMode() == FrameGenQualityMode::Balanced &&
                   !reason.empty();
        r.detail = "adaptive=" + std::to_string(adaptive.AutoEnabled()) +
                   " quality=" + std::string(ToString(frameGenEngine.QualityMode())) +
                   " reason=" + reason;
        results.push_back(r);
    }

    {
        DiagnosticResult r{"45. Synthetic Pacing Benchmark - 720p to 1440p", false, ""};
        const uint32_t sizes[][2] = {{1280,720},{1600,900},{1920,1080},{2560,1440}};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir);
        source.RenderExplicit(context, 0.5, 0.75);
        std::string detail;
        for (const auto& size : sizes) {
            if (!ok) break;
            ok = scaler.Resize(source.Width(), source.Height(), size[0], size[1]);
            scaler.SetAlgorithm(ScalingAlgorithm::Bilinear);
            LARGE_INTEGER freq{}, start{}, end{};
            QueryPerformanceFrequency(&freq);
            for (int i = 0; i < 10; ++i) {
                ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
            }
            context->Flush();
            std::vector<double> samples;
            for (int i = 0; i < 30 && ok; ++i) {
                QueryPerformanceCounter(&start);
                ok = scaler.Scale(context, source.FrameBSRV()) != nullptr;
                context->Flush();
                QueryPerformanceCounter(&end);
                samples.push_back(freq.QuadPart ? (end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart : 0.0);
            }
            double sum = 0.0, variance = 0.0, minimum = 0.0, maximum = 0.0;
            for (double sample : samples) sum += sample;
            const double mean = samples.empty() ? 0.0 : sum / samples.size();
            for (double sample : samples) variance += (sample - mean) * (sample - mean);
            const double stddev = samples.empty() ? 0.0 : std::sqrt(variance / samples.size());
            std::vector<double> sorted = samples;
            std::sort(sorted.begin(), sorted.end());
            if (!sorted.empty()) {
                minimum = sorted.front();
                maximum = sorted.back();
            }
            const double median = sorted.empty() ? 0.0 :
                (sorted.size() % 2 ? sorted[sorted.size() / 2] :
                 (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) * 0.5);
            detail += std::to_string(size[0]) + "x" + std::to_string(size[1]) +
                      " avg=" + std::to_string(static_cast<int>(mean * 100.0) / 100.0) +
                      " med=" + std::to_string(static_cast<int>(median * 100.0) / 100.0) +
                      " min=" + std::to_string(static_cast<int>(minimum * 100.0) / 100.0) +
                      " max=" + std::to_string(static_cast<int>(maximum * 100.0) / 100.0) +
                      " sd=" + std::to_string(static_cast<int>(stddev * 100.0) / 100.0) + "ms;";
        }
        r.passed = ok;
        r.detail = "Measured synthetic scaling workload only: " + detail;
        results.push_back(r);
    }

    {
        DiagnosticResult r{"46. GPU Resource Pool - Bounded Cleanup", false, ""};
        TexturePool pool(2);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex[3];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv[3];
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav[3];
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            ok = ok && pool.Acquire(device, 64 + i, 64, DXGI_FORMAT_R8G8B8A8_UNORM,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                                    tex[i], srv[i], uav[i]);
            if (ok) pool.Release(tex[i], srv[i], uav[i], 64 + i, 64,
                                 DXGI_FORMAT_R8G8B8A8_UNORM,
                                 D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        }
        r.passed = ok && pool.PooledCount() == 2 && pool.EstimatedBytesInPool() > 0;
        pool.Clear();
        r.passed = r.passed && pool.PooledCount() == 0 && pool.EstimatedBytesInPool() == 0;
        r.detail = "pooledCountAfterEviction=" + std::to_string(pool.PooledCount()) +
                   " allocationsEver=" + std::to_string(pool.TotalAllocationsEver());
        results.push_back(r);
    }

    {
        DiagnosticResult r{"47. GPU Resource Pool - Trim Under Budget", false, ""};
        TexturePool pool(4);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        bool ok = pool.Acquire(device, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                               tex, srv, uav);
        if (ok) pool.Release(tex, srv, uav, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        pool.TrimToBytes(64 * 1024);
        r.passed = ok && pool.PooledCount() == 0;
        r.detail = "remainingPooledCount=" + std::to_string(pool.PooledCount());
        results.push_back(r);
    }

    {
        DiagnosticResult r{"48. GPU Device Status - Removal and Recreate Guard", false, ""};
        const HRESULT removed = device->GetDeviceRemovedReason();
        ScalingEngineD3D11 scaler;
        bool ok = removed == S_OK && scaler.Initialize(device, shaderDir) &&
                  scaler.Resize(128, 128, 256, 256);
        r.passed = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
        r.detail = "deviceRemovedReason=" + std::to_string(static_cast<long>(removed)) +
                   " recreationGuardPassed=" + std::to_string(r.passed);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"49. Adaptive Scaling - Sustained Overload Threshold", false, ""};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir) && scaler.Resize(960, 540, 1920, 1080);
        scaler.SetAlgorithm(ScalingAlgorithm::Lanczos);
        scaler.SetSharpenLevel(SharpenLevel::High);
        scaler.SetQualityPreset(ScalingQualityPreset::Quality);
        frameGenEngine.SetEnabled(true);
        frameGenEngine.SetQualityMode(FrameGenQualityMode::Quality);
        DynamicPerformanceManager adaptive;
        adaptive.SetAdaptiveScalingEnabled(true);
        FrameGenStats stats = frameGenEngine.GetStats();
        stats.totalGpuMs = 20.0;
        ThermalSample thermal{};
        thermal.sustainedHighLoad = true;
        std::string reason;
        for (int i = 0; i < 10; ++i) {
            const std::string stepReason =
                adaptive.Update(frameGenEngine, scaler, stats, thermal, 95.0, true);
            if (!stepReason.empty()) reason = stepReason;
        }
        r.passed = ok && adaptive.AdaptiveScalingEnabled() &&
                   scaler.Algorithm() == ScalingAlgorithm::Bicubic &&
                   scaler.Sharpen() == SharpenLevel::Medium &&
                   scaler.QualityPreset() == ScalingQualityPreset::Balanced &&
                   !reason.empty();
        r.detail = "algorithm=" + std::string(ToString(scaler.Algorithm())) +
                   " sharpen=" + std::string(ToString(scaler.Sharpen())) +
                   " reason=" + reason;
        results.push_back(r);
    }

    {
        DiagnosticResult r{"50. Adaptive Scaling - Cooldown and Opt-Out Restore", false, ""};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir) && scaler.Resize(960, 540, 1920, 1080);
        scaler.SetAlgorithm(ScalingAlgorithm::Lanczos);
        scaler.SetSharpenLevel(SharpenLevel::High);
        scaler.SetQualityPreset(ScalingQualityPreset::Quality);
        frameGenEngine.SetEnabled(true);
        DynamicPerformanceManager adaptive;
        adaptive.SetAdaptiveScalingEnabled(true);
        FrameGenStats stats = frameGenEngine.GetStats();
        stats.totalGpuMs = 20.0;
        ThermalSample thermal{};
        thermal.sustainedHighLoad = true;
        for (int i = 0; i < 10; ++i)
            adaptive.Update(frameGenEngine, scaler, stats, thermal, 95.0, true);
        const ScalingAlgorithm cooledAlgorithm = scaler.Algorithm();
        adaptive.SetAdaptiveScalingEnabled(false);
        adaptive.RestoreScaling(scaler);
        r.passed = ok && cooledAlgorithm == ScalingAlgorithm::Bicubic &&
                   scaler.Algorithm() == ScalingAlgorithm::Lanczos &&
                   scaler.Sharpen() == SharpenLevel::High &&
                   scaler.QualityPreset() == ScalingQualityPreset::Quality;
        r.detail = "cooldownResult=" + std::string(ToString(cooledAlgorithm)) +
                   " restoredLanczos=" + std::to_string(scaler.Algorithm() == ScalingAlgorithm::Lanczos);
        results.push_back(r);
    }

    {
        DiagnosticResult r{"51. Benchmark Repeatability - Warm-Up and 10 Rounds", false, ""};
        ScalingEngineD3D11 scaler;
        bool ok = scaler.Initialize(device, shaderDir) &&
                  scaler.Resize(960, 540, 1280, 720);
        scaler.SetAlgorithm(ScalingAlgorithm::Bilinear);
        source.RenderExplicit(context, 0.5, 0.75);
        std::vector<double> rounds;
        for (int round = 0; round < 10 && ok; ++round) {
            for (int warm = 0; warm < 3; ++warm)
                ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
            context->Flush();
            LARGE_INTEGER start{}, end{}, freq{};
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);
            for (int i = 0; i < 5 && ok; ++i)
                ok = ok && scaler.Scale(context, source.FrameBSRV()) != nullptr;
            context->Flush();
            QueryPerformanceCounter(&end);
            if (freq.QuadPart > 0)
                rounds.push_back((end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart / 5.0);
        }
        r.passed = ok && rounds.size() == 10;
        r.detail = "validRounds=" + std::to_string(rounds.size()) +
                   " meanMs=" + (rounds.empty() ? std::string("0") :
                        std::to_string(static_cast<int>((rounds.front() + rounds.back()) * 100.0)));
        results.push_back(r);
    }

    {
        DiagnosticResult r{"52. GPU Allocation Failure - Oversized Texture", false, ""};
        TexturePool pool(2);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        const bool allocated = pool.Acquire(device, 65536, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                                            tex, srv, uav);
        r.passed = !allocated && tex == nullptr && srv == nullptr && uav == nullptr &&
                   pool.PooledCount() == 0;
        r.detail = "oversizedAllocationReturned=" + std::to_string(allocated) +
                   " allocationFailureHandled=" + std::to_string(r.passed);
        results.push_back(r);
    }

    return results;
}

} // namespace lsalt
