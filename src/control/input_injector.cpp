#include "input_injector.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>

namespace aerodesk {

namespace {

void ensureDesktopSynced() {
    static uint64_t lastSyncTick = 0;
    uint64_t now = GetTickCount64();
    if (now - lastSyncTick > 1000) {
        lastSyncTick = now;
        InputInjector::syncToInputDesktop();
    }
}

void mapNormalizedToVirtualDesktop(float normX, float normY, const MonitorDesc& mon, LONG& outDx, LONG& outDy) {
    normX = std::clamp(normX, 0.0f, 1.0f);
    normY = std::clamp(normY, 0.0f, 1.0f);

    double targetX = mon.x + normX * std::max(1, mon.width - 1);
    double targetY = mon.y + normY * std::max(1, mon.height - 1);

    int vLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth  = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    int vHeight = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));

    outDx = static_cast<LONG>(((targetX - vLeft) * 65535.0) / std::max(1, vWidth - 1));
    outDy = static_cast<LONG>(((targetY - vTop) * 65535.0) / std::max(1, vHeight - 1));
}

} // namespace

void InputInjector::injectMouseMove(float normX, float normY, const MonitorDesc& monitor) {
    ensureDesktopSynced();
    normX = std::clamp(normX, 0.0f, 1.0f);
    normY = std::clamp(normY, 0.0f, 1.0f);

    double targetX = monitor.x + normX * std::max(1, monitor.width - 1);
    double targetY = monitor.y + normY * std::max(1, monitor.height - 1);

    // Hardware cursor positioning (always succeeds even over elevated windows/Task Manager)
    SetCursorPos(static_cast<int>(std::round(targetX)), static_cast<int>(std::round(targetY)));

    LONG dx = 0, dy = 0;
    mapNormalizedToVirtualDesktop(normX, normY, monitor, dx, dy);

    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dx = dx;
    inp.mi.dy = dy;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
        syncToInputDesktop();
        if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
            mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, dx, dy, 0, 0);
        }
    }
}

void InputInjector::injectMouseButton(MouseButtonId button, bool isDown, float normX, float normY, const MonitorDesc& monitor) {
    ensureDesktopSynced();
    normX = std::clamp(normX, 0.0f, 1.0f);
    normY = std::clamp(normY, 0.0f, 1.0f);

    double targetX = monitor.x + normX * std::max(1, monitor.width - 1);
    double targetY = monitor.y + normY * std::max(1, monitor.height - 1);
    SetCursorPos(static_cast<int>(std::round(targetX)), static_cast<int>(std::round(targetY)));

    LONG dx = 0, dy = 0;
    mapNormalizedToVirtualDesktop(normX, normY, monitor, dx, dy);

    DWORD btnFlag = 0;
    switch (button) {
        case MouseButtonId::Left:
            btnFlag = isDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case MouseButtonId::Right:
            btnFlag = isDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButtonId::Middle:
            btnFlag = isDown ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
    }
    if (btnFlag == 0) return;

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = dx;
    inputs[0].mi.dy = dy;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dx = dx;
    inputs[1].mi.dy = dy;
    inputs[1].mi.dwFlags = btnFlag | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    if (SendInput(2, inputs, sizeof(INPUT)) == 0) {
        syncToInputDesktop();
        if (SendInput(2, inputs, sizeof(INPUT)) == 0) {
            mouse_event(inputs[0].mi.dwFlags, inputs[0].mi.dx, inputs[0].mi.dy, 0, 0);
            mouse_event(inputs[1].mi.dwFlags, inputs[1].mi.dx, inputs[1].mi.dy, 0, 0);
        }
    }
}

void InputInjector::injectMouseWheel(int32_t verticalDelta, int32_t horizontalDelta) {
    ensureDesktopSynced();
    if (verticalDelta != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.mouseData = static_cast<DWORD>(verticalDelta);
        inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
        if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(verticalDelta), 0);
        }
    }
    if (horizontalDelta != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.mouseData = static_cast<DWORD>(horizontalDelta);
        inp.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
            mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, static_cast<DWORD>(horizontalDelta), 0);
        }
    }
}

void InputInjector::injectKeyEvent(uint16_t vkCode, uint16_t scanCode, bool isDown, bool isExtended) {
    ensureDesktopSynced();
    if (scanCode == 0 && vkCode != 0) {
        scanCode = static_cast<uint16_t>(MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC));
    }

    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = vkCode;
    inp.ki.wScan = scanCode;
    inp.ki.dwFlags = 0;

    if (scanCode != 0) {
        inp.ki.dwFlags |= KEYEVENTF_SCANCODE;
    }
    if (isExtended) {
        inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (!isDown) {
        inp.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
        syncToInputDesktop();
        if (SendInput(1, &inp, sizeof(INPUT)) == 0) {
            keybd_event(static_cast<BYTE>(vkCode), static_cast<BYTE>(scanCode),
                        (isDown ? 0 : KEYEVENTF_KEYUP) | (isExtended ? KEYEVENTF_EXTENDEDKEY : 0), 0);
        }
    }
}

void InputInjector::releaseAllModifiers() {
    const uint16_t modVks[] = {
        VK_LSHIFT, VK_RSHIFT, VK_SHIFT,
        VK_LCONTROL, VK_RCONTROL, VK_CONTROL,
        VK_LMENU, VK_RMENU, VK_MENU,
        VK_LWIN, VK_RWIN
    };
    for (uint16_t vk : modVks) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            injectKeyEvent(vk, 0, false, (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN));
        }
    }
}

bool InputInjector::syncToInputDesktop() {
    HDESK hInputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hInputDesk) {
        hInputDesk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    }
    if (hInputDesk) {
        HDESK hCurDesk = GetThreadDesktop(GetCurrentThreadId());
        if (hCurDesk != hInputDesk) {
            SetThreadDesktop(hInputDesk);
        }
        if (GetThreadDesktop(GetCurrentThreadId()) != hInputDesk) {
            CloseDesktop(hInputDesk);
        }
        return true;
    }
    return false;
}

bool InputInjector::isElevated() {
    bool elevated = false;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation{};
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            elevated = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }
    return elevated;
}

bool InputInjector::relaunchAsAdmin(void* hwndParent) {
    wchar_t szPath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, szPath, MAX_PATH) == 0) {
        return false;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = szPath;
    sei.hwnd = reinterpret_cast<HWND>(hwndParent);
    sei.nShow = SW_NORMAL;

    return ShellExecuteExW(&sei) != FALSE;
}

bool InputInjector::executeSystemAction(SystemActionType action) {
    switch (action) {
        case SystemActionType::TaskManager:
            ShellExecuteA(nullptr, "open", "taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case SystemActionType::ShowDesktop:
            injectKeyEvent(VK_LWIN, 0, true, false);
            injectKeyEvent('D', 0, true, false);
            injectKeyEvent('D', 0, false, false);
            injectKeyEvent(VK_LWIN, 0, false, false);
            return true;

        case SystemActionType::LockWorkstation:
            return LockWorkStation() != FALSE;

        case SystemActionType::SendCtrlAltDel: {
            typedef VOID(WINAPI* SendSASFunc)(BOOL AsUser);
            HMODULE hSas = LoadLibraryA("sas.dll");
            bool sasSucceeded = false;
            if (hSas) {
                auto pFunc = GetProcAddress(hSas, "SendSAS");
                if (pFunc) {
                    auto pSendSAS = reinterpret_cast<SendSASFunc>(reinterpret_cast<void(*)()>(pFunc));
                    pSendSAS(FALSE);
                    sasSucceeded = true;
                }
                FreeLibrary(hSas);
            }
            if (!sasSucceeded) {
                // Fallback: launch Task Manager
                ShellExecuteA(nullptr, "open", "taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
            }
            return true;
        }

        case SystemActionType::EmergencyReboot: {
            HANDLE hToken = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
                TOKEN_PRIVILEGES tkp{};
                LookupPrivilegeValueA(nullptr, "SeShutdownPrivilege", &tkp.Privileges[0].Luid);
                tkp.PrivilegeCount = 1;
                tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, nullptr, nullptr);
                CloseHandle(hToken);
            }
            return ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED) != FALSE;
        }

        default:
            return false;
    }
}

} // namespace aerodesk
