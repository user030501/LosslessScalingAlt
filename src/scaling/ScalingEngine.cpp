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
    uint32_t padding[3]; // pad to a 16-byte multiple (D3D11 constant buffer requirement)
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

ScalingEngineD3D11::ScalingEngineD3D11() = default;
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

void ScalingEngineD3D11::RecomputeViewport() {
    if (m_srcWidth == 0 || m_srcHeight == 0 || m_dstWidth == 0 || m_dstHeight == 0) {
        m_viewportX = m_viewportY = 0;
        m_viewportW = m_viewportH = 0;
        m_integerFactor = 1;
        return;
    }

    if (m_algorithm == ScalingAlgorithm::IntegerScale) {
        // Integer scaling is aspect-preserving by definition: the largest
        // whole-number factor that still fits in both dimensions.
        const uint32_t factorX = m_dstWidth / m_srcWidth;
        const uint32_t factorY = m_dstHeight / m_srcHeight;
        m_integerFactor = std::max(1u, std::min(factorX, factorY));
        m_viewportW = m_srcWidth * m_integerFactor;
        m_viewportH = m_srcHeight * m_integerFactor;
    } else if (m_preserveAspect) {
        const double scaleX = static_cast<double>(m_dstWidth) / m_srcWidth;
        const double scaleY = static_cast<double>(m_dstHeight) / m_srcHeight;
        const double scale = std::min(scaleX, scaleY);
        m_viewportW = static_cast<uint32_t>(std::lround(m_srcWidth * scale));
        m_viewportH = static_cast<uint32_t>(std::lround(m_srcHeight * scale));
        m_integerFactor = 1;
    } else {
        m_viewportW = m_dstWidth;
        m_viewportH = m_dstHeight;
        m_integerFactor = 1;
    }

    m_viewportX = (m_dstWidth > m_viewportW) ? (m_dstWidth - m_viewportW) / 2 : 0;
    m_viewportY = (m_dstHeight > m_viewportH) ? (m_dstHeight - m_viewportH) / 2 : 0;
}

bool ScalingEngineD3D11::Resize(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight) {
    m_valid = false;
    m_srcWidth = srcWidth;
    m_srcHeight = srcHeight;
    m_dstWidth = dstWidth;
    m_dstHeight = dstHeight;
    RecomputeViewport();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = std::max(dstWidth, 1u);
    desc.Height = std::max(dstHeight, 1u);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_outputTexture.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(m_device->CreateShaderResourceView(m_outputTexture.Get(), nullptr, m_outputSrv.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(m_device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr, m_outputUav.ReleaseAndGetAddressOf()))) return false;

    m_valid = true;
    return true;
}

void ScalingEngineD3D11::SetAlgorithm(ScalingAlgorithm algorithm) {
    if (algorithm == m_algorithm) return;
    m_algorithm = algorithm;
    RecomputeViewport();
}

void ScalingEngineD3D11::SetPreserveAspectRatio(bool preserve) {
    if (preserve == m_preserveAspect) return;
    m_preserveAspect = preserve;
    RecomputeViewport();
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
