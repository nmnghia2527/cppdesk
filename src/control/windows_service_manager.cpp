#include "windows_service_manager.hpp"
#include "../core/crypto_identity.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#include <shlobj.h>
#include <bcrypt.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace cppdesk {

namespace {

static constexpr uint32_t REBOOT_TOKEN_MAGIC = 0x43505052; // "CPPR"

static SERVICE_STATUS        g_svcStatus{};
static SERVICE_STATUS_HANDLE g_svcStatusHandle = nullptr;
static HANDLE                g_svcStopEvent = nullptr;

static VOID WINAPI ServiceCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        g_svcStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_svcStatusHandle, &g_svcStatus);
        if (g_svcStopEvent) {
            SetEvent(g_svcStopEvent);
        }
        break;
    default:
        break;
    }
}

static VOID WINAPI ServiceMain(DWORD dwArgc, LPWSTR* lpszArgv) {
    (void)dwArgc;
    (void)lpszArgv;

    g_svcStatusHandle = RegisterServiceCtrlHandlerW(WindowsServiceManager::SERVICE_NAME, ServiceCtrlHandler);
    if (!g_svcStatusHandle) return;

    g_svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svcStatus.dwCurrentState = SERVICE_START_PENDING;
    g_svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    g_svcStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_svcStopEvent) {
        g_svcStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_svcStatusHandle, &g_svcStatus);
        return;
    }

    g_svcStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    // Keep service running until stop event is signaled
    WaitForSingleObject(g_svcStopEvent, INFINITE);

    g_svcStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_svcStatusHandle, &g_svcStatus);

    if (g_svcStopEvent) {
        CloseHandle(g_svcStopEvent);
        g_svcStopEvent = nullptr;
    }
}

} // namespace

// ---------------- Service Management ----------------

bool WindowsServiceManager::isServiceInstalled() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
    bool installed = (hSvc != nullptr);
    if (hSvc) CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return installed;
}

bool WindowsServiceManager::isServiceRunning() {
    return getServiceState() == ServiceStatusState::Running;
}

ServiceStatusState WindowsServiceManager::getServiceState() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return ServiceStatusState::NotInstalled;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return ServiceStatusState::NotInstalled;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;
    ServiceStatusState state = ServiceStatusState::Stopped;

    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            state = ServiceStatusState::Running;
        } else if (ssp.dwCurrentState == SERVICE_START_PENDING || ssp.dwCurrentState == SERVICE_STOP_PENDING) {
            state = ServiceStatusState::Pending;
        } else {
            state = ServiceStatusState::Stopped;
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return state;
}

bool WindowsServiceManager::installService(const std::wstring& customExePath) {
    wchar_t modPath[MAX_PATH] = {};
    if (customExePath.empty()) {
        GetModuleFileNameW(nullptr, modPath, MAX_PATH);
    } else {
        wcsncpy(modPath, customExePath.c_str(), MAX_PATH - 1);
        modPath[MAX_PATH - 1] = L'\0';
    }

    std::wstring binPath = L"\"" + std::wstring(modPath) + L"\" --service-run";

    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) return false;

    SC_HANDLE hSvc = CreateServiceW(
        hSCM,
        SERVICE_NAME,
        DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return true;
}

bool WindowsServiceManager::uninstallService() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, DELETE | SERVICE_STOP);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS ss{};
    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
    bool ok = (DeleteService(hSvc) != 0);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok;
}

bool WindowsServiceManager::startService() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_START);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    bool ok = (StartServiceW(hSvc, 0, nullptr) != 0);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok;
}

bool WindowsServiceManager::stopService() {
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, SERVICE_NAME, SERVICE_STOP);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS ss{};
    bool ok = (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss) != 0);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return ok;
}

int WindowsServiceManager::runServiceDispatcher() {
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };
    return StartServiceCtrlDispatcherW(serviceTable) ? 0 : 1;
}

// ---------------- Reboot Token Management ----------------

std::string WindowsServiceManager::getRebootTokenFilePath() {
    char commonAppDir[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, commonAppDir))) {
        std::filesystem::path dir = std::filesystem::path(commonAppDir) / "CppDesk";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / "reboot_token.bin").string();
    }
    char tempDir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    return (std::filesystem::path(tempDir) / "cppdesk_reboot_token.bin").string();
}

std::string WindowsServiceManager::generateRebootTokenHex() {
    std::array<uint8_t, 32> randBytes{};
    BCryptGenRandom(nullptr, randBytes.data(), static_cast<ULONG>(randBytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return CryptoUtils::toHex(randBytes.data(), randBytes.size());
}

bool WindowsServiceManager::saveRebootToken(const std::string& tokenHex, uint64_t callerDeskId, uint32_t validSeconds) {
    if (tokenHex.empty()) return false;

    std::string path = getRebootTokenFilePath();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    uint32_t magic = REBOOT_TOKEN_MAGIC;
    uint32_t tokenLen = static_cast<uint32_t>(tokenHex.size());
    uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    uint64_t expireMs = nowMs + static_cast<uint64_t>(validSeconds) * 1000ULL;

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&tokenLen), sizeof(tokenLen));
    out.write(tokenHex.data(), tokenLen);
    out.write(reinterpret_cast<const char*>(&callerDeskId), sizeof(callerDeskId));
    out.write(reinterpret_cast<const char*>(&expireMs), sizeof(expireMs));
    out.flush();

    return out.good();
}

bool WindowsServiceManager::validateAndConsumeRebootToken(const std::string& tokenHex, uint64_t callerDeskId) {
    if (tokenHex.empty()) return false;

    std::string path = getRebootTokenFilePath();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0;
    uint32_t tokenLen = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&tokenLen), sizeof(tokenLen));
    if (magic != REBOOT_TOKEN_MAGIC || tokenLen == 0 || tokenLen > 256) {
        in.close();
        clearRebootToken();
        return false;
    }

    std::string storedToken(tokenLen, '\0');
    in.read(storedToken.data(), tokenLen);
    uint64_t storedDeskId = 0;
    uint64_t expireMs = 0;
    in.read(reinterpret_cast<char*>(&storedDeskId), sizeof(storedDeskId));
    in.read(reinterpret_cast<char*>(&expireMs), sizeof(expireMs));
    in.close();

    uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    if (nowMs > expireMs) {
        clearRebootToken();
        return false; // Expired
    }
    if (storedDeskId != callerDeskId) {
        return false; // Wrong caller
    }
    if (storedToken != tokenHex) {
        return false; // Wrong token
    }

    // Successfully validated: consume (delete) token for one-time use
    clearRebootToken();
    return true;
}

bool WindowsServiceManager::hasPendingRebootToken(uint64_t* outCallerDeskId) {
    std::string path = getRebootTokenFilePath();
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0;
    uint32_t tokenLen = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&tokenLen), sizeof(tokenLen));
    if (magic != REBOOT_TOKEN_MAGIC || tokenLen == 0 || tokenLen > 256) {
        in.close();
        return false;
    }

    std::string storedToken(tokenLen, '\0');
    in.read(storedToken.data(), tokenLen);
    uint64_t storedDeskId = 0;
    uint64_t expireMs = 0;
    in.read(reinterpret_cast<char*>(&storedDeskId), sizeof(storedDeskId));
    in.read(reinterpret_cast<char*>(&expireMs), sizeof(expireMs));

    uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    if (nowMs > expireMs) {
        in.close();
        return false;
    }
    in.close();
    if (outCallerDeskId) {
        *outCallerDeskId = storedDeskId;
    }
    return true;
}

void WindowsServiceManager::clearRebootToken() {
    std::string path = getRebootTokenFilePath();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool WindowsServiceManager::initiateHostReboot(uint32_t countdownSeconds, bool safeMode) {
    // Enable SE_SHUTDOWN_NAME privilege
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp{};
        LookupPrivilegeValueA(nullptr, "SeShutdownPrivilege", &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(hToken);
    }

    if (safeMode) {
        // Configure safeboot network via bcdedit command
        WinExec("bcdedit /set {current} safeboot network", SW_HIDE);
    }

    char msg[128];
    std::snprintf(msg, sizeof(msg), "CppDesk Remote Administration Reboot (Resuming in %u seconds)", countdownSeconds);

    BOOL res = InitiateSystemShutdownExA(
        nullptr,
        msg,
        countdownSeconds,
        TRUE,  // Force apps closed
        TRUE,  // Reboot after shutdown
        SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG | SHTDN_REASON_FLAG_PLANNED
    );

    if (!res) {
        // Fallback to ExitWindowsEx
        res = ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG,
                            SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG);
    }
    return (res != FALSE);
}

} // namespace cppdesk
