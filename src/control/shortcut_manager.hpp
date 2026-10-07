#pragma once

#include <string>

namespace cppdesk {

class ShortcutManager {
public:
    // Creates a Windows .lnk shortcut on the current user's desktop
    static bool createDesktopShortcut(const std::string& deskId, const std::string& customAlias = "");

    // Check if cppdesk:// URL protocol is registered in HKCU
    static bool isUriProtocolRegistered();

    // Register or unregister cppdesk:// URL protocol in HKCU
    static bool setUriProtocolRegistered(bool enable);

    // Parses a raw command line string or URI into a clean 9-digit desk ID if present
    static std::string parseStartupConnectTarget(const std::wstring& cmdLine);

    // Sanitizes desk ID string (removing spaces, non-digits)
    static std::string sanitizeDeskId(const std::string& input);
};

} // namespace cppdesk
