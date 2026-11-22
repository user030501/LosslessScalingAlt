#pragma once
#include <cstdint>
#include <string>

// Shared enums used across the Scaling, Frame Generation, GPU and Profile
// modules. These describe *settings* the UI can configure in every phase;
// which values are actually wired up to a working engine depends on which
// phase has landed (see docs/ARCHITECTURE.md for the phase matrix).

namespace lsalt {

enum class ScalingAlgorithm : int {
    Nearest = 0,
    Bilinear,
    Bicubic,
    Lanczos,
    IntegerScale,
    FSR1,   // AMD FidelityFX Super Resolution 1.x (spatial)
    NIS,    // NVIDIA Image Scaling
    Count
};

inline const char* ToString(ScalingAlgorithm a) {
    switch (a) {
        case ScalingAlgorithm::Nearest:      return "Nearest Neighbor";
        case ScalingAlgorithm::Bilinear:     return "Linear (Bilinear)";
        case ScalingAlgorithm::Bicubic:      return "Bicubic";
        case ScalingAlgorithm::Lanczos:      return "Lanczos";
        case ScalingAlgorithm::IntegerScale: return "Integer Scaling";
        case ScalingAlgorithm::FSR1:         return "AMD FSR 1.0 (spatial)";
        case ScalingAlgorithm::NIS:          return "NVIDIA Image Scaling";
        default:                             return "Unknown";
    }
}

enum class WindowMode : int { Fullscreen = 0, BorderlessWindow, Windowed };

inline const char* ToString(WindowMode m) {
    switch (m) {
        case WindowMode::Fullscreen:       return "Fullscreen";
        case WindowMode::BorderlessWindow: return "Borderless Window";
        case WindowMode::Windowed:         return "Windowed";
        default:                           return "Unknown";
    }
}

enum class FrameGenMultiplier : int { Off = 1, X2 = 2, X3 = 3, X4 = 4 };

inline const char* ToString(FrameGenMultiplier m) {
    switch (m) {
        case FrameGenMultiplier::Off: return "Off";
        case FrameGenMultiplier::X2:  return "2x";
        case FrameGenMultiplier::X3:  return "3x (planned)";
        case FrameGenMultiplier::X4:  return "4x (planned)";
        default:                      return "Unknown";
    }
}

enum class FrameGenQualityMode : int { Performance = 0, Balanced, Quality };

inline const char* ToString(FrameGenQualityMode m) {
    switch (m) {
        case FrameGenQualityMode::Performance: return "Performance";
        case FrameGenQualityMode::Balanced:    return "Balanced";
        case FrameGenQualityMode::Quality:     return "Quality";
        default:                               return "Unknown";
    }
}

enum class CaptureMethod : int { WindowsGraphicsCapture = 0, DesktopDuplication };

inline const char* ToString(CaptureMethod m) {
    switch (m) {
        case CaptureMethod::WindowsGraphicsCapture: return "Windows Graphics Capture";
        case CaptureMethod::DesktopDuplication:      return "Desktop Duplication API";
        default:                                     return "Unknown";
    }
}

enum class RenderApi : int { D3D11 = 0, D3D12 };

inline const char* ToString(RenderApi a) {
    switch (a) {
        case RenderApi::D3D11: return "Direct3D 11";
        case RenderApi::D3D12: return "Direct3D 12";
        default:                return "Unknown";
    }
}

enum class GpuPreference : int { Auto = 0, ForceAdapterIndex };

// Identifies a physical adapter the way the UI and config present it.
struct GpuAdapterInfo {
    int32_t index = -1;
    std::wstring description;
    uint32_t vendorId = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
    bool isHardware = true;
    bool supportsD3D11 = false;
};

constexpr uint32_t kVendorNVIDIA = 0x10DE;
constexpr uint32_t kVendorAMD    = 0x1002;
constexpr uint32_t kVendorIntel  = 0x8086;

} // namespace lsalt
