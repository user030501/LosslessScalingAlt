#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace lsalt {

// Runs the occlusion-detection and motion-compensated interpolation compute
// shaders given a forward (A->B) and backward (B->A) block-grid motion
// vector field from MotionEstimator, producing a single blended output
// frame at t=0.5.
class FrameInterpolator {
public:
    bool Initialize(ID3D11Device* device, const std::wstring& shaderDir);
    bool Resize(ID3D11Device* device, uint32_t width, uint32_t height,
                uint32_t numBlocksX, uint32_t numBlocksY, uint32_t blockSize);

    // Returns the SRV of the generated intermediate frame, or null on failure.
    ID3D11ShaderResourceView* Interpolate(ID3D11DeviceContext* context,
                                           ID3D11ShaderResourceView* frameA, ID3D11ShaderResourceView* frameB,
                                           ID3D11ShaderResourceView* fwdMV, ID3D11ShaderResourceView* bwdMV);

    ID3D11Texture2D* OutputTexture() const { return m_outputTexture.Get(); }
    ID3D11ShaderResourceView* OutputSRV() const { return m_outputSrv.Get(); }
    ID3D11ShaderResourceView* OcclusionMaskSRV() const { return m_occlusionSrv.Get(); }
    ID3D11Texture2D* OcclusionMaskTexture() const { return m_occlusionTexture.Get(); }
    uint32_t OcclusionMaskWidth() const { return m_numBlocksX; }
    uint32_t OcclusionMaskHeight() const { return m_numBlocksY; }

private:
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csOcclusion;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_csInterpolate;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_occlusionCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_interpolationCB;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_linearClampSampler;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_occlusionTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_occlusionSrv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_occlusionUav;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSrv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUav;

    uint32_t m_width = 0, m_height = 0;
    uint32_t m_numBlocksX = 0, m_numBlocksY = 0, m_blockSize = 16;
    bool m_valid = false;
};

} // namespace lsalt
