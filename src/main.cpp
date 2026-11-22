#include <windows.h>
#include <string>
#include "App.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    const bool selfTest = std::wstring(lpCmdLine ? lpCmdLine : L"").find(L"--selftest") != std::wstring::npos;

    lsalt::App app;
    if (!app.Initialize(hInstance)) {
        MessageBoxW(nullptr, L"Failed to initialize LSAlt. Check the log file in "
                              L"%LOCALAPPDATA%\\LSAlt\\logs for details.",
                    L"LSAlt - Startup Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    const int exitCode = selfTest ? app.RunSelfTest() : app.Run();
    app.Shutdown();
    return exitCode;
}
