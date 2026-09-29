#include "crypto_identity.hpp"
#include "../simd/simd_kernels.hpp"
#include "protocol.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace aerodesk {

namespace {

std::string generateRandomSixCharCode() {
    const char* chars = "23456789abcdefghjkmnpqrstuvwxyz";
    uint8_t rnd[6] = {};
    CryptoUtils::randomBytes(rnd, sizeof(rnd));
    std::string code;
    code.reserve(6);
    for (int i = 0; i < 6; ++i) {
        code.push_back(chars[rnd[i] % 31]);
    }
    return code;
}

bool constantTimeEquals32(const std::array<uint8_t, 32>& a, const std::array<uint8_t, 32>& b) {
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; ++i) {
        diff |= (a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace

bool CryptoUtils::randomBytes(void* buffer, size_t len) {
    if (len == 0) return true;
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>(buffer),
        static_cast<ULONG>(len),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    return status >= 0;
}

uint64_t CryptoUtils::generateNineDigitId() {
    uint64_t rnd = 0;
    randomBytes(&rnd, sizeof(rnd));
    return 100000000ULL + (rnd % 900000000ULL);
}

std::array<uint8_t, 32> CryptoUtils::sha256(const void* data, size_t len) {
    std::array<uint8_t, 32> digest{};
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
            if (len > 0 && data != nullptr) {
                BCryptHashData(
                    hHash,
                    const_cast<PUCHAR>(static_cast<const uint8_t*>(data)),
                    static_cast<ULONG>(len),
                    0
                );
            }
            BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return digest;
}

std::array<uint8_t, 32> CryptoUtils::sha256(const std::string& text) {
    return sha256(text.data(), text.size());
}

std::string CryptoUtils::toHex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string CryptoUtils::sha256Hex(const std::string& text) {
    auto d = sha256(text);
    return toHex(d.data(), d.size());
}

std::string CryptoUtils::hashPassword(const std::string& password, const std::string& salt) {
    std::string combined = "AeroDesk-v1:" + salt + ":" + password;
    return sha256Hex(combined);
}

std::array<uint8_t, 32> CryptoUtils::computeChallengeResponseFromToken(
    const std::string& passwordVerifierToken,
    uint64_t hostId,
    uint64_t clientId,
    const std::array<uint8_t, 32>& nonce)
{
    ByteWriter w;
    w.writeString(passwordVerifierToken);
    w.writeU64(hostId);
    w.writeU64(clientId);
    w.writeBytes(nonce.data(), nonce.size());
    return sha256(w.buffer().data(), w.buffer().size());
}

std::array<uint8_t, 32> CryptoUtils::computeChallengeResponse(
    const std::string& passwordPlain,
    uint64_t hostId,
    uint64_t clientId,
    const std::array<uint8_t, 32>& nonce)
{
    std::string passToken = hashPassword(passwordPlain, std::to_string(hostId));
    return computeChallengeResponseFromToken(passToken, hostId, clientId, nonce);
}

std::array<uint8_t, 32> CryptoUtils::deriveSessionKey(
    uint64_t hostId,
    uint64_t clientId,
    const std::array<uint8_t, 32>& nonce)
{
    ByteWriter w;
    w.writeString("AeroDesk-SessionKey-v1");
    w.writeU64(hostId);
    w.writeU64(clientId);
    w.writeBytes(nonce.data(), nonce.size());
    return sha256(w.buffer().data(), w.buffer().size());
}

std::string CryptoUtils::sessionFingerprintHex(const std::array<uint8_t, 32>& sessionKey) {
    auto digest = sha256(sessionKey.data(), sessionKey.size());
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02X%02X-%02X%02X",
                  digest[0], digest[1], digest[2], digest[3]);
    return std::string(buf);
}

void CryptoUtils::transformPayload(
    uint8_t* data,
    size_t len,
    const std::array<uint8_t, 32>& sessionKey,
    uint64_t frameSequence)
{
    if (!data || len == 0) return;

    // Fast 64-bit SplitMix64 CTR stream cipher seeded from 256-bit sessionKey + frameSequence
    uint64_t k0 = 0, k1 = 0, k2 = 0, k3 = 0;
    std::memcpy(&k0, sessionKey.data() + 0, 8);
    std::memcpy(&k1, sessionKey.data() + 8, 8);
    std::memcpy(&k2, sessionKey.data() + 16, 8);
    std::memcpy(&k3, sessionKey.data() + 24, 8);

    uint64_t state = k0 ^ (k1 + 0x9E3779B97F4A7C15ULL) ^ (frameSequence * 0xBF58476D1CE4E5B9ULL);
    auto nextWord = [&]() -> uint64_t {
        state += 0x9E3779B97F4A7C15ULL;
        uint64_t z = (state ^ k2);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27) ^ k3) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };

    size_t i = 0;
    if (SimdKernels::hasAvx2() && len >= 32) {
        alignas(32) uint64_t ksBatch[32]; // Up to 8 x 32-byte (256-bit) AVX2 blocks per call
        while (i + 32 <= len) {
            size_t blocks = std::min<size_t>((len - i) / 32, 8);
            size_t words = blocks * 4;
            for (size_t w = 0; w < words; ++w) {
                ksBatch[w] = nextWord();
            }
            aerodesk_avx2_xor_blocks32(data + i, reinterpret_cast<const uint8_t*>(ksBatch), blocks);
            i += blocks * 32;
        }
    }

    while (i + 8 <= len) {
        uint64_t ks = nextWord();
        uint64_t chunk = 0;
        std::memcpy(&chunk, data + i, 8);
        chunk ^= ks;
        std::memcpy(data + i, &chunk, 8);
        i += 8;
    }
    if (i < len) {
        uint64_t ks = nextWord();
        const uint8_t* ksBytes = reinterpret_cast<const uint8_t*>(&ks);
        for (size_t j = 0; i < len; ++i, ++j) {
            data[i] ^= ksBytes[j];
        }
    }
}

std::string CryptoUtils::formatDeskId(uint64_t id) {
    if (id < 100000000ULL || id > 999999999ULL) {
        return std::to_string(id);
    }
    char buf[32];
    uint32_t p1 = static_cast<uint32_t>(id / 1000000ULL);
    uint32_t p2 = static_cast<uint32_t>((id / 1000ULL) % 1000ULL);
    uint32_t p3 = static_cast<uint32_t>(id % 1000ULL);
    std::snprintf(buf, sizeof(buf), "%03u %03u %03u", p1, p2, p3);
    return std::string(buf);
}

uint64_t CryptoUtils::parseDeskId(const std::string& input) {
    if (input.find('.') != std::string::npos || input.find(':') != std::string::npos) {
        return 0;
    }
    std::string digits;
    digits.reserve(input.size());
    for (char c : input) {
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
        } else if (c != ' ' && c != '-' && c != '\t') {
            return 0;
        }
    }
    if (digits.size() != 9) return 0;
    try {
        uint64_t val = std::stoull(digits);
        if (val >= 100000000ULL && val <= 999999999ULL) {
            return val;
        }
    } catch (...) {}
    return 0;
}

// ---------------- IdentityManager ----------------

IdentityManager::IdentityManager(int instanceId)
    : instanceId_(std::max(1, instanceId))
{
    listenPort_ = static_cast<uint16_t>(DEFAULT_HOST_PORT + (instanceId_ - 1));
    sessionCode_ = generateRandomSixCharCode();

    char compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD sz = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(compName, &sz)) {
        hostname_ = compName;
    } else {
        hostname_ = "Windows-PC";
    }
    if (instanceId_ > 1) {
        hostname_ += " #" + std::to_string(instanceId_);
    }

    char appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::filesystem::path dir = std::filesystem::path(appData) / "AeroDesk";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        configPath_ = (dir / ("config_" + std::to_string(instanceId_) + ".ini")).string();
    } else {
        configPath_ = "aerodesk_" + std::to_string(instanceId_) + ".ini";
    }
}

bool IdentityManager::loadOrCreate() {
    std::string legacyPlainPassword;
    std::ifstream in(configPath_);
    if (in.is_open()) {
        std::string line;
        recentSessions_.clear();
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            if (key == "desk_id") {
                try { deskId_ = std::stoull(val); } catch (...) {}
            } else if (key == "listen_port") {
                try { listenPort_ = static_cast<uint16_t>(std::stoi(val)); } catch (...) {}
            } else if (key == "unattended_enabled") {
                unattendedEnabled_ = (val == "1" || val == "true");
            } else if (key == "unattended_verifier") {
                if (val.size() == 64) unattendedVerifier_ = val;
            } else if (key == "unattended_password") {
                legacyPlainPassword = val;
            } else if (key == "relay_server") {
                if (!val.empty()) relayServerAddr_ = val;
            } else if (key == "dark_theme") {
                settings_.darkTheme = (val == "1" || val == "true");
            } else if (key == "target_fps") {
                try { settings_.targetFps = clampTargetFps(static_cast<uint8_t>(std::stoi(val))); } catch (...) {}
            } else if (key == "adaptive_fps") {
                settings_.adaptiveFps = (val == "1" || val == "true");
            } else if (key == "default_quality") {
                try {
                    int q = std::clamp(std::stoi(val), 0, 2);
                    settings_.defaultQuality = static_cast<QualityPreset>(q);
                } catch (...) {}
            } else if (key == "default_scale") {
                try {
                    settings_.defaultScaleMode = static_cast<uint8_t>(std::clamp(std::stoi(val), 0, 2));
                } catch (...) {}
            } else if (key == "show_remote_cursor") {
                settings_.showRemoteCursor = (val == "1" || val == "true");
            } else if (key == "show_session_hud") {
                settings_.showSessionHud = (val == "1" || val == "true");
            } else if (key == "auto_accept") {
                settings_.autoAcceptIncoming = (val == "1" || val == "true");
            } else if (key == "default_perms") {
                try {
                    settings_.defaultPermissions = static_cast<uint8_t>(std::stoi(val) & PERM_ALL);
                } catch (...) {}
            } else if (key == "lock_on_disconnect") {
                settings_.lockWorkstationOnDisconnect = (val == "1" || val == "true");
            } else if (key == "recent") {
                // Format: deskId|hostname|address or deskId|hostname|address|fav
                auto p1 = val.find('|');
                auto p2 = (p1 != std::string::npos) ? val.find('|', p1 + 1) : std::string::npos;
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    auto p3 = val.find('|', p2 + 1);
                    RecentSessionEntry entry;
                    try { entry.deskId = std::stoull(val.substr(0, p1)); } catch (...) {}
                    entry.hostname = val.substr(p1 + 1, p2 - p1 - 1);
                    if (p3 != std::string::npos) {
                        entry.address = val.substr(p2 + 1, p3 - p2 - 1);
                        std::string favStr = val.substr(p3 + 1);
                        entry.isFavorite = (favStr == "1" || favStr == "true");
                    } else {
                        entry.address = val.substr(p2 + 1);
                    }
                    if (entry.deskId > 0 || !entry.address.empty()) {
                        recentSessions_.push_back(entry);
                    }
                }
            }
        }
        in.close();
    }

    settings_.targetFps = clampTargetFps(settings_.targetFps);

    bool modified = false;
    if (deskId_ < 100000000ULL || deskId_ > 999999999ULL) {
        deskId_ = CryptoUtils::generateNineDigitId();
        modified = true;
    }

    if (sessionCode_.empty()) {
        sessionCode_ = generateRandomSixCharCode();
    }

    // Transparently migrate legacy plaintext password to salted SHA-256 verifier
    if (!legacyPlainPassword.empty() && unattendedVerifier_.empty()) {
        unattendedVerifier_ = CryptoUtils::hashPassword(legacyPlainPassword, std::to_string(deskId_));
        modified = true;
    }

    if (unattendedVerifier_.empty()) {
        unattendedVerifier_ = CryptoUtils::hashPassword(sessionCode_, std::to_string(deskId_));
        modified = true;
    }

    if (modified) {
        save();
    }
    return true;
}

bool IdentityManager::save() const {
    std::ofstream out(configPath_, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# AeroDesk Configuration (Salted SHA-256 Verifier Protected)\n";
    out << "desk_id=" << deskId_ << "\n";
    out << "listen_port=" << listenPort_ << "\n";
    out << "unattended_enabled=" << (unattendedEnabled_ ? "1" : "0") << "\n";
    out << "unattended_verifier=" << unattendedVerifier_ << "\n";
    out << "relay_server=" << relayServerAddr_ << "\n";
    out << "dark_theme=" << (settings_.darkTheme ? "1" : "0") << "\n";
    out << "target_fps=" << static_cast<int>(clampTargetFps(settings_.targetFps)) << "\n";
    out << "adaptive_fps=" << (settings_.adaptiveFps ? "1" : "0") << "\n";
    out << "default_quality=" << static_cast<int>(settings_.defaultQuality) << "\n";
    out << "default_scale=" << static_cast<int>(settings_.defaultScaleMode) << "\n";
    out << "show_remote_cursor=" << (settings_.showRemoteCursor ? "1" : "0") << "\n";
    out << "show_session_hud=" << (settings_.showSessionHud ? "1" : "0") << "\n";
    out << "auto_accept=" << (settings_.autoAcceptIncoming ? "1" : "0") << "\n";
    out << "default_perms=" << static_cast<int>(settings_.defaultPermissions & PERM_ALL) << "\n";
    out << "lock_on_disconnect=" << (settings_.lockWorkstationOnDisconnect ? "1" : "0") << "\n";
    for (const auto& r : recentSessions_) {
        out << "recent=" << r.deskId << "|" << r.hostname << "|" << r.address << "|" << (r.isFavorite ? "1" : "0") << "\n";
    }
    return true;
}

void IdentityManager::setUnattendedEnabled(bool enabled) {
    unattendedEnabled_ = enabled;
    save();
}

void IdentityManager::setUnattendedPassword(const std::string& newPassword) {
    if (newPassword.empty()) return;
    unattendedPassword_ = newPassword;
    unattendedVerifier_ = CryptoUtils::hashPassword(newPassword, std::to_string(deskId_));
    save();
}

std::string IdentityManager::regenerateSessionCode() {
    sessionCode_ = generateRandomSixCharCode();
    if (unattendedPassword_.empty()) {
        unattendedVerifier_ = CryptoUtils::hashPassword(sessionCode_, std::to_string(deskId_));
        save();
    }
    return sessionCode_;
}

bool IdentityManager::verifyChallengeResponse(
    uint64_t clientId,
    const std::array<uint8_t, 32>& nonce,
    const std::array<uint8_t, 32>& clientDigest) const
{
    if (!unattendedEnabled_) {
        return false;
    }

    // 1. Check against stored salted SHA-256 unattended password verifier
    if (!unattendedVerifier_.empty()) {
        auto expected = CryptoUtils::computeChallengeResponseFromToken(
            unattendedVerifier_, deskId_, clientId, nonce
        );
        if (constantTimeEquals32(expected, clientDigest)) {
            return true;
        }
    }

    // 2. Check against active One-Time Session Code
    if (!sessionCode_.empty()) {
        auto expectedCode = CryptoUtils::computeChallengeResponse(
            sessionCode_, deskId_, clientId, nonce
        );
        if (constantTimeEquals32(expectedCode, clientDigest)) {
            return true;
        }
    }

    return false;
}

void IdentityManager::setRelayServerAddress(const std::string& addr) {
    relayServerAddr_ = addr;
    save();
}

void IdentityManager::addOrUpdateRecentSession(uint64_t id, const std::string& host, const std::string& addr) {
    bool wasFav = false;
    recentSessions_.erase(
        std::remove_if(recentSessions_.begin(), recentSessions_.end(), [&](const RecentSessionEntry& e) {
            bool match = (id > 0 && e.deskId == id) || (!addr.empty() && e.address == addr);
            if (match && e.isFavorite) wasFav = true;
            return match;
        }),
        recentSessions_.end()
    );
    RecentSessionEntry entry;
    entry.deskId = id;
    entry.hostname = host.empty() ? CryptoUtils::formatDeskId(id) : host;
    entry.address = addr;
    entry.isFavorite = wasFav;
    recentSessions_.insert(recentSessions_.begin(), entry);

    std::stable_sort(recentSessions_.begin(), recentSessions_.end(), [](const RecentSessionEntry& a, const RecentSessionEntry& b) {
        return a.isFavorite && !b.isFavorite;
    });

    if (recentSessions_.size() > 10) {
        recentSessions_.resize(10);
    }
    save();
}

void IdentityManager::toggleFavoriteSession(uint64_t id) {
    for (auto& e : recentSessions_) {
        if (e.deskId == id) {
            e.isFavorite = !e.isFavorite;
            break;
        }
    }
    std::stable_sort(recentSessions_.begin(), recentSessions_.end(), [](const RecentSessionEntry& a, const RecentSessionEntry& b) {
        return a.isFavorite && !b.isFavorite;
    });
    save();
}

void IdentityManager::removeRecentSession(uint64_t id) {
    recentSessions_.erase(
        std::remove_if(recentSessions_.begin(), recentSessions_.end(), [&](const RecentSessionEntry& e) {
            return e.deskId == id;
        }),
        recentSessions_.end()
    );
    save();
}

void IdentityManager::clearRecentSessions() {
    recentSessions_.clear();
    save();
}

void IdentityManager::updateSettings(const AppSettings& newSettings) {
    settings_ = newSettings;
    settings_.targetFps = clampTargetFps(settings_.targetFps);
    settings_.defaultPermissions &= PERM_ALL;
    save();
}

void IdentityManager::resetSettingsToDefault() {
    settings_ = AppSettings{};
    save();
}

} // namespace aerodesk
