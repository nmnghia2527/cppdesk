#pragma once

#include "../core/protocol.hpp"

#include <string>
#include <functional>
#include <vector>

namespace cppdesk {

struct UpdateInfo {
    bool checkSucceeded = false;
    bool success = false; // Compatibility alias
    bool updateRequired = false;
    std::string currentVersion = CPP_DESK_VERSION;
    std::string latestVersion;
    std::string downloadUrl = "https://github.com/nmnghia2527/cppdesk/releases/latest";
    std::string releaseUrl = "https://github.com/nmnghia2527/cppdesk/releases/latest"; // Compatibility alias
    std::string releaseNotes;
    std::string errorMessage;
};

class AutoUpdater {
public:
    // Parses a semantic version string (e.g. "3.0.0", "v3.1.2") into major, minor, patch
    static bool parseVersionTriad(const std::string& verStr, int& major, int& minor, int& patch);

    // Returns true if candidateVer > baselineVer according to semantic versioning
    static bool isNewerVersion(const std::string& candidateVer, const std::string& baselineVer);

    // Synchronous query using WinHTTP hitting GitHub Releases API
    static UpdateInfo queryLatestReleaseSync(
        const std::string& repoOwner = "nmnghia2527",
        const std::string& repoName = "cppdesk",
        const std::string& currentVersion = CPP_DESK_VERSION);

    // Asynchronous background query (calls onComplete on background thread)
    static void checkForUpdatesAsync(
        std::function<void(const UpdateInfo&)> onComplete,
        const std::string& repoOwner = "nmnghia2527",
        const std::string& repoName = "cppdesk",
        const std::string& currentVersion = CPP_DESK_VERSION);

    // Overload accepting (repoOwner, repoName, currentVersion, onComplete)
    static void checkForUpdatesAsync(
        const std::string& repoOwner,
        const std::string& repoName,
        const std::string& currentVersion,
        std::function<void(const UpdateInfo&)> onComplete) {
        checkForUpdatesAsync(std::move(onComplete), repoOwner, repoName, currentVersion);
    }

    // Simple JSON value extractor helper
    static std::string extractJsonString(const std::string& json, const std::string& key);
};

} // namespace cppdesk
