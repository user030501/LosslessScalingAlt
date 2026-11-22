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

void GameSessionController::RestoreState(FrameGenEngineD3D11* engine, int& selectedProfileIndex, bool liveDemoActive) {
    if (!m_hasSavedState) return;
    selectedProfileIndex = m_savedSelectedProfileIndex;
    if (engine) {
        engine->SetQualityMode(m_savedQuality);
        engine->SetMultiplier(m_savedMultiplier);
        // See the class comment on Update(): don't fight the user's own
        // Live Engine checkbox over the one flag both currently share.
        if (!liveDemoActive) engine->SetEnabled(m_savedEnabled);
    }
    m_hasSavedState = false;
}

void GameSessionController::ApplyProfileToEngine(const GameProfile& profile, FrameGenEngineD3D11* engine, bool liveDemoActive) {
    if (!engine) return;
    engine->SetQualityMode(profile.frameGenQuality);
    engine->SetMultiplier(profile.frameGenEnabled ? profile.frameGenMultiplier : FrameGenMultiplier::Off);
    if (!liveDemoActive) engine->SetEnabled(profile.frameGenEnabled);
}

bool GameSessionController::Update(const std::vector<DetectedProcess>& scan, ProfileManager& profiles,
                                     FrameGenEngineD3D11* engine, int& selectedProfileIndex, std::string& statusMessage,
                                     bool liveDemoActive) {
    m_lastScan = scan;

    if (!m_autoSwitchEnabled) {
        if (m_hasSavedState) {
            RestoreState(engine, selectedProfileIndex, liveDemoActive);
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
        RestoreState(engine, selectedProfileIndex, liveDemoActive);
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
            ApplyProfileToEngine(*bestProfile, engine, liveDemoActive);

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
                              ") running -- applied its quality/multiplier settings to this app" +
                              (liveDemoActive ? " (Live Engine's own enable state was left alone -- it's under your control)."
                                                : ", including enable/disable.") +
                              " This does not automatically start capturing it -- use the Game Library tab's "
                              "\"Capture\" button on this card to actually capture its frames.";
            Logger::Instance().Info("GameSessionController", statusMessage);
            return true;
        }
        return false;
    }

    return false;
}

} // namespace lsalt
