#include "updater.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <sstream>
#include <vector>
#include <thread>
#include <cctype>

namespace cppdesk {

namespace {

std::wstring toWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 1) return L"";
    std::wstring out(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], len);
    return out;
}

} // namespace

bool AutoUpdater::parseVersionTriad(const std::string& verStr, int& major, int& minor, int& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    std::string s = verStr;
    size_t start = 0;
    while (start < s.size() && (std::isspace(static_cast<unsigned char>(s[start])) || s[start] == 'v' || s[start] == 'V')) {
        start++;
    }
    s = s.substr(start);
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0]))) {
        return false;
    }

    std::vector<int> parts;
    std::stringstream ss(s);
    std::string seg;
    while (std::getline(ss, seg, '.')) {
        try {
            size_t endDigit = 0;
            while (endDigit < seg.size() && std::isdigit(static_cast<unsigned char>(seg[endDigit]))) {
                endDigit++;
            }
            if (endDigit > 0) {
                parts.push_back(std::stoi(seg.substr(0, endDigit)));
            } else {
                parts.push_back(0);
            }
        } catch (...) {
            parts.push_back(0);
        }
    }
    if (parts.empty()) return false;
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    major = parts[0];
    minor = parts[1];
    patch = parts[2];
    return true;
}

bool AutoUpdater::isNewerVersion(const std::string& candidateVer, const std::string& baselineVer) {
    if (candidateVer.empty()) return false;
    int candMaj = 0, candMin = 0, candPat = 0;
    int baseMaj = 0, baseMin = 0, basePat = 0;
    if (!parseVersionTriad(candidateVer, candMaj, candMin, candPat)) return false;
    if (!parseVersionTriad(baselineVer, baseMaj, baseMin, basePat)) return false;

    if (candMaj > baseMaj) return true;
    if (candMaj < baseMaj) return false;
    if (candMin > baseMin) return true;
    if (candMin < baseMin) return false;
    return candPat > basePat;
}

std::string AutoUpdater::extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return "";

    size_t quoteStart = json.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) return "";

    std::string result;
    bool escape = false;
    for (size_t i = quoteStart + 1; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else if (c == '\\') result += '\\';
            else if (c == '"') result += '"';
            else if (c == '/') result += '/';
            else result += c;
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

UpdateInfo AutoUpdater::queryLatestReleaseSync(
    const std::string& repoOwner,
    const std::string& repoName,
    const std::string& currentVersion)
{
    UpdateInfo info;
    info.currentVersion = currentVersion;
    info.downloadUrl = "https://github.com/" + repoOwner + "/" + repoName + "/releases/latest";

    HINTERNET hSession = WinHttpOpen(
        L"CppDesk-Updater/3.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) {
        info.errorMessage = "Failed to initialize WinHTTP session";
        return info;
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"api.github.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        info.errorMessage = "Failed to connect to GitHub API";
        return info;
    }

    std::wstring path = L"/repos/" + toWide(repoOwner) + L"/" + toWide(repoName) + L"/releases/latest";

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        info.errorMessage = "Failed to open WinHTTP request";
        return info;
    }

    // Set standard GitHub API request headers
    LPCWSTR headers = L"User-Agent: CppDesk-Updater\r\nAccept: application/vnd.github.v3+json\r\n";
    BOOL bSend = WinHttpSendRequest(
        hRequest,
        headers,
        -1L,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (!bSend || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        info.errorMessage = "Failed to query GitHub release response";
        return info;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX
    );

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        info.errorMessage = "GitHub API returned HTTP " + std::to_string(statusCode);
        return info;
    }

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer.data(), bytesRead);
        } else {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    info.latestVersion = extractJsonString(responseBody, "tag_name");
    std::string releaseUrl = extractJsonString(responseBody, "html_url");
    if (!releaseUrl.empty()) {
        info.downloadUrl = releaseUrl;
        info.releaseUrl = releaseUrl;
    }
    info.releaseNotes = extractJsonString(responseBody, "body");

    if (!info.latestVersion.empty()) {
        info.checkSucceeded = true;
        info.success = true;
        info.updateRequired = isNewerVersion(info.latestVersion, currentVersion);
    } else {
        info.errorMessage = "No release tag found in GitHub API response";
    }

    return info;
}

void AutoUpdater::checkForUpdatesAsync(
    std::function<void(const UpdateInfo&)> onComplete,
    const std::string& repoOwner,
    const std::string& repoName,
    const std::string& currentVersion)
{
    std::thread([onComplete, repoOwner, repoName, currentVersion]() {
        UpdateInfo info = queryLatestReleaseSync(repoOwner, repoName, currentVersion);
        if (onComplete) {
            onComplete(info);
        }
    }).detach();
}

} // namespace cppdesk
