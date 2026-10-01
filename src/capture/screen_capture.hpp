#pragma once

#include "../core/protocol.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

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

// ---------------- TileThreadPool (Option 2A) ----------------
// High-performance CPU worker thread pool for parallel tile compression
class TileThreadPool {
public:
    static TileThreadPool& instance();

    TileThreadPool();
    ~TileThreadPool();

    TileThreadPool(const TileThreadPool&) = delete;
    TileThreadPool& operator=(const TileThreadPool&) = delete;

    struct RectTask {
        uint16_t rx = 0;
        uint16_t ry = 0;
        uint16_t rw = 0;
        uint16_t rh = 0;
        std::vector<uint8_t> bgraPixels;
        QualityPreset preset = QualityPreset::Balanced;
        EncodedTile result;
    };

    void parallelEncode(std::vector<RectTask>& tasks);

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::mutex               mutex_;
    std::condition_variable  cvTask_;
    std::condition_variable  cvDone_;
    std::atomic<bool>        stop_{false};

    std::vector<RectTask*>   activeBatch_;
    std::atomic<size_t>      nextTaskIdx_{0};
    std::atomic<size_t>      remainingTasks_{0};
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
