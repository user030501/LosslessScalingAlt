#include "ScalingEngine.h"
#include "../framegen/ShaderCompiler.h"
#include "../core/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsalt {

namespace {
struct ScalingCB {
    uint32_t dstSize[2];
    uint32_t srcSize[2];
    uint32_t viewportOrigin[2];
    uint32_t viewportSize[2];
    uint32_t integerFactor;
    float sharpenAmount;
    uint32_t padding[2]; // pad to a 16-byte multiple (D3D11 constant buffer requirement)
};

uint32_t DivRoundUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

Microsoft::WRL::ComPtr<ID3D11Buffer> CreateConstantBuffer(ID3D11Device* device, size_t byteSize) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(byteSize);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    device->CreateBuffer(&desc, nullptr, buf.GetAddressOf());
    return buf;
}

template <typename T>
void UpdateDynamicCB(ID3D11DeviceContext* context, ID3D11Buffer* cb, const T& data) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(T));
        context->Unmap(cb, 0);
    }
}
} // namespace

ScalingEngineD3D11::ScalingEngineD3D11() : m_texturePool(4) {}
ScalingEngineD3D11::~ScalingEngineD3D11() = default;

bool ScalingEngineD3D11::Initialize(ID3D11Device* device, const std::wstring& shaderDir) {
    m_device = device;

    const std::wstring path = shaderDir + L"\\Scaling.hlsl";
    m_csNearest = ShaderCompiler::CompileComputeShader(device, path, "CSNearest");
    m_csBilinear = ShaderCompiler::CompileComputeShader(device, path, "CSBilinear");
    m_csBicubic = ShaderCompiler::CompileComputeShader(device, path, "CSBicubic");
    m_csLanczos = ShaderCompiler::CompileComputeShader(device, path, "CSLanczos");
    m_csIntegerScale = ShaderCompiler::CompileComputeShader(device, path, "CSIntegerScale");

    if (!m_csNearest || !m_csBilinear || !m_csBicubic || !m_csLanczos || !m_csIntegerScale) {
        Logger::Instance().Error("ScalingEngine", "Failed to compile one or more scaling shaders.");
        return false;
    }

    m_cb = CreateConstantBuffer(device, sizeof(ScalingCB));

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampDesc, m_linearClampSampler.ReleaseAndGetAddressOf()))) return false;

    return m_cb != nullptr;
}

AspectMode ScalingEngineD3D11::EffectiveAspectMode() const {
    // Backward compatibility: selecting the IntegerScale ALGORITHM has
    // always implied integer-factor placement, since before AspectMode
    // existed that was the only way to ask for it. Keep that working
    // exactly as before for any caller that only ever touches
    // SetAlgorithm/SetPreserveAspectRatio.
    if (m_algorithm == ScalingAlgorithm::IntegerScale) return AspectMode::IntegerScale;
    return m_aspectMode;
}

float ScalingEngineD3D11::SharpenAmountFor(SharpenLevel level) const {
    switch (level) {
        case SharpenLevel::Off:    return 0.0f;
        case SharpenLevel::Low:    return 0.15f;
        case SharpenLevel::Medium: return 0.35f;
        case SharpenLevel::High:   return 0.60f;
        default:                   return 0.0f;
    }
}

void ScalingEngineD3D11::RecomputeViewport() {
    if (m_srcWidth == 0 || m_srcHeight == 0 || m_dstWidth == 0 || m_dstHeight == 0) {
        m_viewportX = m_viewportY = 0;
        m_viewportW = m_viewportH = 0;
        m_integerFactor = 1;
        return;
    }

    switch (EffectiveAspectMode()) {
        case AspectMode::IntegerScale: {
            // Aspect-preserving by definition: the largest whole-number
            // factor that still fits in both dimensions.
            const uint32_t factorX = m_dstWidth / m_srcWidth;
            const uint32_t factorY = m_dstHeight / m_srcHeight;
            m_integerFactor = std::max(1u, std::min(factorX, factorY));
            m_viewportW = m_srcWidth * m_integerFactor;
            m_viewportH = m_srcHeight * m_integerFactor;
            break;
        }
        case AspectMode::Fit: {
            const double scaleX = static_cast<double>(m_dstWidth) / m_srcWidth;
            const double scaleY = static_cast<double>(m_dstHeight) / m_srcHeight;
            const double scale = std::min(scaleX, scaleY);
            m_viewportW = static_cast<uint32_t>(std::lround(m_srcWidth * scale));
            m_viewportH = static_cast<uint32_t>(std::lround(m_srcHeight * scale));
            m_integerFactor = 1;
            break;
        }
        case AspectMode::Stretch:
        default:
            m_viewportW = m_dstWidth;
            m_viewportH = m_dstHeight;
            m_integerFactor = 1;
            break;
    }

    m_viewportX = (m_dstWidth > m_viewportW) ? (m_dstWidth - m_viewportW) / 2 : 0;
    m_viewportY = (m_dstHeight > m_viewportH) ? (m_dstHeight - m_viewportH) / 2 : 0;
}

void ScalingEngineD3D11::ClampRequestedSize(uint32_t& dstWidth, uint32_t& dstHeight) {
    m_lastClampReason.clear();

    const uint32_t beforeW = dstWidth, beforeH = dstHeight;
    dstWidth = std::min(dstWidth, kMaxTextureDimension);
    dstHeight = std::min(dstHeight, kMaxTextureDimension);
    if (dstWidth != beforeW || dstHeight != beforeH) {
        m_lastClampReason = "Requested " + std::to_string(beforeW) + "x" + std::to_string(beforeH) +
                              " exceeds the real D3D11 hardware limit (" + std::to_string(kMaxTextureDimension) +
                              " px/dimension); clamped to " + std::to_string(dstWidth) + "x" + std::to_string(dstHeight) + ".";
        Logger::Instance().Warn("ScalingEngine", m_lastClampReason);
    }

    if (!m_vramQuery) return;
    uint64_t used = 0, budget = 0;
    if (!m_vramQuery(used, budget) || budget == 0) return;

    const uint64_t headroom = (budget > used) ? (budget - used) : 0;
    const uint64_t safeHeadroom = static_cast<uint64_t>(headroom * 0.85); // leave margin, same policy as FrameGenEngine's guard

    // Conservative resource limit for 2GB-class VRAM (MX150): the output
    // texture plus one source-sized buffer, 4 bytes/px each, must fit.
    for (int attempt = 0; attempt < 12; ++attempt) {
        const uint64_t estimated = (static_cast<uint64_t>(dstWidth) * dstHeight +
                                     static_cast<uint64_t>(m_srcWidth) * m_srcHeight) * 4;
        if (estimated <= safeHeadroom || (dstWidth <= 64 && dstHeight <= 64)) break;

        // Scale both dimensions down together so the aspect ratio of the
        // request itself is preserved even while shrinking it to fit --
        // shrinking only one axis would distort what the caller asked for.
        const double shrink = std::sqrt(static_cast<double>(safeHeadroom) / static_cast<double>(std::max<uint64_t>(estimated, 1)));
        const uint32_t newW = std::max(64u, static_cast<uint32_t>(dstWidth * shrink));
        const uint32_t newH = std::max(64u, static_cast<uint32_t>(dstHeight * shrink));
        if (newW >= dstWidth && newH >= dstHeight) break; // no progress -- avoid an infinite loop
        dstWidth = newW;
        dstHeight = newH;
    }

    if (dstWidth != beforeW || dstHeight != beforeH) {
        if (!m_lastClampReason.empty()) m_lastClampReason += " ";
        m_lastClampReason += "VRAM headroom (" + std::to_string(safeHeadroom / (1024 * 1024)) +
                               " MB safe) required shrinking the request to fit this GPU's budget.";
        Logger::Instance().Warn("ScalingEngine", "Clamped destination size for VRAM headroom: now " +
                                                       std::to_string(dstWidth) + "x" + std::to_string(dstHeight) + ".");
    }
}

bool ScalingEngineD3D11::Resize(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) {
    m_valid = false;
    m_srcWidth = srcWidth;
    m_srcHeight = srcHeight;

    ClampRequestedSize(dstWidth, dstHeight);
    m_dstWidth = dstWidth;
    m_dstHeight = dstHeight;
    RecomputeViewport();

    // Hand the current output texture back to the pool instead of
    // destroying it outright -- if we resize back to this exact shape
    // later (e.g. toggling between two quality presets/resolutions), it's
    // reused instead of reallocated.
    if (m_outputTexture) {
        D3D11_TEXTURE2D_DESC oldDesc{};
        m_outputTexture->GetDesc(&oldDesc);
        m_texturePool.Release(m_outputTexture, m_outputSrv, m_outputUav, oldDesc.Width, oldDesc.Height, oldDesc.Format,
                               oldDesc.BindFlags);
        m_outputTexture.Reset();
        m_outputSrv.Reset();
        m_outputUav.Reset();
    }

    const UINT bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (!m_texturePool.Acquire(m_device.Get(), std::max(dstWidth, 1u), std::max(dstHeight, 1u),
                                 DXGI_FORMAT_R8G8B8A8_UNORM, bindFlags, m_outputTexture, m_outputSrv, m_outputUav)) {
        return false;
    }

    m_valid = true;
    return true;
}

void ScalingEngineD3D11::SetAlgorithm(ScalingAlgorithm algorithm) {
    if (algorithm == m_algorithm) return;
    m_algorithm = algorithm;
    RecomputeViewport();
}

void ScalingEngineD3D11::SetPreserveAspectRatio(bool preserve) {
    m_preserveAspect = preserve;
    m_aspectMode = preserve ? AspectMode::Fit : AspectMode::Stretch;
    RecomputeViewport();
}

void ScalingEngineD3D11::SetAspectMode(AspectMode mode) {
    if (mode == m_aspectMode) return;
    m_aspectMode = mode;
    m_preserveAspect = (mode != AspectMode::Stretch);
    RecomputeViewport();
}

void ScalingEngineD3D11::SetSharpenLevel(SharpenLevel level) { m_sharpenLevel = level; }

uint64_t ScalingEngineD3D11::EstimatedOutputTextureBytes() const {
    return static_cast<uint64_t>(m_dstWidth) * m_dstHeight * 4;
}

ID3D11ShaderResourceView* ScalingEngineD3D11::Scale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* srcSRV) {
    if (!m_valid || !srcSRV) return nullptr;

    ScalingCB cb{};
    cb.dstSize[0] = m_dstWidth;
    cb.dstSize[1] = m_dstHeight;
    cb.srcSize[0] = m_srcWidth;
    cb.srcSize[1] = m_srcHeight;
    cb.viewportOrigin[0] = m_viewportX;
    cb.viewportOrigin[1] = m_viewportY;
    cb.viewportSize[0] = m_viewportW;
    cb.viewportSize[1] = m_viewportH;
    cb.integerFactor = m_integerFactor;
    // Never sharpened -- see Scaling.hlsl's file header comment.
    cb.sharpenAmount = (m_algorithm == ScalingAlgorithm::IntegerScale) ? 0.0f : SharpenAmountFor(m_sharpenLevel);
    UpdateDynamicCB(context, m_cb.Get(), cb);

    ID3D11ComputeShader* shader = m_csBilinear.Get();
    switch (m_algorithm) {
        case ScalingAlgorithm::Nearest:      shader = m_csNearest.Get(); break;
        case ScalingAlgorithm::Bilinear:     shader = m_csBilinear.Get(); break;
        case ScalingAlgorithm::Bicubic:      shader = m_csBicubic.Get(); break;
        case ScalingAlgorithm::Lanczos:      shader = m_csLanczos.Get(); break;
        case ScalingAlgorithm::IntegerScale: shader = m_csIntegerScale.Get(); break;
        case ScalingAlgorithm::FSR1:
        case ScalingAlgorithm::NIS:
        default:
            // Not implemented (see IScalingEngine's class comment): fall
            // back to a real, working algorithm instead of producing
            // nothing or duplicating the source unscaled.
            shader = m_csBilinear.Get();
            break;
    }

    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    ID3D11ShaderResourceView* srvs[1] = {srcSRV};
    context->CSSetShaderResources(0, 1, srvs);
    context->CSSetSamplers(0, 1, m_linearClampSampler.GetAddressOf());
    ID3D11UnorderedAccessView* uavs[1] = {m_outputUav.Get()};
    context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context->Dispatch(DivRoundUp(m_dstWidth, 8), DivRoundUp(m_dstHeight, 8), 1);

    ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
    context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    context->CSSetShaderResources(0, 1, nullSrv);

    return m_outputSrv.Get();
}

} // namespace lsalt
