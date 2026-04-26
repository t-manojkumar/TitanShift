/*
 * TitanShift Native - High-Performance Bulk File Operations
 * Pure Win32 + Direct2D + Windows kernel APIs
 * No frameworks. No overhead. Direct OS calls only.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <winternl.h>
#include <psapi.h>
#include <pdh.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <filesystem>
#include <chrono>
#include <functional>
#include <memory>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <fstream>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ntdll.lib")

#include "TitanShift.h"
#include "FileEngine.h"
#include "MetricsEngine.h"



// Global application instance
TitanShiftApp* g_App = nullptr;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // Enable high-DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialize COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Initialize common controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Create and run application
    g_App = new TitanShiftApp(hInstance);
    int result = g_App->Run(nCmdShow);

    delete g_App;
    CoUninitialize();
    return result;
}
