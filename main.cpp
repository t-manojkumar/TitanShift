/*
 * TitanShift Native - High-Performance Bulk File Operations
 * Pure Win32 + Direct2D + Windows kernel APIs
 */
#include "TitanShift.h"
#include "FileEngine.h"
#include "MetricsEngine.h"
#include <commctrl.h>

// DPI_AWARENESS_CONTEXT is defined in modern <windef.h> via DECLARE_HANDLE.
// We just need the V2 constant if missing.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

// Use ANSI WinMain entry — works with both MSVC (default) and MinGW (default).
// Wide character support is unaffected because we use *W APIs throughout.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Try to enable Per-Monitor DPI v2 (Windows 10 1703+) — load dynamically
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = (PFN_SetProcessDpiAwarenessContext)
            GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware(); // fallback for older Windows
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    TitanShiftApp* app = new TitanShiftApp(hInstance);
    int result = app->Run(nCmdShow);
    delete app;

    CoUninitialize();
    return result;
}