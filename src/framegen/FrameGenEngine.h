#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include "../core/Types.h"
#include "FrameGenTypes.h"

namespace lsalt {

// Interface the rest of the app (ScalingEngine, UI) programs against.
// FrameGenEngineD3D11 below is the first concrete implementation: real
// motion-compensated interpolation (block-matching motion estimation +
// forward/backward warping + occlusion-aware blending), not frame
// duplication. It currently has no real game frames to run on -- the
// Capture Engine (ICaptureEngine in capture/CaptureEngine.h) is still an
// unimplemented stub -- so it is exercised through a synthetic test
// pattern source (see framegen/TestPatternSource.h) until capture lands.
class IFrameGenEngine {
public:
    virtual ~IFrameGenEngine() = default;
    virtual bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND windowForRefreshRate) = 0;
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool SetMultiplier(FrameGenMultiplier multiplier) = 0; // false if unsupported (3x/4x)
    virtual void SetQualityMode(FrameGenQualityMode quality) = 0;
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    // Given frameA (older) and frameB (newer) as D3D11 shader-resource-
    // bindable textures, produces one interpolated frame at t=0.5 (2x mode
    // only -- see SetMultiplier). Returns the generated frame's SRV, or
    // null if generation is disabled/unsupported/failed.
    virtual ID3D11ShaderResourceView* GenerateInterpolatedFrame(ID3D11ShaderResourceView* frameA,
                                                                 ID3D11ShaderResourceView* frameB) = 0;

    virtual const FrameGenCapabilities& Capabilities() const = 0;
    virtual FrameGenStats GetStats() const = 0;
    virtual bool IsEnabled() const = 0;
};

// Concrete D3D11 implementation. Owns MotionEstimator, FrameInterpolator
// and FramePacing, and performs runtime hardware-capability detection plus
// device-removed handling so the UI can show *why* generation is
// unavailable instead of silently doing nothing.
class FrameGenEngineD3D11 : public IFrameGenEngine {
public:
    FrameGenEngineD3D11();
    // Declared and defined out-of-line (in the .cpp, where MotionEstimator/
    // FrameInterpolator/FramePacing are fully defined) so callers that only
    // include this header -- like App.h -- don't need those headers just to
    // destroy a unique_ptr<FrameGenEngineD3D11>.
    ~FrameGenEngineD3D11() override;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND windowForRefreshRate) override;
    void SetEnabled(bool enabled) override;
    bool SetMultiplier(FrameGenMultiplier multiplier) override;
    void SetQualityMode(FrameGenQualityMode quality) override;
    bool Resize(uint32_t width, uint32_t height) override;

    ID3D11ShaderResourceView* GenerateInterpolatedFrame(ID3D11ShaderResourceView* frameA,
                                                         ID3D11ShaderResourceView* frameB) override;

    const FrameGenCapabilities& Capabilities() const override { return m_capabilities; }
    FrameGenStats GetStats() const override;

    // Phase 5: call once per real IDXGISwapChain::Present() call, while
    // Live Engine is active, right after Present() succeeds -- this is
    // what FrameGenStats::displayedFps now measures. See FrameGenTypes.h's
    // comment on displayedFps for exactly what this does and doesn't
    // represent (this app's own present cadence, not a captured game's).
    void RecordPresentedFrame();
    // Phase 4: starts a fresh avg/min/max displayed-FPS window (e.g. a
    // "Reset Stats" button), without touching any other pacing/timing state.
    void ResetRunningFpsStats();
    bool IsEnabled() const override { return m_enabled && m_capabilities.computeShadersSupported; }

    FrameGenMultiplier Multiplier() const { return m_multiplier; }
    FrameGenQualityMode QualityMode() const { return m_quality; }

    // Phase 3: lets the engine check real VRAM headroom (via
    // GpuDeviceManager::QueryVideoMemoryUsage, a real DXGI budget query --
    // see PerfMonitor) before allocating a quality mode's resources, and
    // automatically step down to a lower quality mode with a logged, UI-
    // visible reason instead of risking a failed/thrashing allocation on
    // the MX150's 2GB VRAM budget. Optional: if never set, no VRAM guard
    // is applied. `query` returns false if VRAM usage isn't queryable on
    // this adapter/driver (matches PerfMonitor's own fallback).
    void SetVramQuery(std::function<bool(uint64_t& usedBytes, uint64_t& budgetBytes)> query) {
        m_vramQuery = std::move(query);
    }
    // Exposed only so DiagnosticTests can temporarily install a synthetic
    // low-budget query to exercise the VRAM-guard code path deterministically
    // and then restore the real one afterward -- not meant for general use.
    const std::function<bool(uint64_t&, uint64_t&)>& VramQuery() const { return m_vramQuery; }

    // Exposed for the UI preview and DiagnosticTests' CPU readback checks.
    ID3D11Texture2D* GeneratedOutputTexture() const;
    ID3D11ShaderResourceView* GeneratedOutputSRV() const;
    ID3D11Texture2D* OcclusionMaskTexture() const;
    uint32_t OcclusionMaskWidth() const;
    uint32_t OcclusionMaskHeight() const;
    double DetectedRefreshHz() const;
    const std::deque<float>& FrameTimeHistoryMs() const;

private:
    bool DetectCapabilities(ID3D11Device* device);
    bool RecreatePipeline();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    HWND m_window = nullptr;

    std::unique_ptr<class MotionEstimator> m_motionEstimator;
    std::unique_ptr<class FrameInterpolator> m_interpolator;
    std::unique_ptr<class FramePacing> m_pacing;

    FrameGenCapabilities m_capabilities;
    FrameGenStats m_cachedStats;
    std::function<bool(uint64_t&, uint64_t&)> m_vramQuery;
    std::string m_lastAutoAdjustReason;

    bool m_enabled = false;
    bool m_initialized = false;
    FrameGenMultiplier m_multiplier = FrameGenMultiplier::X2;
    FrameGenQualityMode m_quality = FrameGenQualityMode::Balanced;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace lsalt
