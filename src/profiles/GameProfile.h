#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Types.h"

namespace lsalt {

// All settings that apply to one specific game/executable. The UI reads
// and writes these; the Capture/Scaling/FrameGen engines (Phase 2-4) will
// consume them when a matching process is detected.
struct GameProfile {
    std::string displayName;
    std::string executableName;   // e.g. "VALORANT-Win64-Shipping.exe" (matched case-insensitively)
    std::string notes;

    ScalingAlgorithm scalingAlgorithm = ScalingAlgorithm::Bilinear;
    int scalePercent = 100;                 // 25-200, custom scaling percentage
    bool preserveAspectRatio = true;
    WindowMode windowMode = WindowMode::BorderlessWindow;

    bool frameGenEnabled = false;
    FrameGenMultiplier frameGenMultiplier = FrameGenMultiplier::Off;
    FrameGenQualityMode frameGenQuality = FrameGenQualityMode::Balanced;

    GpuPreference gpuPreference = GpuPreference::Auto;
    int32_t forcedAdapterIndex = -1;

    CaptureMethod captureMethod = CaptureMethod::WindowsGraphicsCapture;
    RenderApi renderApi = RenderApi::D3D11;

    bool vsync = false;
    int fpsLimit = 0;                        // 0 = unlimited
    bool lowGpuOverheadMode = false;

    bool isBuiltIn = false; // built-in sample profiles are not user-deletable
};

void to_json(nlohmann::json& j, const GameProfile& p);
void from_json(const nlohmann::json& j, GameProfile& p);

// Ships with the app so the "Game Profiles" panel is useful out of the box.
// Reflects the recommendations from the project brief: competitive/
// latency-sensitive titles do NOT get frame generation enabled by default.
std::vector<GameProfile> BuiltInSampleProfiles();

} // namespace lsalt
