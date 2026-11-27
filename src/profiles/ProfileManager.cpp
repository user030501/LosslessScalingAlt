#include "ProfileManager.h"
#include "../core/Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace lsalt {

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

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}
} // namespace

ProfileManager::ProfileManager() {
    Load();
}

std::wstring ProfileManager::ProfilesFilePath() const {
    return ConfigDir() + L"\\profiles.json";
}

void ProfileManager::Load() {
    const std::wstring path = ProfilesFilePath();
    std::ifstream file(path);
    if (!file.is_open()) {
        m_profiles = BuiltInSampleProfiles();
        Save();
        Logger::Instance().Info("ProfileManager", "No profiles.json found; seeded built-in sample profiles.");
        return;
    }

    try {
        nlohmann::json j;
        file >> j;
        m_profiles = j.get<std::vector<GameProfile>>();
        if (MigrateIfNeeded()) {
            Save();
            Logger::Instance().Info("ProfileManager", "Migrated legacy profile fields and re-saved profiles.json.");
        }
        Logger::Instance().Info("ProfileManager", "Loaded " + std::to_string(m_profiles.size()) + " profile(s).");
    } catch (const std::exception& e) {
        Logger::Instance().Error("ProfileManager", std::string("Failed to parse profiles.json: ") + e.what());
        m_profiles = BuiltInSampleProfiles();
    }
}

void ProfileManager::Save() const {
    const std::wstring path = ProfilesFilePath();
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("ProfileManager", "Could not open profiles.json for writing.");
        return;
    }
    nlohmann::json j = m_profiles;
    file << j.dump(2);
}

GameProfile* ProfileManager::AtIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_profiles.size())) return nullptr;
    return &m_profiles[index];
}

GameProfile* ProfileManager::Find(const std::string& displayName) {
    for (auto& p : m_profiles) {
        if (p.displayName == displayName) return &p;
    }
    return nullptr;
}

GameProfile* ProfileManager::FindByExecutable(const std::string& executableName) {
    for (auto& p : m_profiles) {
        if (IEquals(p.executableName, executableName)) return &p;
    }
    return nullptr;
}

int ProfileManager::IndexOf(const std::string& displayName) const {
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].displayName == displayName) return static_cast<int>(i);
    }
    return -1;
}

void ProfileManager::AddOrUpdate(const GameProfile& profile) {
    if (GameProfile* existing = Find(profile.displayName)) {
        *existing = profile;
    } else {
        m_profiles.push_back(profile);
    }
    Save();
}

bool ProfileManager::Remove(const std::string& displayName) {
    auto it = std::find_if(m_profiles.begin(), m_profiles.end(), [&](const GameProfile& p) {
        return p.displayName == displayName;
    });
    if (it == m_profiles.end() || it->isBuiltIn) return false;
    m_profiles.erase(it);
    Save();
    return true;
}

bool ProfileManager::MigrateIfNeeded() {
    bool changed = false;
    for (GameProfile& p : m_profiles) {
        if (p.profileSchemaVersion < GameProfile::kCurrentSchemaVersion) {
            p.profileSchemaVersion = GameProfile::kCurrentSchemaVersion;
            changed = true;
        }
        // These vendor names are not implemented by this application.
        // Legacy files that selected them must migrate to a real engine
        // algorithm rather than appearing as a supported feature.
        if (p.scalingAlgorithm == ScalingAlgorithm::FSR1 ||
            p.scalingAlgorithm == ScalingAlgorithm::NIS) {
            p.scalingAlgorithm = ScalingAlgorithm::Bilinear;
            changed = true;
        }
        if (p.targetResWidth != 0 && p.inputResWidth == 0) {
            p.inputResWidth = 0;
        }
    }
    return changed;
}

} // namespace lsalt
