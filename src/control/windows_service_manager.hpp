#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppdesk {

enum class ServiceStatusState : uint8_t {
    NotInstalled = 0,
    Stopped      = 1,
    Running      = 2,
    Pending      = 3
};

class WindowsServiceManager {
public:
    static constexpr const wchar_t* SERVICE_NAME = L"CppDeskService";
    static constexpr const wchar_t* DISPLAY_NAME = L"CppDesk Remote Desktop Service";

    // Service Control Manager lifecycle
    static bool isServiceInstalled();
    static bool isServiceRunning();
    static ServiceStatusState getServiceState();

    static bool installService(const std::wstring& customExePath = L"");
    static bool uninstallService();
    static bool startService();
    static bool stopService();
    static int  runServiceDispatcher();

    // Remote reboot token persistence & validation
    static std::string generateRebootTokenHex();
    static bool saveRebootToken(const std::string& tokenHex, uint64_t callerDeskId, uint32_t validSeconds = 600);
    static bool validateAndConsumeRebootToken(const std::string& tokenHex, uint64_t callerDeskId);
    static bool hasPendingRebootToken(uint64_t* outCallerDeskId = nullptr);
    static void clearRebootToken();

    // System reboot execution
    static bool initiateHostReboot(uint32_t countdownSeconds = 5, bool safeMode = false);

private:
    static std::string getRebootTokenFilePath();
};

} // namespace cppdesk
