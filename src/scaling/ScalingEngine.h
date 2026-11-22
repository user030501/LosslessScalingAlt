#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <string>
#include "../core/Types.h"

namespace lsalt {

// Interface the rest of the app programs against. ScalingEngineD3D11 below
// is the first concrete implementation: real compute-shader upscaling
// (Nearest/Bilinear/Bicubic/Lanczos/Integer Scale -- see shaders/Scaling.
// hlsl), with real aspect-ratio-preserving letterbox/pillarbox placement.
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
    // before Scale(), and again whenever either size changes.
    virtual bool Resize(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) = 0;

    virtual void SetAlgorithm(ScalingAlgorithm algorithm) = 0;
    virtual void SetPreserveAspectRatio(bool preserve) = 0;

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
    ID3D11ShaderResourceView* Scale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srcSRV) override;

    ID3D11Texture2D* OutputTexture() const override { return m_outputTexture.Get(); }
    ID3D11ShaderResourceView* OutputSRV() const override { return m_outputSrv.Get(); }

    ScalingAlgorithm Algorithm() const { return m_algorithm; }
    bool PreserveAspectRatio() const { return m_preserveAspect; }

    // Exposed for diagnostics/UI: where (and how large) the actual scaled
    // content ended up within the destination texture, after aspect-ratio
    // letterboxing/pillarboxing. Border pixels outside this rect are black.
    void Viewport(uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) const {
        x = m_viewportX; y = m_viewportY; w = m_viewportW; h = m_viewportH;
    }
    uint32_t IntegerFactor() const { return m_integerFactor; }

private:
    void RecomputeViewport();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csNearest, m_csBilinear, m_csBicubic, m_csLanczos, m_csIntegerScale;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearClampSampler;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSrv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUav;

    uint32_t m_srcWidth = 0, m_srcHeight = 0, m_dstWidth = 0, m_dstHeight = 0;
    uint32_t m_viewportX = 0, m_viewportY = 0, m_viewportW = 0, m_viewportH = 0;
    uint32_t m_integerFactor = 1;
    ScalingAlgorithm m_algorithm = ScalingAlgorithm::Bilinear;
    bool m_preserveAspect = true;
    bool m_valid = false;
};

} // namespace lsalt
