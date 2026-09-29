#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace aerodesk {

constexpr uint32_t PROTOCOL_MAGIC   = 0x4144534B; // "ADSK"
constexpr uint16_t PROTOCOL_VERSION = 1;

constexpr uint16_t DEFAULT_HOST_PORT      = 50990;
constexpr uint16_t DEFAULT_DISCOVERY_PORT = 50998;
constexpr uint16_t DEFAULT_RELAY_PORT     = 50999;

constexpr size_t MAX_PACKET_PAYLOAD_SIZE = 32 * 1024 * 1024; // 32 MB safety cap
constexpr size_t FILE_CHUNK_SIZE         = 64 * 1024;        // 64 KB per file chunk
constexpr int    TILE_SIZE               = 64;               // 64x64 dirty-tile grid

enum class PacketType : uint8_t {
    // Handshake & Authentication
    HELLO               = 0x01,
    AUTH_CHALLENGE      = 0x02,
    AUTH_RESPONSE       = 0x03,
    AUTH_WAITING        = 0x04, // Waiting for host user to click Accept/Reject
    AUTH_RESULT         = 0x05,
    PERMISSION_UPDATE   = 0x06,
    PING                = 0x07,
    PONG                = 0x08,
    DISCONNECT          = 0x09,

    // Video & Monitor Control
    VIDEO_CONFIG        = 0x10,
    VIDEO_FRAME_TILES   = 0x11,
    CURSOR_UPDATE       = 0x12,
    VIDEO_CONTROL_REQ   = 0x13, // Quality preset, keyframe request, or monitor switch

    // Remote Input Injection
    INPUT_MOUSE_MOVE    = 0x20,
    INPUT_MOUSE_BUTTON  = 0x21,
    INPUT_MOUSE_WHEEL   = 0x22,
    INPUT_KEY_EVENT     = 0x23,
    INPUT_RELEASE_ALL   = 0x24, // Release all pressed modifier keys on focus loss / tab switch
    SYSTEM_ACTION       = 0x25, // Trigger remote system action (Task Manager, Show Desktop, Lock PC)

    // Clipboard, File Transfer & Live Chat
    CLIPBOARD_TEXT      = 0x30,
    FILE_OFFER          = 0x31,
    FILE_ACCEPT         = 0x32,
    FILE_CHUNK          = 0x33,
    FILE_COMPLETE       = 0x34,
    FILE_CANCEL         = 0x35,
    CHAT_MESSAGE        = 0x36,

    // Relay & Rendezvous Protocol
    RELAY_REGISTER      = 0x50,
    RELAY_REGISTER_ACK  = 0x51,
    RELAY_LOOKUP        = 0x52,
    RELAY_LOOKUP_RESP   = 0x53,
    RELAY_CONNECT_REQ   = 0x54,
    RELAY_INCOMING_REQ  = 0x55,
    RELAY_BRIDGE_ACCEPT = 0x56,
    RELAY_BRIDGE_READY  = 0x57
};

enum class SystemActionType : uint8_t {
    TaskManager     = 1,
    ShowDesktop     = 2,
    LockWorkstation = 3
};

enum FrameFlags : uint8_t {
    FLAG_NONE      = 0x00,
    FLAG_ENCRYPTED = 0x01,
    FLAG_KEYFRAME  = 0x02
};

enum PermissionFlags : uint8_t {
    PERM_NONE          = 0x00,
    PERM_INPUT         = 0x01, // Mouse & Keyboard control
    PERM_CLIPBOARD     = 0x02, // Bidirectional clipboard sync
    PERM_FILE_TRANSFER = 0x04, // Send/receive files
    PERM_ALL           = PERM_INPUT | PERM_CLIPBOARD | PERM_FILE_TRANSFER
};

enum class QualityPreset : uint8_t {
    Ultra        = 0, // Lossless Zstd for UI + JPEG Q90, 60 FPS target
    Balanced     = 1, // Hybrid Zstd / JPEG Q75, 30 FPS target
    LowBandwidth = 2  // JPEG Q50, 15-20 FPS target
};

inline uint8_t clampTargetFps(uint8_t fps) {
    if (fps <= 20) return 15;
    if (fps <= 45) return 30;
    return 60;
}

// Automatically drops effective FPS (60 -> 30 -> 15) when network RTT or TCP send duration indicates poor connection
inline uint8_t computeAdaptiveFpsCap(uint8_t userTargetFps, bool adaptiveEnabled, uint32_t rttMs, float avgSendMs) {
    uint8_t target = clampTargetFps(userTargetFps);
    if (!adaptiveEnabled) {
        return target;
    }
    if (rttMs >= 180 || avgSendMs >= 75.0f) {
        return 15;
    }
    if (rttMs >= 95 || avgSendMs >= 38.0f) {
        return std::min<uint8_t>(target, 30);
    }
    return target;
}

enum class TileEncoding : uint8_t {
    RawBGRA = 0,
    Zstd    = 1,
    Jpeg    = 2
};

enum class AuthResultCode : uint8_t {
    Accepted         = 0,
    RejectedByUser   = 1,
    InvalidPassword  = 2,
    HostBusy         = 3,
    ProtocolError    = 4,
    RateLimited      = 5
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;       // PROTOCOL_MAGIC
    uint8_t  type;        // PacketType
    uint8_t  flags;       // FrameFlags
    uint32_t payloadSize; // Payload byte count
};

struct DiscoveryBeaconPacket {
    uint32_t magic;       // PROTOCOL_MAGIC
    uint8_t  packetKind;  // 1 = Announce, 2 = QueryById
    uint64_t deskId;      // 9-digit numeric ID
    uint64_t queryId;     // Target ID when packetKind == 2
    uint16_t tcpPort;     // Host listening port
    char     hostname[64];
};

struct TileHeader {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t  encoding;    // TileEncoding
    uint32_t dataSize;
};
#pragma pack(pop)

struct MonitorDesc {
    int32_t     index = 0;
    int32_t     x = 0;
    int32_t     y = 0;
    int32_t     width = 1920;
    int32_t     height = 1080;
    bool        isPrimary = true;
    std::string name;
};

struct EncodedTile {
    uint16_t             x = 0;
    uint16_t             y = 0;
    uint16_t             width = 0;
    uint16_t             height = 0;
    TileEncoding         encoding = TileEncoding::Zstd;
    std::vector<uint8_t> data;
};

// Binary serialization helper
class ByteWriter {
public:
    void writeU8(uint8_t v) { buf_.push_back(v); }
    void writeU16(uint16_t v) { writeBytes(&v, sizeof(v)); }
    void writeU32(uint32_t v) { writeBytes(&v, sizeof(v)); }
    void writeI32(int32_t v) { writeBytes(&v, sizeof(v)); }
    void writeU64(uint64_t v) { writeBytes(&v, sizeof(v)); }
    void writeF32(float v) { writeBytes(&v, sizeof(v)); }

    void writeString(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 65535));
        writeU16(len);
        if (len > 0) {
            writeBytes(s.data(), len);
        }
    }

    void writeBytes(const void* ptr, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        buf_.insert(buf_.end(), p, p + len);
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> takeBuffer() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}
    explicit ByteReader(const std::vector<uint8_t>& vec) : data_(vec.data()), size_(vec.size()), pos_(0) {}

    bool hasRemaining(size_t n) const { return pos_ + n <= size_; }
    size_t remaining() const { return size_ - pos_; }

    uint8_t readU8() {
        require(1);
        return data_[pos_++];
    }

    uint16_t readU16() {
        uint16_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    uint32_t readU32() {
        uint32_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    int32_t readI32() {
        int32_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    uint64_t readU64() {
        uint64_t v = 0;
        readBytes(&v, sizeof(v));
        return v;
    }

    float readF32() {
        float v = 0.0f;
        readBytes(&v, sizeof(v));
        return v;
    }

    std::string readString() {
        uint16_t len = readU16();
        require(len);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }

    void readBytes(void* out, size_t len) {
        require(len);
        if (len > 0) {
            std::memcpy(out, data_ + pos_, len);
            pos_ += len;
        }
    }

    const uint8_t* currentPtr() const { return data_ + pos_; }
    void skip(size_t len) {
        require(len);
        pos_ += len;
    }

private:
    void require(size_t n) const {
        if (pos_ + n > size_) {
            throw std::runtime_error("Packet buffer underflow");
        }
    }

    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
};

} // namespace aerodesk
