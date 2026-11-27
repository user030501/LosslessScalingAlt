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

    // Phase 4 additions --------------------------------------------------
    // Full path to the executable, when known (populated by the "Browse..."
    // file picker, or by automatic detection once a matching process has
    // actually been seen running). executableName above remains the match
    // key; this is display-only (Game Library card's "Installation path").
    std::string installPath;
    // Unix timestamp (seconds) this profile was last auto-detected as
    // running, or 0 if never. Display-only ("Last played").
    int64_t lastPlayedUnixSeconds = 0;
    // Optional target processing resolution override ("Game resolution").
    // 0x0 means "native/unset" -- unused until the Capture/Scaling Engines
    // exist to actually act on it. Phase 7: this is the OUTPUT resolution;
    // see inputResWidth/Height below for the matching input side.
    uint32_t targetResWidth = 0;
    uint32_t targetResHeight = 0;

    // Phase 7 additions ---------------------------------------------------
    // Input (captured/source) resolution override. 0x0 = automatic (use
    // whatever the real captured window's actual size is).
    uint32_t inputResWidth = 0;
    uint32_t inputResHeight = 0;

    ScalingQualityPreset scalingQuality = ScalingQualityPreset::Balanced;
    SharpenLevel sharpenLevel = SharpenLevel::Off;
    AspectMode aspectMode = AspectMode::Fit;
    bool adaptiveScalingEnabled = false;
    ScalingQualityPreset preferredScalingQuality = ScalingQualityPreset::Balanced;
    ScalingQualityPreset minimumScalingQuality = ScalingQualityPreset::Performance;
    ScalingQualityPreset maximumScalingQuality = ScalingQualityPreset::Quality;
    SharpenLevel preferredSharpenLevel = SharpenLevel::Off;
    ScalingAlgorithm preferredScalingAlgorithm = ScalingAlgorithm::Bilinear;
    int adaptiveScalingCooldownSeconds = 30;

    // Bumped whenever a profile-file schema change needs old files to be
    // migrated (see ProfileManager::MigrateIfNeeded). 0 means "written
    // before this field existed" -- nlohmann's `.value(key, default)`
    // already makes every individual field backward-compatible on its
    // own; this version number is for migration steps that are more than
    // "just default a missing field" (recorded explicitly, logged, and
    // the file re-saved, rather than silently re-defaulting every load).
    int profileSchemaVersion = 0;
    static constexpr int kCurrentSchemaVersion = 2;
};

void to_json(nlohmann::json& j, const GameProfile& p);
void from_json(const nlohmann::json& j, GameProfile& p);

// Ships with the app so the "Game Profiles" panel is useful out of the box.
// Reflects the recommendations from the project brief: competitive/
// latency-sensitive titles do NOT get frame generation enabled by default.
std::vector<GameProfile> BuiltInSampleProfiles();

} // namespace lsalt
