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
        {"autoProfileSwitchingEnabled", c.autoProfileSwitchingEnabled},
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
    c.autoProfileSwitchingEnabled = j.value("autoProfileSwitchingEnabled", true);
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

namespace {
std::wstring BackupFilePath(const std::wstring& configPath) { return configPath + L".bak"; }

bool TryLoadFrom(const std::wstring& path, AppConfig& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    try {
        nlohmann::json j;
        file >> j;
        out = j.get<AppConfig>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace

void ConfigManager::Load() {
    const std::wstring path = ConfigFilePath();
    if (TryLoadFrom(path, m_config)) return;

    // config.json is missing or failed to parse (e.g. the process was
    // killed mid-write). Before falling back to factory defaults, try the
    // backup Save() keeps of the last known-good config.
    const std::wstring backupPath = BackupFilePath(path);
    if (TryLoadFrom(backupPath, m_config)) {
        Logger::Instance().Warn("ConfigManager", "config.json missing/corrupt; recovered settings from config.json.bak.");
        return;
    }

    Logger::Instance().Error("ConfigManager", "config.json and its backup are both missing/corrupt; using defaults.");
    m_config = AppConfig{};
    Save();
}

void ConfigManager::Save() const {
    const std::wstring path = ConfigFilePath();

    // Back up whatever is currently on disk before overwriting it, so a
    // write that's interrupted partway (crash, power loss) never leaves
    // the user with a corrupt config.json and no way back to their last
    // known-good settings.
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::copy_file(path, BackupFilePath(path), std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::ofstream file(path);
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
