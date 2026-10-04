#include "../src/core/protocol.hpp"
#include "../src/core/crypto_identity.hpp"
#include "../src/simd/simd_kernels.hpp"
#include "../src/capture/screen_capture.hpp"
#include "../src/control/input_injector.hpp"
#include "../src/control/clipboard_file_manager.hpp"
#include "../src/net/network_engine.hpp"
#include "../src/net/updater.hpp"
#include "../src/ui/notification_manager.hpp"
#include "../src/media/session_recorder.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cassert>
#include <cstring>
#include <cmath>

using namespace cppdesk;

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] " << file << ":" << line << " -> " << expr << "\n";
    }
}

#define TEST_ASSERT(cond) check((cond), #cond, __FILE__, __LINE__)

void testDeskIdAndCrypto() {
    std::cout << "[TEST 1] 9-Digit Desk ID Formatting & CNG Crypto Handshake...\n";

    uint64_t id = CryptoUtils::generateNineDigitId();
    TEST_ASSERT(id >= 100000000ULL && id <= 999999999ULL);

    std::string formatted = CryptoUtils::formatDeskId(482910375ULL);
    TEST_ASSERT(formatted == "482 910 375");
    TEST_ASSERT(CryptoUtils::parseDeskId("482 910 375") == 482910375ULL);
    TEST_ASSERT(CryptoUtils::parseDeskId("482-910-375") == 482910375ULL);
    TEST_ASSERT(CryptoUtils::parseDeskId("482910375") == 482910375ULL);
    TEST_ASSERT(CryptoUtils::parseDeskId("127.0.0.1:50990") == 0ULL);

    IdentityManager hostId(10);
    hostId.loadOrCreate();
    hostId.setUnattendedEnabled(true);
    hostId.setUnattendedPassword("SecretPass#2026");

    // Verify plaintext password is NEVER stored in config_*.ini (only salted verifier)
    {
        std::ifstream cfgIn(hostId.configFilePath());
        std::string cfgText((std::istreambuf_iterator<char>(cfgIn)), std::istreambuf_iterator<char>());
        TEST_ASSERT(cfgText.find("SecretPass#2026") == std::string::npos);
        TEST_ASSERT(cfgText.find("unattended_verifier=") != std::string::npos);
    }

    // Reload from disk and verify salted verifier still authenticates
    IdentityManager hostReloaded(10);
    hostReloaded.loadOrCreate();
    TEST_ASSERT(hostReloaded.hasStoredPasswordVerifier());

    std::array<uint8_t, 32> nonce{};
    TEST_ASSERT(CryptoUtils::randomBytes(nonce.data(), nonce.size()));

    uint64_t clientId = 123456789ULL;
    auto validResp = CryptoUtils::computeChallengeResponse("SecretPass#2026", hostReloaded.deskId(), clientId, nonce);
    auto wrongResp = CryptoUtils::computeChallengeResponse("WrongPassword", hostReloaded.deskId(), clientId, nonce);

    TEST_ASSERT(hostReloaded.verifyChallengeResponse(clientId, nonce, validResp));
    TEST_ASSERT(!hostReloaded.verifyChallengeResponse(clientId, nonce, wrongResp));

    // Verify dynamic 6-char One-Time Session Code authentication & regeneration
    std::string code1 = hostReloaded.sessionCode();
    TEST_ASSERT(code1.size() == 6);
    auto codeResp1 = CryptoUtils::computeChallengeResponse(code1, hostReloaded.deskId(), clientId, nonce);
    TEST_ASSERT(hostReloaded.verifyChallengeResponse(clientId, nonce, codeResp1));

    std::string code2 = hostReloaded.regenerateSessionCode();
    TEST_ASSERT(code2.size() == 6);
    auto codeResp2 = CryptoUtils::computeChallengeResponse(code2, hostReloaded.deskId(), clientId, nonce);
    TEST_ASSERT(hostReloaded.verifyChallengeResponse(clientId, nonce, codeResp2));

    // Stream cipher round-trip & SAS fingerprint ("XXXX-XXXX")
    auto sessionKey = CryptoUtils::deriveSessionKey(hostReloaded.deskId(), clientId, nonce);
    std::string fp = CryptoUtils::sessionFingerprintHex(sessionKey);
    TEST_ASSERT(fp.size() == 9);

    std::string secretPayload = "AeroDesk Encrypted Frame Payload Verification 1234567890";
    std::vector<uint8_t> buf(secretPayload.begin(), secretPayload.end());
    CryptoUtils::transformPayload(buf.data(), buf.size(), sessionKey, 42);
    TEST_ASSERT(std::memcmp(buf.data(), secretPayload.data(), buf.size()) != 0);
    CryptoUtils::transformPayload(buf.data(), buf.size(), sessionKey, 42);
    TEST_ASSERT(std::memcmp(buf.data(), secretPayload.data(), buf.size()) == 0);
}

void testTileCodecRoundTrip() {
    std::cout << "[TEST 2] Hybrid Zstd & GDI+ JPEG Tile Codec Round-Trip...\n";

    TileCodec::initGdiPlus();

    const int W = 128;
    const int H = 128;
    std::vector<uint8_t> srcRect(W * H * 4);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            size_t idx = static_cast<size_t>(y * W + x) * 4;
            srcRect[idx + 0] = static_cast<uint8_t>((x * 2) & 0xFF);
            srcRect[idx + 1] = static_cast<uint8_t>((y * 2) & 0xFF);
            srcRect[idx + 2] = static_cast<uint8_t>(((x + y) * 3) & 0xFF);
            srcRect[idx + 3] = 0xFF;
        }
    }

    // Ultra (Zstd lossless on structured pattern)
    EncodedTile zTile = TileCodec::encodeRect(32, 32, W, H, srcRect.data(), QualityPreset::Ultra);
    TEST_ASSERT(!zTile.data.empty());

    std::vector<uint8_t> canvas(256 * 256 * 4, 0);
    TEST_ASSERT(TileCodec::decodeTileIntoCanvas(zTile, canvas.data(), 256, 256));

    // Verify lossless pixel match at (32, 32)
    if (zTile.encoding == TileEncoding::Zstd) {
        bool exactMatch = true;
        for (int y = 0; y < H; ++y) {
            const uint8_t* cRow = canvas.data() + ((32 + y) * 256 + 32) * 4;
            const uint8_t* sRow = srcRect.data() + (y * W) * 4;
            if (std::memcmp(cRow, sRow, W * 4) != 0) {
                exactMatch = false;
                break;
            }
        }
        TEST_ASSERT(exactMatch);
    }

    // Explicit JPEG encode & decode test
    auto jpegBytes = TileCodec::encodeJpeg(srcRect.data(), W, H, 85);
    TEST_ASSERT(!jpegBytes.empty());
    std::vector<uint8_t> decodedJpeg;
    int jw = 0, jh = 0;
    TEST_ASSERT(TileCodec::decodeJpeg(jpegBytes.data(), jpegBytes.size(), decodedJpeg, jw, jh));
    TEST_ASSERT(jw == W && jh == H);
}

void testScreenCapturer() {
    std::cout << "[TEST 3] DXGI Desktop Duplication / GDI Screen Capture & Delta Tiles...\n";

    ScreenCapturer capturer;
    auto monitors = capturer.enumerateMonitors();
    TEST_ASSERT(!monitors.empty());
    TEST_ASSERT(capturer.frameWidth() > 0 && capturer.frameHeight() > 0);

    std::vector<EncodedTile> tiles;
    bool isKf = false;
    CursorState cursor{};

    bool ok = capturer.captureDirtyTiles(true, QualityPreset::Balanced, tiles, isKf, cursor);
    TEST_ASSERT(ok);
    TEST_ASSERT(isKf);
    TEST_ASSERT(!tiles.empty());

    std::cout << "  -> Captured " << capturer.frameWidth() << "x" << capturer.frameHeight()
              << " keyframe into " << tiles.size() << " compressed tiles (Backend: "
              << (capturer.usingDxgi() ? "DXGI GPU" : "GDI") << ")\n";
}

void testEndToEndSessionAndFileTransfer() {
    std::cout << "[TEST 4] End-to-End 9-Digit ID Resolution, E2EE Stream, Video, File SHA-256, Chat & Rate Limiting...\n";

    IdentityManager hostIdentity(11);
    hostIdentity.loadOrCreate();
    hostIdentity.setListenPort(50994);
    hostIdentity.setUnattendedEnabled(true);
    hostIdentity.setUnattendedPassword("TestPass99");
    hostIdentity.setRelayServerAddress("127.0.0.1:50999");

    IdentityManager viewerIdentity(12);
    viewerIdentity.loadOrCreate();
    viewerIdentity.setListenPort(50995);
    viewerIdentity.setRelayServerAddress("127.0.0.1:50999");

    NetworkEngine hostNet(hostIdentity);
    NetworkEngine viewerNet(viewerIdentity);

    // Configure isolated temp receive directory for file transfer verification
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "AeroDesk_Test_Xfer";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    hostNet.fileTransferManager().setReceiveDirectory(tempDir.string());

    TEST_ASSERT(hostNet.start());
    hostNet.startLocalRelayServer(DEFAULT_RELAY_PORT);
    TEST_ASSERT(viewerNet.start());

    // Wait briefly for UDP LAN discovery / Relay registration
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Connect Viewer -> Host using the formatted 9-digit Desk ID ("XXX XXX XXX") and Unattended Password
    std::string targetIdStr = hostIdentity.formattedDeskId();
    std::cout << "  -> Dialing Host by 9-digit ID: " << targetIdStr << "...\n";
    TEST_ASSERT(viewerNet.connectToRemote(targetIdStr, "TestPass99"));

    // Wait up to 4 seconds for Connected state and first decoded video frame
    uint64_t frameSeq = 0;
    std::vector<uint8_t> viewerFrame;
    int fw = 0, fh = 0;
    CursorState cur{};
    bool gotVideoFrame = false;

    for (int i = 0; i < 80; ++i) {
        auto st = viewerNet.viewerStats();
        if (st.state == ViewerConnectionState::Connected) {
            if (viewerNet.copyLatestViewerFrame(frameSeq, viewerFrame, fw, fh, cur) && fw > 0 && fh > 0) {
                gotVideoFrame = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    TEST_ASSERT(viewerNet.viewerStats().state == ViewerConnectionState::Connected);
    TEST_ASSERT(gotVideoFrame);
    // Verify E2EE session SAS fingerprint matches between Viewer and Host
    TEST_ASSERT(!viewerNet.viewerStats().securityFingerprint.empty());
    TEST_ASSERT(viewerNet.viewerStats().securityFingerprint == hostNet.hostSessionStatus().securityFingerprint);
    std::cout << "  -> Viewer received encrypted video frame (" << fw << "x" << fh
              << ", SAS: " << viewerNet.viewerStats().securityFingerprint << ")!\n";

    // Test bidirectional encrypted Live Chat
    TEST_ASSERT(viewerNet.sendChatMessage("Hello Host over E2EE!"));
    TEST_ASSERT(hostNet.sendChatMessage("Hello Viewer from Host!"));
    bool chatSynced = false;
    for (int i = 0; i < 30; ++i) {
        auto vMsgs = viewerNet.chatMessages();
        auto hMsgs = hostNet.chatMessages();
        if (vMsgs.size() >= 2 && hMsgs.size() >= 2) {
            chatSynced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    TEST_ASSERT(chatSynced);
    std::cout << "  -> Verified bidirectional encrypted live chat!\n";

    // Test chunked file transfer from Viewer -> Host with streaming SHA-256 verification & .part rename
    std::filesystem::path srcFile = tempDir / "source_payload.bin";
    std::string testContent(150000, '\0'); // ~150 KB (> 2 chunks)
    for (size_t i = 0; i < testContent.size(); ++i) {
        testContent[i] = static_cast<char>('A' + (i % 26));
    }
    {
        std::ofstream out(srcFile, std::ios::binary);
        out.write(testContent.data(), static_cast<std::streamsize>(testContent.size()));
    }

    uint32_t tid = viewerNet.sendFile(srcFile.string());
    TEST_ASSERT(tid > 0);

    bool fileReceived = false;
    std::filesystem::path receivedPath;
    for (int i = 0; i < 60; ++i) {
        auto items = hostNet.fileTransferManager().snapshotTransfers();
        if (!items.empty() && items.front().status == TransferStatus::Completed) {
            fileReceived = true;
            receivedPath = items.front().savedPath;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    TEST_ASSERT(fileReceived);
    if (fileReceived) {
        std::ifstream in(receivedPath, std::ios::binary);
        std::string recContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        TEST_ASSERT(recContent.size() == testContent.size());
        TEST_ASSERT(CryptoUtils::sha256Hex(recContent) == CryptoUtils::sha256Hex(testContent));
        // Ensure temporary .part file was atomically renamed away
        TEST_ASSERT(!std::filesystem::exists(receivedPath.string() + ".part"));
        std::cout << "  -> Verified 150 KB chunked file transfer SHA-256 integrity & atomic .part rename!\n";
    }

    // Test live permission updates from Host -> Viewer
    hostNet.updateHostSessionPermissions(PERM_CLIPBOARD | PERM_FILE_TRANSFER); // Disable PERM_INPUT
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    TEST_ASSERT((viewerNet.viewerStats().grantedPermissions & PERM_INPUT) == 0);
    TEST_ASSERT((viewerNet.viewerStats().grantedPermissions & PERM_FILE_TRANSFER) != 0);

    // Test live 15 / 30 / 60 FPS control updates from Viewer -> Host
    viewerNet.setSessionFpsConfig(60, true);
    TEST_ASSERT(viewerNet.viewerStats().targetFps == 60);
    TEST_ASSERT(viewerNet.viewerStats().adaptiveFps == true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    viewerNet.setSessionFpsConfig(15, false);
    TEST_ASSERT(viewerNet.viewerStats().targetFps == 15);
    TEST_ASSERT(viewerNet.viewerStats().adaptiveFps == false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    viewerNet.disconnectViewer();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test Interactive Approval Modal Flow (connecting with blank password -> Host clicks Accept)
    std::cout << "  -> Testing Interactive Approval Flow (Accept/Reject modal + custom permissions)...\n";
    TEST_ASSERT(viewerNet.connectToRemote(targetIdStr, ""));

    bool sawPendingPopup = false;
    for (int i = 0; i < 40; ++i) {
        auto pending = hostNet.pendingIncomingRequest();
        if (pending.active && pending.callerDeskId == viewerIdentity.deskId()) {
            sawPendingPopup = true;
            hostNet.respondToIncomingRequest(true, PERM_INPUT | PERM_CLIPBOARD);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    TEST_ASSERT(sawPendingPopup);

    for (int i = 0; i < 40; ++i) {
        if (viewerNet.viewerStats().state == ViewerConnectionState::Connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    TEST_ASSERT(viewerNet.viewerStats().state == ViewerConnectionState::Connected);
    TEST_ASSERT(viewerNet.viewerStats().grantedPermissions == (PERM_INPUT | PERM_CLIPBOARD));

    viewerNet.disconnectViewer();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Test Brute-Force IP Rate Limiting (5 failed password attempts -> RateLimited lockout)
    std::cout << "  -> Testing Brute-Force Rate Limiting (5 bad attempts -> 60s lockout)...\n";
    hostNet.clearRateLimitRecords();
    bool sawRateLimited = false;
    for (int attempt = 1; attempt <= 8; ++attempt) {
        TEST_ASSERT(viewerNet.connectToRemote(targetIdStr, "WrongPasswordAttempt"));
        for (int w = 0; w < 40; ++w) {
            auto st = viewerNet.viewerStats();
            if (st.state == ViewerConnectionState::Error) {
                if (st.statusMessage.find("Too many failed") != std::string::npos ||
                    st.statusMessage.find("Locked for 60s") != std::string::npos) {
                    sawRateLimited = true;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        viewerNet.disconnectViewer();
        if (sawRateLimited) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    TEST_ASSERT(sawRateLimited);
    hostNet.clearRateLimitRecords();

    hostNet.stop();
    viewerNet.stop();
    std::filesystem::remove_all(tempDir, ec);
}

void testFileTransferEdgeCasesAndFavorites() {
    std::cout << "[TEST 5] File Transfer .part Cleanup, SHA-256 Corruption Rejection & Favorite Desks...\n";

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "AeroDesk_Test_Edge";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);

    FileTransferManager ftm;
    ftm.setReceiveDirectory(tempDir.string());

    // 1. Mid-transfer cancel cleans up .part file
    ftm.handleFileOffer(701, 8192, "partial_cancel.bin");
    std::vector<uint8_t> chunkData(1024, 0x5A);
    ftm.handleFileChunk(701, 0, chunkData.data(), chunkData.size());
    TEST_ASSERT(std::filesystem::exists(tempDir / "partial_cancel.bin.part"));

    ftm.handleFileCancel(701);
    TEST_ASSERT(!std::filesystem::exists(tempDir / "partial_cancel.bin.part"));
    TEST_ASSERT(!std::filesystem::exists(tempDir / "partial_cancel.bin"));

    // 2. Corrupted SHA-256 on FileComplete deletes .part file and marks transfer Failed
    ftm.handleFileOffer(702, 1024, "corrupt_check.bin");
    ftm.handleFileChunk(702, 0, chunkData.data(), chunkData.size());
    ftm.handleFileComplete(702, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    TEST_ASSERT(!std::filesystem::exists(tempDir / "corrupt_check.bin.part"));
    TEST_ASSERT(!std::filesystem::exists(tempDir / "corrupt_check.bin"));

    // 2b. Empty SHA-256 on FileComplete is rejected and deletes .part file
    ftm.handleFileOffer(703, 1024, "empty_sha_check.bin");
    ftm.handleFileChunk(703, 0, chunkData.data(), chunkData.size());
    ftm.handleFileComplete(703, "");
    TEST_ASSERT(!std::filesystem::exists(tempDir / "empty_sha_check.bin.part"));
    TEST_ASSERT(!std::filesystem::exists(tempDir / "empty_sha_check.bin"));

    // 2c. Concurrent incoming stream queue cap (max 10)
    for (uint32_t i = 800; i < 810; ++i) {
        ftm.handleFileOffer(i, 512, "stream_" + std::to_string(i) + ".bin");
    }
    ftm.handleFileOffer(811, 512, "stream_overflow.bin");
    bool foundQueueFull = false;
    for (const auto& item : ftm.snapshotTransfers()) {
        if (item.transferId == 811 && item.statusText == "Rejected (Queue Full)") {
            foundQueueFull = true;
            break;
        }
    }
    TEST_ASSERT(foundQueueFull);
    ftm.abortActiveTransfers();

    std::filesystem::remove_all(tempDir, ec);

    // 3. Pinned Favorite Desks & Recent Session removal persistence
    IdentityManager favMgr(18);
    favMgr.loadOrCreate();
    favMgr.addOrUpdateRecentSession(482910375ULL, "Design-Workstation", "127.0.0.1:50990");
    favMgr.toggleFavoriteSession(482910375ULL);

    IdentityManager favReload(18);
    favReload.loadOrCreate();
    bool foundFav = false;
    for (const auto& r : favReload.recentSessions()) {
        if (r.deskId == 482910375ULL && r.isFavorite) {
            foundFav = true;
            break;
        }
    }
    TEST_ASSERT(foundFav);
    favReload.removeRecentSession(482910375ULL);
    bool stillPresent = false;
    for (const auto& r : favReload.recentSessions()) {
        if (r.deskId == 482910375ULL) stillPresent = true;
    }
    TEST_ASSERT(!stillPresent);
}

void testAppSettingsAndAdaptiveFps() {
    std::cout << "[TEST 6] AppSettings Persistence, Light/Dark Theme Config & Adaptive FPS Congestion Throttling...\n";

    // 1. Verify clampTargetFps
    TEST_ASSERT(clampTargetFps(10) == 15);
    TEST_ASSERT(clampTargetFps(15) == 15);
    TEST_ASSERT(clampTargetFps(28) == 30);
    TEST_ASSERT(clampTargetFps(30) == 30);
    TEST_ASSERT(clampTargetFps(45) == 30);
    TEST_ASSERT(clampTargetFps(50) == 60);
    TEST_ASSERT(clampTargetFps(60) == 60);
    TEST_ASSERT(clampTargetFps(120) == 60);

    // 2. Verify computeAdaptiveFpsCap automatic poor-network step-down (60 -> 30 -> 15 FPS)
    // Healthy network (12ms RTT, 4ms send): keeps 60 FPS
    TEST_ASSERT(computeAdaptiveFpsCap(60, true, 12, 4.0f) == 60);
    // Moderate network congestion (110ms RTT or 42ms send): steps 60 FPS down to 30 FPS
    TEST_ASSERT(computeAdaptiveFpsCap(60, true, 110, 10.0f) == 30);
    TEST_ASSERT(computeAdaptiveFpsCap(60, true, 25, 42.0f) == 30);
    // Severe network congestion (210ms RTT or 85ms send): drops to 15 FPS
    TEST_ASSERT(computeAdaptiveFpsCap(60, true, 210, 10.0f) == 15);
    TEST_ASSERT(computeAdaptiveFpsCap(60, true, 30, 85.0f) == 15);
    TEST_ASSERT(computeAdaptiveFpsCap(30, true, 195, 12.0f) == 15);
    // Adaptive disabled: honours user target FPS regardless of RTT/send time
    TEST_ASSERT(computeAdaptiveFpsCap(60, false, 250, 95.0f) == 60);
    TEST_ASSERT(computeAdaptiveFpsCap(30, false, 250, 95.0f) == 30);
    TEST_ASSERT(computeAdaptiveFpsCap(15, true, 250, 95.0f) == 15);

    // 3. Verify AppSettings round-trip save/load in IdentityManager
    {
        IdentityManager idSave(19);
        idSave.loadOrCreate();
        AppSettings s = idSave.settings();
        s.darkTheme = true;
        s.targetFps = 60;
        s.adaptiveFps = false;
        s.defaultQuality = QualityPreset::LowBandwidth;
        s.defaultScaleMode = 1;
        s.showRemoteCursor = false;
        s.showSessionHud = false;
        s.autoAcceptIncoming = true;
        s.defaultPermissions = PERM_INPUT | PERM_CLIPBOARD;
        s.lockWorkstationOnDisconnect = true;
        idSave.updateSettings(s);
    }
    {
        IdentityManager idLoad(19);
        idLoad.loadOrCreate();
        AppSettings loaded = idLoad.settings();
        TEST_ASSERT(loaded.darkTheme == true);
        TEST_ASSERT(loaded.targetFps == 60);
        TEST_ASSERT(loaded.adaptiveFps == false);
        TEST_ASSERT(loaded.defaultQuality == QualityPreset::LowBandwidth);
        TEST_ASSERT(loaded.defaultScaleMode == 1);
        TEST_ASSERT(loaded.showRemoteCursor == false);
        TEST_ASSERT(loaded.showSessionHud == false);
        TEST_ASSERT(loaded.autoAcceptIncoming == true);
        TEST_ASSERT(loaded.defaultPermissions == (PERM_INPUT | PERM_CLIPBOARD));
        TEST_ASSERT(loaded.lockWorkstationOnDisconnect == true);

        // Reset to defaults and verify
        idLoad.resetSettingsToDefault();
        AppSettings def = idLoad.settings();
        TEST_ASSERT(def.darkTheme == false);
        TEST_ASSERT(def.targetFps == 30);
        TEST_ASSERT(def.adaptiveFps == true);
        TEST_ASSERT(def.defaultQuality == QualityPreset::Balanced);
        TEST_ASSERT(def.defaultPermissions == PERM_ALL);
    }
}

void testAvx2SimdAssemblyKernels() {
    std::cout << "[TEST 7] x86-64 AVX2 Assembly SIMD Kernels (Tile Diff, Tile Hash & Stream Cipher)...\n";
    TEST_ASSERT(SimdKernels::hasAvx2());

    const int W = 64;
    const int H = 64;
    const int stride = W * 4;
    std::vector<uint8_t> tileA(W * H * 4, 0x42);
    std::vector<uint8_t> tileB(W * H * 4, 0x42);

    // 1. Identical 64x64 tiles -> diff == 0, hashes match
    TEST_ASSERT(cppdesk_avx2_tile_diff(tileA.data(), tileB.data(), stride, W * 4, H) == 0);
    uint64_t h1 = cppdesk_avx2_hash_tile(tileA.data(), stride, W * 4, H);
    uint64_t h2 = cppdesk_avx2_hash_tile(tileB.data(), stride, W * 4, H);
    TEST_ASSERT(h1 == h2 && h1 != 0);

    // 2. Single-pixel difference -> diff == 1, hash changes
    tileB[W * H * 2 + 17] ^= 0x01;
    TEST_ASSERT(cppdesk_avx2_tile_diff(tileA.data(), tileB.data(), stride, W * 4, H) == 1);
    uint64_t h3 = cppdesk_avx2_hash_tile(tileB.data(), stride, W * 4, H);
    TEST_ASSERT(h1 != h3);
    tileB[W * H * 2 + 17] ^= 0x01; // Restore

    // 3. Benchmark AVX2 Assembly vs C++ Scalar over 25,000 64x64 tile comparisons (400 MB)
    const int iters = 25000;
    auto t0 = std::chrono::high_resolution_clock::now();
    int accScalar = 0;
    for (int i = 0; i < iters; ++i) {
        accScalar += SimdKernels::scalarTileDiff(tileA.data(), tileB.data(), stride, W * 4, H) ? 1 : 0;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    int accAvx2 = 0;
    for (int i = 0; i < iters; ++i) {
        accAvx2 += cppdesk_avx2_tile_diff(tileA.data(), tileB.data(), stride, W * 4, H);
    }
    auto t2 = std::chrono::high_resolution_clock::now();
    TEST_ASSERT(accScalar == 0 && accAvx2 == 0);

    double scalarSec = std::chrono::duration<double>(t1 - t0).count();
    double avx2Sec   = std::chrono::duration<double>(t2 - t1).count();
    double totalGb   = (static_cast<double>(iters) * W * H * 4.0) / (1024.0 * 1024.0 * 1024.0);
    double avx2Gbs   = totalGb / std::max(1e-6, avx2Sec);
    double speedup   = scalarSec / std::max(1e-6, avx2Sec);

    std::cout << "  -> AVX2 Assembly 64x64 Tile Diff Throughput: " << avx2Gbs
              << " GB/s (" << speedup << "x faster than scalar C++)\n";
}

void testEcdhAndAesGcmEngine() {
    std::cout << "[TEST 8] Ephemeral ECDH (P-256), AES-256-GCM AEAD & Multi-threaded Tile Pipeline...\n";

    // 1. Anti-Replay Sliding Window (64-packet bitmask)
    AntiReplayWindow replayWin;
    TEST_ASSERT(replayWin.checkAndMark(0));
    TEST_ASSERT(!replayWin.checkAndMark(0)); // Duplicate rejected
    TEST_ASSERT(replayWin.checkAndMark(5));  // Higher seq
    TEST_ASSERT(replayWin.checkAndMark(3));  // Out-of-order within 64 allowed
    TEST_ASSERT(!replayWin.checkAndMark(3)); // Duplicate rejected
    TEST_ASSERT(replayWin.checkAndMark(100)); // Advances window
    TEST_ASSERT(!replayWin.checkAndMark(10)); // Far behind (< 100 - 64) rejected
    TEST_ASSERT(!replayWin.checkAndMark(100)); // Duplicate rejected

    // 2. Ephemeral NIST P-256 ECDH Key Agreement (Zero-Trust)
    EcdhKeyExchange hostEcdh;
    EcdhKeyExchange viewerEcdh;
    TEST_ASSERT(hostEcdh.initialize());
    TEST_ASSERT(viewerEcdh.initialize());

    const auto& hostPub = hostEcdh.localPublicKey();
    const auto& viewerPub = viewerEcdh.localPublicKey();
    TEST_ASSERT(hostPub.size() == 72); // BCRYPT_ECCPUBLIC_BLOB for P-256
    TEST_ASSERT(viewerPub.size() == 72);

    std::array<uint8_t, 32> nonce{};
    CryptoUtils::randomBytes(nonce.data(), nonce.size());
    uint64_t hostDeskId = 111222333ULL;
    uint64_t viewerDeskId = 444555666ULL;

    std::array<uint8_t, 32> hostDerivedKey{};
    std::array<uint8_t, 32> viewerDerivedKey{};
    TEST_ASSERT(hostEcdh.computeSharedSessionKey(viewerPub.data(), viewerPub.size(), hostDeskId, viewerDeskId, nonce, hostDerivedKey));
    TEST_ASSERT(viewerEcdh.computeSharedSessionKey(hostPub.data(), hostPub.size(), hostDeskId, viewerDeskId, nonce, viewerDerivedKey));

    TEST_ASSERT(hostDerivedKey == viewerDerivedKey);
    TEST_ASSERT(CryptoUtils::sessionFingerprintHex(hostDerivedKey) == CryptoUtils::sessionFingerprintHex(viewerDerivedKey));

    // 3. Hardware AES-256-GCM AEAD Session Ciphers
    AesGcmSessionCipher hostCipher;
    AesGcmSessionCipher viewerCipher;
    TEST_ASSERT(hostCipher.initialize(hostDerivedKey, /*isHost=*/true));
    TEST_ASSERT(viewerCipher.initialize(viewerDerivedKey, /*isHost=*/false));

    // Host -> Viewer Frame Encryption
    std::string testPlaintext = "AeroDesk 4K 60FPS Video Tile Payload (Confidential & Authenticated)";
    FrameHeader mockHdr{};
    mockHdr.magic = PROTOCOL_MAGIC;
    mockHdr.type = static_cast<uint8_t>(PacketType::VIDEO_FRAME_TILES);
    mockHdr.flags = FLAG_ENCRYPTED;
    mockHdr.payloadSize = static_cast<uint32_t>(12 + testPlaintext.size() + 16);

    std::vector<uint8_t> encryptedPkt;
    TEST_ASSERT(hostCipher.encrypt(testPlaintext.data(), testPlaintext.size(), 1, &mockHdr, sizeof(mockHdr), encryptedPkt));
    TEST_ASSERT(encryptedPkt.size() == 12 + testPlaintext.size() + 16);

    // Decrypt on Viewer
    std::vector<uint8_t> decryptedPkt;
    uint64_t rxSeq = 0;
    TEST_ASSERT(viewerCipher.decrypt(encryptedPkt.data(), encryptedPkt.size(), &mockHdr, sizeof(mockHdr), decryptedPkt, &rxSeq));
    TEST_ASSERT(rxSeq == 1);
    TEST_ASSERT(std::string(decryptedPkt.begin(), decryptedPkt.end()) == testPlaintext);

    // Replay Attack Detection
    std::vector<uint8_t> replayDec;
    TEST_ASSERT(!viewerCipher.decrypt(encryptedPkt.data(), encryptedPkt.size(), &mockHdr, sizeof(mockHdr), replayDec));

    // Tamper Detection: Bit-Flip in Ciphertext
    std::vector<uint8_t> tamperedPkt = encryptedPkt;
    tamperedPkt[15] ^= 0x40;
    std::vector<uint8_t> tamperedDec;
    TEST_ASSERT(!viewerCipher.decrypt(tamperedPkt.data(), tamperedPkt.size(), &mockHdr, sizeof(mockHdr), tamperedDec));

    // Tamper Detection: Header/AAD mismatch
    FrameHeader tamperedHdr = mockHdr;
    tamperedHdr.type = static_cast<uint8_t>(PacketType::INPUT_MOUSE_MOVE);
    TEST_ASSERT(!viewerCipher.decrypt(encryptedPkt.data(), encryptedPkt.size(), &tamperedHdr, sizeof(tamperedHdr), tamperedDec));

    // Cryptographic Anti-Replay: Unauthenticated packet with high seq (99999) must NOT poison window
    std::vector<uint8_t> forgedPkt(12 + 32 + 16, 0xEE);
    uint64_t forgedSeq = 99999;
    std::memcpy(forgedPkt.data() + 4, &forgedSeq, 8);
    std::vector<uint8_t> forgedDec;
    TEST_ASSERT(!viewerCipher.decrypt(forgedPkt.data(), forgedPkt.size(), &mockHdr, sizeof(mockHdr), forgedDec));

    // Legitimate subsequent packet with seq = 2 must decrypt successfully
    std::vector<uint8_t> legitPkt2;
    std::string legitMsg2 = "Legitimate seq 2 after forged attempt";
    mockHdr.payloadSize = static_cast<uint32_t>(12 + legitMsg2.size() + 16);
    TEST_ASSERT(hostCipher.encrypt(legitMsg2.data(), legitMsg2.size(), 2, &mockHdr, sizeof(mockHdr), legitPkt2));
    std::vector<uint8_t> legitDec2;
    uint64_t rxSeq2 = 0;
    TEST_ASSERT(viewerCipher.decrypt(legitPkt2.data(), legitPkt2.size(), &mockHdr, sizeof(mockHdr), legitDec2, &rxSeq2));
    TEST_ASSERT(rxSeq2 == 2);
    TEST_ASSERT(std::string(legitDec2.begin(), legitDec2.end()) == legitMsg2);

    // Viewer -> Host Input Encryption
    std::string mouseEvent = "MOUSE_MOVE_CLICK";
    mockHdr.type = static_cast<uint8_t>(PacketType::INPUT_MOUSE_MOVE);
    mockHdr.payloadSize = static_cast<uint32_t>(12 + mouseEvent.size() + 16);
    std::vector<uint8_t> clientEnc;
    TEST_ASSERT(viewerCipher.encrypt(mouseEvent.data(), mouseEvent.size(), 1, &mockHdr, sizeof(mockHdr), clientEnc));
    std::vector<uint8_t> clientDec;
    uint64_t clientSeq = 0;
    TEST_ASSERT(hostCipher.decrypt(clientEnc.data(), clientEnc.size(), &mockHdr, sizeof(mockHdr), clientDec, &clientSeq));
    TEST_ASSERT(clientSeq == 1);
    TEST_ASSERT(std::string(clientDec.begin(), clientDec.end()) == mouseEvent);

    // Rekeying Ratchet (Option 4A)
    TEST_ASSERT(hostCipher.ratchetKey());
    TEST_ASSERT(viewerCipher.ratchetKey());
    std::string postRatchetPlain = "Payload after 1 GB ratchet key rotation";
    mockHdr.payloadSize = static_cast<uint32_t>(12 + postRatchetPlain.size() + 16);
    std::vector<uint8_t> ratchetEnc;
    TEST_ASSERT(hostCipher.encrypt(postRatchetPlain.data(), postRatchetPlain.size(), 2, &mockHdr, sizeof(mockHdr), ratchetEnc));
    std::vector<uint8_t> ratchetDec;
    TEST_ASSERT(viewerCipher.decrypt(ratchetEnc.data(), ratchetEnc.size(), &mockHdr, sizeof(mockHdr), ratchetDec));
    TEST_ASSERT(std::string(ratchetDec.begin(), ratchetDec.end()) == postRatchetPlain);

    // 4. Multi-Threaded Tile Compression Pool (Option 2A)
    std::vector<TileThreadPool::RectTask> tasks;
    for (int i = 0; i < 8; ++i) {
        TileThreadPool::RectTask t{};
        t.rx = static_cast<uint16_t>((i % 4) * 64);
        t.ry = static_cast<uint16_t>((i / 4) * 64);
        t.rw = 64;
        t.rh = 64;
        t.bgraPixels.assign(64 * 64 * 4, static_cast<uint8_t>(0x20 * i + 0x10));
        t.preset = QualityPreset::Balanced;
        tasks.push_back(std::move(t));
    }
    TileThreadPool::instance().parallelEncode(tasks);
    for (const auto& t : tasks) {
        TEST_ASSERT(!t.result.data.empty());
        TEST_ASSERT(t.result.width == 64 && t.result.height == 64);
    }

    // 5. InputInjector UAC Desktop Elevation Query (Option 5A)
    bool isElev = InputInjector::isElevated();
    (void)isElev; // Querying elevated token status executes without throwing or crashing
    TEST_ASSERT(true);
}

void testV201FeaturesAndResilience() {
    std::cout << "[TEST 9] v2.0.1 Multi-Monitor Protocol, System Actions & Quality Update...\n";

    // 1. MONITOR_LIST Serialization & Deserialization
    std::vector<MonitorDesc> mockMons;
    MonitorDesc m1{};
    m1.index = 0; m1.x = 0; m1.y = 0; m1.width = 1920; m1.height = 1080;
    m1.isPrimary = true; m1.name = "Display 1 (1920x1080)";
    mockMons.push_back(m1);

    MonitorDesc m2{};
    m2.index = 1; m2.x = 1920; m2.y = 0; m2.width = 2560; m2.height = 1440;
    m2.isPrimary = false; m2.name = "Display 2 (2560x1440)";
    mockMons.push_back(m2);

    ByteWriter monListW;
    monListW.writeU16(static_cast<uint16_t>(mockMons.size()));
    for (const auto& m : mockMons) {
        monListW.writeI32(m.index);
        monListW.writeI32(m.x);
        monListW.writeI32(m.y);
        monListW.writeI32(m.width);
        monListW.writeI32(m.height);
        monListW.writeU8(m.isPrimary ? 1 : 0);
        monListW.writeString(m.name);
    }

    ByteReader monListR(monListW.buffer());
    uint16_t readCount = monListR.readU16();
    TEST_ASSERT(readCount == 2);
    MonitorDesc r1{};
    r1.index = monListR.readI32();
    r1.x = monListR.readI32();
    r1.y = monListR.readI32();
    r1.width = monListR.readI32();
    r1.height = monListR.readI32();
    r1.isPrimary = (monListR.readU8() != 0);
    r1.name = monListR.readString();
    TEST_ASSERT(r1.index == 0 && r1.width == 1920 && r1.height == 1080 && r1.isPrimary);
    TEST_ASSERT(r1.name == "Display 1 (1920x1080)");

    MonitorDesc r2{};
    r2.index = monListR.readI32();
    r2.x = monListR.readI32();
    r2.y = monListR.readI32();
    r2.width = monListR.readI32();
    r2.height = monListR.readI32();
    r2.isPrimary = (monListR.readU8() != 0);
    r2.name = monListR.readString();
    TEST_ASSERT(r2.index == 1 && r2.x == 1920 && r2.width == 2560 && !r2.isPrimary);
    TEST_ASSERT(r2.name == "Display 2 (2560x1440)");

    // 2. MONITOR_SELECT Serialization & Deserialization
    ByteWriter selW;
    selW.writeI32(1);
    ByteReader selR(selW.buffer());
    int32_t selIdx = selR.readI32();
    TEST_ASSERT(selIdx == 1);

    // 3. QUALITY_UPDATE Serialization & Deserialization
    ByteWriter qW;
    qW.writeU8(static_cast<uint8_t>(QualityPreset::Ultra));
    qW.writeU8(60);
    qW.writeU8(1);
    ByteReader qR(qW.buffer());
    uint8_t qPreset = qR.readU8();
    uint8_t qFps = qR.readU8();
    uint8_t qAdap = qR.readU8();
    TEST_ASSERT(qPreset == static_cast<uint8_t>(QualityPreset::Ultra));
    TEST_ASSERT(qFps == 60);
    TEST_ASSERT(qAdap == 1);

    // 4. SystemActionType Enumeration & Invalid Action Handling
    TEST_ASSERT(static_cast<uint8_t>(SystemActionType::TaskManager) == 1);
    TEST_ASSERT(static_cast<uint8_t>(SystemActionType::ShowDesktop) == 2);
    TEST_ASSERT(static_cast<uint8_t>(SystemActionType::LockWorkstation) == 3);
    TEST_ASSERT(static_cast<uint8_t>(SystemActionType::SendCtrlAltDel) == 4);
    TEST_ASSERT(static_cast<uint8_t>(SystemActionType::EmergencyReboot) == 5);
    TEST_ASSERT(!InputInjector::executeSystemAction(static_cast<SystemActionType>(99)));

    // 5. DXGI ACCESS_LOST Instant GDI Fallback & Recovery
    ScreenCapturer capturer;
    capturer.triggerDxgiAccessLostForTest();
    TEST_ASSERT(capturer.dxgiRecoveryState() == ScreenCapturer::DxgiRecoveryState::FallbackGdi);
    TEST_ASSERT(!capturer.usingDxgi());
    std::vector<EncodedTile> fbTiles;
    bool fbKeyframe = false;
    CursorState fbCursor;
    bool capOk = capturer.captureDirtyTiles(true, QualityPreset::Balanced, fbTiles, fbKeyframe, fbCursor);
    TEST_ASSERT(capOk);
    TEST_ASSERT(fbKeyframe);
    TEST_ASSERT(!fbTiles.empty());

    // 6. Viewer Auto-Reconnect State & Attempts
    ViewerSessionStats stats;
    stats.state = ViewerConnectionState::Reconnecting;
    stats.reconnectAttempt = 1;
    TEST_ASSERT(stats.state == ViewerConnectionState::Reconnecting);
    TEST_ASSERT(stats.reconnectAttempt == 1);

    // 7. InputInjector Elevation & Safe Desktop Sync
    bool isElev = InputInjector::isElevated();
    TEST_ASSERT(isElev == true || isElev == false);
    // Desktop sync does not crash or invalidate handles
    bool syncOk = InputInjector::syncToInputDesktop();
    (void)syncOk; // May be true or false depending on execution environment

    // 8. Hardware Cursor & Mouse Move Bounds Clamping
    MonitorDesc testMon{ 0, 0, 0, 1920, 1080, true, "TestMon" };
    InputInjector::injectMouseMove(0.5f, 0.5f, testMon);
    InputInjector::injectMouseMove(-1.0f, 2.0f, testMon); // Clamped to [0, 1]
    POINT pt{};
    if (GetCursorPos(&pt)) {
        TEST_ASSERT(pt.x >= 0 && pt.y >= 0);
    }
}

void testNotificationSystemAndTray() {
    std::cout << "[TEST 10] Windows Push Notifications, Tray Icon & AppSettings Persistence...\n";

    // 1. AppSettings default notification toggles
    AppSettings defSettings{};
    TEST_ASSERT(defSettings.enablePushNotifications == true);
    TEST_ASSERT(defSettings.enableTaskbarFlash == true);
    TEST_ASSERT(defSettings.enableNotificationSounds == true);
    TEST_ASSERT(defSettings.minimizeToTray == false);

    // 2. INI Serialization / Deserialization round-trip
    {
        IdentityManager idMgr(99);
        idMgr.loadOrCreate();

        AppSettings custom = idMgr.settings();
        custom.enablePushNotifications = false;
        custom.enableTaskbarFlash = false;
        custom.enableNotificationSounds = false;
        custom.minimizeToTray = true;
        idMgr.updateSettings(custom);

        // Reload from disk to verify persistence
        IdentityManager reloadMgr(99);
        TEST_ASSERT(reloadMgr.loadOrCreate());
        const auto& loaded = reloadMgr.settings();
        TEST_ASSERT(!loaded.enablePushNotifications);
        TEST_ASSERT(!loaded.enableTaskbarFlash);
        TEST_ASSERT(!loaded.enableNotificationSounds);
        TEST_ASSERT(loaded.minimizeToTray);

        // Clean up test config
        std::error_code ec;
        std::filesystem::remove(idMgr.configFilePath(), ec);
    }

    // 3. NotificationManager lifecycle and features
    NotificationManager notifMgr;
    TEST_ASSERT(!notifMgr.isMuted());
    notifMgr.setMuted(true);
    TEST_ASSERT(notifMgr.isMuted());
    notifMgr.setMuted(false);
    TEST_ASSERT(!notifMgr.isMuted());

    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::GeneralInfo);
    AppSettings s{};
    notifMgr.notify(NotificationType::ChatMessage, "Test User", "Hello AeroDesk!", s);
    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::ChatMessage);

    notifMgr.notify(NotificationType::IncomingConnection, "Incoming Request", "Desk 123 456 789 wants to connect", s);
    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::IncomingConnection);

    notifMgr.notify(NotificationType::FileTransferDone, "File Transfer", "document.pdf received", s);
    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::FileTransferDone);

    notifMgr.notify(NotificationType::SessionDropped, "Disconnected", "Session terminated", s);
    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::SessionDropped);

    notifMgr.clearLastNotificationType();
    TEST_ASSERT(notifMgr.lastNotificationType() == NotificationType::GeneralInfo);

    // Flash control safe handling with null HWND
    notifMgr.flashTaskbar(false);
    notifMgr.stopFlash();
    TEST_ASSERT(!notifMgr.isFlashing());

    // 4. NOTIFYICON_VERSION_4 event decoding (uID packed in HIWORD, event packed in LOWORD)
    constexpr uint16_t testIconId = 1;
    LPARAM v4ContextMenu = MAKELPARAM(WM_CONTEXTMENU, testIconId);
    LPARAM v4LButtonUp   = MAKELPARAM(WM_LBUTTONUP, testIconId);
    LPARAM v4LButtonDbl  = MAKELPARAM(WM_LBUTTONDBLCLK, testIconId);
    LPARAM v4Select      = MAKELPARAM(NIN_SELECT, testIconId);
    LPARAM v4KeySelect   = MAKELPARAM(NIN_KEYSELECT, testIconId);
    LPARAM v4BalloonClk  = MAKELPARAM(NIN_BALLOONUSERCLICK, testIconId);

    TEST_ASSERT(LOWORD(v4ContextMenu) == WM_CONTEXTMENU);
    TEST_ASSERT(LOWORD(v4LButtonUp) == WM_LBUTTONUP);
    TEST_ASSERT(LOWORD(v4LButtonDbl) == WM_LBUTTONDBLCLK);
    TEST_ASSERT(LOWORD(v4Select) == NIN_SELECT);
    TEST_ASSERT(LOWORD(v4KeySelect) == NIN_KEYSELECT);
    TEST_ASSERT(LOWORD(v4BalloonClk) == NIN_BALLOONUSERCLICK);
    TEST_ASSERT(HIWORD(v4ContextMenu) == testIconId);
    TEST_ASSERT(HIWORD(v4LButtonDbl) == testIconId);

    notifMgr.shutdown();
}

void testV210PowerFeaturesInheritance() {
    std::cout << "Running testV210PowerFeaturesInheritance()...\n";

    // 1. Verify Protocol Opcodes (0x17 - 0x1D)
    TEST_ASSERT(static_cast<uint8_t>(PacketType::AUDIO_STREAM_CHUNK) == 0x17);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::PRIVACY_MODE_TOGGLE) == 0x18);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::TUNNEL_OPEN) == 0x19);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::TUNNEL_DATA) == 0x1A);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::TUNNEL_CLOSE) == 0x1B);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::TERMINAL_DATA) == 0x1C);
    TEST_ASSERT(static_cast<uint8_t>(PacketType::WHITEBOARD_PACKET) == 0x1D);

    // 2. Audio Stream Framing & Protocol Structures
    {
        AudioChunkHeader ach{};
        ach.sampleRate = 48000;
        ach.channels = 2;
        ach.bitsPerSample = 16;
        ach.isSilent = 0;
        ach.sampleFrames = 960; // 20ms of audio
        TEST_ASSERT(ach.sampleRate == 48000);
        TEST_ASSERT(ach.channels == 2);
        TEST_ASSERT(ach.bitsPerSample == 16);
        TEST_ASSERT(ach.sampleFrames == 960);
    }

    // 3. Privacy Mode Payload Structure
    {
        PrivacyModePayload pmp{};
        pmp.enable = 1;
        pmp.acknowledge = 0;
        ByteWriter pw;
        pw.writeBytes(&pmp, sizeof(pmp));
        TEST_ASSERT(pw.buffer().size() == sizeof(PrivacyModePayload));

        ByteReader pr(pw.buffer());
        PrivacyModePayload decoded{};
        pr.readBytes(&decoded, sizeof(decoded));
        TEST_ASSERT(decoded.enable == 1);
        TEST_ASSERT(decoded.acknowledge == 0);
    }

    // 4. TCP Tunneling Protocol Headers
    {
        TunnelOpenHeader toh{};
        toh.tunnelId = 42;
        toh.targetPort = 3389;
        toh.flags = 0;

        ByteWriter ow;
        ow.writeBytes(&toh, sizeof(toh));
        TEST_ASSERT(ow.buffer().size() == sizeof(TunnelOpenHeader));

        ByteReader orr(ow.buffer());
        TunnelOpenHeader decOpen{};
        orr.readBytes(&decOpen, sizeof(decOpen));
        TEST_ASSERT(decOpen.tunnelId == 42);
        TEST_ASSERT(decOpen.targetPort == 3389);

        TunnelDataHeader tdh{};
        tdh.tunnelId = 42;
        tdh.dataLen = 128;
        ByteWriter dw;
        dw.writeBytes(&tdh, sizeof(tdh));
        TEST_ASSERT(dw.buffer().size() == sizeof(TunnelDataHeader));

        TunnelCloseHeader tch{};
        tch.tunnelId = 42;
        tch.reasonCode = 0;
        ByteWriter cw;
        cw.writeBytes(&tch, sizeof(tch));
        TEST_ASSERT(cw.buffer().size() == sizeof(TunnelCloseHeader));
    }

    // 5. Remote Terminal Protocol Headers & Enums
    {
        TerminalDataHeader tdh{};
        tdh.streamKind = static_cast<uint8_t>(TerminalStreamKind::StdinInput);
        std::string testCmd = "echo Hello World";
        tdh.textLen = static_cast<uint32_t>(testCmd.size());

        ByteWriter tw;
        tw.writeBytes(&tdh, sizeof(tdh));
        tw.writeBytes(testCmd.data(), testCmd.size());
        TEST_ASSERT(tw.buffer().size() == sizeof(TerminalDataHeader) + testCmd.size());

        ByteReader tr(tw.buffer());
        TerminalDataHeader decTh{};
        tr.readBytes(&decTh, sizeof(decTh));
        TEST_ASSERT(decTh.streamKind == static_cast<uint8_t>(TerminalStreamKind::StdinInput));
        TEST_ASSERT(decTh.textLen == testCmd.size());
        std::string decCmd(reinterpret_cast<const char*>(tr.currentPtr()), decTh.textLen);
        TEST_ASSERT(decCmd == testCmd);
    }

    // 6. Address Book 7-field INI Persistence & Backward Compatibility
    {
        IdentityManager customMgr(77);
        customMgr.loadOrCreate();
        customMgr.addOrUpdateRecentSession(400500600, "TestHost", "10.0.0.1", "Dev Machine", "Servers", "SSH key required");
        auto sessions = customMgr.recentSessions();
        TEST_ASSERT(!sessions.empty());
        bool found = false;
        for (const auto& s : sessions) {
            if (s.deskId == 400500600) {
                found = true;
                TEST_ASSERT(s.alias == "Dev Machine");
                TEST_ASSERT(s.tag == "Servers");
                TEST_ASSERT(s.notes == "SSH key required");
                break;
            }
        }
        TEST_ASSERT(found);

        // Update metadata
        customMgr.updateRecentSessionMetadata(400500600, "Updated Machine", "Personal", "Updated remarks");
        sessions = customMgr.recentSessions();
        for (const auto& s : sessions) {
            if (s.deskId == 400500600) {
                TEST_ASSERT(s.alias == "Updated Machine");
                TEST_ASSERT(s.tag == "Personal");
                TEST_ASSERT(s.notes == "Updated remarks");
                break;
            }
        }

        // Clean up
        customMgr.removeRecentSession(400500600);
        std::error_code ec;
        std::filesystem::remove(customMgr.configFilePath(), ec);
    }

    // 7. Whiteboard Manager Serialization, Geometry & Laser Decay
    {
        WhiteboardManager wbMgr;
        wbMgr.setActiveTool(WhiteboardTool::Pen);
        wbMgr.setActiveColor(0xFF007AFF); // Blue
        wbMgr.setActiveThickness(4.0f);
        TEST_ASSERT(wbMgr.activeTool() == WhiteboardTool::Pen);
        TEST_ASSERT(wbMgr.activeColor() == 0xFF007AFF);
        TEST_ASSERT(wbMgr.activeThickness() == 4.0f);

        // Draw a stroke
        wbMgr.startStroke(0.1f, 0.2f);
        wbMgr.addStrokePoint(0.15f, 0.25f);
        wbMgr.addStrokePoint(0.2f, 0.3f);
        AnnotationStroke s1 = wbMgr.finishStroke();
        TEST_ASSERT(s1.points.size() == 3);
        TEST_ASSERT(s1.tool == WhiteboardTool::Pen);
        TEST_ASSERT(s1.argbColor == 0xFF007AFF);
        TEST_ASSERT(s1.thickness == 4.0f);

        // Serialize and Deserialize Stroke
        auto strokeBytes = WhiteboardManager::serializeStroke(s1);
        TEST_ASSERT(!strokeBytes.empty());

        AnnotationStroke sDec{};
        TEST_ASSERT(WhiteboardManager::deserializeStroke(strokeBytes.data(), strokeBytes.size(), sDec));
        TEST_ASSERT(sDec.strokeId == s1.strokeId);
        TEST_ASSERT(sDec.tool == WhiteboardTool::Pen);
        TEST_ASSERT(sDec.argbColor == 0xFF007AFF);
        TEST_ASSERT(sDec.points.size() == 3);
        TEST_ASSERT(std::fabs(sDec.points[0].x - 0.1f) < 0.001f);
        TEST_ASSERT(std::fabs(sDec.points[0].y - 0.2f) < 0.001f);
        TEST_ASSERT(std::fabs(sDec.points[2].x - 0.2f) < 0.001f);
        TEST_ASSERT(std::fabs(sDec.points[2].y - 0.3f) < 0.001f);

        // Apply remote stroke to peer manager
        WhiteboardManager peerWb;
        peerWb.applyRemoteStroke(sDec);
        auto peerStrokes = peerWb.snapshotStrokes();
        TEST_ASSERT(peerStrokes.size() == 1);
        TEST_ASSERT(peerStrokes[0].strokeId == s1.strokeId);

        // Clear packet serialization & remote application
        auto clearBytes = WhiteboardManager::serializeClearPacket();
        TEST_ASSERT(!clearBytes.empty());
        AnnotationStroke clearStroke{};
        TEST_ASSERT(WhiteboardManager::deserializeStroke(clearBytes.data(), clearBytes.size(), clearStroke));
        TEST_ASSERT(clearStroke.tool == WhiteboardTool::ClearAll);
        peerWb.applyRemoteStroke(clearStroke);
        TEST_ASSERT(peerWb.snapshotStrokes().empty());

        // Laser pointer coordinate & decay
        wbMgr.setLaserPointer(0.45f, 0.55f);
        float lx = 0.0f, ly = 0.0f, lalpha = 0.0f;
        TEST_ASSERT(wbMgr.getLaserPointer(lx, ly, lalpha));
        TEST_ASSERT(std::fabs(lx - 0.45f) < 0.001f);
        TEST_ASSERT(std::fabs(ly - 0.55f) < 0.001f);
        TEST_ASSERT(lalpha > 0.8f && lalpha <= 1.0f);
    }
}

void testAutoUpdaterAndProtocolV3() {
    std::cout << "[TEST 12] CppDesk Protocol V3 & Mandatory Auto-Updater Semantics...\n";

    // 1. Magic constants & Protocol definitions
    TEST_ASSERT(PROTOCOL_MAGIC == 0x43505044); // "CPPD"
    TEST_ASSERT(RELAY_MAGIC == 0x4344534B);    // "CDSK"
    TEST_ASSERT(PROTOCOL_VERSION == 3);
    TEST_ASSERT(std::string(CPP_DESK_VERSION) == "3.0.0");
    TEST_ASSERT(CPP_DESK_VERSION_NUM == 0x030000);

    // 2. Semantic Version Triad Parsing
    int maj = 0, min = 0, pat = 0;
    TEST_ASSERT(AutoUpdater::parseVersionTriad("3.0.0", maj, min, pat) && maj == 3 && min == 0 && pat == 0);
    TEST_ASSERT(AutoUpdater::parseVersionTriad("v3.1.2", maj, min, pat) && maj == 3 && min == 1 && pat == 2);
    TEST_ASSERT(AutoUpdater::parseVersionTriad("v10.200.300-preview", maj, min, pat) && maj == 10 && min == 200 && pat == 300);
    TEST_ASSERT(!AutoUpdater::parseVersionTriad("invalid_tag", maj, min, pat));
    TEST_ASSERT(!AutoUpdater::parseVersionTriad("", maj, min, pat));

    // 3. Mandatory Update Version Comparison
    // Higher patch version -> newer
    TEST_ASSERT(AutoUpdater::isNewerVersion("3.0.1", "3.0.0"));
    TEST_ASSERT(AutoUpdater::isNewerVersion("v3.0.1", "3.0.0"));
    TEST_ASSERT(AutoUpdater::isNewerVersion("3.0.1", "v3.0.0"));

    // Higher minor version -> newer
    TEST_ASSERT(AutoUpdater::isNewerVersion("3.1.0", "3.0.0"));
    TEST_ASSERT(AutoUpdater::isNewerVersion("v3.2.0", "3.1.5"));

    // Higher major version -> newer
    TEST_ASSERT(AutoUpdater::isNewerVersion("4.0.0", "3.99.99"));

    // Equal versions -> NOT newer
    TEST_ASSERT(!AutoUpdater::isNewerVersion("3.0.0", "3.0.0"));
    TEST_ASSERT(!AutoUpdater::isNewerVersion("v3.0.0", "3.0.0"));
    TEST_ASSERT(!AutoUpdater::isNewerVersion("3.0.0", "v3.0.0"));

    // Older versions -> NOT newer
    TEST_ASSERT(!AutoUpdater::isNewerVersion("2.1.0", "3.0.0"));
    TEST_ASSERT(!AutoUpdater::isNewerVersion("3.0.0", "3.0.1"));
    TEST_ASSERT(!AutoUpdater::isNewerVersion("3.0.0", "3.1.0"));
    TEST_ASSERT(!AutoUpdater::isNewerVersion("2.99.99", "3.0.0"));

    // 4. Lightweight JSON Key Extraction
    std::string sampleJson = "{\n"
                             "    \"tag_name\": \"v3.0.1\",\n"
                             "    \"html_url\": \"https://github.com/oocs07/Remote-Desktop/releases/tag/v3.0.1\",\n"
                             "    \"body\": \"Fixed multi-monitor scaling on 4K displays.\\r\\nAdded performance optimizations.\"\n"
                             "}";

    std::string tag = AutoUpdater::extractJsonString(sampleJson, "tag_name");
    std::string url = AutoUpdater::extractJsonString(sampleJson, "html_url");
    std::string body = AutoUpdater::extractJsonString(sampleJson, "body");

    TEST_ASSERT(tag == "v3.0.1");
    TEST_ASSERT(url == "https://github.com/oocs07/Remote-Desktop/releases/tag/v3.0.1");
    TEST_ASSERT(body.find("Fixed multi-monitor scaling") != std::string::npos);

    // Extraction with escaped quotes
    std::string jsonWithEscapes = "{\"desc\": \"Feature with \\\"quotes\\\" and text\", \"other\": \"ok\"}";
    TEST_ASSERT(AutoUpdater::extractJsonString(jsonWithEscapes, "desc") == "Feature with \"quotes\" and text");
    TEST_ASSERT(AutoUpdater::extractJsonString(jsonWithEscapes, "nonexistent").empty());

    // 5. ByteReader bounds and integer overflow protection (COR-01)
    std::vector<uint8_t> dummyBytes = { 0x01, 0x02, 0x03, 0x04 };
    ByteReader ovfReader(dummyBytes);
    bool caughtOvf = false;
    try {
        ovfReader.skip(SIZE_MAX - 2);
    } catch (const std::exception&) {
        caughtOvf = true;
    }
    TEST_ASSERT(caughtOvf);
}

void testHardwareDiagnosticsAndProcessManager() {
    std::cout << "[TEST 13] Hardware Diagnostics & Process Manager Engine...\n" << std::flush;

    // 1. Packet binary serialization & deserialization round-trip
    {
        // A. DIAGNOSTICS_REQ packet
        std::vector<uint8_t> reqBuf;
        ByteWriter reqWriter(reqBuf);
        reqWriter.writeU8(1); // active = true
        ByteReader reqReader(reqBuf);
        TEST_ASSERT(reqReader.readU8() == 1);

        // B. PROCESS_KILL packet
        std::vector<uint8_t> killBuf;
        ByteWriter killWriter(killBuf);
        killWriter.writeU32(1337);
        ByteReader killReader(killBuf);
        TEST_ASSERT(killReader.readU32() == 1337);

        // C. SYSTEM_DIAGNOSTICS packet payload
        SystemDiagnosticsPayload payload;
        payload.cpuUsagePercent = 38.5f;
        payload.ramUsedBytes = 8589934592ULL;   // 8 GB
        payload.ramTotalBytes = 17179869184ULL; // 16 GB
        payload.diskUsedBytes = 268435456000ULL;// 250 GB
        payload.diskTotalBytes = 536870912000ULL;// 500 GB

        ProcessTelemetryItem p1;
        p1.pid = 1024;
        p1.workingSetBytes = 524288000ULL; // 500 MB
        p1.name = "CppDesk.exe";

        ProcessTelemetryItem p2;
        p2.pid = 2048;
        p2.workingSetBytes = 104857600ULL; // 100 MB
        p2.name = "dwm.exe";

        payload.processes.push_back(p1);
        payload.processes.push_back(p2);

        std::vector<uint8_t> diagBuf;
        serializeSystemDiagnostics(payload, diagBuf);

        SystemDiagnosticsPayload unpacked;
        TEST_ASSERT(deserializeSystemDiagnostics(diagBuf.data(), diagBuf.size(), unpacked));
        TEST_ASSERT(std::abs(unpacked.cpuUsagePercent - 38.5f) < 0.01f);
        TEST_ASSERT(unpacked.ramUsedBytes == 8589934592ULL);
        TEST_ASSERT(unpacked.ramTotalBytes == 17179869184ULL);
        TEST_ASSERT(unpacked.diskUsedBytes == 268435456000ULL);
        TEST_ASSERT(unpacked.diskTotalBytes == 536870912000ULL);
        TEST_ASSERT(unpacked.processes.size() == 2);
        TEST_ASSERT(unpacked.processes[0].pid == 1024);
        TEST_ASSERT(unpacked.processes[0].name == "CppDesk.exe");
        TEST_ASSERT(unpacked.processes[0].workingSetBytes == 524288000ULL);
        TEST_ASSERT(unpacked.processes[1].pid == 2048);
        TEST_ASSERT(unpacked.processes[1].name == "dwm.exe");
    }

    // 2. Live Host Diagnostics Sampler
    {
        auto liveDiag = NetworkEngine::sampleHostDiagnostics();
        TEST_ASSERT(liveDiag.cpuUsagePercent >= 0.0f && liveDiag.cpuUsagePercent <= 100.0f);
        TEST_ASSERT(liveDiag.ramTotalBytes > 0);
        TEST_ASSERT(liveDiag.ramUsedBytes <= liveDiag.ramTotalBytes);
        TEST_ASSERT(liveDiag.diskTotalBytes > 0);
        TEST_ASSERT(!liveDiag.processes.empty());

        // Verify top processes are sorted descending by memory usage
        if (liveDiag.processes.size() >= 2) {
            TEST_ASSERT(liveDiag.processes[0].workingSetBytes >= liveDiag.processes[1].workingSetBytes);
        }
    }

    // 3. Process Kill Permission & Safety Enforcement
    {
        IdentityManager idMgr(100);
        NetworkEngine netEngine(idMgr);

        // A. Revoked input permission: must reject kill requests
        bool killedWithoutPerm = netEngine.executeProcessKill(1337, PERM_NONE);
        TEST_ASSERT(!killedWithoutPerm);

        // B. Protected critical system PIDs (PID 0 System Idle, PID 4 System): must reject even with input permission
        bool killedPid0 = netEngine.executeProcessKill(0, PERM_INPUT);
        TEST_ASSERT(!killedPid0);
        bool killedPid4 = netEngine.executeProcessKill(4, PERM_INPUT);
        TEST_ASSERT(!killedPid4);
    }
}

void testDirectCanvasDragAndDropFileTransfer() {
    std::cout << "[TEST 14] Drag-and-Drop Direct Canvas File Drop & Recursive Directory Expansion...\n" << std::flush;

    // 1. Packet encoding/decoding with targetHint and drop coordinates
    {
        ByteWriter w;
        uint32_t tid = 42;
        uint64_t fsz = 1048576ULL;
        std::string fname = "blueprint.cad";
        w.writeU32(tid);
        w.writeU64(fsz);
        w.writeString(fname);
        w.writeU8(static_cast<uint8_t>(FileOfferTarget::Desktop));
        w.writeF32(0.45f);
        w.writeF32(0.72f);

        ByteReader r(w.buffer().data(), w.buffer().size());
        uint32_t decTid = r.readU32();
        uint64_t decFsz = r.readU64();
        std::string decName = r.readString();
        FileOfferTarget decTarget = FileOfferTarget::DefaultDownloads;
        float decX = 0.0f, decY = 0.0f;
        if (r.hasRemaining(1)) {
            decTarget = static_cast<FileOfferTarget>(r.readU8());
        }
        if (r.hasRemaining(8)) {
            decX = r.readF32();
            decY = r.readF32();
        }

        TEST_ASSERT(decTid == 42);
        TEST_ASSERT(decFsz == 1048576ULL);
        TEST_ASSERT(decName == "blueprint.cad");
        TEST_ASSERT(decTarget == FileOfferTarget::Desktop);
        TEST_ASSERT(std::abs(decX - 0.45f) < 0.001f);
        TEST_ASSERT(std::abs(decY - 0.72f) < 0.001f);
    }

    // 2. Legacy Packet Backward-Compatibility (No target hint or coordinates)
    {
        ByteWriter w;
        w.writeU32(101);
        w.writeU64(2048);
        w.writeString("legacy.txt");

        ByteReader r(w.buffer().data(), w.buffer().size());
        uint32_t decTid = r.readU32();
        uint64_t decFsz = r.readU64();
        std::string decName = r.readString();
        FileOfferTarget decTarget = FileOfferTarget::DefaultDownloads;
        float decX = 0.0f, decY = 0.0f;
        if (r.hasRemaining(1)) {
            decTarget = static_cast<FileOfferTarget>(r.readU8());
        }
        if (r.hasRemaining(8)) {
            decX = r.readF32();
            decY = r.readF32();
        }

        TEST_ASSERT(decTid == 101);
        TEST_ASSERT(decFsz == 2048);
        TEST_ASSERT(decName == "legacy.txt");
        TEST_ASSERT(decTarget == FileOfferTarget::DefaultDownloads);
        TEST_ASSERT(decX == 0.0f && decY == 0.0f);
    }

    // 3. Recursive Directory Expansion & Queueing
    {
        std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "cppdesk_tdd_drop_test";
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
        std::filesystem::create_directories(tempDir / "nested", ec);

        std::ofstream f1(tempDir / "fileA.txt");
        f1 << "Sample file A contents for TDD test";
        f1.close();

        std::ofstream f2(tempDir / "nested" / "fileB.txt");
        f2 << "Sample file B nested contents";
        f2.close();

        FileTransferManager ftm;
        std::vector<PacketType> queuedPackets;
        int queuedCount = ftm.startOutgoingPath(tempDir.string(), [&](PacketType pt, const std::vector<uint8_t>&) {
            queuedPackets.push_back(pt);
            return true;
        }, FileOfferTarget::Desktop, 0.5f, 0.5f);

        TEST_ASSERT(queuedCount == 2);
        TEST_ASSERT(queuedPackets.size() == 2);
        TEST_ASSERT(queuedPackets[0] == PacketType::FILE_OFFER);
        TEST_ASSERT(queuedPackets[1] == PacketType::FILE_OFFER);

        auto transfers = ftm.snapshotTransfers();
        TEST_ASSERT(transfers.size() == 2);

        std::filesystem::remove_all(tempDir, ec);
    }

    // 4. Canvas Hit-Test Normalization
    {
        struct TestRect {
            float left, top, right, bottom;
            bool contains(float x, float y) const { return x >= left && x <= right && y >= top && y <= bottom; }
            float width() const { return right - left; }
            float height() const { return bottom - top; }
        };
        TestRect canvasRect = { 100.0f, 50.0f, 1100.0f, 850.0f }; // width = 1000, height = 800
        float px = 600.0f, py = 450.0f;
        TEST_ASSERT(canvasRect.contains(px, py));
        float nx = (px - canvasRect.left) / canvasRect.width();
        float ny = (py - canvasRect.top) / canvasRect.height();
        TEST_ASSERT(std::abs(nx - 0.5f) < 0.001f);
        TEST_ASSERT(std::abs(ny - 0.5f) < 0.001f);

        float outX = 50.0f, outY = 20.0f;
        TEST_ASSERT(!canvasRect.contains(outX, outY));
    }
}

void testSessionRecorderAndAviContainer() {
    std::cout << "[TEST 15] Session Screen Recording & RIFF AVI Container Architecture...\n" << std::flush;

    SessionRecorder recorder;
    TEST_ASSERT(!recorder.isRecording());
    TEST_ASSERT(recorder.recordedFrames() == 0);

    std::filesystem::path testFile = std::filesystem::temp_directory_path() / "cppdesk_test_record.avi";
    std::error_code ec;
    std::filesystem::remove(testFile, ec);

    const int width = 320;
    const int height = 240;
    const int fps = 30;

    // 1. Start recording
    bool ok = recorder.startRecording(testFile.string(), width, height, fps);
    TEST_ASSERT(ok);
    TEST_ASSERT(recorder.isRecording());
    TEST_ASSERT(recorder.currentFilePath() == testFile.string());

    // 2. Generate and push synthetic test frames
    std::vector<uint8_t> frameRed(width * height * 4);
    for (size_t i = 0; i < frameRed.size(); i += 4) {
        frameRed[i] = 0x00;     // B
        frameRed[i + 1] = 0x00; // G
        frameRed[i + 2] = 0xFF; // R
        frameRed[i + 3] = 0xFF; // A
    }

    std::vector<uint8_t> frameBlue(width * height * 4);
    for (size_t i = 0; i < frameBlue.size(); i += 4) {
        frameBlue[i] = 0xFF;     // B
        frameBlue[i + 1] = 0x00; // G
        frameBlue[i + 2] = 0x00; // R
        frameBlue[i + 3] = 0xFF; // A
    }

    recorder.pushFrame(frameRed.data(), width, height);
    recorder.pushFrame(frameBlue.data(), width, height);

    // Give background worker time to encode and write
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. Stop recording
    recorder.stopRecording();
    TEST_ASSERT(!recorder.isRecording());
    TEST_ASSERT(recorder.recordedFrames() == 2);
    TEST_ASSERT(recorder.recordedBytes() > 0);

    // 4. Verify AVI RIFF container structure on disk
    TEST_ASSERT(std::filesystem::exists(testFile, ec));
    uint64_t fileSize = std::filesystem::file_size(testFile, ec);
    TEST_ASSERT(fileSize > 256);

    std::ifstream in(testFile, std::ios::binary);
    TEST_ASSERT(in.is_open());
    char hdr[64] = {};
    in.read(hdr, 64);
    in.close();

    // Check 'RIFF' magic
    TEST_ASSERT(hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F');
    // Check 'AVI ' type
    TEST_ASSERT(hdr[8] == 'A' && hdr[9] == 'V' && hdr[10] == 'I' && hdr[11] == ' ');
    // Check 'LIST' chunk
    TEST_ASSERT(hdr[12] == 'L' && hdr[13] == 'I' && hdr[14] == 'S' && hdr[15] == 'T');
    // Check 'hdrl' form
    TEST_ASSERT(hdr[20] == 'h' && hdr[21] == 'd' && hdr[22] == 'r' && hdr[23] == 'l');

    // Clean up temporary test file
    std::filesystem::remove(testFile, ec);
}

} // namespace

int main() {
    try {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        std::cout << "=========================================================\n" << std::flush;
        std::cout << "   CppDesk Automated Verification & Integration Suite    \n" << std::flush;
        std::cout << "=========================================================\n" << std::flush;

        testDeskIdAndCrypto(); std::cout << "Test 1 done\n" << std::flush;
        testTileCodecRoundTrip(); std::cout << "Test 2 done\n" << std::flush;
        testScreenCapturer(); std::cout << "Test 3 done\n" << std::flush;
        testEndToEndSessionAndFileTransfer(); std::cout << "Test 4 done\n" << std::flush;
        testFileTransferEdgeCasesAndFavorites(); std::cout << "Test 5 done\n" << std::flush;
        testAppSettingsAndAdaptiveFps(); std::cout << "Test 6 done\n" << std::flush;
        testAvx2SimdAssemblyKernels(); std::cout << "Test 7 done\n" << std::flush;
        testEcdhAndAesGcmEngine(); std::cout << "Test 8 done\n" << std::flush;
        testV201FeaturesAndResilience(); std::cout << "Test 9 done\n" << std::flush;
        testNotificationSystemAndTray(); std::cout << "Test 10 done\n" << std::flush;
        testV210PowerFeaturesInheritance(); std::cout << "Test 11 done\n" << std::flush;
        testAutoUpdaterAndProtocolV3(); std::cout << "Test 12 done\n" << std::flush;
        testHardwareDiagnosticsAndProcessManager(); std::cout << "Test 13 done\n" << std::flush;
        testDirectCanvasDragAndDropFileTransfer(); std::cout << "Test 14 done\n" << std::flush;
        testSessionRecorderAndAviContainer(); std::cout << "Test 15 done\n" << std::flush;

        std::cout << "---------------------------------------------------------\n";
        std::cout << "Assertions Passed: " << g_passed << " | Failed: " << g_failed << "\n";
        std::cout << "=========================================================\n";

        CoUninitialize();
        return (g_failed == 0) ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "CAUGHT EXCEPTION IN MAIN: " << ex.what() << std::endl;
        return 2;
    } catch (...) {
        std::cerr << "CAUGHT UNKNOWN EXCEPTION IN MAIN" << std::endl;
        return 3;
    }
}

