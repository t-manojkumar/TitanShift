#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <math.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "../include/TitanShift.h"
#include "../include/FileEngine.h"
#include "../include/MetricsEngine.h"

static const wchar_t* CLASS_NAME = L"TitanShiftWnd";

// ─────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────
TitanShiftApp::TitanShiftApp(HINSTANCE hInst) : m_hInst(hInst) {
    // Zero init buttons
    for (int i = 0; i < BTN_COUNT; i++) {
        m_buttons[i].id      = (BtnID)i;
        m_buttons[i].enabled = true;
        m_buttons[i].active  = false;
    }
    m_buttons[BTN_MODE_COPY].active  = true;
    m_buttons[BTN_OPT_OVERWRITE].active = true;
    m_buttons[BTN_OPT_PRESERVE].active  = true;
}

TitanShiftApp::~TitanShiftApp() { Cleanup(); }

// ─────────────────────────────────────────────────────────────────
// Run
// ─────────────────────────────────────────────────────────────────
int TitanShiftApp::Run(int nCmdShow) {
    if (!InitWindow()) return 1;
    if (!InitD2D())    return 1;

    CreateFonts();
    UpdateButtonLayout();

    // Start metrics engine
    m_metricsEngine = std::make_unique<MetricsEngine>(m_hwnd);
    m_metricsEngine->Start();

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

// ─────────────────────────────────────────────────────────────────
// Window init
// ─────────────────────────────────────────────────────────────────
bool TitanShiftApp::InitWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) return false;

    // Get DPI for primary monitor
    HDC hdc = GetDC(nullptr);
    m_dpi   = (float)GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    m_scale = m_dpi / 96.f;

    int W = (int)(1200 * m_scale);
    int H = (int)(740  * m_scale);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        CLASS_NAME, L"TitanShift",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN) - W) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - H) / 2,
        W, H, nullptr, nullptr, m_hInst, this
    );
    return m_hwnd != nullptr;
}

// ─────────────────────────────────────────────────────────────────
// Direct2D init
// ─────────────────────────────────────────────────────────────────
bool TitanShiftApp::InitD2D() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwFactory));
    if (FAILED(hr)) return false;

    RecreateTarget();
    return true;
}

void TitanShiftApp::RecreateTarget() {
    if (m_rt) { m_rt->Release(); m_rt = nullptr; }
    if (m_brush) { m_brush->Release(); m_brush = nullptr; }

    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = {(UINT32)(rc.right - rc.left), (UINT32)(rc.bottom - rc.top)};

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        m_dpi, m_dpi
    );
    D2D1_HWND_RENDER_TARGET_PROPERTIES htp = D2D1::HwndRenderTargetProperties(m_hwnd, size);

    m_d2dFactory->CreateHwndRenderTarget(rtp, htp, &m_rt);
    if (m_rt) m_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1), &m_brush);
}

// ─────────────────────────────────────────────────────────────────
// Fonts
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::CreateFonts() {
    if (!m_dwFactory) return;
    struct FDef { float pt; bool bold; bool mono; };
    FDef defs[FONT_COUNT] = {
        {9,  false, true },  // FONT_MONO_SM
        {11, false, true },  // FONT_MONO_MD
        {14, false, true },  // FONT_MONO_LG
        {20, true,  true },  // FONT_MONO_XL
        {9,  false, false},  // FONT_UI_SM
        {11, false, false},  // FONT_UI_MD
        {13, true,  false},  // FONT_UI_LG
    };
    for (int i = 0; i < FONT_COUNT; i++) {
        const wchar_t* face = defs[i].mono ? L"Consolas" : L"Segoe UI";
        DWRITE_FONT_WEIGHT wt = defs[i].bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR;
        m_dwFactory->CreateTextFormat(face, nullptr, wt,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            defs[i].pt * m_scale, L"en-US", &m_fonts[i]);
        if (m_fonts[i]) {
            m_fonts[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}

void TitanShiftApp::Cleanup() {
    if (m_metricsEngine) m_metricsEngine->Stop();
    if (m_workerThread.joinable()) m_workerThread.join();
    if (m_metricsThread.joinable()) m_metricsThread.join();

    for (auto& f : m_fonts) if (f) { f->Release(); f = nullptr; }
    if (m_brush)     { m_brush->Release();     m_brush     = nullptr; }
    if (m_rt)        { m_rt->Release();         m_rt        = nullptr; }
    if (m_dwFactory) { m_dwFactory->Release();  m_dwFactory = nullptr; }
    if (m_d2dFactory){ m_d2dFactory->Release(); m_d2dFactory= nullptr; }
}

// ─────────────────────────────────────────────────────────────────
// WndProc
// ─────────────────────────────────────────────────────────────────
LRESULT CALLBACK TitanShiftApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TitanShiftApp* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        app = reinterpret_cast<TitanShiftApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        app->m_hwnd = hwnd;
    } else {
        app = reinterpret_cast<TitanShiftApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (app) return app->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────
// HandleMessage
// ─────────────────────────────────────────────────────────────────
LRESULT TitanShiftApp::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        OnPaint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        OnResize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wp), reinterpret_cast<RECT*>(lp));
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_CHAR:
        OnChar((wchar_t)wp);
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wp);
        return 0;

    // Custom messages from worker threads
    case WM_METRICS_UPDATE: {
        if (m_metricsEngine) {
            std::lock_guard<std::mutex> lk(m_metricsMutex);
            m_metrics = m_metricsEngine->GetMetrics();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_PROGRESS_UPDATE:
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_LOG_APPEND:
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_OP_COMPLETE: {
        m_opState = OpState::DONE;
        m_lastProgress.state = OpState::DONE;
        m_lastProgress.pct   = 100;
        PostLog(LogLevel::SUCCESS, L"Operation completed successfully.");
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_OP_ERROR: {
        m_opState = OpState::ERROR;
        m_lastProgress.state = OpState::ERROR;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    // Hit-test for custom titlebar (allows resize + move)
    case WM_NCHITTEST: {
        LRESULT def = DefWindowProcW(m_hwnd, msg, wp, lp);
        if (def == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(m_hwnd, &pt);
            float tb = S(32.f);
            if (pt.y < tb) return HTCAPTION;
        }
        return def;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = { (LONG)(800 * m_scale), (LONG)(600 * m_scale) };
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // prevent flicker

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────
// Resize / DPI
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::OnResize(int w, int h) {
    if (m_rt) m_rt->Resize({(UINT32)w, (UINT32)h});
    m_size = {(float)w, (float)h};
    UpdateButtonLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TitanShiftApp::OnDpiChanged(int dpi, RECT* r) {
    m_dpi   = (float)dpi;
    m_scale = m_dpi / 96.f;
    SetWindowPos(m_hwnd, nullptr, r->left, r->top,
        r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
    RecreateTarget();
    for (auto& f : m_fonts) if (f) { f->Release(); f = nullptr; }
    CreateFonts();
    UpdateButtonLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────
// PostProgress (called from worker thread)
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::PostProgress(const ProgressPayload& p) {
    {
        std::lock_guard<std::mutex> lk(m_progressMutex);
        m_lastProgress = p;
        m_opState      = p.state;
    }
    PostMessageW(m_hwnd, WM_PROGRESS_UPDATE, 0, 0);
}

void TitanShiftApp::PostLog(LogLevel lvl, const std::wstring& msg) {
    LogEntry e;
    e.level = lvl;
    e.msg   = msg;
    e.timestamp = FormatTime();
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_logs.push_back(e);
        if ((int)m_logs.size() > MAX_LOGS) m_logs.erase(m_logs.begin());
    }
    PostMessageW(m_hwnd, WM_LOG_APPEND, 0, 0);
}

void TitanShiftApp::PostComplete() { PostMessageW(m_hwnd, WM_OP_COMPLETE, 0, 0); }
void TitanShiftApp::PostError(const std::wstring&) { PostMessageW(m_hwnd, WM_OP_ERROR, 0, 0); }

// ─────────────────────────────────────────────────────────────────
// Button layout
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::UpdateButtonLayout() {
    RECT rc; GetClientRect(m_hwnd, &rc);
    float W = (float)(rc.right - rc.left);
    float H = (float)(rc.bottom - rc.top);
    m_size  = {W, H};

    const float TB  = S(32.f);    // titlebar height
    const float LP  = S(320.f);   // left panel width
    const float PAD = S(12.f);
    const float BTH = S(26.f);    // button height

    // Win controls
    float bx = W - S(44.f);
    m_buttons[BTN_WIN_CLOSE].rect = R(bx, 0, S(44.f), TB); bx -= S(44.f);
    m_buttons[BTN_WIN_MAX].rect   = R(bx, 0, S(44.f), TB); bx -= S(44.f);
    m_buttons[BTN_WIN_MIN].rect   = R(bx, 0, S(44.f), TB);

    // Mode buttons (left panel, below titlebar)
    float mx = PAD, my = TB + PAD;
    float mw = (LP - PAD*2 - S(4.f)*5) / 6.f;
    BtnID modes[] = {BTN_MODE_COPY, BTN_MODE_MOVE, BTN_MODE_RENAME,
                     BTN_MODE_DELETE, BTN_MODE_DEDUPE, BTN_MODE_SYNC};
    for (int i = 0; i < 6; i++) {
        m_buttons[modes[i]].rect = R(mx + i*(mw + S(4.f)), my, mw, BTH);
    }

    // Source buttons
    float sy = my + BTH + S(60.f) + S(8.f) + S(100.f) + S(8.f);
    m_buttons[BTN_ADD_FILES].rect  = R(PAD,          sy, S(70.f), BTH);
    m_buttons[BTN_ADD_FOLDER].rect = R(PAD+S(74.f),  sy, S(70.f), BTH);
    m_buttons[BTN_CLEAR_SRC].rect  = R(PAD+S(148.f), sy, S(60.f), BTH);

    // Dest button
    float dy = sy + BTH + S(8.f) + BTH + S(8.f);
    m_buttons[BTN_PICK_DEST].rect = R(LP - PAD - S(60.f), dy, S(60.f), BTH);

    // Options
    float oy = dy + BTH + S(8.f);
    m_buttons[BTN_OPT_OVERWRITE].rect = R(PAD,          oy, S(88.f), BTH);
    m_buttons[BTN_OPT_VERIFY].rect    = R(PAD+S(92.f),  oy, S(68.f), BTH);
    m_buttons[BTN_OPT_PRESERVE].rect  = R(PAD+S(164.f), oy, S(88.f), BTH);

    // Thread count
    m_buttons[BTN_OPT_THREADS].rect = R(PAD + S(256.f), oy, S(44.f), BTH);

    // Run button
    float ry = oy + BTH + S(8.f);
    m_buttons[BTN_RUN].rect = R(PAD, ry, LP - PAD*2, S(36.f));

    // Cancel / Pause (right panel)
    float rp = LP + S(1.f); // right panel x
    float pw = W - rp;
    float cy2 = TB + PAD + S(36.f) + S(8.f);
    float bw2 = (pw - PAD*2 - S(8.f)) / 2.f;
    m_buttons[BTN_CANCEL].rect = R(rp + PAD,        cy2 + S(68.f), bw2, BTH);
    m_buttons[BTN_PAUSE].rect  = R(rp + PAD + bw2 + S(8.f), cy2 + S(68.f), bw2, BTH);

    // Clear log
    m_buttons[BTN_CLEAR_LOG].rect = R(PAD, H - S(14.f) - BTH, S(60.f), BTH);

    // Mark danger buttons
    m_buttons[BTN_MODE_DELETE].danger = true;
    m_buttons[BTN_CANCEL].danger      = true;

    // Disable controls based on state
    bool running = (m_opState == OpState::RUNNING || m_opState == OpState::PAUSED
                 || m_opState == OpState::SCANNING);
    m_buttons[BTN_RUN].enabled    = !running;
    m_buttons[BTN_CANCEL].enabled = running;
    m_buttons[BTN_PAUSE].enabled  = running;
}

// ─────────────────────────────────────────────────────────────────
// Drawing helpers
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::SetBrushColor(D2D1_COLOR_F c) {
    if (m_brush) m_brush->SetColor(c);
}

void TitanShiftApp::DrawRect(D2D1_RECT_F r, D2D1_COLOR_F fill, D2D1_COLOR_F stroke, float sw) {
    if (!m_rt || !m_brush) return;
    if (fill.a > 0.f) { SetBrushColor(fill); m_rt->FillRectangle(r, m_brush); }
    if (sw > 0.f && stroke.a > 0.f) { SetBrushColor(stroke); m_rt->DrawRectangle(r, m_brush, sw); }
}

void TitanShiftApp::DrawRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F fill, D2D1_COLOR_F stroke, float sw) {
    if (!m_rt || !m_brush) return;
    D2D1_ROUNDED_RECT rr{ r, radius, radius };
    if (fill.a > 0.f)     { SetBrushColor(fill);   m_rt->FillRoundedRectangle(rr, m_brush); }
    if (sw > 0.f && stroke.a > 0.f) { SetBrushColor(stroke); m_rt->DrawRoundedRectangle(rr, m_brush, sw); }
}

void TitanShiftApp::DrawLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F col, float w) {
    if (!m_rt || !m_brush) return;
    SetBrushColor(col);
    m_rt->DrawLine({x1,y1}, {x2,y2}, m_brush, w);
}

void TitanShiftApp::DrawText(const wchar_t* t, D2D1_RECT_F r, FontID fid,
                              D2D1_COLOR_F col,
                              DWRITE_TEXT_ALIGNMENT ha,
                              DWRITE_PARAGRAPH_ALIGNMENT va) {
    if (!m_rt || !m_brush || !m_fonts[fid] || !t) return;
    IDWriteTextFormat* fmt = m_fonts[fid];
    fmt->SetTextAlignment(ha);
    fmt->SetParagraphAlignment(va);
    SetBrushColor(col);
    m_rt->DrawTextW(t, (UINT32)wcslen(t), fmt, r, m_brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void TitanShiftApp::DrawSectionLabel(const wchar_t* text, float x, float y, float w) {
    DrawLine(x, y + S(7.f), x + w, y + S(7.f), C::BORDER, 0.5f);
    DrawText(text, R(x, y, w, S(14.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

// ─────────────────────────────────────────────────────────────────
// DrawButton
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawButton(const Button& btn) {
    if (!m_rt) return;
    D2D1_RECT_F r = btn.rect;
    float rad = S(3.f);

    D2D1_COLOR_F fill, textCol, stroke;
    stroke = {0,0,0,0};

    if (!btn.enabled) {
        fill    = C::BG2;
        textCol = C::TEXT_DIM;
        D2D1_COLOR_F sc = C::BORDER;
        DrawRoundRect(r, rad, fill, sc, 0.5f);
    } else if (btn.active && !btn.danger) {
        fill    = C::ACCENT;
        textCol = C::ACCENT_DK;
        DrawRoundRect(r, rad, fill, {0,0,0,0}, 0.f);
    } else if (btn.active && btn.danger) {
        fill    = C::DANGER;
        textCol = {1,1,1,1};
        DrawRoundRect(r, rad, fill, {0,0,0,0}, 0.f);
    } else if (btn.hovered && !btn.danger) {
        fill    = C::BG3;
        textCol = C::ACCENT;
        D2D1_COLOR_F sc = C::ACCENT; sc.a = 0.5f;
        DrawRoundRect(r, rad, fill, sc, 0.5f);
    } else if (btn.hovered && btn.danger) {
        fill    = {0.2f, 0.04f, 0.05f, 1.f};
        textCol = C::DANGER;
        DrawRoundRect(r, rad, fill, C::DANGER, 0.5f);
    } else {
        fill    = C::BG2;
        textCol = C::TEXT_DIM;
        DrawRoundRect(r, rad, fill, C::BORDER, 0.5f);
    }

    // Special: RUN button fills full width
    if (btn.id == BTN_RUN) {
        if (btn.enabled) {
            bool del = (m_mode == OpMode::DELETE);
            fill    = del ? C::DANGER : C::ACCENT;
            textCol = del ? D2D1_COLOR_F{1,1,1,1} : C::ACCENT_DK;
            DrawRoundRect(r, rad, fill, {0,0,0,0}, 0.f);
        }
    }

    DrawText(btn.label.c_str(), r, FONT_MONO_SM, textCol,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────
// OnPaint — main render entry
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::OnPaint() {
    if (!m_rt) return;
    m_rt->BeginDraw();

    RECT rc; GetClientRect(m_hwnd, &rc);
    float W = (float)(rc.right - rc.left);
    float H = (float)(rc.bottom - rc.top);
    m_size  = {W, H};

    const float LP  = S(320.f);
    const float TB  = S(32.f);

    // Background
    m_rt->Clear(C::BG);

    // Left panel background
    DrawRect(R(0, TB, LP, H - TB), C::BG1);

    // Divider line
    DrawLine(LP, TB, LP, H, C::BORDER, 1.f);

    DrawTitleBar();
    DrawLeftPanel();
    DrawRightPanel();

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) RecreateTarget();
}

// ─────────────────────────────────────────────────────────────────
// DrawTitleBar
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawTitleBar() {
    float W = m_size.width;
    float TB = S(32.f);

    DrawRect(R(0, 0, W, TB), C::BG1);
    DrawLine(0, TB, W, TB, C::BORDER, 0.5f);

    // Logo diamond
    float lx = S(14.f), ly = TB/2.f;
    D2D1_POINT_2F pts[4] = {{lx, ly-S(7.f)}, {lx+S(7.f), ly}, {lx, ly+S(7.f)}, {lx-S(7.f), ly}};
    SetBrushColor(C::ACCENT);
    // Draw diamond as 4 lines
    for (int i = 0; i < 4; i++) {
        m_rt->DrawLine(pts[i], pts[(i+1)%4], m_brush, S(1.2f));
    }

    // App name
    DrawText(L"TITANSHIFT", R(S(28.f), 0, S(120.f), TB), FONT_MONO_MD, C::ACCENT,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawText(L"v2.0", R(S(148.f), 0, S(40.f), TB), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // State indicator
    if (m_opState == OpState::RUNNING || m_opState == OpState::SCANNING) {
        DrawText(L"● ACTIVE", R(S(200.f), 0, S(100.f), TB), FONT_MONO_SM, C::ACCENT,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Win controls
    m_buttons[BTN_WIN_MIN].label   = L"─";
    m_buttons[BTN_WIN_MAX].label   = L"□";
    m_buttons[BTN_WIN_CLOSE].label = L"✕";
    m_buttons[BTN_WIN_CLOSE].danger = true;
    DrawButton(m_buttons[BTN_WIN_MIN]);
    DrawButton(m_buttons[BTN_WIN_MAX]);
    DrawButton(m_buttons[BTN_WIN_CLOSE]);
}

// ─────────────────────────────────────────────────────────────────
// DrawLeftPanel
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawLeftPanel() {
    const float LP  = S(320.f);
    const float TB  = S(32.f);
    const float PAD = S(12.f);
    const float BTH = S(26.f);
    float y = TB + PAD;

    // ── Mode bar ──────────────────────────────────────────
    static const wchar_t* modeLabels[] = {L"COPY",L"MOVE",L"RENAME",L"DELETE",L"DEDUPE",L"SYNC"};
    BtnID modes[] = {BTN_MODE_COPY,BTN_MODE_MOVE,BTN_MODE_RENAME,BTN_MODE_DELETE,BTN_MODE_DEDUPE,BTN_MODE_SYNC};
    for (int i = 0; i < 6; i++) {
        m_buttons[modes[i]].label  = modeLabels[i];
        m_buttons[modes[i]].active = ((int)m_mode == i);
        DrawButton(m_buttons[modes[i]]);
    }
    y += BTH + S(10.f);

    // ── SOURCE ────────────────────────────────────────────
    DrawSectionLabel(L"SOURCE", PAD, y, LP - PAD*2);
    y += S(14.f);

    // Drop zone
    float dzH = S(50.f);
    DrawRoundRect(R(PAD, y, LP - PAD*2, dzH), S(4.f), C::BG2, C::BORDER2, 0.5f);
    DrawText(L"Drop files/folders here — or use buttons below",
        R(PAD, y, LP - PAD*2, dzH), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += dzH + S(4.f);

    // Source file list
    float listH = S(90.f);
    DrawRoundRect(R(PAD, y, LP - PAD*2, listH), S(2.f), C::BG, C::BORDER, 0.5f);
    {
        std::vector<FileEntry> srcs;
        srcs = m_sources; // copy for render
        float iy = y + S(4.f);
        float lineH = S(16.f);
        int maxVisible = (int)(listH / lineH) - 1;
        int start = m_srcScrollOffset;
        int end   = std::min((int)srcs.size(), start + maxVisible);
        for (int i = start; i < end; i++) {
            D2D1_COLOR_F bg = (i % 2 == 0) ? C::BG : C::BG1;
            DrawRect(R(PAD + S(1.f), iy, LP - PAD*2 - S(2.f), lineH), bg);
            DrawText(srcs[i].name.c_str(),
                R(PAD + S(6.f), iy, LP - PAD*2 - S(12.f), lineH),
                FONT_MONO_SM, C::TEXT,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            // Size
            wchar_t sz[32];
            swprintf_s(sz, L"%s", FormatBytes(srcs[i].size).c_str());
            DrawText(sz, R(PAD + S(6.f), iy, LP - PAD*2 - S(12.f), lineH),
                FONT_MONO_SM, C::TEXT_DIM,
                DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            iy += lineH;
        }
        if (srcs.empty()) {
            DrawText(L"No files selected",
                R(PAD, y, LP - PAD*2, listH),
                FONT_MONO_SM, C::TEXT_DIM,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    y += listH + S(4.f);

    // Source buttons
    m_buttons[BTN_ADD_FILES].label  = L"+ Files";
    m_buttons[BTN_ADD_FOLDER].label = L"+ Folder";
    m_buttons[BTN_CLEAR_SRC].label  = L"Clear";
    m_buttons[BTN_CLEAR_SRC].danger = true;
    DrawButton(m_buttons[BTN_ADD_FILES]);
    DrawButton(m_buttons[BTN_ADD_FOLDER]);
    DrawButton(m_buttons[BTN_CLEAR_SRC]);
    y += BTH + S(10.f);

    // ── DESTINATION ───────────────────────────────────────
    bool showDest = (m_mode != OpMode::DELETE && m_mode != OpMode::DEDUPE);
    bool showRename = (m_mode == OpMode::RENAME);

    if (showDest && !showRename) {
        DrawSectionLabel(L"DESTINATION", PAD, y, LP - PAD*2);
        y += S(14.f);
        DrawRoundRect(R(PAD, y, LP - PAD*2 - S(68.f), BTH), S(2.f), C::BG2, C::BORDER, 0.5f);
        const wchar_t* dtext = m_dest.empty() ? L"—" : m_dest.c_str();
        D2D1_COLOR_F dc = m_dest.empty() ? C::TEXT_DIM : C::ACCENT;
        DrawText(dtext, R(PAD + S(6.f), y, LP - PAD*2 - S(74.f), BTH),
            FONT_MONO_SM, dc, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_buttons[BTN_PICK_DEST].label = L"Browse";
        DrawButton(m_buttons[BTN_PICK_DEST]);
        y += BTH + S(10.f);
    }

    if (showRename) {
        DrawSectionLabel(L"RENAME PATTERN", PAD, y, LP - PAD*2);
        y += S(14.f);
        DrawText(L"Tokens: {name} {ext} {n} {date}", R(PAD, y, LP-PAD*2, S(14.f)),
            FONT_MONO_SM, C::TEXT_DIM);
        y += S(16.f);
        // Input box
        bool active = m_renameInputActive;
        DrawRoundRect(R(PAD, y, LP - PAD*2, BTH), S(2.f),
            C::BG2, active ? C::ACCENT : C::BORDER, active ? 1.f : 0.5f);
        std::wstring disp = m_renameInput.empty() ? L"e.g. report_{n}{ext}" : m_renameInput;
        D2D1_COLOR_F ic = m_renameInput.empty() ? C::TEXT_DIM : C::TEXT;
        DrawText(disp.c_str(), R(PAD + S(6.f), y, LP - PAD*2 - S(12.f), BTH),
            FONT_MONO_MD, ic, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        y += BTH + S(10.f);
    }

    // ── OPTIONS ───────────────────────────────────────────
    DrawSectionLabel(L"OPTIONS", PAD, y, LP - PAD*2);
    y += S(14.f);
    m_buttons[BTN_OPT_OVERWRITE].label = m_buttons[BTN_OPT_OVERWRITE].active ? L"✓ Overwrite" : L"  Overwrite";
    m_buttons[BTN_OPT_VERIFY].label    = m_buttons[BTN_OPT_VERIFY].active    ? L"✓ Verify"    : L"  Verify";
    m_buttons[BTN_OPT_PRESERVE].label  = m_buttons[BTN_OPT_PRESERVE].active  ? L"✓ Timestamps": L"  Timestamps";
    DrawButton(m_buttons[BTN_OPT_OVERWRITE]);
    DrawButton(m_buttons[BTN_OPT_VERIFY]);
    DrawButton(m_buttons[BTN_OPT_PRESERVE]);

    // Thread count spinner
    wchar_t thr[16]; swprintf_s(thr, L"×%d", m_threadCount);
    m_buttons[BTN_OPT_THREADS].label = thr;
    DrawButton(m_buttons[BTN_OPT_THREADS]);
    DrawText(L"threads", R(m_buttons[BTN_OPT_THREADS].rect.right + S(4.f), y, S(50.f), BTH),
        FONT_MONO_SM, C::TEXT_DIM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    y += BTH + S(10.f);

    // ── RUN BUTTON ────────────────────────────────────────
    static const wchar_t* runLabels[] = {
        L"▶  RUN COPY", L"▶  RUN MOVE", L"▶  RUN RENAME",
        L"▶  DELETE FILES", L"▶  RUN DEDUPE", L"▶  RUN SYNC"
    };
    m_buttons[BTN_RUN].label = runLabels[(int)m_mode];
    m_buttons[BTN_RUN].rect  = R(PAD, y, LP - PAD*2, S(36.f));
    DrawButton(m_buttons[BTN_RUN]);
    y += S(36.f) + S(10.f);

    // ── LOG PANEL ─────────────────────────────────────────
    DrawSectionLabel(L"LOG", PAD, y, LP - PAD*2);
    m_buttons[BTN_CLEAR_LOG].label = L"Clear";
    m_buttons[BTN_CLEAR_LOG].rect  = R(LP - PAD - S(44.f), y, S(44.f), S(12.f));
    DrawButton(m_buttons[BTN_CLEAR_LOG]);
    y += S(14.f);

    float logH = m_size.height - y - PAD;
    DrawRoundRect(R(PAD, y, LP - PAD*2, logH), S(2.f), C::BG, C::BORDER, 0.5f);
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        float ly2  = y + S(4.f);
        float lineH = S(14.f);
        int maxVis = (int)(logH / lineH) - 1;
        int total  = (int)m_logs.size();
        int start  = std::max(0, total - maxVis - m_logScrollOffset);
        int end    = std::min(total, start + maxVis);

        static const D2D1_COLOR_F logColors[] = {
            C::TEXT_DIM,  // INFO
            C::SUCCESS,   // SUCCESS
            C::WARN,      // WARN
            C::DANGER,    // ERR
        };

        for (int i = start; i < end; i++) {
            const auto& e = m_logs[i];
            std::wstring line = L"[" + e.timestamp + L"] " + e.msg;
            D2D1_COLOR_F col = logColors[(int)e.level];
            DrawText(line.c_str(), R(PAD + S(4.f), ly2, LP - PAD*2 - S(8.f), lineH),
                FONT_MONO_SM, col, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            ly2 += lineH;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// DrawRightPanel
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawRightPanel() {
    const float LP  = S(320.f);
    const float TB  = S(32.f);
    const float PAD = S(12.f);
    float x = LP + PAD;
    float y = TB + PAD;
    float pw = m_size.width - LP - PAD*2;

    // ── PROGRESS ─────────────────────────────────────────
    DrawSectionLabel(L"PROGRESS", x, y, pw);
    y += S(14.f);
    DrawProgressCard(x, y, pw);
    y += S(100.f) + S(10.f);

    // ── THROUGHPUT GRAPH ─────────────────────────────────
    DrawSectionLabel(L"THROUGHPUT (MB/s)", x, y, pw);
    y += S(14.f);
    float graphH = S(120.f);
    DrawThroughputGraph(x, y, pw, graphH);
    y += graphH + S(10.f);

    // ── HARDWARE METRICS ─────────────────────────────────
    DrawSectionLabel(L"HARDWARE", x, y, pw);
    y += S(14.f);

    float metW = (pw - S(8.f)*2) / 3.f;
    float metH = S(110.f);

    // CPU ring
    float cx1 = x + metW/2.f;
    SystemMetrics m;
    { std::lock_guard<std::mutex> lk(m_metricsMutex); m = m_metrics; }

    DrawRoundRect(R(x, y, metW, metH), S(4.f), C::BG1, C::BORDER, 0.5f);
    DrawText(L"CPU", R(x, y + S(6.f), metW, S(14.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawMetricRing(cx1, y + S(20.f) + S(24.f), S(24.f), m.cpuLoad / 100.f, C::ACCENT,
        nullptr, nullptr);
    wchar_t cv[16]; swprintf_s(cv, L"%.0f%%", m.cpuLoad);
    DrawText(cv, R(x, y + S(20.f) + S(8.f), metW, S(32.f)), FONT_MONO_MD, C::TEXT,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawText(L"CPU LOAD", R(x, y + metH - S(20.f), metW, S(14.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // MEM ring
    float cx2 = x + metW + S(8.f) + metW/2.f;
    DrawRoundRect(R(x + metW + S(8.f), y, metW, metH), S(4.f), C::BG1, C::BORDER, 0.5f);
    DrawText(L"MEMORY", R(x + metW + S(8.f), y + S(6.f), metW, S(14.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawMetricRing(cx2, y + S(20.f) + S(24.f), S(24.f), m.memUsedPct / 100.f, C::INFO,
        nullptr, nullptr);
    wchar_t mv[16]; swprintf_s(mv, L"%.0f%%", m.memUsedPct);
    DrawText(mv, R(x + metW + S(8.f), y + S(20.f) + S(8.f), metW, S(32.f)), FONT_MONO_MD, C::TEXT,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    std::wstring memSub = FormatBytes(m.memUsed) + L" / " + FormatBytes(m.memTotal);
    DrawText(memSub.c_str(), R(x + metW + S(8.f), y + metH - S(20.f), metW, S(14.f)),
        FONT_MONO_SM, C::TEXT_DIM, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // DISK bars
    DrawDiskMetric(x + (metW + S(8.f))*2, y, metW, metH);
}

// ─────────────────────────────────────────────────────────────────
// DrawProgressCard
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawProgressCard(float x, float y, float w) {
    float h   = S(100.f);
    float PAD = S(10.f);

    DrawRoundRect(R(x, y, w, h), S(4.f), C::BG1, C::BORDER, 0.5f);

    ProgressPayload p;
    { std::lock_guard<std::mutex> lk(m_progressMutex); p = m_lastProgress; }

    // Status badge
    static const wchar_t* stateLabels[] = {
        L"IDLE", L"SCANNING…", L"IN PROGRESS", L"PAUSED", L"COMPLETE", L"CANCELLED", L"ERROR"
    };
    static const D2D1_COLOR_F stateColors[] = {
        C::TEXT_DIM, C::WARN, C::ACCENT, C::INFO, C::SUCCESS, C::DANGER, C::DANGER
    };
    int si = (int)m_opState;
    if (si < 0 || si > 6) si = 0;

    DrawRoundRect(R(x + PAD, y + PAD, S(90.f), S(18.f)), S(2.f),
        {stateColors[si].r*0.1f, stateColors[si].g*0.1f, stateColors[si].b*0.1f, 1.f},
        stateColors[si], 0.5f);
    DrawText(stateLabels[si], R(x + PAD, y + PAD, S(90.f), S(18.f)),
        FONT_MONO_SM, stateColors[si],
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Files / bytes meta
    if (p.totalFiles > 0) {
        wchar_t meta[128];
        swprintf_s(meta, L"%llu/%llu files  ·  %s / %s",
            p.doneFiles, p.totalFiles,
            FormatBytes(p.doneBytes).c_str(), FormatBytes(p.totalBytes).c_str());
        DrawText(meta, R(x + PAD + S(96.f), y + PAD, w - PAD*2 - S(96.f), S(18.f)),
            FONT_MONO_SM, C::TEXT_DIM, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Current file
    float fy = y + PAD + S(22.f);
    if (!p.currentFile.empty()) {
        DrawText(p.currentFile.c_str(), R(x + PAD, fy, w - PAD*2, S(16.f)),
            FONT_MONO_SM, C::TEXT, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    fy += S(18.f);

    // Progress bar
    float barW = w - PAD*2 - S(44.f);
    float barH = S(6.f);
    DrawRoundRect(R(x + PAD, fy + S(4.f), barW, barH), S(3.f), C::BG3, {0,0,0,0}, 0.f);
    if (p.pct > 0) {
        float fill = barW * (p.pct / 100.f);
        DrawRoundRect(R(x + PAD, fy + S(4.f), fill, barH), S(3.f), C::ACCENT, {0,0,0,0}, 0.f);
    }
    wchar_t pct[8]; swprintf_s(pct, L"%d%%", p.pct);
    DrawText(pct, R(x + PAD + barW + S(4.f), fy, S(36.f), S(14.f)),
        FONT_MONO_SM, C::ACCENT, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fy += S(16.f);

    // Speed + ETA
    if (p.bytesPerSec > 0) {
        wchar_t spd[64];
        swprintf_s(spd, L"%s/s  ·  ETA %s",
            FormatBytes(p.bytesPerSec).c_str(), FormatETA(p.etaSecs).c_str());
        DrawText(spd, R(x + PAD, fy, w - PAD*2, S(14.f)),
            FONT_MONO_SM, C::TEXT_DIM, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Cancel / Pause buttons
    m_buttons[BTN_CANCEL].label = L"CANCEL";
    m_buttons[BTN_PAUSE].label  = (m_opState == OpState::PAUSED) ? L"RESUME" : L"PAUSE";
    DrawButton(m_buttons[BTN_CANCEL]);
    DrawButton(m_buttons[BTN_PAUSE]);
}

// ─────────────────────────────────────────────────────────────────
// DrawThroughputGraph
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawThroughputGraph(float x, float y, float w, float h) {
    const float PAD = S(4.f);
    DrawRoundRect(R(x, y, w, h), S(4.f), C::BG1, C::BORDER, 0.5f);

    SystemMetrics m;
    { std::lock_guard<std::mutex> lk(m_metricsMutex); m = m_metrics; }

    auto drawSeries = [&](const std::deque<float>& hist, D2D1_COLOR_F lineCol, D2D1_COLOR_F fillCol) {
        if (hist.empty()) return;
        float maxVal = 1.f;
        for (float v : hist) maxVal = std::max(maxVal, v);

        int   n   = (int)hist.size();
        float gW  = w - PAD*2;
        float gH  = h - PAD*2 - S(12.f);
        float gX  = x + PAD;
        float gY  = y + PAD;

        // Build path as array of points
        std::vector<D2D1_POINT_2F> pts;
        pts.reserve(n + 2);
        for (int i = 0; i < n; i++) {
            float px = gX + (gW * i / std::max(1, n - 1));
            float py = gY + gH - (hist[i] / maxVal) * gH;
            pts.push_back({px, py});
        }

        if (pts.size() < 2) return;

        // Draw line using DrawLine (Direct2D has no polyline directly)
        SetBrushColor(lineCol);
        for (int i = 0; i + 1 < (int)pts.size(); i++) {
            m_rt->DrawLine(pts[i], pts[i+1], m_brush, S(1.5f));
        }

        // Scale label
        wchar_t lbl[32]; swprintf_s(lbl, L"%.1f", maxVal);
    };

    // Grid lines
    int cols = 12, rows = 4;
    float gX2 = x + PAD, gW2 = w - PAD*2;
    float gY2 = y + PAD, gH2 = h - PAD*2 - S(12.f);
    for (int i = 0; i <= cols; i++) {
        float lx = gX2 + gW2 * i / cols;
        DrawLine(lx, gY2, lx, gY2 + gH2, C::BORDER, 0.5f);
    }
    for (int i = 0; i <= rows; i++) {
        float ly = gY2 + gH2 * i / rows;
        DrawLine(gX2, ly, gX2 + gW2, ly, C::BORDER, 0.5f);
    }

    drawSeries(m.diskReadHistory,  C::INFO,   C::INFO);
    drawSeries(m.diskWriteHistory, C::ACCENT, C::ACCENT);

    // Legend
    float lx = x + PAD;
    float legY = y + h - S(12.f);
    DrawText(L"▸ READ", R(lx, legY, S(60.f), S(12.f)), FONT_MONO_SM, C::INFO,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawText(L"▸ WRITE", R(lx + S(65.f), legY, S(65.f), S(12.f)), FONT_MONO_SM, C::ACCENT,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Scale
    SystemMetrics m2; { std::lock_guard<std::mutex> lk(m_metricsMutex); m2 = m_metrics; }
    float maxR = 0.1f, maxW2 = 0.1f;
    for (float v : m2.diskReadHistory) maxR = std::max(maxR, v);
    for (float v : m2.diskWriteHistory) maxW2 = std::max(maxW2, v);
    wchar_t sc[32]; swprintf_s(sc, L"%.1f MB/s", std::max(maxR, maxW2));
    DrawText(sc, R(x + PAD, y + PAD, w - PAD*2, S(12.f)),
        FONT_MONO_SM, C::TEXT_DIM, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────
// DrawMetricRing (Direct2D arc geometry)
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawMetricRing(float cx, float cy, float r, float pct,
                                    D2D1_COLOR_F color, const wchar_t*, const wchar_t*) {
    if (!m_rt || !m_d2dFactory) return;

    // Background ring
    ID2D1PathGeometry* bgGeo = nullptr;
    m_d2dFactory->CreatePathGeometry(&bgGeo);
    if (!bgGeo) return;

    // Draw background circle
    SetBrushColor(C::BG3);
    D2D1_ELLIPSE ell = { {cx, cy}, r, r };
    m_rt->DrawEllipse(ell, m_brush, S(5.f));

    // Draw filled arc
    float angle = pct * 360.f;
    float startA = -90.f * (3.14159f / 180.f);
    float sweepA =  angle * (3.14159f / 180.f);

    // Use DrawEllipse with different stroke-dasharray isn't possible in D2D directly.
    // Use path geometry for arc fill.
    float endA = startA + sweepA;
    float rOut = r + S(2.5f), rIn = r - S(2.5f);

    ID2D1GeometrySink* sink = nullptr;
    bgGeo->Open(&sink);
    if (sink) {
        D2D1_POINT_2F startOuter = { cx + rOut * cosf(startA), cy + rOut * sinf(startA) };
        sink->BeginFigure(startOuter, D2D1_FIGURE_BEGIN_FILLED);

        D2D1_ARC_SEGMENT arcO{};
        arcO.point       = { cx + rOut * cosf(endA), cy + rOut * sinf(endA) };
        arcO.size        = { rOut, rOut };
        arcO.rotationAngle = 0;
        arcO.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arcO.arcSize     = (pct > 0.5f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arcO);

        sink->AddLine({cx + rIn * cosf(endA), cy + rIn * sinf(endA)});

        D2D1_ARC_SEGMENT arcI{};
        arcI.point       = startOuter;
        arcI.size        = { rIn, rIn };
        arcI.rotationAngle = 0;
        arcI.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
        arcI.arcSize     = (pct > 0.5f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arcI);

        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();

        SetBrushColor(color);
        m_rt->FillGeometry(bgGeo, m_brush);
    }
    bgGeo->Release();
}

// ─────────────────────────────────────────────────────────────────
// DrawDiskMetric
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::DrawDiskMetric(float x, float y, float w, float h) {
    DrawRoundRect(R(x, y, w, h), S(4.f), C::BG1, C::BORDER, 0.5f);
    DrawText(L"DISK", R(x, y + S(6.f), w, S(14.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    SystemMetrics m;
    { std::lock_guard<std::mutex> lk(m_metricsMutex); m = m_metrics; }

    float maxVal = std::max(1.f, std::max(m.diskReadMBs, m.diskWriteMBs));
    float pad = S(10.f);
    float barW = w - pad*2 - S(30.f) - S(36.f);
    float barH = S(6.f);

    // READ bar
    float ry2 = y + S(28.f);
    DrawText(L"R", R(x + pad, ry2, S(14.f), barH + S(4.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawRoundRect(R(x + pad + S(18.f), ry2, barW, barH), S(3.f), C::BG3, {0,0,0,0}, 0.f);
    float rFill = barW * (m.diskReadMBs / maxVal);
    if (rFill > 0) DrawRoundRect(R(x + pad + S(18.f), ry2, rFill, barH), S(3.f), C::INFO, {0,0,0,0}, 0.f);
    wchar_t rv[16]; swprintf_s(rv, L"%.1f", m.diskReadMBs);
    DrawText(rv, R(x + pad + S(18.f) + barW + S(4.f), ry2, S(36.f), barH + S(4.f)),
        FONT_MONO_SM, C::TEXT, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // WRITE bar
    float wy2 = ry2 + S(18.f);
    DrawText(L"W", R(x + pad, wy2, S(14.f), barH + S(4.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    DrawRoundRect(R(x + pad + S(18.f), wy2, barW, barH), S(3.f), C::BG3, {0,0,0,0}, 0.f);
    float wFill = barW * (m.diskWriteMBs / maxVal);
    if (wFill > 0) DrawRoundRect(R(x + pad + S(18.f), wy2, wFill, barH), S(3.f), C::ACCENT, {0,0,0,0}, 0.f);
    wchar_t wv[16]; swprintf_s(wv, L"%.1f", m.diskWriteMBs);
    DrawText(wv, R(x + pad + S(18.f) + barW + S(4.f), wy2, S(36.f), barH + S(4.f)),
        FONT_MONO_SM, C::TEXT, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    DrawText(L"MB/s", R(x, wy2 + S(14.f), w, S(12.f)), FONT_MONO_SM, C::TEXT_DIM,
        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ─────────────────────────────────────────────────────────────────
// Input handling
// ─────────────────────────────────────────────────────────────────
Button* TitanShiftApp::HitTestButtons(int x, int y) {
    float fx = (float)x, fy = (float)y;
    for (int i = 0; i < BTN_COUNT; i++) {
        auto& b = m_buttons[i];
        if (fx >= b.rect.left && fx <= b.rect.right &&
            fy >= b.rect.top  && fy <= b.rect.bottom) {
            return &b;
        }
    }
    return nullptr;
}

void TitanShiftApp::OnLButtonDown(int x, int y) {
    SetCapture(m_hwnd);
    Button* btn = HitTestButtons(x, y);
    if (btn && btn->enabled) {
        btn->pressed = true;
        OnButtonClick(btn->id);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // Rename input click
    auto& rn = m_buttons[BTN_MODE_RENAME];
    bool inRenameBox = false;
    // Check if rename input box was clicked (simple rect test)
    m_renameInputActive = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TitanShiftApp::OnLButtonUp(int x, int y) {
    ReleaseCapture();
    for (auto& b : m_buttons) b.pressed = false;
}

void TitanShiftApp::OnMouseMove(int x, int y) {
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    float fx = (float)x, fy = (float)y;
    bool changed = false;
    for (int i = 0; i < BTN_COUNT; i++) {
        auto& b = m_buttons[i];
        bool was = b.hovered;
        b.hovered = (fx >= b.rect.left && fx <= b.rect.right &&
                     fy >= b.rect.top  && fy <= b.rect.bottom);
        if (b.hovered != was) changed = true;
    }
    if (changed) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void TitanShiftApp::OnMouseLeave() {
    m_trackingMouse = false;
    for (auto& b : m_buttons) b.hovered = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TitanShiftApp::OnMouseWheel(int delta, int, int) {
    m_logScrollOffset = std::max(0, m_logScrollOffset + (delta > 0 ? 1 : -1));
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TitanShiftApp::OnChar(wchar_t c) {
    if (!m_renameInputActive) return;
    if (c == '\b') {
        if (!m_renameInput.empty()) m_renameInput.pop_back();
    } else if (c >= 32) {
        m_renameInput += c;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TitanShiftApp::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE && m_renameInputActive) {
        m_renameInputActive = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ─────────────────────────────────────────────────────────────────
// Button actions
// ─────────────────────────────────────────────────────────────────
void TitanShiftApp::OnButtonClick(BtnID id) {
    switch (id) {
    case BTN_WIN_MIN:   ShowWindow(m_hwnd, SW_MINIMIZE); break;
    case BTN_WIN_MAX:   ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case BTN_WIN_CLOSE: DestroyWindow(m_hwnd); break;

    case BTN_MODE_COPY:   m_mode = OpMode::COPY;   break;
    case BTN_MODE_MOVE:   m_mode = OpMode::MOVE;   break;
    case BTN_MODE_RENAME: m_mode = OpMode::RENAME; m_renameInputActive = true; break;
    case BTN_MODE_DELETE: m_mode = OpMode::DELETE; break;
    case BTN_MODE_DEDUPE: m_mode = OpMode::DEDUPE; break;
    case BTN_MODE_SYNC:   m_mode = OpMode::SYNC;   break;

    case BTN_ADD_FILES:  PickSourceFiles(false); break;
    case BTN_ADD_FOLDER: PickSourceFiles(true);  break;
    case BTN_CLEAR_SRC:  m_sources.clear();      break;
    case BTN_PICK_DEST:  PickDestination();       break;

    case BTN_OPT_OVERWRITE: m_buttons[BTN_OPT_OVERWRITE].active ^= 1; m_optOverwrite = m_buttons[BTN_OPT_OVERWRITE].active; break;
    case BTN_OPT_VERIFY:    m_buttons[BTN_OPT_VERIFY].active    ^= 1; m_optVerify    = m_buttons[BTN_OPT_VERIFY].active;    break;
    case BTN_OPT_PRESERVE:  m_buttons[BTN_OPT_PRESERVE].active  ^= 1; m_optPreserve  = m_buttons[BTN_OPT_PRESERVE].active;  break;
    case BTN_OPT_THREADS:
        m_threadCount = (m_threadCount % 8) + 1;
        break;

    case BTN_RUN:    StartOperation(); break;
    case BTN_CANCEL: CancelOperation(); break;
    case BTN_PAUSE:  PauseOperation(); break;
    case BTN_CLEAR_LOG: { std::lock_guard<std::mutex> lk(m_logMutex); m_logs.clear(); } break;
    default: break;
    }
    UpdateButtonLayout();
}

void TitanShiftApp::PickSourceFiles(bool folders) {
    // Use IFileOpenDialog for modern multi-select
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
        IID_IFileOpenDialog, (void**)&dlg))) return;

    DWORD flags = FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT;
    if (folders) flags |= FOS_PICKFOLDERS;
    dlg->SetOptions(flags);

    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        WIN32_FILE_ATTRIBUTE_DATA info{};
                        if (GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
                            FileEntry e;
                            e.path     = path;
                            e.name     = PathFindFileNameW(path);
                            e.isDir    = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                            ULARGE_INTEGER sz;
                            sz.LowPart  = info.nFileSizeLow;
                            sz.HighPart = info.nFileSizeHigh;
                            e.size     = e.isDir ? 0 : sz.QuadPart;
                            e.modified = info.ftLastWriteTime;
                            m_sources.push_back(e);
                        }
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            items->Release();
        }
    }
    dlg->Release();
}

void TitanShiftApp::PickDestination() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
        IID_IFileOpenDialog, (void**)&dlg))) return;
    dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
    if (SUCCEEDED(dlg->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                m_dest = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
}

static std::atomic<bool> s_cancel{false};
static std::atomic<bool> s_pause{false};
static OpStats s_stats;

void TitanShiftApp::StartOperation() {
    if (m_sources.empty()) { PostLog(LogLevel::WARN, L"No source files."); return; }
    if ((m_mode == OpMode::COPY || m_mode == OpMode::MOVE || m_mode == OpMode::SYNC)
        && m_dest.empty()) {
        PostLog(LogLevel::WARN, L"No destination selected."); return;
    }

    // Reset state
    s_cancel = false;
    s_pause  = false;
    s_stats  = OpStats{};
    s_stats.startTick = GetTickCount64();
    m_opState = OpState::SCANNING;
    m_lastProgress = {};

    // Copy needed data for thread
    std::vector<FileEntry> srcCopy = m_sources;
    std::wstring destCopy = m_dest;
    std::wstring renamePat = m_renameInput;
    OpMode mode = m_mode;
    CopyOptions opts;
    opts.overwrite = m_optOverwrite;
    opts.verify    = m_optVerify;
    opts.preserveTimestamps = m_optPreserve;
    opts.threadCount = m_threadCount;

    UpdateButtonLayout();

    if (m_workerThread.joinable()) m_workerThread.join();

    m_workerThread = std::thread([this, srcCopy, destCopy, renamePat, mode, opts]() {
        PostLog(LogLevel::INFO, L"Scanning files…");

        // Expand directories to file list
        std::vector<std::wstring> roots;
        for (const auto& e : srcCopy) roots.push_back(e.path);
        std::vector<FileEntry> allFiles = FileEngine::ScanPaths(roots, &s_cancel);

        if (s_cancel.load()) {
            PostLog(LogLevel::WARN, L"Cancelled during scan.");
            m_opState = OpState::CANCELLED;
            PostMessageW(m_hwnd, WM_PROGRESS_UPDATE, 0, 0);
            return;
        }

        s_stats.totalFiles = (ULONGLONG)allFiles.size();
        s_stats.totalBytes = FileEngine::TotalSize(allFiles);

        wchar_t buf[128];
        swprintf_s(buf, L"Found %llu files (%s)",
            s_stats.totalFiles.load(),
            FormatBytes(s_stats.totalBytes.load()).c_str());
        PostLog(LogLevel::INFO, buf);

        m_opState = OpState::RUNNING;

        FileEngine engine(this, &s_stats, &s_cancel, &s_pause);
        switch (mode) {
        case OpMode::COPY:   engine.Copy(allFiles,   destCopy, opts); break;
        case OpMode::MOVE:   engine.Move(allFiles,   destCopy, opts); break;
        case OpMode::RENAME: engine.Rename(allFiles, renamePat);       break;
        case OpMode::DELETE: engine.Delete(allFiles);                   break;
        case OpMode::DEDUPE: engine.Dedupe(allFiles, destCopy);         break;
        case OpMode::SYNC:   engine.Sync(allFiles,   destCopy, opts);   break;
        }

        if (!s_cancel.load()) PostComplete();
        else { m_opState = OpState::CANCELLED; PostMessageW(m_hwnd, WM_PROGRESS_UPDATE, 0, 0); }
    });
}

void TitanShiftApp::CancelOperation() {
    s_cancel = true;
    PostLog(LogLevel::WARN, L"Cancellation requested…");
}

void TitanShiftApp::PauseOperation() {
    bool was = s_pause.load();
    s_pause  = !was;
    m_opState = s_pause.load() ? OpState::PAUSED : OpState::RUNNING;
    PostLog(LogLevel::INFO, s_pause.load() ? L"Paused." : L"Resumed.");
    UpdateButtonLayout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────
std::wstring TitanShiftApp::FormatBytes(ULONGLONG bytes) {
    if (bytes == 0) return L"0 B";
    static const wchar_t* units[] = {L"B",L"KB",L"MB",L"GB",L"TB",L"PB"};
    int i = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && i < 5) { d /= 1024.0; i++; }
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f %s", d, units[i]);
    return buf;
}

std::wstring TitanShiftApp::FormatSpeed(ULONGLONG bps) {
    return FormatBytes(bps) + L"/s";
}

std::wstring TitanShiftApp::FormatETA(ULONGLONG secs) {
    if (secs == 0) return L"—";
    wchar_t buf[32];
    if (secs < 60)        swprintf_s(buf, L"%llus", secs);
    else if (secs < 3600) swprintf_s(buf, L"%llum %llus", secs/60, secs%60);
    else                  swprintf_s(buf, L"%lluh %llum", secs/3600, (secs%3600)/60);
    return buf;
}

std::wstring TitanShiftApp::FormatTime() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}
