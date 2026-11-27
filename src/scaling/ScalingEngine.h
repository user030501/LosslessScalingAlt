#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <string>
#include <array>
#include "../core/Types.h"
#include "../gpu/TexturePool.h"

namespace lsalt {

// Interface the rest of the app programs against. ScalingEngineD3D11 below
// is the first concrete implementation: real compute-shader upscaling
// (Nearest/Bilinear/Bicubic/Lanczos/Integer Scale -- see shaders/Scaling.
// hlsl), with real aspect-ratio-preserving letterbox/pillarbox placement
// and real (not labeled-only) sharpening.
// FSR1/NIS remain unimplemented (porting a vendor reference implementation
// is out of scope here) and fall back to Bilinear with a logged warning
// rather than silently doing nothing or duplicating the source.
//
// Like FrameGenEngine before a real Capture Engine exists, this has no
// real captured game frame to run on yet -- it is exercised against the
// same synthetic, animated TestPatternSource frames the Frame Generation
// Engine uses (see App.cpp), which are real per-frame-rendered GPU
// textures, not a single static test image.
class IScalingEngine {
public:
    virtual ~IScalingEngine() = default;

    virtual bool Initialize(ID3D11Device* device, const std::wstring& shaderDir) = 0;
    // (Re)allocates the output texture and recomputes the aspect-ratio
    // viewport for the given source/destination sizes. Must be called
    // before Scale(), and again whenever either size changes. May clamp
    // dstWidth/dstHeight down (see MaxTextureDimension / the VRAM guard);
    // call DstWidth()/DstHeight() afterward if the actual allocated size
    // matters to the caller.
    virtual bool Resize(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) = 0;

    virtual void SetAlgorithm(ScalingAlgorithm algorithm) = 0;
    virtual void SetPreserveAspectRatio(bool preserve) = 0; // back-compat: maps to AspectMode::Fit/Stretch
    virtual void SetAspectMode(AspectMode mode) = 0;
    virtual void SetSharpenLevel(SharpenLevel level) = 0;

    // Scales srcSRV into the internal output texture and returns its SRV,
    // or null on failure/not-yet-resized.
    virtual ID3D11ShaderResourceView* Scale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srcSRV) = 0;

    virtual ID3D11Texture2D* OutputTexture() const = 0;
    virtual ID3D11ShaderResourceView* OutputSRV() const = 0;
};

class ScalingEngineD3D11 : public IScalingEngine {
public:
    ScalingEngineD3D11();
    ~ScalingEngineD3D11() override;

    bool Initialize(ID3D11Device* device, const std::wstring& shaderDir) override;
    bool Resize(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) override;
    void SetAlgorithm(ScalingAlgorithm algorithm) override;
    void SetPreserveAspectRatio(bool preserve) override;
    void SetAspectMode(AspectMode mode) override;
    void SetSharpenLevel(SharpenLevel level) override;
    ID3D11ShaderResourceView* Scale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srcSRV) override;

    ID3D11Texture2D* OutputTexture() const override { return m_outputTexture.Get(); }
    ID3D11ShaderResourceView* OutputSRV() const override { return m_outputSrv.Get(); }

    ScalingAlgorithm Algorithm() const { return m_algorithm; }
    bool PreserveAspectRatio() const { return m_preserveAspect; }
    AspectMode EffectiveAspectMode() const;
    SharpenLevel Sharpen() const { return m_sharpenLevel; }
    ScalingQualityPreset QualityPreset() const { return m_qualityPreset; }
    void SetQualityPreset(ScalingQualityPreset preset);

    // Exposed for diagnostics/UI: where (and how large) the actual scaled
    // content ended up within the destination texture, after aspect-mode
    // placement. Border pixels outside this rect are black.
    void Viewport(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) const {
        x = m_viewportX; y = m_viewportY; w = m_viewportW; h = m_viewportH;
    }
    uint32_t IntegerFactor() const { return m_integerFactor; }
    uint32_t DstWidth() const { return m_dstWidth; }
    uint32_t DstHeight() const { return m_dstHeight; }
    uint32_t SrcWidth() const { return m_srcWidth; }
    uint32_t SrcHeight() const { return m_srcHeight; }

    // True if the content is narrower than the destination (bars on the
    // sides -- "pillarboxed"); false if it's shorter (bars top/bottom --
    // "letterboxed"); meaningless (returns false) if there's no border at
    // all. Display-only classification, for the UI/diagnostics.
    bool IsPillarboxed() const { return m_viewportW < m_dstWidth && m_viewportH == m_dstHeight; }
    bool IsLetterboxed() const { return m_viewportH < m_dstHeight && m_viewportW == m_dstWidth; }

    // Phase 7: real VRAM-aware admission control, mirroring
    // FrameGenEngineD3D11::SetVramQuery (see its own comment). Also used
    // to derive the hard per-dimension clamp (min(hardware max, a VRAM-
    // budget-derived safe max)) applied in Resize().
    void SetVramQuery(std::function<bool(uint64_t& usedBytes, uint64_t& budgetBytes)> query) {
        m_vramQuery = std::move(query);
    }
    uint64_t EstimatedOutputTextureBytes() const;
    uint64_t EstimatedPoolBytes() const { return m_texturePool.EstimatedBytesInPool(); }
    uint64_t PoolReuseCount() const { return m_texturePool.TotalReusesEver(); }
    uint64_t PoolAllocationCount() const { return m_texturePool.TotalAllocationsEver(); }
    double LastGpuExecutionMs() const { return m_lastGpuMs; }
    double LastCpuSubmissionMs() const { return m_lastCpuSubmissionMs; }
    bool GpuTimingAvailable() const { return m_gpuTimingAvailable; }
    uint64_t ResourceTrimCount() const { return m_resourceTrimCount; }
    uint64_t AllocationFailureCount() const { return m_texturePool.TotalAllocationFailuresEver(); }
    void ApplyVramPressure();
    const std::string& LastClampReason() const { return m_lastClampReason; }

    // The real D3D11 hardware limit for a single texture dimension at
    // feature level 11_0+ (16384). Requests above this are clamped, not
    // silently wrapped or left to fail inside the driver.
    static constexpr uint32_t kMaxTextureDimension = 16384;

private:
    void RecomputeViewport();
    float SharpenAmountFor(SharpenLevel level) const;
    void ClampRequestedSize(uint32_t& dstWidth, uint32_t& dstHeight);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csNearest, m_csBilinear, m_csBicubic, m_csLanczos, m_csIntegerScale;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearClampSampler;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSrv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUav;
    TexturePool m_texturePool;

    uint32_t m_srcWidth = 0, m_srcHeight = 0, m_dstWidth = 0, m_dstHeight = 0;
    uint32_t m_viewportX = 0, m_viewportY = 0, m_viewportW = 0, m_viewportH = 0;
    uint32_t m_integerFactor = 1;
    ScalingAlgorithm m_algorithm = ScalingAlgorithm::Bilinear;
    bool m_preserveAspect = true;
    AspectMode m_aspectMode = AspectMode::Fit;
    SharpenLevel m_sharpenLevel = SharpenLevel::Off;
    ScalingQualityPreset m_qualityPreset = ScalingQualityPreset::Balanced;
    bool m_valid = false;

    std::function<bool(uint64_t&, uint64_t&)> m_vramQuery;
    std::string m_lastClampReason;
    struct ScalingTimingSlot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> start;
        Microsoft::WRL::ComPtr<ID3D11Query> end;
        bool pending = false;
    };
    std::array<ScalingTimingSlot, 4> m_timingSlots;
    int m_timingWriteIndex = 0;
    int m_timingReadIndex = 0;
    double m_lastGpuMs = -1.0;
    double m_lastCpuSubmissionMs = -1.0;
    bool m_gpuTimingAvailable = false;
    uint64_t m_resourceTrimCount = 0;
};

} // namespace lsalt
