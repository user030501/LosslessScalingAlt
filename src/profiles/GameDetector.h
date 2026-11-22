#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lsalt {

// A single currently-running process this app considers plausibly
// "game-like": it owns at least one visible, non-tool top-level window
// (see GameDetector::Scan for the exact rule). This is a deliberately
// simple, real heuristic to avoid flagging background services/utilities
// as games, per the brief's "avoid detecting unrelated background
// applications" requirement -- it is not foolproof (plenty of ordinary
// desktop apps also have a visible top-level window), which is why
// matching against a known profile's executableName (ProfileManager::
// FindByExecutable) is still what actually decides whether to act on it.
struct DetectedProcess {
    DWORD processId = 0;
    std::string executableName; // e.g. "VALORANT-Win64-Shipping.exe"
    std::string executablePath; // full path, when queryable (may be empty)
    bool isForegroundWindow = false; // true if this is the current foreground app
};

// Real, read-only Win32 process enumeration (CreateToolhelp32Snapshot) +
// top-level window ownership check (EnumWindows) and foreground-window
// query (GetForegroundWindow). This only ever *observes* already-running
// processes through ordinary, unprivileged OS queries -- the same kind of
// information Task Manager shows -- and never reads another process'
// memory, injects code, suspends/terminates anything, or touches game
// files. See GameSessionController for what happens with the result.
class GameDetector {
public:
    // Re-scans running processes. Call periodically (e.g. every 1-2
    // seconds from a timer), not every frame -- process enumeration has
    // real (if modest) CPU cost, and "is a game running" does not change
    // at frame rate.
    std::vector<DetectedProcess> Scan() const;
};

} // namespace lsalt
