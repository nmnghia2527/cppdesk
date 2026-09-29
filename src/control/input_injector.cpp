#include "input_injector.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>

namespace aerodesk {

namespace {

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
    LONG dx = 0, dy = 0;
    mapNormalizedToVirtualDesktop(normX, normY, monitor, dx, dy);

    INPUT inp{};
    inp.type = INPUT_MOUSE;
    inp.mi.dx = dx;
    inp.mi.dy = dy;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputInjector::injectMouseButton(MouseButtonId button, bool isDown, float normX, float normY, const MonitorDesc& monitor) {
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

    SendInput(2, inputs, sizeof(INPUT));
}

void InputInjector::injectMouseWheel(int32_t verticalDelta, int32_t horizontalDelta) {
    if (verticalDelta != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.mouseData = static_cast<DWORD>(verticalDelta);
        inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
        SendInput(1, &inp, sizeof(INPUT));
    }
    if (horizontalDelta != 0) {
        INPUT inp{};
        inp.type = INPUT_MOUSE;
        inp.mi.mouseData = static_cast<DWORD>(horizontalDelta);
        inp.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        SendInput(1, &inp, sizeof(INPUT));
    }
}

void InputInjector::injectKeyEvent(uint16_t vkCode, uint16_t scanCode, bool isDown, bool isExtended) {
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

    SendInput(1, &inp, sizeof(INPUT));
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

} // namespace aerodesk
