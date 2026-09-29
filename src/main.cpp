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
    uint16_t relayPort = aerodesk::DEFAULT_RELAY_PORT;

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
        aerodesk::RelayServer server;
        if (!server.start(relayPort)) {
            return 1;
        }
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return 0;
    }

    aerodesk::IdentityManager identity(instanceId);
    identity.loadOrCreate();

    aerodesk::NetworkEngine network(identity);
    network.start();

    aerodesk::AeroDeskWindow window(identity, network);
    if (!window.create(hInstance, nCmdShow)) {
        network.stop();
        CoUninitialize();
        return 1;
    }

    int exitCode = window.messageLoop();
    network.stop();
    CoUninitialize();
    return exitCode;
}
