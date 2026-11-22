#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "Types.h"
#include "Logger.h" // for LogLevel, used by AppConfig below

namespace lsalt {

// Application-wide settings that are not tied to a specific game profile:
// window position, last-selected GPU, global defaults new profiles start
// from, and advanced/diagnostic toggles.
struct AppConfig {
    int windowX = -1; // -1 = let the OS pick
    int windowY = -1;
    int windowWidth = 1180;
    int windowHeight = 760;

    int preferredAdapterIndex = -1; // -1 = Auto
    LogLevel logLevel = LogLevel::Info;

    ScalingAlgorithm defaultScalingAlgorithm = ScalingAlgorithm::Bilinear;
    int defaultScalePercent = 100;
    FrameGenMultiplier defaultFrameGenMultiplier = FrameGenMultiplier::Off;
    FrameGenQualityMode defaultFrameGenQuality = FrameGenQualityMode::Balanced;
    CaptureMethod defaultCaptureMethod = CaptureMethod::WindowsGraphicsCapture;
    RenderApi defaultRenderApi = RenderApi::D3D11;

    bool lowGpuOverheadMode = false;
    bool vsync = false;
    int fpsLimit = 0;

    std::string lastSelectedProfile;
};

void to_json(nlohmann::json& j, const AppConfig& c);
void from_json(const nlohmann::json& j, AppConfig& c);

// Loads/saves AppConfig to %LOCALAPPDATA%\LSAlt\config.json.
class ConfigManager {
public:
    ConfigManager();

    AppConfig& Get() { return m_config; }
    const AppConfig& Get() const { return m_config; }

    void Load();
    void Save() const;
    void ResetToDefaults();

    std::wstring ConfigFilePath() const;

private:
    AppConfig m_config;
};

} // namespace lsalt
