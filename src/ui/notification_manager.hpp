#pragma once

#include "../core/crypto_identity.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <memory>
#include <atomic>

namespace aerodesk {

enum class NotificationType : uint8_t {
    IncomingConnection = 0,
    ChatMessage        = 1,
    FileTransferDone   = 2,
    SessionDropped     = 3,
    GeneralInfo        = 4
};

static constexpr UINT WM_TRAYICON        = WM_USER + 101;
static constexpr UINT IDM_TRAY_RESTORE   = 2001;
static constexpr UINT IDM_TRAY_STATUS    = 2002;
static constexpr UINT IDM_TRAY_MUTE      = 2003;
static constexpr UINT IDM_TRAY_EXIT      = 2004;

class NotificationManager {
public:
    NotificationManager();
    ~NotificationManager();

    bool init(HWND hwnd, HINSTANCE hInstance, const std::string& appTitle);
    void shutdown();

    void notify(NotificationType type,
                const std::string& title,
                const std::string& message,
                const AppSettings& settings);

    void flashTaskbar(bool flash);
    void stopFlash();
    bool isFlashing() const { return isFlashing_.load(); }

    void setMuted(bool muted) { muted_.store(muted); }
    bool isMuted() const { return muted_.load(); }

    void showContextMenu(int x, int y, const std::string& statusText);

    NotificationType lastNotificationType() const { return lastType_.load(); }
    void clearLastNotificationType() { lastType_.store(NotificationType::GeneralInfo); }

private:
    HWND                    hwnd_ = nullptr;
    HINSTANCE               hInstance_ = nullptr;
    HICON                   hTrayIcon_ = nullptr;
    bool                    trayAdded_ = false;
    std::atomic<bool>       isFlashing_{false};
    std::atomic<bool>       muted_{false};
    std::atomic<NotificationType> lastType_{NotificationType::GeneralInfo};
    std::string             appTitle_ = "AeroDesk";
};

} // namespace aerodesk
