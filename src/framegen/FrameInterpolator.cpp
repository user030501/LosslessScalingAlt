#include "FrameInterpolator.h"
#include "ShaderCompiler.h"
#include "../core/Logger.h"

#include <algorithm>
#include <cstring>

namespace lsalt {

namespace {
struct OcclusionCB {
    uint32_t numBlocks[2];
    uint32_t blockSize;
    float consistencyThreshold;
};

struct InterpolationCB {
    uint32_t outputSize[2];
    uint32_t numBlocks[2];
    uint32_t blockSize;
    uint32_t padding0;
    uint32_t padding1[2];
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

bool FrameInterpolator::Initialize(ID3D11Device* device, const std::wstring& shaderDir) {
    m_csOcclusion = ShaderCompiler::CompileComputeShader(device, shaderDir + L"\\OcclusionDetection.hlsl", "CSOcclusionMask");
    m_csInterpolate = ShaderCompiler::CompileComputeShader(device, shaderDir + L"\\FrameInterpolation.hlsl", "CSInterpolate");
    if (!m_csOcclusion || !m_csInterpolate) {
        Logger::Instance().Error("FrameInterpolator", "Failed to compile one or more interpolation shaders.");
        return false;
    }

    m_occlusionCB = CreateConstantBuffer(device, sizeof(OcclusionCB));
    m_interpolationCB = CreateConstantBuffer(device, sizeof(InterpolationCB));

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampDesc, m_linearClampSampler.ReleaseAndGetAddressOf()))) return false;

    return m_occlusionCB && m_interpolationCB;
}

bool FrameInterpolator::Resize(ID3D11Device* device, uint32_t width, uint32_t height,
                                uint32_t numBlocksX, uint32_t numBlocksY, uint32_t blockSize) {
    m_valid = false;
    m_width = width;
    m_height = height;
    m_numBlocksX = numBlocksX;
    m_numBlocksY = numBlocksY;
    m_blockSize = blockSize;

    D3D11_TEXTURE2D_DESC occDesc{};
    occDesc.Width = std::max(numBlocksX, 1u);
    occDesc.Height = std::max(numBlocksY, 1u);
    occDesc.MipLevels = 1;
    occDesc.ArraySize = 1;
    occDesc.Format = DXGI_FORMAT_R32_FLOAT;
    occDesc.SampleDesc.Count = 1;
    occDesc.Usage = D3D11_USAGE_DEFAULT;
    occDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(device->CreateTexture2D(&occDesc, nullptr, m_occlusionTexture.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(m_occlusionTexture.Get(), nullptr, m_occlusionSrv.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateUnorderedAccessView(m_occlusionTexture.Get(), nullptr, m_occlusionUav.ReleaseAndGetAddressOf()))) return false;

    D3D11_TEXTURE2D_DESC outDesc{};
    outDesc.Width = std::max(width, 1u);
    outDesc.Height = std::max(height, 1u);
    outDesc.MipLevels = 1;
    outDesc.ArraySize = 1;
    outDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outDesc.SampleDesc.Count = 1;
    outDesc.Usage = D3D11_USAGE_DEFAULT;
    outDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(device->CreateTexture2D(&outDesc, nullptr, m_outputTexture.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(m_outputTexture.Get(), nullptr, m_outputSrv.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr, m_outputUav.ReleaseAndGetAddressOf()))) return false;

    m_valid = true;
    return true;
}

ID3D11ShaderResourceView* FrameInterpolator::Interpolate(ID3D11DeviceContext* context,
                                                           ID3D11ShaderResourceView* frameA, ID3D11ShaderResourceView* frameB,
                                                           ID3D11ShaderResourceView* fwdMV, ID3D11ShaderResourceView* bwdMV) {
    if (!m_valid) return nullptr;

    // --- Occlusion detection ---
    OcclusionCB occCb{};
    occCb.numBlocks[0] = m_numBlocksX;
    occCb.numBlocks[1] = m_numBlocksY;
    occCb.blockSize = m_blockSize;
    occCb.consistencyThreshold = std::max(2.0f, m_blockSize * 0.25f);
    UpdateDynamicCB(context, m_occlusionCB.Get(), occCb);

    context->CSSetShader(m_csOcclusion.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, m_occlusionCB.GetAddressOf());
    ID3D11ShaderResourceView* occSrvs[2] = {fwdMV, bwdMV};
    context->CSSetShaderResources(0, 2, occSrvs);
    ID3D11UnorderedAccessView* occUavs[1] = {m_occlusionUav.Get()};
    context->CSSetUnorderedAccessViews(0, 1, occUavs, nullptr);
    context->Dispatch(DivRoundUp(m_numBlocksX, 8), DivRoundUp(m_numBlocksY, 8), 1);

    ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
    context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv2[2] = {nullptr, nullptr};
    context->CSSetShaderResources(0, 2, nullSrv2);

    // --- Motion-compensated interpolation ---
    InterpolationCB interpCb{};
    interpCb.outputSize[0] = m_width;
    interpCb.outputSize[1] = m_height;
    interpCb.numBlocks[0] = m_numBlocksX;
    interpCb.numBlocks[1] = m_numBlocksY;
    interpCb.blockSize = m_blockSize;
    UpdateDynamicCB(context, m_interpolationCB.Get(), interpCb);

    context->CSSetShader(m_csInterpolate.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, m_interpolationCB.GetAddressOf());
    ID3D11ShaderResourceView* srvs[5] = {frameA, frameB, fwdMV, bwdMV, m_occlusionSrv.Get()};
    context->CSSetShaderResources(0, 5, srvs);
    context->CSSetSamplers(0, 1, m_linearClampSampler.GetAddressOf());
    ID3D11UnorderedAccessView* outUavs[1] = {m_outputUav.Get()};
    context->CSSetUnorderedAccessViews(0, 1, outUavs, nullptr);
    context->Dispatch(DivRoundUp(m_width, 8), DivRoundUp(m_height, 8), 1);

    context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv5[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    context->CSSetShaderResources(0, 5, nullSrv5);

    return m_outputSrv.Get();
}

} // namespace lsalt
