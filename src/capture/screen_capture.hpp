#pragma once

#include "../core/protocol.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace aerodesk {

struct CursorState {
    float normX = 0.0f;
    float normY = 0.0f;
    bool  visible = true;
};

class TileCodec {
public:
    static void initGdiPlus();
    static void shutdownGdiPlus();

    // Encode a contiguous BGRA rectangle (width * height * 4 bytes) into an EncodedTile
    static EncodedTile encodeRect(
        uint16_t x,
        uint16_t y,
        uint16_t width,
        uint16_t height,
        const uint8_t* bgraRect,
        QualityPreset preset);

    // Decode an EncodedTile and blit it into a full-frame BGRA canvas (canvasW * canvasH * 4)
    static bool decodeTileIntoCanvas(
        const EncodedTile& tile,
        uint8_t* canvasBgra,
        int canvasW,
        int canvasH);

    // Compress/decompress helpers exposed for testing
    static std::vector<uint8_t> compressZstd(const void* src, size_t srcLen, int level = 1);
    static bool decompressZstd(const void* src, size_t srcLen, void* dst, size_t dstCapacity);
    static std::vector<uint8_t> encodeJpeg(const uint8_t* bgra, int width, int height, int quality);
    static bool decodeJpeg(const uint8_t* jpegData, size_t jpegLen, std::vector<uint8_t>& outBgra, int& outW, int& outH);
};

class ScreenCapturer {
public:
    ScreenCapturer();
    ~ScreenCapturer();

    // Enumerate all connected displays
    std::vector<MonitorDesc> enumerateMonitors();

    // Select active monitor index (0-based)
    bool selectMonitor(int monitorIndex);
    int currentMonitorIndex() const { return activeMonitorIdx_; }
    MonitorDesc currentMonitor() const { return activeMonitor_; }

    // Capture current screen and produce dirty tiles since previous frame
    bool captureDirtyTiles(
        bool forceKeyframe,
        QualityPreset preset,
        std::vector<EncodedTile>& outTiles,
        bool& outIsKeyframe,
        CursorState& outCursor);

    // Raw frame access for tests
    int frameWidth() const { return frameW_; }
    int frameHeight() const { return frameH_; }
    const std::vector<uint8_t>& currentFrameBgra() const { return currentFrame_; }
    bool usingDxgi() const { return dxgiInitialized_; }

private:
    bool initDxgiForMonitor(int monitorIndex);
    void releaseDxgi();
    bool captureViaDxgi(bool& outFrameUpdated);
    bool captureViaGdi();
    CursorState captureCursorState() const;

    struct DxgiImpl;
    std::unique_ptr<DxgiImpl> dxgi_;
    bool                      dxgiInitialized_ = false;
    bool                      hasValidFrame_ = false;

    std::vector<MonitorDesc>  monitors_;
    int                       activeMonitorIdx_ = 0;
    MonitorDesc               activeMonitor_;

    int                       frameW_ = 0;
    int                       frameH_ = 0;
    std::vector<uint8_t>      currentFrame_;
    std::vector<uint64_t>     prevTileHashes_;
};

} // namespace aerodesk
