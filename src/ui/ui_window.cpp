#include "ui_window.hpp"
#include "../control/windows_service_manager.hpp"

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

namespace cppdesk {

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

D2D1_COLOR_F COL_PRIMARY_ACCENT      = rgba(37, 99, 235);
D2D1_COLOR_F COL_PRIMARY_ACCENT_HV   = rgba(29, 78, 216);
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
    COL_PRIMARY_ACCENT    = lerpColor(rgba(37, 99, 235),   rgba(59, 130, 246), darkT);
    COL_PRIMARY_ACCENT_HV = lerpColor(rgba(29, 78, 216),   rgba(96, 165, 250), darkT);
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

CppDeskWindow::CppDeskWindow(IdentityManager& identity, NetworkEngine& network)
    : identity_(identity)
    , network_(network)
{
    const auto& s = identity_.settings();
    relayServerEdit_ = s.relayServer.empty() ? identity_.relayServerAddress() : s.relayServer;
    relayAuthKeyEdit_ = s.relayAuthKey;
    stunServerEdit_ = s.stunServer;
    relayModeEdit_ = s.relayMode;
    themeAnimT_ = s.darkTheme ? 1.0f : 0.0f;
    scaleMode_ = static_cast<ScaleMode>(std::clamp<int>(s.defaultScaleMode, 0, 3));
    updateActivePalette(themeAnimT_);

    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    qpcFreq_ = freq.QuadPart;
    lastQpcCounter_ = now.QuadPart;
}

CppDeskWindow::~CppDeskWindow() {
    if (sessionRecorder_.isRecording()) {
        sessionRecorder_.stopRecording();
    }
    if (notificationMgr_) {
        notificationMgr_->shutdown();
    }
    releaseGraphics();
}

void CppDeskWindow::applyWindowThemeAttribute() {
    if (!hwnd_) return;
    BOOL dark = identity_.settings().darkTheme ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD cornerPref = 2; // DWMWCP_ROUND (native rounded window corners)
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
}

void CppDeskWindow::switchTab(ActiveTab newTab) {
    if (activeTab_ == newTab) return;
    if (activeTab_ == ActiveTab::RemoteSession && newTab != ActiveTab::RemoteSession) {
        if (sessionRecorder_.isRecording()) {
            sessionRecorder_.stopRecording();
        }
        if (network_.isVoiceIntercomActive()) {
            network_.stopVoiceIntercom();
        }
        network_.sendReleaseAllModifiers();

        // Multi-Session: Preserve active tab framebuffer & settings
        if (!frameBufferBgra_.empty()) {
            sessionTabs_.cacheActiveTabFrame(frameBufferBgra_.data(), frameBufferW_, frameBufferH_, displayedFrameSeq_, remoteCursor_);
        }
        SessionTab* curTab = sessionTabs_.activeTab();
        if (curTab) {
            curTab->scaleMode = scaleMode_;
            curTab->remoteInputEnabled = remoteInputEnabled_;
        }
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

bool CppDeskWindow::create(HINSTANCE hInstance, int nCmdShow) {
    if (!initGraphics()) {
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &CppDeskWindow::WndProcStatic;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!appIcon) appIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.lpszClassName = L"CppDeskMainWindowClass";
    RegisterClassExW(&wc);

    std::string title = "CppDesk";
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

    notificationMgr_ = std::make_unique<NotificationManager>();
    notificationMgr_->init(hwnd_, hInstance, title);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    SetTimer(hwnd_, 1, 32, nullptr);
    return true;
}

int CppDeskWindow::messageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool CppDeskWindow::initGraphics() {
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

void CppDeskWindow::discardDeviceResources() {
    if (remoteBitmap_) { remoteBitmap_->Release(); remoteBitmap_ = nullptr; }
    if (solidBrush_) { solidBrush_->Release(); solidBrush_ = nullptr; }
    if (renderTarget_) { renderTarget_->Release(); renderTarget_ = nullptr; }
    bitmapW_ = 0;
    bitmapH_ = 0;
}

void CppDeskWindow::releaseGraphics() {
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

bool CppDeskWindow::stepAnimations(float dt) {
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

    // 7. Fullscreen macOS Dynamic Island Floating Top Bar Spring
    RECT rcCl{};
    GetClientRect(hwnd_, &rcCl);
    float winW = static_cast<float>(std::max<LONG>(1, rcCl.right - rcCl.left));
    float pillHalfW = 355.0f;
    bool islandHovered = (mouseInsideClient_ && (mouseY_ <= 42.0f || (mouseY_ <= floatingToolbarY_ + 54.0f && std::fabs(mouseX_ - winW * 0.5f) <= pillHalfW + 24.0f)));
    bool islandDropdownOpen = (showDisplayMenu_ || showAdminMenu_ || showQualityMenu_);
    float targetToolbarY = (isFullscreen_ && activeTab_ == ActiveTab::RemoteSession)
        ? ((floatingToolbarPinned_ || islandHovered || islandDropdownOpen) ? 14.0f : -64.0f)
        : -64.0f;
    if (stepSpring(floatingToolbarY_, floatingToolbarVel_, targetToolbarY, 26.0f, 0.78f, dt)) active = true;

    // 8. Keyboard Shortcuts Modal Spring
    float targetShortcuts = showShortcutsModal_ ? 1.0f : 0.0f;
    if (stepSpring(shortcutsModalAnimT_, shortcutsModalAnimVel_, targetShortcuts, 28.0f, 0.74f, dt)) active = true;

    // 9. TCP Port Forwarding Manager Modal Spring
    float targetPortModal = showPortForwardModal_ ? 1.0f : 0.0f;
    if (stepSpring(portForwardModalAnimT_, portForwardModalAnimVel_, targetPortModal, 28.0f, 0.74f, dt)) active = true;

    // 10. Address Book Edit Modal Spring
    float targetABModal = showAddressBookEditModal_ ? 1.0f : 0.0f;
    if (stepSpring(addressBookModalAnimT_, addressBookModalAnimVel_, targetABModal, 28.0f, 0.74f, dt)) active = true;

    // 11. Mandatory Update Modal Spring (v3.0.0)
    float targetUpdateModal = showUpdateRequiredModal_ ? 1.0f : 0.0f;
    if (stepSpring(updateModalAnimT_, updateModalAnimVel_, targetUpdateModal, 28.0f, 0.74f, dt)) active = true;

    // 12. Real-Time Performance HUD Overlay Spring (Phase 10)
    float targetHud = showPerformanceHud_ ? 1.0f : 0.0f;
    if (stepSpring(hudAnimT_, hudAnimVel_, targetHud, 26.0f, 0.78f, dt)) active = true;

    // 13. Remote Reboot & Reconnect Modal Spring (Phase 12)
    float targetRebootModal = showRebootConfirmModal_ ? 1.0f : 0.0f;
    if (stepSpring(rebootModalAnimT_, rebootModalAnimVel_, targetRebootModal, 28.0f, 0.74f, dt)) active = true;

    // Startup auto-update check timer (queries GitHub 2.0s after launch)
    if (!startupCheckTriggered_) {
        startupUpdateCheckTimer_ -= dt;
        if (startupUpdateCheckTimer_ <= 0.0f) {
            startupCheckTriggered_ = true;
            triggerUpdateCheck(false);
        } else {
            active = true;
        }
    }

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

LRESULT CALLBACK CppDeskWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CppDeskWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<CppDeskWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<CppDeskWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CppDeskWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
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
        case WM_ACTIVATE:
        case WM_SETFOCUS: {
            if (notificationMgr_) {
                notificationMgr_->stopFlash();
            }
            break;
        }
        case WM_SYSCOMMAND: {
            if ((wParam & 0xFFF0) == SC_MINIMIZE && identity_.settings().minimizeToTray) {
                ShowWindow(hwnd_, SW_HIDE);
                return 0;
            }
            break;
        }
        case WM_CLOSE: {
            if (identity_.settings().minimizeToTray) {
                ShowWindow(hwnd_, SW_HIDE);
                return 0;
            }
            DestroyWindow(hwnd_);
            return 0;
        }
        case WM_COMMAND: {
            WORD cmdId = LOWORD(wParam);
            if (cmdId == IDM_TRAY_RESTORE) {
                restoreFromTray();
                return 0;
            } else if (cmdId == IDM_TRAY_MUTE) {
                if (notificationMgr_) {
                    notificationMgr_->setMuted(!notificationMgr_->isMuted());
                    showToast(notificationMgr_->isMuted() ? "Notifications muted" : "Notifications unmuted");
                }
                return 0;
            } else if (cmdId == IDM_TRAY_EXIT) {
                DestroyWindow(hwnd_);
                return 0;
            }
            break;
        }
        case WM_TRAYICON: {
            UINT event = LOWORD(lParam);
            switch (event) {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case NIN_BALLOONUSERCLICK: {
                    NotificationType nType = notificationMgr_ ? notificationMgr_->lastNotificationType() : NotificationType::GeneralInfo;
                    restoreFromTray(nType);
                    if (notificationMgr_) notificationMgr_->clearLastNotificationType();
                    return 0;
                }
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: {
                    POINT pt;
                    if (!GetCursorPos(&pt)) {
                        pt.x = static_cast<short>(LOWORD(wParam));
                        pt.y = static_cast<short>(HIWORD(wParam));
                    }
                    std::string status = "Online";
                    auto vStats = network_.viewerStats();
                    if (vStats.state == ViewerConnectionState::Connected) {
                        status = "Controlling " + vStats.remoteHostname;
                    } else if (network_.hostSessionStatus().active) {
                        status = "Host Active";
                    }
                    if (notificationMgr_) {
                        notificationMgr_->showContextMenu(pt.x, pt.y, status);
                    }
                    return 0;
                }
            }
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
                scaleMode_ = static_cast<ScaleMode>(std::clamp<int>(identity_.settings().defaultScaleMode, 0, 3));
                if (vStats.remoteAddress.find("127.0.0.1") != std::string::npos ||
                    vStats.remoteDeskId == identity_.deskId()) {
                    remoteInputEnabled_ = false;
                    showToast("Connected on same PC. View-Only enabled (F8 to toggle).");
                } else {
                    remoteInputEnabled_ = true;
                    showToast("Connected to " + vStats.remoteHostname);
                }

                SessionTab* activeTab = sessionTabs_.activeTab();
                if (activeTab) {
                    activeTab->state = ViewerConnectionState::Connected;
                    activeTab->deskId = vStats.remoteDeskId;
                    activeTab->remoteHostname = vStats.remoteHostname;
                    if (!vStats.remoteHostname.empty()) {
                        activeTab->title = vStats.remoteHostname;
                    }
                    activeTab->connectedSinceTickMs = tickNow;
                }
            } else if (vStats.state == ViewerConnectionState::Error &&
                       prevViewerState_ != ViewerConnectionState::Error) {
                SessionTab* activeTab = sessionTabs_.activeTab();
                if (activeTab) {
                    activeTab->state = ViewerConnectionState::Error;
                    activeTab->statusMessage = vStats.statusMessage;
                }
                showToast(vStats.statusMessage, true);
                if (sessionTabs_.tabCount() <= 1) {
                    switchTab(ActiveTab::Dashboard);
                }
            } else if (prevViewerState_ == ViewerConnectionState::Connected &&
                       vStats.state == ViewerConnectionState::Disconnected) {
                if (sessionRecorder_.isRecording()) {
                    sessionRecorder_.stopRecording();
                }
                if (network_.isVoiceIntercomActive()) {
                    network_.stopVoiceIntercom();
                }
                if (notificationMgr_) {
                    std::string hName = prevViewerHostname_.empty() ? "Remote Desktop" : prevViewerHostname_;
                    notificationMgr_->notify(NotificationType::SessionDropped, "Session Disconnected",
                                             "Remote session with " + hName + " ended.", identity_.settings());
                }
                SessionTab* activeTab = sessionTabs_.activeTab();
                if (activeTab) {
                    activeTab->state = ViewerConnectionState::Disconnected;
                }
                if (sessionTabs_.tabCount() <= 1) {
                    sessionTabs_.closeAllTabs();
                    switchTab(ActiveTab::Dashboard);
                }
            }
            if (vStats.state == ViewerConnectionState::Connected) {
                prevViewerHostname_ = vStats.remoteHostname;
                SessionTab* curTab = sessionTabs_.activeTab();
                if (curTab) {
                    curTab->fps = vStats.fps;
                    curTab->rttMs = vStats.rttMs;
                    curTab->activeMonitorIndex = vStats.activeMonitorIndex;
                    curTab->monitorCount = vStats.monitorCount;
                    curTab->qualityPreset = vStats.qualityPreset;
                    curTab->privacyModeEngaged = vStats.privacyModeEngaged;
                    if (!vStats.remoteHostname.empty() && curTab->remoteHostname != vStats.remoteHostname) {
                        curTab->remoteHostname = vStats.remoteHostname;
                        curTab->title = vStats.remoteHostname;
                    }
                }
            }
            prevViewerState_ = vStats.state;

            // Host session disconnect detection
            auto hostStatus = network_.hostSessionStatus();
            if (prevHostActive_ && !hostStatus.active) {
                if (notificationMgr_) {
                    std::string cName = prevHostClientName_.empty() ? "Remote Client" : prevHostClientName_;
                    notificationMgr_->notify(NotificationType::SessionDropped, "Client Disconnected",
                                             cName + " disconnected from your desktop.", identity_.settings());
                }
            }
            if (hostStatus.active) {
                prevHostClientName_ = hostStatus.viewerHostname;
            }
            prevHostActive_ = hostStatus.active;

            // Chat message notifications
            uint32_t unreadChat = network_.unreadChatCount();
            if (showFileDrawer_ && drawerTab_ == DrawerTab::LiveChat && unreadChat > 0) {
                network_.markChatRead();
                unreadChat = 0;
            }
            if (unreadChat > lastSeenUnreadChat_) {
                auto msgs = network_.chatMessages();
                if (!msgs.empty() && !msgs.back().fromLocal) {
                    showToast(msgs.back().senderName + ": " + msgs.back().text);
                    bool isUnfocused = (GetForegroundWindow() != hwnd_ || IsIconic(hwnd_));
                    bool drawerClosed = (!showFileDrawer_ || drawerTab_ != DrawerTab::LiveChat);
                    if ((isUnfocused || drawerClosed) && notificationMgr_) {
                        notificationMgr_->notify(
                            NotificationType::ChatMessage,
                            msgs.back().senderName.empty() ? "New Chat Message" : msgs.back().senderName,
                            msgs.back().text,
                            identity_.settings()
                        );
                    }
                }
            }
            lastSeenUnreadChat_ = unreadChat;

            // Completed file transfer notifications
            auto transfers = network_.fileTransferManager().snapshotTransfers();
            size_t completedCount = 0;
            std::string newlyCompletedName;
            for (const auto& item : transfers) {
                if (item.status == TransferStatus::Completed) {
                    completedCount++;
                    if (!item.isOutgoing && completedCount > prevCompletedTransfersCount_) {
                        newlyCompletedName = item.fileName;
                    }
                }
            }
            if (completedCount > prevCompletedTransfersCount_) {
                if (!newlyCompletedName.empty() && notificationMgr_) {
                    notificationMgr_->notify(
                        NotificationType::FileTransferDone,
                        "File Transfer Complete",
                        "File '" + newlyCompletedName + "' received successfully.",
                        identity_.settings()
                    );
                }
            }
            prevCompletedTransfersCount_ = completedCount;

            // Incoming connection request notification
            auto pending = network_.pendingIncomingRequest();
            if (pending.active && !prevHasPendingIncoming_) {
                modalPermissions_ = identity_.settings().defaultPermissions;
                SetForegroundWindow(hwnd_);
                if (notificationMgr_) {
                    std::string title = "Incoming Connection Request";
                    std::string caller = pending.callerHostname.empty() ? "Remote User" : pending.callerHostname;
                    std::string msg = caller + " (" + CryptoUtils::formatDeskId(pending.callerDeskId) + ") is requesting remote control.";
                    notificationMgr_->notify(NotificationType::IncomingConnection, title, msg, identity_.settings());
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            prevHasPendingIncoming_ = pending.active;
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

        case WM_DESK_UPDATE_CHECK_DONE: {
            auto* info = reinterpret_cast<UpdateInfo*>(wParam);
            bool manual = (lParam != 0);
            if (info) {
                latestUpdateInfo_ = *info;
                isCheckingUpdates_ = false;
                if (!info->success) {
                    updateStatusText_ = "Check failed (Offline or rate-limited)";
                    if (manual) showToast("Could not check for updates", true);
                } else if (info->updateRequired) {
                    updateStatusText_ = "Mandatory Update: v" + info->latestVersion;
                    showUpdateRequiredModal_ = true;
                    if (manual) showToast("Mandatory update v" + info->latestVersion + " available!", true);
                } else {
                    updateStatusText_ = "CppDesk is up to date (v" + std::string(CPP_DESK_VERSION) + ")";
                    if (manual) showToast("You have the latest version (v" + std::string(CPP_DESK_VERSION) + ")");
                }
                delete info;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---------------- Primitive Drawing, Vector Icons & macOS Surface Helpers ----------------

void CppDeskWindow::drawCardShadow(const UiRect& r, float radius, float intensity) {
    if (!renderTarget_ || !solidBrush_ || intensity <= 0.01f) return;
    float shadowScale = 1.0f + 2.4f * std::clamp(themeAnimT_, 0.0f, 1.0f);
    fillRoundRect(r.offset(0.0f, 5.0f).inflate(2.5f, 2.5f), radius + 2.5f, rgba(5, 8, 18, 0.022f * shadowScale * intensity));
    fillRoundRect(r.offset(0.0f, 2.5f).inflate(1.0f, 1.0f), radius + 1.0f, rgba(5, 8, 18, 0.032f * shadowScale * intensity));
    fillRoundRect(r.offset(0.0f, 1.0f), radius, rgba(5, 8, 18, 0.028f * shadowScale * intensity));
}

void CppDeskWindow::drawCardSurface(const UiRect& r, float radius, float alpha, bool /*accentHeader*/) {
    drawCardShadow(r, radius, alpha);
    fillRoundRect(r, radius, withAlpha(COL_BG_CARD, alpha));
    strokeRoundRect(r, radius, withAlpha(COL_BORDER, alpha), 1.0f);

    // Subtle macOS top inner specular highlight
    float specAlpha = lerpColor(rgba(255, 255, 255, 0.75f), rgba(255, 255, 255, 0.05f), themeAnimT_).a * alpha;
    fillRoundRect({ r.left + 14.0f, r.top + 0.8f, r.right - 14.0f, r.top + 1.8f }, 0.5f, rgba(255, 255, 255, specAlpha));
}

void CppDeskWindow::drawPulseDot(float cx, float cy, float baseRadius, D2D1_COLOR_F color, float alpha) {
    if (!renderTarget_ || !solidBrush_ || alpha <= 0.01f) return;
    float wave = 0.5f + 0.5f * std::sin(animTimeSec_ * 2.2f);
    float haloRadius = baseRadius + 1.2f + 2.2f * wave;
    float haloAlpha = (0.20f - 0.12f * wave) * alpha;

    solidBrush_->SetColor(withAlpha(color, haloAlpha));
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), haloRadius, haloRadius), solidBrush_);

    solidBrush_->SetColor(withAlpha(color, alpha));
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), baseRadius, baseRadius), solidBrush_);
}

void CppDeskWindow::drawSparkline(const UiRect& r, const float* values, size_t count, float maxVal, D2D1_COLOR_F color, float alpha) {
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

void CppDeskWindow::drawIconStar(float cx, float cy, float radius, bool filled, D2D1_COLOR_F color) {
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

void CppDeskWindow::drawIconClose(float cx, float cy, float halfSize, D2D1_COLOR_F color, float strokeWidth) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    renderTarget_->DrawLine(D2D1::Point2F(cx - halfSize, cy - halfSize), D2D1::Point2F(cx + halfSize, cy + halfSize), solidBrush_, strokeWidth);
    renderTarget_->DrawLine(D2D1::Point2F(cx + halfSize, cy - halfSize), D2D1::Point2F(cx - halfSize, cy + halfSize), solidBrush_, strokeWidth);
}

void CppDeskWindow::drawIconTheme(float cx, float cy, float radius, bool isDark, D2D1_COLOR_F color) {
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

void CppDeskWindow::fillRoundRect(const UiRect& r, float radius, D2D1_COLOR_F color) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius);
    renderTarget_->FillRoundedRectangle(rr, solidBrush_);
}

void CppDeskWindow::strokeRoundRect(const UiRect& r, float radius, D2D1_COLOR_F color, float strokeWidth) {
    if (!renderTarget_ || !solidBrush_) return;
    solidBrush_->SetColor(color);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius);
    renderTarget_->DrawRoundedRectangle(rr, solidBrush_, strokeWidth);
}

void CppDeskWindow::drawText(
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

void CppDeskWindow::drawButton(
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
        fillRoundRect(animRect.offset(0.0f, 2.0f), radius, withAlpha(COL_PRIMARY_ACCENT, 0.12f * hT * std::max(0.0f, 1.0f - pT)));
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

void CppDeskWindow::drawTextField(
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
        fillRoundRect(glowR, 11.5f, withAlpha(COL_PRIMARY_ACCENT, 0.15f * std::clamp(fT, 0.0f, 1.0f)));
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

void CppDeskWindow::drawToggleSwitch(
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
    D2D1_COLOR_F onCol  = lerpColor(COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, hT);
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

void CppDeskWindow::onPaint() {
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
    if (!isFullscreen_) {
        drawTopNavBar(width, topOffset);
    }

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

    // Fullscreen Dynamic Island Floating Toolbar
    if (isFullscreen_ && activeTab_ == ActiveTab::RemoteSession) {
        drawDynamicIslandToolbar(width, height);
    }

    // Dynamic Island Clipboard File Transfer Progress Pill
    if (activeTab_ == ActiveTab::RemoteSession) {
        drawClipboardTransferPill(width, height);
    }

    // Keyboard Shortcuts Sheet Modal
    if (shortcutsModalAnimT_ > 0.004f) {
        drawShortcutsModal(width, height, shortcutsModalAnimT_);
    }

    // TCP Port Forwarding Manager Sheet Modal
    if (portForwardModalAnimT_ > 0.004f) {
        drawPortForwardModal(width, height, portForwardModalAnimT_);
    }

    // Address Book Edit Sheet Modal
    if (addressBookModalAnimT_ > 0.004f) {
        drawAddressBookModal(width, height, addressBookModalAnimT_);
    }

    // Mandatory Update Required Modal (v3.0.0)
    if (updateModalAnimT_ > 0.004f) {
        drawUpdateRequiredModal(width, height, updateModalAnimT_);
    }

    // Remote Reboot & Reconnect Sheet Modal (Phase 12)
    if (rebootModalAnimT_ > 0.004f) {
        drawRebootConfirmModal(width, height, rebootModalAnimT_);
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

void CppDeskWindow::drawTopNavBar(float width, float& outTopOffset) {
    float navH = 58.0f;
    outTopOffset = navH;

    UiRect navRect = { 0.0f, 0.0f, width, navH };
    fillRoundRect(navRect, 0.0f, COL_BG_NAV);
    fillRoundRect({ 0.0f, navH - 1.0f, width, navH }, 0.0f, COL_BORDER);

    // Left: Minimal Squircle Brand Badge + Online Dot
    UiRect logoBadge = { 20.0f, 13.0f, 52.0f, 45.0f };
    fillRoundRect(logoBadge.offset(0.0f, 2.0f), 9.5f, withAlpha(COL_PRIMARY_ACCENT, 0.22f));
    fillRoundRect(logoBadge, 9.5f, COL_PRIMARY_ACCENT);
    drawText("CD", logoBadge, fmtBodyBold_, COL_TEXT_ON_ACCENT, DWRITE_TEXT_ALIGNMENT_CENTER);

    std::string brandTitle = "CppDesk";
    if (identity_.instanceId() > 1) {
        brandTitle += " #" + std::to_string(identity_.instanceId());
    }
    drawText(brandTitle, { 62.0f, 10.0f, 200.0f, 31.0f }, fmtSubheading_, COL_TEXT_PRIMARY);

    drawPulseDot(67.0f, 39.5f, 3.4f, COL_SUCCESS);
    drawText("Online", { 76.0f, 30.0f, 180.0f, 48.0f }, fmtSmall_, COL_TEXT_SECONDARY);

    // Center: Dead-Center macOS Segmented Control Track + Liquid Spring Pill
    auto vStats = network_.viewerStats();
    bool hasSession = (vStats.state != ViewerConnectionState::Disconnected) || (sessionTabs_.tabCount() > 0);

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
    fillRoundRect(slidingPill.offset(0.0f, 1.8f), 8.5f, withAlpha(COL_PRIMARY_ACCENT, 0.24f));
    fillRoundRect(slidingPill, 8.5f, COL_PRIMARY_ACCENT);

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
            : (sessionTabs_.tabCount() > 1 ? ("Sessions (" + std::to_string(sessionTabs_.tabCount()) + ")") : "Session");
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
               (showFileDrawer_ || unreadChat > 0) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (showFileDrawer_ || unreadChat > 0) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
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

void CppDeskWindow::drawDashboardView(const UiRect& bounds, float alpha) {
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

    bool elevated = InputInjector::isElevated();
    drawText("This Desk", { lx, curY, rx - (elevated ? 0.0f : 110.0f), curY + 26.0f }, fmtHeading_, withAlpha(COL_TEXT_PRIMARY, alpha));
    if (!elevated) {
        UiRect elevBtn = { rx - 108.0f, curY, rx, curY + 26.0f };
        drawButton("btn_elevate_admin", elevBtn, "Elevate Admin",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_PRIMARY_ACCENT, 6.5f, [this]() {
                       if (InputInjector::relaunchAsAdmin(hwnd_)) {
                           PostQuitMessage(0);
                       }
                   }, fmtSmall_, true, COL_BORDER);
    }
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
             fmtHeroId_, withAlpha(COL_PRIMARY_ACCENT, alpha));

    UiRect copyIdBtn = { idBox.right - 98.0f, idBox.top + 25.0f, idBox.right - 16.0f, idBox.bottom - 17.0f };
    drawButton("btn_copy_id", copyIdBtn, "Copy ID",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.5f, [this]() {
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
    drawText(identity_.sessionCode(), codeBadge, fmtMono_, COL_PRIMARY_ACCENT, DWRITE_TEXT_ALIGNMENT_CENTER);

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
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 7.5f, [this]() {
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
        drawPulseDot(hx + 5.0f, hy + 10.0f, 4.0f, COL_PRIMARY_ACCENT, alpha);
        drawText("ACTIVE SESSION", { hx + 16.0f, hy, hrx, hy + 20.0f }, fmtSmall_, COL_PRIMARY_ACCENT);
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
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 9.0f, [this]() {
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

    // Address Book search bar & tag filter chips (v2.1.0)
    float searchW = 160.0f;
    UiRect searchBox = { px, py, px + searchW, py + 26.0f };
    drawTextField("field_dash_search", FocusedField::DashboardSearch, searchBox,
                  dashboardSearchQuery_, "Search desks...", false);

    // Tag filter chips: All, Favorites, Work, Servers, Personal
    float chipX = searchBox.right + 8.0f;
    std::vector<std::string> tags = { "All", "Favorites", "Work", "Servers", "Personal" };
    for (const auto& t : tags) {
        float chipW = static_cast<float>(t.size()) * 7.2f + 16.0f;
        if (chipX + chipW > prx) break;
        UiRect chipRect = { chipX, py, chipX + chipW, py + 26.0f };
        bool isActive = (dashboardFilterTag_ == t);
        drawButton("tag_chip_" + t, chipRect, t,
                   isActive ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   isActive ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   isActive ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   6.0f, [this, t]() {
                       dashboardFilterTag_ = t;
                   }, fmtSmall_, !isActive, COL_BORDER,
                   isActive ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        chipX = chipRect.right + 5.0f;
    }
    py += 34.0f;

    struct PeerCardItem {
        uint64_t    deskId;
        std::string hostname;
        std::string endpoint;
        bool        isLive;
        bool        isFavorite;
        bool        isFromRecent;
        std::string alias;
        std::string tag;
        std::string notes;
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
        cardItems.push_back({ r.deskId, r.hostname, ep, liveNow, true, true, r.alias, r.tag, r.notes });
    }

    for (const auto& d : discovered) {
        bool already = false;
        for (const auto& c : cardItems) {
            if (c.deskId == d.deskId) { already = true; break; }
        }
        if (!already) {
            std::string dAlias, dTag, dNotes;
            for (const auto& r : recents) {
                if (r.deskId == d.deskId) {
                    dAlias = r.alias;
                    dTag = r.tag;
                    dNotes = r.notes;
                    break;
                }
            }
            cardItems.push_back({ d.deskId, d.hostname, d.ip + ":" + std::to_string(d.port), true, false, false, dAlias, dTag, dNotes });
        }
    }

    for (const auto& r : recents) {
        bool already = false;
        for (const auto& c : cardItems) {
            if (r.deskId > 0 && c.deskId == r.deskId) { already = true; break; }
        }
        if (!already) {
            cardItems.push_back({ r.deskId, r.hostname, r.address, false, r.isFavorite, true, r.alias, r.tag, r.notes });
        }
    }

    // Filter cards by Tag and Search Query
    {
        std::vector<PeerCardItem> filtered;
        std::string qLower = dashboardSearchQuery_;
        std::transform(qLower.begin(), qLower.end(), qLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (const auto& item : cardItems) {
            if (dashboardFilterTag_ == "Favorites") {
                if (!item.isFavorite) continue;
            } else if (dashboardFilterTag_ != "All") {
                std::string itemTagLower = item.tag;
                std::transform(itemTagLower.begin(), itemTagLower.end(), itemTagLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::string filterTagLower = dashboardFilterTag_;
                std::transform(filterTagLower.begin(), filterTagLower.end(), filterTagLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (itemTagLower != filterTagLower) continue;
            }

            if (!qLower.empty()) {
                std::string haystack = std::to_string(item.deskId) + " " +
                                       CryptoUtils::formatDeskId(item.deskId) + " " +
                                       item.hostname + " " + item.endpoint + " " +
                                       item.alias + " " + item.tag + " " + item.notes;
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (haystack.find(qLower) == std::string::npos) {
                    continue;
                }
            }
            filtered.push_back(item);
        }
        cardItems = std::move(filtered);
    }

    if (cardItems.empty()) {
        UiRect emptyBox = { px, py + 4.0f, prx, peersCard.bottom - 24.0f };
        fillRoundRect(emptyBox, 13.0f, COL_BG_SUBTLE);
        strokeRoundRect(emptyBox, 13.0f, COL_BORDER);

        float cyEmpty = emptyBox.centerY();
        drawPulseDot(emptyBox.centerX(), cyEmpty - 24.0f, 5.5f, COL_PRIMARY_ACCENT, alpha);
        drawText("No matching desks found",
                 { emptyBox.left + 24.0f, cyEmpty - 8.0f, emptyBox.right - 24.0f, cyEmpty + 16.0f },
                 fmtSubheading_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_CENTER);
        drawText("Try changing search keywords or active filter tag.",
                 { emptyBox.left + 24.0f, cyEmpty + 18.0f, emptyBox.right - 24.0f, cyEmpty + 42.0f },
                 fmtSmall_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        int cols = 2;
        float gap = 14.0f;
        float cardW = (prx - px - gap) / 2.0f;
        float cardH = 96.0f;

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
            strokeRoundRect(cardR, 12.0f, lerpColor(cardItems[i].isFavorite ? COL_BORDER_ALT : COL_BORDER, COL_PRIMARY_ACCENT, std::clamp(cardHover, 0.0f, 1.0f) * 0.6f), 1.0f);

            if (cardItems[i].isLive) {
                drawPulseDot(cardR.left + 19.0f, cardR.top + 18.0f, 3.4f, COL_SUCCESS, alpha);
                std::string badge = cardItems[i].isFavorite ? "FAVORITE • ONLINE" : "ONLINE";
                if (!cardItems[i].tag.empty()) badge += " • [" + cardItems[i].tag + "]";
                drawText(badge, { cardR.left + 28.0f, cardR.top + 10.0f, cardR.right - 92.0f, cardR.top + 26.0f },
                         fmtSmall_, cardItems[i].isFavorite ? COL_PRIMARY_ACCENT : COL_TEXT_SECONDARY);
            } else {
                std::string badge = cardItems[i].isFavorite ? "FAVORITE" : "RECENT";
                if (!cardItems[i].tag.empty()) badge += " • [" + cardItems[i].tag + "]";
                drawText(badge, { cardR.left + 14.0f, cardR.top + 10.0f, cardR.right - 92.0f, cardR.top + 26.0f },
                         fmtSmall_, cardItems[i].isFavorite ? COL_PRIMARY_ACCENT : COL_TEXT_MUTED);
            }

            uint64_t peerId = cardItems[i].deskId;
            std::string peerHost = cardItems[i].hostname;
            std::string peerEp = cardItems[i].endpoint;

            // Edit Alias / Tag / Notes Button
            if (peerId > 0) {
                UiRect editBtn = { cardR.right - 92.0f, cardR.top + 7.0f, cardR.right - 68.0f, cardR.top + 27.0f };
                drawButton("peer_edit_" + std::to_string(i), editBtn, "✎",
                           COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_SECONDARY,
                           6.0f, [this, peerId, item = cardItems[i]]() {
                               editingDeskId_ = peerId;
                               editAliasInput_ = item.alias;
                               editTagInput_ = item.tag;
                               editNotesInput_ = item.notes;
                               showAddressBookEditModal_ = true;
                           }, fmtSmall_, true, COL_BORDER, COL_PRIMARY_ACCENT);
            }

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
                             cardItems[i].isFavorite ? COL_PRIMARY_ACCENT : COL_TEXT_SECONDARY);
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

            std::string idFormatted = (peerId > 0) ? CryptoUtils::formatDeskId(peerId) : peerEp;
            std::string mainTitle = !cardItems[i].alias.empty() ? cardItems[i].alias : idFormatted;
            drawText(mainTitle, { cardR.left + 14.0f, cardR.top + 28.0f, cardR.right - 108.0f, cardR.top + 54.0f },
                     fmtSubheading_, lerpColor(COL_TEXT_PRIMARY, COL_PRIMARY_ACCENT, std::clamp(cardHover, 0.0f, 1.0f) * 0.7f));

            std::string subInfo;
            if (!cardItems[i].alias.empty()) {
                subInfo = idFormatted + (!peerHost.empty() ? (" (" + peerHost + ")") : "");
            } else {
                subInfo = peerHost.empty() ? peerEp : peerHost;
            }
            drawText(subInfo, { cardR.left + 14.0f, cardR.top + 54.0f, cardR.right - 108.0f, cardR.bottom - 10.0f },
                     fmtSmall_, COL_TEXT_SECONDARY);

            std::string targetStr = (peerId > 0) ? CryptoUtils::formatDeskId(peerId) : peerEp;
            UiRect quickConnBtn = { cardR.right - 98.0f, cardR.top + 38.0f, cardR.right - 12.0f, cardR.bottom - 16.0f };
            drawButton(cardBtnId, quickConnBtn, "Connect",
                       COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.0f, [this, targetStr]() {
                           remoteIdInput_ = targetStr;
                           initiateConnection();
                       }, fmtSmall_);
        }
    }
}

// ---------------- Session Tab Bar (Feature 6) ----------------

void CppDeskWindow::drawSessionTabBar(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f || sessionTabs_.tabCount() == 0) return;

    fillRoundRect(bounds, 0.0f, COL_BG_SUBTLE);
    fillRoundRect({ bounds.left, bounds.bottom - 1.0f, bounds.right, bounds.bottom }, 0.0f, COL_BORDER);

    float curX = bounds.left + 8.0f;
    float tabH = 26.0f;
    float tabY = bounds.top + (bounds.height() - tabH) * 0.5f;
    size_t count = sessionTabs_.tabCount();
    float maxTabW = 160.0f;
    float minTabW = 100.0f;
    float availableW = bounds.width() - 60.0f;
    float tabW = std::clamp(availableW / static_cast<float>(std::max<size_t>(1, count)), minTabW, maxTabW);

    for (const auto& tab : sessionTabs_.tabs()) {
        bool isActive = (tab.id == sessionTabs_.activeTabId());
        UiRect chipRect = { curX, tabY, curX + tabW, tabY + tabH };

        D2D1_COLOR_F chipBg = isActive ? COL_BG_CARD : withAlpha(COL_BG_INPUT, 0.75f);
        D2D1_COLOR_F chipBorder = isActive ? COL_PRIMARY_ACCENT : COL_BORDER;

        // Base chip click area
        std::string tabChipId = "tab_chip_" + std::to_string(tab.id);
        drawButton(tabChipId, chipRect, "", chipBg, withAlpha(chipBg, 0.85f), COL_TEXT_PRIMARY, 6.0f, [this, tabId = tab.id]() {
            switchToSessionTab(tabId);
        }, nullptr, true, chipBorder);

        // Status indicator dot
        bool isConn = (tab.state == ViewerConnectionState::Connected);
        bool isConnecting = (tab.state == ViewerConnectionState::ConnectingTcp ||
                             tab.state == ViewerConnectionState::ResolvingId ||
                             tab.state == ViewerConnectionState::Authenticating ||
                             tab.state == ViewerConnectionState::WaitingApproval ||
                             tab.state == ViewerConnectionState::Reconnecting);
        D2D1_COLOR_F dotColor = isConn ? COL_SUCCESS : (isConnecting ? COL_WARNING : COL_TEXT_MUTED);
        drawPulseDot(chipRect.left + 10.0f, chipRect.centerY(), 3.2f, dotColor, alpha);

        // Title
        std::string displayTitle = tab.title.empty() ? (tab.targetInput.empty() ? ("Desk " + std::to_string(tab.deskId)) : tab.targetInput) : tab.title;
        UiRect titleRect = { chipRect.left + 18.0f, chipRect.top, chipRect.right - 22.0f, chipRect.bottom };
        drawText(displayTitle, titleRect, fmtSmall_, isActive ? COL_TEXT_PRIMARY : COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_LEADING);

        // Close button '×'
        UiRect closeRect = { chipRect.right - 20.0f, chipRect.top + 3.0f, chipRect.right - 4.0f, chipRect.bottom - 3.0f };
        std::string closeBtnId = "tab_close_" + std::to_string(tab.id);
        drawButton(closeBtnId, closeRect, "×", D2D1::ColorF(0, 0, 0, 0), withAlpha(COL_DANGER, 0.35f),
                   isActive ? COL_TEXT_PRIMARY : COL_TEXT_MUTED, 4.0f, [this, tabId = tab.id]() {
            closeSessionTab(tabId);
        }, fmtSmall_);

        curX += tabW + 4.0f;
    }

    // New Tab '+' button
    UiRect plusRect = { curX + 2.0f, tabY + 1.0f, curX + 26.0f, tabY + tabH - 1.0f };
    drawButton("tab_plus_btn", plusRect, "+", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 5.0f, [this]() {
        switchTab(ActiveTab::Dashboard);
        focusedField_ = FocusedField::RemoteId;
    }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
}

// ---------------- Remote Session View ----------------

void CppDeskWindow::drawRemoteSessionView(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f) return;

    auto stats = network_.viewerStats();
    const auto& appSett = identity_.settings();

    UiRect stageRect = bounds;

    if (!isFullscreen_) {
        float tabBarH = (sessionTabs_.tabCount() > 0) ? 36.0f : 0.0f;
        if (tabBarH > 0.0f) {
            UiRect tabBarRect = { bounds.left, bounds.top, bounds.right, bounds.top + tabBarH };
            drawSessionTabBar(tabBarRect, alpha);
        }

        float barH = 48.0f;
        UiRect hudBar = { bounds.left, bounds.top + tabBarH, bounds.right, bounds.top + tabBarH + barH };
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
                       if (sessionRecorder_.isRecording()) {
                           sessionRecorder_.stopRecording();
                       }
                       if (network_.isVoiceIntercomActive()) {
                           network_.stopVoiceIntercom();
                       }
                       network_.disconnectViewer();
                       if (sessionTabs_.tabCount() > 0) {
                           closeSessionTab(sessionTabs_.activeTabId());
                       } else {
                           switchTab(ActiveTab::Dashboard);
                           showToast("Disconnected");
                       }
                   }, fmtSmall_);
        rx = discBtn.left - 6.0f;

        UiRect fsBtn = { rx - 84.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_fullscreen", fsBtn, "Fullscreen",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                       toggleFullscreen();
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        rx = fsBtn.left - 6.0f;

        uint32_t unreadChat = network_.unreadChatCount();
        bool chatOpen = (showFileDrawer_ && drawerTab_ == DrawerTab::LiveChat);
        std::string chatLabel = (unreadChat > 0) ? ("Chat (" + std::to_string(unreadChat) + ")") : "Chat";
        UiRect chatBtn = { rx - 76.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_chat", chatBtn, chatLabel,
                   (chatOpen || unreadChat > 0) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   (chatOpen || unreadChat > 0) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   (chatOpen || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this, chatOpen]() {
                       if (chatOpen) {
                           showFileDrawer_ = false;
                       } else {
                           showFileDrawer_ = true;
                           drawerTab_ = DrawerTab::LiveChat;
                           network_.markChatRead();
                           focusedField_ = FocusedField::ChatInput;
                       }
                   }, fmtSmall_, !(chatOpen || unreadChat > 0), COL_BORDER,
                   (chatOpen || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = chatBtn.left - 6.0f;

        UiRect shotBtn = { rx - 80.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_screenshot", shotBtn, "Screenshot",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this]() {
                       saveRemoteScreenshot();
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        rx = shotBtn.left - 6.0f;

        bool isRec = sessionRecorder_.isRecording();
        std::string recLabel;
        if (isRec) {
            uint64_t durSec = sessionRecorder_.durationSeconds();
            char durBuf[32];
            std::snprintf(durBuf, sizeof(durBuf), "REC %02llu:%02llu",
                          (unsigned long long)(durSec / 60), (unsigned long long)(durSec % 60));
            recLabel = durBuf;
        } else {
            recLabel = "Record";
        }
        float recBtnWidth = isRec ? 92.0f : 68.0f;
        UiRect recBtn = { rx - recBtnWidth, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_record", recBtn, recLabel,
                   isRec ? COL_DANGER : COL_SEC_BTN_BG,
                   isRec ? COL_DANGER_HV : COL_SEC_BTN_HV,
                   isRec ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       toggleScreenRecording();
                   }, fmtSmall_, !isRec, isRec ? COL_DANGER : COL_BORDER,
                   isRec ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = recBtn.left - 6.0f;

        bool voiceActive = network_.isVoiceIntercomActive();
        bool voiceMuted = network_.isVoiceIntercomMicMuted();
        std::string voiceLabel;
        if (!voiceActive) {
            voiceLabel = "Intercom";
        } else if (voiceMuted) {
            voiceLabel = "Mic: Muted";
        } else {
            float lvl = network_.voiceIntercomInputLevel();
            if (lvl > 0.35f) {
                voiceLabel = "Talk [|||]";
            } else if (lvl > 0.08f) {
                voiceLabel = "Talk [||.]";
            } else {
                voiceLabel = "Talk [|..]";
            }
        }

        UiRect voiceBtn = { rx - 78.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_voice_intercom", voiceBtn, voiceLabel,
                   voiceActive ? (voiceMuted ? COL_SEC_BTN_BG : rgba(16, 185, 129, 0.95f)) : COL_SEC_BTN_BG,
                   voiceActive ? (voiceMuted ? COL_SEC_BTN_HV : rgba(5, 150, 105, 0.95f)) : COL_SEC_BTN_HV,
                   voiceActive ? (voiceMuted ? COL_TEXT_PRIMARY : COL_TEXT_ON_ACCENT) : COL_TEXT_PRIMARY,
                   7.5f, [this, voiceActive, voiceMuted]() {
                       if (!voiceActive) {
                           if (network_.startVoiceIntercom()) {
                               showToast("Voice Intercom active (Mic live)");
                           } else {
                               showToast("Failed to open microphone", true);
                           }
                       } else if (!voiceMuted) {
                           network_.setVoiceIntercomMicMuted(true);
                           showToast("Intercom Mic muted");
                       } else {
                           network_.stopVoiceIntercom();
                           showToast("Voice Intercom stopped");
                       }
                   }, fmtSmall_, !voiceActive || voiceMuted, COL_BORDER,
                   (voiceActive && !voiceMuted) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = voiceBtn.left - 6.0f;

        bool diagOpen = (showFileDrawer_ && drawerTab_ == DrawerTab::Diagnostics);
        UiRect taskBtn = { rx - 74.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_taskmgr", taskBtn, "Task Mgr",
                   diagOpen ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   diagOpen ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   diagOpen ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this, diagOpen]() {
                       if (diagOpen) {
                           network_.setDiagnosticsActive(false);
                           showFileDrawer_ = false;
                       } else {
                           showFileDrawer_ = true;
                           drawerTab_ = DrawerTab::Diagnostics;
                           network_.setDiagnosticsActive(true);
                       }
                   }, fmtSmall_, !diagOpen, COL_BORDER,
                   diagOpen ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = taskBtn.left - 6.0f;

        std::string scaleLabel = (scaleMode_ == ScaleMode::FitAspect) ? "Scale: Fit" :
                                 (scaleMode_ == ScaleMode::FillAspect) ? "Scale: Fill" :
                                 (scaleMode_ == ScaleMode::Stretch) ? "Scale: Stretch" : "Scale: 1:1";
        UiRect scaleBtn = { rx - 88.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_scale", scaleBtn, scaleLabel,
                   (scaleMode_ == ScaleMode::FillAspect) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   (scaleMode_ == ScaleMode::FillAspect) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   (scaleMode_ == ScaleMode::FillAspect) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       if (scaleMode_ == ScaleMode::FitAspect) scaleMode_ = ScaleMode::FillAspect;
                       else if (scaleMode_ == ScaleMode::FillAspect) scaleMode_ = ScaleMode::Stretch;
                       else if (scaleMode_ == ScaleMode::Stretch) scaleMode_ = ScaleMode::Original;
                       else scaleMode_ = ScaleMode::FitAspect;
                   }, fmtSmall_, (scaleMode_ != ScaleMode::FillAspect), COL_BORDER,
                   (scaleMode_ == ScaleMode::FillAspect) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = scaleBtn.left - 6.0f;

        UiRect matchBtn = { rx - 84.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_match_res", matchBtn, "Match Res",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.5f, [this, bounds]() {
                       float targetW = bounds.width();
                       float targetH = std::max(480.0f, bounds.height() - 48.0f);
                       network_.requestHostResolution(static_cast<uint32_t>(targetW), static_cast<uint32_t>(targetH), 2);
                       showToast("Requested host resolution match (" + std::to_string(static_cast<int>(targetW)) + "x" + std::to_string(static_cast<int>(targetH)) + ")");
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);
        rx = matchBtn.left - 6.0f;

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
                   inputActive ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   inputActive ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
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
        rx = inputBtn.left - 6.0f;

        // Audio Mute / Volume Button
        bool isMuted = network_.isAudioMuted();
        int vol = network_.audioVolume();
        std::string audioLabel = isMuted ? "Audio: Mute" : ("Vol: " + std::to_string(vol) + "%");
        UiRect audioBtn = { rx - 88.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_audio_btn", audioBtn, audioLabel,
                   isMuted ? COL_SEC_BTN_BG : COL_PRIMARY_ACCENT,
                   isMuted ? COL_SEC_BTN_HV : COL_PRIMARY_ACCENT_HV,
                   isMuted ? COL_TEXT_PRIMARY : COL_TEXT_ON_ACCENT,
                   7.5f, [this, isMuted]() {
                       network_.setAudioMuted(!isMuted);
                       showToast(!isMuted ? "Audio muted" : "Audio unmuted");
                   }, fmtSmall_, isMuted, COL_BORDER,
                   isMuted ? COL_TEXT_ACCENT : COL_TEXT_ON_ACCENT);
        rx = audioBtn.left - 6.0f;

        // Privacy Mode Curtain Button
        bool privacyOn = stats.privacyModeEngaged;
        std::string privLabel = privacyOn ? "Privacy: ON" : "Privacy: OFF";
        UiRect privBtn = { rx - 92.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_privacy_btn", privBtn, privLabel,
                   privacyOn ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   privacyOn ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   privacyOn ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       network_.requestTogglePrivacyMode();
                   }, fmtSmall_, !privacyOn, COL_BORDER,
                   privacyOn ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = privBtn.left - 6.0f;

        // Port Forwarding Tunnels Button
        UiRect tunnelsBtn = { rx - 76.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_tunnels_btn", tunnelsBtn, "Tunnels",
                   showPortForwardModal_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   showPortForwardModal_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   showPortForwardModal_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       showPortForwardModal_ = !showPortForwardModal_;
                   }, fmtSmall_, !showPortForwardModal_, COL_BORDER,
                   showPortForwardModal_ ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = tunnelsBtn.left - 6.0f;

        // Whiteboard Annotation Button
        std::string wbLabel = whiteboardActive_ ? "Board: ON" : "Board";
        UiRect wbBtn = { rx - 78.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_whiteboard_btn", wbBtn, wbLabel,
                   whiteboardActive_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   whiteboardActive_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   whiteboardActive_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       whiteboardActive_ = !whiteboardActive_;
                       showToast(whiteboardActive_ ? "Whiteboard active" : "Whiteboard hidden");
                   }, fmtSmall_, !whiteboardActive_, COL_BORDER,
                   whiteboardActive_ ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
        rx = wbBtn.left - 6.0f;

        // Performance HUD Button
        std::string hudLabel = showPerformanceHud_ ? "HUD: ON" : "HUD";
        UiRect hudBtn = { rx - 72.0f, hudBar.top + 8.0f, rx, hudBar.bottom - 8.0f };
        drawButton("sess_hud_btn", hudBtn, hudLabel,
                   showPerformanceHud_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   showPerformanceHud_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   showPerformanceHud_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   7.5f, [this]() {
                       showPerformanceHud_ = !showPerformanceHud_;
                       showToast(showPerformanceHud_ ? "Performance HUD: ON" : "Performance HUD: OFF");
                   }, fmtSmall_, !showPerformanceHud_, COL_BORDER,
                   showPerformanceHud_ ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

        stageRect = { bounds.left, hudBar.bottom, bounds.right, bounds.bottom };
    }
    stageRect_ = stageRect;

    // Remote Desktop Canvas Stage
    fillRoundRect(stageRect, 0.0f, COL_STAGE_BG);

    if (network_.copyLatestViewerFrame(displayedFrameSeq_, frameBufferBgra_, frameBufferW_, frameBufferH_, remoteCursor_)) {
        sessionTabs_.cacheActiveTabFrame(frameBufferBgra_.data(), frameBufferW_, frameBufferH_, displayedFrameSeq_, remoteCursor_);
        if (frameBufferW_ > 0 && frameBufferH_ > 0 && renderTarget_) {
            if (sessionRecorder_.isRecording()) {
                sessionRecorder_.pushFrame(frameBufferBgra_.data(), frameBufferW_, frameBufferH_);
            }
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
        float availW = isFullscreen_ ? stageRect.width() : (stageRect.width() - 24.0f);
        float availH = isFullscreen_ ? stageRect.height() : (stageRect.height() - 24.0f);
        float drawW = availW;
        float drawH = availH;

        if (scaleMode_ == ScaleMode::FitAspect) {
            float scale = std::min(availW / bitmapW_, availH / bitmapH_);
            drawW = bitmapW_ * scale;
            drawH = bitmapH_ * scale;
        } else if (scaleMode_ == ScaleMode::FillAspect) {
            float scale = std::max(availW / bitmapW_, availH / bitmapH_);
            drawW = bitmapW_ * scale;
            drawH = bitmapH_ * scale;
        } else if (scaleMode_ == ScaleMode::Original) {
            drawW = std::min(availW, static_cast<float>(bitmapW_));
            drawH = std::min(availH, static_cast<float>(bitmapH_));
        }

        float left = stageRect.left + (stageRect.width() - drawW) * 0.5f;
        float top  = stageRect.top + (stageRect.height() - drawH) * 0.5f;
        renderedCanvasRect_ = { left, top, left + drawW, top + drawH };

        if (!isFullscreen_) {
            drawCardShadow(renderedCanvasRect_, 8.0f, alpha);
            strokeRoundRect(renderedCanvasRect_.inflate(1.2f, 1.2f), 6.0f, COL_BORDER_ALT, 1.2f);
        }

        if (scaleMode_ == ScaleMode::FillAspect) {
            D2D1_RECT_F clipRect = D2D1::RectF(stageRect.left, stageRect.top, stageRect.right, stageRect.bottom);
            renderTarget_->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }

        D2D1_RECT_F dRect = D2D1::RectF(renderedCanvasRect_.left, renderedCanvasRect_.top,
                                        renderedCanvasRect_.right, renderedCanvasRect_.bottom);
        renderTarget_->DrawBitmap(remoteBitmap_, dRect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

        if (scaleMode_ == ScaleMode::FillAspect) {
            renderTarget_->PopAxisAlignedClip();
        }

        if (remoteCursor_.visible && appSett.showRemoteCursor) {
            float curX = renderedCanvasRect_.left + remoteCursor_.normX * renderedCanvasRect_.width();
            float curY = renderedCanvasRect_.top + remoteCursor_.normY * renderedCanvasRect_.height();
            solidBrush_->SetColor(withAlpha(COL_PRIMARY_ACCENT, 0.92f));
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(curX, curY), 5.0f, 5.0f), solidBrush_);
            solidBrush_->SetColor(rgba(255, 255, 255, 0.98f));
            renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(curX, curY), 6.0f, 6.0f), solidBrush_, 1.6f);
        }

        // Draw Whiteboard overlay annotations and floating tool palette
        drawWhiteboardOverlay(renderedCanvasRect_);

        // Direct Canvas Drag-and-Drop Confirmation Ripple Effect (Feature 2)
        if (canvasDropEffectActive_) {
            uint64_t elapsed = GetTickCount64() - canvasDropTick_;
            if (elapsed < 1400) {
                float t = static_cast<float>(elapsed) / 1400.0f;
                float ripRadius = 16.0f + 64.0f * std::sin(t * 1.57079f);
                float ripAlpha = (1.0f - t) * alpha;

                solidBrush_->SetColor(withAlpha(COL_PRIMARY_ACCENT, ripAlpha * 0.7f));
                renderTarget_->DrawEllipse(D2D1::Ellipse(canvasDropPos_, ripRadius, ripRadius), solidBrush_, 2.4f);

                solidBrush_->SetColor(withAlpha(COL_PRIMARY_ACCENT, ripAlpha * 0.35f));
                renderTarget_->FillEllipse(D2D1::Ellipse(canvasDropPos_, 12.0f * (1.0f - t), 12.0f * (1.0f - t)), solidBrush_);

                UiRect badge = { canvasDropPos_.x - 85.0f, canvasDropPos_.y + ripRadius + 6.0f,
                                 canvasDropPos_.x + 85.0f, canvasDropPos_.y + ripRadius + 28.0f };
                fillRoundRect(badge, 6.0f, rgba(15, 23, 42, ripAlpha * 0.92f));
                strokeRoundRect(badge, 6.0f, withAlpha(COL_PRIMARY_ACCENT, ripAlpha * 0.5f));
                drawText("Dropped " + std::to_string(canvasDropCount_) + " file(s)", badge, fmtSmall_,
                         withAlpha(COL_TEXT_PRIMARY, ripAlpha), DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                canvasDropEffectActive_ = false;
            }
        }

        // Floating In-Canvas File Transfer Progress HUD Pill (Feature 2)
        auto transfers = network_.fileTransferManager().snapshotTransfers();
        const FileTransferItem* activeItem = nullptr;
        for (const auto& item : transfers) {
            if (item.status == TransferStatus::InProgress) {
                activeItem = &item;
                break;
            }
        }

        if (activeItem && !showFileDrawer_) {
            float pillW = 280.0f;
            float pillH = 46.0f;
            UiRect hudPill = { renderedCanvasRect_.right - pillW - 16.0f,
                               renderedCanvasRect_.bottom - pillH - 16.0f,
                               renderedCanvasRect_.right - 16.0f,
                               renderedCanvasRect_.bottom - 16.0f };

            fillRoundRect(hudPill, 9.0f, rgba(12, 16, 24, 0.94f));
            strokeRoundRect(hudPill, 9.0f, COL_BORDER_ALT);

            float pct = (activeItem->totalBytes > 0)
                ? std::clamp(static_cast<float>(activeItem->transferredBytes) / static_cast<float>(activeItem->totalBytes), 0.0f, 1.0f)
                : 0.0f;

            char pctBuf[32];
            std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", pct * 100.0f);

            std::string dirIcon = activeItem->isOutgoing ? "^" : "v";
            drawText(dirIcon, { hudPill.left + 10.0f, hudPill.top + 6.0f, hudPill.left + 24.0f, hudPill.top + 22.0f },
                     fmtBodyBold_, COL_PRIMARY_ACCENT);

            drawText(activeItem->fileName, { hudPill.left + 26.0f, hudPill.top + 6.0f, hudPill.right - 50.0f, hudPill.top + 22.0f },
                     fmtSmall_, COL_TEXT_PRIMARY);

            drawText(pctBuf, { hudPill.right - 48.0f, hudPill.top + 6.0f, hudPill.right - 10.0f, hudPill.top + 22.0f },
                     fmtSmall_, COL_TEXT_ACCENT, DWRITE_TEXT_ALIGNMENT_TRAILING);

            UiRect barBg = { hudPill.left + 10.0f, hudPill.bottom - 14.0f, hudPill.right - 10.0f, hudPill.bottom - 8.0f };
            fillRoundRect(barBg, 3.0f, rgba(255, 255, 255, 0.08f));
            UiRect barFill = { barBg.left, barBg.top, barBg.left + barBg.width() * pct, barBg.bottom };
            fillRoundRect(barFill, 3.0f, COL_PRIMARY_ACCENT);

            clickRegions_.push_back({ hudPill, "canvas_transfer_hud", [this]() {
                drawerTab_ = DrawerTab::FilesAndClip;
                showFileDrawer_ = true;
            }, false });
        }

        // Screen Recording Live Watermark Pill (Feature 3)
        if (sessionRecorder_.isRecording()) {
            float recPillW = 120.0f;
            float recPillH = 30.0f;
            UiRect recPill = { renderedCanvasRect_.left + 16.0f,
                               renderedCanvasRect_.top + 16.0f,
                               renderedCanvasRect_.left + 16.0f + recPillW,
                               renderedCanvasRect_.top + 16.0f + recPillH };

            fillRoundRect(recPill, 15.0f, rgba(15, 23, 42, 0.88f));
            strokeRoundRect(recPill, 15.0f, withAlpha(COL_DANGER, 0.65f), 1.2f);

            uint64_t tick = GetTickCount64();
            float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(tick % 1000) / 1000.0f * 6.28318f);
            D2D1_COLOR_F dotColor = D2D1::ColorF(0.95f, 0.2f, 0.2f, 0.5f + 0.5f * pulse);
            solidBrush_->SetColor(dotColor);
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(recPill.left + 16.0f, (recPill.top + recPill.bottom) * 0.5f), 5.0f, 5.0f), solidBrush_);

            uint64_t durSec = sessionRecorder_.durationSeconds();
            char recTimeBuf[32];
            std::snprintf(recTimeBuf, sizeof(recTimeBuf), "REC %02llu:%02llu",
                          (unsigned long long)(durSec / 60), (unsigned long long)(durSec % 60));

            UiRect recTextRect = { recPill.left + 28.0f, recPill.top, recPill.right - 8.0f, recPill.bottom };
            drawText(recTimeBuf, recTextRect, fmtSmall_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        // Voice Intercom Live In-Canvas Pill (Feature 4)
        if (network_.isVoiceIntercomActive()) {
            float topY = sessionRecorder_.isRecording()
                ? (renderedCanvasRect_.top + 16.0f + 30.0f + 8.0f)
                : (renderedCanvasRect_.top + 16.0f);
            float icomW = 120.0f;
            float icomH = 30.0f;
            UiRect icomPill = { renderedCanvasRect_.left + 16.0f,
                                topY,
                                renderedCanvasRect_.left + 16.0f + icomW,
                                topY + icomH };

            bool isMuted = network_.isVoiceIntercomMicMuted();
            D2D1_COLOR_F accentCol = isMuted ? rgba(239, 68, 68, 0.9f) : rgba(16, 185, 129, 0.9f);

            fillRoundRect(icomPill, 15.0f, rgba(15, 23, 42, 0.88f));
            strokeRoundRect(icomPill, 15.0f, withAlpha(accentCol, 0.65f), 1.2f);

            // Audio level indicator dot with size responding to RMS volume
            float lvl = network_.voiceIntercomInputLevel();
            float dotRadius = 4.0f + std::min(lvl * 12.0f, 4.0f);
            solidBrush_->SetColor(accentCol);
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(icomPill.left + 16.0f, (icomPill.top + icomPill.bottom) * 0.5f), dotRadius, dotRadius), solidBrush_);

            std::string icomText = isMuted ? "Mic: Muted" : "Voice: Live";
            UiRect icomTextRect = { icomPill.left + 28.0f, icomPill.top, icomPill.right - 8.0f, icomPill.bottom };
            drawText(icomText, icomTextRect, fmtSmall_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    } else {
        renderedCanvasRect_ = {};
        if (network_.isAutoReconnectingWithToken()) {
            float bannerW = 440.0f;
            float bannerH = 150.0f;
            UiRect bannerRect = { stageRect.centerX() - bannerW * 0.5f,
                                  stageRect.centerY() - bannerH * 0.5f,
                                  stageRect.centerX() + bannerW * 0.5f,
                                  stageRect.centerY() + bannerH * 0.5f };
            drawCardShadow(bannerRect, 14.0f, 0.95f);
            fillRoundRect(bannerRect, 14.0f, rgba(15, 23, 42, 0.96f));
            strokeRoundRect(bannerRect, 14.0f, withAlpha(COL_PRIMARY_ACCENT, 0.6f), 1.2f);

            drawPulseDot(bannerRect.centerX(), bannerRect.top + 28.0f, 5.0f, COL_PRIMARY_ACCENT);
            drawText("Host Restarting...", { bannerRect.left + 20.0f, bannerRect.top + 42.0f, bannerRect.right - 20.0f, bannerRect.top + 68.0f },
                     fmtHeading_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_CENTER);

            std::string subText = "Auto-reconnecting with security token...\nWaiting for remote system to come back online.";
            drawText(subText, { bannerRect.left + 20.0f, bannerRect.top + 70.0f, bannerRect.right - 20.0f, bannerRect.bottom - 46.0f },
                     fmtSmall_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_CENTER);

            UiRect cancelBtn = { bannerRect.centerX() - 70.0f, bannerRect.bottom - 38.0f, bannerRect.centerX() + 70.0f, bannerRect.bottom - 10.0f };
            drawButton("btn_cancel_reboot_rec", cancelBtn, "Cancel Reconnect",
                       COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
                           network_.cancelAutoReconnection();
                           network_.disconnectViewer();
                           showToast("Auto-reconnection cancelled");
                       }, fmtSmall_, true, COL_BORDER);
        } else {
            drawText(stats.statusMessage, stageRect, fmtSubheading_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // Real-Time Performance & Diagnostics HUD Overlay (Phase 10)
    drawPerformanceHud(stageRect_, alpha);
}

// ---------------- macOS System Settings View ----------------

void CppDeskWindow::drawSettingsView(const UiRect& bounds, float alpha) {
    if (alpha <= 0.01f) return;

    float sL = std::clamp(tabEnterStaggerT_, 0.0f, 1.15f);
    float sR = std::clamp((tabEnterStaggerT_ - 0.05f) * 1.12f, 0.0f, 1.15f);
    float staggerL = (1.0f - sL) * 12.0f;
    float staggerR = (1.0f - sR) * 14.0f;

    float pad = 20.0f;

    // ---------------- TOP HERO BANNER: SOFTWARE UPDATE & VERSION (v3.0.0) ----------------
    UiRect updateBanner = UiRect{ bounds.left + pad, bounds.top + pad, bounds.right - pad, bounds.top + pad + 66.0f }.offset(0.0f, staggerL);
    drawCardShadow(updateBanner, 14.0f, alpha);
    drawCardSurface(updateBanner, 14.0f, alpha);

    float bPad = 16.0f;
    // App Emblem "CD"
    UiRect iconBadge = { updateBanner.left + bPad, updateBanner.top + 13.0f, updateBanner.left + bPad + 40.0f, updateBanner.top + 53.0f };
    fillRoundRect(iconBadge, 10.0f, COL_PRIMARY_ACCENT);
    drawText("CD", iconBadge, fmtSubheading_, COL_TEXT_ON_ACCENT, DWRITE_TEXT_ALIGNMENT_CENTER);

    // App Name & Product Value Proposition
    float infoX = iconBadge.right + 14.0f;
    UiRect titleRect = { infoX, updateBanner.top + 12.0f, updateBanner.right - 340.0f, updateBanner.top + 34.0f };
    drawText("CppDesk v" + std::string(CPP_DESK_VERSION) + "  •  Fast, secure remote desktop for Windows",
             titleRect, fmtSubheading_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);

    // Status Indicator Dot & Live Text
    float dotX = infoX + 5.0f;
    float dotY = updateBanner.top + 45.0f;
    D2D1_COLOR_F statusDotCol = latestUpdateInfo_.updateRequired ? COL_DANGER : (isCheckingUpdates_ ? COL_WARNING : COL_SUCCESS);
    drawPulseDot(dotX, dotY, 3.8f, statusDotCol);

    UiRect statusTextRect = { dotX + 11.0f, updateBanner.top + 35.0f, updateBanner.right - 340.0f, updateBanner.top + 55.0f };
    D2D1_COLOR_F statusTxtCol = latestUpdateInfo_.updateRequired ? COL_DANGER : COL_TEXT_SECONDARY;
    drawText(updateStatusText_, statusTextRect, fmtSmall_, statusTxtCol, DWRITE_TEXT_ALIGNMENT_LEADING);

    // Buttons on Right Side of Top Banner
    float btnH = 34.0f;
    float btnY = updateBanner.top + (updateBanner.height() - btnH) * 0.5f;

    // "Check for update" Button (exact text requested by user)
    float chkW = 150.0f;
    UiRect chkBtn = { updateBanner.right - bPad - chkW, btnY, updateBanner.right - bPad, btnY + btnH };
    drawButton("sett_check_updates_top", chkBtn, isCheckingUpdates_ ? "Checking..." : "Check for update",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT,
               8.0f, [this]() {
                   triggerUpdateCheck(true);
               }, fmtSmall_);

    // "Release Notes" Link Button
    float relW = 120.0f;
    UiRect relBtn = { chkBtn.left - 10.0f - relW, btnY, chkBtn.left - 10.0f, btnY + btnH };
    drawButton("sett_view_release_top", relBtn, "Release Notes",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   ShellExecuteA(nullptr, "open", "https://github.com/nmnghia2527/cppdesk/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    // ---------------- TWO-COLUMN SETTINGS BODY ----------------
    float totalW = bounds.width() - pad * 2.0f;
    float colW = (totalW - pad) * 0.5f;
    float cardTop = updateBanner.bottom + 12.0f;

    UiRect leftCard  = UiRect{ bounds.left + pad, cardTop, bounds.left + pad + colW, bounds.bottom - pad }.offset(0.0f, staggerL);
    UiRect rightCard = UiRect{ leftCard.right + pad, cardTop, bounds.right - pad, bounds.bottom - pad }.offset(0.0f, staggerR);

    const AppSettings s = identity_.settings();

    // ==================== LEFT COLUMN: APPEARANCE & DISPLAY ====================
    drawCardSurface(leftCard, 16.0f, alpha);

    float lx = leftCard.left + 24.0f;
    float lrx = leftCard.right - 24.0f;
    float ly = leftCard.top + 16.0f;
    float innerW = lrx - lx;

    drawText("Appearance & Display", { lx, ly, lrx, ly + 24.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    ly += 25.0f;
    drawText("Customize theme, frame rate, and display preferences.",
             { lx, ly, lrx, ly + 16.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    ly += 22.0f;

    // 1. Theme Segmented Box
    UiRect themeBox = { lx, ly, lrx, ly + 64.0f };
    fillRoundRect(themeBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(themeBox, 12.0f, COL_BORDER);

    drawText("THEME", { themeBox.left + 16.0f, themeBox.top + 8.0f, themeBox.right - 16.0f, themeBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);
    float halfBtnW = (innerW - 32.0f - 10.0f) * 0.5f;
    UiRect lightBtn = { themeBox.left + 16.0f, themeBox.top + 26.0f, themeBox.left + 16.0f + halfBtnW, themeBox.top + 56.0f };
    UiRect darkBtn  = { lightBtn.right + 10.0f, themeBox.top + 26.0f, themeBox.right - 16.0f, themeBox.top + 56.0f };

    drawButton("sett_theme_light", lightBtn, "Light",
               !s.darkTheme ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               !s.darkTheme ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               !s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   AppSettings ns = identity_.settings();
                   ns.darkTheme = false;
                   identity_.updateSettings(ns);
                   applyWindowThemeAttribute();
               }, fmtSmall_, s.darkTheme, COL_BORDER, !s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_theme_dark", darkBtn, "Dark",
               s.darkTheme ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               s.darkTheme ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   AppSettings ns = identity_.settings();
                   ns.darkTheme = true;
                   identity_.updateSettings(ns);
                   applyWindowThemeAttribute();
               }, fmtSmall_, !s.darkTheme, COL_BORDER, s.darkTheme ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ly = themeBox.bottom + 10.0f;

    // 2. Frame Rate
    UiRect fpsBox = { lx, ly, lrx, ly + 106.0f };
    fillRoundRect(fpsBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(fpsBox, 12.0f, COL_BORDER);

    drawText("FRAME RATE",
             { fpsBox.left + 16.0f, fpsBox.top + 8.0f, fpsBox.right - 16.0f, fpsBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float thirdW = (innerW - 32.0f - 16.0f) / 3.0f;
    uint8_t curFps = clampTargetFps(s.targetFps);
    UiRect fps15Btn = { fpsBox.left + 16.0f, fpsBox.top + 26.0f, fpsBox.left + 16.0f + thirdW, fpsBox.top + 58.0f };
    UiRect fps30Btn = { fps15Btn.right + 8.0f, fpsBox.top + 26.0f, fps15Btn.right + 8.0f + thirdW, fpsBox.top + 58.0f };
    UiRect fps60Btn = { fps30Btn.right + 8.0f, fpsBox.top + 26.0f, fpsBox.right - 16.0f, fpsBox.top + 58.0f };

    auto setFpsAction = [this](uint8_t fpsVal) {
        AppSettings ns = identity_.settings();
        ns.targetFps = fpsVal;
        identity_.updateSettings(ns);
        network_.setSessionFpsConfig(fpsVal, ns.adaptiveFps);
    };

    drawButton("sett_fps_15", fps15Btn, "15 FPS",
               (curFps == 15) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (curFps == 15) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (curFps == 15) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(15); }, fmtSmall_, curFps != 15, COL_BORDER,
               (curFps == 15) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_fps_30", fps30Btn, "30 FPS",
               (curFps == 30) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (curFps == 30) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (curFps == 30) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(30); }, fmtSmall_, curFps != 30, COL_BORDER,
               (curFps == 30) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_fps_60", fps60Btn, "60 FPS",
               (curFps == 60) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (curFps == 60) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (curFps == 60) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setFpsAction]() { setFpsAction(60); }, fmtSmall_, curFps != 60, COL_BORDER,
               (curFps == 60) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawToggleSwitch("sett_adaptive_fps", { fpsBox.left + 16.0f, fpsBox.top + 68.0f, fpsBox.right - 16.0f, fpsBox.top + 98.0f },
                     s.adaptiveFps, "Adjust automatically on slow connections", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.adaptiveFps = !ns.adaptiveFps;
                         identity_.updateSettings(ns);
                         network_.setSessionFpsConfig(ns.targetFps, ns.adaptiveFps);
                     });

    ly = fpsBox.bottom + 10.0f;

    // 3. Quality & Scaling
    UiRect qualBox = { lx, ly, lrx, ly + 114.0f };
    fillRoundRect(qualBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(qualBox, 12.0f, COL_BORDER);

    drawText("QUALITY & SCALING", { qualBox.left + 16.0f, qualBox.top + 8.0f, qualBox.right - 16.0f, qualBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    QualityPreset defQ = s.defaultQuality;
    UiRect qUltraBtn = { qualBox.left + 16.0f, qualBox.top + 26.0f, qualBox.left + 16.0f + thirdW, qualBox.top + 58.0f };
    UiRect qBalBtn   = { qUltraBtn.right + 8.0f, qualBox.top + 26.0f, qUltraBtn.right + 8.0f + thirdW, qualBox.top + 58.0f };
    UiRect qFastBtn  = { qBalBtn.right + 8.0f, qualBox.top + 26.0f, qualBox.right - 16.0f, qualBox.top + 58.0f };

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
               (defQ == QualityPreset::Ultra) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defQ == QualityPreset::Ultra) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::Ultra) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::Ultra); }, fmtSmall_, defQ != QualityPreset::Ultra, COL_BORDER,
               (defQ == QualityPreset::Ultra) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_q_bal", qBalBtn, "Balanced",
               (defQ == QualityPreset::Balanced) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defQ == QualityPreset::Balanced) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::Balanced) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::Balanced); }, fmtSmall_, defQ != QualityPreset::Balanced, COL_BORDER,
               (defQ == QualityPreset::Balanced) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_q_fast", qFastBtn, "Fast",
               (defQ == QualityPreset::LowBandwidth) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defQ == QualityPreset::LowBandwidth) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defQ == QualityPreset::LowBandwidth) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setQualAction]() { setQualAction(QualityPreset::LowBandwidth); }, fmtSmall_, defQ != QualityPreset::LowBandwidth, COL_BORDER,
               (defQ == QualityPreset::LowBandwidth) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    uint8_t defScale = s.defaultScaleMode;
    float quarterW = (qualBox.width() - 32.0f - 24.0f) / 4.0f;
    UiRect scFitBtn  = { qualBox.left + 16.0f, qualBox.top + 68.0f, qualBox.left + 16.0f + quarterW, qualBox.top + 100.0f };
    UiRect scFillBtn = { scFitBtn.right + 8.0f, qualBox.top + 68.0f, scFitBtn.right + 8.0f + quarterW, qualBox.top + 100.0f };
    UiRect scStrBtn  = { scFillBtn.right + 8.0f, qualBox.top + 68.0f, scFillBtn.right + 8.0f + quarterW, qualBox.top + 100.0f };
    UiRect scOrigBtn = { scStrBtn.right + 8.0f, qualBox.top + 68.0f, qualBox.right - 16.0f, qualBox.top + 100.0f };

    auto setScaleAction = [this](uint8_t scMode) {
        AppSettings ns = identity_.settings();
        ns.defaultScaleMode = scMode;
        identity_.updateSettings(ns);
        scaleMode_ = static_cast<ScaleMode>(scMode);
    };

    drawButton("sett_sc_fit", scFitBtn, "Fit",
               (defScale == 0) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defScale == 0) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defScale == 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(0); }, fmtSmall_, defScale != 0, COL_BORDER,
               (defScale == 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_sc_fill", scFillBtn, "Fill",
               (defScale == 3) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defScale == 3) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defScale == 3) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(3); }, fmtSmall_, defScale != 3, COL_BORDER,
               (defScale == 3) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_sc_str", scStrBtn, "Stretch",
               (defScale == 1) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defScale == 1) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defScale == 1) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(1); }, fmtSmall_, defScale != 1, COL_BORDER,
               (defScale == 1) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_sc_orig", scOrigBtn, "1:1",
               (defScale == 2) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (defScale == 2) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (defScale == 2) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [setScaleAction]() { setScaleAction(2); }, fmtSmall_, defScale != 2, COL_BORDER,
               (defScale == 2) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ly = qualBox.bottom + 10.0f;

    // 4. Session Display
    UiRect ovBox = { lx, ly, lrx, ly + 86.0f };
    fillRoundRect(ovBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(ovBox, 12.0f, COL_BORDER);

    drawText("SESSION DISPLAY", { ovBox.left + 16.0f, ovBox.top + 8.0f, ovBox.right - 16.0f, ovBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    drawToggleSwitch("sett_show_cursor", { ovBox.left + 16.0f, ovBox.top + 26.0f, ovBox.right - 16.0f, ovBox.top + 52.0f },
                     s.showRemoteCursor, "Show remote cursor", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.showRemoteCursor = !ns.showRemoteCursor;
                         identity_.updateSettings(ns);
                     });

    drawToggleSwitch("sett_show_hud", { ovBox.left + 16.0f, ovBox.top + 54.0f, ovBox.right - 16.0f, ovBox.top + 80.0f },
                     s.showSessionHud, "Show frame rate in session bar", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.showSessionHud = !ns.showSessionHud;
                         identity_.updateSettings(ns);
                     });

    ly = ovBox.bottom + 10.0f;

    // 5. About CppDesk (draw if at least 40px remaining)
    if (leftCard.bottom - 10.0f > ly + 40.0f) {
        UiRect aboutBox = { lx, ly, lrx, leftCard.bottom - 16.0f };
        fillRoundRect(aboutBox, 12.0f, COL_BG_SUBTLE);
        strokeRoundRect(aboutBox, 12.0f, COL_BORDER);

        float aPad = 16.0f;
        drawText("ABOUT CPPDESK",
                 { aboutBox.left + aPad, aboutBox.top + 8.0f, aboutBox.right - aPad, aboutBox.top + 22.0f },
                 fmtSmall_, COL_TEXT_ACCENT);

        float btnW = 68.0f;
        float btnH = 26.0f;
        float btnRight = aboutBox.right - aPad;
        float btnY = aboutBox.top + 15.0f;

        // Interactive "License" Pill Button
        UiRect licBtn = { btnRight - btnW, btnY, btnRight, btnY + btnH };
        drawButton("sett_about_license", licBtn, "License",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, []() {
                       ShellExecuteA(nullptr, "open", "https://github.com/nmnghia2527/cppdesk/blob/main/LICENSE", nullptr, nullptr, SW_SHOWNORMAL);
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

        // Interactive "GitHub" Pill Button
        UiRect ghBtn = { licBtn.left - 8.0f - btnW, btnY, licBtn.left - 8.0f, btnY + btnH };
        drawButton("sett_about_github", ghBtn, "GitHub",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, []() {
                       ShellExecuteA(nullptr, "open", "https://github.com/nmnghia2527/cppdesk", nullptr, nullptr, SW_SHOWNORMAL);
                   }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

        // App Title & Value Summary
        UiRect textRect = { aboutBox.left + aPad, aboutBox.top + 24.0f, ghBtn.left - 12.0f, aboutBox.bottom - 6.0f };
        drawText("CppDesk v" + std::string(CPP_DESK_VERSION) + " • Free & Open Source\nZero-install, high-speed remote access with end-to-end encryption.",
                 textRect, fmtSmall_, COL_TEXT_SECONDARY);
    }

    // ==================== RIGHT COLUMN: ACCESS & NETWORK ====================
    drawCardSurface(rightCard, 16.0f, alpha);

    float rx = rightCard.left + 24.0f;
    float rrx = rightCard.right - 24.0f;
    float ry = rightCard.top + 16.0f;

    drawText("Access & Network", { rx, ry, rrx, ry + 24.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    ry += 25.0f;
    drawText("Permissions, privacy, and connection settings.",
             { rx, ry, rrx, ry + 16.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    ry += 22.0f;

    // 1. Permissions
    UiRect permBox = { rx, ry, rrx, ry + 172.0f };
    fillRoundRect(permBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(permBox, 12.0f, COL_BORDER);

    drawText("PERMISSIONS",
             { permBox.left + 16.0f, permBox.top + 8.0f, permBox.right - 16.0f, permBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float py = permBox.top + 26.0f;
    drawToggleSwitch("sett_auto_accept", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 24.0f },
                     s.autoAcceptIncoming, "Automatically accept incoming connections", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.autoAcceptIncoming = !ns.autoAcceptIncoming;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 28.0f;

    drawToggleSwitch("sett_def_perm_input", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 24.0f },
                     (s.defaultPermissions & PERM_INPUT) != 0, "Allow mouse and keyboard control", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_INPUT;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 28.0f;

    drawToggleSwitch("sett_def_perm_clip", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 24.0f },
                     (s.defaultPermissions & PERM_CLIPBOARD) != 0, "Allow clipboard sharing", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_CLIPBOARD;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 28.0f;

    drawToggleSwitch("sett_def_perm_file", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 24.0f },
                     (s.defaultPermissions & PERM_FILE_TRANSFER) != 0, "Allow file transfers", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.defaultPermissions ^= PERM_FILE_TRANSFER;
                         identity_.updateSettings(ns);
                         network_.setAutoAcceptIncoming(ns.autoAcceptIncoming, ns.defaultPermissions);
                     });
    py += 28.0f;

    drawToggleSwitch("sett_lock_disc", { permBox.left + 16.0f, py, permBox.right - 16.0f, py + 24.0f },
                     s.lockWorkstationOnDisconnect, "Lock computer when session ends", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.lockWorkstationOnDisconnect = !ns.lockWorkstationOnDisconnect;
                         identity_.updateSettings(ns);
                     });

    ry = permBox.bottom + 10.0f;

    // 2. Notifications & System Tray
    UiRect notifBox = { rx, ry, rrx, ry + 132.0f };
    fillRoundRect(notifBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(notifBox, 12.0f, COL_BORDER);

    drawText("NOTIFICATIONS & TRAY",
             { notifBox.left + 16.0f, notifBox.top + 8.0f, notifBox.right - 16.0f, notifBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    float ny = notifBox.top + 26.0f;
    drawToggleSwitch("sett_push_notif", { notifBox.left + 16.0f, ny, notifBox.right - 16.0f, ny + 24.0f },
                     s.enablePushNotifications, "Windows Action Center push toasts", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.enablePushNotifications = !ns.enablePushNotifications;
                         identity_.updateSettings(ns);
                     });
    ny += 25.0f;

    drawToggleSwitch("sett_taskbar_flash", { notifBox.left + 16.0f, ny, notifBox.right - 16.0f, ny + 24.0f },
                     s.enableTaskbarFlash, "Flash taskbar orange when unfocused", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.enableTaskbarFlash = !ns.enableTaskbarFlash;
                         identity_.updateSettings(ns);
                     });
    ny += 25.0f;

    drawToggleSwitch("sett_notif_sound", { notifBox.left + 16.0f, ny, notifBox.right - 16.0f, ny + 24.0f },
                     s.enableNotificationSounds, "Play sound on incoming alerts", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.enableNotificationSounds = !ns.enableNotificationSounds;
                         identity_.updateSettings(ns);
                     });
    ny += 25.0f;

    drawToggleSwitch("sett_min_to_tray", { notifBox.left + 16.0f, ny, notifBox.right - 16.0f, ny + 24.0f },
                     s.minimizeToTray, "Minimize window to system tray", [this]() {
                         AppSettings ns = identity_.settings();
                         ns.minimizeToTray = !ns.minimizeToTray;
                         identity_.updateSettings(ns);
                     });

    ry = notifBox.bottom + 10.0f;

    // 3. Relay & Rendezvous Network (v3.2.0 Phase 09)
    UiRect netBox = { rx, ry, rrx, ry + 196.0f };
    fillRoundRect(netBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(netBox, 12.0f, COL_BORDER);

    drawText("RELAY & RENDEZVOUS NETWORK",
             { netBox.left + 16.0f, netBox.top + 8.0f, netBox.left + 240.0f, netBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    // Preset pills on top right
    float presetW = 54.0f;
    float presetH = 20.0f;
    float prX = netBox.right - 16.0f;
    UiRect lanPreset = { prX - presetW, netBox.top + 5.0f, prX, netBox.top + 5.0f + presetH };
    UiRect localPreset = { lanPreset.left - 4.0f - 64.0f, netBox.top + 5.0f, lanPreset.left - 4.0f, netBox.top + 5.0f + presetH };
    UiRect pubPreset = { localPreset.left - 4.0f - 54.0f, netBox.top + 5.0f, localPreset.left - 4.0f, netBox.top + 5.0f + presetH };

    drawButton("sett_pre_pub", pubPreset, "Public",
               (relayModeEdit_ == 0) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (relayModeEdit_ == 0) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (relayModeEdit_ == 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               5.0f, [this]() {
                   relayServerEdit_ = "relay.cppdesk.io:50999";
                   stunServerEdit_ = "stun.l.google.com:19302";
                   relayModeEdit_ = 0;
               }, fmtSmall_, relayModeEdit_ != 0, COL_BORDER);

    drawButton("sett_pre_loc", localPreset, "Localhost",
               (relayModeEdit_ == 1) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (relayModeEdit_ == 1) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (relayModeEdit_ == 1) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               5.0f, [this]() {
                   relayServerEdit_ = "127.0.0.1:50999";
                   stunServerEdit_ = "stun.l.google.com:19302";
                   relayModeEdit_ = 1;
               }, fmtSmall_, relayModeEdit_ != 1, COL_BORDER);

    drawButton("sett_pre_lan", lanPreset, "LAN",
               (relayModeEdit_ == 2) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (relayModeEdit_ == 2) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (relayModeEdit_ == 2) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               5.0f, [this]() {
                   relayServerEdit_ = "127.0.0.1:50999";
                   stunServerEdit_ = "";
                   relayModeEdit_ = 2;
               }, fmtSmall_, relayModeEdit_ != 2, COL_BORDER);

    // Row 1: Relay Address
    float row1Y = netBox.top + 28.0f;
    UiRect relayField = { netBox.left + 16.0f, row1Y, netBox.right - 16.0f, row1Y + 30.0f };
    drawTextField("field_relay_srv_sett", FocusedField::RelayServer, relayField,
                  relayServerEdit_, "Relay host:port (e.g. 127.0.0.1:50999)", false);

    // Row 2: Auth Key (left) & STUN Server (right)
    float row2Y = row1Y + 34.0f;
    float halfFieldW = (netBox.width() - 32.0f - 8.0f) * 0.5f;
    UiRect keyField = { netBox.left + 16.0f, row2Y, netBox.left + 16.0f + halfFieldW, row2Y + 30.0f };
    UiRect stunField = { keyField.right + 8.0f, row2Y, netBox.right - 16.0f, row2Y + 30.0f };

    drawTextField("field_relay_key_sett", FocusedField::RelayAuthKey, keyField,
                  relayAuthKeyEdit_, "Auth key (optional)", true);
    drawTextField("field_stun_srv_sett", FocusedField::StunServer, stunField,
                  stunServerEdit_, "STUN server (e.g. stun.l.google.com:19302)", false);

    // Row 3: Action Buttons
    float row3Y = row2Y + 34.0f;
    float thirdBtnW = (netBox.width() - 32.0f - 16.0f) / 3.0f;
    UiRect pingBtn = { netBox.left + 16.0f, row3Y, netBox.left + 16.0f + thirdBtnW, row3Y + 30.0f };
    UiRect saveBtn = { pingBtn.right + 8.0f, row3Y, pingBtn.right + 8.0f + thirdBtnW, row3Y + 30.0f };
    UiRect toggleRelayBtn = { saveBtn.right + 8.0f, row3Y, netBox.right - 16.0f, row3Y + 30.0f };

    bool isDiagActive = network_.isNetworkDiagnosticRunning();
    drawButton("sett_ping_btn", pingBtn, isDiagActive ? "Testing..." : "Test & Ping",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 7.0f, [this]() {
                   network_.startNetworkDiagnostics(relayServerEdit_, stunServerEdit_);
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("sett_apply_relay", saveBtn, "Save & Apply",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 7.0f, [this]() {
                   AppSettings ns = identity_.settings();
                   ns.relayServer = relayServerEdit_;
                   ns.relayAuthKey = relayAuthKeyEdit_;
                   ns.stunServer = stunServerEdit_;
                   ns.relayMode = relayModeEdit_;
                   identity_.updateSettings(ns);
                   network_.setRelayAddressAndReconnect(relayServerEdit_);
                   showToast("Network settings applied & reconnected");
               }, fmtSmall_);

    bool relayRunning = network_.isLocalRelayRunning();
    drawButton("sett_toggle_relay", toggleRelayBtn, relayRunning ? "Relay: ON" : "Relay: OFF",
               relayRunning ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               relayRunning ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               relayRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               7.0f, [this, relayRunning]() {
                   if (relayRunning) {
                       network_.stopLocalRelayServer();
                       showToast("Local relay stopped");
                   } else if (network_.startLocalRelayServer(DEFAULT_RELAY_PORT)) {
                       showToast("Local relay started on :50999");
                   } else {
                       showToast("Relay port already in use", true);
                   }
               }, fmtSmall_, !relayRunning, COL_BORDER, relayRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    // Row 4: Status Indicator & Telemetry Dot
    float row4Y = row3Y + 33.0f;
    RelayProbeResult rDiag;
    StunNatResult sDiag;
    bool hasResult = network_.getNetworkDiagnosticResult(rDiag, sDiag);

    D2D1_COLOR_F dotCol = COL_TEXT_MUTED;
    std::string diagText;
    if (isDiagActive) {
        dotCol = COL_WARNING;
        diagText = "Probing relay TCP and RFC 5389 STUN NAT traversal...";
    } else if (hasResult) {
        if (rDiag.reachable) {
            dotCol = COL_SUCCESS;
            diagText = "Relay: " + rDiag.message + (sDiag.success ? ("  |  NAT: " + sDiag.publicIp + ":" + std::to_string(sDiag.publicPort)) : ("  |  " + sDiag.natTypeDescription));
        } else {
            dotCol = COL_DANGER;
            diagText = "Relay: " + rDiag.message + ("  |  " + sDiag.natTypeDescription);
        }
    } else {
        dotCol = COL_TEXT_SECONDARY;
        std::string modeName = (relayModeEdit_ == 0) ? "Auto" : (relayModeEdit_ == 1 ? "Self-Hosted" : "Direct LAN");
        diagText = "Mode: " + modeName + "  |  Ready to probe network";
    }

    drawPulseDot(netBox.left + 22.0f, row4Y + 14.0f, 3.8f, dotCol, alpha);
    UiRect diagTextRect = { netBox.left + 32.0f, row4Y, netBox.right - 16.0f, row4Y + 28.0f };
    drawText(diagText, diagTextRect, fmtSmall_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_LEADING);

    ry = netBox.bottom + 10.0f;

    // 4. Windows System Service (v3.2.0 Phase 12)
    UiRect svcBox = { rx, ry, rrx, ry + 78.0f };
    fillRoundRect(svcBox, 12.0f, COL_BG_SUBTLE);
    strokeRoundRect(svcBox, 12.0f, COL_BORDER);

    drawText("WINDOWS SYSTEM SERVICE",
             { svcBox.left + 16.0f, svcBox.top + 8.0f, svcBox.left + 240.0f, svcBox.top + 22.0f },
             fmtSmall_, COL_TEXT_ACCENT);

    ServiceStatusState svcState = WindowsServiceManager::getServiceState();
    std::string svcStateLabel = (svcState == ServiceStatusState::Running) ? "Running" :
                                (svcState == ServiceStatusState::Stopped) ? "Stopped" : "Not Installed";
    D2D1_COLOR_F svcStateCol = (svcState == ServiceStatusState::Running) ? COL_SUCCESS :
                               (svcState == ServiceStatusState::Stopped) ? COL_WARNING : COL_TEXT_MUTED;

    drawPulseDot(svcBox.right - 100.0f, svcBox.top + 15.0f, 3.8f, svcStateCol, alpha);
    drawText(svcStateLabel, { svcBox.right - 90.0f, svcBox.top + 7.0f, svcBox.right - 16.0f, svcBox.top + 23.0f },
             fmtSmall_, svcStateCol, DWRITE_TEXT_ALIGNMENT_LEADING);

    float sBtnTop = svcBox.top + 28.0f;
    float sBtnBot = svcBox.top + 64.0f;
    float sQuarterW = (rrx - rx - 32.0f - 24.0f) / 4.0f;

    UiRect installBtn   = { svcBox.left + 16.0f, sBtnTop, svcBox.left + 16.0f + sQuarterW, sBtnBot };
    UiRect uninstallBtn = { installBtn.right + 8.0f, sBtnTop, installBtn.right + 8.0f + sQuarterW, sBtnBot };
    UiRect startBtn     = { uninstallBtn.right + 8.0f, sBtnTop, uninstallBtn.right + 8.0f + sQuarterW, sBtnBot };
    UiRect stopBtn      = { startBtn.right + 8.0f, sBtnTop, svcBox.right - 16.0f, sBtnBot };

    bool isInstalled = (svcState != ServiceStatusState::NotInstalled);
    bool isRunning = (svcState == ServiceStatusState::Running);

    drawButton("sett_svc_install", installBtn, "Install",
               !isInstalled ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               !isInstalled ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               !isInstalled ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               7.0f, [this, isInstalled]() {
                   if (!isInstalled) {
                       if (WindowsServiceManager::installService()) {
                           showToast("Service installed successfully");
                       } else {
                           showToast("Failed to install service (Admin required)", true);
                       }
                   } else {
                       showToast("Service already installed");
                   }
               }, fmtSmall_, isInstalled, COL_BORDER, !isInstalled ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_svc_uninstall", uninstallBtn, "Uninstall",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               7.0f, [this, isInstalled]() {
                   if (isInstalled) {
                       if (WindowsServiceManager::uninstallService()) {
                           showToast("Service uninstalled");
                       } else {
                           showToast("Failed to uninstall service (Admin required)", true);
                       }
                   } else {
                       showToast("Service not installed");
                   }
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("sett_svc_start", startBtn, "Start",
               (isInstalled && !isRunning) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (isInstalled && !isRunning) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (isInstalled && !isRunning) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               7.0f, [this, isInstalled, isRunning]() {
                   if (isInstalled && !isRunning) {
                       if (WindowsServiceManager::startService()) {
                           showToast("Service started");
                       } else {
                           showToast("Failed to start service (Admin required)", true);
                       }
                   } else if (!isInstalled) {
                       showToast("Install service first", true);
                   }
               }, fmtSmall_, !(isInstalled && !isRunning), COL_BORDER,
               (isInstalled && !isRunning) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    drawButton("sett_svc_stop", stopBtn, "Stop",
               isRunning ? COL_DANGER : COL_SEC_BTN_BG,
               isRunning ? COL_DANGER_HV : COL_SEC_BTN_HV,
               isRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               7.0f, [this, isRunning]() {
                   if (isRunning) {
                       if (WindowsServiceManager::stopService()) {
                           showToast("Service stopped");
                       } else {
                           showToast("Failed to stop service", true);
                       }
                   }
               }, fmtSmall_, !isRunning, COL_BORDER, isRunning ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);

    ry = svcBox.bottom + 10.0f;

    // 5. Data & Reset Actions
    if (rightCard.bottom - 10.0f > ry + 36.0f) {
        UiRect maintBox = { rx, ry, rrx, rightCard.bottom - 16.0f };
        fillRoundRect(maintBox, 12.0f, COL_BG_SUBTLE);
        strokeRoundRect(maintBox, 12.0f, COL_BORDER);

        drawText("DATA & MAINTENANCE",
                 { maintBox.left + 16.0f, maintBox.top + 8.0f, maintBox.right - 16.0f, maintBox.top + 22.0f },
                 fmtSmall_, COL_TEXT_ACCENT);

        float mThirdW = (rrx - rx - 32.0f - 16.0f) / 3.0f;
        float btnTop = maintBox.top + 28.0f;
        float btnBot = std::min(btnTop + 34.0f, maintBox.bottom - 8.0f);

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
}

// ---------------- Floating macOS Side Sheet Drawer ----------------

void CppDeskWindow::drawFileTransferDrawer(const UiRect& bounds, float slideProgress) {
    float slideOffsetX = (bounds.width() + 24.0f) * (1.0f - slideProgress);
    UiRect r = bounds.offset(slideOffsetX, 0.0f);

    drawCardSurface(r, 16.0f, std::clamp(slideProgress, 0.0f, 1.0f));

    float x = r.left + 20.0f;
    float rx = r.right - 20.0f;
    float y = r.top + 16.0f;

    float tabW = (rx - x - 34.0f - 20.0f) / 5.0f;
    UiRect tabFiles = { x, y, x + tabW, y + 32.0f };
    UiRect tabChat  = { tabFiles.right + 5.0f, y, tabFiles.right + 5.0f + tabW, y + 32.0f };
    UiRect tabTerm  = { tabChat.right + 5.0f, y, tabChat.right + 5.0f + tabW, y + 32.0f };
    UiRect tabDiag  = { tabTerm.right + 5.0f, y, tabTerm.right + 5.0f + tabW, y + 32.0f };
    UiRect tabHist  = { tabDiag.right + 5.0f, y, tabDiag.right + 5.0f + tabW, y + 32.0f };

    bool onFiles = (drawerTab_ == DrawerTab::FilesAndClip);
    bool onChat  = (drawerTab_ == DrawerTab::LiveChat);
    bool onTerm  = (drawerTab_ == DrawerTab::RemoteTerminal);
    bool onDiag  = (drawerTab_ == DrawerTab::Diagnostics);
    bool onHist  = (drawerTab_ == DrawerTab::ClipboardHistory);

    drawButton("drawer_tab_files", tabFiles, "Files",
               onFiles ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               onFiles ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               onFiles ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   if (drawerTab_ == DrawerTab::Diagnostics) network_.setDiagnosticsActive(false);
                   drawerTab_ = DrawerTab::FilesAndClip;
               }, fmtSmall_);

    uint32_t unread = network_.unreadChatCount();
    std::string chatTabLbl = unread > 0 ? ("Chat (" + std::to_string(unread) + ")") : "Chat";
    drawButton("drawer_tab_chat", tabChat, chatTabLbl,
               onChat ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               onChat ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               onChat ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   if (drawerTab_ == DrawerTab::Diagnostics) network_.setDiagnosticsActive(false);
                   drawerTab_ = DrawerTab::LiveChat;
                   network_.markChatRead();
               }, fmtSmall_);

    drawButton("drawer_tab_term", tabTerm, "Terminal",
               onTerm ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               onTerm ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               onTerm ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   if (drawerTab_ == DrawerTab::Diagnostics) network_.setDiagnosticsActive(false);
                   drawerTab_ = DrawerTab::RemoteTerminal;
                   focusedField_ = FocusedField::TerminalInput;
               }, fmtSmall_);

    drawButton("drawer_tab_diag", tabDiag, "TaskMgr",
               onDiag ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               onDiag ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               onDiag ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   if (drawerTab_ != DrawerTab::Diagnostics) {
                       drawerTab_ = DrawerTab::Diagnostics;
                       network_.setDiagnosticsActive(true);
                   }
               }, fmtSmall_);

    drawButton("drawer_tab_hist", tabHist, "History",
               onHist ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               onHist ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               onHist ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   if (drawerTab_ == DrawerTab::Diagnostics) network_.setDiagnosticsActive(false);
                   drawerTab_ = DrawerTab::ClipboardHistory;
                   focusedField_ = FocusedField::ClipboardSearch;
               }, fmtSmall_);

    UiRect closeBtn = { rx - 28.0f, y + 1.0f, rx, y + 31.0f };
    drawButton("drawer_close", closeBtn, "", COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY, 7.5f, [this]() {
        if (drawerTab_ == DrawerTab::Diagnostics) {
            network_.setDiagnosticsActive(false);
        }
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
                   COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.0f, [this]() {
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
                                    (it.status == TransferStatus::Failed || it.status == TransferStatus::Cancelled) ? COL_DANGER : COL_PRIMARY_ACCENT);

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
                    fillRoundRect(progFg, 2.2f, it.status == TransferStatus::Completed ? COL_SUCCESS : COL_PRIMARY_ACCENT);
                }

                y += 64.0f;
            }
        }
    } else if (drawerTab_ == DrawerTab::LiveChat) {
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
            float rowBaseH = 46.0f;
            int totalMsgs = static_cast<int>(msgs.size());
            int maxVisible = std::max(1, static_cast<int>((chatBox.height() - 16.0f) / (rowBaseH + 6.0f)));
            int maxOffset = std::max(0, totalMsgs - maxVisible);
            chatScrollOffset_ = std::clamp(chatScrollOffset_, 0, maxOffset);
            size_t startIdx = static_cast<size_t>(std::max(0, totalMsgs - maxVisible - chatScrollOffset_));
            size_t endIdx = std::min(msgs.size(), startIdx + static_cast<size_t>(maxVisible));
            float my = chatBox.top + 8.0f;

            for (size_t i = startIdx; i < endIdx; ++i) {
                const auto& m = msgs[i];
                float msgH = m.hasImage ? 86.0f : 46.0f;
                UiRect bubble = m.fromLocal
                    ? UiRect{ chatBox.left + 36.0f, my, chatBox.right - 10.0f, my + msgH }
                    : UiRect{ chatBox.left + 10.0f, my, chatBox.right - 36.0f, my + msgH };
                fillRoundRect(bubble, 9.5f, m.fromLocal ? COL_BG_CARD_ALT : COL_BG_CARD);
                strokeRoundRect(bubble, 9.5f, m.fromLocal ? COL_BORDER_ALT : COL_BORDER);

                drawText(m.senderName, { bubble.left + 10.0f, bubble.top + 4.0f, bubble.right - 10.0f, bubble.top + 20.0f },
                         fmtSmall_, m.fromLocal ? COL_PRIMARY_ACCENT : COL_TEXT_ACCENT);

                if (m.hasImage) {
                    UiRect imgBadge = { bubble.left + 10.0f, bubble.top + 20.0f, bubble.right - 10.0f, bubble.top + 56.0f };
                    fillRoundRect(imgBadge, 6.0f, rgba(0, 0, 0, 0.28f));
                    strokeRoundRect(imgBadge, 6.0f, COL_BORDER);
                    char imgInfo[128];
                    std::snprintf(imgInfo, sizeof(imgInfo), "[Image Attachment %ux%u - %zu KB]",
                                  m.imgWidth, m.imgHeight, m.imageJpegData.size() / 1024);
                    drawText(imgInfo, { imgBadge.left + 8.0f, imgBadge.top + 8.0f, imgBadge.right - 8.0f, imgBadge.bottom - 4.0f },
                             fmtSmall_, COL_PRIMARY_ACCENT);

                    if (!m.text.empty()) {
                        drawText(m.text, { bubble.left + 10.0f, bubble.top + 60.0f, bubble.right - 10.0f, bubble.bottom - 4.0f },
                                 fmtBody_, COL_TEXT_PRIMARY);
                    }
                } else {
                    drawText(m.text, { bubble.left + 10.0f, bubble.top + 20.0f, bubble.right - 10.0f, bubble.bottom - 4.0f },
                             fmtBody_, COL_TEXT_PRIMARY);
                }
                my += msgH + 6.0f;
            }
        }

        UiRect chatField = { x, r.bottom - 52.0f, rx - 114.0f, r.bottom - 14.0f };
        drawTextField("field_chat_input", FocusedField::ChatInput, chatField,
                      chatInput_, "Message...", false);

        UiRect clipImgBtn = { chatField.right + 4.0f, chatField.top, chatField.right + 40.0f, chatField.bottom };
        drawButton("btn_send_img", clipImgBtn, "Pic",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.5f, [this]() {
                       std::vector<uint8_t> jpeg;
                       uint32_t w = 0, h = 0;
                       if (ClipboardManager::getClipboardImageJpeg(jpeg, w, h)) {
                           network_.sendChatImage(jpeg, w, h, chatInput_);
                           chatInput_.clear();
                           showToast("Sent image from clipboard");
                       } else {
                           showToast("No image in clipboard (copy image first)", true);
                       }
                   }, fmtSmall_);

        UiRect sendChatBtn = { clipImgBtn.right + 4.0f, chatField.top, rx, chatField.bottom };
        drawButton("btn_send_chat", sendChatBtn, "Send",
                   COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.5f, [this]() {
                       sendChatFromInput();
                   }, fmtSmall_);
    } else if (drawerTab_ == DrawerTab::RemoteTerminal) {
        drawText("Interactive Remote Host Console",
                 { x, y, rx, y + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
        y += 24.0f;

        float qBtnW = (rx - x - 20.0f) / 5.0f;
        UiRect b1 = { x, y, x + qBtnW, y + 26.0f };
        UiRect b2 = { b1.right + 5.0f, y, b1.right + 5.0f + qBtnW, y + 26.0f };
        UiRect b3 = { b2.right + 5.0f, y, b2.right + 5.0f + qBtnW, y + 26.0f };
        UiRect b4 = { b3.right + 5.0f, y, b3.right + 5.0f + qBtnW, y + 26.0f };
        UiRect b5 = { b4.right + 5.0f, y, rx, y + 26.0f };

        drawButton("term_ipconfig", b1, "ipconfig", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
            network_.sendTerminalCommand("ipconfig");
        }, fmtSmall_, true, COL_BORDER);

        drawButton("term_tasklist", b2, "tasklist", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
            network_.sendTerminalCommand("tasklist");
        }, fmtSmall_, true, COL_BORDER);

        drawButton("term_netstat", b3, "netstat", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
            network_.sendTerminalCommand("netstat -ano");
        }, fmtSmall_, true, COL_BORDER);

        drawButton("term_whoami", b4, "whoami", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
            network_.sendTerminalCommand("whoami");
        }, fmtSmall_, true, COL_BORDER);

        drawButton("term_clear", b5, "Clear", COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY, 6.0f, [this]() {
            network_.clearTerminalScrollback();
        }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);

        y += 32.0f;

        UiRect termBox = { x, y, rx, r.bottom - 64.0f };
        fillRoundRect(termBox, 11.0f, rgba(12, 16, 24, 0.95f));
        strokeRoundRect(termBox, 11.0f, COL_BORDER);

        auto lines = network_.getTerminalScrollback();
        if (lines.empty()) {
            drawText("Host console ready. Type commands below or click quick actions.",
                     { termBox.left + 14.0f, termBox.centerY() - 14.0f, termBox.right - 14.0f, termBox.centerY() + 14.0f },
                     fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            float lineH = 18.0f;
            int maxVisible = std::max(1, static_cast<int>((termBox.height() - 16.0f) / lineH));
            int totalLines = static_cast<int>(lines.size());
            int scroll = static_cast<int>(terminalScrollOffset_);
            int maxOffset = std::max(0, totalLines - maxVisible);
            scroll = std::clamp(scroll, 0, maxOffset);
            size_t startIdx = static_cast<size_t>(std::max(0, totalLines - maxVisible - scroll));
            size_t endIdx = std::min(lines.size(), startIdx + static_cast<size_t>(maxVisible));
            float ly = termBox.top + 8.0f;

            for (size_t i = startIdx; i < endIdx; ++i) {
                UiRect lr = { termBox.left + 10.0f, ly, termBox.right - 10.0f, ly + lineH };
                D2D1_COLOR_F lineCol = (lines[i].rfind("> ", 0) == 0)
                    ? COL_PRIMARY_ACCENT
                    : rgba(140, 235, 175, 0.95f);
                drawText(lines[i], lr, fmtMono_, lineCol, DWRITE_TEXT_ALIGNMENT_LEADING);
                ly += lineH;
            }
        }

        UiRect termField = { x, r.bottom - 52.0f, rx - 76.0f, r.bottom - 14.0f };
        drawTextField("field_term_input", FocusedField::TerminalInput, termField,
                      terminalInputText_, "Command...", false);

        UiRect sendTermBtn = { termField.right + 6.0f, termField.top, rx, termField.bottom };
        drawButton("btn_send_term", sendTermBtn, "Run",
                   COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.5f, [this]() {
                       sendTerminalFromInput();
                   }, fmtSmall_);
    } else if (drawerTab_ == DrawerTab::Diagnostics) {
        drawText("Live Hardware & Process Telemetry",
                 { x, y, rx, y + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
        y += 24.0f;

        auto diag = network_.latestDiagnostics();

        // 1. Hardware Metrics Card (CPU, RAM, Disk)
        UiRect metricsBox = { x, y, rx, y + 104.0f };
        fillRoundRect(metricsBox, 11.0f, COL_BG_SUBTLE);
        strokeRoundRect(metricsBox, 11.0f, COL_BORDER);

        float mx = metricsBox.left + 14.0f;
        float mrx = metricsBox.right - 14.0f;
        float my = metricsBox.top + 10.0f;
        float mBarW = mrx - mx;

        // CPU Metric
        float cpu = std::clamp(diag.cpuUsagePercent, 0.0f, 100.0f);
        char cpuStr[64];
        std::snprintf(cpuStr, sizeof(cpuStr), "CPU: %.1f%%", cpu);
        drawText(cpuStr, { mx, my, mrx, my + 16.0f }, fmtBodyBold_, COL_TEXT_PRIMARY);
        UiRect cpuBarBg = { mx, my + 17.0f, mrx, my + 23.0f };
        fillRoundRect(cpuBarBg, 3.0f, rgba(255, 255, 255, 0.08f));
        UiRect cpuBarFill = { mx, my + 17.0f, mx + mBarW * (cpu / 100.0f), my + 23.0f };
        D2D1_COLOR_F cpuCol = (cpu < 60.0f) ? COL_SUCCESS : (cpu < 85.0f) ? rgba(250, 173, 20, 1.0f) : COL_DANGER;
        fillRoundRect(cpuBarFill, 3.0f, cpuCol);
        my += 29.0f;

        // RAM Metric
        double ramUsedGb = diag.ramUsedBytes / (1024.0 * 1024.0 * 1024.0);
        double ramTotalGb = diag.ramTotalBytes / (1024.0 * 1024.0 * 1024.0);
        float ramPct = (diag.ramTotalBytes > 0)
            ? static_cast<float>(diag.ramUsedBytes * 100.0 / diag.ramTotalBytes)
            : 0.0f;
        ramPct = std::clamp(ramPct, 0.0f, 100.0f);
        char ramStr[64];
        std::snprintf(ramStr, sizeof(ramStr), "RAM: %.1f / %.1f GB (%.0f%%)", ramUsedGb, ramTotalGb, ramPct);
        drawText(ramStr, { mx, my, mrx, my + 16.0f }, fmtBodyBold_, COL_TEXT_PRIMARY);

        UiRect ramBarBg = { mx, my + 17.0f, mrx, my + 23.0f };
        fillRoundRect(ramBarBg, 3.0f, rgba(255, 255, 255, 0.08f));
        UiRect ramBarFill = { mx, my + 17.0f, mx + mBarW * (ramPct / 100.0f), my + 23.0f };
        D2D1_COLOR_F ramCol = (ramPct < 70.0f) ? COL_PRIMARY_ACCENT : (ramPct < 88.0f) ? rgba(250, 173, 20, 1.0f) : COL_DANGER;
        fillRoundRect(ramBarFill, 3.0f, ramCol);
        my += 29.0f;

        // Disk Metric (C:)
        double diskUsedGb = diag.diskUsedBytes / (1024.0 * 1024.0 * 1024.0);
        double diskTotalGb = diag.diskTotalBytes / (1024.0 * 1024.0 * 1024.0);
        float diskPct = (diag.diskTotalBytes > 0)
            ? static_cast<float>(diag.diskUsedBytes * 100.0 / diag.diskTotalBytes)
            : 0.0f;
        diskPct = std::clamp(diskPct, 0.0f, 100.0f);
        char diskStr[64];
        std::snprintf(diskStr, sizeof(diskStr), "Disk (C:): %.0f / %.0f GB (%.0f%%)", diskUsedGb, diskTotalGb, diskPct);
        drawText(diskStr, { mx, my, mrx, my + 16.0f }, fmtBodyBold_, COL_TEXT_PRIMARY);

        UiRect diskBarBg = { mx, my + 17.0f, mrx, my + 23.0f };
        fillRoundRect(diskBarBg, 3.0f, rgba(255, 255, 255, 0.08f));
        UiRect diskBarFill = { mx, my + 17.0f, mx + mBarW * (diskPct / 100.0f), my + 23.0f };
        D2D1_COLOR_F diskCol = (diskPct < 75.0f) ? rgba(52, 199, 89, 1.0f) : (diskPct < 90.0f) ? rgba(250, 173, 20, 1.0f) : COL_DANGER;
        fillRoundRect(diskBarFill, 3.0f, diskCol);

        y = metricsBox.bottom + 14.0f;

        // 2. Process Table Header
        drawText("TOP PROCESSES (MEMORY FOOTPRINT)",
                 { x, y, rx, y + 16.0f }, fmtSmall_, COL_TEXT_ACCENT);
        y += 20.0f;

        UiRect tableBox = { x, y, rx, r.bottom - 16.0f };
        fillRoundRect(tableBox, 11.0f, rgba(12, 16, 24, 0.95f));
        strokeRoundRect(tableBox, 11.0f, COL_BORDER);

        if (diag.processes.empty()) {
            drawText("Sampling host processes...",
                     { tableBox.left + 14.0f, tableBox.centerY() - 12.0f, tableBox.right - 14.0f, tableBox.centerY() + 12.0f },
                     fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            float rowH = 34.0f;
            int maxVisible = std::max(1, static_cast<int>((tableBox.height() - 8.0f) / rowH));
            int totalProcs = static_cast<int>(diag.processes.size());
            int maxScroll = std::max(0, totalProcs - maxVisible);
            int scroll = std::clamp(static_cast<int>(diagnosticsScrollOffset_), 0, maxScroll);

            float py = tableBox.top + 6.0f;
            for (int i = scroll; i < totalProcs && (i - scroll) < maxVisible; ++i) {
                const auto& proc = diag.processes[i];
                UiRect rowRect = { tableBox.left + 8.0f, py, tableBox.right - 8.0f, py + rowH - 4.0f };

                if ((i % 2) == 1) {
                    fillRoundRect(rowRect, 6.0f, rgba(255, 255, 255, 0.03f));
                }

                // Process Name & PID
                std::string procLine = proc.name;
                drawText(procLine, { rowRect.left + 8.0f, rowRect.top + 2.0f, rowRect.right - 140.0f, rowRect.bottom },
                         fmtBodyBold_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);

                // Memory in MB
                double memMb = proc.workingSetBytes / (1024.0 * 1024.0);
                char memBuf[32];
                std::snprintf(memBuf, sizeof(memBuf), "%.0f MB", memMb);
                drawText(memBuf, { rowRect.right - 145.0f, rowRect.top + 2.0f, rowRect.right - 68.0f, rowRect.bottom },
                         fmtSmall_, COL_TEXT_SECONDARY, DWRITE_TEXT_ALIGNMENT_TRAILING);

                // "End Task" action button
                UiRect killBtn = { rowRect.right - 62.0f, rowRect.top + 2.0f, rowRect.right - 4.0f, rowRect.bottom - 2.0f };
                std::string kBtnId = "proc_kill_" + std::to_string(proc.pid);
                uint32_t targetPid = proc.pid;
                std::string targetName = proc.name;

                drawButton(kBtnId, killBtn, "End", COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY, 5.0f,
                           [this, targetPid, targetName]() {
                               network_.sendProcessKill(targetPid);
                               showToast("Terminating " + targetName + " (PID " + std::to_string(targetPid) + ")...");
                           }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);

                py += rowH;
            }
        }
    } else if (drawerTab_ == DrawerTab::ClipboardHistory) {
        drawText("In-Session Clipboard History Hub",
                 { x, y, rx, y + 18.0f }, fmtSmall_, COL_TEXT_SECONDARY);
        y += 24.0f;

        UiRect searchField = { x, y, rx - 72.0f, y + 32.0f };
        drawTextField("field_clip_search", FocusedField::ClipboardSearch, searchField,
                      clipSearchQuery_, "Search history...", false);

        UiRect clearBtn = { searchField.right + 6.0f, y, rx, y + 32.0f };
        drawButton("btn_clip_clear", clearBtn, "Clear",
                   COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY, 7.5f, [this]() {
                       network_.clipboardManager().history().clear();
                       showToast("Clipboard history cleared");
                   }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);

        y += 40.0f;

        UiRect listBox = { x, y, rx, r.bottom - 16.0f };
        fillRoundRect(listBox, 11.0f, COL_BG_SUBTLE);
        strokeRoundRect(listBox, 11.0f, COL_BORDER);

        auto histItems = network_.clipboardManager().history().search(clipSearchQuery_);
        if (histItems.empty()) {
            drawText("No clipboard history yet.",
                     { listBox.left + 14.0f, listBox.centerY() - 14.0f, listBox.right - 14.0f, listBox.centerY() + 14.0f },
                     fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            float cardH = 58.0f;
            int maxVisible = std::max(1, static_cast<int>((listBox.height() - 16.0f) / (cardH + 6.0f)));
            int totalItems = static_cast<int>(histItems.size());
            int maxOffset = std::max(0, totalItems - maxVisible);
            clipHistoryScrollOffset_ = std::clamp(clipHistoryScrollOffset_, 0, maxOffset);
            size_t startIdx = static_cast<size_t>(clipHistoryScrollOffset_);
            size_t endIdx = std::min(histItems.size(), startIdx + static_cast<size_t>(maxVisible));
            float cy = listBox.top + 8.0f;

            for (size_t i = startIdx; i < endIdx; ++i) {
                const auto& item = histItems[i];
                UiRect card = { listBox.left + 8.0f, cy, listBox.right - 8.0f, cy + cardH };
                fillRoundRect(card, 8.0f, COL_BG_CARD);
                strokeRoundRect(card, 8.0f, COL_BORDER);

                // Type badge pill
                float bx = card.left + 8.0f;
                UiRect badgeRect = { bx, card.top + 6.0f, bx + 42.0f, card.top + 22.0f };
                D2D1_COLOR_F badgeBg = (item.typeBadge == "URL") ? rgba(0, 122, 255, 0.25f) :
                                       (item.typeBadge == "Code") ? rgba(175, 82, 222, 0.25f) :
                                       (item.typeBadge == "Path") ? rgba(52, 199, 89, 0.25f) :
                                       rgba(142, 142, 147, 0.25f);
                D2D1_COLOR_F badgeFg = (item.typeBadge == "URL") ? COL_PRIMARY_ACCENT :
                                       (item.typeBadge == "Code") ? rgba(191, 90, 242, 1.0f) :
                                       (item.typeBadge == "Path") ? COL_SUCCESS :
                                       COL_TEXT_SECONDARY;
                fillRoundRect(badgeRect, 4.0f, badgeBg);
                drawText(item.typeBadge, badgeRect, fmtSmall_, badgeFg, DWRITE_TEXT_ALIGNMENT_CENTER);

                // Origin & chars
                char metaStr[64];
                std::snprintf(metaStr, sizeof(metaStr), "%s - %zu chars",
                              item.isFromRemote ? "Remote" : "Local", item.charCount);
                drawText(metaStr, { badgeRect.right + 8.0f, card.top + 6.0f, card.right - 90.0f, card.top + 22.0f },
                         fmtSmall_, COL_TEXT_MUTED);

                // Copy button
                uint32_t iid = item.id;
                UiRect copyBtn = { card.right - 82.0f, card.top + 6.0f, card.right - 36.0f, card.top + 26.0f };
                drawButton("clip_copy_" + std::to_string(iid), copyBtn, "Copy",
                           COL_SEC_BTN_BG, COL_PRIMARY_ACCENT, COL_TEXT_PRIMARY, 5.0f, [this, iid]() {
                               if (network_.clipboardManager().history().copyItemToClipboard(iid)) {
                                   showToast("Copied to clipboard");
                               }
                           }, fmtSmall_);

                // Delete button
                UiRect delBtn = { card.right - 30.0f, card.top + 6.0f, card.right - 6.0f, card.top + 26.0f };
                drawButton("clip_del_" + std::to_string(iid), delBtn, "X",
                           COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_MUTED, 5.0f, [this, iid]() {
                               network_.clipboardManager().history().deleteItem(iid);
                           }, fmtSmall_);

                // Text preview
                drawText(item.previewText, { card.left + 8.0f, card.top + 28.0f, card.right - 8.0f, card.bottom - 4.0f },
                         fmtSmall_, COL_TEXT_PRIMARY);

                cy += cardH + 6.0f;
            }
        }
    }
}

// ---------------- macOS Sheet Connection Approval Modal ----------------

void CppDeskWindow::drawIncomingApprovalModal(float width, float height, float modalProgress) {
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
    drawText(callerLine, { mx, my, mrx, my + 24.0f }, fmtSubheading_, COL_PRIMARY_ACCENT);
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
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 9.5f, [this]() {
                   network_.respondToIncomingRequest(true, modalPermissions_);
                   showToast("Connection accepted");
               });

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- macOS Dynamic Island Floating Top Bar ----------------

void CppDeskWindow::drawDynamicIslandToolbar(float width, float /*height*/) {
    if (floatingToolbarY_ <= -58.0f) return;

    auto stats = network_.viewerStats();
    float pillW = 1090.0f;
    float pillH = 42.0f;
    float pillLeft = (width - pillW) * 0.5f;
    float pillRight = pillLeft + pillW;
    float pillTop = floatingToolbarY_;
    float pillBottom = pillTop + pillH;
    UiRect pillRect = { pillLeft, pillTop, pillRight, pillBottom };

    // Frosted card surface with shadow
    drawCardShadow(pillRect, 21.0f, 0.95f);
    fillRoundRect(pillRect, 21.0f, withAlpha(COL_BG_CARD, 0.96f));
    strokeRoundRect(pillRect, 21.0f, withAlpha(COL_BORDER_FOCUS, 0.35f), 1.2f);
    fillRoundRect({ pillRect.left + 24.0f, pillRect.top + 1.0f, pillRect.right - 24.0f, pillRect.top + 2.0f },
                  0.5f, rgba(255, 255, 255, 0.40f));

    // Left info: Pulse dot + Host title
    float curX = pillLeft + 16.0f;
    drawPulseDot(curX + 4.0f, pillRect.centerY(), 3.5f, COL_SUCCESS, 1.0f);
    curX += 16.0f;

    std::string peerTitle = stats.remoteHostname.empty()
        ? CryptoUtils::formatDeskId(stats.remoteDeskId)
        : stats.remoteHostname;
    drawText(peerTitle, { curX, pillTop, curX + 130.0f, pillBottom }, fmtSmall_, COL_TEXT_PRIMARY);
    curX += 136.0f;

    // 1. Display Switcher
    std::string monLabel = "Disp " + std::to_string(stats.activeMonitorIndex + 1) + " ▾";
    UiRect monBtn = { curX, pillTop + 6.0f, curX + 78.0f, pillBottom - 6.0f };
    drawButton("island_mon_btn", monBtn, monLabel,
               showDisplayMenu_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               showDisplayMenu_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               showDisplayMenu_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   showDisplayMenu_ = !showDisplayMenu_;
                   showAdminMenu_ = false;
                   showQualityMenu_ = false;
               }, fmtSmall_, true, COL_BORDER);
    curX = monBtn.right + 6.0f;

    // 2. Admin Actions Menu
    UiRect adminBtn = { curX, pillTop + 6.0f, curX + 74.0f, pillBottom - 6.0f };
    drawButton("island_admin_btn", adminBtn, "Admin ▾",
               showAdminMenu_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               showAdminMenu_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               showAdminMenu_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   showAdminMenu_ = !showAdminMenu_;
                   showDisplayMenu_ = false;
                   showQualityMenu_ = false;
               }, fmtSmall_, true, COL_BORDER);
    curX = adminBtn.right + 6.0f;

    // 3. Quality & FPS Menu
    std::string qLabel = (stats.qualityPreset == QualityPreset::Ultra ? "Ultra ▾" :
                         (stats.qualityPreset == QualityPreset::Balanced ? "Bal ▾" : "Low ▾"));
    UiRect qualBtn = { curX, pillTop + 6.0f, curX + 72.0f, pillBottom - 6.0f };
    drawButton("island_qual_btn", qualBtn, qLabel,
               showQualityMenu_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               showQualityMenu_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               showQualityMenu_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   showQualityMenu_ = !showQualityMenu_;
                   showDisplayMenu_ = false;
                   showAdminMenu_ = false;
               }, fmtSmall_, true, COL_BORDER);
    curX = qualBtn.right + 6.0f;

    // 4. Clipboard Sync Toggle
    bool clipOn = network_.isClipboardSyncEnabled();
    UiRect clipBtn = { curX, pillTop + 6.0f, curX + 76.0f, pillBottom - 6.0f };
    drawButton("island_clip_btn", clipBtn, clipOn ? "Clip: ON" : "Clip: OFF",
               clipOn ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               clipOn ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               clipOn ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this, clipOn]() {
                   bool n = !clipOn;
                   network_.setClipboardSyncEnabled(n);
                   showToast(n ? "Clipboard Sync: Enabled" : "Clipboard Sync: Disabled");
               }, fmtSmall_, !clipOn, COL_BORDER);
    curX = clipBtn.right + 6.0f;

    // Audio Mute Button
    bool islandAudioMuted = network_.isAudioMuted();
    std::string islandAudioLabel = islandAudioMuted ? "Muted" : "Vol: 100%";
    UiRect islandAudioBtn = { curX, pillTop + 6.0f, curX + 76.0f, pillBottom - 6.0f };
    drawButton("island_audio_btn", islandAudioBtn, islandAudioLabel,
               islandAudioMuted ? COL_SEC_BTN_BG : COL_PRIMARY_ACCENT,
               islandAudioMuted ? COL_SEC_BTN_HV : COL_PRIMARY_ACCENT_HV,
               islandAudioMuted ? COL_TEXT_PRIMARY : COL_TEXT_ON_ACCENT,
               8.0f, [this, islandAudioMuted]() {
                   network_.setAudioMuted(!islandAudioMuted);
                   showToast(!islandAudioMuted ? "Audio muted" : "Audio unmuted");
               }, fmtSmall_, islandAudioMuted, COL_BORDER);
    curX = islandAudioBtn.right + 6.0f;

    // Privacy Mode Curtain Button
    bool islandPrivacyOn = stats.privacyModeEngaged;
    UiRect islandPrivacyBtn = { curX, pillTop + 6.0f, curX + 76.0f, pillBottom - 6.0f };
    drawButton("island_privacy_btn", islandPrivacyBtn, islandPrivacyOn ? "Curtain: ON" : "Curtain",
               islandPrivacyOn ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               islandPrivacyOn ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               islandPrivacyOn ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   network_.requestTogglePrivacyMode();
               }, fmtSmall_, !islandPrivacyOn, COL_BORDER);
    curX = islandPrivacyBtn.right + 6.0f;

    // Port Forwarding Tunnels Button
    UiRect islandTunnelBtn = { curX, pillTop + 6.0f, curX + 70.0f, pillBottom - 6.0f };
    drawButton("island_tunnel_btn", islandTunnelBtn, "Tunnels",
               showPortForwardModal_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               showPortForwardModal_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               showPortForwardModal_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   showPortForwardModal_ = !showPortForwardModal_;
               }, fmtSmall_, !showPortForwardModal_, COL_BORDER);
    curX = islandTunnelBtn.right + 6.0f;

    // Whiteboard Button
    UiRect islandWbBtn = { curX, pillTop + 6.0f, curX + 70.0f, pillBottom - 6.0f };
    drawButton("island_wb_btn", islandWbBtn, whiteboardActive_ ? "Board: ON" : "Board",
               whiteboardActive_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               whiteboardActive_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               whiteboardActive_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   whiteboardActive_ = !whiteboardActive_;
                   showToast(whiteboardActive_ ? "Whiteboard active" : "Whiteboard hidden");
               }, fmtSmall_, !whiteboardActive_, COL_BORDER);
    curX = islandWbBtn.right + 6.0f;

    // 5. Chat Button
    uint32_t unreadChat = network_.unreadChatCount();
    bool islandChatOpen = (showFileDrawer_ && drawerTab_ == DrawerTab::LiveChat);
    std::string islandChatLabel = (unreadChat > 0) ? ("Chat (" + std::to_string(unreadChat) + ")") : "Chat";
    UiRect islandChatBtn = { curX, pillTop + 6.0f, curX + 72.0f, pillBottom - 6.0f };
    drawButton("island_chat_btn", islandChatBtn, islandChatLabel,
               (islandChatOpen || unreadChat > 0) ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               (islandChatOpen || unreadChat > 0) ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               (islandChatOpen || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this, islandChatOpen]() {
                   if (islandChatOpen) {
                       showFileDrawer_ = false;
                   } else {
                       showFileDrawer_ = true;
                       drawerTab_ = DrawerTab::LiveChat;
                       network_.markChatRead();
                       focusedField_ = FocusedField::ChatInput;
                   }
               }, fmtSmall_, !(islandChatOpen || unreadChat > 0), COL_BORDER,
               (islandChatOpen || unreadChat > 0) ? COL_TEXT_ON_ACCENT : COL_TEXT_ACCENT);
    curX = islandChatBtn.right + 6.0f;

    // HUD Toggle Button
    UiRect islandHudBtn = { curX, pillTop + 6.0f, curX + 58.0f, pillBottom - 6.0f };
    drawButton("island_hud_btn", islandHudBtn, showPerformanceHud_ ? "HUD: ON" : "HUD",
               showPerformanceHud_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               showPerformanceHud_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               showPerformanceHud_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   showPerformanceHud_ = !showPerformanceHud_;
                   showToast(showPerformanceHud_ ? "Performance HUD: ON" : "Performance HUD: OFF");
               }, fmtSmall_, !showPerformanceHud_, COL_BORDER);
    curX = islandHudBtn.right + 6.0f;

    // 6. Shortcuts Button "?"
    UiRect helpBtn = { curX, pillTop + 6.0f, curX + 32.0f, pillBottom - 6.0f };
    drawButton("island_help_btn", helpBtn, "?",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   toggleShortcutsModal();
               }, fmtBodyBold_, true, COL_BORDER);
    curX = helpBtn.right + 6.0f;

    // 6. Pin Button
    UiRect pinBtn = { curX, pillTop + 6.0f, curX + 54.0f, pillBottom - 6.0f };
    drawButton("island_pin_btn", pinBtn, floatingToolbarPinned_ ? "Pinned" : "Pin",
               floatingToolbarPinned_ ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
               floatingToolbarPinned_ ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
               floatingToolbarPinned_ ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   floatingToolbarPinned_ = !floatingToolbarPinned_;
                   showToast(floatingToolbarPinned_ ? "Toolbar Pinned" : "Toolbar Auto-hide");
               }, fmtSmall_, !floatingToolbarPinned_, COL_BORDER);
    curX = pinBtn.right + 6.0f;

    // 7. Fullscreen Exit Button
    UiRect fsBtn = { curX, pillTop + 6.0f, pillRight - 12.0f, pillBottom - 6.0f };
    drawButton("island_fs_btn", fsBtn, "Exit (F11)",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   toggleFullscreen();
               }, fmtSmall_, true, COL_BORDER);

    // Dropdown 1: Display Switcher Menu
    if (showDisplayMenu_) {
        float itemH = 32.0f;
        int count = std::max(1, static_cast<int>(stats.monitors.size()));
        float dropH = count * itemH + 16.0f;
        UiRect dropRect = { monBtn.left - 20.0f, pillBottom + 6.0f, monBtn.left + 230.0f, pillBottom + 6.0f + dropH };
        drawCardShadow(dropRect, 14.0f, 0.95f);
        fillRoundRect(dropRect, 14.0f, withAlpha(COL_BG_CARD, 0.98f));
        strokeRoundRect(dropRect, 14.0f, COL_BORDER, 1.2f);

        float dy = dropRect.top + 8.0f;
        if (stats.monitors.empty()) {
            UiRect itemR = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
            drawButton("drop_mon_0", itemR, "Primary Display (Default)",
                       COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 6.0f,
                       [this]() { showDisplayMenu_ = false; }, fmtSmall_);
        } else {
            for (size_t i = 0; i < stats.monitors.size(); ++i) {
                const auto& m = stats.monitors[i];
                bool isCur = (m.index == stats.activeMonitorIndex);
                UiRect itemR = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
                std::string btnId = "drop_mon_" + std::to_string(i);
                std::string label = m.name.empty() ? ("Display " + std::to_string(m.index + 1)) : m.name;
                drawButton(btnId, itemR, label,
                           isCur ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                           isCur ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                           isCur ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                           6.0f, [this, m]() {
                               network_.selectRemoteMonitor(m.index);
                               showDisplayMenu_ = false;
                               showToast("Switched to Display " + std::to_string(m.index + 1));
                           }, fmtSmall_, !isCur, COL_BORDER);
                dy += itemH;
            }
        }
    }

    // Dropdown 2: Admin Actions Menu
    if (showAdminMenu_) {
        float itemH = 32.0f;
        float dropH = 5 * itemH + 16.0f;
        UiRect dropRect = { adminBtn.left - 20.0f, pillBottom + 6.0f, adminBtn.left + 210.0f, pillBottom + 6.0f + dropH };
        drawCardShadow(dropRect, 14.0f, 0.95f);
        fillRoundRect(dropRect, 14.0f, withAlpha(COL_BG_CARD, 0.98f));
        strokeRoundRect(dropRect, 14.0f, COL_BORDER, 1.2f);

        float dy = dropRect.top + 8.0f;
        UiRect r1 = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
        drawButton("drop_adm_lock", r1, "Lock Workstation",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
                       network_.sendSystemAction(SystemActionType::LockWorkstation);
                       showAdminMenu_ = false;
                       showToast("Remote Workstation Locked");
                   }, fmtSmall_, true, COL_BORDER);
        dy += itemH;

        UiRect r2 = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
        drawButton("drop_adm_desktop", r2, "Show Desktop",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
                       network_.sendSystemAction(SystemActionType::ShowDesktop);
                       showAdminMenu_ = false;
                       showToast("Remote Show Desktop");
                   }, fmtSmall_, true, COL_BORDER);
        dy += itemH;

        UiRect r3 = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
        drawButton("drop_adm_sas", r3, "Task Manager (Ctrl+Alt+Del)",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
                       network_.sendSystemAction(SystemActionType::SendCtrlAltDel);
                       showAdminMenu_ = false;
                       showToast("Sent Ctrl+Alt+Del to remote PC");
                   }, fmtSmall_, true, COL_BORDER);
        dy += itemH;

        UiRect r4 = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
        drawButton("drop_adm_reboot_rec", r4, "Reboot & Reconnect...",
                   COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.0f, [this]() {
                       showAdminMenu_ = false;
                       showRebootConfirmModal_ = true;
                   }, fmtSmall_, true, COL_BORDER);
        dy += itemH;

        UiRect r5 = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 28.0f };
        drawButton("drop_adm_reboot", r5, "Emergency Reboot...",
                   COL_DANGER, COL_DANGER_HV, COL_TEXT_ON_ACCENT, 6.0f, [this]() {
                       network_.sendSystemAction(SystemActionType::EmergencyReboot);
                       showAdminMenu_ = false;
                       showToast("Remote Emergency Reboot sent", true);
                   }, fmtSmall_);
    }

    // Dropdown 3: Quality & FPS Menu
    if (showQualityMenu_) {
        float itemH = 30.0f;
        float dropH = 6 * itemH + 20.0f;
        UiRect dropRect = { qualBtn.left - 40.0f, pillBottom + 6.0f, qualBtn.left + 220.0f, pillBottom + 6.0f + dropH };
        drawCardShadow(dropRect, 14.0f, 0.95f);
        fillRoundRect(dropRect, 14.0f, withAlpha(COL_BG_CARD, 0.98f));
        strokeRoundRect(dropRect, 14.0f, COL_BORDER, 1.2f);

        float dy = dropRect.top + 8.0f;
        auto addQualItem = [&](const std::string& id, const std::string& label, QualityPreset q, uint8_t fps, bool adap) {
            bool isCur = (stats.qualityPreset == q);
            UiRect ir = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 26.0f };
            drawButton(id, ir, label,
                       isCur ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                       isCur ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                       isCur ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                       6.0f, [this, q, fps, adap]() {
                           network_.updateQualitySettings(q, fps, adap);
                           showQualityMenu_ = false;
                           showToast("Updated Video Quality");
                       }, fmtSmall_, !isCur, COL_BORDER);
            dy += itemH;
        };

        addQualItem("drop_q_ultra", "Ultra (60 FPS Lossless)", QualityPreset::Ultra, 60, false);
        addQualItem("drop_q_bal", "Balanced (30 FPS Adaptive)", QualityPreset::Balanced, 30, true);
        addQualItem("drop_q_low", "Low Bandwidth (15 FPS)", QualityPreset::LowBandwidth, 15, false);

        fillRoundRect({ dropRect.left + 12.0f, dy + 1.0f, dropRect.right - 12.0f, dy + 2.0f }, 0.5f, COL_BORDER);
        dy += 6.0f;

        auto addFpsItem = [&](const std::string& id, const std::string& label, uint8_t fps) {
            bool isCur = (stats.targetFps == fps);
            UiRect ir = { dropRect.left + 8.0f, dy, dropRect.right - 8.0f, dy + 26.0f };
            drawButton(id, ir, label,
                       isCur ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                       isCur ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                       isCur ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                       6.0f, [this, stats, fps]() {
                           network_.updateQualitySettings(stats.qualityPreset, fps, stats.adaptiveFps);
                           showQualityMenu_ = false;
                           showToast("FPS Target: " + std::to_string(fps));
                       }, fmtSmall_, !isCur, COL_BORDER);
            dy += itemH;
        };

        addFpsItem("drop_fps_60", "FPS Cap: 60 FPS", 60);
        addFpsItem("drop_fps_30", "FPS Cap: 30 FPS", 30);
        addFpsItem("drop_fps_15", "FPS Cap: 15 FPS", 15);
    }
}

// ---------------- Dynamic Island Clipboard File Transfer Progress Pill ----------------

void CppDeskWindow::drawClipboardTransferPill(float width, float /*height*/) {
    auto& clipMgr = network_.clipboardFileTransferManager();
    bool active = clipMgr.isTransferActive();

    // Check completion transition to trigger toast
    if (lastClipTransferActive_ && !active) {
        if (lastClipTransferBytes_ > 0) {
            std::string szStr;
            if (lastClipTransferBytes_ < 1024ULL * 1024ULL) {
                szStr = std::to_string(lastClipTransferBytes_ / 1024ULL) + " KB";
            } else {
                szStr = std::to_string(lastClipTransferBytes_ / (1024ULL * 1024ULL)) + " MB";
            }
            showToast("Clipboard file transfer complete (" + szStr + ")");
        }
        lastClipTransferBytes_ = 0;
    }
    lastClipTransferActive_ = active;

    if (!active) return;

    std::string fname = clipMgr.activeFileName();
    float progress = clipMgr.activeProgressFraction();
    float speedMBs = clipMgr.activeTransferRateMBs();
    lastClipTransferBytes_ = clipMgr.activeTotalBytes();

    int pct = std::clamp(static_cast<int>(progress * 100.0f), 0, 100);

    // Dynamic Island Pill sizing and position
    float pillW = 380.0f;
    float pillH = 38.0f;
    float pillLeft = (width - pillW) * 0.5f;
    float pillRight = pillLeft + pillW;
    float pillTop = isFullscreen_ ? (floatingToolbarY_ > 0.0f ? floatingToolbarY_ + 48.0f : 16.0f) : 66.0f;
    float pillBottom = pillTop + pillH;
    UiRect pillRect = { pillLeft, pillTop, pillRight, pillBottom };

    // Obsidian card background (#0B0D13) with glass styling
    drawCardShadow(pillRect, 19.0f, 0.90f);
    fillRoundRect(pillRect, 19.0f, rgba(11, 13, 19, 0.94f));
    strokeRoundRect(pillRect, 19.0f, withAlpha(COL_BORDER_FOCUS, 0.35f), 1.0f);

    // Accent progress fill bar under the surface (Windows Fluent Blue #0078D6)
    if (progress > 0.005f) {
        float barW = (pillW - 8.0f) * std::clamp(progress, 0.0f, 1.0f);
        UiRect barRect = { pillLeft + 4.0f, pillBottom - 4.0f, pillLeft + 4.0f + barW, pillBottom - 2.0f };
        fillRoundRect(barRect, 1.0f, COL_PRIMARY_ACCENT);
    }

    // Pulse dot
    drawPulseDot(pillLeft + 16.0f, pillRect.centerY(), 3.5f, COL_PRIMARY_ACCENT, 1.0f);

    // Label formatting: e.g. "Setup.iso • 78% • 24.6 MB/s"
    char buf[128];
    if (fname.size() > 18) {
        fname = fname.substr(0, 15) + "...";
    }
    if (speedMBs > 0.05f) {
        std::snprintf(buf, sizeof(buf), "%s • %d%% • %.1f MB/s", fname.c_str(), pct, speedMBs);
    } else {
        std::snprintf(buf, sizeof(buf), "%s • %d%%", fname.c_str(), pct);
    }
    UiRect textRect = { pillLeft + 28.0f, pillTop, pillRight - 38.0f, pillBottom };
    drawText(buf, textRect, fmtSmall_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);

    // Interactive Cancel Button "✕"
    UiRect cancelBtn = { pillRight - 32.0f, pillTop + 6.0f, pillRight - 8.0f, pillBottom - 6.0f };
    drawButton("island_clip_cancel_btn", cancelBtn, "✕",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   network_.clipboardFileTransferManager().cancelActiveTransfer();
                   showToast("Clipboard transfer cancelled");
               }, fmtSmall_, true, COL_BORDER);
}

// ---------------- macOS Keyboard Shortcuts Sheet Modal ----------------

void CppDeskWindow::drawShortcutsModal(float width, float height, float modalProgress) {
    if (modalProgress <= 0.005f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 7, 12, 0.55f * alpha));

    // Clicking scrim closes modal
    clickRegions_.push_back({ { 0.0f, 0.0f, width, height }, "modal_sc_scrim", [this]() {
        showShortcutsModal_ = false;
    }, false });

    float mw = 560.0f;
    float mh = 440.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.90f + 0.10f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardSurface(modal, 20.0f, alpha);

    float mx = modal.left + 28.0f;
    float mrx = modal.right - 28.0f;
    float my = modal.top + 24.0f;

    drawText("Keyboard Shortcuts", { mx, my, mrx - 40.0f, my + 28.0f }, fmtHeading_, COL_TEXT_PRIMARY);

    UiRect closeBtn = { mrx - 28.0f, my, mrx, my + 28.0f };
    drawButton("modal_sc_close", closeBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 14.0f,
               [this]() { showShortcutsModal_ = false; }, fmtHeading_);
    my += 38.0f;

    drawText("Speed up remote navigation and desktop control with global hotkeys.",
             { mx, my, mrx, my + 20.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 26.0f;

    struct ShortcutRow {
        std::string key;
        std::string desc;
    };
    std::vector<ShortcutRow> rows = {
        { "F11", "Toggle Borderless Fullscreen Mode" },
        { "F1  or  ?", "Open / Close this Shortcuts Cheat Sheet" },
        { "F8", "Toggle Remote Input Control (View-Only vs Control)" },
        { "Ctrl + Alt + [1-9]", "Switch Remote Display Monitor instantly" },
        { "Ctrl + Alt + L", "Lock Remote Workstation" },
        { "Ctrl + Alt + D", "Show Desktop (Minimize all remote windows)" },
        { "Ctrl + Alt + Del", "Open Task Manager / Lock Screen" },
        { "Ctrl + Shift + O", "Toggle Real-Time Performance & Diagnostics HUD" },
        { "Esc", "Dismiss open menus, modals, or exit fullscreen" }
    };

    float rowH = 30.0f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        UiRect badgeRect = { mx, my, mx + 160.0f, my + 24.0f };
        fillRoundRect(badgeRect, 6.0f, COL_BG_SUBTLE);
        strokeRoundRect(badgeRect, 6.0f, COL_BORDER, 1.0f);
        drawText(r.key, badgeRect, fmtMono_, COL_TEXT_ACCENT, DWRITE_TEXT_ALIGNMENT_CENTER);

        UiRect descRect = { mx + 172.0f, my, mrx, my + 24.0f };
        drawText(r.desc, descRect, fmtBody_, COL_TEXT_PRIMARY, DWRITE_TEXT_ALIGNMENT_LEADING);

        my += rowH;
    }

    my += 8.0f;
    drawText("Tip: In Fullscreen, move your mouse to the top edge to reveal the Dynamic Island.",
             { mx, my, mrx, my + 20.0f }, fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- TCP Port Forwarding Manager Sheet Modal (v2.1.0) ----------------

void CppDeskWindow::drawPortForwardModal(float width, float height, float modalProgress) {
    if (!showPortForwardModal_ && modalProgress <= 0.01f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 8, 15, 0.52f * alpha));

    float mw = 620.0f;
    float mh = 510.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.90f + 0.10f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardSurface(modal, 20.0f, alpha);

    float mx = modal.left + 28.0f;
    float mrx = modal.right - 28.0f;
    float my = modal.top + 22.0f;

    drawText("TCP Port Forwarding & Tunneling", { mx, my, mrx - 40.0f, my + 28.0f }, fmtHeading_, COL_TEXT_PRIMARY);

    UiRect closeBtn = { mrx - 28.0f, my, mrx, my + 28.0f };
    drawButton("modal_pf_close", closeBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 14.0f,
               [this]() { showPortForwardModal_ = false; }, fmtHeading_);
    my += 34.0f;

    drawText("Forward local network ports securely to services on the remote PC.",
             { mx, my, mrx, my + 20.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 26.0f;

    // Presets Row
    drawText("QUICK PRESETS", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_MUTED);
    my += 20.0f;

    float preW = (mrx - mx - 18.0f) / 4.0f;
    UiRect p1 = { mx, my, mx + preW, my + 28.0f };
    UiRect p2 = { p1.right + 6.0f, my, p1.right + 6.0f + preW, my + 28.0f };
    UiRect p3 = { p2.right + 6.0f, my, p2.right + 6.0f + preW, my + 28.0f };
    UiRect p4 = { p3.right + 6.0f, my, mrx, my + 28.0f };

    drawButton("pre_rdp", p1, "RDP (33890->3389)", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.5f, [this]() {
        forwardLocalPortEdit_ = "33890";
        forwardTargetPortEdit_ = "3389";
        forwardDescEdit_ = "RDP Remote Desktop";
    }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("pre_ssh", p2, "SSH (2222->22)", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.5f, [this]() {
        forwardLocalPortEdit_ = "2222";
        forwardTargetPortEdit_ = "22";
        forwardDescEdit_ = "SSH Terminal";
    }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("pre_web", p3, "Web (8080->80)", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.5f, [this]() {
        forwardLocalPortEdit_ = "8080";
        forwardTargetPortEdit_ = "80";
        forwardDescEdit_ = "Web Server";
    }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    drawButton("pre_vnc", p4, "VNC (5901->5900)", COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 6.5f, [this]() {
        forwardLocalPortEdit_ = "5901";
        forwardTargetPortEdit_ = "5900";
        forwardDescEdit_ = "VNC Server";
    }, fmtSmall_, true, COL_BORDER, COL_TEXT_ACCENT);

    my += 36.0f;

    // Add Rule Inputs Row
    drawText("NEW TUNNEL RULE", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_MUTED);
    my += 20.0f;

    float fLocalW = 100.0f;
    float fTargetW = 100.0f;
    float fAddW = 76.0f;
    float fDescW = mrx - mx - fLocalW - fTargetW - fAddW - 24.0f;

    UiRect rLocal = { mx, my, mx + fLocalW, my + 32.0f };
    drawTextField("field_fwd_local", FocusedField::ForwardLocal, rLocal, forwardLocalPortEdit_, "Local Port", false);

    UiRect rTarget = { rLocal.right + 8.0f, my, rLocal.right + 8.0f + fTargetW, my + 32.0f };
    drawTextField("field_fwd_target", FocusedField::ForwardTarget, rTarget, forwardTargetPortEdit_, "Target Port", false);

    UiRect rDesc = { rTarget.right + 8.0f, my, rTarget.right + 8.0f + fDescW, my + 32.0f };
    drawTextField("field_fwd_desc", FocusedField::ForwardDesc, rDesc, forwardDescEdit_, "Description (optional)", false);

    UiRect rAdd = { rDesc.right + 8.0f, my, mrx, my + 32.0f };
    drawButton("btn_add_fwd_rule", rAdd, "Add Rule", COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 7.5f, [this]() {
        try {
            int lp = std::stoi(forwardLocalPortEdit_);
            int tp = std::stoi(forwardTargetPortEdit_);
            if (lp > 0 && lp <= 65535 && tp > 0 && tp <= 65535) {
                network_.addPortForwardRule(static_cast<uint16_t>(lp), static_cast<uint16_t>(tp), forwardDescEdit_, true);
                showToast("Tunnel rule added: " + std::to_string(lp) + " -> " + std::to_string(tp));
            } else {
                showToast("Ports must be between 1 and 65535", true);
            }
        } catch (...) {
            showToast("Invalid port number", true);
        }
    }, fmtSmall_);

    my += 44.0f;

    // Active Rules Table
    drawText("ACTIVE TUNNELS", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_MUTED);
    my += 20.0f;

    UiRect tableBox = { mx, my, mrx, modal.bottom - 22.0f };
    fillRoundRect(tableBox, 10.0f, COL_BG_SUBTLE);
    strokeRoundRect(tableBox, 10.0f, COL_BORDER);

    auto rules = network_.portForwardRules();
    if (rules.empty()) {
        drawText("No port forwarding rules configured. Add one above or select a preset.",
                 { tableBox.left + 16.0f, tableBox.centerY() - 14.0f, tableBox.right - 16.0f, tableBox.centerY() + 14.0f },
                 fmtSmall_, COL_TEXT_MUTED, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        float rTop = tableBox.top + 8.0f;
        float itemH = 46.0f;

        for (size_t i = 0; i < rules.size() && (rTop + itemH <= tableBox.bottom - 6.0f); ++i) {
            const auto& r = rules[i];
            UiRect cardR = { tableBox.left + 8.0f, rTop, tableBox.right - 8.0f, rTop + itemH - 4.0f };
            fillRoundRect(cardR, 8.0f, COL_BG_CARD);
            strokeRoundRect(cardR, 8.0f, COL_BORDER);

            drawPulseDot(cardR.left + 14.0f, cardR.centerY(), 3.5f, r.active ? COL_SUCCESS : COL_TEXT_MUTED);

            std::string routeStr = "127.0.0.1:" + std::to_string(r.localPort) + "  ➔  Remote :" + std::to_string(r.targetPort);
            drawText(routeStr, { cardR.left + 26.0f, cardR.top + 4.0f, cardR.right - 140.0f, cardR.top + 22.0f },
                     fmtBodyBold_, COL_TEXT_PRIMARY);

            std::string sub = r.description.empty() ? "TCP Proxy" : r.description;
            char statsBuf[96];
            std::snprintf(statsBuf, sizeof(statsBuf), " • In: %.1f KB  Out: %.1f KB",
                          r.bytesTransferredIn / 1024.0f, r.bytesTransferredOut / 1024.0f);
            sub += statsBuf;
            drawText(sub, { cardR.left + 26.0f, cardR.top + 22.0f, cardR.right - 140.0f, cardR.bottom - 4.0f },
                     fmtSmall_, COL_TEXT_SECONDARY);

            uint32_t rid = r.ruleId;
            bool isAct = r.active;
            UiRect toggleBtn = { cardR.right - 130.0f, cardR.top + 7.0f, cardR.right - 66.0f, cardR.bottom - 7.0f };
            drawButton("rule_tog_" + std::to_string(rid), toggleBtn, isAct ? "Pause" : "Resume",
                       isAct ? COL_SEC_BTN_BG : COL_PRIMARY_ACCENT,
                       isAct ? COL_SEC_BTN_HV : COL_PRIMARY_ACCENT_HV,
                       isAct ? COL_TEXT_PRIMARY : COL_TEXT_ON_ACCENT,
                       6.0f, [this, rid, isAct]() {
                           network_.setPortForwardRuleActive(rid, !isAct);
                       }, fmtSmall_, isAct, COL_BORDER);

            UiRect delBtn = { cardR.right - 60.0f, cardR.top + 7.0f, cardR.right - 8.0f, cardR.bottom - 7.0f };
            drawButton("rule_del_" + std::to_string(rid), delBtn, "Del",
                       COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_SECONDARY,
                       6.0f, [this, rid]() {
                           network_.removePortForwardRule(rid);
                           showToast("Tunnel rule removed");
                       }, fmtSmall_, false, D2D1::ColorF(0, 0, 0, 0), COL_TEXT_ON_ACCENT);

            rTop += itemH;
        }
    }

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- Address Book Edit Sheet Modal (v2.1.0) ----------------

void CppDeskWindow::drawAddressBookModal(float width, float height, float modalProgress) {
    if (!showAddressBookEditModal_ && modalProgress <= 0.01f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 8, 15, 0.52f * alpha));

    float mw = 480.0f;
    float mh = 380.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.90f + 0.10f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardSurface(modal, 20.0f, alpha);

    float mx = modal.left + 28.0f;
    float mrx = modal.right - 28.0f;
    float my = modal.top + 22.0f;

    drawText("Edit Desk Details", { mx, my, mrx - 40.0f, my + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);

    UiRect closeBtn = { mrx - 28.0f, my, mrx, my + 28.0f };
    drawButton("modal_ab_close", closeBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 14.0f,
               [this]() { showAddressBookEditModal_ = false; }, fmtHeading_);
    my += 34.0f;

    std::string idText = "Desk ID: " + CryptoUtils::formatDeskId(editingDeskId_);
    drawText(idText, { mx, my, mrx, my + 18.0f }, fmtSmall_, COL_PRIMARY_ACCENT);
    my += 24.0f;

    // Alias Field
    drawText("Machine Alias / Display Name", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 18.0f;
    UiRect aliasField = { mx, my, mrx, my + 34.0f };
    drawTextField("field_edit_alias", FocusedField::EditAlias, aliasField,
                  editAliasInput_, "e.g. Office Workstation...", false);
    my += 44.0f;

    // Tag Field
    drawText("Category Tag (e.g. Work, Servers, Personal)", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 18.0f;
    UiRect tagField = { mx, my, mrx, my + 34.0f };
    drawTextField("field_edit_tag", FocusedField::EditTag, tagField,
                  editTagInput_, "e.g. Work, Servers, Personal...", false);
    my += 44.0f;

    // Notes Field
    drawText("Notes & Remarks", { mx, my, mrx, my + 16.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 18.0f;
    UiRect notesField = { mx, my, mrx, my + 34.0f };
    drawTextField("field_edit_notes", FocusedField::EditNotes, notesField,
                  editNotesInput_, "e.g. Port 3389 forwarded, VPN needed...", false);
    my += 48.0f;

    // Action Buttons
    float btnW = (mrx - mx - 12.0f) * 0.5f;
    UiRect cancelBtn = { mx, my, mx + btnW, my + 36.0f };
    drawButton("modal_ab_cancel", cancelBtn, "Cancel",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f, [this]() {
                   showAddressBookEditModal_ = false;
               }, fmtSmall_, true, COL_BORDER);

    UiRect saveBtn = { cancelBtn.right + 12.0f, my, mrx, my + 36.0f };
    drawButton("modal_ab_save", saveBtn, "Save Details",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.0f, [this]() {
                   identity_.updateRecentSessionMetadata(editingDeskId_, editAliasInput_, editTagInput_, editNotesInput_);
                   showAddressBookEditModal_ = false;
                   showToast("Saved desk details");
               }, fmtSmall_);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- Mandatory Update Modal (v3.0.0) ----------------

void CppDeskWindow::drawUpdateRequiredModal(float width, float height, float modalProgress) {
    if (!showUpdateRequiredModal_ && modalProgress <= 0.01f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 8, 15, 0.72f * alpha));

    float mw = 520.0f;
    float mh = 330.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.90f + 0.10f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardShadow(modal, 24.0f, 1.5f * alpha);
    drawCardSurface(modal, 20.0f, alpha);
    strokeRoundRect(modal, 20.0f, withAlpha(COL_DANGER, 0.6f * alpha), 1.5f);

    float mx = modal.left + 32.0f;
    float mrx = modal.right - 32.0f;
    float my = modal.top + 28.0f;

    // Warning Badge
    UiRect badgeRect = { mx, my, mx + 160.0f, my + 24.0f };
    fillRoundRect(badgeRect, 6.0f, rgba(235, 87, 87, 0.18f));
    strokeRoundRect(badgeRect, 6.0f, rgba(235, 87, 87, 0.45f), 1.0f);
    drawText("MANDATORY UPDATE", badgeRect, fmtSmall_, COL_DANGER, DWRITE_TEXT_ALIGNMENT_CENTER);
    my += 34.0f;

    // Heading
    drawText("Update Required to Continue", { mx, my, mrx, my + 30.0f }, fmtHeading_, COL_TEXT_PRIMARY);
    my += 34.0f;

    // Version Information
    std::string verText = "CppDesk v" + latestUpdateInfo_.latestVersion + " is now available (Current: v" + CPP_DESK_VERSION + ").";
    drawText(verText, { mx, my, mrx, my + 20.0f }, fmtBodyBold_, COL_PRIMARY_ACCENT);
    my += 28.0f;

    // Description text box
    UiRect infoBox = { mx, my, mrx, my + 88.0f };
    fillRoundRect(infoBox, 10.0f, COL_BG_SUBTLE);
    strokeRoundRect(infoBox, 10.0f, COL_BORDER);

    std::string descText = "To ensure end-to-end cryptographic security, protocol compatibility, and uninterrupted remote desktop connections, all clients must update to the latest release.\n\nRemote sessions are locked until CppDesk is updated.";
    drawText(descText, { infoBox.left + 14.0f, infoBox.top + 10.0f, infoBox.right - 14.0f, infoBox.bottom - 10.0f },
             fmtSmall_, COL_TEXT_SECONDARY);
    my = infoBox.bottom + 26.0f;

    // Action buttons
    float btnW = (mrx - mx - 14.0f) * 0.5f;

    UiRect exitBtn = { mx, my, mx + btnW, my + 40.0f };
    drawButton("update_modal_exit", exitBtn, "Exit CppDesk",
               COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_PRIMARY, 9.0f, [this]() {
                   DestroyWindow(hwnd_);
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ON_ACCENT);

    UiRect dlBtn = { exitBtn.right + 14.0f, my, mrx, my + 40.0f };
    drawButton("update_modal_download", dlBtn, "Download & Update Now",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 9.0f, [this]() {
                   std::string targetUrl = latestUpdateInfo_.releaseUrl.empty()
                       ? "https://github.com/nmnghia2527/cppdesk/releases/latest"
                       : latestUpdateInfo_.releaseUrl;
                   ShellExecuteA(nullptr, "open", targetUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
               }, fmtSmall_);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- Remote Reboot & Reconnect Sheet Modal (Phase 12) ----------------

void CppDeskWindow::drawRebootConfirmModal(float width, float height, float modalProgress) {
    if (!showRebootConfirmModal_ && modalProgress <= 0.01f) return;

    float alpha = std::clamp(modalProgress, 0.0f, 1.0f);
    fillRoundRect({ 0.0f, 0.0f, width, height }, 0.0f, rgba(5, 8, 15, 0.55f * alpha));

    float mw = 480.0f;
    float mh = 250.0f;
    UiRect modal = { (width - mw) * 0.5f, (height - mh) * 0.5f, (width + mw) * 0.5f, (height + mh) * 0.5f };

    float scale = 0.90f + 0.10f * modalProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(modal.centerX(), modal.centerY()))
    );

    drawCardSurface(modal, 18.0f, alpha);

    float mx = modal.left + 26.0f;
    float mrx = modal.right - 26.0f;
    float my = modal.top + 22.0f;

    drawText("Remote Reboot & Reconnect", { mx, my, mrx - 40.0f, my + 26.0f }, fmtHeading_, COL_TEXT_PRIMARY);

    UiRect closeBtn = { mrx - 26.0f, my, mrx, my + 26.0f };
    drawButton("modal_reboot_close", closeBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 13.0f,
               [this]() { showRebootConfirmModal_ = false; }, fmtHeading_);
    my += 34.0f;

    drawText("The remote machine will restart and automatically reconnect once Windows boots back up. A secure resume token will authenticate the new session without requiring a password.",
             { mx, my, mrx, my + 44.0f }, fmtSmall_, COL_TEXT_SECONDARY);
    my += 52.0f;

    // Safe mode toggle
    UiRect toggleR = { mx, my, mrx, my + 26.0f };
    drawToggleSwitch("modal_reboot_safemode", toggleR, rebootSafeModeChoice_,
                     "Boot into Safe Mode with Networking", [this]() {
                         rebootSafeModeChoice_ = !rebootSafeModeChoice_;
                     });
    my += 40.0f;

    // Action buttons
    float btnW = 140.0f;
    float btnH = 34.0f;
    UiRect cancelBtn = { mrx - btnW * 2.0f - 10.0f, my, mrx - btnW - 10.0f, my + btnH };
    UiRect rebootBtn = { mrx - btnW, my, mrx, my + btnH };

    drawButton("modal_reboot_cancel", cancelBtn, "Cancel",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_PRIMARY, 8.0f,
               [this]() { showRebootConfirmModal_ = false; }, fmtSmall_, true, COL_BORDER);

    drawButton("modal_reboot_confirm", rebootBtn, "Reboot & Reconnect",
               COL_PRIMARY_ACCENT, COL_PRIMARY_ACCENT_HV, COL_TEXT_ON_ACCENT, 8.0f,
               [this]() {
                   showRebootConfirmModal_ = false;
                   if (network_.requestRemoteReboot(rebootSafeModeChoice_, 5)) {
                       showToast("Reboot requested. Reconnecting once online...");
                   } else {
                       showToast("Failed to request reboot", true);
                   }
               }, fmtSmall_);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void CppDeskWindow::triggerUpdateCheck(bool manual) {
    if (isCheckingUpdates_) return;
    isCheckingUpdates_ = true;
    updateStatusText_ = "Checking for updates...";
    if (manual) {
        showToast("Checking GitHub for updates...");
    }

    HWND targetHwnd = hwnd_;
    AutoUpdater::checkForUpdatesAsync("nmnghia2527", "cppdesk", CPP_DESK_VERSION, [targetHwnd, manual](const UpdateInfo& info) {
        if (targetHwnd && IsWindow(targetHwnd)) {
            auto* pInfo = new UpdateInfo(info);
            if (!PostMessageW(targetHwnd, WM_DESK_UPDATE_CHECK_DONE, reinterpret_cast<WPARAM>(pInfo), manual ? 1 : 0)) {
                delete pInfo;
            }
        }
    });
}

// ---------------- Whiteboard & Screen Annotation Overlay (v2.1.0) ----------------

void CppDeskWindow::drawWhiteboardOverlay(const UiRect& canvasRect) {
    if (canvasRect.width() <= 1.0f || canvasRect.height() <= 1.0f) return;

    auto strokes = network_.whiteboardManager().snapshotStrokes();
    for (const auto& s : strokes) {
        if (s.points.size() < 2 && s.tool != WhiteboardTool::LaserPointer) continue;

        float alpha = ((s.argbColor >> 24) & 0xFF) / 255.0f;
        if (s.tool == WhiteboardTool::Highlighter) {
            alpha = 0.35f;
        }
        D2D1_COLOR_F col = rgba(
            (s.argbColor >> 16) & 0xFF,
            (s.argbColor >> 8) & 0xFF,
            s.argbColor & 0xFF,
            alpha
        );
        solidBrush_->SetColor(col);

        float strokeThick = (s.tool == WhiteboardTool::Highlighter) ? (s.thickness * 2.5f) : s.thickness;

        if (s.tool == WhiteboardTool::Arrow && s.points.size() >= 2) {
            const auto& p0 = s.points.front();
            const auto& p1 = s.points.back();
            float x0 = canvasRect.left + p0.x * canvasRect.width();
            float y0 = canvasRect.top + p0.y * canvasRect.height();
            float x1 = canvasRect.left + p1.x * canvasRect.width();
            float y1 = canvasRect.top + p1.y * canvasRect.height();

            renderTarget_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), solidBrush_, strokeThick);

            float dx = x1 - x0;
            float dy = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 4.0f) {
                float angle = std::atan2(dy, dx);
                constexpr float headAngle = 0.45f;
                float headLen = std::min(18.0f, len * 0.35f);
                float a1x = x1 - headLen * std::cos(angle - headAngle);
                float a1y = y1 - headLen * std::sin(angle - headAngle);
                float a2x = x1 - headLen * std::cos(angle + headAngle);
                float a2y = y1 - headLen * std::sin(angle + headAngle);
                renderTarget_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(a1x, a1y), solidBrush_, strokeThick);
                renderTarget_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(a2x, a2y), solidBrush_, strokeThick);
            }
        } else if (s.points.size() >= 2) {
            for (size_t i = 1; i < s.points.size(); ++i) {
                const auto& pt0 = s.points[i - 1];
                const auto& pt1 = s.points[i];
                float x0 = canvasRect.left + pt0.x * canvasRect.width();
                float y0 = canvasRect.top + pt0.y * canvasRect.height();
                float x1 = canvasRect.left + pt1.x * canvasRect.width();
                float y1 = canvasRect.top + pt1.y * canvasRect.height();
                renderTarget_->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), solidBrush_, strokeThick);
            }
        }
    }

    // Laser pointer glowing dot
    float lx = -1.0f, ly = -1.0f, lalpha = 0.0f;
    if (network_.whiteboardManager().getLaserPointer(lx, ly, lalpha)) {
        float lpx = canvasRect.left + lx * canvasRect.width();
        float lpy = canvasRect.top + ly * canvasRect.height();
        solidBrush_->SetColor(rgba(255, 59, 48, 0.28f * lalpha));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lpx, lpy), 14.0f, 14.0f), solidBrush_);
        solidBrush_->SetColor(rgba(255, 59, 48, 0.95f * lalpha));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lpx, lpy), 5.5f, 5.5f), solidBrush_);
        solidBrush_->SetColor(rgba(255, 255, 255, 0.98f * lalpha));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lpx, lpy), 2.2f, 2.2f), solidBrush_);
    }

    if (!whiteboardActive_) return;

    // Floating Whiteboard Toolbar Palette at bottom of canvas
    float palW = 460.0f;
    float palH = 42.0f;
    float palLeft = canvasRect.centerX() - palW * 0.5f;
    float palTop = canvasRect.bottom - palH - 16.0f;
    float palRight = palLeft + palW;
    float palBottom = palTop + palH;
    UiRect palRect = { palLeft, palTop, palRight, palBottom };

    drawCardShadow(palRect, 21.0f, 0.95f);
    fillRoundRect(palRect, 21.0f, withAlpha(COL_BG_CARD, 0.96f));
    strokeRoundRect(palRect, 21.0f, withAlpha(COL_BORDER_FOCUS, 0.40f), 1.2f);

    float curX = palLeft + 12.0f;

    auto addToolBtn = [&](const std::string& id, const std::string& label, WhiteboardTool tool) {
        bool isSel = (whiteboardTool_ == tool);
        UiRect tr = { curX, palTop + 6.0f, curX + 52.0f, palBottom - 6.0f };
        drawButton(id, tr, label,
                   isSel ? COL_PRIMARY_ACCENT : COL_SEC_BTN_BG,
                   isSel ? COL_PRIMARY_ACCENT_HV : COL_SEC_BTN_HV,
                   isSel ? COL_TEXT_ON_ACCENT : COL_TEXT_PRIMARY,
                   8.0f, [this, tool]() {
                       whiteboardTool_ = tool;
                       network_.whiteboardManager().setActiveTool(tool);
                   }, fmtSmall_, !isSel, COL_BORDER);
        curX = tr.right + 5.0f;
    };

    addToolBtn("wb_tool_pen", "Pen", WhiteboardTool::Pen);
    addToolBtn("wb_tool_high", "High", WhiteboardTool::Highlighter);
    addToolBtn("wb_tool_arrow", "Arrow", WhiteboardTool::Arrow);
    addToolBtn("wb_tool_laser", "Laser", WhiteboardTool::LaserPointer);

    // Clear All button
    UiRect clearBtn = { curX, palTop + 6.0f, curX + 48.0f, palBottom - 6.0f };
    drawButton("wb_clear_btn", clearBtn, "Clear",
               COL_SEC_BTN_BG, COL_DANGER, COL_TEXT_PRIMARY,
               8.0f, [this]() {
                   network_.whiteboardManager().clearAllStrokes();
                   network_.sendWhiteboardClear();
                   showToast("Whiteboard cleared");
               }, fmtSmall_, true, COL_BORDER, COL_TEXT_ON_ACCENT);
    curX = clearBtn.right + 10.0f;

    // Vertical separator
    fillRoundRect({ curX, palTop + 10.0f, curX + 1.0f, palBottom - 10.0f }, 0.5f, COL_BORDER);
    curX += 8.0f;

    // Color Swatches: Red, Green, Blue, Yellow, White
    struct ColorSwatch { uint32_t argb; const char* id; };
    ColorSwatch swatches[] = {
        { 0xFFFF3B30, "wb_col_red" },
        { 0xFF34C759, "wb_col_green" },
        { 0xFF007AFF, "wb_col_blue" },
        { 0xFFFFCC00, "wb_col_yellow" },
        { 0xFFFFFFFF, "wb_col_white" }
    };

    for (const auto& sw : swatches) {
        bool isSel = (whiteboardColor_ == sw.argb);
        UiRect cr = { curX, palTop + 11.0f, curX + 20.0f, palBottom - 11.0f };
        D2D1_COLOR_F c = rgba(
            (sw.argb >> 16) & 0xFF,
            (sw.argb >> 8) & 0xFF,
            sw.argb & 0xFF
        );
        fillRoundRect(cr, 10.0f, c);
        if (isSel) {
            strokeRoundRect(cr.inflate(2.0f, 2.0f), 12.0f, COL_PRIMARY_ACCENT, 2.0f);
        } else {
            strokeRoundRect(cr, 10.0f, rgba(0, 0, 0, 0.25f), 1.0f);
        }
        clickRegions_.push_back({ cr, sw.id, [this, argb = sw.argb]() {
            whiteboardColor_ = argb;
            network_.whiteboardManager().setActiveColor(argb);
        }, false });
        curX = cr.right + 6.0f;
    }

    curX += 4.0f;

    // Close whiteboard palette
    UiRect closePalBtn = { curX, palTop + 6.0f, palRight - 8.0f, palBottom - 6.0f };
    drawButton("wb_close_palette", closePalBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_SECONDARY,
               8.0f, [this]() {
                   whiteboardActive_ = false;
                   showToast("Whiteboard closed");
               }, fmtBodyBold_, true, COL_BORDER);
}

// ---------------- Real-Time Performance & Diagnostics HUD Overlay (Phase 10) ----------------

void CppDeskWindow::drawPerformanceHud(const UiRect& stageRect, float alpha) {
    if (hudAnimT_ <= 0.01f || alpha <= 0.01f) return;

    float hudProgress = std::clamp(hudAnimT_, 0.0f, 1.0f) * std::clamp(alpha, 0.0f, 1.0f);
    auto stats = network_.viewerStats();

    float hudW = 310.0f;
    float hudH = 224.0f;
    float hudRight = stageRect.right - 18.0f;
    float hudTop = stageRect.top + 16.0f;
    float hudLeft = hudRight - hudW;
    float hudBottom = hudTop + hudH;
    UiRect hudCard = { hudLeft, hudTop, hudRight, hudBottom };

    // Apply spring slide & scale transform
    float scale = 0.94f + 0.06f * hudProgress;
    renderTarget_->SetTransform(
        D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(hudCard.right, hudCard.top))
    );

    // Frosted glass card surface with specular highlight (hud-sci-fi-fui)
    drawCardShadow(hudCard, 14.0f, 0.95f * hudProgress);
    fillRoundRect(hudCard, 14.0f, rgba(11, 14, 20, 0.92f * hudProgress));
    strokeRoundRect(hudCard, 14.0f, withAlpha(COL_BORDER_FOCUS, 0.45f * hudProgress), 1.2f);
    fillRoundRect({ hudCard.left + 14.0f, hudCard.top + 1.0f, hudCard.right - 14.0f, hudCard.top + 2.0f },
                  0.5f, rgba(255, 255, 255, 0.40f * hudProgress));

    // Header: Pulse dot + Title + Close button
    float curX = hudCard.left + 14.0f;
    drawPulseDot(curX + 4.0f, hudCard.top + 16.0f, 3.6f, rgba(16, 185, 129, hudProgress), hudProgress);
    curX += 16.0f;
    drawText("DIAGNOSTICS & TELEMETRY", { curX, hudCard.top + 8.0f, hudCard.right - 36.0f, hudCard.top + 26.0f },
             fmtSmall_, withAlpha(COL_TEXT_PRIMARY, hudProgress));

    UiRect closeBtn = { hudCard.right - 28.0f, hudCard.top + 7.0f, hudCard.right - 8.0f, hudCard.top + 27.0f };
    drawButton("hud_close_btn", closeBtn, "×",
               COL_SEC_BTN_BG, COL_SEC_BTN_HV, COL_TEXT_SECONDARY,
               6.0f, [this]() {
                   showPerformanceHud_ = false;
               }, fmtSmall_, true, COL_BORDER);

    // Thin separator line
    fillRoundRect({ hudCard.left + 12.0f, hudCard.top + 33.0f, hudCard.right - 12.0f, hudCard.top + 34.0f },
                  0.0f, withAlpha(COL_BORDER, 0.65f * hudProgress));

    float cy = hudCard.top + 40.0f;

    // Row 1: Framerate & Display
    float curFps = stats.fps;
    float frameTimeMs = (curFps > 0.5f) ? (1000.0f / curFps) : 0.0f;
    D2D1_COLOR_F fpsColor = (curFps >= 50.0f) ? rgba(16, 185, 129, hudProgress) :
                            (curFps >= 25.0f) ? rgba(245, 158, 11, hudProgress) :
                                                rgba(239, 68, 68, hudProgress);

    char fpsBuf[64];
    std::snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS (%.1f ms)", curFps, frameTimeMs);
    drawText(fpsBuf, { hudCard.left + 14.0f, cy, hudCard.left + 160.0f, cy + 18.0f },
             fmtBodyBold_, fpsColor);

    char resBuf[64];
    const char* presetName = (stats.qualityPreset == QualityPreset::Ultra) ? "Ultra" :
                             (stats.qualityPreset == QualityPreset::Balanced) ? "Balanced" : "Low";
    std::snprintf(resBuf, sizeof(resBuf), "%dx%d • %s", stats.frameWidth, stats.frameHeight, presetName);
    drawText(resBuf, { hudCard.left + 160.0f, cy, hudCard.right - 14.0f, cy + 18.0f },
             fmtSmall_, withAlpha(COL_TEXT_SECONDARY, hudProgress), DWRITE_TEXT_ALIGNMENT_TRAILING);

    cy += 24.0f;

    // Row 2: Latency Pipeline (Capture | Encode | Decode)
    drawText("LATENCY PIPELINE", { hudCard.left + 14.0f, cy, hudCard.right - 14.0f, cy + 14.0f },
             fmtSmall_, withAlpha(COL_TEXT_MUTED, hudProgress * 0.85f));
    cy += 16.0f;

    char pipeBuf[80];
    std::snprintf(pipeBuf, sizeof(pipeBuf), "Cap: %.1f ms   Enc: %.1f ms   Dec: %.1f ms",
                  stats.captureLatencyMs, stats.encodeLatencyMs, stats.decodeLatencyMs);
    drawText(pipeBuf, { hudCard.left + 14.0f, cy, hudCard.right - 14.0f, cy + 18.0f },
             fmtMono_ ? fmtMono_ : fmtSmall_, withAlpha(COL_TEXT_PRIMARY, hudProgress));

    cy += 24.0f;

    // Row 3: Network Ping & Bitrate
    D2D1_COLOR_F pingColor = (stats.rttMs < 35) ? rgba(16, 185, 129, hudProgress) :
                             (stats.rttMs < 85) ? rgba(245, 158, 11, hudProgress) :
                                                  rgba(239, 68, 68, hudProgress);
    char netBuf[64];
    std::snprintf(netBuf, sizeof(netBuf), "RTT: %u ms   •   %.1f kbps", stats.rttMs, stats.kbps);
    drawText(netBuf, { hudCard.left + 14.0f, cy, hudCard.left + 190.0f, cy + 18.0f },
             fmtSmall_, pingColor);

    char compBuf[64];
    std::snprintf(compBuf, sizeof(compBuf), "Comp: %.1f:1 (%u tiles)",
                  ((std::isfinite(stats.compressionRatio) && stats.compressionRatio > 0.1f) ? stats.compressionRatio : 1.0f),
                  stats.deltaTilesCount);
    drawText(compBuf, { hudCard.left + 160.0f, cy, hudCard.right - 14.0f, cy + 18.0f },
             fmtSmall_, withAlpha(COL_TEXT_SECONDARY, hudProgress), DWRITE_TEXT_ALIGNMENT_TRAILING);

    cy += 20.0f;

    // Row 4: RTT Latency Sparkline
    if (stats.rttHistory.size() >= 2) {
        UiRect sparkRect = { hudCard.left + 14.0f, cy, hudCard.right - 14.0f, cy + 28.0f };
        drawSparkline(sparkRect, stats.rttHistory.data(), stats.rttHistory.size(), 120.0f, COL_PRIMARY_ACCENT, hudProgress);
    } else {
        UiRect sparkRect = { hudCard.left + 14.0f, cy, hudCard.right - 14.0f, cy + 28.0f };
        fillRoundRect(sparkRect, 4.0f, withAlpha(COL_BG_SUBTLE, hudProgress * 0.5f));
        drawText("Sampling network latency...", sparkRect, fmtSmall_,
                 withAlpha(COL_TEXT_MUTED, hudProgress), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    cy += 34.0f;

    // Row 5: Hotkey helper
    drawText("Press Ctrl + Shift + O to toggle overlay",
             { hudCard.left + 14.0f, cy, hudCard.right - 14.0f, cy + 16.0f },
             fmtSmall_, withAlpha(COL_TEXT_MUTED, hudProgress * 0.70f), DWRITE_TEXT_ALIGNMENT_CENTER);

    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
}

// ---------------- macOS Dynamic Capsule Toast Banner ----------------

void CppDeskWindow::drawToastBanner(float width, float height, float toastProgress) {
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

    drawPulseDot(r.left + 18.0f, r.centerY(), 3.6f, toastIsError_ ? COL_DANGER : COL_PRIMARY_ACCENT, alpha);
    drawText(toastText_, { r.left + 28.0f, r.top, r.right - 18.0f, r.bottom },
             fmtBodyBold_, withAlpha(COL_TEXT_PRIMARY, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---------------- Input & Interaction Handlers ----------------

bool CppDeskWindow::mapCanvasPointToNormalized(float x, float y, float& outNormX, float& outNormY) const {
    if (renderedCanvasRect_.width() <= 1.0f || renderedCanvasRect_.height() <= 1.0f) {
        return false;
    }
    if (!renderedCanvasRect_.contains(x, y)) {
        return false;
    }
    if (scaleMode_ == ScaleMode::FillAspect && stageRect_.width() > 1.0f && stageRect_.height() > 1.0f) {
        if (!stageRect_.contains(x, y)) {
            return false;
        }
    }

    if (modalAnimT_ > 0.004f || shortcutsModalAnimT_ > 0.004f || portForwardModalAnimT_ > 0.004f || addressBookModalAnimT_ > 0.004f) {
        return false;
    }
    if (isFullscreen_ && floatingToolbarY_ > -50.0f && y <= (floatingToolbarY_ + 50.0f)) {
        return false;
    }
    if (showDisplayMenu_ || showAdminMenu_ || showQualityMenu_) {
        return false;
    }

    if (drawerAnimT_ > 0.004f && hwnd_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        float w = static_cast<float>(rc.right - rc.left);
        float drawerW = std::min(395.0f, w * 0.42f);
        if (x >= (w - drawerW - 14.0f)) {
            return false;
        }
    }

    outNormX = std::clamp((x - renderedCanvasRect_.left) / renderedCanvasRect_.width(), 0.0f, 1.0f);
    outNormY = std::clamp((y - renderedCanvasRect_.top) / renderedCanvasRect_.height(), 0.0f, 1.0f);
    return true;
}

std::string* CppDeskWindow::activeFocusedTextBuffer() {
    if (focusedField_ == FocusedField::RemoteId) return &remoteIdInput_;
    if (focusedField_ == FocusedField::RemotePassword) return &remotePasswordInput_;
    if (focusedField_ == FocusedField::LocalPassword) return &localPasswordEdit_;
    if (focusedField_ == FocusedField::RelayServer) return &relayServerEdit_;
    if (focusedField_ == FocusedField::RelayAuthKey) return &relayAuthKeyEdit_;
    if (focusedField_ == FocusedField::StunServer) return &stunServerEdit_;
    if (focusedField_ == FocusedField::ChatInput) return &chatInput_;
    if (focusedField_ == FocusedField::TerminalInput) return &terminalInputText_;
    if (focusedField_ == FocusedField::ForwardLocal) return &forwardLocalPortEdit_;
    if (focusedField_ == FocusedField::ForwardTarget) return &forwardTargetPortEdit_;
    if (focusedField_ == FocusedField::ForwardDesc) return &forwardDescEdit_;
    if (focusedField_ == FocusedField::DashboardSearch) return &dashboardSearchQuery_;
    if (focusedField_ == FocusedField::EditAlias) return &editAliasInput_;
    if (focusedField_ == FocusedField::EditTag) return &editTagInput_;
    if (focusedField_ == FocusedField::EditNotes) return &editNotesInput_;
    if (focusedField_ == FocusedField::ClipboardSearch) return &clipSearchQuery_;
    return nullptr;
}

void CppDeskWindow::onMouseMove(float x, float y) {
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

    if (activeTab_ == ActiveTab::RemoteSession && !showFileDrawer_ &&
        !network_.pendingIncomingRequest().active) {
        float nx = 0.0f, ny = 0.0f;
        if (mapCanvasPointToNormalized(x, y, nx, ny)) {
            if (whiteboardActive_) {
                if (mouseLeftDown_) {
                    network_.whiteboardManager().addStrokePoint(nx, ny);
                    if (whiteboardTool_ == WhiteboardTool::LaserPointer) {
                        network_.sendWhiteboardLaser(nx, ny);
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            } else if (remoteInputEnabled_) {
                uint64_t now = GetTickCount64();
                if (now - lastMouseSendTick_ >= 10) {
                    lastMouseSendTick_ = now;
                    network_.sendMouseMove(nx, ny);
                }
            }
        }
    }

    if (hoverChanged) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CppDeskWindow::onMouseButton(MouseButtonId btn, bool isDown, float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    mouseInsideClient_ = true;

    // If Mandatory Update Modal is active, intercept clicks and only allow update_modal_* buttons
    if (showUpdateRequiredModal_) {
        if (btn == MouseButtonId::Left) {
            mouseLeftDown_ = isDown;
            if (isDown) {
                for (auto it = clickRegions_.rbegin(); it != clickRegions_.rend(); ++it) {
                    if (it->rect.contains(x, y) && it->id.rfind("update_modal_", 0) == 0) {
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
            } else {
                pressedWidgetId_.clear();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
        return; // Block all other interaction while mandatory update is pending
    }

    bool insideDrawer = false;
    if (drawerAnimT_ > 0.004f && hwnd_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        float w = static_cast<float>(rc.right - rc.left);
        float drawerW = std::min(395.0f, w * 0.42f);
        if (x >= (w - drawerW - 14.0f)) {
            insideDrawer = true;
        }
    }

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
            if (insideDrawer) {
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        } else {
            if (!pressedWidgetId_.empty()) {
                pressedWidgetId_.clear();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            if (insideDrawer) {
                return;
            }
            for (auto it = clickRegions_.rbegin(); it != clickRegions_.rend(); ++it) {
                if (it->rect.contains(x, y)) {
                    return;
                }
            }
        }
    } else if (insideDrawer) {
        return;
    }

    if (activeTab_ == ActiveTab::RemoteSession && !network_.pendingIncomingRequest().active) {
        float nx = 0.0f, ny = 0.0f;
        if (mapCanvasPointToNormalized(x, y, nx, ny)) {
            if (focusedField_ != FocusedField::ChatInput || !insideDrawer) {
                focusedField_ = FocusedField::RemoteCanvas;
            }
            if (whiteboardActive_) {
                if (btn == MouseButtonId::Left) {
                    if (isDown) {
                        network_.whiteboardManager().setActiveTool(whiteboardTool_);
                        network_.whiteboardManager().setActiveColor(whiteboardColor_);
                        network_.whiteboardManager().setActiveThickness(whiteboardThickness_);
                        network_.whiteboardManager().startStroke(nx, ny);
                        if (whiteboardTool_ == WhiteboardTool::LaserPointer) {
                            network_.sendWhiteboardLaser(nx, ny);
                        }
                    } else {
                        auto finished = network_.whiteboardManager().finishStroke();
                        if (!finished.points.empty()) {
                            network_.sendWhiteboardStroke(finished);
                        }
                    }
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return;
            }
            if (remoteInputEnabled_) {
                network_.sendMouseButton(btn, isDown, nx, ny);
            }
        }
    }
}

void CppDeskWindow::onMouseWheel(int delta) {
    if (drawerAnimT_ > 0.004f && hwnd_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        float w = static_cast<float>(rc.right - rc.left);
        float drawerW = std::min(395.0f, w * 0.42f);
        if (mouseX_ >= (w - drawerW - 14.0f)) {
            if (drawerTab_ == DrawerTab::LiveChat) {
                chatScrollOffset_ += (delta > 0 ? 1 : -1);
                if (chatScrollOffset_ < 0) chatScrollOffset_ = 0;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (drawerTab_ == DrawerTab::RemoteTerminal) {
                terminalScrollOffset_ += (delta > 0 ? -1.0f : 1.0f);
                if (terminalScrollOffset_ < 0.0f) terminalScrollOffset_ = 0.0f;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (drawerTab_ == DrawerTab::Diagnostics) {
                diagnosticsScrollOffset_ += (delta > 0 ? -1.0f : 1.0f);
                if (diagnosticsScrollOffset_ < 0.0f) diagnosticsScrollOffset_ = 0.0f;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (drawerTab_ == DrawerTab::ClipboardHistory) {
                clipHistoryScrollOffset_ += (delta > 0 ? -1 : 1);
                if (clipHistoryScrollOffset_ < 0) clipHistoryScrollOffset_ = 0;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }
    }

    if (activeTab_ == ActiveTab::RemoteSession && remoteInputEnabled_ &&
        renderedCanvasRect_.contains(mouseX_, mouseY_)) {
        float nx = 0.0f, ny = 0.0f;
        if (mapCanvasPointToNormalized(mouseX_, mouseY_, nx, ny)) {
            network_.sendMouseWheel(delta, 0);
        }
    }
}

void CppDeskWindow::onCharInput(wchar_t ch) {
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
        } else if (focusedField_ == FocusedField::TerminalInput) {
            sendTerminalFromInput();
        }
    } else if (ch == L'\t') {
        if (focusedField_ == FocusedField::RemoteId) focusedField_ = FocusedField::RemotePassword;
        else if (focusedField_ == FocusedField::RemotePassword) focusedField_ = FocusedField::LocalPassword;
        else if (focusedField_ == FocusedField::LocalPassword) focusedField_ = FocusedField::RelayServer;
        else focusedField_ = FocusedField::RemoteId;
    } else if (ch == 22) {
        if (focusedField_ == FocusedField::ChatInput) {
            std::vector<uint8_t> jpeg;
            uint32_t w = 0, h = 0;
            if (ClipboardManager::getClipboardImageJpeg(jpeg, w, h)) {
                network_.sendChatImage(jpeg, w, h, chatInput_);
                chatInput_.clear();
                showToast("Sent image from clipboard");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
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

void CppDeskWindow::onKeyEvent(uint16_t vk, uint16_t scan, bool isDown, bool isExtended) {
    if (isDown) {
        if (vk == VK_F1 || (vk == 0xBF /* VK_OEM_2 /? */ && (GetKeyState(VK_SHIFT) & 0x8000))) {
            toggleShortcutsModal();
            return;
        }
        if (vk == VK_F11) {
            toggleFullscreen();
            return;
        }
        if (vk == VK_ESCAPE) {
            if (showPerformanceHud_) {
                showPerformanceHud_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (showPortForwardModal_) {
                showPortForwardModal_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (showShortcutsModal_) {
                showShortcutsModal_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (showDisplayMenu_ || showAdminMenu_ || showQualityMenu_) {
                showDisplayMenu_ = false;
                showAdminMenu_ = false;
                showQualityMenu_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            if (focusedField_ == FocusedField::ChatInput) {
                focusedField_ = FocusedField::None;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
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
        // Ctrl+Shift+O: Toggle Real-time Performance & Diagnostics HUD Overlay
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (vk == 'O' || vk == 'o')) {
            showPerformanceHud_ = !showPerformanceHud_;
            showToast(showPerformanceHud_ ? "Performance HUD: ON" : "Performance HUD: OFF");
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        // Remote Hotkeys: Ctrl+Alt+[1-9], Ctrl+Alt+L, Ctrl+Alt+D, Ctrl+Alt+Del
        if (activeTab_ == ActiveTab::RemoteSession && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000)) {
            if (vk >= '1' && vk <= '9') {
                int monIdx = (vk - '1');
                network_.selectRemoteMonitor(monIdx);
                showToast("Switched to Display " + std::to_string(monIdx + 1));
                return;
            }
            if (vk == 'L') {
                network_.sendSystemAction(SystemActionType::LockWorkstation);
                showToast("Remote Workstation Locked");
                return;
            }
            if (vk == 'D') {
                network_.sendSystemAction(SystemActionType::ShowDesktop);
                showToast("Remote Show Desktop");
                return;
            }
            if (vk == VK_DELETE) {
                network_.sendSystemAction(SystemActionType::SendCtrlAltDel);
                showToast("Sent Ctrl+Alt+Del to remote PC");
                return;
            }
        }
        // Multi-Session Tab Navigation Hotkeys:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            if (vk == VK_TAB) {
                bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                uint32_t tid = shiftDown ? sessionTabs_.prevTab() : sessionTabs_.nextTab();
                if (tid != 0) {
                    switchToSessionTab(tid);
                }
                return;
            }
            if (vk == 'W' && activeTab_ == ActiveTab::RemoteSession && sessionTabs_.tabCount() > 0) {
                closeSessionTab(sessionTabs_.activeTabId());
                return;
            }
            if (vk == 'T' && activeTab_ == ActiveTab::RemoteSession) {
                switchTab(ActiveTab::Dashboard);
                focusedField_ = FocusedField::RemoteId;
                return;
            }
            if (vk >= '1' && vk <= '9' && !(GetKeyState(VK_SHIFT) & 0x8000) && sessionTabs_.tabCount() > 1) {
                size_t tabIdx = static_cast<size_t>(vk - '1');
                if (tabIdx < sessionTabs_.tabCount()) {
                    switchToSessionTab(sessionTabs_.tabs()[tabIdx].id);
                    return;
                }
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

void CppDeskWindow::onDropFiles(HDROP hDrop) {
    POINT pt{};
    DragQueryPoint(hDrop, &pt);

    float nx = 0.0f, ny = 0.0f;
    bool droppedOnCanvas = (activeTab_ == ActiveTab::RemoteSession &&
                            renderedCanvasRect_.contains(static_cast<float>(pt.x), static_cast<float>(pt.y)) &&
                            mapCanvasPointToNormalized(static_cast<float>(pt.x), static_cast<float>(pt.y), nx, ny));

    FileOfferTarget targetHint = droppedOnCanvas ? FileOfferTarget::Desktop : FileOfferTarget::DefaultDownloads;

    UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
    int sentCount = 0;
    for (UINT i = 0; i < count; ++i) {
        char filePath[MAX_PATH] = {};
        if (DragQueryFileA(hDrop, i, filePath, MAX_PATH) > 0) {
            sentCount += network_.sendDropPath(filePath, targetHint, nx, ny);
        }
    }
    DragFinish(hDrop);

    if (sentCount > 0) {
        if (droppedOnCanvas) {
            canvasDropEffectActive_ = true;
            canvasDropPos_ = { static_cast<float>(pt.x), static_cast<float>(pt.y) };
            canvasDropTick_ = GetTickCount64();
            canvasDropCount_ = sentCount;
            showToast("Dropped " + std::to_string(sentCount) + " file(s) onto Remote Desktop");
            if (hwnd_) {
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else {
            drawerTab_ = DrawerTab::FilesAndClip;
            showFileDrawer_ = true;
            showToast("Sending " + std::to_string(sentCount) + " file(s) to Downloads");
        }
    } else {
        showToast("Connect to a remote desk first", true);
    }
}

// ---------------- High-Level UI Actions ----------------

void CppDeskWindow::switchToSessionTab(uint32_t tabId, bool force) {
    if (tabId == 0) return;
    if (!force && tabId == sessionTabs_.activeTabId()) return;

    // 1. Cache current active tab state and frame
    SessionTab* curTab = sessionTabs_.activeTab();
    if (curTab && curTab->id != tabId) {
        curTab->scaleMode = scaleMode_;
        curTab->remoteInputEnabled = remoteInputEnabled_;
        if (!frameBufferBgra_.empty()) {
            sessionTabs_.cacheActiveTabFrame(frameBufferBgra_.data(), frameBufferW_, frameBufferH_, displayedFrameSeq_, remoteCursor_);
        }
    }

    // 2. Select new tab
    if (!sessionTabs_.selectTab(tabId)) return;
    SessionTab* newTab = sessionTabs_.activeTab();
    if (!newTab) return;

    // 3. Restore per-tab state
    scaleMode_ = newTab->scaleMode;
    remoteInputEnabled_ = newTab->remoteInputEnabled;

    // 4. Restore cached frame buffer
    if (!newTab->cachedFrameBgra.empty() && newTab->cachedW > 0 && newTab->cachedH > 0) {
        frameBufferBgra_ = newTab->cachedFrameBgra;
        frameBufferW_ = newTab->cachedW;
        frameBufferH_ = newTab->cachedH;
        displayedFrameSeq_ = newTab->lastFrameSeq;
        remoteCursor_ = newTab->cursor;

        if (renderTarget_) {
            if (remoteBitmap_) {
                remoteBitmap_->Release();
                remoteBitmap_ = nullptr;
            }
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
        }
    } else {
        frameBufferBgra_.clear();
        frameBufferW_ = 0;
        frameBufferH_ = 0;
        displayedFrameSeq_ = 0;
        if (remoteBitmap_) {
            remoteBitmap_->Release();
            remoteBitmap_ = nullptr;
        }
        bitmapW_ = 0;
        bitmapH_ = 0;
    }

    // 5. Check if connection target matches current network engine connection
    auto vStats = network_.viewerStats();
    bool isSameDesk = (newTab->deskId != 0 && vStats.remoteDeskId == newTab->deskId);
    if (!isSameDesk) {
        if (sessionRecorder_.isRecording()) {
            sessionRecorder_.stopRecording();
        }
        if (network_.isVoiceIntercomActive()) {
            network_.stopVoiceIntercom();
        }
        prevViewerState_ = ViewerConnectionState::Disconnected;
        network_.disconnectViewer();
        if (!newTab->targetInput.empty()) {
            network_.connectToRemote(newTab->targetInput, newTab->password);
        }
    }

    switchTab(ActiveTab::RemoteSession);
    std::string toastTitle = newTab->title.empty() ? (newTab->targetInput.empty() ? ("Desk " + std::to_string(newTab->deskId)) : newTab->targetInput) : newTab->title;
    showToast("Switched to tab: " + toastTitle);
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CppDeskWindow::closeSessionTab(uint32_t tabId) {
    if (tabId == 0) return;

    bool wasActive = (tabId == sessionTabs_.activeTabId());
    SessionTab* tab = sessionTabs_.getTab(tabId);
    std::string tabName = tab ? (tab->title.empty() ? tab->targetInput : tab->title) : "Session";

    if (wasActive) {
        if (sessionRecorder_.isRecording()) {
            sessionRecorder_.stopRecording();
        }
        if (network_.isVoiceIntercomActive()) {
            network_.stopVoiceIntercom();
        }
        prevViewerState_ = ViewerConnectionState::Disconnected;
        network_.disconnectViewer();
    }

    sessionTabs_.closeTab(tabId);

    if (sessionTabs_.tabCount() > 0) {
        if (wasActive) {
            switchToSessionTab(sessionTabs_.activeTabId(), true);
        }
        showToast("Closed tab: " + tabName);
    } else {
        frameBufferBgra_.clear();
        frameBufferW_ = 0;
        frameBufferH_ = 0;
        displayedFrameSeq_ = 0;
        if (remoteBitmap_) {
            remoteBitmap_->Release();
            remoteBitmap_ = nullptr;
        }
        bitmapW_ = 0;
        bitmapH_ = 0;
        switchTab(ActiveTab::Dashboard);
        showToast("All session tabs closed");
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void CppDeskWindow::initiateConnection() {
    if (showUpdateRequiredModal_ || latestUpdateInfo_.updateRequired) {
        showUpdateRequiredModal_ = true;
        showToast("Update required to connect to remote sessions!", true);
        return;
    }
    if (remoteIdInput_.empty()) {
        showToast("Enter a 9-digit Desk ID", true);
        return;
    }

    uint64_t targetDeskId = CryptoUtils::parseDeskId(remoteIdInput_);
    SessionTab* existing = (targetDeskId != 0) ? sessionTabs_.findTabByDeskId(targetDeskId) : sessionTabs_.findTabByTarget(remoteIdInput_);
    uint32_t targetTabId = 0;
    if (!existing) {
        targetTabId = sessionTabs_.createTab(targetDeskId, remoteIdInput_, "Desk " + remoteIdInput_);
        SessionTab* tab = sessionTabs_.getTab(targetTabId);
        if (tab) {
            tab->password = remotePasswordInput_;
            tab->state = ViewerConnectionState::ConnectingTcp;
        }
    } else {
        targetTabId = existing->id;
        existing->password = remotePasswordInput_;
        existing->state = ViewerConnectionState::ConnectingTcp;
    }

    switchToSessionTab(targetTabId, true);
}

void CppDeskWindow::openSendFileDialog() {
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

void CppDeskWindow::saveRemoteScreenshot() {
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

void CppDeskWindow::toggleScreenRecording() {
    if (sessionRecorder_.isRecording()) {
        std::string path = sessionRecorder_.currentFilePath();
        sessionRecorder_.stopRecording();

        std::error_code ec;
        uint64_t fsz = std::filesystem::file_size(path, ec);
        char szBuf[64];
        if (fsz > 1024 * 1024) {
            std::snprintf(szBuf, sizeof(szBuf), "%.1f MB", static_cast<double>(fsz) / (1024.0 * 1024.0));
        } else {
            std::snprintf(szBuf, sizeof(szBuf), "%.1f KB", static_cast<double>(fsz) / 1024.0);
        }

        std::string fname = std::filesystem::path(path).filename().string();
        showToast("Recording saved: " + fname + " (" + szBuf + ")");
    } else {
        if (frameBufferBgra_.empty() || frameBufferW_ <= 0 || frameBufferH_ <= 0) {
            showToast("No active remote video stream to record", true);
            return;
        }

        std::string dir = network_.fileTransferManager().receiveDirectory();
        std::filesystem::path recDir = std::filesystem::path(dir) / "Recordings";
        std::error_code ec;
        std::filesystem::create_directories(recDir, ec);

        SYSTEMTIME st;
        GetLocalTime(&st);
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "Recording_%04u-%02u-%02u_%02u-%02u-%02u.avi",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        std::filesystem::path fullPath = recDir / nameBuf;
        if (sessionRecorder_.startRecording(fullPath.string(), frameBufferW_, frameBufferH_, 30)) {
            sessionRecorder_.pushFrame(frameBufferBgra_.data(), frameBufferW_, frameBufferH_);
            showToast("Screen recording started");
        } else {
            showToast("Failed to start screen recording", true);
        }
    }
}

void CppDeskWindow::sendChatFromInput() {
    if (chatInput_.empty()) return;
    if (network_.sendChatMessage(chatInput_)) {
        chatInput_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        showToast("Connect to a remote desk first", true);
    }
}

void CppDeskWindow::sendTerminalFromInput() {
    if (terminalInputText_.empty()) return;
    network_.sendTerminalCommand(terminalInputText_);
    terminalInputText_.clear();
    terminalHistoryIndex_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CppDeskWindow::toggleFullscreen() {
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

void CppDeskWindow::toggleShortcutsModal() {
    showShortcutsModal_ = !showShortcutsModal_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CppDeskWindow::showToast(const std::string& message, bool isError) {
    toastText_ = message;
    toastIsError_ = isError;
    toastExpireTick_ = GetTickCount64() + 3500;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CppDeskWindow::restoreFromTray(NotificationType contextType) {
    ShowWindow(hwnd_, SW_SHOW);
    if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
    }
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    if (notificationMgr_) {
        notificationMgr_->stopFlash();
    }
    if (contextType == NotificationType::ChatMessage) {
        showFileDrawer_ = true;
        drawerTab_ = DrawerTab::LiveChat;
        network_.markChatRead();
        focusedField_ = FocusedField::ChatInput;
    } else if (contextType == NotificationType::FileTransferDone) {
        showFileDrawer_ = true;
        drawerTab_ = DrawerTab::FilesAndClip;
    } else if (contextType == NotificationType::IncomingConnection) {
        auto pending = network_.pendingIncomingRequest();
        if (pending.active) {
            SetForegroundWindow(hwnd_);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace cppdesk
