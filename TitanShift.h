#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>
#include <memory>
#include <chrono>
#include <deque>

// ── Colours (Direct2D RGBA) ───────────────────────────────────────
namespace C {
    static const D2D1_COLOR_F BG        = {0.039f, 0.039f, 0.039f, 1.f}; // #0a0a0a
    static const D2D1_COLOR_F BG1       = {0.067f, 0.067f, 0.067f, 1.f}; // #111
    static const D2D1_COLOR_F BG2       = {0.094f, 0.094f, 0.094f, 1.f}; // #181818
    static const D2D1_COLOR_F BG3       = {0.133f, 0.133f, 0.133f, 1.f}; // #222
    static const D2D1_COLOR_F BORDER    = {0.165f, 0.165f, 0.165f, 1.f}; // #2a2a2a
    static const D2D1_COLOR_F BORDER2   = {0.200f, 0.200f, 0.200f, 1.f}; // #333
    static const D2D1_COLOR_F TEXT      = {0.831f, 0.831f, 0.831f, 1.f}; // #d4d4d4
    static const D2D1_COLOR_F TEXT_DIM  = {0.400f, 0.400f, 0.400f, 1.f}; // #666
    static const D2D1_COLOR_F ACCENT    = {0.910f, 1.000f, 0.278f, 1.f}; // #e8ff47
    static const D2D1_COLOR_F ACCENT_DK = {0.000f, 0.000f, 0.000f, 1.f}; // black on accent
    static const D2D1_COLOR_F DANGER    = {1.000f, 0.267f, 0.333f, 1.f}; // #ff4455
    static const D2D1_COLOR_F INFO      = {0.278f, 0.784f, 1.000f, 1.f}; // #47c8ff
    static const D2D1_COLOR_F SUCCESS   = {0.278f, 1.000f, 0.541f, 1.f}; // #47ff8a
    static const D2D1_COLOR_F WARN      = {1.000f, 0.702f, 0.000f, 1.f}; // #ffb300
    static const D2D1_COLOR_F TRANS     = {0.f,0.f,0.f,0.f};
}

// ── WM custom messages ────────────────────────────────────────────
#define WM_PROGRESS_UPDATE  (WM_USER + 100)
#define WM_METRICS_UPDATE   (WM_USER + 101)
#define WM_LOG_APPEND       (WM_USER + 102)
#define WM_OP_COMPLETE      (WM_USER + 103)
#define WM_OP_ERROR         (WM_USER + 104)
#define WM_SCAN_COMPLETE    (WM_USER + 105)

// ── Enums ─────────────────────────────────────────────────────────
enum class OpMode  { COPY, MOVE, RENAME, DELETE, DEDUPE, SYNC };
enum class OpState { IDLE, SCANNING, RUNNING, PAUSED, DONE, CANCELLED, ERROR };
enum class LogLevel{ INFO, SUCCESS, WARN, ERR };

// ── Structs ───────────────────────────────────────────────────────
struct FileEntry {
    std::wstring path;
    std::wstring name;
    ULONGLONG    size;
    FILETIME     modified;
    bool         isDir;
};

struct OpStats {
    std::atomic<ULONGLONG> totalBytes{0};
    std::atomic<ULONGLONG> doneBytes{0};
    std::atomic<ULONGLONG> totalFiles{0};
    std::atomic<ULONGLONG> doneFiles{0};
    std::atomic<ULONGLONG> skipped{0};
    std::atomic<ULONGLONG> errors{0};
    std::wstring           currentFile;
    std::mutex             fileMutex;
    ULONGLONG              startTick{0};
};

struct ProgressPayload {
    int          pct;          // 0-100
    ULONGLONG    doneBytes;
    ULONGLONG    totalBytes;
    ULONGLONG    doneFiles;
    ULONGLONG    totalFiles;
    ULONGLONG    bytesPerSec;  // throughput
    ULONGLONG    etaSecs;
    std::wstring currentFile;
    OpState      state;
};

struct LogEntry {
    LogLevel     level;
    std::wstring msg;
    std::wstring timestamp;
};

struct SystemMetrics {
    float cpuLoad;       // 0-100
    float memUsedPct;    // 0-100
    ULONGLONG memUsed;
    ULONGLONG memTotal;
    float diskReadMBs;   // MB/s
    float diskWriteMBs;
    float netSendMBs;
    float netRecvMBs;
    // History (60 samples)
    std::deque<float> cpuHistory;
    std::deque<float> diskReadHistory;
    std::deque<float> diskWriteHistory;
};

// ── Font IDs ──────────────────────────────────────────────────────
enum FontID {
    FONT_MONO_SM = 0,   // 9pt mono
    FONT_MONO_MD,       // 11pt mono
    FONT_MONO_LG,       // 14pt mono
    FONT_MONO_XL,       // 20pt mono bold
    FONT_UI_SM,         // 9pt ui
    FONT_UI_MD,         // 11pt ui
    FONT_UI_LG,         // 13pt ui bold
    FONT_COUNT
};

// ── Button IDs ────────────────────────────────────────────────────
enum BtnID {
    BTN_MODE_COPY=0, BTN_MODE_MOVE, BTN_MODE_RENAME,
    BTN_MODE_DELETE, BTN_MODE_DEDUPE, BTN_MODE_SYNC,
    BTN_ADD_FILES, BTN_ADD_FOLDER, BTN_CLEAR_SRC,
    BTN_PICK_DEST, BTN_RUN, BTN_CANCEL, BTN_PAUSE,
    BTN_CLEAR_LOG, BTN_OPEN_DEST,
    BTN_OPT_OVERWRITE, BTN_OPT_VERIFY, BTN_OPT_PRESERVE,
    BTN_OPT_THREADS,
    BTN_WIN_MIN, BTN_WIN_MAX, BTN_WIN_CLOSE,
    BTN_COUNT
};

struct Button {
    D2D1_RECT_F rect;
    std::wstring label;
    bool         enabled;
    bool         hovered;
    bool         pressed;
    bool         active;   // toggle state
    bool         danger;
    BtnID        id;
};

// ── Main application class ────────────────────────────────────────
class TitanShiftApp {
public:
    explicit TitanShiftApp(HINSTANCE hInst);
    ~TitanShiftApp();

    int Run(int nCmdShow);

    // Called from worker threads
    void PostProgress(const ProgressPayload& p);
    void PostLog(LogLevel lvl, const std::wstring& msg);
    void PostComplete();
    void PostError(const std::wstring& err);

    HWND GetHWnd() const { return m_hwnd; }

private:
    // Win32
    HINSTANCE  m_hInst;
    HWND       m_hwnd;
    bool       m_trackingMouse{false};
    POINT      m_dragOffset{};
    bool       m_dragging{false};

    // Direct2D / DirectWrite
    ID2D1Factory*           m_d2dFactory{nullptr};
    ID2D1HwndRenderTarget*  m_rt{nullptr};
    IDWriteFactory*         m_dwFactory{nullptr};
    IDWriteTextFormat*      m_fonts[FONT_COUNT]{};
    ID2D1SolidColorBrush*   m_brush{nullptr};

    // Layout
    D2D1_SIZE_F   m_size{};
    float         m_dpi{96.f};
    float         m_scale{1.f};

    // UI state
    OpMode        m_mode{OpMode::COPY};
    OpState       m_opState{OpState::IDLE};
    std::vector<FileEntry>  m_sources;
    std::wstring  m_dest;
    std::wstring  m_renamePat;
    bool          m_optOverwrite{true};
    bool          m_optVerify{false};
    bool          m_optPreserve{true};
    int           m_threadCount{4};
    int           m_srcScrollOffset{0};
    int           m_logScrollOffset{0};

    // Buttons
    Button        m_buttons[BTN_COUNT];
    int           m_hoveredBtn{-1};

    // Progress
    ProgressPayload m_lastProgress{};
    std::mutex      m_progressMutex;

    // Logs
    std::vector<LogEntry>  m_logs;
    std::mutex             m_logMutex;
    static const int       MAX_LOGS = 500;

    // Metrics
    SystemMetrics  m_metrics{};
    std::mutex     m_metricsMutex;

    // Rename input state
    std::wstring   m_renameInput;
    bool           m_renameInputActive{false};
    int            m_renameCaret{0};

    // Worker
    std::unique_ptr<class FileEngine>    m_engine;
    std::unique_ptr<class MetricsEngine> m_metricsEngine;
    std::thread    m_workerThread;
    std::thread    m_metricsThread;

    // Win32 callbacks
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);

    // Init / Cleanup
    bool InitWindow();
    bool InitD2D();
    void CreateFonts();
    void CreateButtons();
    void Cleanup();

    // Render
    void OnPaint();
    void DrawTitleBar();
    void DrawLeftPanel();
    void DrawRightPanel();
    void DrawModeBar(float x, float y, float w);
    void DrawSourcePanel(float x, float y, float w, float h);
    void DrawDestPanel(float x, float y, float w);
    void DrawOptionsPanel(float x, float y, float w);
    void DrawRunControls(float x, float y, float w);
    void DrawLogPanel(float x, float y, float w, float h);
    void DrawProgressCard(float x, float y, float w);
    void DrawThroughputGraph(float x, float y, float w, float h);
    void DrawMetricRing(float cx, float cy, float r, float pct, D2D1_COLOR_F color, const wchar_t* label, const wchar_t* val);
    void DrawDiskMetric(float x, float y, float w, float h);
    void DrawButton(const Button& btn);
    void DrawSectionLabel(const wchar_t* text, float x, float y, float w);
    void DrawRect(D2D1_RECT_F r, D2D1_COLOR_F fill, D2D1_COLOR_F stroke={0,0,0,0}, float sw=0.f);
    void DrawRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F fill, D2D1_COLOR_F stroke={0,0,0,0}, float sw=0.f);
    void DrawText(const wchar_t* t, D2D1_RECT_F r, FontID f, D2D1_COLOR_F col, DWRITE_TEXT_ALIGNMENT ha=DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT va=DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    void DrawLine(float x1,float y1,float x2,float y2, D2D1_COLOR_F col, float w=0.5f);
    void SetBrushColor(D2D1_COLOR_F c);

    // Layout helpers
    float S(float logical) const { return logical * m_scale; }
    D2D1_RECT_F R(float x,float y,float w,float h) const { return {x,y,x+w,y+h}; }

    // Input handlers
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnMouseWheel(int delta, int x, int y);
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM vk);
    void OnResize(int w, int h);
    void OnDpiChanged(int dpi, RECT* suggestedRect);

    // Button actions
    void OnButtonClick(BtnID id);
    void PickSourceFiles(bool folders);
    void PickDestination();
    void StartOperation();
    void CancelOperation();
    void PauseOperation();

    // Utility
    std::wstring FormatBytes(ULONGLONG bytes);
    std::wstring FormatSpeed(ULONGLONG bytesPerSec);
    std::wstring FormatETA(ULONGLONG secs);
    std::wstring FormatTime();
    void         UpdateButtonLayout();
    Button*      HitTestButtons(int x, int y);
    void         RecreateTarget();

    // Scroll
    int  GetMaxSrcScroll() const;
    int  GetMaxLogScroll() const;
};
