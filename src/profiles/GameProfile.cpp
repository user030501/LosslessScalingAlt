#include "GameProfile.h"

namespace lsalt {

void to_json(nlohmann::json& j, const GameProfile& p) {
    j = nlohmann::json{
        {"displayName", p.displayName},
        {"executableName", p.executableName},
        {"notes", p.notes},
        {"scalingAlgorithm", static_cast<int>(p.scalingAlgorithm)},
        {"scalePercent", p.scalePercent},
        {"preserveAspectRatio", p.preserveAspectRatio},
        {"windowMode", static_cast<int>(p.windowMode)},
        {"frameGenEnabled", p.frameGenEnabled},
        {"frameGenMultiplier", static_cast<int>(p.frameGenMultiplier)},
        {"frameGenQuality", static_cast<int>(p.frameGenQuality)},
        {"gpuPreference", static_cast<int>(p.gpuPreference)},
        {"forcedAdapterIndex", p.forcedAdapterIndex},
        {"captureMethod", static_cast<int>(p.captureMethod)},
        {"renderApi", static_cast<int>(p.renderApi)},
        {"vsync", p.vsync},
        {"fpsLimit", p.fpsLimit},
        {"lowGpuOverheadMode", p.lowGpuOverheadMode},
        {"isBuiltIn", p.isBuiltIn},
        {"installPath", p.installPath},
        {"lastPlayedUnixSeconds", p.lastPlayedUnixSeconds},
        {"targetResWidth", p.targetResWidth},
        {"targetResHeight", p.targetResHeight},
        {"inputResWidth", p.inputResWidth},
        {"inputResHeight", p.inputResHeight},
        {"scalingQuality", static_cast<int>(p.scalingQuality)},
        {"sharpenLevel", static_cast<int>(p.sharpenLevel)},
        {"aspectMode", static_cast<int>(p.aspectMode)},
        {"profileSchemaVersion", p.profileSchemaVersion},
    };
}

void from_json(const nlohmann::json& j, GameProfile& p) {
    p.displayName = j.value("displayName", std::string{});
    p.executableName = j.value("executableName", std::string{});
    p.notes = j.value("notes", std::string{});
    p.scalingAlgorithm = static_cast<ScalingAlgorithm>(j.value("scalingAlgorithm", 1));
    if (p.scalingAlgorithm == ScalingAlgorithm::FSR1 || p.scalingAlgorithm == ScalingAlgorithm::NIS) {
        p.scalingAlgorithm = ScalingAlgorithm::Bilinear;
    }
    p.scalePercent = j.value("scalePercent", 100);
    p.preserveAspectRatio = j.value("preserveAspectRatio", true);
    p.windowMode = static_cast<WindowMode>(j.value("windowMode", 1));
    p.frameGenEnabled = j.value("frameGenEnabled", false);
    p.frameGenMultiplier = static_cast<FrameGenMultiplier>(j.value("frameGenMultiplier", 1));
    p.frameGenQuality = static_cast<FrameGenQualityMode>(j.value("frameGenQuality", 1));
    p.gpuPreference = static_cast<GpuPreference>(j.value("gpuPreference", 0));
    p.forcedAdapterIndex = j.value("forcedAdapterIndex", -1);
    p.captureMethod = static_cast<CaptureMethod>(j.value("captureMethod", 0));
    p.renderApi = static_cast<RenderApi>(j.value("renderApi", 0));
    p.vsync = j.value("vsync", false);
    p.fpsLimit = j.value("fpsLimit", 0);
    p.lowGpuOverheadMode = j.value("lowGpuOverheadMode", false);
    p.isBuiltIn = j.value("isBuiltIn", false);
    p.installPath = j.value("installPath", std::string{});
    p.lastPlayedUnixSeconds = j.value("lastPlayedUnixSeconds", static_cast<int64_t>(0));
    p.targetResWidth = j.value("targetResWidth", 0u);
    p.targetResHeight = j.value("targetResHeight", 0u);
    // Phase 7 fields: missing in any profile written before this version
    // existed, in which case `.value()`'s defaults alone already make
    // loading that file safe. ProfileManager::MigrateIfNeeded additionally
    // detects profileSchemaVersion < kCurrentSchemaVersion and re-saves
    // the file with these defaults made explicit + the version bumped,
    // logged, rather than silently re-defaulting on every single load.
    p.inputResWidth = j.value("inputResWidth", 0u);
    p.inputResHeight = j.value("inputResHeight", 0u);
    p.scalingQuality = static_cast<ScalingQualityPreset>(j.value("scalingQuality", 1));
    p.sharpenLevel = static_cast<SharpenLevel>(j.value("sharpenLevel", 0));
    p.aspectMode = static_cast<AspectMode>(j.value("aspectMode", 1));
    p.profileSchemaVersion = j.value("profileSchemaVersion", 0);
}

std::vector<GameProfile> BuiltInSampleProfiles() {
    std::vector<GameProfile> profiles;

    // Valorant: competitive FPS, latency-critical. No frame generation by
    // default; conservative scaling only, on the integrated GPU is NOT
    // recommended for this title given its anti-cheat/driver sensitivity,
    // so default to Auto (dGPU when present).
    {
        GameProfile p;
        p.displayName = "Valorant";
        p.executableName = "VALORANT-Win64-Shipping.exe";
        p.notes = "Competitive shooter: frame generation disabled by default "
                  "to avoid added input latency. Scaling is optional and off by default.";
        p.scalingAlgorithm = ScalingAlgorithm::Bilinear;
        p.scalePercent = 100;
        p.windowMode = WindowMode::Fullscreen;
        p.frameGenEnabled = false;
        p.frameGenMultiplier = FrameGenMultiplier::Off;
        p.vsync = false;
        p.lowGpuOverheadMode = true;
        p.scalingQuality = ScalingQualityPreset::Performance;
        p.sharpenLevel = SharpenLevel::Off;
        p.aspectMode = AspectMode::Fit;
        p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    // GTA V: single-player, GPU-bound on this hardware class. Real
    // upscaling (Bicubic, a real implemented algorithm here) plus frame
    // generation is where this app can help the most. NOTE: this
    // previously defaulted to ScalingAlgorithm::FSR1, which this engine
    // does not actually implement (it silently fell back to Bilinear) --
    // changed to a real algorithm so the default never advertises a
    // capability that isn't genuinely there.
    {
        GameProfile p;
        p.displayName = "Grand Theft Auto V";
        p.executableName = "GTA5.exe";
        p.notes = "Single-player, GPU-bound on entry-level hardware: upscaling from a "
                  "lower render resolution plus 2x frame generation is recommended.";
        p.scalingAlgorithm = ScalingAlgorithm::Bicubic;
        p.scalePercent = 75;
        p.windowMode = WindowMode::BorderlessWindow;
        p.frameGenEnabled = true;
        p.frameGenMultiplier = FrameGenMultiplier::X2;
        p.frameGenQuality = FrameGenQualityMode::Balanced;
        p.vsync = false;
        p.scalingQuality = ScalingQualityPreset::Quality;
        p.sharpenLevel = SharpenLevel::Medium;
        p.aspectMode = AspectMode::Fit;
        p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    // ROS (Rules of Survival) Legacy client: older battle-royale title,
    // CPU/GPU light but benefits from a stable frame cap and mild upscaling.
    {
        GameProfile p;
        p.displayName = "ROS Legacy";
        p.executableName = "ROS.exe";
        p.notes = "Older, lighter engine. Mild upscaling is fine; frame generation is "
                  "optional since it is a competitive battle-royale title.";
        p.scalingAlgorithm = ScalingAlgorithm::Bicubic;
        p.scalePercent = 90;
        p.windowMode = WindowMode::BorderlessWindow;
        p.frameGenEnabled = false;
        p.frameGenMultiplier = FrameGenMultiplier::Off;
        p.fpsLimit = 90;
        p.scalingQuality = ScalingQualityPreset::Balanced;
        p.sharpenLevel = SharpenLevel::Low;
        p.aspectMode = AspectMode::Fit;
        p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    // Roblox: wildly variable engine load depending on the experience.
    // Conservative defaults; user can enable frame-gen per-experience later.
    {
        GameProfile p;
        p.displayName = "Roblox";
        p.executableName = "RobloxPlayerBeta.exe";
        p.notes = "Performance varies heavily by experience/place. Defaults are "
                  "conservative; tune per-experience if needed.";
        p.scalingAlgorithm = ScalingAlgorithm::Bilinear;
        p.scalePercent = 100;
        p.windowMode = WindowMode::Windowed;
        p.frameGenEnabled = false;
        p.frameGenMultiplier = FrameGenMultiplier::Off;
        p.scalingQuality = ScalingQualityPreset::Balanced;
        p.sharpenLevel = SharpenLevel::Off;
        p.aspectMode = AspectMode::Fit;
        p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    // Custom Application: a generic, neutral starting template for any
    // other app/game the user wants to add manually. This is NOT a claim
    // of compatibility with any specific title -- it exists so "Create
    // Profile" has a sensible, clearly-labeled default rather than forcing
    // the user to pick one of the game-specific profiles above as a base.
    {
        GameProfile p;
        p.displayName = "Custom Application";
        p.executableName = "";
        p.notes = "Generic starting template for any application. Not tied to any "
                  "specific game -- fill in the executable name and adjust settings "
                  "as needed.";
        p.scalingAlgorithm = ScalingAlgorithm::Bilinear;
        p.scalePercent = 100;
        p.windowMode = WindowMode::BorderlessWindow;
        p.frameGenEnabled = false;
        p.frameGenMultiplier = FrameGenMultiplier::Off;
        p.scalingQuality = ScalingQualityPreset::Balanced;
        p.sharpenLevel = SharpenLevel::Off;
        p.aspectMode = AspectMode::Fit;
        p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    return profiles;
}

} // namespace lsalt
