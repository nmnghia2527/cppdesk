#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace aerodesk {

struct RecentSessionEntry {
    uint64_t    deskId = 0;
    std::string hostname;
    std::string address;
    bool        isFavorite = false;
};

struct AppSettings {
    bool          darkTheme = false;                     // false = Light (White & Red), true = Dark (Obsidian & Red)
    uint8_t       targetFps = 30;                        // 15, 30, or 60 FPS
    bool          adaptiveFps = true;                    // Auto-drop FPS when network connection is poor
    QualityPreset defaultQuality = QualityPreset::Balanced;
    uint8_t       defaultScaleMode = 0;                  // 0 = FitAspect, 1 = Stretch, 2 = Original 1:1
    bool          showRemoteCursor = true;               // Render remote cursor ring on viewer canvas
    bool          showSessionHud = true;                 // Show live FPS / RTT / Bitrate telemetry in session bar
    bool          autoAcceptIncoming = false;            // Automatically accept incoming requests without modal
    uint8_t       defaultPermissions = PERM_ALL;         // Default permissions granted to incoming viewers
    bool          lockWorkstationOnDisconnect = false;   // Lock Windows workstation when host session ends
};

class CryptoUtils {
public:
    static bool randomBytes(void* buffer, size_t len);
    static uint64_t generateNineDigitId();

    static std::array<uint8_t, 32> sha256(const void* data, size_t len);
    static std::array<uint8_t, 32> sha256(const std::string& text);
    static std::string toHex(const uint8_t* data, size_t len);
    static std::string sha256Hex(const std::string& text);

    // Salted password hash for storage & verification (never stores plaintext on disk)
    static std::string hashPassword(const std::string& password, const std::string& salt);

    // Challenge-response digest from precomputed verifier token: SHA256(passToken + hostId + clientId + nonce)
    static std::array<uint8_t, 32> computeChallengeResponseFromToken(
        const std::string& passwordVerifierToken,
        uint64_t hostId,
        uint64_t clientId,
        const std::array<uint8_t, 32>& nonce);

    // Challenge-response digest from plaintext password
    static std::array<uint8_t, 32> computeChallengeResponse(
        const std::string& passwordPlain,
        uint64_t hostId,
        uint64_t clientId,
        const std::array<uint8_t, 32>& nonce);

    // Derive a 256-bit session key from handshake parameters
    static std::array<uint8_t, 32> deriveSessionKey(
        uint64_t hostId,
        uint64_t clientId,
        const std::array<uint8_t, 32>& nonce);

    // 8-character uppercase SAS fingerprint ("XXXX-XXXX") derived from sessionKey to verify zero MITM
    static std::string sessionFingerprintHex(const std::array<uint8_t, 32>& sessionKey);

    // Fast stream cipher for payload confidentiality
    static void transformPayload(
        uint8_t* data,
        size_t len,
        const std::array<uint8_t, 32>& sessionKey,
        uint64_t frameSequence);

    // Format 9-digit ID as "123 456 789"
    static std::string formatDeskId(uint64_t id);

    // Parse "123 456 789" or "123456789" -> 123456789 (returns 0 if not a 9-digit ID)
    static uint64_t parseDeskId(const std::string& input);
};

class IdentityManager {
public:
    explicit IdentityManager(int instanceId = 1);

    bool loadOrCreate();
    bool save() const;

    int instanceId() const { return instanceId_; }
    uint64_t deskId() const { return deskId_; }
    std::string formattedDeskId() const { return CryptoUtils::formatDeskId(deskId_); }
    const std::string& hostname() const { return hostname_; }
    uint16_t listenPort() const { return listenPort_; }
    void setListenPort(uint16_t port) { listenPort_ = port; }

    bool unattendedEnabled() const { return unattendedEnabled_; }
    void setUnattendedEnabled(bool enabled);

    // Returns active dynamic session code (or in-memory unattended password if set during this run)
    const std::string& unattendedPassword() const { return unattendedPassword_.empty() ? sessionCode_ : unattendedPassword_; }
    void setUnattendedPassword(const std::string& newPassword);
    bool hasStoredPasswordVerifier() const { return !unattendedVerifier_.empty(); }
    const std::string& passwordVerifier() const { return unattendedVerifier_; }

    // Dynamic 6-character One-Time Session Password (in-memory only, regenerable anytime)
    const std::string& sessionCode() const { return sessionCode_; }
    std::string regenerateSessionCode();

    bool verifyChallengeResponse(
        uint64_t clientId,
        const std::array<uint8_t, 32>& nonce,
        const std::array<uint8_t, 32>& clientDigest) const;

    const std::string& relayServerAddress() const { return relayServerAddr_; }
    void setRelayServerAddress(const std::string& addr);

    const std::vector<RecentSessionEntry>& recentSessions() const { return recentSessions_; }
    void addOrUpdateRecentSession(uint64_t id, const std::string& host, const std::string& addr);
    void toggleFavoriteSession(uint64_t id);
    void removeRecentSession(uint64_t id);
    void clearRecentSessions();

    const AppSettings& settings() const { return settings_; }
    void updateSettings(const AppSettings& newSettings);
    void resetSettingsToDefault();

    std::string configFilePath() const { return configPath_; }

private:
    int                             instanceId_ = 1;
    uint64_t                        deskId_ = 0;
    std::string                     hostname_;
    uint16_t                        listenPort_ = 50990;
    bool                            unattendedEnabled_ = true;
    std::string                     unattendedPassword_; // In-memory only (never written in plaintext to disk)
    std::string                     unattendedVerifier_; // Salted SHA-256 verifier persisted in config_*.ini
    std::string                     sessionCode_;        // Dynamic 6-char One-Time Session Password
    std::string                     relayServerAddr_ = "127.0.0.1:50999";
    std::vector<RecentSessionEntry> recentSessions_;
    AppSettings                     settings_{};
    std::string                     configPath_;
};

} // namespace aerodesk
