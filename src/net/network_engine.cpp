#include "network_engine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cstring>
#include <chrono>
#include <algorithm>
#include <deque>

namespace cppdesk {

namespace {

#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif
#ifndef AUDCLNT_BUFFERFLAGS_SILENT
#define AUDCLNT_BUFFERFLAGS_SILENT 0x2
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

static const CLSID CLSID_MMDeviceEnumerator_val = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const IID IID_IMMDeviceEnumerator_val = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const IID IID_IAudioClient_val = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static const IID IID_IAudioCaptureClient_val = { 0xc8adbd64, 0xe71e, 0x48a0, { 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17 } };
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_val = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

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

static LRESULT CALLBACK PrivacyCurtainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bgBrush = CreateSolidBrush(RGB(11, 14, 20));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(229, 9, 20));
            HFONT hFontTitle = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFontTitle);

            RECT titleRc = rc;
            titleRc.bottom = rc.top + (rc.bottom - rc.top) / 2;
            DrawTextW(hdc, L"CppDesk Privacy Mode Active", -1, &titleRc, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);

            HFONT hFontSub = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            SelectObject(hdc, hFontSub);
            SetTextColor(hdc, RGB(200, 210, 225));

            RECT subRc = rc;
            subRc.top = titleRc.bottom + 16;
            subRc.bottom = subRc.top + 30;
            DrawTextW(hdc, L"Screen output hidden and local physical inputs secured for authorized remote administration.", -1, &subRc, DT_CENTER | DT_TOP | DT_SINGLELINE);

            RECT hintRc = rc;
            hintRc.top = subRc.bottom + 12;
            hintRc.bottom = hintRc.top + 30;
            SetTextColor(hdc, RGB(130, 145, 165));
            DrawTextW(hdc, L"Host emergency failsafe: Press Ctrl+Alt+Del on host to release control.", -1, &hintRc, DT_CENTER | DT_TOP | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFontTitle);
            DeleteObject(hFontSub);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(nullptr);
            return TRUE;
        case WM_CLOSE:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
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
    InputInjector::executeSystemAction(action);
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

    stopHostAudioCapture();
    destroyPrivacyCurtainWindow();
    shutdownViewerAudioPlayback();

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
    stopVoiceIntercom();
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
        stopHostAudioCapture();
        destroyPrivacyCurtainWindow();
        stopHostTerminal();
        stopHostTunnelProxy();
        whiteboardMgr_.hideHostOverlay();
        whiteboardMgr_.clearAllStrokes();
        InputInjector::releaseAllModifiers();
        fileManager_.abortActiveTransfers();
        hostEncrypted_.store(false);
        {
            std::lock_guard<std::mutex> lock(hostCipherMutex_);
            hostCipher_.reset();
            CryptoUtils::secureZero(hostSessionKey_.data(), hostSessionKey_.size());
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

        startHostAudioCapture();
        if (grantedPerms & PERM_INPUT) {
            startHostTerminal(false);
            startHostTunnelProxy();
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
                        case PacketType::MONITOR_SELECT: {
                            int32_t monIdx = r.readI32();
                            if (monIdx >= 0) {
                                requestedMonitor.store(monIdx);
                                forceKeyframeFlag.store(true);
                            }
                            break;
                        }
                        case PacketType::QUALITY_UPDATE: {
                            uint8_t q = r.readU8();
                            uint8_t fps = r.readU8();
                            uint8_t adap = r.readU8();
                            requestedQuality.store(q);
                            if (fps > 0) requestedTargetFps.store(clampTargetFps(fps));
                            adaptiveFpsEnabled.store(adap != 0);
                            forceKeyframeFlag.store(true);
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
                                if (clipboardSyncEnabled_.load()) {
                                    clipboardManager_.applyRemoteClipboard(txt);
                                }
                            }
                            break;
                        }
                        case PacketType::FILE_OFFER: {
                            if (perms & PERM_FILE_TRANSFER) {
                                uint32_t tid = r.readU32();
                                uint64_t fsz = r.readU64();
                                std::string fname = r.readString();
                                FileOfferTarget targetHint = FileOfferTarget::DefaultDownloads;
                                float dropNx = 0.0f, dropNy = 0.0f;
                                if (r.hasRemaining(1)) {
                                    targetHint = static_cast<FileOfferTarget>(r.readU8());
                                }
                                if (r.hasRemaining(8)) {
                                    dropNx = r.readF32();
                                    dropNy = r.readF32();
                                }
                                fileManager_.handleFileOffer(tid, fsz, fname, targetHint, dropNx, dropNy);
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
                        case PacketType::PRIVACY_MODE_TOGGLE: {
                            if (perms & PERM_INPUT) {
                                if (r.hasRemaining(sizeof(PrivacyModePayload))) {
                                    PrivacyModePayload p{};
                                    r.readBytes(&p, sizeof(p));
                                    setHostPrivacyMode(p.enable != 0);

                                    PrivacyModePayload ack{};
                                    ack.enable = isHostPrivacyModeActive() ? 1 : 0;
                                    ack.acknowledge = 1;
                                    ByteWriter w;
                                    w.writeBytes(&ack, sizeof(ack));
                                    sendHostEncryptedPacket(PacketType::PRIVACY_MODE_TOGGLE, 0, w.buffer().data(), w.buffer().size());
                                }
                            }
                            break;
                        }
                        case PacketType::VOICE_INTERCOM_CHUNK: {
                            handleIncomingVoiceChunk(payload.data(), payload.size());
                            break;
                        }
                        case PacketType::TUNNEL_OPEN: {
                            if (r.hasRemaining(sizeof(TunnelOpenHeader))) {
                                TunnelOpenHeader toh{};
                                r.readBytes(&toh, sizeof(toh));

                                bool permitted = (perms & PERM_INPUT) != 0;
                                SOCKET ts = INVALID_SOCKET;
                                bool ok = false;
                                if (permitted) {
                                    ts = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                                    if (ts != INVALID_SOCKET) {
                                        sockaddr_in targetAddr{};
                                        targetAddr.sin_family = AF_INET;
                                        targetAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                                        targetAddr.sin_port = htons(toh.targetPort);
                                        if (connect(ts, reinterpret_cast<sockaddr*>(&targetAddr), sizeof(targetAddr)) == 0) {
                                            u_long nonblock = 1;
                                            ioctlsocket(ts, FIONBIO, &nonblock);
                                            ok = true;
                                            std::lock_guard<std::mutex> lk(tunnelMutex_);
                                            hostTunnels_[toh.tunnelId] = { toh.tunnelId, 0, ts, toh.targetPort };
                                        } else {
                                            closesocket(ts);
                                        }
                                    }
                                }
                                if (!ok) {
                                    TunnelCloseHeader tch{};
                                    tch.tunnelId = toh.tunnelId;
                                    tch.reasonCode = permitted ? 1 : 2; // Refused / Forbidden
                                    ByteWriter w;
                                    w.writeBytes(&tch, sizeof(tch));
                                    sendHostEncryptedPacket(PacketType::TUNNEL_CLOSE, 0, w.buffer().data(), w.buffer().size());
                                }
                            }
                            break;
                        }
                        case PacketType::TUNNEL_DATA: {
                            if (perms & PERM_INPUT) {
                                if (r.hasRemaining(sizeof(TunnelDataHeader))) {
                                    TunnelDataHeader tdh{};
                                    r.readBytes(&tdh, sizeof(tdh));
                                    if (r.hasRemaining(tdh.dataLen)) {
                                        SOCKET ts = INVALID_SOCKET;
                                        {
                                            std::lock_guard<std::mutex> lk(tunnelMutex_);
                                            auto it = hostTunnels_.find(tdh.tunnelId);
                                            if (it != hostTunnels_.end()) {
                                                ts = it->second.sock;
                                            }
                                        }
                                        if (ts != INVALID_SOCKET) {
                                            sendAllBytes(ts, r.currentPtr(), tdh.dataLen);
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        case PacketType::TUNNEL_CLOSE: {
                            if (r.hasRemaining(sizeof(TunnelCloseHeader))) {
                                TunnelCloseHeader tch{};
                                r.readBytes(&tch, sizeof(tch));
                                std::lock_guard<std::mutex> lk(tunnelMutex_);
                                auto it = hostTunnels_.find(tch.tunnelId);
                                if (it != hostTunnels_.end()) {
                                    closesocket(it->second.sock);
                                    hostTunnels_.erase(it);
                                }
                            }
                            break;
                        }
                        case PacketType::TERMINAL_DATA: {
                            if (perms & PERM_INPUT) {
                                if (r.hasRemaining(sizeof(TerminalDataHeader))) {
                                    TerminalDataHeader tdh{};
                                    r.readBytes(&tdh, sizeof(tdh));
                                    if (tdh.streamKind == static_cast<uint8_t>(TerminalStreamKind::StdinInput)) {
                                        if (r.hasRemaining(tdh.textLen)) {
                                            std::string input(reinterpret_cast<const char*>(r.currentPtr()), tdh.textLen);
                                            injectHostTerminalStdin(input);
                                        }
                                    } else if (tdh.streamKind == static_cast<uint8_t>(TerminalStreamKind::ResetShell)) {
                                        startHostTerminal(false);
                                    } else if (tdh.streamKind == static_cast<uint8_t>(TerminalStreamKind::SwitchShell)) {
                                        startHostTerminal(true);
                                    }
                                }
                            }
                            break;
                        }
                        case PacketType::WHITEBOARD_PACKET: {
                            AnnotationStroke stroke{};
                            if (WhiteboardManager::deserializeStroke(r.currentPtr(), r.remaining(), stroke)) {
                                whiteboardMgr_.showHostOverlay();
                                whiteboardMgr_.applyRemoteStroke(stroke);
                            }
                            break;
                        }
                        case PacketType::DIAGNOSTICS_REQ: {
                            uint8_t act = r.readU8();
                            hostDiagnosticsStreamActive_.store(act != 0);
                            if (act != 0) {
                                auto diag = sampleHostDiagnostics();
                                std::vector<uint8_t> payload;
                                serializeSystemDiagnostics(diag, payload);
                                sendHostEncryptedPacket(PacketType::SYSTEM_DIAGNOSTICS, FLAG_ENCRYPTED, payload.data(), payload.size());
                            }
                            break;
                        }
                        case PacketType::PROCESS_KILL: {
                            uint32_t pid = r.readU32();
                            if (executeProcessKill(pid, perms)) {
                                auto diag = sampleHostDiagnostics();
                                std::vector<uint8_t> payload;
                                serializeSystemDiagnostics(diag, payload);
                                sendHostEncryptedPacket(PacketType::SYSTEM_DIAGNOSTICS, FLAG_ENCRYPTED, payload.data(), payload.size());
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
        uint64_t lastDiagnosticsSend = 0;
        CursorState prevCursor{};

        while (sessionAlive.load() && running_.load() && activeHostClientSock_.load() != ~uintptr_t(0)) {
            uint64_t frameStart = nowTickMs();

            if (hostDiagnosticsStreamActive_.load()) {
                if (frameStart - lastDiagnosticsSend >= 1500) {
                    lastDiagnosticsSend = frameStart;
                    auto diag = sampleHostDiagnostics();
                    std::vector<uint8_t> payload;
                    serializeSystemDiagnostics(diag, payload);
                    sendHostEncryptedPacket(PacketType::SYSTEM_DIAGNOSTICS, FLAG_ENCRYPTED, payload.data(), payload.size());
                }
            }

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

            if (clipboardSyncEnabled_.load() && (hostLivePermissions_.load() & PERM_CLIPBOARD) && (frameStart - lastClipboardCheck >= 400)) {
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
    shutdownViewerAudioPlayback();
    stopVoiceIntercom();
    stopViewerTunnelMultiplexer();
    viewerPrivacyModeActive_.store(false);
    whiteboardMgr_.clearAllStrokes();
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
    ViewerSessionStats s = viewerStats_;
    s.privacyModeEngaged = viewerPrivacyModeActive_.load();
    s.audioMuted = audioMuted_.load();
    s.audioVolume = audioVolumePercent_.load();
    return s;
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
    std::string resolvedIp;
    uint16_t resolvedPort = DEFAULT_HOST_PORT;

    auto doConnectAndAuth = [&](bool isReconnecting) -> bool {
        SOCKET connectedSock = INVALID_SOCKET;

        if (targetDeskId > 0) {
            if (isReconnecting && !resolvedIp.empty() && resolvedIp.rfind("Relay:", 0) != 0) {
                connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1200);
            }

            if (connectedSock == INVALID_SOCKET) {
                if (targetDeskId == identity_.deskId()) {
                    resolvedIp = "127.0.0.1";
                    resolvedPort = identity_.listenPort();
                    connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
                }
            }

            if (connectedSock == INVALID_SOCKET) {
                sendDiscoveryQuery(targetDeskId);
                for (int waitStep = 0; waitStep < (isReconnecting ? 4 : 6) && viewerActive_.load(); ++waitStep) {
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
                    if (!isReconnecting) {
                        setStatus(ViewerConnectionState::ConnectingTcp, "Connecting on LAN (" + resolvedIp + ":" + std::to_string(resolvedPort) + ")...");
                    }
                    connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
                }
            }

            if (connectedSock == INVALID_SOCKET && viewerActive_.load()) {
                std::string rHost;
                uint16_t rPort = DEFAULT_RELAY_PORT;
                if (parseHostPort(identity_.relayServerAddress(), rHost, rPort, DEFAULT_RELAY_PORT)) {
                    if (!isReconnecting) {
                        setStatus(ViewerConnectionState::ResolvingId, "Querying Relay Server (" + rHost + ":" + std::to_string(rPort) + ")...");
                    }
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
                        if (!isReconnecting) {
                            setStatus(ViewerConnectionState::ConnectingTcp, "Connecting to " + resolvedIp + ":" + std::to_string(resolvedPort) + "...");
                        }
                        connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 1500);
                    }

                    if (connectedSock == INVALID_SOCKET && viewerActive_.load()) {
                        if (!isReconnecting) {
                            setStatus(ViewerConnectionState::ConnectingTcp, "Bridging via Relay Server...");
                        }
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
                if (!isReconnecting) {
                    setStatus(ViewerConnectionState::Error, "Invalid Desk ID or IP:Port format.");
                }
                return false;
            }
            if (!isReconnecting) {
                setStatus(ViewerConnectionState::ConnectingTcp, "Connecting to " + resolvedIp + ":" + std::to_string(resolvedPort) + "...");
            }
            connectedSock = connectTcpWithTimeout(resolvedIp, resolvedPort, 2500);
        }

        if (connectedSock == INVALID_SOCKET) {
            if (!isReconnecting) {
                setStatus(ViewerConnectionState::Error, "Could not reach remote desk (" + targetInput + "). Verify the peer is online.");
            }
            return false;
        }

        uintptr_t vSock = fromWinSock(connectedSock);
        viewerSock_.store(vSock);
        viewerEncrypted_.store(false);

        try {
            if (!isReconnecting) {
                setStatus(ViewerConnectionState::Authenticating, "Performing cryptographic handshake...");
            }

            // Initialize Ephemeral ECDH (NIST P-256) for forward secrecy
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
                        if (!isReconnecting) {
                            setStatus(ViewerConnectionState::Error, msg.empty() ? "Host busy or rate-limited." : msg);
                        }
                    } else if (!isReconnecting) {
                        setStatus(ViewerConnectionState::Error, "Connection closed during handshake.");
                    }
                    closeWinSock(vSock);
                    viewerSock_.store(~uintptr_t(0));
                    return false;
                }
            }

            // 2. Receive AUTH_CHALLENGE
            FrameHeader hdr{};
            std::vector<uint8_t> payload;
            if (!recvFrame(vSock, hdr, payload)) {
                if (!isReconnecting) {
                    setStatus(ViewerConnectionState::Error, "Failed to receive authentication challenge.");
                }
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                return false;
            }

            if (static_cast<PacketType>(hdr.type) == PacketType::AUTH_RESULT) {
                ByteReader r(payload);
                r.readU8(); r.readU8();
                std::string msg = r.readString();
                if (!isReconnecting) {
                    setStatus(ViewerConnectionState::Error, msg.empty() ? "Host busy or rate-limited." : msg);
                }
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                return false;
            }

            if (static_cast<PacketType>(hdr.type) != PacketType::AUTH_CHALLENGE) {
                if (!isReconnecting) {
                    setStatus(ViewerConnectionState::Error, "Unexpected handshake packet.");
                }
                closeWinSock(vSock);
                viewerSock_.store(~uintptr_t(0));
                return false;
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
                    if (!isReconnecting) {
                        setStatus(ViewerConnectionState::Error, "Failed to send authentication response.");
                    }
                    closeWinSock(vSock);
                    viewerSock_.store(~uintptr_t(0));
                    return false;
                }
            }

            // 4. Receive AUTH_WAITING and/or AUTH_RESULT
            while (viewerActive_.load()) {
                if (!recvFrame(vSock, hdr, payload)) {
                    if (!isReconnecting) {
                        setStatus(ViewerConnectionState::Error, "Connection closed during authorization.");
                    }
                    closeWinSock(vSock);
                    viewerSock_.store(~uintptr_t(0));
                    return false;
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
                        if (!isReconnecting) {
                            setStatus(ViewerConnectionState::Error, msg.empty() ? "Authentication rejected." : msg);
                        }
                        closeWinSock(vSock);
                        viewerSock_.store(~uintptr_t(0));
                        return false;
                    }

                    // Activate mandatory end-to-end stream encryption
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
                        viewerStats_.statusMessage = isReconnecting ? ("Reconnected to " + remoteHost) : ("Connected to " + remoteHost);
                        viewerStats_.reconnectAttempt = 0;
                        viewerStats_.remoteDeskId = remoteId;
                        viewerStats_.remoteHostname = remoteHost;
                        viewerStats_.remoteAddress = resolvedIp + ":" + std::to_string(resolvedPort);
                        viewerStats_.securityFingerprint = sasFingerprint;
                        if (!isReconnecting) {
                            viewerStats_.connectedSinceTickMs = nowTickMs();
                        }
                        viewerStats_.grantedPermissions = perms;
                    }
                    identity_.addOrUpdateRecentSession(remoteId, remoteHost, targetInput);
                    initViewerAudioPlayback();
                    startViewerTunnelMultiplexer();

                    requestVideoSettings(
                        identity_.settings().defaultQuality,
                        viewerStats_.activeMonitorIndex,
                        true,
                        clampTargetFps(identity_.settings().targetFps),
                        identity_.settings().adaptiveFps ? 1 : 0
                    );
                    return true;
                }
            }
        } catch (...) {}

        closeWinSock(vSock);
        viewerSock_.store(~uintptr_t(0));
        return false;
    };

    bool isReconnecting = false;

    while (viewerActive_.load() && running_.load()) {
        if (!isReconnecting) {
            if (!doConnectAndAuth(false)) {
                viewerActive_.store(false);
                break;
            }
        } else {
            bool recovered = false;
            for (uint32_t attempt = 1; attempt <= 3 && viewerActive_.load() && running_.load(); ++attempt) {
                {
                    std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                    viewerStats_.state = ViewerConnectionState::Reconnecting;
                    viewerStats_.reconnectAttempt = attempt;
                    viewerStats_.statusMessage = "Connection lost. Reconnecting (attempt " + std::to_string(attempt) + "/3)...";
                }
                uint32_t backoffMs = (attempt == 1) ? 1000 : (attempt == 2) ? 3000 : 5000;
                for (uint32_t waited = 0; waited < backoffMs && viewerActive_.load() && running_.load(); waited += 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!viewerActive_.load() || !running_.load()) break;

                if (doConnectAndAuth(true)) {
                    recovered = true;
                    break;
                }
            }

            if (!recovered) {
                if (viewerActive_.load()) {
                    setStatus(ViewerConnectionState::Error, "Connection lost. Reconnect failed after 3 attempts.");
                    viewerActive_.store(false);
                }
                break;
            }
        }

        uintptr_t vSock = viewerSock_.load();
        if (vSock == ~uintptr_t(0)) break;

        // 5. Connected! Receive encrypted video tiles, cursor updates, clipboard, files, and chat
        uint64_t viewerRecvSeq = 0;
        uint64_t lastMetricTick = nowTickMs();
        uint64_t lastPingTick = 0;
        uint32_t framesInWindow = 0;
        uint64_t bytesInWindow = 0;
        bool socketDropped = false;

        auto sendPacketHelper = [&](PacketType pt, const std::vector<uint8_t>& buf) -> bool {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        };

        try {
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
                    if (clipboardSyncEnabled_.load() && clipboardManager_.pollLocalChange(newClip)) {
                        ByteWriter cw;
                        cw.writeString(newClip);
                        sendViewerEncryptedPacket(PacketType::CLIPBOARD_TEXT, 0, cw.buffer().data(), cw.buffer().size());
                    }
                }

                fileManager_.pumpOutgoingChunks(sendPacketHelper, 4);

                SOCKET ws = toWinSock(vSock);
                if (ws == INVALID_SOCKET) {
                    socketDropped = true;
                    break;
                }

                fd_set rfds{};
                FD_ZERO(&rfds);
                FD_SET(ws, &rfds);
                timeval tv{ 0, 20000 }; // 20 ms

                int sel = select(0, &rfds, nullptr, nullptr, &tv);
                if (sel < 0) {
                    socketDropped = true;
                    break;
                }
                if (sel == 0) continue;

                FrameHeader hdr{};
                std::vector<uint8_t> payload;
                if (!recvFrame(vSock, hdr, payload, &viewerCipher_, &viewerRecvSeq)) {
                    socketDropped = true;
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
                        case PacketType::MONITOR_LIST: {
                            uint16_t mcount = r.readU16();
                            std::vector<MonitorDesc> mons;
                            mons.reserve(mcount);
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
                                std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                                viewerStats_.monitors = std::move(mons);
                                viewerStats_.monitorCount = std::max<int>(1, static_cast<int>(viewerStats_.monitors.size()));
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
                            if (clipboardSyncEnabled_.load()) {
                                clipboardManager_.applyRemoteClipboard(txt);
                            }
                            break;
                        }
                        case PacketType::FILE_OFFER: {
                            uint32_t tid = r.readU32();
                            uint64_t fsz = r.readU64();
                            std::string fname = r.readString();
                            FileOfferTarget targetHint = FileOfferTarget::DefaultDownloads;
                            float dropNx = 0.0f, dropNy = 0.0f;
                            if (r.hasRemaining(1)) {
                                targetHint = static_cast<FileOfferTarget>(r.readU8());
                            }
                            if (r.hasRemaining(8)) {
                                dropNx = r.readF32();
                                dropNy = r.readF32();
                            }
                            fileManager_.handleFileOffer(tid, fsz, fname, targetHint, dropNx, dropNy);
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
                        case PacketType::AUDIO_STREAM_CHUNK: {
                            enqueueViewerAudioChunk(payload.data(), payload.size());
                            break;
                        }
                        case PacketType::VOICE_INTERCOM_CHUNK: {
                            handleIncomingVoiceChunk(payload.data(), payload.size());
                            break;
                        }
                        case PacketType::PRIVACY_MODE_TOGGLE: {
                            if (payload.size() >= sizeof(PrivacyModePayload)) {
                                PrivacyModePayload ack{};
                                std::memcpy(&ack, payload.data(), sizeof(ack));
                                viewerPrivacyModeActive_.store(ack.enable != 0);
                                {
                                    std::lock_guard<std::mutex> lock(viewerStatsMutex_);
                                    viewerStats_.privacyModeEngaged = (ack.enable != 0);
                                }
                            }
                            break;
                        }
                        case PacketType::SYSTEM_DIAGNOSTICS: {
                            SystemDiagnosticsPayload diag;
                            if (deserializeSystemDiagnostics(payload.data(), payload.size(), diag)) {
                                std::lock_guard<std::mutex> lk(diagnosticsMutex_);
                                latestDiagnostics_ = std::move(diag);
                            }
                            break;
                        }
                        case PacketType::TUNNEL_DATA: {
                            if (payload.size() >= sizeof(TunnelDataHeader)) {
                                TunnelDataHeader tdh{};
                                std::memcpy(&tdh, payload.data(), sizeof(tdh));
                                const uint8_t* tdata = payload.data() + sizeof(tdh);
                                size_t tlen = payload.size() - sizeof(tdh);
                                if (tlen > 0) {
                                    SOCKET cs = INVALID_SOCKET;
                                    uint32_t rId = 0;
                                    {
                                        std::lock_guard<std::mutex> lk(tunnelMutex_);
                                        auto it = viewerTunnels_.find(tdh.tunnelId);
                                        if (it != viewerTunnels_.end()) {
                                            cs = it->second.sock;
                                            rId = it->second.ruleId;
                                        }
                                    }
                                    if (cs != INVALID_SOCKET) {
                                        sendAllBytes(cs, tdata, tlen);
                                        std::lock_guard<std::mutex> lk(tunnelMutex_);
                                        for (auto& r : tunnelRules_) {
                                            if (r.ruleId == rId) {
                                                r.bytesTransferredIn += tlen;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        case PacketType::TUNNEL_CLOSE: {
                            if (payload.size() >= sizeof(TunnelCloseHeader)) {
                                TunnelCloseHeader tch{};
                                std::memcpy(&tch, payload.data(), sizeof(tch));
                                std::lock_guard<std::mutex> lk(tunnelMutex_);
                                auto it = viewerTunnels_.find(tch.tunnelId);
                                if (it != viewerTunnels_.end()) {
                                    closesocket(it->second.sock);
                                    viewerTunnels_.erase(it);
                                }
                            }
                            break;
                        }
                        case PacketType::TERMINAL_DATA: {
                            if (payload.size() >= sizeof(TerminalDataHeader)) {
                                TerminalDataHeader tdh{};
                                std::memcpy(&tdh, payload.data(), sizeof(tdh));
                                const char* ttext = reinterpret_cast<const char*>(payload.data() + sizeof(tdh));
                                size_t tlen = payload.size() - sizeof(tdh);
                                if (tlen > 0) {
                                    std::lock_guard<std::mutex> lk(terminalMutex_);
                                    if (terminalLines_.empty()) {
                                        terminalLines_.push_back("");
                                    }
                                    for (size_t i = 0; i < tlen; ++i) {
                                        char ch = ttext[i];
                                        if (ch == '\r') {
                                            continue;
                                        }
                                        if (ch == '\n') {
                                            terminalLines_.push_back("");
                                            if (terminalLines_.size() > 500) {
                                                terminalLines_.erase(terminalLines_.begin());
                                            }
                                        } else {
                                            terminalLines_.back() += ch;
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        case PacketType::WHITEBOARD_PACKET: {
                            AnnotationStroke stroke{};
                            if (WhiteboardManager::deserializeStroke(payload.data(), payload.size(), stroke)) {
                                whiteboardMgr_.applyRemoteStroke(stroke);
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

        uintptr_t curS = viewerSock_.exchange(~uintptr_t(0));
        closeWinSock(curS);
        viewerEncrypted_.store(false);
        {
            std::lock_guard<std::mutex> lock(viewerCipherMutex_);
            viewerCipher_.reset();
        }

        if (socketDropped && viewerActive_.load() && running_.load()) {
            isReconnecting = true;
            continue;
        } else {
            break;
        }
    }

    uintptr_t s = viewerSock_.exchange(~uintptr_t(0));
    closeWinSock(s);
    shutdownViewerAudioPlayback();
    stopViewerTunnelMultiplexer();
    viewerPrivacyModeActive_.store(false);
    whiteboardMgr_.clearAllStrokes();
    viewerEncrypted_.store(false);
    {
        std::lock_guard<std::mutex> lock(viewerCipherMutex_);
        viewerCipher_.reset();
        CryptoUtils::secureZero(viewerSessionKey_.data(), viewerSessionKey_.size());
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

void NetworkEngine::selectRemoteMonitor(int monitorIndex) {
    if (viewerSock_.load() == ~uintptr_t(0)) return;
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        viewerStats_.activeMonitorIndex = monitorIndex;
    }
    ByteWriter w;
    w.writeI32(monitorIndex);
    sendViewerEncryptedPacket(PacketType::MONITOR_SELECT, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::updateQualitySettings(QualityPreset preset, uint8_t targetFps, bool adaptiveFps) {
    if (viewerSock_.load() == ~uintptr_t(0)) return;
    {
        std::lock_guard<std::mutex> lock(viewerStatsMutex_);
        viewerStats_.qualityPreset = preset;
        viewerStats_.targetFps = clampTargetFps(targetFps);
        viewerStats_.adaptiveFps = adaptiveFps;
        viewerStats_.effectiveFpsCap = computeAdaptiveFpsCap(
            viewerStats_.targetFps,
            viewerStats_.adaptiveFps,
            viewerStats_.rttMs,
            0.0f
        );
        viewerStats_.networkThrottled = (viewerStats_.effectiveFpsCap < viewerStats_.targetFps);
    }
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(preset));
    w.writeU8(targetFps);
    w.writeU8(adaptiveFps ? 1 : 0);
    sendViewerEncryptedPacket(PacketType::QUALITY_UPDATE, 0, w.buffer().data(), w.buffer().size());
}

uint32_t NetworkEngine::sendFile(const std::string& filePath, FileOfferTarget targetHint, float dropNx, float dropNy) {
    uintptr_t vSock = viewerSock_.load();
    uintptr_t hSock = activeHostClientSock_.load();

    if (vSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingFile(filePath, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        }, targetHint, dropNx, dropNy);
    } else if (hSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingFile(filePath, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendHostEncryptedPacket(pt, 0, buf.data(), buf.size());
        }, targetHint, dropNx, dropNy);
    }
    return 0;
}

int NetworkEngine::sendDropPath(const std::string& path, FileOfferTarget targetHint, float dropNx, float dropNy) {
    uintptr_t vSock = viewerSock_.load();
    uintptr_t hSock = activeHostClientSock_.load();

    if (vSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingPath(path, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendViewerEncryptedPacket(pt, 0, buf.data(), buf.size());
        }, targetHint, dropNx, dropNy);
    } else if (hSock != ~uintptr_t(0)) {
        return fileManager_.startOutgoingPath(path, [this](PacketType pt, const std::vector<uint8_t>& buf) {
            return sendHostEncryptedPacket(pt, 0, buf.data(), buf.size());
        }, targetHint, dropNx, dropNy);
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

// ---------------- Audio Streaming & Privacy Screen Controls (v2.1.0) ----------------

void NetworkEngine::setAudioVolume(int percent) {
    audioVolumePercent_.store(std::clamp(percent, 0, 100));
}

int NetworkEngine::audioVolume() const {
    return audioVolumePercent_.load();
}

void NetworkEngine::setAudioMuted(bool muted) {
    audioMuted_.store(muted);
}

bool NetworkEngine::isAudioMuted() const {
    return audioMuted_.load();
}

void NetworkEngine::requestTogglePrivacyMode() {
    bool current = viewerPrivacyModeActive_.load();
    PrivacyModePayload p{};
    p.enable = current ? 0 : 1;
    p.acknowledge = 0;
    ByteWriter w;
    w.writeBytes(&p, sizeof(p));
    sendViewerEncryptedPacket(PacketType::PRIVACY_MODE_TOGGLE, 0, w.buffer().data(), w.buffer().size());
}

bool NetworkEngine::isPrivacyModeEngaged() const {
    return viewerPrivacyModeActive_.load();
}

bool NetworkEngine::isHostPrivacyModeActive() const {
    return hostPrivacyModeActive_.load();
}

void NetworkEngine::setHostPrivacyMode(bool enable) {
    if (enable) {
        createPrivacyCurtainWindow();
    } else {
        destroyPrivacyCurtainWindow();
    }
}

void NetworkEngine::createPrivacyCurtainWindow() {
    if (hwndPrivacyCurtain_) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PrivacyCurtainWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CppDeskPrivacyCurtainClass";
    wc.hCursor = nullptr;
    RegisterClassExW(&wc);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwndPrivacyCurtain_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"CppDeskPrivacyCurtainClass",
        L"CppDesk Privacy Curtain",
        WS_POPUP,
        vx, vy, vw, vh,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (hwndPrivacyCurtain_) {
        SetWindowDisplayAffinity(hwndPrivacyCurtain_, WDA_EXCLUDEFROMCAPTURE);
        ShowWindow(hwndPrivacyCurtain_, SW_SHOWMAXIMIZED);
        SetWindowPos(hwndPrivacyCurtain_, HWND_TOPMOST, vx, vy, vw, vh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        UpdateWindow(hwndPrivacyCurtain_);
    }

    BlockInput(TRUE);
    hostPrivacyModeActive_.store(true);
}

void NetworkEngine::destroyPrivacyCurtainWindow() {
    BlockInput(FALSE);
    if (hwndPrivacyCurtain_) {
        DestroyWindow(hwndPrivacyCurtain_);
        hwndPrivacyCurtain_ = nullptr;
    }
    hostPrivacyModeActive_.store(false);
}

void NetworkEngine::startHostAudioCapture() {
    stopHostAudioCapture();
    hostAudioActive_.store(true);
    hostAudioThread_ = std::thread(&NetworkEngine::hostAudioCaptureLoop, this);
}

void NetworkEngine::stopHostAudioCapture() {
    hostAudioActive_.store(false);
    if (hostAudioThread_.joinable()) {
        hostAudioThread_.join();
    }
}

void NetworkEngine::hostAudioCaptureLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInited = SUCCEEDED(hr);

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX* pwfx = nullptr;

    auto cleanupCom = [&]() {
        if (pwfx) { CoTaskMemFree(pwfx); pwfx = nullptr; }
        if (pCaptureClient) { pCaptureClient->Release(); pCaptureClient = nullptr; }
        if (pAudioClient) { pAudioClient->Stop(); pAudioClient->Release(); pAudioClient = nullptr; }
        if (pDevice) { pDevice->Release(); pDevice = nullptr; }
        if (pEnumerator) { pEnumerator->Release(); pEnumerator = nullptr; }
        if (coInited) { CoUninitialize(); }
    };

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator_val, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator_val, (void**)&pEnumerator);
    if (FAILED(hr)) { cleanupCom(); return; }

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) { cleanupCom(); return; }

    hr = pDevice->Activate(IID_IAudioClient_val, CLSCTX_ALL, nullptr, (void**)&pAudioClient);
    if (FAILED(hr)) { cleanupCom(); return; }

    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr) || !pwfx) { cleanupCom(); return; }

    REFERENCE_TIME hnsBufferDuration = 1000000; // 100ms
    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsBufferDuration,
        0,
        pwfx,
        nullptr
    );
    if (FAILED(hr)) { cleanupCom(); return; }

    hr = pAudioClient->GetService(IID_IAudioCaptureClient_val, (void**)&pCaptureClient);
    if (FAILED(hr)) { cleanupCom(); return; }

    hr = pAudioClient->Start();
    if (FAILED(hr)) { cleanupCom(); return; }

    bool isFloat = false;
    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* pExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx);
        if (std::memcmp(&pExt->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_val, sizeof(GUID)) == 0) {
            isFloat = true;
        }
    }

    uint32_t srcSampleRate = pwfx->nSamplesPerSec;
    WORD srcChannels = pwfx->nChannels;

    std::vector<int16_t> convertedPcm;
    std::vector<int16_t> resampledPcm;

    while (hostAudioActive_.load() && running_.load() && activeHostClientSock_.load() != ~uintptr_t(0)) {
        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        if (packetLength == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }

        BYTE* pData = nullptr;
        UINT32 numFramesRead = 0;
        DWORD flags = 0;

        hr = pCaptureClient->GetBuffer(&pData, &numFramesRead, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        if (numFramesRead > 0) {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                AudioChunkHeader hdr{};
                hdr.sampleRate = 48000;
                hdr.channels = 2;
                hdr.bitsPerSample = 16;
                hdr.isSilent = 1;
                hdr.sampleFrames = numFramesRead;

                ByteWriter w;
                w.writeBytes(&hdr, sizeof(hdr));
                sendHostEncryptedPacket(PacketType::AUDIO_STREAM_CHUNK, 0, w.buffer().data(), w.buffer().size());
            } else if (pData) {
                convertedPcm.resize(numFramesRead * 2);
                if (isFloat) {
                    const float* fData = reinterpret_cast<const float*>(pData);
                    for (UINT32 f = 0; f < numFramesRead; ++f) {
                        float left = fData[f * srcChannels];
                        float right = (srcChannels > 1) ? fData[f * srcChannels + 1] : left;
                        convertedPcm[f * 2 + 0] = static_cast<int16_t>(std::clamp(left, -1.0f, 1.0f) * 32767.0f);
                        convertedPcm[f * 2 + 1] = static_cast<int16_t>(std::clamp(right, -1.0f, 1.0f) * 32767.0f);
                    }
                } else if (pwfx->wBitsPerSample == 16) {
                    const int16_t* sData = reinterpret_cast<const int16_t*>(pData);
                    for (UINT32 f = 0; f < numFramesRead; ++f) {
                        int16_t left = sData[f * srcChannels];
                        int16_t right = (srcChannels > 1) ? sData[f * srcChannels + 1] : left;
                        convertedPcm[f * 2 + 0] = left;
                        convertedPcm[f * 2 + 1] = right;
                    }
                }

                const int16_t* pcmToSend = convertedPcm.data();
                uint32_t finalFrames = numFramesRead;

                if (srcSampleRate != 48000 && srcSampleRate > 0) {
                    finalFrames = static_cast<uint32_t>((static_cast<uint64_t>(numFramesRead) * 48000) / srcSampleRate);
                    if (finalFrames > 0) {
                        resampledPcm.resize(finalFrames * 2);
                        for (uint32_t i = 0; i < finalFrames; ++i) {
                            double srcPos = static_cast<double>(i) * srcSampleRate / 48000.0;
                            size_t idx0 = static_cast<size_t>(srcPos);
                            size_t idx1 = std::min(idx0 + 1, static_cast<size_t>(numFramesRead - 1));
                            float frac = static_cast<float>(srcPos - idx0);

                            float l0 = convertedPcm[idx0 * 2 + 0];
                            float l1 = convertedPcm[idx1 * 2 + 0];
                            float r0 = convertedPcm[idx0 * 2 + 1];
                            float r1 = convertedPcm[idx1 * 2 + 1];

                            resampledPcm[i * 2 + 0] = static_cast<int16_t>((1.0f - frac) * l0 + frac * l1);
                            resampledPcm[i * 2 + 1] = static_cast<int16_t>((1.0f - frac) * r0 + frac * r1);
                        }
                        pcmToSend = resampledPcm.data();
                    }
                }

                if (finalFrames > 0) {
                    AudioChunkHeader hdr{};
                    hdr.sampleRate = 48000;
                    hdr.channels = 2;
                    hdr.bitsPerSample = 16;
                    hdr.isSilent = 0;
                    hdr.sampleFrames = finalFrames;

                    ByteWriter w;
                    w.writeBytes(&hdr, sizeof(hdr));
                    w.writeBytes(pcmToSend, finalFrames * 2 * sizeof(int16_t));
                    sendHostEncryptedPacket(PacketType::AUDIO_STREAM_CHUNK, 0, w.buffer().data(), w.buffer().size());
                }
            }
        }

        pCaptureClient->ReleaseBuffer(numFramesRead);
    }

    cleanupCom();
}

void NetworkEngine::initViewerAudioPlayback() {
    std::lock_guard<std::mutex> lk(audioPlaybackMutex_);
    if (hWaveOut_) return;

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 48000;
    wfx.nAvgBytesPerSec = 48000 * 2 * sizeof(int16_t);
    wfx.nBlockAlign = 2 * sizeof(int16_t);
    wfx.wBitsPerSample = 16;
    wfx.cbSize = 0;

    MMRESULT res = waveOutOpen(&hWaveOut_, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        hWaveOut_ = nullptr;
        return;
    }

    for (size_t i = 0; i < 3; ++i) {
        waveBuffers_[i].assign(8192, 0);
        std::memset(&waveHeaders_[i], 0, sizeof(WAVEHDR));
        waveHeaders_[i].lpData = reinterpret_cast<LPSTR>(waveBuffers_[i].data());
        waveHeaders_[i].dwBufferLength = static_cast<DWORD>(waveBuffers_[i].size());
    }
    currentWaveIdx_ = 0;
}

void NetworkEngine::shutdownViewerAudioPlayback() {
    std::lock_guard<std::mutex> lk(audioPlaybackMutex_);
    if (!hWaveOut_) return;

    waveOutReset(hWaveOut_);
    for (size_t i = 0; i < 3; ++i) {
        if (waveHeaders_[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(hWaveOut_, &waveHeaders_[i], sizeof(WAVEHDR));
        }
        waveHeaders_[i].dwFlags = 0;
    }
    waveOutClose(hWaveOut_);
    hWaveOut_ = nullptr;
}

void NetworkEngine::enqueueViewerAudioChunk(const uint8_t* payload, size_t len) {
    if (!hWaveOut_ || len < sizeof(AudioChunkHeader)) return;

    AudioChunkHeader hdr{};
    std::memcpy(&hdr, payload, sizeof(hdr));
    if (hdr.isSilent || hdr.sampleFrames == 0) return;

    size_t pcmBytes = len - sizeof(AudioChunkHeader);
    if (pcmBytes == 0) return;

    const int16_t* inSamples = reinterpret_cast<const int16_t*>(payload + sizeof(AudioChunkHeader));
    size_t sampleCount = pcmBytes / sizeof(int16_t);

    std::lock_guard<std::mutex> lk(audioPlaybackMutex_);
    if (!hWaveOut_) return;

    bool isMuted = audioMuted_.load();
    int volPercent = audioVolumePercent_.load();
    float volScale = isMuted ? 0.0f : (volPercent / 100.0f);

    WAVEHDR& curHdr = waveHeaders_[currentWaveIdx_];
    if (curHdr.dwFlags & WHDR_PREPARED) {
        if (!(curHdr.dwFlags & WHDR_DONE)) {
            return;
        }
        waveOutUnprepareHeader(hWaveOut_, &curHdr, sizeof(WAVEHDR));
        curHdr.dwFlags = 0;
    }

    if (waveBuffers_[currentWaveIdx_].size() < pcmBytes) {
        waveBuffers_[currentWaveIdx_].resize(pcmBytes);
    }

    int16_t* outSamples = reinterpret_cast<int16_t*>(waveBuffers_[currentWaveIdx_].data());
    for (size_t i = 0; i < sampleCount; ++i) {
        float s = static_cast<float>(inSamples[i]) * volScale;
        outSamples[i] = static_cast<int16_t>(std::clamp(s, -32768.0f, 32767.0f));
    }

    curHdr.lpData = reinterpret_cast<LPSTR>(outSamples);
    curHdr.dwBufferLength = static_cast<DWORD>(pcmBytes);
    curHdr.dwFlags = 0;

    if (waveOutPrepareHeader(hWaveOut_, &curHdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
        waveOutWrite(hWaveOut_, &curHdr, sizeof(WAVEHDR));
        currentWaveIdx_ = (currentWaveIdx_ + 1) % 3;
    }
}

// ---------------- Bidirectional Voice Intercom (Feature 4) ----------------

bool NetworkEngine::startVoiceIntercom() {
    voiceIntercom_.startPlayback(48000, 1);

    return voiceIntercom_.startCapture([this](const uint8_t* pcm, size_t bytes, uint32_t sampleRate, uint8_t channels) {
        VoiceChunkHeader hdr{};
        hdr.sampleRate = sampleRate;
        hdr.channels = channels;
        hdr.bitsPerSample = 16;
        hdr.flags = 0x00;
        hdr.sampleFrames = static_cast<uint32_t>(bytes / (channels * sizeof(int16_t)));

        ByteWriter w;
        w.writeBytes(&hdr, sizeof(hdr));
        w.writeBytes(pcm, bytes);

        if (viewerActive_.load()) {
            sendViewerEncryptedPacket(PacketType::VOICE_INTERCOM_CHUNK, 0, w.buffer().data(), w.buffer().size());
        } else if (activeHostClientSock_.load() != ~uintptr_t(0)) {
            sendHostEncryptedPacket(PacketType::VOICE_INTERCOM_CHUNK, 0, w.buffer().data(), w.buffer().size());
        }
    }, 48000, 1);
}

void NetworkEngine::stopVoiceIntercom() {
    voiceIntercom_.stopCapture();
    voiceIntercom_.stopPlayback();
}

bool NetworkEngine::isVoiceIntercomActive() const {
    return voiceIntercom_.isCapturing();
}

void NetworkEngine::setVoiceIntercomMicMuted(bool muted) {
    voiceIntercom_.setMicMuted(muted);
}

bool NetworkEngine::isVoiceIntercomMicMuted() const {
    return voiceIntercom_.isMicMuted();
}

float NetworkEngine::voiceIntercomInputLevel() const {
    return voiceIntercom_.inputLevel();
}

void NetworkEngine::handleIncomingVoiceChunk(const uint8_t* payload, size_t len) {
    if (!payload || len < sizeof(VoiceChunkHeader)) return;
    const uint8_t* pcm = payload + sizeof(VoiceChunkHeader);
    size_t pcmBytes = len - sizeof(VoiceChunkHeader);
    if (pcmBytes == 0) return;

    if (!voiceIntercom_.isPlaying()) {
        voiceIntercom_.startPlayback(48000, 1);
    }
    voiceIntercom_.enqueuePlaybackChunk(pcm, pcmBytes);
}

// ---------------- Remote Terminal & TCP Port Forwarding (v2.1.0) ----------------

void NetworkEngine::sendTerminalCommand(const std::string& cmd) {
    if (cmd.empty()) return;
    {
        std::lock_guard<std::mutex> lk(terminalMutex_);
        terminalHistory_.push_back(cmd);
        if (terminalHistory_.size() > 50) terminalHistory_.erase(terminalHistory_.begin());
        terminalLines_.push_back("> " + cmd);
        if (terminalLines_.size() > 500) terminalLines_.erase(terminalLines_.begin());
    }

    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(TerminalStreamKind::StdinInput));
    w.writeU32(static_cast<uint32_t>(cmd.size()));
    w.writeBytes(cmd.data(), cmd.size());
    sendViewerEncryptedPacket(PacketType::TERMINAL_DATA, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::resetRemoteTerminal(bool usePowerShell) {
    ByteWriter w;
    w.writeU8(static_cast<uint8_t>(usePowerShell ? TerminalStreamKind::SwitchShell : TerminalStreamKind::ResetShell));
    w.writeU32(0);
    sendViewerEncryptedPacket(PacketType::TERMINAL_DATA, 0, w.buffer().data(), w.buffer().size());
}

std::vector<std::string> NetworkEngine::getTerminalScrollback() const {
    std::lock_guard<std::mutex> lk(terminalMutex_);
    return terminalLines_;
}

void NetworkEngine::clearTerminalScrollback() {
    std::lock_guard<std::mutex> lk(terminalMutex_);
    terminalLines_.clear();
}

void NetworkEngine::sendWhiteboardStroke(const AnnotationStroke& stroke) {
    auto payload = WhiteboardManager::serializeStroke(stroke);
    if (viewerSock_.load() != ~uintptr_t(0)) {
        sendViewerEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    } else if (activeHostClientSock_.load() != ~uintptr_t(0)) {
        sendHostEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    }
}

void NetworkEngine::sendWhiteboardClear() {
    auto payload = WhiteboardManager::serializeClearPacket();
    if (viewerSock_.load() != ~uintptr_t(0)) {
        sendViewerEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    } else if (activeHostClientSock_.load() != ~uintptr_t(0)) {
        sendHostEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    }
}

void NetworkEngine::sendWhiteboardLaser(float normX, float normY) {
    AnnotationStroke stroke{};
    stroke.tool = WhiteboardTool::LaserPointer;
    stroke.points.push_back({ normX, normY });
    auto payload = WhiteboardManager::serializeStroke(stroke);
    if (viewerSock_.load() != ~uintptr_t(0)) {
        sendViewerEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    } else if (activeHostClientSock_.load() != ~uintptr_t(0)) {
        sendHostEncryptedPacket(PacketType::WHITEBOARD_PACKET, 0, payload.data(), payload.size());
    }
}

void NetworkEngine::startHostTerminal(bool usePowerShell) {
    stopHostTerminal();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;

    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) return;
    if (!SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        return;
    }

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        return;
    }
    if (!SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        return;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStdoutWrite;

    PROCESS_INFORMATION pi{};
    wchar_t cmdLine[256];
    if (usePowerShell) {
        wcscpy_s(cmdLine, L"powershell.exe -NoLogo -NoExit");
    } else {
        wcscpy_s(cmdLine, L"cmd.exe /Q /K");
    }

    BOOL created = CreateProcessW(
        nullptr, cmdLine, nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi
    );

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!created) {
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        return;
    }

    hChildStdinWrite_ = hStdinWrite;
    hChildStdoutRead_ = hStdoutRead;
    hChildProcess_ = pi.hProcess;
    hChildThread_ = pi.hThread;

    hostTerminalActive_.store(true);
    hostTerminalThread_ = std::thread(&NetworkEngine::hostTerminalReaderLoop, this);
}

void NetworkEngine::stopHostTerminal() {
    hostTerminalActive_.store(false);
    if (hostTerminalThread_.joinable()) {
        hostTerminalThread_.join();
    }
    if (hChildProcess_) {
        TerminateProcess(hChildProcess_, 0);
        CloseHandle(hChildProcess_);
        hChildProcess_ = nullptr;
    }
    if (hChildThread_) {
        CloseHandle(hChildThread_);
        hChildThread_ = nullptr;
    }
    if (hChildStdinWrite_) {
        CloseHandle(hChildStdinWrite_);
        hChildStdinWrite_ = nullptr;
    }
    if (hChildStdoutRead_) {
        CloseHandle(hChildStdoutRead_);
        hChildStdoutRead_ = nullptr;
    }
}

void NetworkEngine::injectHostTerminalStdin(const std::string& input) {
    if (!hChildStdinWrite_) return;
    std::string toWrite = input + "\r\n";
    DWORD written = 0;
    WriteFile(hChildStdinWrite_, toWrite.data(), static_cast<DWORD>(toWrite.size()), &written, nullptr);
}

void NetworkEngine::hostTerminalReaderLoop() {
    char buf[4096];
    while (hostTerminalActive_.load() && running_.load() && activeHostClientSock_.load() != ~uintptr_t(0)) {
        if (!hChildStdoutRead_) break;
        DWORD avail = 0;
        if (PeekNamedPipe(hChildStdoutRead_, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD toRead = std::min<DWORD>(avail, sizeof(buf));
            DWORD numRead = 0;
            if (ReadFile(hChildStdoutRead_, buf, toRead, &numRead, nullptr) && numRead > 0) {
                ByteWriter w;
                w.writeU8(static_cast<uint8_t>(TerminalStreamKind::StdoutChunk));
                w.writeU32(numRead);
                w.writeBytes(buf, numRead);
                sendHostEncryptedPacket(PacketType::TERMINAL_DATA, 0, w.buffer().data(), w.buffer().size());
                continue;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

uint32_t NetworkEngine::addPortForwardRule(uint16_t localPort, uint16_t targetPort, const std::string& desc, bool startActive) {
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    PortForwardRule r;
    r.ruleId = nextRuleId_++;
    r.localPort = localPort;
    r.targetPort = targetPort;
    r.description = desc;
    r.active = startActive;
    tunnelRules_.push_back(r);

    if (startActive && viewerActive_.load()) {
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls != INVALID_SOCKET) {
            u_long nonblock = 1;
            ioctlsocket(ls, FIONBIO, &nonblock);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(localPort);
            if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                listen(ls, 5) == 0) {
                tunnelListeners_.push_back({ r.ruleId, localPort, targetPort, ls });
            } else {
                closesocket(ls);
            }
        }
    }
    return r.ruleId;
}

void NetworkEngine::removePortForwardRule(uint32_t ruleId) {
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    for (auto it = tunnelListeners_.begin(); it != tunnelListeners_.end(); ++it) {
        if (it->ruleId == ruleId) {
            closesocket(it->listenSock);
            tunnelListeners_.erase(it);
            break;
        }
    }
    for (auto it = tunnelRules_.begin(); it != tunnelRules_.end(); ++it) {
        if (it->ruleId == ruleId) {
            tunnelRules_.erase(it);
            break;
        }
    }
}

void NetworkEngine::setPortForwardRuleActive(uint32_t ruleId, bool active) {
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    for (auto& r : tunnelRules_) {
        if (r.ruleId == ruleId) {
            r.active = active;
            if (!active) {
                for (auto it = tunnelListeners_.begin(); it != tunnelListeners_.end(); ++it) {
                    if (it->ruleId == ruleId) {
                        closesocket(it->listenSock);
                        tunnelListeners_.erase(it);
                        break;
                    }
                }
            } else if (viewerActive_.load()) {
                SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (ls != INVALID_SOCKET) {
                    u_long nonblock = 1;
                    ioctlsocket(ls, FIONBIO, &nonblock);
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    addr.sin_port = htons(r.localPort);
                    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                        listen(ls, 5) == 0) {
                        tunnelListeners_.push_back({ r.ruleId, r.localPort, r.targetPort, ls });
                    } else {
                        closesocket(ls);
                    }
                }
            }
            break;
        }
    }
}

std::vector<PortForwardRule> NetworkEngine::portForwardRules() const {
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    return tunnelRules_;
}

void NetworkEngine::startPortForwarding() {
    startViewerTunnelMultiplexer();
}

void NetworkEngine::stopPortForwarding() {
    stopViewerTunnelMultiplexer();
    stopHostTunnelProxy();
}

void NetworkEngine::startViewerTunnelMultiplexer() {
    stopViewerTunnelMultiplexer();
    viewerTunnelActive_.store(true);
    viewerTunnelThread_ = std::thread(&NetworkEngine::viewerTunnelMultiplexerLoop, this);
}

void NetworkEngine::stopViewerTunnelMultiplexer() {
    viewerTunnelActive_.store(false);
    if (viewerTunnelThread_.joinable()) {
        viewerTunnelThread_.join();
    }
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    for (auto& l : tunnelListeners_) {
        closesocket(l.listenSock);
    }
    tunnelListeners_.clear();
    for (auto& kv : viewerTunnels_) {
        closesocket(kv.second.sock);
    }
    viewerTunnels_.clear();
}

void NetworkEngine::viewerTunnelMultiplexerLoop() {
    {
        std::lock_guard<std::mutex> lk(tunnelMutex_);
        for (const auto& r : tunnelRules_) {
            if (r.active) {
                SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (ls != INVALID_SOCKET) {
                    u_long nonblock = 1;
                    ioctlsocket(ls, FIONBIO, &nonblock);
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    addr.sin_port = htons(r.localPort);
                    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                        listen(ls, 5) == 0) {
                        tunnelListeners_.push_back({ r.ruleId, r.localPort, r.targetPort, ls });
                    } else {
                        closesocket(ls);
                    }
                }
            }
        }
    }

    std::vector<uint8_t> readBuf(16384);

    while (viewerTunnelActive_.load() && running_.load() && viewerActive_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        SOCKET maxSock = 0;

        std::vector<ListenerState> currentListeners;
        std::vector<ActiveTunnel> currentTunnels;
        {
            std::lock_guard<std::mutex> lk(tunnelMutex_);
            currentListeners = tunnelListeners_;
            for (const auto& kv : viewerTunnels_) {
                currentTunnels.push_back(kv.second);
            }
        }

        for (const auto& l : currentListeners) {
            FD_SET(l.listenSock, &readSet);
            if (l.listenSock > maxSock) maxSock = l.listenSock;
        }
        for (const auto& t : currentTunnels) {
            FD_SET(t.sock, &readSet);
            if (t.sock > maxSock) maxSock = t.sock;
        }

        timeval tv{ 0, 20000 };
        int res = select(static_cast<int>(maxSock + 1), &readSet, nullptr, nullptr, &tv);
        if (res <= 0) continue;

        for (const auto& l : currentListeners) {
            if (FD_ISSET(l.listenSock, &readSet)) {
                sockaddr_in clientAddr{};
                int addrLen = sizeof(clientAddr);
                SOCKET cs = accept(l.listenSock, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
                if (cs != INVALID_SOCKET) {
                    u_long nonblock = 1;
                    ioctlsocket(cs, FIONBIO, &nonblock);

                    uint32_t tid = 0;
                    {
                        std::lock_guard<std::mutex> lk(tunnelMutex_);
                        tid = nextTunnelId_++;
                        viewerTunnels_[tid] = { tid, l.ruleId, cs, l.targetPort };
                    }

                    TunnelOpenHeader toh{};
                    toh.tunnelId = tid;
                    toh.targetPort = l.targetPort;
                    toh.flags = 0;
                    ByteWriter w;
                    w.writeBytes(&toh, sizeof(toh));
                    sendViewerEncryptedPacket(PacketType::TUNNEL_OPEN, 0, w.buffer().data(), w.buffer().size());
                }
            }
        }

        std::vector<uint32_t> closedTunnels;
        for (const auto& t : currentTunnels) {
            if (FD_ISSET(t.sock, &readSet)) {
                int n = recv(t.sock, reinterpret_cast<char*>(readBuf.data()), static_cast<int>(readBuf.size()), 0);
                if (n > 0) {
                    TunnelDataHeader tdh{};
                    tdh.tunnelId = t.tunnelId;
                    tdh.dataLen = static_cast<uint32_t>(n);
                    ByteWriter w;
                    w.writeBytes(&tdh, sizeof(tdh));
                    w.writeBytes(readBuf.data(), n);
                    sendViewerEncryptedPacket(PacketType::TUNNEL_DATA, 0, w.buffer().data(), w.buffer().size());

                    std::lock_guard<std::mutex> lk(tunnelMutex_);
                    for (auto& r : tunnelRules_) {
                        if (r.ruleId == t.ruleId) {
                            r.bytesTransferredOut += n;
                            break;
                        }
                    }
                } else {
                    closedTunnels.push_back(t.tunnelId);
                }
            }
        }

        for (uint32_t tid : closedTunnels) {
            TunnelCloseHeader tch{};
            tch.tunnelId = tid;
            tch.reasonCode = 0;
            ByteWriter w;
            w.writeBytes(&tch, sizeof(tch));
            sendViewerEncryptedPacket(PacketType::TUNNEL_CLOSE, 0, w.buffer().data(), w.buffer().size());

            std::lock_guard<std::mutex> lk(tunnelMutex_);
            auto it = viewerTunnels_.find(tid);
            if (it != viewerTunnels_.end()) {
                closesocket(it->second.sock);
                viewerTunnels_.erase(it);
            }
        }
    }
}

void NetworkEngine::startHostTunnelProxy() {
    stopHostTunnelProxy();
    hostTunnelActive_.store(true);
    hostTunnelThread_ = std::thread(&NetworkEngine::hostTunnelProxyLoop, this);
}

void NetworkEngine::stopHostTunnelProxy() {
    hostTunnelActive_.store(false);
    if (hostTunnelThread_.joinable()) {
        hostTunnelThread_.join();
    }
    std::lock_guard<std::mutex> lk(tunnelMutex_);
    for (auto& kv : hostTunnels_) {
        closesocket(kv.second.sock);
    }
    hostTunnels_.clear();
}

void NetworkEngine::hostTunnelProxyLoop() {
    std::vector<uint8_t> readBuf(16384);

    while (hostTunnelActive_.load() && running_.load() && activeHostClientSock_.load() != ~uintptr_t(0)) {
        fd_set readSet;
        FD_ZERO(&readSet);
        SOCKET maxSock = 0;

        std::vector<ActiveTunnel> currentTunnels;
        {
            std::lock_guard<std::mutex> lk(tunnelMutex_);
            for (const auto& kv : hostTunnels_) {
                currentTunnels.push_back(kv.second);
            }
        }

        if (currentTunnels.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        for (const auto& t : currentTunnels) {
            FD_SET(t.sock, &readSet);
            if (t.sock > maxSock) maxSock = t.sock;
        }

        timeval tv{ 0, 20000 };
        int res = select(static_cast<int>(maxSock + 1), &readSet, nullptr, nullptr, &tv);
        if (res <= 0) continue;

        std::vector<uint32_t> closedTunnels;
        for (const auto& t : currentTunnels) {
            if (FD_ISSET(t.sock, &readSet)) {
                int n = recv(t.sock, reinterpret_cast<char*>(readBuf.data()), static_cast<int>(readBuf.size()), 0);
                if (n > 0) {
                    TunnelDataHeader tdh{};
                    tdh.tunnelId = t.tunnelId;
                    tdh.dataLen = static_cast<uint32_t>(n);
                    ByteWriter w;
                    w.writeBytes(&tdh, sizeof(tdh));
                    w.writeBytes(readBuf.data(), n);
                    sendHostEncryptedPacket(PacketType::TUNNEL_DATA, 0, w.buffer().data(), w.buffer().size());
                } else {
                    closedTunnels.push_back(t.tunnelId);
                }
            }
        }

        for (uint32_t tid : closedTunnels) {
            TunnelCloseHeader tch{};
            tch.tunnelId = tid;
            tch.reasonCode = 0;
            ByteWriter w;
            w.writeBytes(&tch, sizeof(tch));
            sendHostEncryptedPacket(PacketType::TUNNEL_CLOSE, 0, w.buffer().data(), w.buffer().size());

            std::lock_guard<std::mutex> lk(tunnelMutex_);
            auto it = hostTunnels_.find(tid);
            if (it != hostTunnels_.end()) {
                closesocket(it->second.sock);
                hostTunnels_.erase(it);
            }
        }
    }
}

// ---------------- Remote Hardware Diagnostics & Live Process Telemetry ----------------

SystemDiagnosticsPayload NetworkEngine::sampleHostDiagnostics() {
    SystemDiagnosticsPayload diag{};

    // 1. CPU Usage % via GetSystemTimes
    static uint64_t s_prevIdleTime = 0;
    static uint64_t s_prevKernelTime = 0;
    static uint64_t s_prevUserTime = 0;

    FILETIME idleTime{}, kernelTime{}, userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        auto ftToU64 = [](const FILETIME& ft) -> uint64_t {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        uint64_t curIdle = ftToU64(idleTime);
        uint64_t curKernel = ftToU64(kernelTime);
        uint64_t curUser = ftToU64(userTime);

        if (s_prevKernelTime > 0 || s_prevUserTime > 0) {
            uint64_t deltaIdle = (curIdle >= s_prevIdleTime) ? (curIdle - s_prevIdleTime) : 0;
            uint64_t deltaKernel = (curKernel >= s_prevKernelTime) ? (curKernel - s_prevKernelTime) : 0;
            uint64_t deltaUser = (curUser >= s_prevUserTime) ? (curUser - s_prevUserTime) : 0;
            uint64_t deltaTotal = deltaKernel + deltaUser;
            if (deltaTotal > 0 && deltaTotal >= deltaIdle) {
                diag.cpuUsagePercent = static_cast<float>((deltaTotal - deltaIdle) * 100.0 / static_cast<double>(deltaTotal));
                diag.cpuUsagePercent = std::clamp(diag.cpuUsagePercent, 0.0f, 100.0f);
            }
        } else {
            // First time sampling: short sleep to establish an initial delta
            Sleep(15);
            FILETIME i2{}, k2{}, u2{};
            if (GetSystemTimes(&i2, &k2, &u2)) {
                uint64_t curIdle2 = ftToU64(i2);
                uint64_t curKernel2 = ftToU64(k2);
                uint64_t curUser2 = ftToU64(u2);
                uint64_t deltaIdle = (curIdle2 >= curIdle) ? (curIdle2 - curIdle) : 0;
                uint64_t deltaKernel = (curKernel2 >= curKernel) ? (curKernel2 - curKernel) : 0;
                uint64_t deltaUser = (curUser2 >= curUser) ? (curUser2 - curUser) : 0;
                uint64_t deltaTotal = deltaKernel + deltaUser;
                if (deltaTotal > 0 && deltaTotal >= deltaIdle) {
                    diag.cpuUsagePercent = static_cast<float>((deltaTotal - deltaIdle) * 100.0 / static_cast<double>(deltaTotal));
                    diag.cpuUsagePercent = std::clamp(diag.cpuUsagePercent, 0.0f, 100.0f);
                }
                curIdle = curIdle2;
                curKernel = curKernel2;
                curUser = curUser2;
            }
        }
        s_prevIdleTime = curIdle;
        s_prevKernelTime = curKernel;
        s_prevUserTime = curUser;
    }

    // 2. Physical RAM Usage via GlobalMemoryStatusEx
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        diag.ramTotalBytes = memStatus.ullTotalPhys;
        diag.ramUsedBytes = (memStatus.ullTotalPhys >= memStatus.ullAvailPhys)
            ? (memStatus.ullTotalPhys - memStatus.ullAvailPhys)
            : 0;
    }

    // 3. Primary Disk Space via GetDiskFreeSpaceExW
    ULARGE_INTEGER freeBytesAvail{}, totalBytes{}, totalFreeBytes{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvail, &totalBytes, &totalFreeBytes)) {
        diag.diskTotalBytes = totalBytes.QuadPart;
        diag.diskUsedBytes = (totalBytes.QuadPart >= totalFreeBytes.QuadPart)
            ? (totalBytes.QuadPart - totalFreeBytes.QuadPart)
            : 0;
    }

    // 4. Top active processes sorted by memory usage
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            std::vector<ProcessTelemetryItem> candidates;
            candidates.reserve(128);
            do {
                if (pe.th32ProcessID == 0) continue; // skip System Idle

                ProcessTelemetryItem item;
                item.pid = pe.th32ProcessID;

                // Convert wchar_t process name to UTF-8
                int reqSize = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nullptr, 0, nullptr, nullptr);
                if (reqSize > 1) {
                    std::string u8Name(reqSize - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, u8Name.data(), reqSize, nullptr, nullptr);
                    item.name = std::move(u8Name);
                } else {
                    item.name = "Unknown";
                }

                // Query working set memory
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_MEMORY_COUNTERS pmc{};
                    pmc.cb = sizeof(pmc);
                    if (K32GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        item.workingSetBytes = pmc.WorkingSetSize;
                    }
                    CloseHandle(hProc);
                }
                candidates.push_back(std::move(item));
            } while (Process32NextW(hSnap, &pe));

            // Sort descending by memory usage
            std::sort(candidates.begin(), candidates.end(), [](const ProcessTelemetryItem& a, const ProcessTelemetryItem& b) {
                return a.workingSetBytes > b.workingSetBytes;
            });

            // Keep top 15
            size_t maxProcs = std::min<size_t>(candidates.size(), 15);
            diag.processes.assign(candidates.begin(), candidates.begin() + maxProcs);
        }
        CloseHandle(hSnap);
    }

    return diag;
}

bool NetworkEngine::executeProcessKill(uint32_t pid, uint8_t callerPermissions) {
    if ((callerPermissions & PERM_INPUT) == 0) {
        return false;
    }
    if (pid == 0 || pid == 4) {
        return false;
    }
    if (pid == GetCurrentProcessId()) {
        return false;
    }

    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        return false;
    }
    BOOL success = TerminateProcess(hProc, 1);
    CloseHandle(hProc);
    return success != FALSE;
}

void NetworkEngine::setDiagnosticsActive(bool active) {
    viewerDiagnosticsActive_.store(active);
    ByteWriter w;
    w.writeU8(active ? 1 : 0);
    sendViewerEncryptedPacket(PacketType::DIAGNOSTICS_REQ, 0, w.buffer().data(), w.buffer().size());
}

void NetworkEngine::sendProcessKill(uint32_t pid) {
    ByteWriter w;
    w.writeU32(pid);
    sendViewerEncryptedPacket(PacketType::PROCESS_KILL, 0, w.buffer().data(), w.buffer().size());
}

SystemDiagnosticsPayload NetworkEngine::latestDiagnostics() const {
    std::lock_guard<std::mutex> lk(diagnosticsMutex_);
    return latestDiagnostics_;
}

} // namespace cppdesk
