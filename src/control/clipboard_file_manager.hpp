#pragma once

#include "../core/protocol.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <memory>
#include <functional>

namespace cppdesk {

enum class TransferStatus : uint8_t {
    InProgress = 0,
    Completed  = 1,
    Failed     = 2,
    Cancelled  = 3
};

struct FileTransferItem {
    uint32_t       transferId = 0;
    std::string    fileName;
    std::string    savedPath;
    uint64_t       totalBytes = 0;
    uint64_t       transferredBytes = 0;
    bool           isOutgoing = false;
    TransferStatus status = TransferStatus::InProgress;
    std::string    statusText;
    std::string    sha256Hex;

    float progressFraction() const {
        if (totalBytes == 0) return status == TransferStatus::Completed ? 1.0f : 0.0f;
        return static_cast<float>(static_cast<double>(transferredBytes) / static_cast<double>(totalBytes));
    }
};

class ClipboardManager {
public:
    ClipboardManager();

    static std::string getClipboardUtf8();
    static bool setClipboardUtf8(const std::string& utf8Text);

    // Returns true if user copied new text locally (ignores changes we applied from remote)
    bool pollLocalChange(std::string& outNewText);

    // Apply incoming remote clipboard text and record hash to avoid echo loop
    void applyRemoteClipboard(const std::string& utf8Text);

private:
    uint32_t    lastSeqNumber_ = 0;
    std::string lastAppliedHash_;
};

class FileTransferManager {
public:
    using SendPacketFn = std::function<bool(PacketType, const std::vector<uint8_t>&)>;

    FileTransferManager();
    ~FileTransferManager();

    void setReceiveDirectory(const std::string& dirPath);
    std::string receiveDirectory() const;
    void openReceiveDirectoryInExplorer() const;

    // Queue a local file to be sent to the connected peer
    uint32_t startOutgoingFile(const std::string& filePath, const SendPacketFn& sendPacket,
                               FileOfferTarget targetHint = FileOfferTarget::DefaultDownloads,
                               float dropNx = 0.0f, float dropNy = 0.0f);

    // Recursively queue a file or directory tree for transfer
    int startOutgoingPath(const std::string& path, const SendPacketFn& sendPacket,
                          FileOfferTarget targetHint = FileOfferTarget::DefaultDownloads,
                          float dropNx = 0.0f, float dropNy = 0.0f);

    // Pump up to maxChunks outgoing chunks (called periodically by session worker)
    bool pumpOutgoingChunks(const SendPacketFn& sendPacket, int maxChunks = 4);

    // Cancel an active outgoing or incoming transfer and notify peer
    bool cancelTransfer(uint32_t transferId, const SendPacketFn& sendPacket = nullptr);

    // Abort all in-progress transfers on unexpected session disconnect and purge .part files
    void abortActiveTransfers();

    // Handle incoming file transfer packets from remote peer
    void handleFileOffer(uint32_t transferId, uint64_t totalBytes, const std::string& fileName,
                         FileOfferTarget targetHint = FileOfferTarget::DefaultDownloads,
                         float dropNx = 0.0f, float dropNy = 0.0f);
    void handleFileChunk(uint32_t transferId, uint64_t offset, const uint8_t* chunkData, size_t chunkLen);
    void handleFileComplete(uint32_t transferId, const std::string& sha256Hex);
    void handleFileCancel(uint32_t transferId);

    void clearCompleted();
    std::vector<FileTransferItem> snapshotTransfers() const;
    bool hasActiveTransfers() const;

private:
    struct IncrementalSha256;

    struct ActiveOutgoing {
        uint32_t                           transferId = 0;
        std::string                        filePath;
        std::string                        fileName;
        uint64_t                           totalBytes = 0;
        uint64_t                           offset = 0;
        std::ifstream                      stream;
        std::unique_ptr<IncrementalSha256> hasher;
    };

    struct ActiveIncoming {
        uint32_t                           transferId = 0;
        std::string                        fileName;
        std::string                        partPath;
        std::string                        savePath;
        uint64_t                           totalBytes = 0;
        uint64_t                           receivedBytes = 0;
        std::ofstream                      stream;
        std::unique_ptr<IncrementalSha256> hasher;
    };

    mutable std::mutex                             mutex_;
    std::string                                    receiveDir_;
    uint32_t                                       nextTransferId_ = 1;
    std::vector<FileTransferItem>                  items_;
    std::vector<std::unique_ptr<ActiveOutgoing>>   outgoingQueue_;
    std::vector<std::unique_ptr<ActiveIncoming>>   incomingStreams_;
};

} // namespace cppdesk
