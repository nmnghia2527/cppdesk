#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace cppdesk {

constexpr uint32_t PROTOCOL_MAGIC   = 0x43505044; // "CPPD" (CppDesk packet magic)
constexpr uint32_t RELAY_MAGIC      = 0x4344534B; // "CDSK" (CppDesk relay magic)
constexpr uint16_t PROTOCOL_VERSION = 3;
constexpr const char* CPP_DESK_VERSION = "3.0.0";
constexpr uint32_t CPP_DESK_VERSION_NUM = 0x030000;

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
    MONITOR_LIST        = 0x14, // Host -> Viewer: List of active remote monitors
    MONITOR_SELECT      = 0x15, // Viewer -> Host: Request active monitor switch
    QUALITY_UPDATE      = 0x16, // Viewer -> Host: Live quality preset & FPS update
    AUDIO_STREAM_CHUNK  = 0x17, // Host -> Viewer: 48kHz 16-bit stereo PCM audio samples
    PRIVACY_MODE_TOGGLE = 0x18, // Viewer <-> Host: Enable/Disable Host Privacy Screen & input lock
    TUNNEL_OPEN         = 0x19, // Viewer -> Host: Request proxy connection to target port
    TUNNEL_DATA         = 0x1A, // Bidirectional: Multiplexed TCP payload chunk
    TUNNEL_CLOSE        = 0x1B, // Bidirectional: Terminate tunnel connection
    TERMINAL_DATA       = 0x1C, // Bidirectional: Interactive command shell I/O
    WHITEBOARD_PACKET   = 0x1D, // Bidirectional: Whiteboard strokes & laser pointer updates

    // Remote Input Injection
    INPUT_MOUSE_MOVE    = 0x20,
    INPUT_MOUSE_BUTTON  = 0x21,
    INPUT_MOUSE_WHEEL   = 0x22,
    INPUT_KEY_EVENT     = 0x23,
    INPUT_RELEASE_ALL   = 0x24, // Release all pressed modifier keys on focus loss / tab switch
    SYSTEM_ACTION       = 0x25, // Trigger remote system action (Task Manager, Show Desktop, Lock PC, SAS, Reboot)

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
    LockWorkstation = 3,
    SendCtrlAltDel  = 4,
    EmergencyReboot = 5
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

using CppDeskHeader = FrameHeader;

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

struct AudioChunkHeader {
    uint32_t sampleRate;     // Target sample rate (e.g. 48000)
    uint8_t  channels;       // Channel count (e.g. 2 for stereo)
    uint8_t  bitsPerSample;  // e.g. 16
    uint8_t  isSilent;       // 1 = silent/zero payload, 0 = active PCM
    uint32_t sampleFrames;   // number of sample frames in this chunk
};

struct PrivacyModePayload {
    uint8_t enable;          // 1 = engage, 0 = disengage
    uint8_t acknowledge;     // 0 = request, 1 = ACK confirmation
};

struct TunnelOpenHeader {
    uint32_t tunnelId;
    uint16_t targetPort;
    uint8_t  flags;
};

struct TunnelDataHeader {
    uint32_t tunnelId;
    uint32_t dataLen;
};

struct TunnelCloseHeader {
    uint32_t tunnelId;
    uint8_t  reasonCode;     // 0 = Normal, 1 = Refused, 2 = Timeout/Reset
};

enum class TerminalStreamKind : uint8_t {
    StdoutChunk = 1,
    StdinInput  = 2,
    ResetShell  = 3,
    SwitchShell = 4
};

struct TerminalDataHeader {
    uint8_t  streamKind;     // TerminalStreamKind
    uint32_t textLen;
};

enum class WhiteboardTool : uint8_t {
    Pen          = 0,
    Highlighter  = 1,
    Arrow        = 2,
    Laser        = 3,
    LaserPointer = 3,
    ClearAll     = 4
};

enum class WhiteboardAction : uint8_t {
    BeginStroke  = 0,
    AppendPoints = 1,
    EndStroke    = 2,
    LaserUpdate  = 3,
    ClearCanvas  = 4
};

struct WhiteboardPoint {
    float x; // Normalized 0.0 to 1.0
    float y; // Normalized 0.0 to 1.0
};
#pragma pack(pop)

struct WhiteboardStroke {
    uint32_t strokeId = 0;
    WhiteboardTool tool = WhiteboardTool::Pen;
    uint32_t colorRgba = 0xE50914FF; // Default Crimson Red
    float strokeWidth = 3.0f;
    std::vector<WhiteboardPoint> points;
};

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
    std::string toString() const {
        return std::string(reinterpret_cast<const char*>(buf_.data()), buf_.size());
    }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}
    explicit ByteReader(const std::vector<uint8_t>& vec) : data_(vec.data()), size_(vec.size()), pos_(0) {}

    bool hasRemaining(size_t n) const { return pos_ + n <= size_; }
    size_t remaining() const { return size_ - pos_; }

    std::vector<uint8_t> readBytesVector(size_t n) {
        require(n);
        std::vector<uint8_t> v(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return v;
    }

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
        if (n > size_ || pos_ > size_ - n) {
            throw std::runtime_error("Packet buffer underflow");
        }
    }

    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
};

} // namespace cppdesk
