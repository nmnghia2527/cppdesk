#include "ui_window.hpp"

#include <windowsx.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstdio>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace aerodesk {

namespace {

D2D1_COLOR_F rgba(uint8_t r, uint8_t g, uint8_t b, float a = 1.0f) {
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float aMultiplier) {
    return D2D1::ColorF(c.r, c.g, c.b, std::clamp(c.a * aMultiplier, 0.0f, 1.0f));
}

D2D1_COLOR_F lerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    );
}

// Second-order damped harmonic oscillator (macOS CoreAnimation CASpringAnimation physics)
bool stepSpring(float& pos, float& vel, float target, float omega, float zeta, float dt, float eps = 0.0012f) {
    float diff = pos - target;
    if (std::fabs(diff) <= eps && std::fabs(vel) <= eps * 8.0f) {
        if (pos != target || vel != 0.0f) {
            pos = target;
            vel = 0.0f;
            return true;
        }
        return false;
    }

    // Sub-step integration for unconditional stability across variable frame times
    int steps = (dt > 0.008f) ? static_cast<int>(std::ceil(dt / 0.008f)) : 1;
    steps = std::clamp(steps, 1, 6);
    float subDt = dt / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        float disp = pos - target;
        float accel = (-omega * omega * disp) - (2.0f * zeta * omega * vel);
        vel += accel * subDt;
        pos += vel * subDt;
    }

    if (std::fabs(pos - target) <= eps && std::fabs(vel) <= eps * 8.0f) {
        pos = target;
        vel = 0.0f;
    }
    return true;
}

bool stepExp(float& current, float target, float speed, float dt) {
    float diff = target - current;
    if (std::fabs(diff) <= 0.003f) {
        if (current != target) {
            current = target;
            return true;
        }
        return false;
    }
    float factor = 1.0f - std::exp(-speed * dt);
    current += diff * factor;
    return true;
}

float smoothStepEase(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------- Dynamic Light (White & Blue) & Dark (Black & Blue) macOS Palette ----------------
D2D1_COLOR_F COL_BG_MAIN          = rgba(245, 247, 250);
D2D1_COLOR_F COL_BG_NAV           = rgba(255, 255, 255);
D2D1_COLOR_F COL_BG_CARD          = rgba(255, 255, 255);
D2D1_COLOR_F COL_BG_SUBTLE        = rgba(244, 246, 250);
D2D1_COLOR_F COL_BG_CARD_ALT      = rgba(239, 246, 255);
D2D1_COLOR_F COL_BG_INPUT         = rgba(244, 246, 250);
D2D1_COLOR_F COL_BG_INPUT_FOCUS   = rgba(255, 255, 255);
D2D1_COLOR_F COL_BORDER           = rgba(226, 232, 240);
D2D1_COLOR_F COL_BORDER_ALT       = rgba(191, 219, 254);
D2D1_COLOR_F COL_BORDER_FOCUS     = rgba(37, 99, 235);

D2D1_COLOR_F COL_PRIMARY_RED      = rgba(37, 99, 235);
D2D1_COLOR_F COL_PRIMARY_RED_HV   = rgba(29, 78, 216);
D2D1_COLOR_F COL_SEC_BTN_BG       = rgba(241, 245, 249);
D2D1_COLOR_F COL_SEC_BTN_HV       = rgba(226, 236, 252);

D2D1_COLOR_F COL_SUCCESS          = rgba(16, 185, 129);
D2D1_COLOR_F COL_WARNING          = rgba(245, 158, 11);
D2D1_COLOR_F COL_DANGER           = rgba(220, 38, 38);
D2D1_COLOR_F COL_DANGER_HV        = rgba(185, 28, 28);

D2D1_COLOR_F COL_TEXT_PRIMARY     = rgba(15, 23, 42);
D2D1_COLOR_F COL_TEXT_SECONDARY   = rgba(71, 85, 105);
D2D1_COLOR_F COL_TEXT_MUTED       = rgba(148, 163, 184);
D2D1_COLOR_F COL_TEXT_ACCENT      = rgba(29, 78, 216);
D2D1_COLOR_F COL_TEXT_ON_ACCENT   = rgba(255, 255, 255);
D2D1_COLOR_F COL_STAGE_BG         = rgba(226, 232, 240);

void updateActivePalette(float darkT) {
    darkT = std::clamp(darkT, 0.0f, 1.0f);

    // 60% Dominant Surfaces: Crisp White (#F5F7FA / #FFFFFF) <-> Pitch Black (#05070B / #0B0F17)
    COL_BG_MAIN        = lerpColor(rgba(245, 247, 250), rgba(5, 7, 11), darkT);
    COL_BG_NAV         = lerpColor(rgba(255, 255, 255), rgba(9, 13, 21), darkT);
    COL_BG_CARD        = lerpColor(rgba(255, 255, 255), rgba(11, 16, 26), darkT);
    COL_BG_SUBTLE      = lerpColor(rgba(244, 246, 250), rgba(16, 23, 38), darkT);

    // 30% Secondary Surfaces & Hairline Borders: Soft Ice Blue <-> Midnight Blue-Black
    COL_BG_CARD_ALT    = lerpColor(rgba(239, 246, 255), rgba(13, 25, 48), darkT);
    COL_BG_INPUT       = lerpColor(rgba(244, 246, 250), rgba(8, 12, 20), darkT);
    COL_BG_INPUT_FOCUS = lerpColor(rgba(255, 255, 255), rgba(15, 22, 36), darkT);
    COL_BORDER         = lerpColor(rgba(226, 232, 240), rgba(28, 39, 56), darkT);
    COL_BORDER_ALT     = lerpColor(rgba(191, 219, 254), rgba(30, 58, 138), darkT);
    COL_BORDER_FOCUS   = lerpColor(rgba(37, 99, 235),   rgba(59, 130, 246), darkT);

    // 10% Signature Accent: Royal Blue (#2563EB) <-> Electric Blue (#3B82F6)
    COL_PRIMARY_RED    = lerpColor(rgba(37, 99, 235),   rgba(59, 130, 246), darkT);
    COL_PRIMARY_RED_HV = lerpColor(rgba(29, 78, 216),   rgba(96, 165, 250), darkT);
    COL_SEC_BTN_BG     = lerpColor(rgba(241, 245, 249), rgba(18, 26, 41), darkT);
    COL_SEC_BTN_HV     = lerpColor(rgba(224, 236, 254), rgba(24, 38, 76), darkT);

    // Typography Hierarchy (>= 4.5:1 WCAG contrast in both modes)
    COL_TEXT_PRIMARY   = lerpColor(rgba(15, 23, 42),    rgba(248, 250, 252), darkT);
    COL_TEXT_SECONDARY = lerpColor(rgba(71, 85, 105),   rgba(148, 163, 184), darkT);
    COL_TEXT_MUTED     = lerpColor(rgba(130, 144, 165), rgba(100, 116, 139), darkT);
    COL_TEXT_ACCENT    = lerpColor(rgba(29, 78, 216),   rgba(96, 165, 250), darkT);
    COL_TEXT_ON_ACCENT = rgba(255, 255, 255);
    COL_STAGE_BG       = lerpColor(rgba(226, 232, 240), rgba(3, 5, 8), darkT);
}

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), w.data(), len);
    return w;
}

} // namespace

AeroDeskWindow::AeroDeskWindow(IdentityManager& identity, NetworkEngine& network)
    : identity_(identity)
    , network_(network)
{
    relayServerEdit_ = identity_.relayServerAddress();

    const auto& s = identity_.settings();
    themeAnimT_ = s.darkTheme ? 1.0f : 0.0f;
    scaleMode_ = static_cast<ScaleMode>(std::clamp<int>(s.defaultScaleMode, 0, 2));
    updateActivePalette(themeAnimT_);

    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    qpcFreq_ = freq.QuadPart;
    lastQpcCounter_ = now.QuadPart;
}

AeroDeskWindow::~AeroDeskWindow() {
    releaseGraphics();
}

void AeroDeskWindow::applyWindowThemeAttribute() {
    if (!hwnd_) return;
    BOOL dark = identity_.settings().darkTheme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD cornerPref = 2; // DWMWCP_ROUND (native rounded window corners)
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
}

void AeroDeskWindow::switchTab(ActiveTab newTab) {
    if (activeTab_ == newTab) return;
    if (activeTab_ == ActiveTab::RemoteSession && newTab != ActiveTab::RemoteSession) {
        network_.sendReleaseAllModifiers();
    }
    activeTab_ = newTab;
    tabEnterStaggerT_ = 0.0f;
    tabEnterStaggerVel_ = 0.0f;
    if (newTab == ActiveTab::RemoteSession) {
        focusedField_ = FocusedField::RemoteCanvas;
    } else if (newTab == ActiveTab::Dashboard && focusedField_ == FocusedField::RemoteCanvas) {
        focusedField_ = FocusedField::RemoteId;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool AeroDeskWindow::create(HINSTANCE hInstance, int nCmdShow) {
    if (!initGraphics()) {
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &AeroDeskWindow::WndProcStatic;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!appIcon) appIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.lpszClassName = L"AeroDeskMainWindowClass";
    RegisterClassExW(&wc);

    std::string title = "AeroDesk";
    if (identity_.instanceId() > 1) {
        title += " #" + std::to_string(identity_.instanceId());
    }
    std::wstring wTitle = utf8ToWide(title);

    int offset = (identity_.instanceId() - 1) * 44;
    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        wTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        100 + offset,
        70 + offset,
        1220,
        760,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd_) {
        return false;
    }

    applyWindowThemeAttribute();
    DragAcceptFiles(hwnd_, TRUE);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    SetTimer(hwnd_, 1, 32, nullptr);
    return true;
}

int AeroDeskWindow::messageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool AeroDeskWindow::initGraphics() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
    if (FAILED(hr) || !d2dFactory_) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwriteFactory_)
    );
    if (FAILED(hr) || !dwriteFactory_) return false;

    auto createFmt = [&](const wchar_t* font, DWRITE_FONT_WEIGHT weight, float size, IDWriteTextFormat** outFmt) {
        dwriteFactory_->CreateTextFormat(
            font, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"en-us", outFmt
        );
        if (*outFmt) {
            (*outFmt)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    };

    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 33.0f, &fmtHeroId_);
    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 18.5f, &fmtHeading_);
    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.5f, &fmtSubheading_);
    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 13.0f, &fmtBody_);
    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f, &fmtBodyBold_);
    createFmt(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, &fmtSmall_);
    createFmt(L"Consolas", DWRITE_FONT_WEIGHT_BOLD, 14.0f, &fmtMono_);

    return true;
}

void AeroDeskWindow::discardDeviceResources() {
    if (remoteBitmap_) { remoteBitmap_->Release(); remoteBitmap_ = nullptr; }
    if (solidBrush_) { solidBrush_->Release(); solidBrush_ = nullptr; }
    if (renderTarget_) { renderTarget_->Release(); renderTarget_ = nullptr; }
    bitmapW_ = 0;
    bitmapH_ = 0;
}

void AeroDeskWindow::releaseGraphics() {
    discardDeviceResources();
    if (fmtHeroId_) { fmtHeroId_->Release(); fmtHeroId_ = nullptr; }
    if (fmtHeading_) { fmtHeading_->Release(); fmtHeading_ = nullptr; }
    if (fmtSubheading_) { fmtSubheading_->Release(); fmtSubheading_ = nullptr; }
    if (fmtBody_) { fmtBody_->Release(); fmtBody_ = nullptr; }
    if (fmtBodyBold_) { fmtBodyBold_->Release(); fmtBodyBold_ = nullptr; }
    if (fmtSmall_) { fmtSmall_->Release(); fmtSmall_ = nullptr; }
    if (fmtMono_) { fmtMono_->Release(); fmtMono_ = nullptr; }
    if (dwriteFactory_) { dwriteFactory_->Release(); dwriteFactory_ = nullptr; }
    if (d2dFactory_) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
}

bool AeroDeskWindow::stepAnimations(float dt) {
    bool active = false;
    animTimeSec_ += dt;

    // 1. macOS Spring Viewport & Stagger Choreography
    float targetViewport = (activeTab_ == ActiveTab::Dashboard) ? 0.0f :
                           (activeTab_ == ActiveTab::RemoteSession) ? 1.0f : 2.0f;
    if (stepSpring(viewportPos_, viewportVel_, targetViewport, 25.0f, 0.82f, dt)) active = true;
    if (stepSpring(tabEnterStaggerT_, tabEnterStaggerVel_, 1.0f, 22.0f, 0.78f, dt)) active = true;

    // 2. Liquid macOS Segmented Control Pill (leading edge moves faster than trailing edge)
    if (navPillInit_) {
        bool movingRight = (targetPillLeft_ >= navPillLeft_);
        float omegaL = movingRight ? 24.0f : 32.0f;
        float omegaR = movingRight ? 32.0f : 24.0f;
        if (stepSpring(navPillLeft_, navPillVelL_, targetPillLeft_, omegaL, 0.78f, dt, 0.15f)) active = true;
        if (stepSpring(navPillRight_, navPillVelR_, targetPillRight_, omegaR, 0.78f, dt, 0.15f)) active = true;
    }

    // 3. Smooth Light <-> Dark Mode Transition
    float wantTheme = identity_.settings().darkTheme ? 1.0f : 0.0f;
    if (stepSpring(themeAnimT_, themeAnimVel_, wantTheme, 20.0f, 0.92f, dt)) active = true;

    // 4. Floating Side Sheet Drawer Spring
    float targetDrawer = showFileDrawer_ ? 1.0f : 0.0f;
    if (stepSpring(drawerAnimT_, drawerAnimVel_, targetDrawer, 26.0f, 0.80f, dt)) active = true;

    // 5. Sheet Modal Spring (with subtle macOS pop overshoot)
    bool modalNow = network_.pendingIncomingRequest().active;
    float targetModal = modalNow ? 1.0f : 0.0f;
    if (stepSpring(modalAnimT_, modalAnimVel_, targetModal, 28.0f, 0.74f, dt)) active = true;

    // 6. Dynamic Island / Capsule Toast Spring
    bool toastVisible = (!toastText_.empty() && GetTickCount64() <= toastExpireTick_);
    float targetToast = toastVisible ? 1.0f : 0.0f;
    if (stepSpring(toastAnimT_, toastAnimVel_, targetToast, 28.0f, 0.72f, dt)) active = true;

    // 7. Per-widget macOS tactile hover & press springs
    for (auto& kv : widgetAnims_) {
        const std::string& id = kv.first;
        WidgetAnimState& st = kv.second;

        float wantHover = (id == hoveredWidgetId_) ? 1.0f : 0.0f;
        float wantPress = (mouseLeftDown_ && id == pressedWidgetId_ && id == hoveredWidgetId_) ? 1.0f : 0.0f;

        if (stepSpring(st.hoverT, st.hoverVel, wantHover, 28.0f, 0.84f, dt)) active = true;
        if (stepSpring(st.pressT, st.pressVel, wantPress, 36.0f, 0.70f, dt)) active = true;

        if (st.rippleT < 1.0f) {
            st.rippleT = std::min(1.0f, st.rippleT + dt * 3.4f);
            active = true;
        }
    }

    return active;
}

LRESULT CALLBACK AeroDeskWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AeroDeskWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AeroDeskWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<AeroDeskWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AeroDeskWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (hoveredIsTextInput_) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                } else if (!hoveredWidgetId_.empty()) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
                return TRUE;
            }
            break;
        }
        case WM_MOUSELEAVE: {
            mouseInsideClient_ = false;
            trackingMouseLeave_ = false;
            hoveredWidgetId_.clear();
            hoveredIsTextInput_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_KILLFOCUS: {
            mouseLeftDown_ = false;
            pressedWidgetId_.clear();
            network_.sendReleaseAllModifiers();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE: {
            if (renderTarget_) {
                RECT rc{};
                GetClientRect(hwnd_, &rc);
                D2D1_SIZE_U sz = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
                renderTarget_->Resize(sz);
            }
            navPillInit_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER: {
            uint64_t tickNow = GetTickCount64();
            auto vStats = network_.viewerStats();
            if (vStats.state == ViewerConnectionState::Connected &&
                prevViewerState_ != ViewerConnectionState::Connected) {
                switchTab(ActiveTab::RemoteSession);
                scaleMode_ = static_cast<ScaleMode>(std::clamp<int>(identity_.settings().defaultScaleMode, 0, 2));
                if (vStats.remoteAddress.find("127.0.0.1") != std::string::npos ||
                    vStats.remoteDeskId == identity_.deskId()) {
                    remoteInputEnabled_ = false;
                    showToast("Connected on same PC. View-Only enabled (F8 to toggle).");
                } else {
                    remoteInputEnabled_ = true;
                    showToast("Connected to " + vStats.remoteHostname);
                }
            } else if (vStats.state == ViewerConnectionState::Error &&
                       prevViewerState_ != ViewerConnectionState::Error) {
                showToast(vStats.statusMessage, true);
                switchTab(ActiveTab::Dashboard);
            }
            prevViewerState_ = vStats.state;

            uint32_t unreadChat = network_.unreadChatCount();
            if (showFileDrawer_ && drawerTab_ == DrawerTab::LiveChat && unreadChat > 0) {
                network_.markChatRead();
                unreadChat = 0;
            }
            if (unreadChat > lastSeenUnreadChat_) {
                auto msgs = network_.chatMessages();
                if (!msgs.empty() && !msgs.back().fromLocal) {
                    showToast(msgs.back().senderName + ": " + msgs.back().text);
                }
            }
            lastSeenUnreadChat_ = unreadChat;

            auto pending = network_.pendingIncomingRequest();
            if (pending.active && !modalWasActive_) {
                modalPermissions_ = identity_.settings().defaultPermissions;
                SetForegroundWindow(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            modalWasActive_ = pending.active;

            bool toastActive = (!toastText_.empty() && tickNow <= toastExpireTick_ + 500);
            bool inLiveSession = (vStats.state != ViewerConnectionState::Disconnected) ||
                                 network_.hostSessionStatus().active ||
                                 pending.active;

            static uint32_t idleTickCounter = 0;
            ++idleTickCounter;
            if (inLiveSession || toastActive || (idleTickCounter % 4 == 0)) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            onPaint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE:
            onMouseMove(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_LBUTTONDOWN:
            SetFocus(hwnd_);
            onMouseButton(MouseButtonId::Left, true, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_LBUTTONUP:
            onMouseButton(MouseButtonId::Left, false, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_RBUTTONDOWN:
            onMouseButton(MouseButtonId::Right, true, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_RBUTTONUP:
            onMouseButton(MouseButtonId::Right, false, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_MBUTTONDOWN:
            onMouseButton(MouseButtonId::Middle, true, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_MBUTTONUP:
            onMouseButton(MouseButtonId::Middle, false, static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
            return 0;

        case WM_MOUSEWHEEL:
            onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_CHAR:
            onCharInput(static_cast<wchar_t>(wParam));
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            uint16_t vk = static_cast<uint16_t>(wParam);
            uint16_t scan = static_cast<uint16_t>((lParam >> 16) & 0xFF);
            bool isExt = (lParam & (1 << 24)) != 0;
            onKeyEvent(vk, scan, true, isExt);
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            uint16_t vk = static_cast<uint16_t>(wParam);
            uint16_t scan = static_cast<uint16_t>((lParam >> 16) & 0xFF);
            bool isExt = (lParam & (1 << 24)) != 0;
            onKeyEvent(vk, scan, false, isExt);
            return 0;
        }
        case WM_DROPFILES:
            onDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---------------- Primitive Drawing, Vector Icons & macOS Surface Helpers ----------------

void AeroDeskWindow::drawCardShadow(const UiRect& r, float radius, float intensity) {
    if (!renderTarget_ || !solidBrush_ || intensity <= 0.01f) return;
    float shadowScale = 1.0f + 2.4f * std::clamp(themeAnimT_, 0.0f, 1.0f);
    fillRoundRect(r.offset(0.0f, 5.0f).inflate(2.5f, 2.5f), radius + 2.5f, rgba(5, 8, 18, 0.022f * shadowScale * intensity));
    fillRoundRect(r.offset(0.0f, 2.5f).inflate(1.0f, 1.0f), radius + 1.0f, rgba(5, 8, 18, 0.032f * shadowScale * intensity));
    fillRoundRect(r.offset(0.0f, 1.0f), radius, rgba(5, 8, 18, 0.028f * shadowScale * intensity));
}

void AeroDeskWindow::drawCardSurface(const UiRect& r, float radius, float alpha, bool /*accentHeader*/) {
    drawCardShadow(r, radius, alpha);
    fillRoundRect(r, radius, withAlpha(COL_BG_CARD, alpha));
    strokeRoundRect(r, radius, withAlpha(COL_BORDER, alpha), 1.0f);

    // Subtle macOS top inner specular highlight
    float specAlpha = lerpColor(rgba(255, 255, 255, 0.75f), rgba(255, 255, 255, 0.05f), themeAnimT_).a * alpha;
    fillRoundRect({ r.left + 14.0f, r.top + 0.8f, r.right - 14.0f, r.top + 1.8f }, 0.5f, rgba(255, 255, 255, specAlpha));
}

void AeroDeskWindow::drawPulseDot(float cx, float cy, float baseRadius, D2D1_COLOR_F color, float alpha) {
    if (!renderTarget_ || !solidBrush_ || alpha <= 0.01f) return;
    float wave = 0.5f + 0.5f * std::sin(animTimeSec_ * 2.2f);
    float haloRadius = baseRadius + 1.2f + 2.2f * wave;
    float haloAlpha = (0.20f - 0.12f * wave) * alpha;

    solidBrush_->SetColor(withAlpha(color, haloAlpha));
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), haloRadius, haloRadius), solidBrush_);

    solidBrush_->SetColor(withAlpha(color, alpha));
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), baseRadius, baseRadius), solidBrush_);
}

void AeroDeskWindow::drawSparkline(const UiRect& r, const float* values, size_t count, float maxVal, D2D1_COLOR_F color, float alpha) {
    if (!renderTarget_ || !solidBrush_ || count < 2 || maxVal <= 0.001f) return;
    fillRoundRect(r, 4.0f, withAlpha(COL_BG_SUBTLE, alpha * 0.85f));
    strokeRoundRect(r, 4.0f, withAlpha(COL_BORDER, alpha * 0.85f), 1.0f);

    solidBrush_->SetColor(withAlpha(color, alpha));
    float stepX = (r.width() - 6.0f) / static_cast<float>(count - 1);
    float innerH = r.height() - 6.0f;

    for (size_t i = 0; i + 1 < count; ++i) {
        float n0 = std::clamp(values[i] / maxVal, 0.0f, 1.0f);
        float n1 = std::clamp(values[i + 1] / maxVal, 0.0f, 1.0f);
        D2D1_POINT_2F p0 = D2D1::Point2F(r.left + 3.0f + i * stepX, r.bottom - 3.0f - n0 * innerH);
        D2D1_POINT_2F p1 = D2D1::Point2F(r.left + 3.0f + (i + 1) * stepX, r.bottom - 3.0f - n1 * innerH);
        renderTarget_->DrawLine(p0, p1, solidBrush_, 1.5f);
    }
}

void AeroDeskWindow::drawIconStar(float cx, float cy, float radius, bool filled, D2D1_COLOR_F color) {
    if (!renderTarget_ || !solidBrush_ || !d2dFactory_) return;
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geo)) || !geo) return;

    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geo->Open(&sink)) && sink) {
        constexpr float PI = 3.14159265f;
        float innerR = radius * 0.42f;
        D2D1_POINT_2F pts[10];
        for (int i = 0; i < 10; ++i) {
            float angle = -PI * 0.5f + i * (PI / 5.0f);
            float r = (i % 2 == 0) ? radius : innerR;
            pts[i] = D2D1::Point2F(cx + r * std::cos(angle), cy + r * std::sin(angle));
        }
        sink->BeginFigure(pts[0], filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(&pts[1], 9);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();

        solidBrush_->SetColor(color);
        if (filled) {
            renderTarget_->FillGeometry(geo, solidBrush_);
        }
        renderTarget_->DrawGeometry(geo, solidBrush_, 1.3f);
    }
    geo->Release();
}

void AeroDeskWindow::drawIconClose(float cx, float cy, float halfSize, D2D1_COLOR_F color, float strokeWidth) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    renderTarget_->DrawLine(D2D1::Point2F(cx - halfSize, cy - halfSize), D2D1::Point2F(cx + halfSize, cy + halfSize), solidBrush_, strokeWidth);
    renderTarget_->DrawLine(D2D1::Point2F(cx + halfSize, cy - halfSize), D2D1::Point2F(cx - halfSize, cy + halfSize), solidBrush_, strokeWidth);
}

void AeroDeskWindow::drawIconTheme(float cx, float cy, float radius, bool isDark, D2D1_COLOR_F color) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    if (!isDark) {
        // Sun icon (circle + 8 rays)
        float coreR = radius * 0.52f;
        renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), coreR, coreR), solidBrush_, 1.5f);
        constexpr float PI = 3.14159265f;
        for (int i = 0; i < 8; ++i) {
            float a = i * (PI * 0.25f);
            float r0 = radius * 0.74f;
            float r1 = radius * 1.02f;
            renderTarget_->DrawLine(
                D2D1::Point2F(cx + r0 * std::cos(a), cy + r0 * std::sin(a)),
                D2D1::Point2F(cx + r1 * std::cos(a), cy + r1 * std::sin(a)),
                solidBrush_, 1.4f
            );
        }
    } else {
        // Crescent indicator (outer ring + inner offset circle)
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.78f, radius * 0.78f), solidBrush_);
        solidBrush_->SetColor(COL_SEC_BTN_BG);
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + radius * 0.32f, cy - radius * 0.24f), radius * 0.62f, radius * 0.62f), solidBrush_);
    }
}

void AeroDeskWindow::fillRoundRect(const UiRect& r, float radius, D2D1_COLOR_F color) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius);
    renderTarget_->FillRoundedRectangle(rr, solidBrush_);
}

void AeroDeskWindow::strokeRoundRect(const UiRect& r, float radius, D2D1_COLOR_F color, float strokeWidth) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius);
    renderTarget_->DrawRoundedRectangle(rr, solidBrush_, strokeWidth);
}

void AeroDeskWindow::drawText(
    const std::string& utf8,
    const UiRect& r,
    IDWriteTextFormat* fmt,
    D2D1_COLOR_F color,
    DWRITE_TEXT_ALIGNMENT hAlign,
    DWRITE_PARAGRAPH_ALIGNMENT vAlign)
{
    if (!renderTarget_ || !solidBrush_ || !fmt || utf8.empty()) return;
    fmt->SetTextAlignment(hAlign);
    fmt->SetParagraphAlignment(vAlign);
    solidBrush_->SetColor(color);
    std::wstring w = utf8ToWide(utf8);
    D2D1_RECT_F dr = D2D1::RectF(r.left, r.top, r.right, r.bottom);
    renderTarget_->DrawText(w.c_str(), static_cast<UINT32>(w.size()), fmt, dr, solidBrush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void AeroDeskWindow::drawButton(
    const std::string& id,
    const UiRect& r,
    const std::string& label,
    D2D1_COLOR_F bgColor,
    D2D1_COLOR_F hoverColor,
    D2D1_COLOR_F textColor,
    float radius,
    std::function<void()> onClick,
    IDWriteTextFormat* fmt,
    bool hasBorder,
    D2D1_COLOR_F borderColor,
    D2D1_COLOR_F hoverTextColor)
{
    WidgetAnimState& anim = widgetAnims_[id];

    // Symmetrical macOS tactile spring scale (hover expands 1.2%, press squishes 3.5%)
    float hT = std::clamp(anim.hoverT, 0.0f, 1.2f);
    float pT = std::clamp(anim.pressT, -0.25f, 1.25f);
    float scaleDelta = (0.012f * hT) - (0.036f * pT);
    float dx = r.width() * scaleDelta * 0.5f;
    float dy = r.height() * scaleDelta * 0.5f;
    float liftY = (-0.8f * hT) + (0.8f * pT);
    UiRect animRect = r.inflate(dx, dy).offset(0.0f, liftY);

    if (hT > 0.02f && bgColor.a > 0.05f) {
        fillRoundRect(animRect.offset(0.0f, 2.0f), radius, withAlpha(COL_PRIMARY_RED, 0.12f * hT * std::max(0.0f, 1.0f - pT)));
    }

    D2D1_COLOR_F curBg = lerpColor(bgColor, hoverColor, std::clamp(hT, 0.0f, 1.0f));
    if (pT > 0.01f) {
        curBg = lerpColor(curBg, rgba(15, 23, 42, curBg.a), 0.12f * std::clamp(pT, 0.0f, 1.0f));
    }
    if (curBg.a > 0.005f) {
        fillRoundRect(animRect, radius, curBg);
    }

    if (hasBorder) {
        strokeRoundRect(animRect, radius, lerpColor(borderColor, COL_BORDER_FOCUS, std::clamp(hT, 0.0f, 1.0f) * 0.55f), 1.0f);
    }

    D2D1_COLOR_F targetTxtCol = (hoverTextColor.a >= 0.0f) ? hoverTextColor : textColor;
    D2D1_COLOR_F curTxtCol = lerpColor(textColor, targetTxtCol, std::clamp(hT, 0.0f, 1.0f));

    if (!label.empty()) {
        drawText(label, animRect, fmt ? fmt : fmtBodyBold_, curTxtCol,
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (onClick) {
        clickRegions_.push_back({ r, id, std::move(onClick), false });
    }
}

void AeroDeskWindow::drawTextField(
    const std::string& id,
    FocusedField fieldType,
    const UiRect& r,
    const std::string& value,
    const std::string& placeholder,
    bool maskPassword)
{
    WidgetAnimState& anim = widgetAnims_[id];
    bool focused = (focusedField_ == fieldType);

    float targetFocus = focused ? 1.0f : 0.0f;
    if (stepSpring(anim.focusT, anim.focusVel, targetFocus, 28.0f, 0.80f, lastDt_)) {
        inlineAnimActive_ = true;
    }

    float fT = std::clamp(anim.focusT, 0.0f, 1.2f);
    if (fT > 0.01f) {
        UiRect glowR = r.inflate(2.8f * fT, 2.8f * fT);
        fillRoundRect(glowR, 11.5f, withAlpha(COL_PRIMARY_RED, 0.15f * std::clamp(fT, 0.0f, 1.0f)));
    }

    D2D1_COLOR_F bg = lerpColor(COL_BG_INPUT, COL_BG_INPUT_FOCUS, std::max(std::clamp(fT, 0.0f, 1.0f), std::clamp(anim.hoverT, 0.0f, 1.0f) * 0.5f));
    fillRoundRect(r, 9.0f, bg);

    D2D1_COLOR_F bdr = lerpColor(
        lerpColor(COL_BORDER, COL_BORDER_ALT, std::clamp(anim.hoverT, 0.0f, 1.0f) * 0.7f),
        COL_BORDER_FOCUS,
        std::clamp(fT, 0.0f, 1.0f)
    );
    strokeRoundRect(r, 9.0f, bdr, 1.0f + 0.6f * std::clamp(fT, 0.0f, 1.0f));

    UiRect textR = { r.left + 13.0f, r.top + 2.0f, r.right - 13.0f, r.bottom - 2.0f };
    if (value.empty() && !focused) {
        drawText(placeholder, textR, fmtBody_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_LEADING);
    } else {
        std::string display = maskPassword ? std::string(value.size(), '*') : value;
        if (focused) {
            float caretWave = 0.5f + 0.5f * std::sin(animTimeSec_ * 5.8f);
            if (caretWave > 0.35f) {
                display.push_back('|');
            }
        }
        drawText(display, textR, fmtMono_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    clickRegions_.push_back({ r, id, [this, fieldType]() {
        focusedField_ = fieldType;
    }, true });
}

void AeroDeskWindow::drawToggleSwitch(
    const std::string& id,
    const UiRect& r,
    bool checked,
    const std::string& label,
    std::function<void()> onToggle)
{
    WidgetAnimState& anim = widgetAnims_[id];
    float targetToggle = checked ? 1.0f : 0.0f;
    if (!anim.toggleInitialized) {
        anim.toggleT = targetToggle;
        anim.toggleVel = 0.0f;
        anim.toggleInitialized = true;
    } else {
        if (stepSpring(anim.toggleT, anim.toggleVel, targetToggle, 28.0f, 0.74f, lastDt_)) {
            inlineAnimActive_ = true;
        }
    }

    // macOS System Settings style: label on the left, capsule switch on the right
    float swW = 40.0f;
    float swH = 22.0f;
    float swTop = r.top + (r.height() - swH) * 0.5f;
    UiRect pill = { r.right - swW, swTop, r.right, swTop + swH };

    UiRect lblRect = { r.left, r.top, pill.left - 12.0f, r.bottom };
    float hT = std::clamp(anim.hoverT, 0.0f, 1.0f);
    drawText(label, lblRect, fmtBody_, lerpColor(COL_TEXT_PRIMARY, COL_TEXT_ACCENT, hT * 0.55f), DWRITE_TEXT_ALIGNMENT_LEADING);

    float tClamped = std::clamp(anim.toggleT, 0.0f, 1.0f);
    D2D1_COLOR_F offBase = lerpColor(rgba(203, 213, 225), rgba(30, 41, 59), themeAnimT_);
    D2D1_COLOR_F offHover = lerpColor(rgba(165, 180, 200), rgba(51, 65, 85), themeAnimT_);
    D2D1_COLOR_F offCol = lerpColor(offBase, offHover, hT);
    D2D1_COLOR_F onCol  = lerpColor(COL_PRIMARY_RED, COL_PRIMARY_RED_HV, hT);
    fillRoundRect(pill, swH * 0.5f, lerpColor(offCol, onCol, tClamped));

    // macOS liquid thumb stretch when pressed or moving at high velocity
    float knobR = 8.4f;
    float stretch = (1.8f * std::clamp(anim.pressT, 0.0f, 1.0f)) + std::min(2.6f, std::fabs(anim.toggleVel) * 0.32f);
    float knobLeftX  = pill.left + 11.0f;
    float knobRightX = pill.right - 11.0f;
    float knobCx = knobLeftX + (knobRightX - knobLeftX) * anim.toggleT;
    float knobCy = swTop + swH * 0.5f;

    UiRect knobRect = { knobCx - knobR - stretch * 0.5f, knobCy - knobR, knobCx + knobR + stretch * 0.5f, knobCy + knobR };
    fillRoundRect(knobRect.offset(0.0f, 1.2f), knobR, rgba(15, 23, 42, 0.18f));
    fillRoundRect(knobRect, knobR, rgba(255, 255, 255));

    if (onToggle) {
        clickRegions_.push_back({ r, id, std::move(onToggle), false });
    }
}

// ---------------- Main Paint Orchestrator ----------------

void AeroDeskWindow::onPaint() {
    if (!d2dFactory_) return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = (qpcFreq_ > 0)
        ? static_cast<float>(now.QuadPart - lastQpcCounter_) / static_cast<float>(qpcFreq_)
        : 0.016f;
    lastQpcCounter_ = now.QuadPart;
    lastDt_ = std::clamp(dt, 0.001f, 0.04f);
    inlineAnimActive_ = false;
    bool animMoving = stepAnimations(lastDt_);

    updateActivePalette(smoothStepEase(themeAnimT_));

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    float width = static_cast<float>(std::max<LONG>(1, rc.right - rc.left));
    float height = static_cast<float>(std::max<LONG>(1, rc.bottom - rc.top));

    if (!renderTarget_) {
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.dpiX = 96.0f;
        rtProps.dpiY = 96.0f;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height))
        );
        if (FAILED(d2dFactory_->CreateHwndRenderTarget(rtProps, hwndProps, &renderTarget_)) || !renderTarget_) {
            return;
        }
        renderTarget_->CreateSolidColorBrush(COL_TEXT_PRIMARY, &solidBrush_);
    }

    clickRegions_.clear();
    renderTarget_->BeginDraw();
    renderTarget_->Clear(COL_BG_MAIN);

    float topOffset = 0.0f;
    drawTopNavBar(width, topOffset);

    UiRect contentBounds = { 0.0f, topOffset, width, height };

    // macOS Spring Viewport Glide + Scale Choreography
    float targetIdx = (activeTab_ == ActiveTab::Dashboard) ? 0.0f :
                      (activeTab_ == ActiveTab::RemoteSession) ? 1.0f : 2.0f;
    float slideX = (targetIdx - viewportPos_) * 42.0f;
    float viewScale = 0.982f + 0.018f * std::clamp(tabEnterStaggerT_, 0.0f, 1.08f);
    float enterAlpha = std::clamp(0.30f + 0.70f * smoothStepEase(tabEnterStaggerT_), 0.0f, 1.0f);

    if (std::fabs(slideX) > 0.15f || std::fabs(viewScale - 1.0f) > 0.001f) {
        D2D1_POINT_2F center = D2D1::Point2F(contentBounds.centerX(), contentBounds.centerY());
        renderTarget_->SetTransform(
            D2D1::Matrix3x2F::Scale(viewScale, viewScale, center) *
            D2D1::Matrix3x2F::Translation(slideX, 0.0f)
        );
    }

    if (activeTab_ == ActiveTab::Dashboard) {
        drawDashboardView(contentBounds, enterAlpha);
    } else if (activeTab_ == ActiveTab::RemoteSession) {
        drawRemoteSessionView(contentBounds, enterAlpha);
    } else {
        drawSettingsView(contentBounds, enterAlpha);
    }

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());

    // Floating macOS Side Sheet Drawer
    if (drawerAnimT_ > 0.004f) {
        float scrimAlpha = std::clamp(drawerAnimT_, 0.0f, 1.0f) * 0.18f;
        fillRoundRect(contentBounds, 0.0f, rgba(5, 8, 15, scrimAlpha));

        float drawerW = std::min(395.0f, width * 0.42f);
        UiRect drawerBounds = { width - drawerW - 14.0f, topOffset + 12.0f, width - 14.0f, height - 14.0f };
        drawFileTransferDrawer(drawerBounds, drawerAnimT_);
    }

    // macOS Sheet Modal with spring overshoot
    if (modalAnimT_ > 0.004f) {
        drawIncomingApprovalModal(width, height, modalAnimT_);
    }

    // Floating Capsule Toast
    if (toastAnimT_ > 0.004f) {
        drawToastBanner(width, height, toastAnimT_);
    }

    // Live hover detection
    std::string newHoverId;
    bool newIsText = false;
    for (auto it = clickRegions_.rbegin(); it != clickRegions_.rend(); ++it) {
        if (it->rect.contains(mouseX_, mouseY_)) {
            newHoverId = it->id;
            newIsText = it->isTextInput;
            break;
        }
    }
    hoveredWidgetId_ = newHoverId;
    hoveredIsTextInput_ = newIsText;

    HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
    }

    if (animMoving || inlineAnimActive_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------- macOS Unified Top Toolbar ----------------

void AeroDeskWindow::drawTopNavBar(float width, float& outTopOffset) {
    float navH = 58.0f;
    outTopOffset = navH;

    UiRect navRect = { 0.0f, 0.0f, width, navH };
    fillRoundRect(navRect, 0.0f, COL_BG_NAV);
    fillRoundRect({ 0.0f, navH - 1.0f, width, navH }, 0.0f, COL_BORDER);

    // Left: Minimal Squircle Brand Badge + Online Dot
    UiRect logoBadge = { 20.0f, 13.0f, 52.0f, 45.0f };
    fillRoundRect(logoBadge.offset(0.0f, 2.0f), 9.5f, withAlpha(COL_PRIMARY_RED, 0.22f));
    fillRoundRect(logoBadge, 9.5f, COL_PRIMARY_RED);
    drawText("AD", logoBadge, fmtBodyBold_, COL_TEXT_ON_ACCENT, DWRITE_TEXT_ALIGNMENT_CENTER);

    std::string brandTitle = "AeroDesk";
    if (identity_.instanceId() > 1) {
        brandTitle += " #" + std::to_string(identity_.instanceId());
    }
    drawText(brandTitle, { 62.0f, 10.0f, 200.0f, 31.0f }, fmtSubheading_, COL_TEXT_PRIMARY);

    drawPulseDot(67.0f, 39.5f, 3.4f, COL_SUCCESS);
    drawText("Online", { 76.0f, 30.0f, 180.0f, 48.0f }, fmtSmall_, COL_TEXT_SECONDARY);

    // Center: Dead-Center macOS Segmented Control Track + Liquid Spring Pill
    auto vStats = network_.viewerStats();
    bool hasSession = (vStats.state != ViewerConnectionState::Disconnected);

    float dashW = 108.0f;
    float sessW = hasSession ? 172.0f : 0.0f;
    float settW = 102.0f;
    float totalTabsW = dashW + (hasSession ? (sessW + 4.0f) : 0.0f) + 4.0f + settW;
    float tabStartX = std::max(195.0f, (width - totalTabsW) * 0.5f);

    UiRect dashTab = { tabStartX, 12.0f, tabStartX + dashW, 46.0f };
    UiRect sessTab = hasSession
        ? UiRect{ dashTab.right + 4.0f, 12.0f, dashTab.right + 4.0f + sessW, 46.0f }
        : UiRect{ dashTab.right, 12.0f, dashTab.right, 46.0f };
    float settLeft = (hasSession ? sessTab.right : dashTab.right) + 4.0f;
    UiRect settTab = { settLeft, 12.0f, settLeft + settW, 46.0f };

    UiRect activeTargetRect = (activeTab_ == ActiveTab::Dashboard) ? dashTab :
                              (activeTab_ == ActiveTab::RemoteSession && hasSession) ? sessTab : settTab;
    targetPillLeft_ = activeTargetRect.left;
    targetPillRight_ = activeTargetRect.right;
    if (!navPillInit_) {
        navPillLeft_ = targetPillLeft_;
        navPillRight_ = targetPillRight_;
        navPillVelL_ = 0.0f;
        navPillVelR_ = 0.0f;
        navPillInit_ = true;
    }

    UiRect trackRect = { dashTab.left - 3.5f, 8.5f, settTab.right + 3.5f, 49.5f };
    fillRoundRect(trackRect, 11.0f, COL_SEC_BTN_BG);
    strokeRoundRect(trackRect, 11.0f, COL_BORDER, 1.0f);

    UiRect slidingPill = { navPillLeft_, 12.0f, navPillRight_, 46.0f };
    fillRoundRect(slidingPill.offset(0.0f, 1.8f), 8.5f, withAlpha(COL_PRIMARY_RED, 0.24f));
    fillRoundRect(slidingPill, 8.5f, COL_PRIMARY_RED);

    bool onDash = (activeTab_ == ActiveTab::Dashboard);
    drawButton("tab_dash", dashTab, "Dashboard",
               rgba(255, 255, 255, 0.0f),
               onDash ? rgba(255, 255, 255, 0.08f) : COL_SEC_BTN_HV,
               onDash ? COL_TEXT_ON_ACCENT : COL_TEXT_SECONDARY,
               8.5f, [this]() {
                   switchTab(ActiveTab::Dashboard);
               }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0),
               onDash ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    if (hasSession) {
        std::string sessLabel = (vStats.remoteDeskId > 0)
            ? ("Session • " + CryptoUtils::formatDeskId(vStats.remoteDeskId))
            : "Session";
        bool onSess = (activeTab_ == ActiveTab::RemoteSession);
        drawButton("tab_session", sessTab, sessLabel,
                   rgba(255, 255, 255, 0.0f),
                   onSess ? rgba(255, 255, 255, 0.08f) : COL_SEC_BTN_HV,
                   onSess ? COL_TEXT_ON_ACCENT : COL_TEXT_SECONDARY,
                   8.5f, [this]() {
                       switchTab(ActiveTab::RemoteSession);
                   }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0),
                   onSess ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
    }

    bool onSett = (activeTab_ == ActiveTab::Settings);
    drawButton("tab_settings", settTab, "Settings",
               rgba(255, 255, 255, 0.0f),
               onSett ? rgba(255, 255, 255, 0.08f) : COL_SEC_BTN_HV,
               onSett ? COL_TEXT_ON_ACCENT : COL_TEXT_SECONDARY,
               8.5f, [this]() {
                   switchTab(ActiveTab::Settings);
               }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0),
               onSett ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    // Right: Minimal Theme Icon Pill + Files & Chat Pill
    bool isDark = identity_.settings().darkTheme;
    UiRect themeBtn = { width - 56.0f, 12.0f, width - 20.0f, 46.0f };
    drawButton("btn_nav_theme", themeBtn, "",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               9.0f, [this]() {
                   AppSettings s = identity_.settings();
                   s.darkTheme = !s.darkTheme;
                   identity_.updateSettings(s);
                   applyWindowThemeAttribute();
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    drawIconTheme(themeBtn.centerX(), themeBtn.centerY(), 7.5f, isDark, COL_TEXT_PRIMARY);

    auto transfers = network_.fileTransferManager().snapshotTransfers();
    uint32_t unreadChat = network_.unreadChatCount();
    std::string fileBtnLabel = "Files & Chat";
    if (unreadChat > 0) {
        fileBtnLabel += " (" + std::to_string(unreadChat) + ")";
    } else if (!transfers.empty()) {
        fileBtnLabel += " (" + std::to_string(transfers.size()) + ")";
    }
    UiRect filesBtn = { themeBtn.left - 126.0f, 12.0f, themeBtn.left - 8.0f, 46.0f };
    drawButton("btn_drawer", filesBtn, fileBtnLabel,
               (showFileDrawer_ || unreadChat > 0) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (showFileDrawer_ || unreadChat > 0) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (showFileDrawer_ || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               9.0f, [this, unreadChat]() {
                   if (!showFileDrawer_ && unreadChat > 0) {
                       drawerTab_ = DrawerTab::LiveChat;
                       network_.markChatRead();
                   }
                   showFileDrawer_ = !showFileDrawer_;
               }, fmtSmall_, !(showFileDrawer_ || unreadChat > 0), COL_BORDER,
               (showFileDrawer_ || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
}

// ---------------- Minimalistic macOS Dashboard View ----------------

void AeroDeskWindow::drawDashboardView(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f) return;

    float s1 = std::clamp(tabEnterStaggerT_, 0.0f, 1.15f);
    float s2 = std::clamp((tabEnterStaggerT_ - 0.06f) * 1.12f, 0.0f, 1.15f);
    float s3 = std::clamp((tabEnterStaggerT_ - 0.12f) * 1.15f, 0.0f, 1.15f);

    float stagger1 = (1.0f - s1) * 14.0f;
    float stagger2 = (1.0f - s2) * 16.0f;
    float stagger3 = (1.0f - s3) * 18.0f;

    float pad = 24.0f;
    float totalW = bounds.width() - pad * 2.0f;
    float leftW = std::clamp(totalW * 0.39f, 380.0f, 456.0f);

    UiRect leftCol = UiRect{ bounds.left + pad, bounds.top + pad, bounds.left + pad + leftW, bounds.bottom - pad }.offset(0.0f, stagger1);
    UiRect rightCol = { leftCol.right + pad, bounds.top + pad, bounds.right - pad, bounds.bottom - pad };

    // ========== LEFT COLUMN: THIS DESK ==========
    drawCardSurface(leftCol, 16.0f, alpha);

    float lx = leftCol.left + 24.0f;
    float rx = leftCol.right - 24.0f;
    float curY = leftCol.top + 22.0f;

    drawText("This Desk", { lx, curY, rx, curY + 26.0f }, fmtHeading_, withAlpha(COL_TEXT_PRIMARY, alpha));
    curY += 26.0f;
    drawText("Share your ID and code to allow remote access.",
             { lx, curY, rx, curY + 20.0f }, fmtSmall_, withAlpha(COL_TEXT_SECONDARY, alpha));
    curY += 28.0f;

    // Hero Desk ID Card
    UiRect idBox = { lx, curY, rx, curY + 86.0f };
    fillRoundRect(idBox, 13.0f, withAlpha(COL_BG_CARD_ALT, alpha));
    strokeRoundRect(idBox, 13.0f, withAlpha(COL_BORDER_ALT, alpha), 1.2f);

    drawText("YOUR DESK ID", { idBox.left + 18.0f, idBox.top + 10.0f, idBox.right - 18.0f, idBox.top + 24.0f },
             fmtSmall_, withAlpha(COL_TEXT_ACCENT, alpha));
    drawText(identity_.formattedDeskId(), { idBox.left + 18.0f, idBox.top + 26.0f, idBox.right - 108.0f, idBox.bottom - 8.0f },
             fmtHeroId_, withAlpha(COL_PRIMARY_RED, alpha));

    UiRect copyIdBtn = { idBox.right - 98.0f, idBox.top + 25.0f, idBox.right - 16.0f, idBox.bottom - 17.0f };
    drawButton("btn_copy_id", copyIdBtn, "Copy ID",
               COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 8.5f, [this]() {
                   ClipboardManager::setClipboardUtf8(identity_.formattedDeskId());
                   showToast("Copied Desk ID");
               }, fmtSmall_);

    curY = idBox.bottom + 16.0f;

    // macOS Inset Group: Access & Password
    UiRect unattBox = { lx, curY, rx, curY + 166.0f };
    fillRoundRect(unattBox, 13.0f, withAlpha(COL_BG_SUBTLE, alpha));
    strokeRoundRect(unattBox, 13.0f, withAlpha(COL_BORDER, alpha), 1.0f);

    float ux = unattBox.left + 16.0f;
    float urx = unattBox.right - 16.0f;
    drawToggleSwitch("toggle_unattended", { ux, unattBox.top + 10.0f, urx, unattBox.top + 38.0f },
                     identity_.unattendedEnabled(), "Allow Password Access", [this]() {
                         identity_.setUnattendedEnabled(!identity_.unattendedEnabled());
                     });

    fillRoundRect({ ux, unattBox.top + 42.0f, urx, unattBox.top + 43.0f }, 0.0f, withAlpha(COL_BORDER, 0.7f * alpha));

    // Session Code Row
    float codeY = unattBox.top + 50.0f;
    drawText("Session Code", { ux, codeY, ux + 96.0f, codeY + 32.0f }, fmtBody_, withAlpha(COL_TEXT_SECONDARY, alpha));
    UiRect codeBadge = { ux + 100.0f, codeY, urx - 134.0f, codeY + 32.0f };
    fillRoundRect(codeBadge, 7.5f, COL_BG_CARD);
    strokeRoundRect(codeBadge, 7.5f, COL_BORDER);
    drawText(identity_.sessionCode(), codeBadge, fmtMono_, COL_PRIMARY_RED, DWRITE_TEXT_ALIGNMENT_CENTER);

    UiRect copyCodeBtn = { codeBadge.right + 6.0f, codeY, codeBadge.right + 66.0f, codeY + 32.0f };
    drawButton("btn_copy_code", copyCodeBtn, "Copy",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   ClipboardManager::setClipboardUtf8(identity_.sessionCode());
                   showToast("Copied session code");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    UiRect regenCodeBtn = { copyCodeBtn.right + 6.0f, codeY, urx, codeY + 32.0f };
    drawButton("btn_regen_code", regenCodeBtn, "New",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   identity_.regenerateSessionCode();
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    fillRoundRect({ ux, codeY + 40.0f, urx, codeY + 41.0f }, 0.0f, withAlpha(COL_BORDER, 0.7f * alpha));

    // Unattended Password Row
    float passY = codeY + 49.0f;
    UiRect passField = { ux, passY, urx - 134.0f, passY + 36.0f };
    drawTextField("field_local_pass", FocusedField::LocalPassword, passField,
                  localPasswordEdit_, "Unattended password...", !showLocalPassword_);

    UiRect showPassBtn = { passField.right + 6.0f, passField.top, passField.right + 66.0f, passField.bottom };
    drawButton("btn_show_local_pass", showPassBtn, showLocalPassword_ ? "Hide" : "Show",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   showLocalPassword_ = !showLocalPassword_;
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    UiRect savePassBtn = { showPassBtn.right + 6.0f, passField.top, urx, passField.bottom };
    drawButton("btn_save_local_pass", savePassBtn, "Save",
               COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 7.5f, [this]() {
                   if (!localPasswordEdit_.empty()) {
                       identity_.setUnattendedPassword(localPasswordEdit_);
                       showToast("Password saved");
                   } else {
                       showToast("Enter a password first", true);
                   }
               }, fmtSmall_);

    curY = unattBox.bottom + 16.0f;

    // Host Session Status Box
    auto hStatus = network_.hostSessionStatus();
    UiRect hostBox = { lx, curY, rx, leftCol.bottom - 22.0f };
    fillRoundRect(hostBox, 13.0f, withAlpha(hStatus.active ? COL_BG_CARD_ALT : COL_BG_SUBTLE, alpha));
    strokeRoundRect(hostBox, 13.0f, withAlpha(hStatus.active ? COL_BORDER_ALT : COL_BORDER, alpha), 1.0f);

    float hx = hostBox.left + 18.0f;
    float hrx = hostBox.right - 18.0f;
    float hy = hostBox.top + 14.0f;

    if (hStatus.active) {
        drawPulseDot(hx + 5.0f, hy + 10.0f, 4.0f, COL_PRIMARY_RED, alpha);
        drawText("ACTIVE SESSION", { hx + 16.0f, hy, hrx, hy + 20.0f }, fmtSmall_, COL_PRIMARY_RED);
        hy += 22.0f;
        std::string who = hStatus.viewerHostname + " (" + CryptoUtils::formatDeskId(hStatus.viewerDeskId) + ")";
        drawText(who, { hx, hy, hrx, hy + 24.0f }, fmtSubheading_, COL_TEXT_PRIMARY);
        hy += 26.0f;

        uint8_t perms = hStatus.permissions;
        drawToggleSwitch("perm_host_input", { hx, hy, hrx, hy + 24.0f }, (perms & PERM_INPUT) != 0,
                         "Mouse & Keyboard", [this, perms]() {
                             network_.updateHostSessionPermissions(perms ^ PERM_INPUT);
                         });
        hy += 28.0f;
        drawToggleSwitch("perm_host_clip", { hx, hy, hrx, hy + 24.0f }, (perms & PERM_CLIPBOARD) != 0,
                         "Clipboard", [this, perms]() {
                             network_.updateHostSessionPermissions(perms ^ PERM_CLIPBOARD);
                         });
        hy += 28.0f;
        drawToggleSwitch("perm_host_file", { hx, hy, hrx, hy + 24.0f }, (perms & PERM_FILE_TRANSFER) != 0,
                         "File Transfer", [this, perms]() {
                             network_.updateHostSessionPermissions(perms ^ PERM_FILE_TRANSFER);
                         });
        hy += 30.0f;

        if (hy + 32.0f <= hostBox.bottom - 8.0f) {
            UiRect discHostBtn = { hx, hy, hrx, hy + 32.0f };
            drawButton("btn_disc_host", discHostBtn, "Disconnect",
                       COL_DANGER, COL_DANGER_HV, COL_TEXT_ON_ACCENT, 8.0f, [this]() {
                           network_.disconnectHostClient();
                           showToast("Disconnected viewer");
                       }, fmtSmall_);
        }
    } else {
        float midY = hostBox.centerY();
        drawPulseDot(hx + 6.0f, midY - 10.0f, 4.2f, COL_SUCCESS, alpha);
        drawText("Ready for connections", { hx + 18.0f, midY - 22.0f, hrx, midY + 2.0f }, fmtSubheading_, COL_TEXT_PRIMARY);
        drawText("Your desktop is ready to share.", { hx + 18.0f, midY + 2.0f, hrx, midY + 24.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    }

    // ========== RIGHT COLUMN: REMOTE DESK & SAVED/NEARBY DESKS ==========
    float topCardH = 148.0f;
    UiRect connectCard = UiRect{ rightCol.left, rightCol.top, rightCol.right, rightCol.top + topCardH }.offset(0.0f, stagger2);
    drawCardSurface(connectCard, 16.0f, alpha);

    float cx = connectCard.left + 24.0f;
    float crx = connectCard.right - 24.0f;
    float cy = connectCard.top + 20.0f;

    drawText("Remote Desk", { cx, cy, crx, cy + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    cy += 25.0f;
    drawText("Enter a Desk ID to connect to another computer.",
             { cx, cy, crx, cy + 20.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    cy += 28.0f;

    float totalInputW = crx - cx;
    float idW = totalInputW * 0.44f;
    float pwW = totalInputW * 0.34f;

    UiRect remoteIdField = { cx, cy, cx + idW, cy + 42.0f };
    drawTextField("field_remote_id", FocusedField::RemoteId, remoteIdField,
                  remoteIdInput_, "Enter 9-digit ID", false);

    UiRect remotePwField = { remoteIdField.right + 10.0f, cy, remoteIdField.right + 10.0f + pwW, cy + 42.0f };
    drawTextField("field_remote_pw", FocusedField::RemotePassword, remotePwField,
                  remotePasswordInput_, "Password or code", !showRemotePassword_);

    UiRect connectBtn = { remotePwField.right + 10.0f, cy, crx, cy + 42.0f };
    drawButton("btn_connect", connectBtn, "Connect",
               COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 9.0f, [this]() {
                   initiateConnection();
               });

    // ========== SAVED & NEARBY DESKS GRID ==========
    UiRect peersCard = UiRect{ rightCol.left, connectCard.bottom + 18.0f - stagger2, rightCol.right, rightCol.bottom }.offset(0.0f, stagger3);
    drawCardSurface(peersCard, 16.0f, alpha);

    float px = peersCard.left + 24.0f;
    float prx = peersCard.right - 24.0f;
    float py = peersCard.top + 20.0f;

    auto discovered = network_.discoveredPeers();
    auto recents = identity_.recentSessions();

    drawText("Saved & Nearby Desks", { px, py, prx - 110.0f, py + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    UiRect refreshBtn = { prx - 92.0f, py - 2.0f, prx, py + 28.0f };
    drawButton("btn_refresh_lan", refreshBtn, "Refresh",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   network_.sendDiscoveryQuery(0);
                   showToast("Refreshing nearby desks...");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    py += 38.0f;

    struct PeerCardItem {
        uint64_t    deskId;
        std::string hostname;
        std::string endpoint;
        bool        isLive;
        bool        isFavorite;
        bool        isFromRecent;
    };
    std::vector<PeerCardItem> cardItems;

    for (const auto& r : recents) {
        if (!r.isFavorite) continue;
        bool liveNow = false;
        std::string ep = r.address;
        for (const auto& d : discovered) {
            if (d.deskId == r.deskId) {
                liveNow = true;
                ep = d.ip + ":" + std::to_string(d.port);
                break;
            }
        }
        cardItems.push_back({ r.deskId, r.hostname, ep, liveNow, true, true });
    }

    for (const auto& d : discovered) {
        bool already = false;
        for (const auto& c : cardItems) {
            if (c.deskId == d.deskId) { already = true; break; }
        }
        if (!already) {
            cardItems.push_back({ d.deskId, d.hostname, d.ip + ":" + std::to_string(d.port), true, false, false });
        }
    }

    for (const auto& r : recents) {
        bool already = false;
        for (const auto& c : cardItems) {
            if (r.deskId > 0 && c.deskId == r.deskId) { already = true; break; }
        }
        if (!already) {
            cardItems.push_back({ r.deskId, r.hostname, r.address, false, r.isFavorite, true });
        }
    }

    if (cardItems.empty()) {
        UiRect emptyBox = { px, py + 4.0f, prx, peersCard.bottom - 24.0f };
        fillRoundRect(emptyBox, 13.0f, COL_BG_SUBTLE);
        strokeRoundRect(emptyBox, 13.0f, COL_BORDER);

        float cyEmpty = emptyBox.centerY();
        drawPulseDot(emptyBox.centerX(), cyEmpty - 24.0f, 5.5f, COL_PRIMARY_RED, alpha);
        drawText("No desks found nearby",
                 { emptyBox.left + 24.0f, cyEmpty - 8.0f, emptyBox.right - 24.0f, cyEmpty + 16.0f },
                 fmtSubheading_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_CENTER);
        drawText("Computers on your local network will appear here automatically.",
                 { emptyBox.left + 24.0f, cyEmpty + 18.0f, emptyBox.right - 24.0f, cyEmpty + 42.0f },
                 fmtSmall_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        int cols = 2;
        float gap = 14.0f;
        float cardW = (prx - px - gap) / 2.0f;
        float cardH = 92.0f;

        for (size_t i = 0; i < cardItems.size(); ++i) {
            int row = static_cast<int>(i) / cols;
            int col = static_cast<int>(i) % cols;
            float itemLeft = px + col * (cardW + gap);
            float itemTop = py + row * (cardH + gap);
            if (itemTop + cardH > peersCard.bottom - 16.0f) break;

            std::string cardBtnId = "peer_conn_" + std::to_string(i);
            float cardHover = std::clamp(widgetAnims_[cardBtnId].hoverT, 0.0f, 1.15f);

            UiRect cardR = UiRect{ itemLeft, itemTop, itemLeft + cardW, itemTop + cardH }
                .inflate(cardW * 0.006f * cardHover, cardH * 0.006f * cardHover)
                .offset(0.0f, -1.8f * cardHover);
            drawCardShadow(cardR, 12.0f, 0.4f + 0.6f * cardHover);
            fillRoundRect(cardR, 12.0f, lerpColor(COL_BG_SUBTLE, COL_BG_CARD, 0.5f + 0.5f * std::clamp(cardHover, 0.0f, 1.0f)));
            strokeRoundRect(cardR, 12.0f, lerpColor(cardItems[i].isFavorite ? COL_BORDER_ALT : COL_BORDER, COL_PRIMARY_RED, std::clamp(cardHover, 0.0f, 1.0f) * 0.6f), 1.0f);

            if (cardItems[i].isLive) {
                drawPulseDot(cardR.left + 19.0f, cardR.top + 18.0f, 3.4f, COL_SUCCESS, alpha);
                std::string badge = cardItems[i].isFavorite ? "FAVORITE • ONLINE" : "ONLINE";
                drawText(badge, { cardR.left + 28.0f, cardR.top + 10.0f, cardR.right - 72.0f, cardR.top + 26.0f },
                         fmtSmall_, cardItems[i].isFavorite ? COL_PRIMARY_RED : COL_TEXT_SECONDARY);
            } else {
                std::string badge = cardItems[i].isFavorite ? "FAVORITE" : "RECENT";
                drawText(badge, { cardR.left + 14.0f, cardR.top + 10.0f, cardR.right - 72.0f, cardR.top + 26.0f },
                         fmtSmall_, cardItems[i].isFavorite ? COL_PRIMARY_RED : COL_TEXT_MUTED);
            }

            uint64_t peerId = cardItems[i].deskId;
            std::string peerHost = cardItems[i].hostname;
            std::string peerEp = cardItems[i].endpoint;

            // Vector Star Favorite button
            if (peerId > 0) {
                UiRect favBtn = { cardR.right - 64.0f, cardR.top + 7.0f, cardR.right - 38.0f, cardR.top + 27.0f };
                drawButton("peer_fav_" + std::to_string(i), favBtn, "",
                           COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_SECONDARY,
                           6.0f, [this, peerId, peerHost, peerEp]() {
                               identity_.addOrUpdateRecentSession(peerId, peerHost, peerEp);
                               identity_.toggleFavoriteSession(peerId);
                           }, fmtSmall_);
                drawIconStar(favBtn.centerX(), favBtn.centerY(), 5.8f, cardItems[i].isFavorite,
                             cardItems[i].isFavorite ? COL_PRIMARY_RED : COL_TEXT_SECONDARY);
            }

            // Vector Close button for recent items
            if (cardItems[i].isFromRecent && peerId > 0) {
                std::string delId = "peer_del_" + std::to_string(i);
                UiRect delBtn = { cardR.right - 34.0f, cardR.top + 7.0f, cardR.right - 10.0f, cardR.top + 27.0f };
                drawButton(delId, delBtn, "",
                           COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY,
                           6.0f, [this, peerId]() {
                               identity_.removeRecentSession(peerId);
                           }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);
                float delHover = std::clamp(widgetAnims_[delId].hoverT, 0.0f, 1.0f);
                drawIconClose(delBtn.centerX(), delBtn.centerY(), 3.6f,
                              lerpColor(COL_TEXT_SECONDARY, COL_TEXT_ON_ACCENT, delHover), 1.5f);
            }

            std::string idFormatted = (peerId > 0)
                ? CryptoUtils::formatDeskId(peerId)
                : peerEp;
            drawText(idFormatted, { cardR.left + 14.0f, cardR.top + 28.0f, cardR.right - 108.0f, cardR.top + 54.0f },
                     fmtSubheading_, lerpColor(COL_TEXT_PRIMARY, COL_PRIMARY_RED, std::clamp(cardHover, 0.0f, 1.0f) * 0.7f));

            std::string subInfo = peerHost.empty() ? peerEp : peerHost;
            drawText(subInfo, { cardR.left + 14.0f, cardR.top + 54.0f, cardR.right - 108.0f, cardR.bottom - 10.0f },
                     fmtSmall_, COL_TEXT_SECONDARY);

            std::string targetStr = (peerId > 0) ? CryptoUtils::formatDeskId(peerId) : peerEp;
            UiRect quickConnBtn = { cardR.right - 98.0f, cardR.top + 35.0f, cardR.right - 12.0f, cardR.bottom - 15.0f };
            drawButton(cardBtnId, quickConnBtn, "Connect",
                       COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 8.0f, [this, targetStr]() {
                           remoteIdInput_ = targetStr;
                           initiateConnection();
                       }, fmtSmall_);
        }
    }
}

// ---------------- Remote Session View ----------------

void AeroDeskWindow::drawRemoteSessionView(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f) return;

    auto stats = network_.viewerStats();
    const auto& appSett = identity_.settings();

    float barH = 48.0f;
    UiRect hudBar = { bounds.left, bounds.top, bounds.right, bounds.top + barH };
    fillRoundRect(hudBar, 0.0f, COL_BG_CARD);
    fillRoundRect({ hudBar.left, hudBar.bottom - 1.0f, hudBar.right, hudBar.bottom }, 0.0f, COL_BORDER);

    float bx = hudBar.left + 18.0f;
    std::string peerTitle = stats.remoteHostname.empty()
        ? CryptoUtils::formatDeskId(stats.remoteDeskId)
        : stats.remoteHostname;
    drawText(peerTitle, { bx, hudBar.top, bx + 170.0f, hudBar.bottom }, fmtBodyBold_, COL_TEXT_PRIMARY);
    bx += 174.0f;

    drawPulseDot(bx + 4.0f, hudBar.centerY(), 3.6f, COL_SUCCESS, alpha);
    if (appSett.showSessionHud) {
        char fpsBuf[48];
        std::snprintf(fpsBuf, sizeof(fpsBuf), "Connected • %.0f FPS", stats.fps);
        drawText(fpsBuf, { bx + 13.0f, hudBar.top, bx + 165.0f, hudBar.bottom }, fmtSmall_, COL_TEXT_SECONDARY);
    } else {
        drawText("Connected", { bx + 13.0f, hudBar.top, bx + 110.0f, hudBar.bottom }, fmtSmall_, COL_TEXT_SECONDARY);
    }

    // Right-aligned session controls
    float rx = hudBar.right - 14.0f;

    UiRect discBtn = { rx - 88.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_disconnect", discBtn, "Disconnect",
               COL_DANGER, COL_DANGER_HV, COL_TEXT_ON_ACCENT, 7.5f, [this]() {
                   network_.disconnectViewer();
                   switchTab(ActiveTab::Dashboard);
                   showToast("Disconnected");
               }, fmtSmall_);
    rx = discBtn.left - 6.0f;

    UiRect fsBtn = { rx - 84.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_fullscreen", fsBtn, isFullscreen_ ? "Exit (F11)" : "Fullscreen",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   toggleFullscreen();
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    rx = fsBtn.left - 6.0f;

    UiRect shotBtn = { rx - 80.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_screenshot", shotBtn, "Screenshot",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   saveRemoteScreenshot();
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    rx = shotBtn.left - 6.0f;

    UiRect taskBtn = { rx - 72.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_taskmgr", taskBtn, "Task Mgr",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   network_.sendSystemAction(SystemActionType::TaskManager);
                   showToast("Opened Task Manager");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    rx = taskBtn.left - 6.0f;

    std::string scaleLabel = (scaleMode_ == ScaleMode::FitAspect) ? "Scale: Fit" :
                             (scaleMode_ == ScaleMode::Stretch) ? "Scale: Stretch" : "Scale: 1:1";
    UiRect scaleBtn = { rx - 88.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_scale", scaleBtn, scaleLabel,
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                   if (scaleMode_ == ScaleMode::FitAspect) scaleMode_ = ScaleMode::Stretch;
                   else if (scaleMode_ == ScaleMode::Stretch) scaleMode_ = ScaleMode::Original;
                   else scaleMode_ = ScaleMode::FitAspect;
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    rx = scaleBtn.left - 6.0f;

    std::string qualLabel = (stats.qualityPreset == QualityPreset::Ultra) ? "Quality: High" :
                            (stats.qualityPreset == QualityPreset::Balanced) ? "Quality: Bal" : "Quality: Fast";
    UiRect qualBtn = { rx - 94.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_quality", qualBtn, qualLabel,
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this, stats]() {
                   QualityPreset nextQ = (stats.qualityPreset == QualityPreset::Ultra) ? QualityPreset::Balanced :
                                         (stats.qualityPreset == QualityPreset::Balanced) ? QualityPreset::LowBandwidth :
                                         QualityPreset::Ultra;
                   network_.requestVideoSettings(nextQ, stats.activeMonitorIndex, true, stats.targetFps, stats.adaptiveFps ? 1 : 0);
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
    rx = qualBtn.left - 6.0f;

    if (stats.monitorCount > 1) {
        std::string monLabel = "Display " + std::to_string(stats.activeMonitorIndex + 1);
        UiRect monBtn = { rx - 78.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_monitor", monBtn, monLabel,
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this, stats]() {
                       int nextMon = (stats.activeMonitorIndex + 1) % std::max(1, stats.monitorCount);
                       network_.requestVideoSettings(stats.qualityPreset, nextMon, true, stats.targetFps, stats.adaptiveFps ? 1 : 0);
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        rx = monBtn.left - 6.0f;
    }

    bool canControl = (stats.grantedPermissions & PERM_INPUT) != 0;
    bool inputActive = canControl && remoteInputEnabled_;
    std::string inputLabel = !canControl ? "View Only" :
                             (inputActive ? "Control: ON" : "View Only");
    UiRect inputBtn = { rx - 98.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
    drawButton("sess_input_toggle", inputBtn, inputLabel,
               inputActive ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               inputActive ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               inputActive ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               7.5f, [this, canControl]() {
                   if (canControl) {
                       remoteInputEnabled_ = !remoteInputEnabled_;
                       if (!remoteInputEnabled_) network_.sendReleaseAllModifiers();
                       showToast(remoteInputEnabled_ ? "Control enabled" : "View-only mode");
                   } else {
                       showToast("Remote control disabled by host", true);
                   }
               }, fmtSmall_, !inputActive, COL_BORDER,
               inputActive ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    // Remote Desktop Canvas Stage
    UiRect stageRect = { bounds.left, hudBar.bottom, bounds.right, bounds.bottom };
    fillRoundRect(stageRect, 0.0f, COL_STAGE_BG);

    if (network_.copyLatestViewerFrame(displayedFrameSeq_, frameBufferBgra_, frameBufferW_, frameBufferH_, remoteCursor_)) {
        if (frameBufferW_ > 0 && frameBufferH_ > 0 && renderTarget_) {
            if (!remoteBitmap_ || bitmapW_ != frameBufferW_ || bitmapH_ != frameBufferH_) {
                if (remoteBitmap_) { remoteBitmap_->Release(); remoteBitmap_ = nullptr; }
                D2D1_BITMAP_PROPERTIES bprops = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
                );
                renderTarget_->CreateBitmap(
                    D2D1::SizeU(static_cast<UINT32>(frameBufferW_), static_cast<UINT32>(frameBufferH_)),
                    frameBufferBgra_.data(),
                    static_cast<UINT32>(frameBufferW_ * 4),
                    &bprops,
                    &remoteBitmap_
                );
                bitmapW_ = frameBufferW_;
                bitmapH_ = frameBufferH_;
            } else {
                D2D1_RECT_U dstU = D2D1::RectU(0, 0, static_cast<UINT32>(frameBufferW_), static_cast<UINT32>(frameBufferH_));
                remoteBitmap_->CopyFromMemory(&dstU, frameBufferBgra_.data(), static_cast<UINT32>(frameBufferW_ * 4));
            }
        }
    }

    if (remoteBitmap_ && bitmapW_ > 0 && bitmapH_ > 0) {
        float availW = stageRect.width() - 24.0f;
        float availH = stageRect.height() - 24.0f;
        float drawW = availW;
        float drawH = availH;

        if (scaleMode_ == ScaleMode::FitAspect) {
            float scale = std::min(availW / bitmapW_, availH / bitmapH_);
            drawW = bitmapW_ * scale;
            drawH = bitmapH_ * scale;
        } else if (scaleMode_ == ScaleMode::Original) {
            drawW = std::min(availW, static_cast<float>(bitmapW_));
            drawH = std::min(availH, static_cast<float>(bitmapH_));
        }

        float left = stageRect.left + (stageRect.width() - drawW) * 0.5f;
        float top  = stageRect.top + (stageRect.height() - drawH) * 0.5f;
        renderedCanvasRect_ = { left, top, left + drawW, top + drawH };

        drawCardShadow(renderedCanvasRect_, 8.0f, alpha);
        strokeRoundRect(renderedCanvasRect_.inflate(1.2f, 1.2f), 6.0f, COL_BORDER_ALT, 1.2f);

        D2D1_RECT_F dRect = D2D1::RectF(renderedCanvasRect_.left, renderedCanvasRect_.top,
                                        renderedCanvasRect_.right, renderedCanvasRect_.bottom);
        renderTarget_->DrawBitmap(remoteBitmap_, dRect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

        if (remoteCursor_.visible && appSett.showRemoteCursor) {
            float curX = renderedCanvasRect_.left + remoteCursor_.normX * renderedCanvasRect_.width();
            float curY = renderedCanvasRect_.top + remoteCursor_.normY * renderedCanvasRect_.height();
            solidBrush_->SetColor(withAlpha(COL_PRIMARY_RED, 0.92f));
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(curX, curY), 5.0f, 5.0f), solidBrush_);
            solidBrush_->SetColor(rgba(255, 255, 255, 0.98f));
            renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(curX, curY), 6.0f, 6.0f), solidBrush_, 1.6f);
        }
    } else {
        renderedCanvasRect_ = {};
        drawText(stats.statusMessage, stageRect, fmtSubheading_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// ---------------- macOS System Settings View ----------------

void AeroDeskWindow::drawSettingsView(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f) return;

    float sL = std::clamp(tabEnterStaggerT_, 0.0f, 1.15f);
    float sR = std::clamp((tabEnterStaggerT_ - 0.05f) * 1.12f, 0.0f, 1.15f);
    float staggerL = (1.0f - sL) * 12.0f;
    float staggerR = (1.0f - sR) * 14.0f;

    float pad = 24.0f;
    float totalW = bounds.width() - pad * 2.0f;
    float colW = (totalW - pad) * 0.5f;

    UiRect leftCard  = UiRect{ bounds.left + pad, bounds.top + pad, bounds.left + pad + colW, bounds.bottom - pad }.offset(0.0f, staggerL);
    UiRect rightCard = UiRect{ leftCard.right + pad, bounds.top + pad, bounds.right - pad, bounds.bottom - pad }.offset(0.0f, staggerR);

    const AppSettings s = identity_.settings();

    // ==================== LEFT COLUMN: APPEARANCE & DISPLAY ====================
    drawCardSurface(leftCard, 16.0f, alpha);

    float lx = leftCard.left + 24.0f;
    float lrx = leftCard.right - 24.0f;
    float ly = leftCard.top + 20.0f;
    float innerW = lrx - lx;

    drawText("Appearance & Display", { lx, ly, lrx, ly + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    ly += 25.0f;
    drawText("Theme, frame rate, and display preferences.",
             { lx, ly, lrx, ly + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    ly += 26.0f;

    // 1. Theme Segmented Box
    UiRect themeBox = { lx, ly, lrx, ly + 82.0f };
    fillRoundRect(themeBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(themeBox, 12.0f, COL_BORDER);

    drawText("THEME", { themeBox.left + 16.0f, themeBox.top + 8.0f, themeBox.right - 16.0f, themeBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);
    float halfBtnW = (innerW - 32.0f - 10.0f) * 0.5f;
    UiRect lightBtn = { themeBox.left + 16.0f, themeBox.top + 32.0f, themeBox.left + 16.0f + halfBtnW, themeBox.bottom - 10.0f };
    UiRect darkBtn  = { lightBtn.right + 10.0f, themeBox.top + 32.0f, themeBox.right - 16.0f, themeBox.bottom - 10.0f };

    drawButton("sett_theme_light", lightBtn, "Light",
               !s.darkTheme ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               !s.darkTheme ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               !s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   AppSettings ns = identity_.settings();
                   ns.darkTheme = false;
                   identity_.updateSettings(ns);
                   applyWindowThemeAttribute();
               }, fmtSmall_, s.darkTheme, COL_BORDER, !s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_theme_dark", darkBtn, "Dark",
               s.darkTheme ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               s.darkTheme ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   AppSettings ns = identity_.settings();
                   ns.darkTheme = true;
                   identity_.updateSettings(ns);
                   applyWindowThemeAttribute();
               }, fmtSmall_, !s.darkTheme, COL_BORDER, s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ly = themeBox.bottom + 14.0f;

    // 2. Frame Rate
    UiRect fpsBox = { lx, ly, lrx, ly + 118.0f };
    fillRoundRect(fpsBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(fpsBox, 12.0f, COL_BORDER);

    drawText("FRAME RATE",
             { fpsBox.left + 16.0f, fpsBox.top + 8.0f, fpsBox.right - 16.0f, fpsBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float thirdW = (innerW - 32.0f - 16.0f) / 3.0f;
    uint8_t curFps = clampTargetFps(s.targetFps);
    UiRect fps15Btn = { fpsBox.left + 16.0f, fpsBox.top + 30.0f, fpsBox.left + 16.0f + thirdW, fpsBox.top + 66.0f };
    UiRect fps30Btn = { fps15Btn.right + 8.0f, fpsBox.top + 30.0f, fps15Btn.right + 8.0f + thirdW, fpsBox.top + 66.0f };
    UiRect fps60Btn = { fps30Btn.right + 8.0f, fpsBox.top + 30.0f, fpsBox.right - 16.0f, fpsBox.top + 66.0f };

    auto setFpsAction = [this](uint8_t fpsVal) {
        AppSettings ns = identity_.settings();
        ns.targetFps = fpsVal;
        identity_.updateSettings(ns);
        network_.setSessionFpsConfig(fpsVal, ns.adaptiveFps);
    };

    drawButton("sett_fps_15", fps15Btn, "15 FPS",
               (curFps == 15) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (curFps == 15) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (curFps == 15) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(15); }, fmtSmall_, curFps != 15, COL_BORDER,
               (curFps == 15) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_fps_30", fps30Btn, "30 FPS",
               (curFps == 30) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (curFps == 30) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (curFps == 30) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(30); }, fmtSmall_, curFps != 30, COL_BORDER,
               (curFps == 30) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_fps_60", fps60Btn, "60 FPS",
               (curFps == 60) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (curFps == 60) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (curFps == 60) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(60); }, fmtSmall_, curFps != 60, COL_BORDER,
               (curFps == 60) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawToggleSwitch("sett_adaptive_fps", { fpsBox.left + 16.0f, fpsBox.top + 78.0f, fpsBox.right - 16.0f, fpsBox.top + 106.0f },
                     s.adaptiveFps, "Adjust automatically on slow connections", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.adaptiveFps = !ns.adaptiveFps;
                         identity_.updateSettings(ns);
                         network_.setSessionFpsConfig(ns.targetFps, ns.adaptiveFps);
                     });

    ly = fpsBox.bottom + 14.0f;

    // 3. Quality & Scaling
    UiRect qualBox = { lx, ly, lrx, ly + 124.0f };
    fillRoundRect(qualBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(qualBox, 12.0f, COL_BORDER);

    drawText("QUALITY & SCALING", { qualBox.left + 16.0f, qualBox.top + 8.0f, qualBox.right - 16.0f, qualBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    QualityPreset defQ = s.defaultQuality;
    UiRect qUltraBtn = { qualBox.left + 16.0f, qualBox.top + 28.0f, qualBox.left + 16.0f + thirdW, qualBox.top + 62.0f };
    UiRect qBalBtn   = { qUltraBtn.right + 8.0f, qualBox.top + 28.0f, qUltraBtn.right + 8.0f + thirdW, qualBox.top + 62.0f };
    UiRect qFastBtn  = { qBalBtn.right + 8.0f, qualBox.top + 28.0f, qualBox.right - 16.0f, qualBox.top + 62.0f };

    auto setQualAction = [this](QualityPreset qp) {
        AppSettings ns = identity_.settings();
        ns.defaultQuality = qp;
        identity_.updateSettings(ns);
        auto st = network_.viewerStats();
        if (st.state == ViewerConnectionState::Connected) {
            network_.requestVideoSettings(qp, st.activeMonitorIndex, true, ns.targetFps, ns.adaptiveFps ? 1 : 0);
        }
    };

    drawButton("sett_q_ultra", qUltraBtn, "High",
               (defQ == QualityPreset::Ultra) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defQ == QualityPreset::Ultra) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::Ultra) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::Ultra); }, fmtSmall_, defQ != QualityPreset::Ultra, COL_BORDER,
               (defQ == QualityPreset::Ultra) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_q_bal", qBalBtn, "Balanced",
               (defQ == QualityPreset::Balanced) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defQ == QualityPreset::Balanced) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::Balanced) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::Balanced); }, fmtSmall_, defQ != QualityPreset::Balanced, COL_BORDER,
               (defQ == QualityPreset::Balanced) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_q_fast", qFastBtn, "Fast",
               (defQ == QualityPreset::LowBandwidth) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defQ == QualityPreset::LowBandwidth) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::LowBandwidth) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::LowBandwidth); }, fmtSmall_, defQ != QualityPreset::LowBandwidth, COL_BORDER,
               (defQ == QualityPreset::LowBandwidth) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    uint8_t defScale = s.defaultScaleMode;
    UiRect scFitBtn  = { qualBox.left + 16.0f, qualBox.top + 72.0f, qualBox.left + 16.0f + thirdW, qualBox.top + 106.0f };
    UiRect scStrBtn  = { scFitBtn.right + 8.0f, qualBox.top + 72.0f, scFitBtn.right + 8.0f + thirdW, qualBox.top + 106.0f };
    UiRect scOrigBtn = { scStrBtn.right + 8.0f, qualBox.top + 72.0f, qualBox.right - 16.0f, qualBox.top + 106.0f };

    auto setScaleAction = [this](uint8_t scMode) {
        AppSettings ns = identity_.settings();
        ns.defaultScaleMode = scMode;
        identity_.updateSettings(ns);
        scaleMode_ = static_cast<ScaleMode>(scMode);
    };

    drawButton("sett_sc_fit", scFitBtn, "Fit",
               (defScale == 0) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defScale == 0) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defScale == 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(0); }, fmtSmall_, defScale != 0, COL_BORDER,
               (defScale == 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_sc_str", scStrBtn, "Stretch",
               (defScale == 1) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defScale == 1) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defScale == 1) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(1); }, fmtSmall_, defScale != 1, COL_BORDER,
               (defScale == 1) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_sc_orig", scOrigBtn, "Actual Size",
               (defScale == 2) ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               (defScale == 2) ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               (defScale == 2) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(2); }, fmtSmall_, defScale != 2, COL_BORDER,
               (defScale == 2) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ly = qualBox.bottom + 14.0f;

    // 4. Session Display
    UiRect ovBox = { lx, ly, lrx, leftCard.bottom - 20.0f };
    fillRoundRect(ovBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(ovBox, 12.0f, COL_BORDER);

    drawText("SESSION DISPLAY", { ovBox.left + 16.0f, ovBox.top + 8.0f, ovBox.right - 16.0f, ovBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    drawToggleSwitch("sett_show_cursor", { ovBox.left + 16.0f, ovBox.top + 30.0f, ovBox.right - 16.0f, ovBox.top + 56.0f },
                     s.showRemoteCursor, "Show remote cursor", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.showRemoteCursor = !ns.showRemoteCursor;
                         identity_.updateSettings(ns);
                     });

    drawToggleSwitch("sett_show_hud", { ovBox.left + 16.0f, ovBox.top + 62.0f, ovBox.right - 16.0f, ovBox.top + 88.0f },
                     s.showSessionHud, "Show frame rate in session bar", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.showSessionHud = !ns.showSessionHud;
                         identity_.updateSettings(ns);
                     });

    // ==================== RIGHT COLUMN: ACCESS & NETWORK ====================
    drawCardSurface(rightCard, 16.0f, alpha);

    float rx = rightCard.left + 24.0f;
    float rrx = rightCard.right - 24.0f;
    float ry = rightCard.top + 20.0f;

    drawText("Access & Network", { rx, ry, rrx, ry + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    ry += 25.0f;
    drawText("Permissions, privacy, and connection settings.",
             { rx, ry, rrx, ry + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    ry += 26.0f;

    // 1. Permissions
    UiRect permBox = { rx, ry, rrx, ry + 198.0f };
    fillRoundRect(permBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(permBox, 12.0f, COL_BORDER);

    drawText("PERMISSIONS",
             { permBox.left + 16.0f, permBox.top + 8.0f, permBox.right - 16.0f, permBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float py = permBox.top + 30.0f;
    drawToggleSwitch("sett_auto_accept", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 26.0f },
                     s.autoAcceptIncoming, "Automatically accept incoming connections", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.autoAcceptIncoming = !ns.autoAcceptIncoming;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 32.0f;

    drawToggleSwitch("sett_def_perm_input", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 26.0f },
                     (s.defaultPermissions & PERM_INPUT) != 0, "Allow mouse and keyboard control", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_INPUT;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 32.0f;

    drawToggleSwitch("sett_def_perm_clip", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 26.0f },
                     (s.defaultPermissions & PERM_CLIPBOARD) != 0, "Allow clipboard sharing", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_CLIPBOARD;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 32.0f;

    drawToggleSwitch("sett_def_perm_file", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 26.0f },
                     (s.defaultPermissions & PERM_FILE_TRANSFER) != 0, "Allow file transfers", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_FILE_TRANSFER;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 32.0f;

    drawToggleSwitch("sett_lock_disc", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 26.0f },
                     s.lockWorkstationOnDisconnect, "Lock computer when session ends", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.lockWorkstationOnDisconnect = !ns.lockWorkstationOnDisconnect;
                         identity_.updateSettings(ns);
                     });

    ry = permBox.bottom + 14.0f;

    // 2. Network & Relay
    UiRect netBox = { rx, ry, rrx, ry + 92.0f };
    fillRoundRect(netBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(netBox, 12.0f, COL_BORDER);

    drawText("NETWORK",
             { netBox.left + 16.0f, netBox.top + 8.0f, netBox.right - 16.0f, netBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    UiRect settRelayField = { netBox.left + 16.0f, netBox.top + 34.0f, netBox.right - 196.0f, netBox.top + 72.0f };
    drawTextField("field_relay_srv_sett", FocusedField::RelayServer, settRelayField,
                  relayServerEdit_, "Relay server address", false);

    UiRect applyRelayBtn = { settRelayField.right + 8.0f, netBox.top + 34.0f, settRelayField.right + 82.0f, netBox.top + 72.0f };
    drawButton("sett_apply_relay", applyRelayBtn, "Save",
               COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 8.0f, [this]() {
                   identity_.setRelayServerAddress(relayServerEdit_);
                   showToast("Relay server saved");
               }, fmtSmall_);

    bool relayRunning = network_.isLocalRelayRunning();
    UiRect localRelayBtn = { applyRelayBtn.right + 8.0f, netBox.top + 34.0f, netBox.right - 16.0f, netBox.top + 72.0f };
    drawButton("sett_toggle_relay", localRelayBtn, relayRunning ? "Relay: ON" : "Relay: OFF",
               relayRunning ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               relayRunning ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               relayRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this, relayRunning]() {
                   if (relayRunning) {
                       network_.stopLocalRelayServer();
                       showToast("Local relay stopped");
                   } else if (network_.startLocalRelayServer(DEFAULT_RELAY_PORT)) {
                       showToast("Local relay started");
                   } else {
                       showToast("Relay port already in use", true);
                   }
               }, fmtSmall_, !relayRunning, COL_BORDER, relayRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ry = netBox.bottom + 14.0f;

    // 3. Data & Reset Actions
    UiRect maintBox = { rx, ry, rrx, rightCard.bottom - 20.0f };
    fillRoundRect(maintBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(maintBox, 12.0f, COL_BORDER);

    drawText("DATA",
             { maintBox.left + 16.0f, maintBox.top + 8.0f, maintBox.right - 16.0f, maintBox.top + 24.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float mThirdW = (rrx - rx - 32.0f - 16.0f) / 3.0f;
    float btnTop = maintBox.top + 34.0f;
    float btnBot = std::min(btnTop + 38.0f, maintBox.bottom - 10.0f);

    UiRect openRecvBtn = { maintBox.left + 16.0f, btnTop, maintBox.left + 16.0f + mThirdW, btnBot };
    UiRect clearRecBtn = { openRecvBtn.right + 8.0f, btnTop, openRecvBtn.right + 8.0f + mThirdW, btnBot };
    UiRect resetBtn    = { clearRecBtn.right + 8.0f, btnTop, maintBox.right - 16.0f, btnBot };

    drawButton("sett_open_recv", openRecvBtn, "Received Files",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f, [this]() {
                   network_.fileTransferManager().openReceiveDirectoryInExplorer();
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("sett_clear_recents", clearRecBtn, "Clear History",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f, [this]() {
                   identity_.clearRecentSessions();
                   showToast("History cleared");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("sett_reset_defaults", resetBtn, "Reset Settings",
               COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_PRIMARY, 8.0f, [this]() {
                   identity_.resetSettingsToDefault();
                   applyWindowThemeAttribute();
                   scaleMode_ = ScaleMode::FitAspect;
                   network_.setAutoAcceptIncoming(false, PERM_ALL);
                   network_.setSessionFpsConfig(30, true);
                   showToast("Settings restored to defaults");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ON_ACCENT);
}

// ---------------- Floating macOS Side Sheet Drawer ----------------

void AeroDeskWindow::drawFileTransferDrawer(const UiRect& bounds, float slideProgress) {
    float slideOffsetX = (bounds.width() + 24.0f) * (1.0f - slideProgress);
    UiRect r = bounds.offset(slideOffsetX, 0.0f);

    drawCardSurface(r, 16.0f, std::clamp(slideProgress, 0.0f, 1.0f));

    float x = r.left + 20.0f;
    float rx = r.right - 20.0f;
    float y = r.top + 16.0f;

    float halfTabW = (rx - x - 38.0f - 6.0f) * 0.5f;
    UiRect tabFiles = { x, y, x + halfTabW, y + 32.0f };
    UiRect tabChat  = { tabFiles.right + 6.0f, y, tabFiles.right + 6.0f + halfTabW, y + 32.0f };

    bool onFiles = (drawerTab_ == DrawerTab::FilesAndClip);
    drawButton("drawer_tab_files", tabFiles, "Files",
               onFiles ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               onFiles ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               onFiles ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() { drawerTab_ = DrawerTab::FilesAndClip; }, fmtSmall_);

    uint32_t unread = network_.unreadChatCount();
    std::string chatTabLbl = unread > 0 ? ("Chat (" + std::to_string(unread) + ")") : "Chat";
    drawButton("drawer_tab_chat", tabChat, chatTabLbl,
               !onFiles ? COL_PRIMARY_RED : COL_SEC_BTN_BG,
               !onFiles ? COL_PRIMARY_RED_HV : COL_SEC_BTN_HV,
               !onFiles ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   drawerTab_ = DrawerTab::LiveChat;
                   network_.markChatRead();
               }, fmtSmall_);

    UiRect closeBtn = { rx - 30.0f, y + 1.0f, rx, y + 31.0f };
    drawButton("drawer_close", closeBtn, "", COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY, 7.5f, [this]() {
        showFileDrawer_ = false;
    }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);
    float closeHover = std::clamp(widgetAnims_["drawer_close"].hoverT, 0.0f, 1.0f);
    drawIconClose(closeBtn.centerX(), closeBtn.centerY(), 4.2f,
                  lerpColor(COL_TEXT_SECONDARY, COL_TEXT_ON_ACCENT, closeHover), 1.6f);
    y += 44.0f;

    if (drawerTab_ == DrawerTab::FilesAndClip) {
        drawText("Drop files anywhere or use the actions below.",
                 { x, y, rx, y + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
        y += 24.0f;

        float halfW = (rx - x - 8.0f) * 0.5f;
        UiRect sendFileBtn = { x, y, x + halfW, y + 36.0f };
        drawButton("drawer_send_file", sendFileBtn, "Send File...",
                   COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 8.0f, [this]() {
                       openSendFileDialog();
                   }, fmtSmall_);

        UiRect openDirBtn = { sendFileBtn.right + 8.0f, y, rx, y + 36.0f };
        drawButton("drawer_open_dir", openDirBtn, "Received Files",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f, [this]() {
                       network_.fileTransferManager().openReceiveDirectoryInExplorer();
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        y += 44.0f;

        UiRect syncClipBtn = { x, y, rx, y + 34.0f };
        drawButton("drawer_sync_clip", syncClipBtn, "Sync Clipboard",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f, [this]() {
                       network_.pushLocalClipboardNow();
                       showToast("Clipboard synced");
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        y += 42.0f;

        drawText("TRANSFERS", { x, y, rx - 60.0f, y + 18.0f }, fmtSmall_, COL_TEXT_MUTED);
        UiRect clrBtn = { rx - 56.0f, y - 2.0f, rx, y + 20.0f };
        drawButton("drawer_clear_done", clrBtn, "Clear", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_SECONDARY, 6.0f, [this]() {
            network_.fileTransferManager().clearCompleted();
        }, fmtSmall_);
        y += 26.0f;

        auto items = network_.fileTransferManager().snapshotTransfers();
        if (items.empty()) {
            drawText("No file transfers yet.", { x, y + 20.0f, rx, y + 50.0f },
                     fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            for (size_t i = 0; i < items.size() && i < 16; ++i) {
                if (y + 62.0f > r.bottom - 14.0f) break;
                const auto& it = items[i];

                if (stepExp(transferProgSmooth_[i], std::clamp(it.progressFraction(), 0.0f, 1.0f), 18.0f, lastDt_)) {
                    inlineAnimActive_ = true;
                }

                UiRect card = { x, y, rx, y + 56.0f };
                fillRoundRect(card, 10.0f, COL_BG_SUBTLE);
                strokeRoundRect(card, 10.0f, COL_BORDER);

                drawText(it.fileName, { card.left + 12.0f, card.top + 6.0f, card.right - 70.0f, card.top + 24.0f },
                         fmtBodyBold_, COL_TEXT_PRIMARY);
                drawText(it.statusText, { card.left + 12.0f, card.top + 24.0f, card.right - 70.0f, card.top + 40.0f },
                         fmtSmall_, it.status == TransferStatus::Completed ? COL_SUCCESS :
                                    (it.status == TransferStatus::Failed || it.status == TransferStatus::Cancelled) ? COL_DANGER : COL_PRIMARY_RED);

                if (it.status == TransferStatus::InProgress) {
                    uint32_t tid = it.transferId;
                    UiRect cancelBtn = { card.right - 62.0f, card.top + 8.0f, card.right - 8.0f, card.top + 34.0f };
                    drawButton("xfer_cancel_" + std::to_string(tid), cancelBtn, "Cancel",
                               COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_PRIMARY, 6.5f, [this, tid]() {
                                   network_.cancelFileTransfer(tid);
                                   showToast("Transfer cancelled");
                               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ON_ACCENT);
                }

                UiRect progBg = { card.left + 12.0f, card.bottom - 10.0f, card.right - 12.0f, card.bottom - 5.5f };
                fillRoundRect(progBg, 2.2f, COL_BORDER);
                float fillW = progBg.width() * transferProgSmooth_[i];
                if (fillW > 1.0f) {
                    UiRect progFg = { progBg.left, progBg.top, progBg.left + fillW, progBg.bottom };
                    fillRoundRect(progFg, 2.2f, it.status == TransferStatus::Completed ? COL_SUCCESS : COL_PRIMARY_RED);
                }

                y += 64.0f;
            }
        }
    } else {
        drawText("Messages",
                 { x, y, rx, y + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
        y += 24.0f;

        UiRect chatBox = { x, y, rx, r.bottom - 64.0f };
        fillRoundRect(chatBox, 11.0f, COL_BG_SUBTLE);
        strokeRoundRect(chatBox, 11.0f, COL_BORDER);

        auto msgs = network_.chatMessages();
        if (msgs.empty()) {
            drawText("No messages yet.",
                     { chatBox.left + 14.0f, chatBox.centerY() - 14.0f, chatBox.right - 14.0f, chatBox.centerY() + 14.0f },
                     fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            float msgH = 46.0f;
            int maxVisible = std::max(1, static_cast<int>((chatBox.height() - 16.0f) / (msgH + 6.0f)));
            size_t startIdx = (msgs.size() > static_cast<size_t>(maxVisible)) ? (msgs.size() - maxVisible) : 0;
            float my = chatBox.top + 8.0f;

            for (size_t i = startIdx; i < msgs.size(); ++i) {
                const auto& m = msgs[i];
                UiRect bubble = m.fromLocal
                    ? UiRect{ chatBox.left + 36.0f, my, chatBox.right - 10.0f, my + msgH }
                    : UiRect{ chatBox.left + 10.0f, my, chatBox.right - 36.0f, my + msgH };
                fillRoundRect(bubble, 9.5f, m.fromLocal ? COL_BG_CARD_ALT : COL_BG_CARD);
                strokeRoundRect(bubble, 9.5f, m.fromLocal ? COL_BORDER_ALT : COL_BORDER);

                drawText(m.senderName, { bubble.left + 10.0f, bubble.top + 4.0f, bubble.right - 10.0f, bubble.top + 20.0f },
                         fmtSmall_, m.fromLocal ? COL_PRIMARY_RED : COL_TEXT_ACCENT);
                drawText(m.text, { bubble.left + 10.0f, bubble.top + 20.0f, bubble.right - 10.0f, bubble.bottom - 4.0f },
                         fmtBody_, COL_TEXT_PRIMARY);
                my += msgH + 6.0f;
            }
        }

        UiRect chatField = { x, r.bottom - 52.0f, rx - 76.0f, r.bottom - 14.0f };
        drawTextField("field_chat_input", FocusedField::ChatInput, chatField,
                      chatInput_, "Message...", false);

        UiRect sendChatBtn = { chatField.right + 6.0f, chatField.top, rx, chatField.bottom };
        drawButton("btn_send_chat", sendChatBtn, "Send",
                   COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 8.5f, [this]() {
                       sendChatFromInput();
                   }, fmtSmall_);
    }
}

// ---------------- macOS Sheet Connection Approval Modal ----------------

void AeroDeskWindow::drawIncomingApprovalModal(float width, float height, float modalProgress) {
    auto req = network_.pendingIncomingRequest();
    if (!req.active && modalProgress <= 0.01f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 8, 15, 0.44f * alpha));

    float mw = 440.0f;
    float mh = 326.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.88f + 0.12f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardSurface(modal, 20.0f, alpha);

    float mx = modal.left + 26.0f;
    float mrx = modal.right - 26.0f;
    float my = modal.top + 24.0f;

    drawText("Connection Request", { mx, my, mrx, my + 28.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    my += 30.0f;

    std::string callerLine = req.callerHostname + " (" + CryptoUtils::formatDeskId(req.callerDeskId) + ")";
    drawText(callerLine, { mx, my, mrx, my + 24.0f }, fmtSubheading_, COL_PRIMARY_RED);
    my += 24.0f;

    drawText("Wants to connect to your desktop.",
             { mx, my, mrx, my + 20.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 28.0f;

    drawToggleSwitch("modal_perm_input", { mx, my, mrx, my + 26.0f }, (modalPermissions_ & PERM_INPUT) != 0,
                     "Allow mouse and keyboard control", [this]() {
                         modalPermissions_ ^= PERM_INPUT;
                     });
    my += 32.0f;

    drawToggleSwitch("modal_perm_clip", { mx, my, mrx, my + 26.0f }, (modalPermissions_ & PERM_CLIPBOARD) != 0,
                     "Allow clipboard sharing", [this]() {
                         modalPermissions_ ^= PERM_CLIPBOARD;
                     });
    my += 32.0f;

    drawToggleSwitch("modal_perm_file", { mx, my, mrx, my + 26.0f }, (modalPermissions_ & PERM_FILE_TRANSFER) != 0,
                     "Allow file transfers", [this]() {
                         modalPermissions_ ^= PERM_FILE_TRANSFER;
                     });
    my += 40.0f;

    float btnW = (mrx - mx - 12.0f) * 0.5f;
    UiRect rejectBtn = { mx, my, mx + btnW, my + 42.0f };
    drawButton("modal_reject", rejectBtn, "Decline",
               COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_PRIMARY, 9.5f, [this]() {
                   network_.respondToIncomingRequest(false, 0);
                   showToast("Connection declined", true);
               }, nullptr, true, COL_BORDER, COL_TEXT_ON_ACCENT);

    UiRect acceptBtn = { rejectBtn.right + 12.0f, my, mrx, my + 42.0f };
    drawButton("modal_accept", acceptBtn, "Accept",
               COL_PRIMARY_RED, COL_PRIMARY_RED_HV, COL_TEXT_ON_ACCENT, 9.5f, [this]() {
                   network_.respondToIncomingRequest(true, modalPermissions_);
                   showToast("Connection accepted");
               });

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- macOS Dynamic Capsule Toast Banner ----------------

void AeroDeskWindow::drawToastBanner(float width, float height, float toastProgress) {
    if (toastText_.empty() || toastProgress <= 0.01f) {
        return;
    }
    float alpha = std::clamp(toastProgress, 0.0f, 1.0f);
    float estTextW = static_cast<float>(toastText_.size()) * 7.6f + 64.0f;
    float bw = std::clamp(estTextW, 220.0f, std::min(560.0f, width - 40.0f));
    float bh = 40.0f;
    float slideY = (1.0f - toastProgress) * 26.0f;
    UiRect r = UiRect{ (width - bw) * 0.5f, height - bh - 22.0f, (width + bw) * 0.5f, height - 22.0f }.offset(0.0f, slideY);

    drawCardShadow(r, bh * 0.5f, alpha * 1.4f);
    fillRoundRect(r, bh * 0.5f, withAlpha(COL_BG_CARD, 0.98f * alpha));
    strokeRoundRect(r, bh * 0.5f, withAlpha(toastIsError_ ? COL_DANGER : COL_BORDER_ALT, alpha), 1.2f);

    drawPulseDot(r.left + 18.0f, r.centerY(), 3.6f, toastIsError_ ? COL_DANGER : COL_PRIMARY_RED, alpha);
    drawText(toastText_, { r.left + 28.0f, r.top, r.right - 18.0f, r.bottom },
             fmtBodyBold_, withAlpha(COL_TEXT_PRIMARY, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---------------- Input & Interaction Handlers ----------------

bool AeroDeskWindow::mapCanvasPointToNormalized(float x, float y, float& outNormX, float& outNormY) const {
    if (renderedCanvasRect_.width() <= 1.0f || renderedCanvasRect_.height() <= 1.0f) {
        return false;
    }
    if (!renderedCanvasRect_.contains(x, y)) {
        return false;
    }
    outNormX = std::clamp((x - renderedCanvasRect_.left) / renderedCanvasRect_.width(), 0.0f, 1.0f);
    outNormY = std::clamp((y - renderedCanvasRect_.top) / renderedCanvasRect_.height(), 0.0f, 1.0f);
    return true;
}

std::string* AeroDeskWindow::activeFocusedTextBuffer() {
    if (focusedField_ == FocusedField::RemoteId) return &remoteIdInput_;
    if (focusedField_ == FocusedField::RemotePassword) return &remotePasswordInput_;
    if (focusedField_ == FocusedField::LocalPassword) return &localPasswordEdit_;
    if (focusedField_ == FocusedField::RelayServer) return &relayServerEdit_;
    if (focusedField_ == FocusedField::ChatInput) return &chatInput_;
    return nullptr;
}

void AeroDeskWindow::onMouseMove(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    mouseInsideClient_ = true;

    if (!trackingMouseLeave_ && hwnd_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        if (TrackMouseEvent(&tme)) {
            trackingMouseLeave_ = true;
        }
    }

    std::string newHover;
    bool newIsText = false;
    for (auto it = clickRegions_.rbegin(); it != clickRegions_.rend(); ++it) {
        if (it->rect.contains(x, y)) {
            newHover = it->id;
            newIsText = it->isTextInput;
            break;
        }
    }

    bool hoverChanged = (newHover != hoveredWidgetId_) || (newIsText != hoveredIsTextInput_);
    hoveredWidgetId_ = newHover;
    hoveredIsTextInput_ = newIsText;

    if (activeTab_ == ActiveTab::RemoteSession && remoteInputEnabled_ && !showFileDrawer_ &&
        !network_.pendingIncomingRequest().active) {
        float nx = 0.0f, ny = 0.0f;
        if (mapCanvasPointToNormalized(x, y, nx, ny)) {
            uint64_t now = GetTickCount64();
            if (now - lastMouseSendTick_ >= 10) {
                lastMouseSendTick_ = now;
                network_.sendMouseMove(nx, ny);
            }
        }
    }

    if (hoverChanged) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void AeroDeskWindow::onMouseButton(MouseButtonId btn, bool isDown, float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    mouseInsideClient_ = true;

    if (btn == MouseButtonId::Left) {
        mouseLeftDown_ = isDown;
        if (isDown) {
            for (auto it = clickRegions_.rbegin(); it != clickRegions_.rend(); ++it) {
                if (it->rect.contains(x, y)) {
                    pressedWidgetId_ = it->id;
                    auto& anim = widgetAnims_[it->id];
                    anim.rippleT = 0.0f;
                    anim.rippleX = x;
                    anim.rippleY = y;
                    if (it->onClick) it->onClick();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
            pressedWidgetId_.clear();
        } else {
            if (!pressedWidgetId_.empty()) {
                pressedWidgetId_.clear();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }

    if (activeTab_ == ActiveTab::RemoteSession && !network_.pendingIncomingRequest().active) {
        float nx = 0.0f, ny = 0.0f;
        if (mapCanvasPointToNormalized(x, y, nx, ny)) {
            focusedField_ = FocusedField::RemoteCanvas;
            if (remoteInputEnabled_) {
                network_.sendMouseButton(btn, isDown, nx, ny);
            }
        }
    }
}

void AeroDeskWindow::onMouseWheel(int delta) {
    if (activeTab_ == ActiveTab::RemoteSession && remoteInputEnabled_ &&
        renderedCanvasRect_.contains(mouseX_, mouseY_)) {
        network_.sendMouseWheel(delta, 0);
    }
}

void AeroDeskWindow::onCharInput(wchar_t ch) {
    if (activeTab_ == ActiveTab::RemoteSession && focusedField_ == FocusedField::RemoteCanvas) {
        return;
    }

    std::string* target = activeFocusedTextBuffer();
    if (!target) return;

    size_t maxLen = (focusedField_ == FocusedField::ChatInput) ? 240 : 64;

    if (ch == L'\b') {
        if (!target->empty()) target->pop_back();
    } else if (ch == 1) {
        target->clear();
    } else if (ch == 3) {
        if (!target->empty() && focusedField_ != FocusedField::RemotePassword && focusedField_ != FocusedField::LocalPassword) {
            ClipboardManager::setClipboardUtf8(*target);
            showToast("Copied to clipboard");
        }
    } else if (ch == L'\r' || ch == L'\n') {
        if (focusedField_ == FocusedField::RemoteId || focusedField_ == FocusedField::RemotePassword) {
            initiateConnection();
        } else if (focusedField_ == FocusedField::LocalPassword && !localPasswordEdit_.empty()) {
            identity_.setUnattendedPassword(localPasswordEdit_);
            showToast("Password saved");
        } else if (focusedField_ == FocusedField::RelayServer && !relayServerEdit_.empty()) {
            identity_.setRelayServerAddress(relayServerEdit_);
            showToast("Relay server saved");
        } else if (focusedField_ == FocusedField::ChatInput) {
            sendChatFromInput();
        }
    } else if (ch == L'\t') {
        if (focusedField_ == FocusedField::RemoteId) focusedField_ = FocusedField::RemotePassword;
        else if (focusedField_ == FocusedField::RemotePassword) focusedField_ = FocusedField::LocalPassword;
        else if (focusedField_ == FocusedField::LocalPassword) focusedField_ = FocusedField::RelayServer;
        else focusedField_ = FocusedField::RemoteId;
    } else if (ch == 22) {
        std::string clip = ClipboardManager::getClipboardUtf8();
        for (char c : clip) {
            if (c >= 32 && c < 127 && target->size() < maxLen) {
                target->push_back(c);
            }
        }
    } else if (ch >= 32 && ch < 127 && target->size() < maxLen) {
        target->push_back(static_cast<char>(ch));
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AeroDeskWindow::onKeyEvent(uint16_t vk, uint16_t scan, bool isDown, bool isExtended) {
    if (isDown) {
        if (vk == VK_F11) {
            toggleFullscreen();
            return;
        }
        if (vk == VK_ESCAPE) {
            if (showFileDrawer_) {
                showFileDrawer_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (isFullscreen_) {
                toggleFullscreen();
                return;
            }
            if (focusedField_ != FocusedField::None && focusedField_ != FocusedField::RemoteCanvas) {
                focusedField_ = FocusedField::None;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
        if (vk == VK_F8 && activeTab_ == ActiveTab::RemoteSession) {
            remoteInputEnabled_ = !remoteInputEnabled_;
            if (!remoteInputEnabled_) network_.sendReleaseAllModifiers();
            showToast(remoteInputEnabled_ ? "Control enabled (F8)" : "View-only mode (F8)");
            return;
        }
    }

    if (activeTab_ == ActiveTab::RemoteSession && focusedField_ == FocusedField::RemoteCanvas && remoteInputEnabled_) {
        network_.sendKeyEvent(vk, scan, isDown, isExtended);
    }
}

void AeroDeskWindow::onDropFiles(HDROP hDrop) {
    UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
    int sentCount = 0;
    for (UINT i = 0; i < count; ++i) {
        char filePath[MAX_PATH] = {};
        if (DragQueryFileA(hDrop, i, filePath, MAX_PATH) > 0) {
            if (network_.sendFile(filePath) > 0) {
                ++sentCount;
            }
        }
    }
    DragFinish(hDrop);

    if (sentCount > 0) {
        drawerTab_ = DrawerTab::FilesAndClip;
        showFileDrawer_ = true;
        showToast("Sending " + std::to_string(sentCount) + " file(s)");
    } else {
        showToast("Connect to a remote desk first", true);
    }
}

// ---------------- High-Level UI Actions ----------------

void AeroDeskWindow::initiateConnection() {
    if (remoteIdInput_.empty()) {
        showToast("Enter a 9-digit Desk ID", true);
        return;
    }
    network_.connectToRemote(remoteIdInput_, remotePasswordInput_);
    showToast("Connecting to " + remoteIdInput_ + "...");
}

void AeroDeskWindow::openSendFileDialog() {
    char fileBuf[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        if (network_.sendFile(fileBuf) > 0) {
            drawerTab_ = DrawerTab::FilesAndClip;
            showFileDrawer_ = true;
            showToast("Sending file...");
        } else {
            showToast("No active session", true);
        }
    }
}

void AeroDeskWindow::saveRemoteScreenshot() {
    if (frameBufferBgra_.empty() || frameBufferW_ <= 0 || frameBufferH_ <= 0) {
        showToast("No video frame available yet", true);
        return;
    }

    std::string dir = network_.fileTransferManager().receiveDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::string fileName = "Screenshot_" + std::to_string(GetTickCount64()) + ".bmp";
    std::filesystem::path fullPath = std::filesystem::path(dir) / fileName;

    uint32_t rowStride = static_cast<uint32_t>(frameBufferW_ * 4);
    uint32_t imgSize = rowStride * static_cast<uint32_t>(frameBufferH_);

    BITMAPFILEHEADER bfh{};
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imgSize;

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = frameBufferW_;
    bih.biHeight = -frameBufferH_;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = imgSize;

    std::ofstream out(fullPath, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
        out.write(reinterpret_cast<const char*>(&bih), sizeof(bih));
        out.write(reinterpret_cast<const char*>(frameBufferBgra_.data()), imgSize);
        out.close();
        showToast("Saved " + fileName);
    } else {
        showToast("Failed to save screenshot", true);
    }
}

void AeroDeskWindow::sendChatFromInput() {
    if (chatInput_.empty()) return;
    if (network_.sendChatMessage(chatInput_)) {
        chatInput_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        showToast("Connect to a remote desk first", true);
    }
}

void AeroDeskWindow::toggleFullscreen() {
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    if (!isFullscreen_) {
        savedWindowPlacement_.length = sizeof(WINDOWPLACEMENT);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetWindowPlacement(hwnd_, &savedWindowPlacement_) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd_, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            isFullscreen_ = true;
        }
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &savedWindowPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        isFullscreen_ = false;
    }
}

void AeroDeskWindow::showToast(const std::string& message, bool isError) {
    toastText_ = message;
    toastIsError_ = isError;
    toastExpireTick_ = GetTickCount64() + 3500;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace aerodesk
