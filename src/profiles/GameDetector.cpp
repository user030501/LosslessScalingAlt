#include "GameDetector.h"
#include "../core/Logger.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <unordered_set>

namespace lsalt {

namespace {
std::string NarrowFromWide(const std::wstring& w) {
    if (w.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

// Real top-level, user-facing window ownership check: visible, not a
// tool window (those are palettes/utility windows, e.g. a docked panel --
// not something a user would call "a running game"), and not owned by
// another window (an owned window is a dialog/popup of some other
// top-level window, not itself the thing to track).
BOOL CALLBACK CollectWindowOwningPids(HWND hwnd, LPARAM lParam) {
    auto* pids = reinterpret_cast<std::unordered_set<DWORD>*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    // A window with no title is almost always a background/helper window,
    // not a user-facing application window.
    wchar_t title[2] = {};
    if (GetWindowTextW(hwnd, title, 2) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) pids->insert(pid);
    return TRUE;
}

std::string QueryProcessExecutablePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t pathBuf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::string result;
    if (QueryFullProcessImageNameW(process, 0, pathBuf, &size)) {
        result = NarrowFromWide(std::wstring(pathBuf, size));
    }
    CloseHandle(process);
    return result;
}
} // namespace

std::vector<DetectedProcess> GameDetector::Scan() const {
    std::vector<DetectedProcess> result;

    std::unordered_set<DWORD> windowOwningPids;
    EnumWindows(CollectWindowOwningPids, reinterpret_cast<LPARAM>(&windowOwningPids));
    if (windowOwningPids.empty()) return result;

    DWORD foregroundPid = 0;
    if (HWND fg = GetForegroundWindow()) {
        GetWindowThreadProcessId(fg, &foregroundPid);
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Logger::Instance().Warn("GameDetector", "CreateToolhelp32Snapshot failed; game detection unavailable this scan.");
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (windowOwningPids.find(entry.th32ProcessID) == windowOwningPids.end()) continue;

            DetectedProcess proc;
            proc.processId = entry.th32ProcessID;
            proc.executableName = NarrowFromWide(entry.szExeFile);
            proc.executablePath = QueryProcessExecutablePath(entry.th32ProcessID);
            proc.isForegroundWindow = (entry.th32ProcessID == foregroundPid);
            result.push_back(std::move(proc));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    return result;
}

} // namespace lsalt
