#include "FrameGenEngine.h"
#include "MotionEstimator.h"
#include "FrameInterpolator.h"
#include "FramePacing.h"
#include "../core/Logger.h"

#include <windows.h>
#include <d3d11.h>

namespace lsalt {

namespace {
std::wstring ShaderDirectory() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path = path.substr(0, slash);
    return path + L"\\shaders\\framegen";
}
} // namespace

FrameGenEngineD3D11::FrameGenEngineD3D11() = default;
FrameGenEngineD3D11::~FrameGenEngineD3D11() = default;

bool FrameGenEngineD3D11::DetectCapabilities(ID3D11Device* device) {
    m_capabilities = FrameGenCapabilities{};

    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    if (level < D3D_FEATURE_LEVEL_11_0) {
        m_capabilities.computeShadersSupported = false;
        m_capabilities.unsupportedReason =
            "GPU/driver reports feature level below 11_0; Shader Model 5.0 compute shaders are required.";
        Logger::Instance().Warn("FrameGenEngine", m_capabilities.unsupportedReason);
        return false;
    }
    m_capabilities.computeShadersSupported = true;

    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fmt2{};
    fmt2.InFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fmt2, sizeof(fmt2));
    const bool typedStore = SUCCEEDED(hr) && (fmt2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    m_capabilities.typedUavLoadStoreSupported = typedStore;

    if (!typedStore) {
        m_capabilities.unsupportedReason =
            "GPU/driver does not report D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE for R8G8B8A8_UNORM; "
            "the interpolation compute shader cannot write its output.";
        Logger::Instance().Warn("FrameGenEngine", m_capabilities.unsupportedReason);
        return false;
    }

    return true;
}

bool FrameGenEngineD3D11::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND windowForRefreshRate) {
    m_device = device;
    m_context = context;
    m_window = windowForRefreshRate;

    if (!DetectCapabilities(device)) {
        m_initialized = false;
        return false;
    }

    m_motionEstimator = std::make_unique<MotionEstimator>();
    m_interpolator = std::make_unique<FrameInterpolator>();
    m_pacing = std::make_unique<FramePacing>();

    const std::wstring shaderDir = ShaderDirectory();
    if (!m_motionEstimator->Initialize(device, shaderDir) || !m_interpolator->Initialize(device, shaderDir)) {
        m_capabilities.unsupportedReason = "Failed to compile/load one or more frame generation shaders. Check the log.";
        m_initialized = false;
        return false;
    }

    m_pacing->Initialize(device, windowForRefreshRate);

    m_initialized = true;
    Logger::Instance().Info("FrameGenEngine", "Initialized successfully (compute shader + typed UAV store supported).");
    return true;
}

void FrameGenEngineD3D11::SetEnabled(bool enabled) { m_enabled = enabled; }

bool FrameGenEngineD3D11::SetMultiplier(FrameGenMultiplier multiplier) {
    if (multiplier != FrameGenMultiplier::X2 && multiplier != FrameGenMultiplier::Off) {
        Logger::Instance().Warn("FrameGenEngine", "3x/4x are architecture placeholders only; not implemented yet.");
        return false;
    }
    m_multiplier = multiplier;
    return true;
}

void FrameGenEngineD3D11::SetQualityMode(FrameGenQualityMode quality) {
    if (quality == m_quality) return;
    m_quality = quality;
    if (m_width > 0 && m_height > 0) RecreatePipeline();
}

bool FrameGenEngineD3D11::RecreatePipeline() {
    if (!m_initialized || m_width == 0 || m_height == 0) return false;

    const FrameGenQualityParams params = FrameGenQualityParams::For(m_quality);
    if (!m_motionEstimator->Resize(m_device.Get(), m_width, m_height, params)) {
        Logger::Instance().Error("FrameGenEngine", "MotionEstimator::Resize failed.");
        return false;
    }
    if (!m_interpolator->Resize(m_device.Get(), m_width, m_height,
                                 m_motionEstimator->NumBlocksX(), m_motionEstimator->NumBlocksY(),
                                 m_motionEstimator->BlockSize())) {
        Logger::Instance().Error("FrameGenEngine", "FrameInterpolator::Resize failed.");
        return false;
    }
    return true;
}

bool FrameGenEngineD3D11::Resize(uint32_t width, uint32_t height) {
    if (!m_initialized) return false;
    if (width == m_width && height == m_height) return true;
    m_width = width;
    m_height = height;
    return RecreatePipeline();
}

ID3D11ShaderResourceView* FrameGenEngineD3D11::GenerateInterpolatedFrame(ID3D11ShaderResourceView* frameA,
                                                                           ID3D11ShaderResourceView* frameB) {
    if (!IsEnabled() || m_multiplier != FrameGenMultiplier::X2) return nullptr;
    if (!frameA || !frameB) return nullptr;

    // Device-removed is a real, recoverable-by-restart failure mode on the
    // hardware this targets (laptop GPUs under thermal/driver stress).
    // Detect it and fail gracefully rather than crashing or producing
    // garbage frames; full device re-creation is out of scope for this
    // module (it would require GpuDeviceManager/App to rebuild the
    // swapchain too) and is documented as a known limitation.
    const HRESULT removedReason = m_device->GetDeviceRemovedReason();
    if (removedReason != S_OK) {
        if (m_enabled) {
            Logger::Instance().Error("FrameGenEngine", "D3D11 device removed, hr=" + std::to_string(removedReason) +
                                                          ". Disabling frame generation.");
        }
        m_enabled = false;
        m_capabilities.unsupportedReason = "D3D11 device was removed (driver crash/reset/TDR). Restart the app to recover.";
        return nullptr;
    }

    m_pacing->RecordSourceFrame();
    m_pacing->BeginGpuTiming(m_context.Get());

    m_motionEstimator->BuildPyramidA(m_context.Get(), frameA);
    m_motionEstimator->BuildPyramidB(m_context.Get(), frameB);
    ID3D11ShaderResourceView* fwdMV = m_motionEstimator->EstimateForward(m_context.Get());
    ID3D11ShaderResourceView* bwdMV = m_motionEstimator->EstimateBackward(m_context.Get());
    m_pacing->MarkMotionEstimationDone(m_context.Get());

    ID3D11ShaderResourceView* generated = nullptr;
    if (fwdMV && bwdMV) {
        generated = m_interpolator->Interpolate(m_context.Get(), frameA, frameB, fwdMV, bwdMV);
    }
    m_pacing->MarkInterpolationDone(m_context.Get());

    if (generated) {
        m_pacing->RecordGeneratedFrame();
        m_pacing->RecordDisplayedFrame(); // frame A
        m_pacing->RecordDisplayedFrame(); // generated frame
        m_pacing->RecordDisplayedFrame(); // frame B
    }

    return generated;
}

ID3D11Texture2D* FrameGenEngineD3D11::GeneratedOutputTexture() const {
    return m_interpolator ? m_interpolator->OutputTexture() : nullptr;
}

ID3D11ShaderResourceView* FrameGenEngineD3D11::GeneratedOutputSRV() const {
    return m_interpolator ? m_interpolator->OutputSRV() : nullptr;
}

ID3D11Texture2D* FrameGenEngineD3D11::OcclusionMaskTexture() const {
    return m_interpolator ? m_interpolator->OcclusionMaskTexture() : nullptr;
}

uint32_t FrameGenEngineD3D11::OcclusionMaskWidth() const {
    return m_interpolator ? m_interpolator->OcclusionMaskWidth() : 0;
}

uint32_t FrameGenEngineD3D11::OcclusionMaskHeight() const {
    return m_interpolator ? m_interpolator->OcclusionMaskHeight() : 0;
}

double FrameGenEngineD3D11::DetectedRefreshHz() const {
    return m_pacing ? m_pacing->DetectedRefreshHz() : 0.0;
}

FrameGenStats FrameGenEngineD3D11::GetStats() const {
    FrameGenStats stats;
    stats.engineActive = IsEnabled();
    if (m_pacing) m_pacing->FillStats(stats);

    if (!m_capabilities.computeShadersSupported) {
        stats.statusText = "Unsupported: " + m_capabilities.unsupportedReason;
    } else if (!m_enabled) {
        stats.statusText = "Disabled";
    } else if (m_multiplier == FrameGenMultiplier::Off) {
        stats.statusText = "Enabled, multiplier set to Off (passthrough)";
    } else {
        stats.statusText = "Running (2x)";
    }
    return stats;
}

} // namespace lsalt
