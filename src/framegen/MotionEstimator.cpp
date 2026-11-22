#include "MotionEstimator.h"
#include "ShaderCompiler.h"
#include "../core/Logger.h"

#include <algorithm>
#include <cstring>

namespace lsalt {

namespace {
struct DownsampleCB {
    uint32_t outputSize[2];
    uint32_t inputSize[2];
};

struct MotionEstimationCB {
    uint32_t levelSize[2];
    uint32_t numBlocks[2];
    uint32_t prevNumBlocks[2];
    uint32_t blockSize;
    uint32_t searchRadius;
    uint32_t hasPrevLevel;
    uint32_t padding0;
    float prevLevelScale[2];
    float padding1[2];
    float padding2[2]; // pad struct to a 16-byte multiple (D3D11 constant buffer requirement)
};

uint32_t DivRoundUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

bool CreateR16FloatResources(ID3D11Device* device, uint32_t w, uint32_t h,
                              Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTex,
                              Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSrv,
                              Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& outUav) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = std::max(w, 1u);
    desc.Height = std::max(h, 1u);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, outTex.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(outTex.Get(), nullptr, outSrv.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateUnorderedAccessView(outTex.Get(), nullptr, outUav.ReleaseAndGetAddressOf()))) return false;
    return true;
}

bool CreateMVResources(ID3D11Device* device, uint32_t w, uint32_t h,
                        Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTex,
                        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSrv,
                        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& outUav) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = std::max(w, 1u);
    desc.Height = std::max(h, 1u);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, outTex.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateShaderResourceView(outTex.Get(), nullptr, outSrv.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(device->CreateUnorderedAccessView(outTex.Get(), nullptr, outUav.ReleaseAndGetAddressOf()))) return false;
    return true;
}

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

bool MotionEstimator::Initialize(ID3D11Device* device, const std::wstring& shaderDir) {
    m_csLumaExtract = ShaderCompiler::CompileComputeShader(device, shaderDir + L"\\Downsample.hlsl", "CSLumaExtract");
    m_csDownsample2x = ShaderCompiler::CompileComputeShader(device, shaderDir + L"\\Downsample.hlsl", "CSDownsample2x");
    m_csBlockMatch = ShaderCompiler::CompileComputeShader(device, shaderDir + L"\\MotionEstimation.hlsl", "CSBlockMatch");

    if (!m_csLumaExtract || !m_csDownsample2x || !m_csBlockMatch) {
        Logger::Instance().Error("MotionEstimator", "Failed to compile one or more motion estimation shaders.");
        return false;
    }

    m_downsampleCB = CreateConstantBuffer(device, sizeof(DownsampleCB));
    m_motionEstimationCB = CreateConstantBuffer(device, sizeof(MotionEstimationCB));
    return m_downsampleCB && m_motionEstimationCB;
}

bool MotionEstimator::CreateLumaPyramidResources(ID3D11Device* device, LumaPyramid& pyramid) {
    pyramid.textures.assign(m_levelSizes.size(), nullptr);
    pyramid.srvs.assign(m_levelSizes.size(), nullptr);
    pyramid.uavs.assign(m_levelSizes.size(), nullptr);

    for (size_t i = 0; i < m_levelSizes.size(); ++i) {
        if (!CreateR16FloatResources(device, m_levelSizes[i].first, m_levelSizes[i].second,
                                      pyramid.textures[i], pyramid.srvs[i], pyramid.uavs[i])) {
            return false;
        }
    }
    return true;
}

bool MotionEstimator::Resize(ID3D11Device* device, uint32_t width, uint32_t height, const FrameGenQualityParams& params) {
    m_valid = false;
    m_width = width;
    m_height = height;
    m_params = params;

    m_levelSizes.clear();
    m_numBlocks.clear();
    uint32_t w = width, h = height;
    for (int level = 0; level < params.pyramidLevels; ++level) {
        m_levelSizes.emplace_back(std::max(w, 1u), std::max(h, 1u));
        m_numBlocks.emplace_back(DivRoundUp(std::max(w, 1u), static_cast<uint32_t>(params.blockSize)),
                                  DivRoundUp(std::max(h, 1u), static_cast<uint32_t>(params.blockSize)));
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }

    if (!CreateLumaPyramidResources(device, m_pyramidA)) return false;
    if (!CreateLumaPyramidResources(device, m_pyramidB)) return false;

    auto allocMvLevels = [&](std::vector<MVLevel>& levels) -> bool {
        levels.assign(m_numBlocks.size(), MVLevel{});
        for (size_t i = 0; i < m_numBlocks.size(); ++i) {
            if (!CreateMVResources(device, m_numBlocks[i].first, m_numBlocks[i].second,
                                    levels[i].texture, levels[i].srv, levels[i].uav)) {
                return false;
            }
        }
        return true;
    };

    if (!allocMvLevels(m_fwdMV)) return false;
    if (!allocMvLevels(m_bwdMV)) return false;

    m_valid = true;
    return true;
}

void MotionEstimator::BuildPyramid(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV, LumaPyramid& pyramid) {
    // Level 0: extract luma directly from the color frame.
    DownsampleCB cb0{};
    cb0.outputSize[0] = m_levelSizes[0].first;
    cb0.outputSize[1] = m_levelSizes[0].second;
    cb0.inputSize[0] = m_levelSizes[0].first;
    cb0.inputSize[1] = m_levelSizes[0].second;
    UpdateDynamicCB(context, m_downsampleCB.Get(), cb0);

    context->CSSetShader(m_csLumaExtract.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, m_downsampleCB.GetAddressOf());
    ID3D11ShaderResourceView* srvs[1] = {colorSRV};
    context->CSSetShaderResources(0, 1, srvs);
    ID3D11UnorderedAccessView* uavs[1] = {pyramid.uavs[0].Get()};
    context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    context->Dispatch(DivRoundUp(m_levelSizes[0].first, 8), DivRoundUp(m_levelSizes[0].second, 8), 1);

    // Unbind before rebinding the same slots with different resources (SRV used as UAV source next).
    ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
    context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    context->CSSetShaderResources(0, 1, nullSrv);

    // Remaining levels: box-downsample the previous level's luma.
    for (size_t level = 1; level < m_levelSizes.size(); ++level) {
        DownsampleCB cb{};
        cb.outputSize[0] = m_levelSizes[level].first;
        cb.outputSize[1] = m_levelSizes[level].second;
        cb.inputSize[0] = m_levelSizes[level - 1].first;
        cb.inputSize[1] = m_levelSizes[level - 1].second;
        UpdateDynamicCB(context, m_downsampleCB.Get(), cb);

        context->CSSetShader(m_csDownsample2x.Get(), nullptr, 0);
        context->CSSetConstantBuffers(0, 1, m_downsampleCB.GetAddressOf());
        ID3D11ShaderResourceView* inSrv[1] = {pyramid.srvs[level - 1].Get()};
        context->CSSetShaderResources(1, 1, inSrv);
        ID3D11UnorderedAccessView* outUav[1] = {pyramid.uavs[level].Get()};
        context->CSSetUnorderedAccessViews(0, 1, outUav, nullptr);
        context->Dispatch(DivRoundUp(m_levelSizes[level].first, 8), DivRoundUp(m_levelSizes[level].second, 8), 1);

        context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        ID3D11ShaderResourceView* nullSrv1[1] = {nullptr};
        context->CSSetShaderResources(1, 1, nullSrv1);
    }
}

bool MotionEstimator::BuildPyramidA(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV) {
    if (!m_valid) return false;
    BuildPyramid(context, colorSRV, m_pyramidA);
    return true;
}

bool MotionEstimator::BuildPyramidB(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV) {
    if (!m_valid) return false;
    BuildPyramid(context, colorSRV, m_pyramidB);
    return true;
}

ID3D11ShaderResourceView* MotionEstimator::EstimateMotion(ID3D11DeviceContext* context, const LumaPyramid& from,
                                                            const LumaPyramid& to, std::vector<MVLevel>& mvLevels) {
    if (!m_valid) return nullptr;

    context->CSSetShader(m_csBlockMatch.Get(), nullptr, 0);

    for (int level = static_cast<int>(m_levelSizes.size()) - 1; level >= 0; --level) {
        const bool hasPrev = (level + 1) < static_cast<int>(m_levelSizes.size());

        MotionEstimationCB cb{};
        cb.levelSize[0] = m_levelSizes[level].first;
        cb.levelSize[1] = m_levelSizes[level].second;
        cb.numBlocks[0] = m_numBlocks[level].first;
        cb.numBlocks[1] = m_numBlocks[level].second;
        cb.prevNumBlocks[0] = hasPrev ? m_numBlocks[level + 1].first : 0;
        cb.prevNumBlocks[1] = hasPrev ? m_numBlocks[level + 1].second : 0;
        cb.blockSize = static_cast<uint32_t>(m_params.blockSize);
        cb.searchRadius = static_cast<uint32_t>(m_params.searchRadius);
        cb.hasPrevLevel = hasPrev ? 1u : 0u;
        cb.prevLevelScale[0] = 2.0f;
        cb.prevLevelScale[1] = 2.0f;
        UpdateDynamicCB(context, m_motionEstimationCB.Get(), cb);
        context->CSSetConstantBuffers(0, 1, m_motionEstimationCB.GetAddressOf());

        ID3D11ShaderResourceView* srvs[3] = {
            from.srvs[level].Get(), to.srvs[level].Get(),
            hasPrev ? mvLevels[level + 1].srv.Get() : nullptr};
        context->CSSetShaderResources(0, 3, srvs);

        ID3D11UnorderedAccessView* uavs[1] = {mvLevels[level].uav.Get()};
        context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        context->Dispatch(m_numBlocks[level].first, m_numBlocks[level].second, 1);

        ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
        context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        ID3D11ShaderResourceView* nullSrv[3] = {nullptr, nullptr, nullptr};
        context->CSSetShaderResources(0, 3, nullSrv);
    }

    return mvLevels[0].srv.Get();
}

ID3D11ShaderResourceView* MotionEstimator::EstimateForward(ID3D11DeviceContext* context) {
    return EstimateMotion(context, m_pyramidA, m_pyramidB, m_fwdMV);
}

ID3D11ShaderResourceView* MotionEstimator::EstimateBackward(ID3D11DeviceContext* context) {
    return EstimateMotion(context, m_pyramidB, m_pyramidA, m_bwdMV);
}

} // namespace lsalt
