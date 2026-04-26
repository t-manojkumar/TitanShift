#pragma once

// Define BEFORE windows.h to prevent Windows defining min/max macros
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00  // Windows 10
#define WINVER       0x0A00

#include <windows.h>

// Some Windows headers define DELETE and ERROR as macros — undefine them so
// our enum class members can use those names.
#ifdef DELETE
#undef DELETE
#endif
#ifdef ERROR
#undef ERROR
#endif

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
    static const D2D1_COLOR_F BG        = {0.039f, 0.039f, 0.039f, 1.f};
    static const D2D1_COLOR_F BG1       = {0.067f, 0.067f, 0.067f, 1.f};
    static const D2D1_COLOR_F BG2       = {0.094f, 0.094f, 0.094f, 1.f};
    static const D2D1_COLOR_F BG3       = {0.133f, 0.133f, 0.133f, 1.f};
    static const D2D1_COLOR_F BORDER    = {0.165f, 0.165f, 0.165f, 1.f};
    static const D2D1_COLOR_F BORDER2   = {0.200f, 0.200f, 0.200f, 1.f};
    static const D2D1_COLOR_F TEXT      = {0.831f, 0.831f, 0.831f, 1.f};
    static const D2D1_COLOR_F TEXT_DIM  = {0.400f, 0.400f, 0.400f, 1.f};
    static const D2D1_COLOR_F ACCENT    = {0.910f, 1.000f, 0.278f, 1.f};
    static const D2D1_COLOR_F ACCENT_DK = {0.000f, 0.000f, 0.000f, 1.f};
    static const D2D1_COLOR_F DANGER    = {1.000f, 0.267f, 0.333f, 1.f};
    static const D2D1_COLOR_F INFO      = {0.278f, 0.784f, 1.000f, 1.f};
    static const D2D1_COLOR_F SUCCESS   = {0.278f, 1.000f, 0.541f, 1.f};
    static const D2D1_COLOR_F WARN      = {1.000f, 0.702f, 0.000f, 1.f};
    static const D2D1_COLOR_F TRANS     = {0.f,0.f,0.f,0.f};
}

#define WM_PROGRESS_UPDATE  (WM_USER + 100)
#define WM_METRICS_UPDATE   (WM_USER + 101)
#define WM_LOG_APPEND       (WM_USER + 102)
#define WM_OP_COMPLETE      (WM_USER + 103)
#define WM_OP_ERROR         (WM_USER + 104)

// ── Enums ─────────────────────────────────────────────────────────
// Renamed DELETE→DEL, ERROR→ERR_STATE to avoid Windows macro conflicts
enum class OpMode  { COPY, MOVE, RENAME, DEL, DEDUPE, SYNC };
enum class OpState { IDLE, SCANNING, RUNNING, PAUSED, DONE, CANCELLED, ERR_STATE };
enum class LogLevel{ INFO, SUCCESS, WARN, ERR };

// ── Structs ───────────────────────────────────────────────────────
struct FileEntry {
    std::wstring path;
    std::wstring name;
    ULONGLONG    size{0};
    FILETIME     modified{};
    bool         isDir{false};
};

// OpStats has atomic + mutex — non-copyable, non-movable.
// Provide explicit reset() instead of relying on assignment.
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

    void reset() {
        totalBytes = 0; doneBytes = 0;
        totalFiles = 0; doneFiles = 0;
        skipped = 0;    errors = 0;
        startTick = 0;
        std::lock_guard<std::mutex> lk(fileMutex);
        currentFile.clear();
    }

    OpStats() = default;
    OpStats(const OpStats&) = delete;
    OpStats& operator=(const OpStats&) = delete;
};

struct ProgressPayload {
    int          pct{0};
    ULONGLONG    doneBytes{0};
    ULONGLONG    totalBytes{0};
    ULONGLONG    doneFiles{0};
    ULONGLONG    totalFiles{0};
    ULONGLONG    bytesPerSec{0};
    ULONGLONG    etaSecs{0};
    std::wstring currentFile;
    OpState      state{OpState::IDLE};
};

struct LogEntry {
    LogLevel     level;
    std::wstring msg;
    std::wstring timestamp;
};

struct SystemMetrics {
    float cpuLoad{0};
    float memUsedPct{0};
    ULONGLONG memUsed{0};
    ULONGLONG memTotal{1};
    float diskReadMBs{0};
    float diskWriteMBs{0};
    float netSendMBs{0};
    float netRecvMBs{0};
    std::deque<float> cpuHistory;
    std::deque<float> diskReadHistory;
    std::deque<float> diskWriteHistory;
};

enum FontID {
    FONT_MONO_SM = 0, FONT_MONO_MD, FONT_MONO_LG, FONT_MONO_XL,
    FONT_UI_SM, FONT_UI_MD, FONT_UI_LG, FONT_COUNT
};

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
    D2D1_RECT_F  rect{};
    std::wstring label;
    bool         enabled{true};
    bool         hovered{false};
    bool         pressed{false};
    bool         active{false};
    bool         danger{false};
    BtnID        id{BTN_COUNT};
};

class TitanShiftApp {
public:
    explicit TitanShiftApp(HINSTANCE hInst);
    ~TitanShiftApp();

    int Run(int nCmdShow);

    void PostProgress(const ProgressPayload& p);
    void PostLog(LogLevel lvl, const std::wstring& msg);
    void PostComplete();
    void PostError(const std::wstring& err);

    HWND GetHWnd() const { return m_hwnd; }

private:
    HINSTANCE  m_hInst;
    HWND       m_hwnd{nullptr};
    bool       m_trackingMouse{false};

    ID2D1Factory*           m_d2dFactory{nullptr};
    ID2D1HwndRenderTarget*  m_rt{nullptr};
    IDWriteFactory*         m_dwFactory{nullptr};
    IDWriteTextFormat*      m_fonts[FONT_COUNT]{};
    ID2D1SolidColorBrush*   m_brush{nullptr};

    D2D1_SIZE_F   m_size{};
    float         m_dpi{96.f};
    float         m_scale{1.f};

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

    Button        m_buttons[BTN_COUNT];

    ProgressPayload m_lastProgress{};
    std::mutex      m_progressMutex;

    std::vector<LogEntry>  m_logs;
    std::mutex             m_logMutex;
    static const int       MAX_LOGS = 500;

    SystemMetrics  m_metrics{};
    std::mutex     m_metricsMutex;

    std::wstring   m_renameInput;
    bool           m_renameInputActive{false};

    std::unique_ptr<class FileEngine>    m_engine;
    std::unique_ptr<class MetricsEngine> m_metricsEngine;
    std::thread    m_workerThread;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);

    bool InitWindow();
    bool InitD2D();
    void CreateFonts();
    void Cleanup();

    void OnPaint();
    void DrawTitleBar();
    void DrawLeftPanel();
    void DrawRightPanel();
    void DrawProgressCard(float x, float y, float w);
    void DrawThroughputGraph(float x, float y, float w, float h);
    void DrawMetricRing(float cx, float cy, float r, float pct, D2D1_COLOR_F color);
    void DrawDiskMetric(float x, float y, float w, float h);
    void DrawButton(const Button& btn);
    void DrawSectionLabel(const wchar_t* text, float x, float y, float w);
    void DrawRect(D2D1_RECT_F r, D2D1_COLOR_F fill, D2D1_COLOR_F stroke={0,0,0,0}, float sw=0.f);
    void DrawRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F fill, D2D1_COLOR_F stroke={0,0,0,0}, float sw=0.f);
    void DrawTextEx(const wchar_t* t, D2D1_RECT_F r, FontID f, D2D1_COLOR_F col,
                    DWRITE_TEXT_ALIGNMENT ha=DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT va=DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    void DrawLineSeg(float x1,float y1,float x2,float y2, D2D1_COLOR_F col, float w=0.5f);
    void SetBrushColor(D2D1_COLOR_F c);

    float S(float logical) const { return logical * m_scale; }
    D2D1_RECT_F R(float x,float y,float w,float h) const { return {x,y,x+w,y+h}; }

    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnMouseWheel(int delta, int x, int y);
    void OnChar(wchar_t c);
    void OnKeyDown(WPARAM vk);
    void OnResize(int w, int h);
    void OnDpiChanged(int dpi, RECT* suggestedRect);

    void OnButtonClick(BtnID id);
    void PickSourceFiles(bool folders);
    void PickDestination();
    void StartOperation();
    void CancelOperation();
    void PauseOperation();

    static std::wstring FormatBytes(ULONGLONG bytes);
    static std::wstring FormatETA(ULONGLONG secs);
    static std::wstring FormatTime();

    void         UpdateButtonLayout();
    Button*      HitTestButtons(int x, int y);
    void         RecreateTarget();
};
