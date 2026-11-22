#include "ProfileDiagnostics.h"
#include "ProfileManager.h"
#include "GameProfile.h"
#include "GameDetector.h"
#include "GameSessionController.h"
#include "PerformanceHistory.h"
#include "../framegen/FrameGenEngine.h"
#include "../core/Logger.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace lsalt {

namespace {
std::string CurrentExecutableName() {
    wchar_t pathBuf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
    std::wstring path(pathBuf);
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring base = (slash != std::wstring::npos) ? path.substr(slash + 1) : path;
    return std::string(base.begin(), base.end());
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

constexpr const char* kTestProfileName = "__LSAlt_SelfTest_Temp_Profile__";
constexpr const char* kTestFakeExecutable = "zzz_lsalt_selftest_fake_game.exe";
} // namespace

std::vector<DiagnosticResult> ProfileDiagnostics::RunAll(ProfileManager& profiles, FrameGenEngineD3D11& engine) {
    std::vector<DiagnosticResult> results;

    // 15. Automatic game detection: this process itself is a real,
    // window-owning running process -- GameDetector should find it
    // without needing a second, separate game running.
    {
        DiagnosticResult r{"15. Automatic Game Detection (self)", false, ""};
        const std::string selfExe = CurrentExecutableName();
        GameDetector detector;
        const auto scan = detector.Scan();
        const auto it = std::find_if(scan.begin(), scan.end(), [&](const DetectedProcess& p) {
            return IEquals(p.executableName, selfExe);
        });
        r.passed = it != scan.end();
        r.detail = r.passed
            ? ("Found self (" + selfExe + ") among " + std::to_string(scan.size()) + " window-owning process(es).")
            : ("Did not find self (" + selfExe + ") among " + std::to_string(scan.size()) + " window-owning process(es).");
        results.push_back(r);
    }

    // 16. Profile CRUD against the real ProfileManager, using an
    // obviously test-only name that is always removed again before this
    // function returns.
    {
        DiagnosticResult r{"16. Profile CRUD", false, ""};
        GameProfile p;
        p.displayName = kTestProfileName;
        p.executableName = "zzz_lsalt_selftest_crud.exe";
        p.frameGenQuality = FrameGenQualityMode::Quality;

        profiles.AddOrUpdate(p);
        const bool created = profiles.Find(kTestProfileName) != nullptr;

        p.frameGenQuality = FrameGenQualityMode::Performance;
        profiles.AddOrUpdate(p);
        GameProfile* updated = profiles.Find(kTestProfileName);
        const bool updatedOk = updated && updated->frameGenQuality == FrameGenQualityMode::Performance;

        const bool foundByExe = profiles.FindByExecutable(p.executableName) != nullptr;
        const int idx = profiles.IndexOf(kTestProfileName);
        const bool indexOk = idx >= 0 && profiles.AtIndex(idx) && profiles.AtIndex(idx)->displayName == kTestProfileName;

        const bool removed = profiles.Remove(kTestProfileName);
        const bool goneAfterRemove = profiles.Find(kTestProfileName) == nullptr;

        r.passed = created && updatedOk && foundByExe && indexOk && removed && goneAfterRemove;
        r.detail = "create=" + std::to_string(created) + " update=" + std::to_string(updatedOk) +
                   " findByExe=" + std::to_string(foundByExe) + " index=" + std::to_string(indexOk) +
                   " remove=" + std::to_string(removed) + " goneAfter=" + std::to_string(goneAfterRemove);
        results.push_back(r);
    }

    // 17. Automatic profile switching + restoration, against a synthetic
    // process scan (no real second game needed -- see GameSessionController's
    // class comment on why it takes a plain scan rather than owning a
    // GameDetector). The real FrameGenEngine is used but its pre-test
    // state is saved/restored independently as a safety net.
    {
        DiagnosticResult r{"17. Automatic Profile Switching + Restoration", false, ""};

        const bool savedEnabled = engine.IsEnabled();
        const FrameGenMultiplier savedMultiplier = engine.Multiplier();
        const FrameGenQualityMode savedQuality = engine.QualityMode();

        GameProfile p;
        p.displayName = kTestProfileName;
        p.executableName = kTestFakeExecutable;
        p.frameGenEnabled = true;
        p.frameGenMultiplier = FrameGenMultiplier::X2;
        p.frameGenQuality = FrameGenQualityMode::Quality;
        profiles.AddOrUpdate(p);

        GameSessionController controller;
        int selectedIndex = 0;
        std::string status;

        std::vector<DetectedProcess> scanWithGame;
        DetectedProcess fakeProc;
        fakeProc.processId = 999999; // not a real PID; GameSessionController only compares it for presence/absence
        fakeProc.executableName = kTestFakeExecutable;
        fakeProc.isForegroundWindow = true;
        scanWithGame.push_back(fakeProc);

        const bool appliedChange = controller.Update(scanWithGame, profiles, &engine, selectedIndex, status, false);
        const bool appliedCorrectly = engine.QualityMode() == FrameGenQualityMode::Quality &&
                                       engine.Multiplier() == FrameGenMultiplier::X2 && engine.IsEnabled() &&
                                       selectedIndex == profiles.IndexOf(kTestProfileName) && controller.HasActiveGame();

        const std::vector<DetectedProcess> scanWithoutGame; // the "game" has closed
        const bool restoredChange = controller.Update(scanWithoutGame, profiles, &engine, selectedIndex, status, false);
        const bool restoredCorrectly = engine.QualityMode() == savedQuality && engine.Multiplier() == savedMultiplier &&
                                        engine.IsEnabled() == savedEnabled && !controller.HasActiveGame();

        // Integration check: while the user's own Live Engine demo is
        // active (liveDemoActive=true), automatic switching must still
        // apply quality/multiplier but must NOT touch engine->SetEnabled
        // -- that flag is the same one the Live Engine checkbox drives,
        // and a detected game silently killing a demo the user turned on
        // by hand would be a real, surprising interference (see
        // GameSessionController::Update's class comment).
        engine.SetEnabled(true); // simulate the user having turned Live Engine on
        const bool enabledBeforeLiveTest = engine.IsEnabled();
        const bool appliedWithLiveDemo = controller.Update(scanWithGame, profiles, &engine, selectedIndex, status, true);
        const bool enabledUntouchedDuringApply = engine.IsEnabled() == enabledBeforeLiveTest;
        const bool restoredWithLiveDemo = controller.Update(scanWithoutGame, profiles, &engine, selectedIndex, status, true);
        const bool enabledUntouchedDuringRestore = engine.IsEnabled() == enabledBeforeLiveTest;
        engine.SetEnabled(savedEnabled); // put it back exactly as found regardless of the simulated toggle above

        profiles.Remove(kTestProfileName);

        r.passed = appliedChange && appliedCorrectly && restoredChange && restoredCorrectly && appliedWithLiveDemo &&
                   enabledUntouchedDuringApply && restoredWithLiveDemo && enabledUntouchedDuringRestore;
        r.detail = "applied=" + std::to_string(appliedChange) + "/" + std::to_string(appliedCorrectly) +
                   " restored=" + std::to_string(restoredChange) + "/" + std::to_string(restoredCorrectly) +
                   " liveDemoGuard(apply)=" + std::to_string(enabledUntouchedDuringApply) +
                   " liveDemoGuard(restore)=" + std::to_string(enabledUntouchedDuringRestore) +
                   " -- note: this only changes this app's own Frame Generation Engine settings, never the "
                   "detected game's actual rendering (no Capture Engine yet).";
        results.push_back(r);
    }

    // 18. Performance history accounting: real BeginSession/RecordSample/
    // EndSession math against known inputs. Appends one clearly-labeled
    // "(Self-Test)" record to the real on-disk session history rather than
    // silently editing history files behind the scenes.
    {
        DiagnosticResult r{"18. Performance History Accounting", false, ""};
        PerformanceHistory history;
        const size_t countBefore = history.Records().size();

        history.BeginSession("(Self-Test)");
        history.RecordSample(60.0, 120.0, 8.0, 100 * 1024 * 1024, 50.0, true, 0);
        history.RecordSample(60.0, 120.0, 8.0, 300 * 1024 * 1024, 70.0, true, 2);
        Sleep(600); // EndSession only persists sessions longer than 0.5s -- a deliberate anti-clutter gate
        history.EndSession();

        const bool appended = history.Records().size() == countBefore + 1;
        bool mathOk = false;
        std::string detail = "did not append a record";
        if (appended) {
            const auto& last = history.Records().back();
            mathOk = last.gameName == "(Self-Test)" && std::abs(last.avgSourceFps - 60.0) < 0.01 &&
                     std::abs(last.avgDisplayedFps - 120.0) < 0.01 && last.peakVramUsedBytes == 300 * 1024 * 1024 &&
                     std::abs(last.avgGpuUtilPercent - 60.0) < 0.01 && last.droppedFrames == 2 &&
                     last.durationSeconds > 0.5;
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "avgSourceFps=%.2f avgDisplayedFps=%.2f peakVramMB=%.0f avgGpuUtil=%.2f dropped=%llu duration=%.2fs",
                     last.avgSourceFps, last.avgDisplayedFps, last.peakVramUsedBytes / (1024.0 * 1024.0),
                     last.avgGpuUtilPercent, static_cast<unsigned long long>(last.droppedFrames), last.durationSeconds);
            detail = buf;
        }
        r.passed = appended && mathOk;
        r.detail = detail;
        results.push_back(r);
    }

    // 19. CSV export: write to a real temp file, verify it exists with a
    // sane header, then clean up the temp file.
    {
        DiagnosticResult r{"19. CSV Export", false, ""};
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        const std::wstring csvPath = std::wstring(tempDir) + L"lsalt_selftest_export.csv";

        PerformanceHistory history; // reload: picks up the (Self-Test) record from test 18, which is fine/expected
        const bool exported = history.ExportCsv(csvPath);

        bool headerOk = false;
        std::ifstream check(csvPath);
        std::string firstLine;
        if (check.is_open()) {
            std::getline(check, firstLine);
            headerOk = firstLine.find("game_name") != std::string::npos;
        }
        check.close();
        DeleteFileW(csvPath.c_str());

        r.passed = exported && headerOk;
        r.detail = exported ? ("wrote CSV with header: " + firstLine) : "ExportCsv returned false";
        results.push_back(r);
    }

    // 20. Cross-instance persistence (simulates "survives an app
    // restart"): a profile added through one ProfileManager instance must
    // be visible to a second, independently-constructed instance reading
    // the same on-disk file.
    {
        DiagnosticResult r{"20. Profile Persistence Across Restart", false, ""};
        GameProfile p;
        p.displayName = kTestProfileName;
        p.executableName = "zzz_lsalt_selftest_persist.exe";
        profiles.AddOrUpdate(p);

        ProfileManager reloaded; // fresh instance, loads the same profiles.json from disk
        GameProfile* seenByReload = reloaded.Find(kTestProfileName);
        const bool visibleAfterReload = seenByReload != nullptr && seenByReload->executableName == p.executableName;

        profiles.Remove(kTestProfileName);
        ProfileManager reloadedAgain;
        const bool goneAfterReload = reloadedAgain.Find(kTestProfileName) == nullptr;

        r.passed = visibleAfterReload && goneAfterReload;
        r.detail = "visibleAfterReload=" + std::to_string(visibleAfterReload) +
                   " goneAfterReload=" + std::to_string(goneAfterReload);
        results.push_back(r);
    }

    return results;
}

} // namespace lsalt
