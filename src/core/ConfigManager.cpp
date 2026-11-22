#include "ConfigManager.h"
#include "Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>

namespace lsalt {

void to_json(nlohmann::json& j, const AppConfig& c) {
    j = nlohmann::json{
        {"windowX", c.windowX},
        {"windowY", c.windowY},
        {"windowWidth", c.windowWidth},
        {"windowHeight", c.windowHeight},
        {"preferredAdapterIndex", c.preferredAdapterIndex},
        {"logLevel", static_cast<int>(c.logLevel)},
        {"defaultScalingAlgorithm", static_cast<int>(c.defaultScalingAlgorithm)},
        {"defaultScalePercent", c.defaultScalePercent},
        {"defaultFrameGenMultiplier", static_cast<int>(c.defaultFrameGenMultiplier)},
        {"defaultFrameGenQuality", static_cast<int>(c.defaultFrameGenQuality)},
        {"defaultCaptureMethod", static_cast<int>(c.defaultCaptureMethod)},
        {"defaultRenderApi", static_cast<int>(c.defaultRenderApi)},
        {"lowGpuOverheadMode", c.lowGpuOverheadMode},
        {"vsync", c.vsync},
        {"fpsLimit", c.fpsLimit},
        {"lastSelectedProfile", c.lastSelectedProfile},
    };
}

void from_json(const nlohmann::json& j, AppConfig& c) {
    c.windowX = j.value("windowX", -1);
    c.windowY = j.value("windowY", -1);
    c.windowWidth = j.value("windowWidth", 1180);
    c.windowHeight = j.value("windowHeight", 760);
    c.preferredAdapterIndex = j.value("preferredAdapterIndex", -1);
    c.logLevel = static_cast<LogLevel>(j.value("logLevel", 1));
    c.defaultScalingAlgorithm = static_cast<ScalingAlgorithm>(j.value("defaultScalingAlgorithm", 1));
    c.defaultScalePercent = j.value("defaultScalePercent", 100);
    c.defaultFrameGenMultiplier = static_cast<FrameGenMultiplier>(j.value("defaultFrameGenMultiplier", 1));
    c.defaultFrameGenQuality = static_cast<FrameGenQualityMode>(j.value("defaultFrameGenQuality", 1));
    c.defaultCaptureMethod = static_cast<CaptureMethod>(j.value("defaultCaptureMethod", 0));
    c.defaultRenderApi = static_cast<RenderApi>(j.value("defaultRenderApi", 0));
    c.lowGpuOverheadMode = j.value("lowGpuOverheadMode", false);
    c.vsync = j.value("vsync", false);
    c.fpsLimit = j.value("fpsLimit", 0);
    c.lastSelectedProfile = j.value("lastSelectedProfile", std::string{});
}

namespace {
std::wstring ConfigDir() {
    PWSTR path = nullptr;
    std::wstring result = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        result = std::wstring(path) + L"\\LSAlt";
        CoTaskMemFree(path);
    }
    std::error_code ec;
    std::filesystem::create_directories(result, ec);
    return result;
}
} // namespace

ConfigManager::ConfigManager() {
    Load();
}

std::wstring ConfigManager::ConfigFilePath() const {
    return ConfigDir() + L"\\config.json";
}

void ConfigManager::Load() {
    std::ifstream file(ConfigFilePath());
    if (!file.is_open()) {
        m_config = AppConfig{};
        Save();
        return;
    }
    try {
        nlohmann::json j;
        file >> j;
        m_config = j.get<AppConfig>();
    } catch (const std::exception& e) {
        Logger::Instance().Error("ConfigManager", std::string("Failed to parse config.json: ") + e.what());
        m_config = AppConfig{};
    }
}

void ConfigManager::Save() const {
    std::ofstream file(ConfigFilePath());
    if (!file.is_open()) {
        Logger::Instance().Error("ConfigManager", "Could not open config.json for writing.");
        return;
    }
    nlohmann::json j = m_config;
    file << j.dump(2);
}

void ConfigManager::ResetToDefaults() {
    m_config = AppConfig{};
    Save();
}

} // namespace lsalt
