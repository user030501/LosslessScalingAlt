#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../core/Types.h"
#include "GameDetector.h"

namespace lsalt {

class ProfileManager;
class FrameGenEngineD3D11;
struct GameProfile;

// Phase 4: turns a real process scan (GameDetector::Scan) into automatic
// profile application.
//
// IMPORTANT, and the reason this class takes a plain scan result rather
// than owning a GameDetector itself: it does NOT hook, capture, or modify
// the detected game's rendering in any way. The Capture Engine
// (capture/CaptureEngine.h) remains an unimplemented stub, so "applying a
// profile" here means exactly, and only: changing this app's OWN Frame
// Generation Engine settings (enabled/multiplier/quality) and the UI's
// selected profile -- precisely as if the user had clicked the same
// controls by hand. That is still genuinely useful (it's real prep for
// when capture lands, and it does affect the Live Engine synthetic demo
// if that's separately turned on), but it is not game optimization yet,
// and the UI says so.
//
// Taking the scan as a parameter (instead of a GameDetector&) also makes
// this class fully unit-testable with synthetic process lists -- see
// ProfileDiagnostics -- without needing a second real process running.
class GameSessionController {
public:
    void SetAutoSwitchEnabled(bool enabled);
    bool AutoSwitchEnabled() const { return m_autoSwitchEnabled; }

    // Call once per detection tick (every 1-2s, not every frame) with a
    // fresh process scan. May call into `engine` and mutate
    // `selectedProfileIndex`/`statusMessage`. Returns true if it just
    // changed something (a new game was detected, or the active one
    // closed) so callers only need to log/react on actual transitions.
    bool Update(const std::vector<DetectedProcess>& scan, ProfileManager& profiles, FrameGenEngineD3D11* engine,
                int& selectedProfileIndex, std::string& statusMessage);

    const std::vector<DetectedProcess>& LastScan() const { return m_lastScan; }
    bool HasActiveGame() const { return m_activeProcessId != 0; }
    const std::string& ActiveGameProfileName() const { static const std::string kEmpty; return m_activeProfileName.value_or(kEmpty); }

private:
    void SaveState(FrameGenEngineD3D11* engine, int selectedProfileIndex);
    void RestoreState(FrameGenEngineD3D11* engine, int& selectedProfileIndex);
    void ApplyProfileToEngine(const GameProfile& profile, FrameGenEngineD3D11* engine);

    bool m_autoSwitchEnabled = true;
    std::vector<DetectedProcess> m_lastScan;

    DWORD m_activeProcessId = 0;
    std::optional<std::string> m_activeProfileName;

    bool m_hasSavedState = false;
    int m_savedSelectedProfileIndex = 0;
    bool m_savedEnabled = false;
    FrameGenMultiplier m_savedMultiplier = FrameGenMultiplier::Off;
    FrameGenQualityMode m_savedQuality = FrameGenQualityMode::Balanced;
};

} // namespace lsalt
