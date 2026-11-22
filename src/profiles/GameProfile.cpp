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
    };
}

void from_json(const nlohmann::json& j, GameProfile& p) {
    p.displayName = j.value("displayName", std::string{});
    p.executableName = j.value("executableName", std::string{});
    p.notes = j.value("notes", std::string{});
    p.scalingAlgorithm = static_cast<ScalingAlgorithm>(j.value("scalingAlgorithm", 1));
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
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    // GTA V: single-player, GPU-bound on this hardware class. FSR-style
    // upscaling plus frame generation is where this app can help the most.
    {
        GameProfile p;
        p.displayName = "Grand Theft Auto V";
        p.executableName = "GTA5.exe";
        p.notes = "Single-player, GPU-bound on entry-level hardware: upscaling from a "
                  "lower render resolution plus 2x frame generation is recommended.";
        p.scalingAlgorithm = ScalingAlgorithm::FSR1;
        p.scalePercent = 75;
        p.windowMode = WindowMode::BorderlessWindow;
        p.frameGenEnabled = true;
        p.frameGenMultiplier = FrameGenMultiplier::X2;
        p.frameGenQuality = FrameGenQualityMode::Balanced;
        p.vsync = false;
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
        p.isBuiltIn = true;
        profiles.push_back(p);
    }

    return profiles;
}

} // namespace lsalt
