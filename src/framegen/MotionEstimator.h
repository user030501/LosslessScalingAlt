#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <vector>
#include <cstdint>
#include "FrameGenTypes.h"

namespace lsalt {

// Builds a luma mip pyramid for a captured color frame and runs
// coarse-to-fine hierarchical block matching between two pyramids to
// produce a per-block motion vector field (see shaders/MotionEstimation.hlsl
// and shaders/Downsample.hlsl for the actual math).
class MotionEstimator {
public:
    bool Initialize(ID3D11Device* device, const std::wstring& shaderDir);

    // (Re)allocates all GPU resources for the given frame size and quality
    // settings. Must be called before use and again whenever either changes.
    bool Resize(ID3D11Device* device, uint32_t width, uint32_t height, const FrameGenQualityParams& params);

    // Builds the luma pyramid for `colorSRV` into the internal A or B slot.
    bool BuildPyramidA(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV);
    bool BuildPyramidB(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV);

    // Estimates motion A->B (forward) or B->A (backward) using the pyramids
    // built above. Returns the finest-level motion vector field SRV.
    ID3D11ShaderResourceView* EstimateForward(ID3D11DeviceContext* context);
    ID3D11ShaderResourceView* EstimateBackward(ID3D11DeviceContext* context);

    uint32_t BlockSize() const { return m_params.blockSize; }
    uint32_t NumBlocksX() const { return m_numBlocks[0].first; }
    uint32_t NumBlocksY() const { return m_numBlocks[0].second; }

    bool IsValid() const { return m_valid; }

private:
    struct LumaPyramid {
        std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> textures;
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> srvs;
        std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> uavs;
    };

    struct MVLevel {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    };

    bool CreateLumaPyramidResources(ID3D11Device* device, LumaPyramid& pyramid);
    void BuildPyramid(ID3D11DeviceContext* context, ID3D11ShaderResourceView* colorSRV, LumaPyramid& pyramid);
    ID3D11ShaderResourceView* EstimateMotion(ID3D11DeviceContext* context, const LumaPyramid& from,
                                              const LumaPyramid& to, std::vector<MVLevel>& mvLevels);

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csLumaExtract;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csDownsample2x;
    // Phase 3: three parallel block-matching variants, one per quality
    // preset's search radius (4/6/8 -- see FrameGenTypes.h), each with a
    // thread-group size that exactly matches its candidate window so every
    // GPU thread does useful work. m_csBlockMatchActive is whichever of the
    // three matches the current FrameGenQualityParams, selected in Resize().
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csBlockMatchR4;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csBlockMatchR6;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csBlockMatchR8;
    ID3D11ComputeShader* m_csBlockMatchActive = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_downsampleCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_motionEstimationCB;

    LumaPyramid m_pyramidA;
    LumaPyramid m_pyramidB;
    std::vector<MVLevel> m_fwdMV;
    std::vector<MVLevel> m_bwdMV;

    std::vector<std::pair<uint32_t, uint32_t>> m_levelSizes;  // pixel dims per pyramid level
    std::vector<std::pair<uint32_t, uint32_t>> m_numBlocks;   // block-grid dims per level

    FrameGenQualityParams m_params;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_valid = false;
};

} // namespace lsalt
