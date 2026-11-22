#pragma once
#include <vector>
#include <string>
#include <optional>
#include "GameProfile.h"

namespace lsalt {

// Owns the list of game profiles, persists them to disk (via the same
// %LOCALAPPDATA%\LSAlt directory ConfigManager uses) and provides simple
// lookup helpers the Capture Engine (Phase 2) will later use to match a
// running process to a profile.
class ProfileManager {
public:
    ProfileManager();

    void Load();
    void Save() const;

    const std::vector<GameProfile>& Profiles() const { return m_profiles; }
    GameProfile* AtIndex(int index);

    GameProfile* Find(const std::string& displayName);
    GameProfile* FindByExecutable(const std::string& executableName);

    void AddOrUpdate(const GameProfile& profile);
    bool Remove(const std::string& displayName); // built-in profiles cannot be removed

    std::wstring ProfilesFilePath() const;

private:
    std::vector<GameProfile> m_profiles;
};

} // namespace lsalt
