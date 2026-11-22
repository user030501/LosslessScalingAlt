#include <windows.h>
#include "App.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    lsalt::App app;
    if (!app.Initialize(hInstance)) {
        MessageBoxW(nullptr, L"Failed to initialize LSAlt. Check the log file in "
                              L"%LOCALAPPDATA%\\LSAlt\\logs for details.",
                    L"LSAlt - Startup Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    const int exitCode = app.Run();
    app.Shutdown();
    return exitCode;
}
