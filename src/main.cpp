#include "core/protocol.hpp"
#include "core/crypto_identity.hpp"
#include "net/network_engine.hpp"
#include "ui/ui_window.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <string>
#include <vector>
#include <cstdio>
#include <chrono>
#include <thread>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int instanceId = 1;
    bool headlessRelayOnly = false;
    uint16_t relayPort = cppdesk::DEFAULT_RELAY_PORT;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--instance" && i + 1 < argc) {
                try {
                    instanceId = std::stoi(argv[++i]);
                } catch (...) {}
            } else if (arg == L"--relay-server") {
                headlessRelayOnly = true;
                if (i + 1 < argc) {
                    try {
                        int p = std::stoi(argv[i + 1]);
                        if (p > 0 && p <= 65535) {
                            relayPort = static_cast<uint16_t>(p);
                            ++i;
                        }
                    } catch (...) {}
                }
            }
        }
        LocalFree(argv);
    }

    if (headlessRelayOnly) {
        cppdesk::RelayServer server;
        if (!server.start(relayPort)) {
            return 1;
        }
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }

    // Enforce single-instance per instanceId to prevent zombie instances and duplicate tray icons
    std::wstring mutexName = L"Local\\CppDesk_SingleInstance_Mutex_" + std::to_wstring(instanceId);
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingHwnd = FindWindowW(L"CppDeskMainWindowClass", nullptr);
        if (existingHwnd) {
            if (IsIconic(existingHwnd)) {
                ShowWindow(existingHwnd, SW_RESTORE);
            } else {
                ShowWindow(existingHwnd, SW_SHOW);
            }
            SetForegroundWindow(existingHwnd);
        }
        CloseHandle(hMutex);
        CoUninitialize();
        return 0;
    }

    cppdesk::IdentityManager identity(instanceId);
    identity.loadOrCreate();

    cppdesk::NetworkEngine network(identity);
    network.start();

    cppdesk::CppDeskWindow window(identity, network);
    if (!window.create(hInstance, nCmdShow)) {
        network.stop();
        if (hMutex) CloseHandle(hMutex);
        CoUninitialize();
        return 1;
    }

    int exitCode = window.messageLoop();
    network.stop();
    if (hMutex) CloseHandle(hMutex);
    CoUninitialize();

    // Absolute zero background residue: terminate all process threads immediately
    ExitProcess(static_cast<UINT>(exitCode));
    return exitCode;
}
