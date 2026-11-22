#pragma once
#include <wrl/client.h>
#include <d3d11.h>
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
    bool IsEnabled() const override { return m_enabled && m_capabilities.computeShadersSupported; }

    FrameGenMultiplier Multiplier() const { return m_multiplier; }
    FrameGenQualityMode QualityMode() const { return m_quality; }

    // Exposed for the UI preview and DiagnosticTests' CPU readback checks.
    ID3D11Texture2D* GeneratedOutputTexture() const;
    ID3D11ShaderResourceView* GeneratedOutputSRV() const;
    ID3D11Texture2D* OcclusionMaskTexture() const;
    uint32_t OcclusionMaskWidth() const;
    uint32_t OcclusionMaskHeight() const;
    double DetectedRefreshHz() const;

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

    bool m_enabled = false;
    bool m_initialized = false;
    FrameGenMultiplier m_multiplier = FrameGenMultiplier::X2;
    FrameGenQualityMode m_quality = FrameGenQualityMode::Balanced;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace lsalt
