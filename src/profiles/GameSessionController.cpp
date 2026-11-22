#include "GameSessionController.h"
#include "ProfileManager.h"
#include "GameProfile.h"
#include "../framegen/FrameGenEngine.h"
#include "../core/Logger.h"

#include <ctime>

namespace lsalt {

void GameSessionController::SetAutoSwitchEnabled(bool enabled) {
    m_autoSwitchEnabled = enabled;
    Logger::Instance().Info("GameSessionController",
        enabled ? "Automatic profile switching enabled." : "Automatic profile switching disabled.");
}

void GameSessionController::SaveState(FrameGenEngineD3D11* engine, int selectedProfileIndex) {
    m_savedSelectedProfileIndex = selectedProfileIndex;
    if (engine) {
        m_savedEnabled = engine->IsEnabled();
        m_savedMultiplier = engine->Multiplier();
        m_savedQuality = engine->QualityMode();
    }
    m_hasSavedState = true;
}

void GameSessionController::RestoreState(FrameGenEngineD3D11* engine, int& selectedProfileIndex) {
    if (!m_hasSavedState) return;
    selectedProfileIndex = m_savedSelectedProfileIndex;
    if (engine) {
        engine->SetQualityMode(m_savedQuality);
        engine->SetMultiplier(m_savedMultiplier);
        engine->SetEnabled(m_savedEnabled);
    }
    m_hasSavedState = false;
}

void GameSessionController::ApplyProfileToEngine(const GameProfile& profile, FrameGenEngineD3D11* engine) {
    if (!engine) return;
    engine->SetQualityMode(profile.frameGenQuality);
    engine->SetMultiplier(profile.frameGenEnabled ? profile.frameGenMultiplier : FrameGenMultiplier::Off);
    engine->SetEnabled(profile.frameGenEnabled);
}

bool GameSessionController::Update(const std::vector<DetectedProcess>& scan, ProfileManager& profiles,
                                     FrameGenEngineD3D11* engine, int& selectedProfileIndex, std::string& statusMessage) {
    m_lastScan = scan;

    if (!m_autoSwitchEnabled) {
        if (m_hasSavedState) {
            RestoreState(engine, selectedProfileIndex);
            m_activeProcessId = 0;
            m_activeProfileName.reset();
            return true;
        }
        return false;
    }

    bool activeStillRunning = false;
    if (m_activeProcessId != 0) {
        for (const auto& proc : scan) {
            if (proc.processId == m_activeProcessId) { activeStillRunning = true; break; }
        }
    }

    if (m_activeProcessId != 0 && !activeStillRunning) {
        const std::string closedName = m_activeProfileName.value_or(std::string{});
        RestoreState(engine, selectedProfileIndex);
        m_activeProcessId = 0;
        m_activeProfileName.reset();
        statusMessage = "'" + closedName + "' is no longer running; restored the previous settings.";
        Logger::Instance().Info("GameSessionController", statusMessage);
        return true;
    }

    if (m_activeProcessId == 0) {
        // Prefer the foreground window's process among all currently
        // running processes that match a known profile; fall back to any
        // other match (e.g. a windowed game running behind the
        // foreground app) if none of the matches is in the foreground.
        const DetectedProcess* best = nullptr;
        GameProfile* bestProfile = nullptr;
        for (const auto& proc : scan) {
            GameProfile* p = profiles.FindByExecutable(proc.executableName);
            if (!p) continue;
            if (!best) { best = &proc; bestProfile = p; }
            if (proc.isForegroundWindow) { best = &proc; bestProfile = p; break; }
        }

        if (best && bestProfile) {
            SaveState(engine, selectedProfileIndex);
            ApplyProfileToEngine(*bestProfile, engine);

            const int idx = profiles.IndexOf(bestProfile->displayName);
            if (idx >= 0) selectedProfileIndex = idx;

            bestProfile->lastPlayedUnixSeconds = static_cast<int64_t>(std::time(nullptr));
            if (bestProfile->installPath.empty() && !best->executablePath.empty()) {
                bestProfile->installPath = best->executablePath;
            }
            profiles.AddOrUpdate(*bestProfile);

            m_activeProcessId = best->processId;
            m_activeProfileName = bestProfile->displayName;
            statusMessage = "Detected " + bestProfile->displayName + " (" + best->executableName +
                              ") running -- applied its Frame Generation settings to this app. This does not yet "
                              "capture or process that game's actual frames (Capture Engine is still unimplemented).";
            Logger::Instance().Info("GameSessionController", statusMessage);
            return true;
        }
        return false;
    }

    return false;
}

} // namespace lsalt
