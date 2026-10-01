#include "network_engine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <cstring>
#include <chrono>
#include <algorithm>
#include <deque>

namespace aerodesk {

namespace {

std::once_flag g_wsaInitFlag;

void ensureWinsockInitialized() {
    std::call_once(g_wsaInitFlag, []() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}

uint64_t nowTickMs() {
    return static_cast<uint64_t>(GetTickCount64());
}

SOCKET toWinSock(uintptr_t s) {
    return (s == ~uintptr_t(0)) ? INVALID_SOCKET : static_cast<SOCKET>(s);
}

uintptr_t fromWinSock(SOCKET s) {
    return (s == INVALID_SOCKET) ? ~uintptr_t(0) : static_cast<uintptr_t>(s);
}

void closeWinSock(uintptr_t& s) {
    SOCKET ws = toWinSock(s);
    if (ws != INVALID_SOCKET) {
        shutdown(ws, SD_BOTH);
        closesocket(ws);
        s = ~uintptr_t(0);
    }
}

void setTcpNoDelay(SOCKET s) {
    BOOL flag = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

void setSocketTimeoutMs(SOCKET s, DWORD timeoutMs) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
}

bool sendAllBytes(SOCKET s, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t sentTotal = 0;
    while (sentTotal < len) {
        int chunk = static_cast<int>(std::min<size_t>(len - sentTotal, 65536));
        int n = send(s, p + sentTotal, chunk, 0);
        if (n <= 0) return false;
        sentTotal += static_cast<size_t>(n);
    }
    return true;
}

bool recvAllBytes(SOCKET s, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t gotTotal = 0;
    while (gotTotal < len) {
        int chunk = static_cast<int>(std::min<size_t>(len - gotTotal, 65536));
        int n = recv(s, p + gotTotal, chunk, 0);
        if (n <= 0) return false;
        gotTotal += static_cast<size_t>(n);
    }
    return true;
}

SOCKET connectTcpWithTimeout(const std::string& host, uint16_t port, int timeoutMs) {
    ensureWinsockInitialized();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return INVALID_SOCKET;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);

    int rc = connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            closesocket(s);
            return INVALID_SOCKET;
        }

        fd_set wfds{}, efds{};
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        FD_SET(s, &wfds);
        FD_SET(s, &efds);

        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int sel = select(0, nullptr, &wfds, &efds, &tv);
        if (sel <= 0 || FD_ISSET(s, &efds) || !FD_ISSET(s, &wfds)) {
            closesocket(s);
            return INVALID_SOCKET;
        }

        int soErr = 0;
        int soLen = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &soLen);
        if (soErr != 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
    }

    nonBlock = 0;
    ioctlsocket(s, FIONBIO, &nonBlock);
    setTcpNoDelay(s);
    return s;
}

bool parseHostPort(const std::string& input, std::string& outHost, uint16_t& outPort, uint16_t defaultPort) {
    if (input.empty()) return false;
    auto colon = input.rfind(':');
    if (colon != std::string::npos) {
        outHost = input.substr(0, colon);
        try {
            int p = std::stoi(input.substr(colon + 1));
            if (p > 0 && p <= 65535) {
                outPort = static_cast<uint16_t>(p);
                return !outHost.empty();
            }
        } catch (...) {
            return false;
        }
    }
    outHost = input;
    outPort = defaultPort;
    return !outHost.empty();
}

std::string detectPrimaryLocalIpv4() {
    ensureWinsockInitialized();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
            sockaddr_in local{};
            int len = sizeof(local);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                char ipStr[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &local.sin_addr, ipStr, sizeof(ipStr))) {
                    closesocket(s);
                    return std::string(ipStr);
                }
            }
        }
        closesocket(s);
    }
    return "127.0.0.1";
}

void spliceSockets(SOCKET a, SOCKET b) {
    char buf[32768];
    while (true) {
        int n = recv(a, buf, sizeof(buf), 0);
        if (n <= 0) break;
        if (!sendAllBytes(b, buf, static_cast<size_t>(n))) break;
    }
    shutdown(a, SD_BOTH);
    shutdown(b, SD_BOTH);
}

void executeRemoteSystemAction(SystemActionType action) {
    switch (action) {
        case SystemActionType::TaskManager:
            ShellExecuteA(nullptr, "open", "taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case SystemActionType::ShowDesktop:
            InputInjector::injectKeyEvent(VK_LWIN, 0, true, false);
            InputInjector::injectKeyEvent('D', 0, true, false);
            InputInjector::injectKeyEvent('D', 0, false, false);
            InputInjector::injectKeyEvent(VK_LWIN, 0, false, false);
            break;
        case SystemActionType::LockWorkstation:
            LockWorkStation();
            break;
        default:
            break;
    }
}

} // namespace

// ---------------- Frame IO ----------------

bool NetworkEngine::sendFrame(
    uintptr_t sock,
    PacketType type,
    uint8_t flags,
    const void* payload,
    size_t payloadLen,
    std::mutex& sendMutex,
    AesGcmSessionCipher* cipher,
    uint64_t* sendSeq)
{
    SOCKET s = toWinSock(sock);
    if (s == INVALID_SOCKET || payloadLen > MAX_PACKET_PAYLOAD_SIZE) return false;

    std::lock_guard<std::mutex> lock(sendMutex);

    FrameHeader hdr{};
    hdr.magic = PROTOCOL_MAGIC;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = flags;

    if (cipher && cipher->isInitialized() && sendSeq) {
        hdr.flags |= FLAG_ENCRYPTED;
        uint64_t seq = (*sendSeq)++;

        // Output layout: [12-byte Nonce][Ciphertext][16-byte Tag]
        hdr.payloadSize = static_cast<uint32_t>(12 + payloadLen + 16);

        std::vector<uint8_t> encrypted;
        // Authenticate with AAD = hdr
        if (!cipher->encrypt(payload, payloadLen, seq, &hdr, sizeof(hdr), encrypted)) {
            return false;
        }

        if (!sendAllBytes(s, &hdr, sizeof(hdr))) return false;
        return sendAllBytes(s, encrypted.data(), encrypted.size());
    } else {
        hdr.payloadSize = static_cast<uint32_t>(payloadLen);
        if (sendSeq) (*sendSeq)++;
        if (!sendAllBytes(s, &hdr, sizeof(hdr))) return false;
        if (payloadLen > 0 && payload != nullptr) {
            return sendAllBytes(s, payload, payloadLen);
        }
        return true;
    }
}

bool NetworkEngine::recvFrame(
    uintptr_t sock,
    FrameHeader& outHeader,
    std::vector<uint8_t>& outPayload,
    AesGcmSessionCipher* cipher,
    uint64_t* recvSeq)
{
    SOCKET s = toWinSock(sock);
    if (s == INVALID_SOCKET) return false;

    if (!recvAllBytes(s, &outHeader, sizeof(outHeader))) {
        return false;
    }
    if (outHeader.magic != PROTOCOL_MAGIC || outHeader.payloadSize > MAX_PACKET_PAYLOAD_SIZE) {
        return false;
    }

    outPayload.resize(outHeader.payloadSize);
    if (outHeader.payloadSize > 0) {
        if (!recvAllBytes(s, outPayload.data(), outHeader.payloadSize)) {
            return false;
        }
    }

    if (cipher && cipher->isInitialized() && recvSeq) {
        // Post-authentication frames must carry FLAG_ENCRYPTED
        if ((outHeader.flags & FLAG_ENCRYPTED) == 0) {
            return false;
        }
        std::vector<uint8_t> decrypted;
        uint64_t pktSeq = 0;
        // Authenticate with AAD = outHeader
        if (!cipher->decrypt(outPayload.data(), outPayload.size(), &outHeader, sizeof(outHeader), decrypted, &pktSeq)) {
            return false; // Authentication tag mismatch or replay detected!
        }
        *recvSeq = pktSeq;
        outPayload = std::move(decrypted);
    }
    return true;
}

bool NetworkEngine::sendHostEncryptedPacket(PacketType type, uint8_t flags, const void* payload, size_t payloadLen) {
    uintptr_t cs = activeHostClientSock_.load();
    if (cs == ~uintptr_t(0)) return false;
    bool enc = hostEncrypted_.load();
    std::lock_guard<std::mutex> lock(hostCipherMutex_);
    return sendFrame(cs, type, flags, payload, payloadLen, hostSendMutex_,
                     enc ? &hostCipher_ : nullptr,
                     enc ? &hostSendSeq_ : nullptr);
}

bool NetworkEngine::sendViewerEncryptedPacket(PacketType type, uint8_t flags, const void* payload, size_t payloadLen) {
    uintptr_t vs = viewerSock_.load();
    if (vs == ~uintptr_t(0)) return false;
    bool enc = viewerEncrypted_.load();
    std::lock_guard<std::mutex> lock(viewerCipherMutex_);
    return sendFrame(vs, type, flags, payload, payloadLen, viewerSendMutex_,
                     enc ? &viewerCipher_ : nullptr,
                     enc ? &viewerSendSeq_ : nullptr);
}

// ---------------- Brute-Force IP Protection ----------------

bool NetworkEngine::isIpRateLimited(const std::string& ip) {
    std::lock_guard<std::mutex> lock(authRateMutex_);
    auto it = authRateMap_.find(ip);
    if (it == authRateMap_.end()) return false;
    uint64_t now = nowTickMs();
    if (it->second.lockedUntilMs > now) {
        return true;
    }
    if (it->second.lockedUntilMs != 0 && now >= it->second.lockedUntilMs) {
        it->second = BruteForceRecord{};
    }
    return false;
}

void NetworkEngine::recordAuthResultForIp(const std::string& ip, bool success) {
    std::lock_guard<std::mutex> lock(authRateMutex_);
    if (success) {
        authRateMap_.erase(ip);
        return;
    }
    uint64_t now = nowTickMs();
    auto& rec = authRateMap_[ip];
    if (rec.windowStartMs == 0 || (now - rec.windowStartMs > 60000)) {
        rec.windowStartMs = now;
        rec.failedCount = 1;
        rec.lockedUntilMs = 0;
    } else {
        rec.failedCount++;
        if (rec.failedCount >= 5) {
            rec.lockedUntilMs = now + 60000; // 60s lockout after 5 bad password attempts
        }
    }
}

void NetworkEngine::clearRateLimitRecords() {
    std::lock_guard<std::mutex> lock(authRateMutex_);
    authRateMap_.clear();
}

// ---------------- RelayServer ----------------

RelayServer::RelayServer() = default;

RelayServer::~RelayServer() {
    stop();
}

bool RelayServer::start(uint16_t port) {
    ensureWinsockInitialized();
    if (running_.load()) return true;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return false;

    BOOL reuse = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(ls);
        return false;
    }

    port_ = port;
    listenSock_ = fromWinSock(ls);
    running_.store(true);
    acceptThread_ = std::thread(&RelayServer::acceptLoop, this);
    return true;
}

void RelayServer::stop() {
    if (!running_.exchange(false)) return;
    closeWinSock(listenSock_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& h : hosts_) {
            closeWinSock(h.controlSock);
        }
        hosts_.clear();
        for (auto& b : bridges_) {
            closeWinSock(b.viewerSock);
            closeWinSock(b.hostSock);
        }
        bridges_.clear();
    }
    bridgeCv_.notify_all();
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

size_t RelayServer::registeredPeerCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hosts_.size();
}

void RelayServer::acceptLoop() {
    while (running_.load()) {
        SOCKET ls = toWinSock(listenSock_);
        if (ls == INVALID_SOCKET) break;

        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);
        SOCKET cs = accept(ls, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (cs == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }
        setTcpNoDelay(cs);

        char ipStr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string peerIp = ipStr;

        std::thread(&RelayServer::handleClientSocket, this, fromWinSock(cs), peerIp).detach();
    }
}

void RelayServer::handleClientSocket(uintptr_t sock, std::string peerIp) {
    FrameHeader hdr{};
    std::vector<uint8_t> payload;

    setSocketTimeoutMs(toWinSock(sock), 8000);

    if (!recvAllBytes(toWinSock(sock), &hdr, sizeof(hdr)) ||
        hdr.magic != PROTOCOL_MAGIC || hdr.payloadSize > 65536) {
        closeWinSock(sock);
        return;
    }
    payload.resize(hdr.payloadSize);
    if (hdr.payloadSize > 0 && !recvAllBytes(toWinSock(sock), payload.data(), hdr.payloadSize)) {
        closeWinSock(sock);
        return;
    }

    PacketType pt = static_cast<PacketType>(hdr.type);

    if (pt == PacketType::RELAY_REGISTER) {
        try {
            ByteReader r(payload);
            uint64_t deskId = r.readU64();
            std::string hostname = r.readString();
            std::string reportedIp = r.readString();
            uint16_t tcpPort = r.readU16();

            std::string effectiveIp = (peerIp == "127.0.0.1" && !reportedIp.empty()) ? reportedIp : peerIp;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                hosts_.erase(
                    std::remove_if(hosts_.begin(), hosts_.end(), [&](RegisteredHost& h) {
                        if (h.deskId == deskId) {
                            closeWinSock(h.controlSock);
                            return true;
                        }
                        return false;
                    }),
                    hosts_.end()
                );
                RegisteredHost rh;
                rh.deskId = deskId;
                rh.hostname = hostname;
                rh.ip = effectiveIp;
                rh.tcpPort = tcpPort;
                rh.controlSock = sock;
                rh.lastSeenMs = nowTickMs();
                hosts_.push_back(rh);
            }

            ByteWriter w;
            w.writeU8(1);
            FrameHeader ackHdr{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_REGISTER_ACK), 0, static_cast<uint32_t>(w.buffer().size()) };
            sendAllBytes(toWinSock(sock), &ackHdr, sizeof(ackHdr));
            sendAllBytes(toWinSock(sock), w.buffer().data(), w.buffer().size());

            setSocketTimeoutMs(toWinSock(sock), 30000);
            while (running_.load()) {
                FrameHeader chdr{};
                if (!recvAllBytes(toWinSock(sock), &chdr, sizeof(chdr)) || chdr.magic != PROTOCOL_MAGIC) {
                    break;
                }
                std::vector<uint8_t> cpay(chdr.payloadSize);
                if (chdr.payloadSize > 0 && !recvAllBytes(toWinSock(sock), cpay.data(), chdr.payloadSize)) {
                    break;
                }
                if (static_cast<PacketType>(chdr.type) == PacketType::PING) {
                    FrameHeader pong{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::PONG), 0, 0 };
                    sendAllBytes(toWinSock(sock), &pong, sizeof(pong));
                }
            }

            std::lock_guard<std::mutex> lock(mutex_);
            hosts_.erase(
                std::remove_if(hosts_.begin(), hosts_.end(), [&](const RegisteredHost& h) {
                    return h.controlSock == sock;
                }),
                hosts_.end()
            );
        } catch (...) {}
        closeWinSock(sock);
        return;
    }

    if (pt == PacketType::RELAY_LOOKUP) {
        try {
            ByteReader r(payload);
            uint64_t targetId = r.readU64();

            bool found = false;
            std::string hostIp, hostName;
            uint16_t hostPort = 0;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& h : hosts_) {
                    if (h.deskId == targetId) {
                        found = true;
                        hostIp = h.ip;
                        hostPort = h.tcpPort;
                        hostName = h.hostname;
                        break;
                    }
                }
            }

            ByteWriter w;
            w.writeU8(found ? 1 : 0);
            w.writeString(hostIp);
            w.writeU16(hostPort);
            w.writeString(hostName);

            FrameHeader respHdr{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_LOOKUP_RESP), 0, static_cast<uint32_t>(w.buffer().size()) };
            sendAllBytes(toWinSock(sock), &respHdr, sizeof(respHdr));
            sendAllBytes(toWinSock(sock), w.buffer().data(), w.buffer().size());
        } catch (...) {}
        closeWinSock(sock);
        return;
    }

    if (pt == PacketType::RELAY_CONNECT_REQ) {
        try {
            ByteReader r(payload);
            uint64_t targetId = r.readU64();

            uintptr_t hostCtrl = ~uintptr_t(0);
            uint64_t token = 0;
            CryptoUtils::randomBytes(&token, sizeof(token));

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& h : hosts_) {
                    if (h.deskId == targetId) {
                        hostCtrl = h.controlSock;
                        break;
                    }
                }
                if (hostCtrl != ~uintptr_t(0)) {
                    PendingBridge pb;
                    pb.token = token;
                    pb.viewerSock = sock;
                    pb.ready = false;
                    bridges_.push_back(pb);
                }
            }

            if (hostCtrl == ~uintptr_t(0)) {
                ByteWriter w;
                w.writeU8(0);
                FrameHeader rh{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_BRIDGE_READY), 0, static_cast<uint32_t>(w.buffer().size()) };
                sendAllBytes(toWinSock(sock), &rh, sizeof(rh));
                sendAllBytes(toWinSock(sock), w.buffer().data(), w.buffer().size());
                closeWinSock(sock);
                return;
            }

            ByteWriter reqW;
            reqW.writeU64(token);
            FrameHeader reqH{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_INCOMING_REQ), 0, static_cast<uint32_t>(reqW.buffer().size()) };
            sendAllBytes(toWinSock(hostCtrl), &reqH, sizeof(reqH));
            sendAllBytes(toWinSock(hostCtrl), reqW.buffer().data(), reqW.buffer().size());

            uintptr_t matchedHostSock = ~uintptr_t(0);
            {
                std::unique_lock<std::mutex> lock(mutex_);
                bridgeCv_.wait_for(lock, std::chrono::milliseconds(3500), [&]() {
                    for (const auto& b : bridges_) {
                        if (b.token == token && b.ready) return true;
                    }
                    return !running_.load();
                });
                for (auto it = bridges_.begin(); it != bridges_.end(); ++it) {
                    if (it->token == token) {
                        if (it->ready) matchedHostSock = it->hostSock;
                        bridges_.erase(it);
                        break;
                    }
                }
            }

            if (matchedHostSock == ~uintptr_t(0)) {
                ByteWriter w;
                w.writeU8(0);
                FrameHeader rh{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_BRIDGE_READY), 0, static_cast<uint32_t>(w.buffer().size()) };
                sendAllBytes(toWinSock(sock), &rh, sizeof(rh));
                sendAllBytes(toWinSock(sock), w.buffer().data(), w.buffer().size());
                closeWinSock(sock);
                return;
            }

            setSocketTimeoutMs(toWinSock(sock), 0);
            setSocketTimeoutMs(toWinSock(matchedHostSock), 0);

            ByteWriter w;
            w.writeU8(1);
            FrameHeader rh{ PROTOCOL_MAGIC, static_cast<uint8_t>(PacketType::RELAY_BRIDGE_READY), 0, static_cast<uint32_t>(w.buffer().size()) };
            sendAllBytes(toWinSock(sock), &rh, sizeof(rh));
            sendAllBytes(toWinSock(sock), w.buffer().data(), w.buffer().size());

            std::thread t(spliceSockets, toWinSock(sock), toWinSock(matchedHostSock));
            spliceSockets(toWinSock(matchedHostSock), toWinSock(sock));
            if (t.joinable()) t.join();

            closeWinSock(matchedHostSock);
            closeWinSock(sock);
            return;
        } catch (...) {
            closeWinSock(sock);
            return;
        }
    }

    if (pt == PacketType::RELAY_BRIDGE_ACCEPT) {
        try {
            ByteReader r(payload);
            uint64_t token = r.readU64();
            bool matched = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& b : bridges_) {
                    if (b.token == token && !b.ready) {
                        b.hostSock = sock;
                        b.ready = true;
                        matched = true;
                        break;
                    }
                }
            }
            if (matched) {
                bridgeCv_.notify_all();
                return;
            }
        } catch (...) {}
        closeWinSock(sock);
        return;
    }

    closeWinSock(sock);
}

// ---------------- NetworkEngine ----------------

NetworkEngine::NetworkEngine(IdentityManager& identity)
    : identity_(identity)
{
    ensureWinsockInitialized();
    localIp_ = detectPrimaryLocalIpv4();
    autoAcceptIncoming_.store(identity_.settings().autoAcceptIncoming);
    autoAcceptPerms_.store(identity_.settings().defaultPermissions);
}

NetworkEngine::~NetworkEngine() {
    stop();
}

bool NetworkEngine::start() {
    if (running_.load()) return true;

    uint16_t basePort = identity_.listenPort();
    SOCKET hs = INVALID_SOCKET;
    uint16_t boundPort = basePort;

    for (uint16_t offset = 0; offset < 12; ++offset) {
        uint16_t candidate = static_cast<uint16_t>(basePort + offset);
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(candidate);

        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            listen(s, SOMAXCONN) == 0) {
            hs = s;
            boundPort = candidate;
            break;
        }
        closesocket(s);
    }

    if (hs == INVALID_SOCKET) {
        return false;
    }

    identity_.setListenPort(boundPort);
    hostListenSock_ = fromWinSock(hs);

    SOCKET us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (us != INVALID_SOCKET) {
        BOOL broadcast = TRUE;
        BOOL reuse = TRUE;
        setsockopt(us, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
        setsockopt(us, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        int slotOffset = std::clamp(static_cast<int>(boundPort) - static_cast<int>(DEFAULT_HOST_PORT), 0, 5);
        uint16_t udpPort = static_cast<uint16_t>(DEFAULT_DISCOVERY_PORT - slotOffset);

        sockaddr_in uaddr{};
        uaddr.sin_family = AF_INET;
        uaddr.sin_addr.s_addr = INADDR_ANY;
        uaddr.sin_port = htons(udpPort);
        if (bind(us, reinterpret_cast<sockaddr*>(&uaddr), sizeof(uaddr)) != 0) {
            uaddr.sin_port = 0;
            bind(us, reinterpret_cast<sockaddr*>(&uaddr), sizeof(uaddr));
        }
        udpSock_ = fromWinSock(us);
    }

    if (identity_.instanceId() == 1) {
        localRelay_.start(DEFAULT_RELAY_PORT);
    }

    running_.store(true);
    discoveryThread_ = std::thread(&NetworkEngine::discoveryLoop, this);
    relayRegThread_ = std::thread(&NetworkEngine::relayRegistrationLoop, this);
    hostAcceptThread_ = std::thread(&NetworkEngine::hostAcceptLoop, this);

    return true;
}

void NetworkEngine::stop() {
    if (!running_.exchange(false)) return;

    disconnectViewer();
    disconnectHostClient();
    respondToIncomingRequest(false, 0);

    closeWinSock(hostListenSock_);
    closeWinSock(udpSock_);
    closeWinSock(relayControlSock_);
    localRelay_.stop();

    if (discoveryThread_.joinable()) discoveryThread_.join();
    if (relayRegThread_.joinable()) relayRegThread_.join();
    if (hostAcceptThread_.joinable()) hostAcceptThread_.join();
    if (hostSessionThread_.joinable()) hostSessionThread_.join();
    if (viewerThread_.joinable()) viewerThread_.join();
}

std::vector<DiscoveredPeer> NetworkEngine::discoveredPeers() const {
    std::lock_guard<std::mutex> lock(peersMutex_);
    uint64_t now = nowTickMs();
    std::vector<DiscoveredPeer> active;
    for (const auto& p : peers_) {
        if (now - p.lastSeenTickMs <= 12000) {
            active.push_back(p);
        }
    }
    return active;
}

void NetworkEngine::sendDiscoveryQuery(uint64_t targetDeskId) {
    SOCKET us = toWinSock(udpSock_);
    if (us == INVALID_SOCKET) return;

    DiscoveryBeaconPacket pkt{};
    pkt.magic = PROTOCOL_MAGIC;
    pkt.packetKind = (targetDeskId == 0) ? 1 : 2;
    pkt.deskId = identity_.deskId();
    pkt.queryId = targetDeskId;
    pkt.tcpPort = identity_.listenPort();
    std::strncpy(pkt.hostname, identity_.hostname().c_str(), sizeof(pkt.hostname) - 1);

    for (int slot = 0; slot < 6; ++slot) {
        uint16_t port = static_cast<uint16_t>(DEFAULT_DISCOVERY_PORT - slot);

        sockaddr_in loopAddr{};
        loopAddr.sin_family = AF_INET;
        loopAddr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &loopAddr.sin_addr);
        sendto(us, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
               reinterpret_cast<sockaddr*>(&loopAddr), sizeof(loopAddr));

        sockaddr_in bcastAddr{};
        bcastAddr.sin_family = AF_INET;
        bcastAddr.sin_port = htons(port);
        bcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
        sendto(us, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
               reinterpret_cast<sockaddr*>(&bcastAddr), sizeof(bcastAddr));
    }
}

void NetworkEngine::discoveryLoop() {
    uint64_t lastBroadcast = 0;

    while (running_.load()) {
        SOCKET us = toWinSock(udpSock_);
        if (us == INVALID_SOCKET) break;

        uint64_t now = nowTickMs();
        if (now - lastBroadcast >= 1500) {
            sendDiscoveryQuery(0);
            lastBroadcast = now;
        }

        fd_set rfds{};
        FD_ZERO(&rfds);
        FD_SET(us, &rfds);
        timeval tv{ 0, 150000 };

        int sel = select(0, &rfds, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(us, &rfds)) {
            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);
            DiscoveryBeaconPacket pkt{};
            int n = recvfrom(us, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0,
                             reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
            if (n == sizeof(DiscoveryBeaconPacket) && pkt.magic == PROTOCOL_MAGIC) {
                if (pkt.deskId != identity_.deskId() && pkt.deskId >= 100000000ULL) {
                    char ipStr[INET_ADDRSTRLEN] = {};
                    inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));
                    std::string senderIp = ipStr;

                    pkt.hostname[sizeof(pkt.hostname) - 1] = '\0';

                    {
                        std::lock_guard<std::mutex> lock(peersMutex_);
                        bool updated = false;
                        for (auto& p : peers_) {
                            if (p.deskId == pkt.deskId) {
                                p.hostname = pkt.hostname;
                                p.ip = senderIp;
                                p.port = pkt.tcpPort;
                                p.lastSeenTickMs = nowTickMs();
                                p.viaRelay = false;
                                updated = true;
                                break;
                            }
                        }
                        if (!updated) {
                            DiscoveredPeer np;
                            np.deskId = pkt.deskId;
                            np.hostname = pkt.hostname;
                            np.ip = senderIp;
                            np.port = pkt.tcpPort;
                            np.lastSeenTickMs = nowTickMs();
                            np.viaRelay = false;
                            peers_.push_back(np);
                        }
                    }

                    if (pkt.packetKind == 2 && (pkt.queryId == 0 || pkt.queryId == identity_.deskId())) {
                        DiscoveryBeaconPacket reply{};
                        reply.magic = PROTOCOL_MAGIC;
                        reply.packetKind = 1;
                        reply.deskId = identity_.deskId();
                        reply.queryId = 0;
                        reply.tcpPort = identity_.listenPort();
                        std::strncpy(reply.hostname, identity_.hostname().c_str(), sizeof(reply.hostname) - 1);
                        sendto(us, reinterpret_cast<const char*>(&reply), sizeof(reply), 0,
                               reinterpret_cast<sockaddr*>(&fromAddr), sizeof(fromAddr));
                    }
                }
            }
        }
    }
}

void NetworkEngine::relayRegistrationLoop() {
    while (running_.load()) {
        std::string relayAddr = identity_.relayServerAddress();
        std::string rHost;
        uint16_t rPort = DEFAULT_RELAY_PORT;
        if (!parseHostPort(relayAddr, rHost, rPort, DEFAULT_RELAY_PORT)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }

        SOCKET rs = connectTcpWithTimeout(rHost, rPort, 1200);
        if (rs == INVALID_SOCKET) {
            for (int i = 0; i < 15 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            continue;
        }

        relayControlSock_ = fromWinSock(rs);

        ByteWriter w;
        w.writeU64(identity_.deskId());
        w.writeString(identity_.hostname());
        w.writeString(localIp_);
        w.writeU16(identity_.listenPort());

        std::mutex regMutex;
        if (!sendFrame(relayControlSock_, PacketType::RELAY_REGISTER, 0, w.buffer().data(), w.buffer().size(), regMutex)) {
            closeWinSock(relayControlSock_);
            continue;
        }

        while (running_.load()) {
            SOCKET s = toWinSock(relayControlSock_);
            if (s == INVALID_SOCKET) break;

            fd_set rfds{};
            FD_ZERO(&rfds);
            FD_SET(s, &rfds);
            timeval tv{ 2, 0 };
            int sel = select(0, &rfds, nullptr, nullptr, &tv);
            if (sel == 0) {
                if (!sendFrame(relayControlSock_, PacketType::PING, 0, nullptr, 0, regMutex)) {
                    break;
                }
                continue;
            }
            if (sel < 0) break;

            FrameHeader hdr{};
            std::vector<uint8_t> pay;
            if (!recvFrame(relayControlSock_, hdr, pay)) {
                break;
            }

            PacketType pt = static_cast<PacketType>(hdr.type);
            if (pt == PacketType::RELAY_INCOMING_REQ) {
                try {
                    ByteReader r(pay);
                    uint64_t bridgeToken = r.readU64();

                    SOCKET bs = connectTcpWithTimeout(rHost, rPort, 1500);
                    if (bs != INVALID_SOCKET) {
                        ByteWriter bw;
                        bw.writeU64(bridgeToken);
                        std::mutex bm;
                        uintptr_t bSock = fromWinSock(bs);
                        if (sendFrame(bSock, PacketType::RELAY_BRIDGE_ACCEPT, 0, bw.buffer().data(), bw.buffer().size(), bm)) {
                            if (activeHostClientSock_.load() == ~uintptr_t(0)) {
                                if (hostSessionThread_.joinable()) hostSessionThread_.join();
                                hostSessionThread_ = std::thread(&NetworkEngine::runHostSession, this, bSock, "Relay-Bridge");
                            } else {
                                closeWinSock(bSock);
                            }
                        } else {
                            closeWinSock(bSock);
                        }
                    }
                } catch (...) {}
            }
        }

        closeWinSock(relayControlSock_);
        for (int i = 0; i < 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

bool NetworkEngine::startLocalRelayServer(uint16_t port) {
    return localRelay_.start(port);
}

void NetworkEngine::stopLocalRelayServer() {
    localRelay_.stop();
}

// ---------------- Host Session Server ----------------

PendingIncomingRequest NetworkEngine::pendingIncomingRequest() const {
    std::lock_guard<std::mutex> lock(approvalMutex_);
    return pendingReq_;
}

void NetworkEngine::respondToIncomingRequest(bool accept, uint8_t permissions) {
    {
        std::lock_guard<std::mutex> lock(approvalMutex_);
        if (!pendingReq_.active) return;
        approvalDecided_ = true;
        approvalAccepted_ = accept;
        approvalPermissions_ = permissions;
        pendingReq_.active = false;
    }
    approvalCv_.notify_all();
}

void NetworkEngine::setAutoAcceptIncoming(bool autoAccept, uint8_t defaultPerms) {
    autoAcceptIncoming_.store(autoAccept);
    autoAcceptPerms_.store(defaultPerms);
}

HostSessionStatus NetworkEngine::hostSessionStatus() const {
    std::lock_guard<std::mutex> lock(hostStatusMutex_);
    return hostStatus_;
}

void NetworkEngine::updateHostSessionPermissions(uint8_t newPermissions) {
    hostLivePermissions_.store(newPermissions);
    {
        std::lock_guard<std::mutex> lock(hostStatusMutex_);
        hostStatus_.permissions = newPermissions;
    }
    ByteWriter w;
    w.writeU8(newPermissions);
    sendHostEncryptedPacket(PacketType::PERMISSION_UPDATE, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::disconnectHostClient() {
    uintptr_t cs = activeHostClientSock_.exchange(~uintptr_t(0));
    if (cs != ~uintptr_t(0)) {
        bool enc = hostEncrypted_.exchange(false);
        {
            std::lock_guard<std::mutex> lock(hostCipherMutex_);
            sendFrame(cs, PacketType::DISCONNECT, 0, nullptr, 0, hostSendMutex_,
                      enc ? &hostCipher_ : nullptr,
                      enc ? &hostSendSeq_ : nullptr);
            hostCipher_.reset();
        }
        closeWinSock(cs);
    }
}

void NetworkEngine::hostAcceptLoop() {
    while (running_.load()) {
        SOCKET ls = toWinSock(hostListenSock_);
        if (ls == INVALID_SOCKET) break;

        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);
        SOCKET cs = accept(ls, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (cs == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }
        setTcpNoDelay(cs);

        char ipStr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string clientIp = ipStr;

        uintptr_t clientSock = fromWinSock(cs);

        // Enforce per-IP brute-force lockout before spawning session thread
        if (isIpRateLimited(clientIp)) {
            ByteWriter w;
            w.writeU8(static_cast<uint8_t>(AuthResultCode::RateLimited));
            w.writeU8(0);
            w.writeString("Too many failed password attempts. Please wait 60 seconds.");
            std::mutex m;
            sendFrame(clientSock, PacketType::AUTH_RESULT, 0, w.buffer().data(), w.buffer().size(), m);
            shutdown(toWinSock(clientSock), SD_SEND);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            closeWinSock(clientSock);
            continue;
        }

        if (activeHostClientSock_.load() != ~uintptr_t(0)) {
            ByteWriter w;
            w.writeU8(static_cast<uint8_t>(AuthResultCode::HostBusy));
            w.writeU8(0);
            w.writeString("Host is already in an active remote session.");
            std::mutex m;
            sendFrame(clientSock, PacketType::AUTH_RESULT, 0, w.buffer().data(), w.buffer().size(), m);
            shutdown(toWinSock(clientSock), SD_SEND);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            closeWinSock(clientSock);
            continue;
        }

        if (hostSessionThread_.joinable()) {
            hostSessionThread_.join();
        }
        hostSessionThread_ = std::thread(&NetworkEngine::runHostSession, this, clientSock, clientIp);
    }
}

void NetworkEngine::runHostSession(uintptr_t clientSock, std::string clientIp) {
    activeHostClientSock_.store(clientSock);
    hostEncrypted_.store(false);

    auto cleanup = [&]() {
        InputInjector::releaseAllModifiers();
        fileManager_.abortActiveTransfers();
        hostEncrypted_.store(false);
        {
            std::lock_guard<std::mutex> lock(hostCipherMutex_);
            hostCipher_.reset();
        }
        uintptr_t s = activeHostClientSock_.exchange(~uintptr_t(0));
        if (s != ~uintptr_t(0)) {
            closeWinSock(s);
        } else {
            closeWinSock(clientSock);
        }
        {
            std::lock_guard<std::mutex> lock(approvalMutex_);
            pendingReq_.active = false;
        }
        {
            std::lock_guard<std::mutex> lock(hostStatusMutex_);
            hostStatus_ = HostSessionStatus{};
        }
    };

    try {
        // Apply 8-second handshake socket timeout to prevent half-open stalls
        setSocketTimeoutMs(toWinSock(clientSock), 8000);

        // Initialize Ephemeral ECDH (NIST P-256) for forward secrecy
        EcdhKeyExchange hostEcdh;
        hostEcdh.initialize();

        // 1. Receive HELLO
        FrameHeader hdr{};
        std::vector<uint8_t> payload;
        if (!recvFrame(clientSock, hdr, payload) || static_cast<PacketType>(hdr.type) != PacketType::HELLO) {
            cleanup();
            return;
        }

        ByteReader helloReader(payload);
        uint16_t protoVer = helloReader.readU16();
        uint64_t viewerId = helloReader.readU64();
        std::string viewerHostname = helloReader.readString();
        std::vector<uint8_t> viewerPubBlob;
        if (helloReader.hasRemaining(2)) {
            uint16_t pubLen = helloReader.readU16();
            if (helloReader.hasRemaining(pubLen)) {
                viewerPubBlob = helloReader.readBytesVector(pubLen);
            }
        }
        if (protoVer != PROTOCOL_VERSION) {
            cleanup();
            return;
        }

        // 2. Send AUTH_CHALLENGE (including Host Ephemeral ECDH Public Key)
        std::array<uint8_t, 32> nonce{};
        CryptoUtils::randomBytes(nonce.data(), nonce.size());

        {
            ByteWriter w;
            w.writeU64(identity_.deskId());
            w.writeString(identity_.hostname());
            w.writeBytes(nonce.data(), nonce.size());
            w.writeU8(identity_.unattendedEnabled() ? 1 : 0);
            w.writeU16(static_cast<uint16_t>(hostEcdh.localPublicKey().size()));
            if (!hostEcdh.localPublicKey().empty()) {
                w.writeBytes(hostEcdh.localPublicKey().data(), hostEcdh.localPublicKey().size());
            }
            if (!sendFrame(clientSock, PacketType::AUTH_CHALLENGE, 0, w.buffer().data(), w.buffer().size(), hostSendMutex_)) {
                cleanup();
                return;
            }
        }

        // 3. Receive AUTH_RESPONSE
        if (!recvFrame(clientSock, hdr, payload) || static_cast<PacketType>(hdr.type) != PacketType::AUTH_RESPONSE) {
            cleanup();
            return;
        }

        ByteReader authReader(payload);
        uint8_t hasPassword = authReader.readU8();
        std::array<uint8_t, 32> clientDigest{};
        authReader.readBytes(clientDigest.data(), clientDigest.size());

        bool accepted = false;
        uint8_t grantedPerms = PERM_ALL;
        AuthResultCode resultCode = AuthResultCode::RejectedByUser;
        std::string resultMsg;

        if (isIpRateLimited(clientIp)) {
            accepted = false;
            resultCode = AuthResultCode::RateLimited;
            resultMsg = "Too many failed password attempts. Locked for 60s.";
        } else if (hasPassword != 0) {
            if (identity_.verifyChallengeResponse(viewerId, nonce, clientDigest)) {
                recordAuthResultForIp(clientIp, true);
                accepted = true;
                grantedPerms = identity_.settings().defaultPermissions;
                resultCode = AuthResultCode::Accepted;
                resultMsg = "Authenticated via Encrypted Challenge-Response";
            } else {
                recordAuthResultForIp(clientIp, false);
                accepted = false;
                resultCode = isIpRateLimited(clientIp) ? AuthResultCode::RateLimited : AuthResultCode::InvalidPassword;
                resultMsg = (resultCode == AuthResultCode::RateLimited)
                    ? "Too many failed password attempts. Locked for 60s."
                    : "Invalid Unattended or Session Password";
            }
        } else if (autoAcceptIncoming_.load() || identity_.settings().autoAcceptIncoming) {
            accepted = true;
            grantedPerms = identity_.settings().defaultPermissions;
            resultCode = AuthResultCode::Accepted;
            resultMsg = "Accepted automatically";
        } else {
            // Interactive approval via UI popup modal!
            setSocketTimeoutMs(toWinSock(clientSock), 0);
            sendFrame(clientSock, PacketType::AUTH_WAITING, 0, nullptr, 0, hostSendMutex_);

            {
                std::lock_guard<std::mutex> lock(approvalMutex_);
                pendingReq_.active = true;
                pendingReq_.callerDeskId = viewerId;
                pendingReq_.callerHostname = viewerHostname;
                pendingReq_.callerIp = clientIp;
                pendingReq_.proposedPermissions = identity_.settings().defaultPermissions;
                approvalDecided_ = false;
                approvalAccepted_ = false;
                approvalPermissions_ = identity_.settings().defaultPermissions;
            }

            {
                std::unique_lock<std::mutex> lock(approvalMutex_);
                approvalCv_.wait_for(lock, std::chrono::seconds(45), [&]() {
                    return approvalDecided_ || !running_.load() || activeHostClientSock_.load() == ~uintptr_t(0);
                });
                pendingReq_.active = false;
                if (approvalDecided_ && approvalAccepted_) {
                    accepted = true;
                    grantedPerms = approvalPermissions_;
                    resultCode = AuthResultCode::Accepted;
                    resultMsg = "Accepted by Host user";
                } else {
                    accepted = false;
                    resultCode = AuthResultCode::RejectedByUser;
                    resultMsg = "Connection declined or timed out";
                }
            }
        }

        // 4. Send AUTH_RESULT (plaintext before activating stream cipher)
        {
            ByteWriter w;
            w.writeU8(static_cast<uint8_t>(resultCode));
            w.writeU8(grantedPerms);
            w.writeString(resultMsg);
            if (!sendFrame(clientSock, PacketType::AUTH_RESULT, 0, w.buffer().data(), w.buffer().size(), hostSendMutex_)) {
                cleanup();
                return;
            }
        }

        if (!accepted) {
            shutdown(toWinSock(clientSock), SD_SEND);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            cleanup();
            return;
        }

        // Clear handshake timeout & activate mandatory end-to-end AES-256-GCM AEAD encryption!
        setSocketTimeoutMs(toWinSock(clientSock), 0);
        if (!viewerPubBlob.empty()) {
            hostEcdh.computeSharedSessionKey(
                viewerPubBlob.data(), viewerPubBlob.size(),
                identity_.deskId(), viewerId, nonce, hostSessionKey_);
        } else {
            hostSessionKey_ = CryptoUtils::deriveSessionKey(identity_.deskId(), viewerId, nonce);
        }

        {
            std::lock_guard<std::mutex> lock(hostCipherMutex_);
            hostCipher_.initialize(hostSessionKey_, /*isHost=*/true);
            hostSendSeq_ = 0;
            hostEncrypted_.store(true);
        }
        uint64_t hostRecvSeq = 0;
        std::string sasFingerprint = CryptoUtils::sessionFingerprintHex(hostSessionKey_);

        hostLivePermissions_.store(grantedPerms);
        {
            std::lock_guard<std::mutex> lock(hostStatusMutex_);
            hostStatus_.active = true;
            hostStatus_.viewerDeskId = viewerId;
            hostStatus_.viewerHostname = viewerHostname;
            hostStatus_.viewerIp = clientIp;
            hostStatus_.permissions = grantedPerms;
            hostStatus_.connectedSinceTickMs = nowTickMs();
            hostStatus_.securityFingerprint = sasFingerprint;
        }

        // 5. Initialize ScreenCapturer & send encrypted VIDEO_CONFIG
        ScreenCapturer capturer;
        auto monitors = capturer.enumerateMonitors();
        capturer.selectMonitor(0);

        auto sendVideoConfig = [&]() -> bool {
            MonitorDesc cur = capturer.currentMonitor();
            ByteWriter w;
            w.writeI32(cur.index);
            w.writeI32(cur.width);
            w.writeI32(cur.height);
            w.writeU16(static_cast<uint16_t>(monitors.size()));
            for (const auto& m : monitors) {
                w.writeI32(m.index);
                w.writeI32(m.x);
                w.writeI32(m.y);
                w.writeI32(m.width);
                w.writeI32(m.height);
                w.writeU8(m.isPrimary ? 1 : 0);
                w.writeString(m.name);
            }
            return sendHostEncryptedPacket(PacketType::VIDEO_CONFIG, 0, w.buffer().data(), w.buffer().size());
        };

        if (!sendVideoConfig()) {
            cleanup();
            return;
        }

        std::atomic<bool> sessionAlive{true};
        std::atomic<uint8_t> requestedQuality{static_cast<uint8_t>(identity_.settings().defaultQuality)};
        std::atomic<uint8_t> requestedTargetFps{clampTargetFps(identity_.settings().targetFps)};
        std::atomic<bool> adaptiveFpsEnabled{identity_.settings().adaptiveFps};
        std::atomic<uint32_t> hostMeasuredRttMs{0};
        std::atomic<int> requestedMonitor{0};
        std::atomic<bool> forceKeyframeFlag{true};
        std::atomic<float> avgSendMs{4.0f};
        MonitorDesc currentMonDesc = capturer.currentMonitor();
        std::mutex monDescMutex;

        struct HostOutboxItem {
            PacketType type;
            uint8_t flags;
            std::vector<uint8_t> payload;
            bool isVideoTile = false;
        };

        std::mutex outboxMutex;
        std::condition_variable outboxCv;
        std::deque<HostOutboxItem> outbox;
        uint64_t totalBytesSent = 0;

        auto enqueueHostPacket = [&](PacketType pt, uint8_t flags, std::vector<uint8_t> payload, bool isVideo) {
            std::lock_guard<std::mutex> lk(outboxMutex);
            if (isVideo) {
                for (auto it = outbox.begin(); it != outbox.end(); ++it) {
                    if (it->isVideoTile) {
                        outbox.erase(it);
                        break;
                    }
                }
            }
            outbox.push_back({ pt, flags, std::move(payload), isVideo });
            outboxCv.notify_one();
        };

        auto sendPacketHelper = [&](PacketType pt, const std::vector<uint8_t>& buf) -> bool {
            enqueueHostPacket(pt, 0, buf, false);
            return true;
        };

        // Stage 3: Dedicated Network Send Worker Thread (Option 3A)
        std::thread sendWorkerThread([&]() {
            while (sessionAlive.load() && running_.load()) {
                HostOutboxItem item;
                {
                    std::unique_lock<std::mutex> lock(outboxMutex);
                    outboxCv.wait(lock, [&]() {
                        return !sessionAlive.load() || !running_.load() || !outbox.empty();
                    });
                    if (!sessionAlive.load() || !running_.load()) break;
                    item = std::move(outbox.front());
                    outbox.pop_front();
                }

                uint64_t sendT0 = nowTickMs();
                if (!sendHostEncryptedPacket(item.type, item.flags, item.payload.data(), item.payload.size())) {
                    sessionAlive.store(false);
                    break;
                }

                totalBytesSent += item.payload.size();
                // Periodic rekeying ratchet every 1 GB (Option 4A)
                if (totalBytesSent >= 1024ULL * 1024ULL * 1024ULL) {
                    totalBytesSent = 0;
                    std::lock_guard<std::mutex> lock(hostCipherMutex_);
                    hostCipher_.ratchetKey();
                }

                if (item.isVideoTile) {
                    float sendDur = static_cast<float>(nowTickMs() - sendT0);
                    float cur = avgSendMs.load();
                    avgSendMs.store(cur * 0.78f + sendDur * 0.22f);
                }
            }
        });

        // Spawn reader thread for low-latency encrypted input/control handling on Host
        std::thread readerThread([&]() {
            while (sessionAlive.load() && running_.load()) {
                FrameHeader rhdr{};
                std::vector<uint8_t> rpay;
                if (!recvFrame(clientSock, rhdr, rpay, &hostCipher_, &hostRecvSeq)) {
                    sessionAlive.store(false);
                    break;
                }

                PacketType rpt = static_cast<PacketType>(rhdr.type);
                uint8_t perms = hostLivePermissions_.load();

                try {
                    ByteReader r(rpay);
                    switch (rpt) {
                        case PacketType::PING: {
                            uint64_t ts = r.readU64();
                            if (r.hasRemaining(4)) {
                                hostMeasuredRttMs.store(r.readU32());
                            }
                            ByteWriter pw;
                            pw.writeU64(ts);
                            sendHostEncryptedPacket(PacketType::PONG, 0, pw.buffer().data(), pw.buffer().size());
                            break;
                        }
                        case PacketType::INPUT_MOUSE_MOVE: {
                            if (perms & PERM_INPUT) {
                                float nx = r.readF32();
                                float ny = r.readF32();
                                MonitorDesc md;
                                {
                                    std::lock_guard<std::mutex> ml(monDescMutex);
                                    md = currentMonDesc;
                                }
                                InputInjector::injectMouseMove(nx, ny, md);
                            }
                            break;
                        }
                        case PacketType::INPUT_MOUSE_BUTTON: {
                            if (perms & PERM_INPUT) {
                                uint8_t btn = r.readU8();
                                uint8_t isDown = r.readU8();
                                float nx = r.readF32();
                                float ny = r.readF32();
                                MonitorDesc md;
                                {
                                    std::lock_guard<std::mutex> ml(monDescMutex);
                                    md = currentMonDesc;
                                }
                                InputInjector::injectMouseButton(static_cast<MouseButtonId>(btn), isDown != 0, nx, ny, md);
                            }
                            break;
                        }
                        case PacketType::INPUT_MOUSE_WHEEL: {
                            if (perms & PERM_INPUT) {
                                int32_t dy = r.readI32();
                                int32_t dx = r.readI32();
                                InputInjector::injectMouseWheel(dy, dx);
                            }
                            break;
                        }
                        case PacketType::INPUT_KEY_EVENT: {
                            if (perms & PERM_INPUT) {
                                uint16_t vk = r.readU16();
                                uint16_t sc = r.readU16();
                                uint8_t isDown = r.readU8();
                                uint8_t isExt = r.readU8();
                                InputInjector::injectKeyEvent(vk, sc, isDown != 0, isExt != 0);
                            }
                            break;
                        }
                        case PacketType::INPUT_RELEASE_ALL: {
                            InputInjector::releaseAllModifiers();
                            break;
                        }
                        case PacketType::SYSTEM_ACTION: {
                            if (perms & PERM_INPUT) {
                                uint8_t act = r.readU8();
                                executeRemoteSystemAction(static_cast<SystemActionType>(act));
                            }
                            break;
                        }
                        case PacketType::VIDEO_CONTROL_REQ: {
                            uint8_t q = r.readU8();
                            int32_t monIdx = r.readI32();
                            uint8_t reqKf = r.readU8();
                            requestedQuality.store(q);
                            if (monIdx >= 0) requestedMonitor.store(monIdx);
                            if (reqKf != 0) forceKeyframeFlag.store(true);
                            if (r.hasRemaining(1)) {
                                uint8_t fpsReq = r.readU8();
                                if (fpsReq > 0) requestedTargetFps.store(clampTargetFps(fpsReq));
                            }
                            if (r.hasRemaining(1)) {
                                uint8_t adapReq = r.readU8();
                                adaptiveFpsEnabled.store(adapReq != 0);
                            }
                            break;
                        }
                        case PacketType::CLIPBOARD_TEXT: {
                            if (perms & PERM_CLIPBOARD) {
                                std::string txt = r.readString();
                                clipboardManager_.applyRemoteClipboard(txt);
                            }
                            break;
                        }
                        case PacketType::FILE_OFFER: {
                            if (perms & PERM_FILE_TRANSFER) {
                                uint32_t tid = r.readU32();
                                uint64_t fsz = r.readU64();
                                std::string fname = r.readString();
                                fileManager_.handleFileOffer(tid, fsz, fname);
                            }
                            break;
                        }
                        case PacketType::FILE_CHUNK: {
                            if (perms & PERM_FILE_TRANSFER) {
                                uint32_t tid = r.readU32();
                                uint64_t off = r.readU64();
                                uint32_t clen = r.readU32();
                                if (r.hasRemaining(clen)) {
                                    fileManager_.handleFileChunk(tid, off, r.currentPtr(), clen);
                                }
                            }
                            break;
                        }
                        case PacketType::FILE_COMPLETE: {
                            if (perms & PERM_FILE_TRANSFER) {
                                uint32_t tid = r.readU32();
                                std::string sha = r.readString();
                                fileManager_.handleFileComplete(tid, sha);
                            }
                            break;
                        }
                        case PacketType::FILE_CANCEL: {
                            uint32_t tid = r.readU32();
                            fileManager_.handleFileCancel(tid);
                            break;
                        }
                        case PacketType::CHAT_MESSAGE: {
                            std::string sender = r.readString();
                            std::string msg = r.readString();
                            if (!msg.empty()) {
                                {
                                    std::lock_guard<std::mutex> cl(chatMutex_);
                                    chatHistory_.push_back({ sender, msg, false, nowTickMs() });
                                    if (chatHistory_.size() > 100) chatHistory_.erase(chatHistory_.begin());
                                }
                                unreadChatCount_.fetch_add(1);
                            }
                            break;
                        }
                        case PacketType::DISCONNECT:
                            sessionAlive.store(false);
                            break;
                        default:
                            break;
                    }
                } catch (...) {}
            }
        });

        // Main capture & streaming loop with 15 / 30 / 60 FPS pacing + Automatic Network Congestion FPS Drop
        uint64_t lastClipboardCheck = 0;
        CursorState prevCursor{};

        while (sessionAlive.load() && running_.load() && activeHostClientSock_.load() != ~uintptr_t(0)) {
            uint64_t frameStart = nowTickMs();

            int targetMon = requestedMonitor.load();
            if (targetMon != capturer.currentMonitorIndex()) {
                capturer.selectMonitor(targetMon);
                {
                    std::lock_guard<std::mutex> ml(monDescMutex);
                    currentMonDesc = capturer.currentMonitor();
                }
                sendVideoConfig();
                forceKeyframeFlag.store(true);
            }

            uint8_t userFps = requestedTargetFps.load();
            bool adaptiveOn = adaptiveFpsEnabled.load();
            uint8_t effectiveFps = computeAdaptiveFpsCap(userFps, adaptiveOn, hostMeasuredRttMs.load(), avgSendMs.load());

            QualityPreset preset = static_cast<QualityPreset>(requestedQuality.load());
            if (adaptiveOn && effectiveFps == 15 && userFps > 15 && preset == QualityPreset::Ultra) {
                preset = QualityPreset::Balanced;
            }
            bool wantKf = forceKeyframeFlag.exchange(false);

            std::vector<EncodedTile> dirtyTiles;
            bool isKf = false;
            CursorState curState{};

            if (capturer.captureDirtyTiles(wantKf, preset, dirtyTiles, isKf, curState)) {
                // Detect dynamic desktop resolution change mid-session
                if (capturer.frameWidth() != currentMonDesc.width || capturer.frameHeight() != currentMonDesc.height) {
                    {
                        std::lock_guard<std::mutex> ml(monDescMutex);
                        currentMonDesc = capturer.currentMonitor();
                        currentMonDesc.width = capturer.frameWidth();
                        currentMonDesc.height = capturer.frameHeight();
                    }
                    sendVideoConfig();
                }

                if (!dirtyTiles.empty()) {
                    ByteWriter w;
                    w.writeU16(static_cast<uint16_t>(capturer.frameWidth()));
                    w.writeU16(static_cast<uint16_t>(capturer.frameHeight()));
                    w.writeU16(static_cast<uint16_t>(dirtyTiles.size()));
                    for (const auto& t : dirtyTiles) {
                        TileHeader th{};
                        th.x = t.x;
                        th.y = t.y;
                        th.width = t.width;
                        th.height = t.height;
                        th.encoding = static_cast<uint8_t>(t.encoding);
                        th.dataSize = static_cast<uint32_t>(t.data.size());
                        w.writeBytes(&th, sizeof(th));
                        if (!t.data.empty()) {
                            w.writeBytes(t.data.data(), t.data.size());
                        }
                    }
                    uint8_t flags = isKf ? FLAG_KEYFRAME : FLAG_NONE;
                    enqueueHostPacket(PacketType::VIDEO_FRAME_TILES, flags, w.buffer(), true);
                } else {
                    float cur = avgSendMs.load();
                    avgSendMs.store(cur * 0.92f);
                }

                if (std::abs(curState.normX - prevCursor.normX) > 0.001f ||
                    std::abs(curState.normY - prevCursor.normY) > 0.001f ||
                    curState.visible != prevCursor.visible) {
                    prevCursor = curState;
                    ByteWriter cw;
                    cw.writeF32(curState.normX);
                    cw.writeF32(curState.normY);
                    cw.writeU8(curState.visible ? 1 : 0);
                    enqueueHostPacket(PacketType::CURSOR_UPDATE, 0, cw.buffer(), false);
                }
            }

            if (hostLivePermissions_.load() & PERM_FILE_TRANSFER) {
                fileManager_.pumpOutgoingChunks(sendPacketHelper, 4);
            }

            if ((hostLivePermissions_.load() & PERM_CLIPBOARD) && (frameStart - lastClipboardCheck >= 400)) {
                lastClipboardCheck = frameStart;
                std::string newClip;
                if (clipboardManager_.pollLocalChange(newClip)) {
                    ByteWriter clipW;
                    clipW.writeString(newClip);
                    enqueueHostPacket(PacketType::CLIPBOARD_TEXT, 0, clipW.buffer(), false);
                }
            }

            uint64_t elapsed = nowTickMs() - frameStart;
            uint64_t targetInterval = (effectiveFps >= 60) ? 16 :
                                      (effectiveFps >= 30) ? 33 : 66;
            if (elapsed < targetInterval) {
                std::this_thread::sleep_for(std::chrono::milliseconds(targetInterval - elapsed));
            }
        }

        sessionAlive.store(false);
        outboxCv.notify_all();
        closeWinSock(clientSock);
        if (sendWorkerThread.joinable()) sendWorkerThread.join();
        if (readerThread.joinable()) readerThread.join();

        if (identity_.settings().lockWorkstationOnDisconnect) {
            LockWorkStation();
        }
    } catch (...) {}

    cleanup();
}

// ---------------- Viewer Session Client ----------------

bool NetworkEngine::connectToRemote(const std::string& targetIdOrAddr, const std::string& password) {
    disconnectViewer();
    if (viewerThread_.joinable()) {
        viewerThread_.join();
    }

    viewerActive_.store(true);
    viewerEncrypted_.store(false);
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        viewerStats_ = ViewerSessionStats{};
        viewerStats_.state = ViewerConnectionState::ResolvingId;
        viewerStats_.statusMessage = "Resolving address...";
        viewerStats_.qualityPreset = identity_.settings().defaultQuality;
        viewerStats_.targetFps = clampTargetFps(identity_.settings().targetFps);
        viewerStats_.effectiveFpsCap = viewerStats_.targetFps;
        viewerStats_.adaptiveFps = identity_.settings().adaptiveFps;
    }

    viewerThread_ = std::thread(&NetworkEngine::runViewerSession, this, targetIdOrAddr, password);
    return true;
}

void NetworkEngine::disconnectViewer() {
    viewerActive_.store(false);
    uintptr_t s = viewerSock_.exchange(~uintptr_t(0));
    if (s != ~uintptr_t(0)) {
        bool enc = viewerEncrypted_.exchange(false);
        {
            std::lock_guard<std::mutex> lock(viewerCipherMutex_);
            sendFrame(s, PacketType::DISCONNECT, 0, nullptr, 0, viewerSendMutex_,
                      enc ? &viewerCipher_ : nullptr,
                      enc ? &viewerSendSeq_ : nullptr);
            viewerCipher_.reset();
        }
        closeWinSock(s);
    }
    fileManager_.abortActiveTransfers();
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        if (viewerStats_.state != ViewerConnectionState::Error) {
            viewerStats_.state = ViewerConnectionState::Disconnected;
            viewerStats_.statusMessage = "Disconnected";
        }
    }
}

ViewerSessionStats NetworkEngine::viewerStats() const {
    std::lock_guard<std::mutex> lock(viewerStatsMutex_);
    return viewerStats_;
}

bool NetworkEngine::copyLatestViewerFrame(
    uint64_t& inOutSeq,
    std::vector<uint8_t>& outBgra,
    int& outW,
    int& outH,
    CursorState& outCursor) const
{
    std::lock_guard<std::mutex> lock(viewerFrameMutex_);
    outCursor = viewerCursor_;
    if (viewerFrameSeq_ == 0 || viewerCanvasW_ <= 0 || viewerCanvasH_ <= 0) {
        return false;
    }
    if (viewerFrameSeq_ == inOutSeq && outW == viewerCanvasW_ && outH == viewerCanvasH_) {
        return false;
    }
    inOutSeq = viewerFrameSeq_;
    outW = viewerCanvasW_;
    outH = viewerCanvasH_;
    outBgra = viewerCanvasBgra_;
    return true;
}

void NetworkEngine::runViewerSession(std::string targetInput, std::string password) {
    auto setStatus = [&](ViewerConnectionState st, const std::string& msg) {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        viewerStats_.state = st;
        viewerStats_.statusMessage = msg;
    };

    while (!targetInput.empty() && (targetInput.front() == ' ' || targetInput.front() == '\t')) targetInput.erase(targetInput.begin());
    while (!targetInput.empty() && (targetInput.back() == ' ' || targetInput.back() == '\t')) targetInput.pop_back();

    uint64_t targetDeskId = CryptoUtils::parseDeskId(targetInput);
    SOCKET connectedSock = INVALID_SOCKET;
    std::string resolvedIp;
    uint16_t resolvedPort = DEFAULT_HOST_PORT;

    if (targetDeskId > 0) {
        setStatus(ViewerConnectionState::ResolvingId, "Searching LAN & Relay for " + CryptoUtils::formatDeskId(targetDeskId) + "...");

        if (targetDeskId == identity_.deskId()) {
            resolvedIp = "127.0.0.1";
            resolvedPort = identity_.listenPort();
            connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
        }

        if (connectedSock == INVALID_SOCKET) {
            sendDiscoveryQuery(targetDeskId);
            for (int waitStep = 0; waitStep < 6 && viewerActive_.load(); ++waitStep) {
                {
                    std::lock_guard<std::mutex> lock(peersMutex_);
                    for (const auto& p : peers_) {
                        if (p.deskId == targetDeskId) {
                            resolvedIp = p.ip;
                            resolvedPort = p.port;
                            break;
                        }
                    }
                }
                if (!resolvedIp.empty()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }

            if (!resolvedIp.empty()) {
                setStatus(ViewerConnectionState::ConnectingTcp, "Connecting on LAN (" + resolvedIp + ":" + std::to_string(resolvedPort) + ")...");
                connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
            }
        }

        if (connectedSock == INVALID_SOCKET && viewerActive_.load()) {
            std::string rHost;
            uint16_t rPort = DEFAULT_RELAY_PORT;
            if (parseHostPort(identity_.relayServerAddress(), rHost, rPort, DEFAULT_RELAY_PORT)) {
                setStatus(ViewerConnectionState::ResolvingId, "Querying Relay Server (" + rHost + ":" + std::to_string(rPort) + ")...");
                SOCKET ls = connectTcpWithTimeout(rHost, rPort, 1500);
                if (ls != INVALID_SOCKET) {
                    uintptr_t lSock = fromWinSock(ls);
                    ByteWriter lw;
                    lw.writeU64(targetDeskId);
                    std::mutex lm;
                    if (sendFrame(lSock, PacketType::RELAY_LOOKUP, 0, lw.buffer().data(), lw.buffer().size(), lm)) {
                        FrameHeader lhdr{};
                        std::vector<uint8_t> lpay;
                        if (recvFrame(lSock, lhdr, lpay) && static_cast<PacketType>(lhdr.type) == PacketType::RELAY_LOOKUP_RESP) {
                            try {
                                ByteReader lr(lpay);
                                uint8_t found = lr.readU8();
                                std::string peerIp = lr.readString();
                                uint16_t peerPort = lr.readU16();
                                if (found && !peerIp.empty()) {
                                    resolvedIp = peerIp;
                                    resolvedPort = peerPort;
                                }
                            } catch (...) {}
                        }
                    }
                    closeWinSock(lSock);
                }

                if (!resolvedIp.empty()) {
                    setStatus(ViewerConnectionState::ConnectingTcp, "Connecting to " + resolvedIp + ":" + std::to_string(resolvedPort) + "...");
                    connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
                }

                if (connectedSock == INVALID_SOCKET && viewerActive_.load()) {
                    setStatus(ViewerConnectionState::ConnectingTcp, "Bridging via Relay Server...");
                    SOCKET bs = connectTcpWithTimeout(rHost, rPort, 1500);
                    if (bs != INVALID_SOCKET) {
                        uintptr_t bSock = fromWinSock(bs);
                        ByteWriter bw;
                        bw.writeU64(targetDeskId);
                        std::mutex bm;
                        if (sendFrame(bSock, PacketType::RELAY_CONNECT_REQ, 0, bw.buffer().data(), bw.buffer().size(), bm)) {
                            FrameHeader bhdr{};
                            std::vector<uint8_t> bpay;
                            if (recvFrame(bSock, bhdr, bpay) && static_cast<PacketType>(bhdr.type) == PacketType::RELAY_BRIDGE_READY) {
                                ByteReader br(bpay);
                                if (br.hasRemaining(1) && br.readU8() == 1) {
                                    connectedSock = bs;
                                    resolvedIp = "Relay:" + rHost;
                                    resolvedPort = rPort;
                                }
                            }
                        }
                        if (connectedSock == INVALID_SOCKET) {
                            closeWinSock(bSock);
                        }
                    }
                }
            }
        }
    } else {
        if (!parseHostPort(targetInput, resolvedIp, resolvedPort, DEFAULT_HOST_PORT)) {
            setStatus(ViewerConnectionState::Error, "Invalid Desk ID or IP:Port format.");
            viewerActive_.store(false);
            return;
        }
        setStatus(ViewerConnectionState::ConnectingTcp, "Connecting to " + resolvedIp + ":" + std::to_string(resolvedPort) + "...");
        connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 2500);
    }

    if (connectedSock == INVALID_SOCKET) {
        setStatus(ViewerConnectionState::Error, "Could not reach remote desk (" + targetInput + "). Verify the peer is online.");
        viewerActive_.store(false);
        return;
    }

    uintptr_t vSock = fromWinSock(connectedSock);
    viewerSock_.store(vSock);
    viewerEncrypted_.store(false);

    try {
        setStatus(ViewerConnectionState::Authenticating, "Performing cryptographic handshake...");

        // Initialize Ephemeral ECDH (NIST P-256) for forward secrecy (Option 1A)
        EcdhKeyExchange viewerEcdh;
        viewerEcdh.initialize();

        // 1. Send HELLO
        {
            ByteWriter w;
            w.writeU16(PROTOCOL_VERSION);
            w.writeU64(identity_.deskId());
            w.writeString(identity_.hostname());
            w.writeU16(static_cast<uint16_t>(viewerEcdh.localPublicKey().size()));
            if (!viewerEcdh.localPublicKey().empty()) {
                w.writeBytes(viewerEcdh.localPublicKey().data(), viewerEcdh.localPublicKey().size());
            }
            if (!sendFrame(vSock, PacketType::HELLO, 0, w.buffer().data(), w.buffer().size(), viewerSendMutex_)) {
                FrameHeader ehdr{};
                std::vector<uint8_t> epay;
                if (recvFrame(vSock, ehdr, epay) && static_cast<PacketType>(ehdr.type) == PacketType::AUTH_RESULT) {
                    ByteReader r(epay);
                    r.readU8(); r.readU8();
                    std::string msg = r.readString();
                    setStatus(ViewerConnectionState::Error, msg.empty() ? "Host busy or rate-limited." : msg);
                } else {
                    setStatus(ViewerConnectionState::Error, "Connection closed during handshake.");
                }
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                viewerActive_.store(false);
                return;
            }
        }

        // 2. Receive AUTH_CHALLENGE
        FrameHeader hdr{};
        std::vector<uint8_t> payload;
        if (!recvFrame(vSock, hdr, payload)) {
            setStatus(ViewerConnectionState::Error, "Failed to receive authentication challenge.");
            closeWinSock(vSock);
            viewerSock_.store(~uintptr_t(0));
            viewerActive_.store(false);
            return;
        }

        if (static_cast<PacketType>(hdr.type) == PacketType::AUTH_RESULT) {
            ByteReader r(payload);
            r.readU8();
            r.readU8();
            std::string msg = r.readString();
            setStatus(ViewerConnectionState::Error, msg.empty() ? "Host busy or rate-limited." : msg);
            closeWinSock(vSock);
            viewerSock_.store(~uintptr_t(0));
            viewerActive_.store(false);
            return;
        }

        if (static_cast<PacketType>(hdr.type) != PacketType::AUTH_CHALLENGE) {
            setStatus(ViewerConnectionState::Error, "Unexpected handshake packet.");
            closeWinSock(vSock);
            viewerSock_.store(~uintptr_t(0));
            viewerActive_.store(false);
            return;
        }

        ByteReader chalReader(payload);
        uint64_t remoteId = chalReader.readU64();
        std::string remoteHost = chalReader.readString();
        std::array<uint8_t, 32> nonce{};
        chalReader.readBytes(nonce.data(), nonce.size());
        /*uint8_t unattendedAllowed =*/ chalReader.readU8();
        std::vector<uint8_t> hostPubBlob;
        if (chalReader.hasRemaining(2)) {
            uint16_t pubLen = chalReader.readU16();
            if (chalReader.hasRemaining(pubLen)) {
                hostPubBlob = chalReader.readBytesVector(pubLen);
            }
        }

        // 3. Compute & send AUTH_RESPONSE
        {
            ByteWriter w;
            if (!password.empty()) {
                w.writeU8(1);
                auto digest = CryptoUtils::computeChallengeResponse(password, remoteId, identity_.deskId(), nonce);
                w.writeBytes(digest.data(), digest.size());
            } else {
                w.writeU8(0);
                std::array<uint8_t, 32> zero{};
                w.writeBytes(zero.data(), zero.size());
            }
            if (!sendFrame(vSock, PacketType::AUTH_RESPONSE, 0, w.buffer().data(), w.buffer().size(), viewerSendMutex_)) {
                setStatus(ViewerConnectionState::Error, "Failed to send authentication response.");
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                viewerActive_.store(false);
                return;
            }
        }

        // 4. Receive AUTH_WAITING and/or AUTH_RESULT
        while (viewerActive_.load()) {
            if (!recvFrame(vSock, hdr, payload)) {
                setStatus(ViewerConnectionState::Error, "Connection closed during authorization.");
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                viewerActive_.store(false);
                return;
            }
            PacketType pt = static_cast<PacketType>(hdr.type);
            if (pt == PacketType::AUTH_WAITING) {
                setStatus(ViewerConnectionState::WaitingApproval,
                          "Waiting for " + remoteHost + " (" + CryptoUtils::formatDeskId(remoteId) + ") to click Accept...");
                continue;
            }
            if (pt == PacketType::AUTH_RESULT) {
                ByteReader r(payload);
                AuthResultCode code = static_cast<AuthResultCode>(r.readU8());
                uint8_t perms = r.readU8();
                std::string msg = r.readString();

                if (code != AuthResultCode::Accepted) {
                    setStatus(ViewerConnectionState::Error, msg.empty() ? "Authentication rejected." : msg);
                    closeWinSock(vSock);
                    viewerSock_.store(~uintptr_t(0));
                    viewerActive_.store(false);
                    return;
                }

                // Activate mandatory end-to-end stream encryption (Option 1A)!
                if (!hostPubBlob.empty()) {
                    viewerEcdh.computeSharedSessionKey(
                        hostPubBlob.data(), hostPubBlob.size(),
                        remoteId, identity_.deskId(), nonce, viewerSessionKey_);
                } else {
                    viewerSessionKey_ = CryptoUtils::deriveSessionKey(remoteId, identity_.deskId(), nonce);
                }

                std::string sasFingerprint;
                {
                    std::lock_guard<std::mutex> lock(viewerCipherMutex_);
                    viewerCipher_.initialize(viewerSessionKey_, /*isHost=*/false);
                    viewerSendSeq_ = 0;
                    viewerEncrypted_.store(true);
                    sasFingerprint = CryptoUtils::sessionFingerprintHex(viewerSessionKey_);
                }

                {
                    std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                    viewerStats_.state = ViewerConnectionState::Connected;
                    viewerStats_.statusMessage = "Connected to " + remoteHost;
                    viewerStats_.remoteDeskId = remoteId;
                    viewerStats_.remoteHostname = remoteHost;
                    viewerStats_.remoteAddress = resolvedIp + ":" + std::to_string(resolvedPort);
                    viewerStats_.securityFingerprint = sasFingerprint;
                    viewerStats_.connectedSinceTickMs = nowTickMs();
                    viewerStats_.grantedPermissions = perms;
                }
                identity_.addOrUpdateRecentSession(remoteId, remoteHost, targetInput);

                requestVideoSettings(
                    identity_.settings().defaultQuality,
                    0,
                    true,
                    clampTargetFps(identity_.settings().targetFps),
                    identity_.settings().adaptiveFps ? 1 : 0
                );
                break;
            }
        }

        // 5. Connected! Receive encrypted video tiles, cursor updates, clipboard, files, and chat
        uint64_t viewerRecvSeq = 0;
        uint64_t lastMetricTick = nowTickMs();
        uint64_t lastPingTick = 0;
        uint32_t framesInWindow = 0;
        uint64_t bytesInWindow = 0;

        auto sendPacketHelper = [&](PacketType pt, const std::vector<uint8_t>& buf) -> bool {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        };

        while (viewerActive_.load() && running_.load()) {
            uint64_t now = nowTickMs();

            if (now - lastPingTick >= 1000) {
                lastPingTick = now;
                uint32_t curRtt = 0;
                {
                    std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                    curRtt = viewerStats_.rttMs;
                }
                ByteWriter pw;
                pw.writeU64(now);
                pw.writeU32(curRtt);
                sendViewerEncryptedPacket(PacketType::PING, 0, pw.buffer().data(), pw.buffer().size());

                std::string newClip;
                if (clipboardManager_.pollLocalChange(newClip)) {
                    ByteWriter cw;
                    cw.writeString(newClip);
                    sendViewerEncryptedPacket(PacketType::CLIPBOARD_TEXT, 0, cw.buffer().data(), cw.buffer().size());
                }
            }

            fileManager_.pumpOutgoingChunks(sendPacketHelper, 4);

            SOCKET ws = toWinSock(vSock);
            if (ws == INVALID_SOCKET) break;

            fd_set rfds{};
            FD_ZERO(&rfds);
            FD_SET(ws, &rfds);
            timeval tv{ 0, 20000 }; // 20 ms

            int sel = select(0, &rfds, nullptr, nullptr, &tv);
            if (sel < 0) break;
            if (sel == 0) continue;

            if (!recvFrame(vSock, hdr, payload, &viewerCipher_, &viewerRecvSeq)) {
                break;
            }

            bytesInWindow += sizeof(FrameHeader) + payload.size();
            PacketType pt = static_cast<PacketType>(hdr.type);

            try {
                ByteReader r(payload);
                switch (pt) {
                    case PacketType::VIDEO_CONFIG: {
                        int32_t monIdx = r.readI32();
                        int32_t fw = r.readI32();
                        int32_t fh = r.readI32();
                        uint16_t mcount = r.readU16();
                        std::vector<MonitorDesc> mons;
                        for (uint16_t i = 0; i < mcount; ++i) {
                            MonitorDesc m;
                            m.index = r.readI32();
                            m.x = r.readI32();
                            m.y = r.readI32();
                            m.width = r.readI32();
                            m.height = r.readI32();
                            m.isPrimary = (r.readU8() != 0);
                            m.name = r.readString();
                            mons.push_back(m);
                        }
                        {
                            std::lock_guard<std::mutex> lock(viewerFrameMutex_);
                            viewerCanvasW_ = fw;
                            viewerCanvasH_ = fh;
                            viewerCanvasBgra_.assign(static_cast<size_t>(fw) * fh * 4, 0);
                            viewerFrameSeq_++;
                        }
                        {
                            std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                            viewerStats_.activeMonitorIndex = monIdx;
                            viewerStats_.monitorCount = std::max<int>(1, static_cast<int>(mons.size()));
                            viewerStats_.monitors = std::move(mons);
                            viewerStats_.frameWidth = fw;
                            viewerStats_.frameHeight = fh;
                        }
                        break;
                    }
                    case PacketType::VIDEO_FRAME_TILES: {
                        uint16_t fw = r.readU16();
                        uint16_t fh = r.readU16();
                        uint16_t tileCount = r.readU16();

                        std::vector<EncodedTile> tiles;
                        tiles.reserve(tileCount);
                        for (uint16_t i = 0; i < tileCount; ++i) {
                            TileHeader th{};
                            r.readBytes(&th, sizeof(th));
                            EncodedTile et;
                            et.x = th.x;
                            et.y = th.y;
                            et.width = th.width;
                            et.height = th.height;
                            et.encoding = static_cast<TileEncoding>(th.encoding);
                            et.data.resize(th.dataSize);
                            if (th.dataSize > 0) {
                                r.readBytes(et.data.data(), th.dataSize);
                            }
                            tiles.push_back(std::move(et));
                        }

                        {
                            std::lock_guard<std::mutex> lock(viewerFrameMutex_);
                            if (viewerCanvasW_ != fw || viewerCanvasH_ != fh) {
                                viewerCanvasW_ = fw;
                                viewerCanvasH_ = fh;
                                viewerCanvasBgra_.assign(static_cast<size_t>(fw) * fh * 4, 0);
                            }
                            for (const auto& t : tiles) {
                                TileCodec::decodeTileIntoCanvas(t, viewerCanvasBgra_.data(), viewerCanvasW_, viewerCanvasH_);
                            }
                            viewerFrameSeq_++;
                        }

                        framesInWindow++;
                        uint64_t tNow = nowTickMs();
                        if (tNow - lastMetricTick >= 500) {
                            double sec = static_cast<double>(tNow - lastMetricTick) / 1000.0;
                            std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                            viewerStats_.frameWidth = fw;
                            viewerStats_.frameHeight = fh;
                            viewerStats_.fps = static_cast<float>(framesInWindow / sec);
                            viewerStats_.kbps = static_cast<float>((bytesInWindow / 1024.0) / sec);
                            framesInWindow = 0;
                            bytesInWindow = 0;
                            lastMetricTick = tNow;
                        }
                        break;
                    }
                    case PacketType::CURSOR_UPDATE: {
                        float cx = r.readF32();
                        float cy = r.readF32();
                        bool cvis = (r.readU8() != 0);
                        {
                            std::lock_guard<std::mutex> lock(viewerFrameMutex_);
                            viewerCursor_.normX = cx;
                            viewerCursor_.normY = cy;
                            viewerCursor_.visible = cvis;
                        }
                        {
                            std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                            viewerStats_.remoteCursor = { cx, cy, cvis };
                        }
                        break;
                    }
                    case PacketType::PERMISSION_UPDATE: {
                        uint8_t newPerms = r.readU8();
                        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                        viewerStats_.grantedPermissions = newPerms;
                        break;
                    }
                    case PacketType::PONG: {
                        uint64_t sentTs = r.readU64();
                        uint64_t rtt = nowTickMs() - sentTs;
                        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                        viewerStats_.rttMs = static_cast<uint32_t>(rtt);
                        uint8_t effCap = computeAdaptiveFpsCap(
                            viewerStats_.targetFps,
                            viewerStats_.adaptiveFps,
                            viewerStats_.rttMs,
                            0.0f
                        );
                        viewerStats_.effectiveFpsCap = effCap;
                        viewerStats_.networkThrottled = (effCap < viewerStats_.targetFps);
                        break;
                    }
                    case PacketType::CLIPBOARD_TEXT: {
                        std::string txt = r.readString();
                        clipboardManager_.applyRemoteClipboard(txt);
                        break;
                    }
                    case PacketType::FILE_OFFER: {
                        uint32_t tid = r.readU32();
                        uint64_t fsz = r.readU64();
                        std::string fname = r.readString();
                        fileManager_.handleFileOffer(tid, fsz, fname);
                        break;
                    }
                    case PacketType::FILE_CHUNK: {
                        uint32_t tid = r.readU32();
                        uint64_t off = r.readU64();
                        uint32_t clen = r.readU32();
                        if (r.hasRemaining(clen)) {
                            fileManager_.handleFileChunk(tid, off, r.currentPtr(), clen);
                        }
                        break;
                    }
                    case PacketType::FILE_COMPLETE: {
                        uint32_t tid = r.readU32();
                        std::string sha = r.readString();
                        fileManager_.handleFileComplete(tid, sha);
                        break;
                    }
                    case PacketType::FILE_CANCEL: {
                        uint32_t tid = r.readU32();
                        fileManager_.handleFileCancel(tid);
                        break;
                    }
                    case PacketType::CHAT_MESSAGE: {
                        std::string sender = r.readString();
                        std::string msg = r.readString();
                        if (!msg.empty()) {
                            {
                                std::lock_guard<std::mutex> cl(chatMutex_);
                                chatHistory_.push_back({ sender, msg, false, nowTickMs() });
                                if (chatHistory_.size() > 100) chatHistory_.erase(chatHistory_.begin());
                            }
                            unreadChatCount_.fetch_add(1);
                        }
                        break;
                    }
                    case PacketType::DISCONNECT:
                        viewerActive_.store(false);
                        break;
                    default:
                        break;
                }
            } catch (...) {}
        }
    } catch (...) {}

    uintptr_t s = viewerSock_.exchange(~uintptr_t(0));
    closeWinSock(s);
    viewerEncrypted_.store(false);
    {
        std::lock_guard<std::mutex> lock(viewerCipherMutex_);
        viewerCipher_.reset();
    }
    viewerActive_.store(false);
    fileManager_.abortActiveTransfers();
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        if (viewerStats_.state != ViewerConnectionState::Error) {
            viewerStats_.state = ViewerConnectionState::Disconnected;
            viewerStats_.statusMessage = "Session ended.";
        }
    }
}

// ---------------- Viewer Action Senders ----------------

void NetworkEngine::sendMouseMove(float normX, float normY) {
    ByteWriter w;
    w.writeF32(normX);
    w.writeF32(normY);
    sendViewerEncryptedPacket(PacketType::INPUT_MOUSE_MOVE, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::sendMouseButton(MouseButtonId button, bool isDown, float normX, float normY) {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(button));
    w.writeU8(isDown ? 1 : 0);
    w.writeF32(normX);
    w.writeF32(normY);
    sendViewerEncryptedPacket(PacketType::INPUT_MOUSE_BUTTON, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::sendMouseWheel(int32_t verticalDelta, int32_t horizontalDelta) {
    ByteWriter w;
    w.writeI32(verticalDelta);
    w.writeI32(horizontalDelta);
    sendViewerEncryptedPacket(PacketType::INPUT_MOUSE_WHEEL, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::sendKeyEvent(uint16_t vkCode, uint16_t scanCode, bool isDown, bool isExtended) {
    ByteWriter w;
    w.writeU16(vkCode);
    w.writeU16(scanCode);
    w.writeU8(isDown ? 1 : 0);
    w.writeU8(isExtended ? 1 : 0);
    sendViewerEncryptedPacket(PacketType::INPUT_KEY_EVENT, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::sendReleaseAllModifiers() {
    sendViewerEncryptedPacket(PacketType::INPUT_RELEASE_ALL, 0, nullptr, 0);
}

void NetworkEngine::sendSystemAction(SystemActionType action) {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(action));
    sendViewerEncryptedPacket(PacketType::SYSTEM_ACTION, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::requestVideoSettings(QualityPreset preset, int monitorIndex, bool forceKeyframe, uint8_t targetFps, int adaptiveFps) {
    uint8_t sendFps = 30;
    uint8_t sendAdap = 1;
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        viewerStats_.qualityPreset = preset;
        if (targetFps > 0) {
            viewerStats_.targetFps = clampTargetFps(targetFps);
        }
        if (adaptiveFps >= 0) {
            viewerStats_.adaptiveFps = (adaptiveFps != 0);
        }
        viewerStats_.effectiveFpsCap = computeAdaptiveFpsCap(
            viewerStats_.targetFps,
            viewerStats_.adaptiveFps,
            viewerStats_.rttMs,
            0.0f
        );
        viewerStats_.networkThrottled = (viewerStats_.effectiveFpsCap < viewerStats_.targetFps);
        sendFps = viewerStats_.targetFps;
        sendAdap = viewerStats_.adaptiveFps ? 1 : 0;
    }
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(preset));
    w.writeI32(monitorIndex);
    w.writeU8(forceKeyframe ? 1 : 0);
    w.writeU8(sendFps);
    w.writeU8(sendAdap);
    sendViewerEncryptedPacket(PacketType::VIDEO_CONTROL_REQ, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::setSessionFpsConfig(uint8_t targetFps, bool adaptiveFps) {
    QualityPreset curPreset = QualityPreset::Balanced;
    int curMon = 0;
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        curPreset = viewerStats_.qualityPreset;
        curMon = viewerStats_.activeMonitorIndex;
    }
    requestVideoSettings(curPreset, curMon, false, clampTargetFps(targetFps), adaptiveFps ? 1 : 0);
}

uint32_t NetworkEngine::sendFile(const std::string& filePath) {
    uintptr_t vSock = viewerSock_.load();
    uintptr_t hSock = activeHostClientSock_.load();

    if (vSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingFile(filePath, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        });
    } else if (hSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingFile(filePath, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendHostEncryptedPacket(pt, 0, buf.data(), buf.size());
        });
    }
    return 0;
}

bool NetworkEngine::cancelFileTransfer(uint32_t transferId) {
    uintptr_t vSock = viewerSock_.load();
    uintptr_t hSock = activeHostClientSock_.load();

    if (vSock != ~uintptr_t(0)) {
        return fileManager_.cancelTransfer(transferId, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        });
    } else if (hSock != ~uintptr_t(0)) {
        return fileManager_.cancelTransfer(transferId, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendHostEncryptedPacket(pt, 0, buf.data(), buf.size());
        });
    }
    return fileManager_.cancelTransfer(transferId, nullptr);
}

void NetworkEngine::pushLocalClipboardNow() {
    std::string txt = ClipboardManager::getClipboardUtf8();
    if (txt.empty()) return;

    ByteWriter w;
    w.writeString(txt);

    if (viewerSock_.load() != ~uintptr_t(0)) {
        sendViewerEncryptedPacket(PacketType::CLIPBOARD_TEXT, 0, w.buffer().data(), w.buffer().size());
    }
    if (activeHostClientSock_.load() != ~uintptr_t(0)) {
        sendHostEncryptedPacket(PacketType::CLIPBOARD_TEXT, 0, w.buffer().data(), w.buffer().size());
    }
}

bool NetworkEngine::sendChatMessage(const std::string& text) {
    if (text.empty() || text.size() > 2048) return false;

    ByteWriter w;
    w.writeString(identity_.hostname());
    w.writeString(text);

    bool sent = false;
    if (viewerSock_.load() != ~uintptr_t(0)) {
        if (sendViewerEncryptedPacket(PacketType::CHAT_MESSAGE, 0, w.buffer().data(), w.buffer().size())) {
            sent = true;
        }
    }
    if (activeHostClientSock_.load() != ~uintptr_t(0)) {
        if (sendHostEncryptedPacket(PacketType::CHAT_MESSAGE, 0, w.buffer().data(), w.buffer().size())) {
            sent = true;
        }
    }

    if (sent) {
        std::lock_guard<std::mutex> lock(chatMutex_);
        chatHistory_.push_back({ "You (" + identity_.hostname() + ")", text, true, nowTickMs() });
        if (chatHistory_.size() > 100) chatHistory_.erase(chatHistory_.begin());
    }
    return sent;
}

std::vector<ChatMessageEntry> NetworkEngine::chatMessages() const {
    std::lock_guard<std::mutex> lock(chatMutex_);
    return chatHistory_;
}

} // namespace aerodesk
