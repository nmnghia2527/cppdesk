#include "clipboard_file_manager.hpp"
#include "../core/crypto_identity.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>

#include <filesystem>
#include <algorithm>
#include <cstring>

namespace cppdesk {

namespace {

constexpr uint64_t MAX_TRANSFER_FILE_BYTES = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2 GB cap

std::string sanitizeFilename(const std::string& rawName) {
    std::filesystem::path p(rawName);
    std::string base = p.filename().string();
    if (base.empty() || base == "." || base == "..") base = "received_file.bin";
    for (char& c : base) {
        if (c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ||
            static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    return base;
}

std::string formatBytesSize(uint64_t bytes) {
    char buf[64];
    if (bytes < 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return std::string(buf);
}

} // namespace

struct FileTransferManager::IncrementalSha256 {
    BCRYPT_ALG_HANDLE  hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    bool               valid = false;

    IncrementalSha256() {
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
            if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
                valid = true;
            }
        }
    }

    ~IncrementalSha256() {
        if (hHash) BCryptDestroyHash(hHash);
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    void update(const void* data, size_t len) {
        if (valid && data && len > 0) {
            BCryptHashData(hHash, const_cast<PUCHAR>(static_cast<const uint8_t*>(data)), static_cast<ULONG>(len), 0);
        }
    }

    std::string finishHex() {
        std::array<uint8_t, 32> digest{};
        if (valid && hHash) {
            BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);
            BCryptDestroyHash(hHash);
            hHash = nullptr;
            valid = false;
        }
        return CryptoUtils::toHex(digest.data(), digest.size());
    }
};

// ---------------- ClipboardManager ----------------

ClipboardManager::ClipboardManager() {
    lastSeqNumber_ = GetClipboardSequenceNumber();
}

std::string ClipboardManager::getClipboardUtf8() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return {};
    }
    if (!OpenClipboard(nullptr)) {
        return {};
    }

    std::string result;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* wText = static_cast<const wchar_t*>(GlobalLock(hData));
        if (wText) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wText, -1, nullptr, 0, nullptr, nullptr);
            if (utf8Len > 1) {
                result.resize(static_cast<size_t>(utf8Len - 1));
                WideCharToMultiByte(CP_UTF8, 0, wText, -1, result.data(), utf8Len, nullptr, nullptr);
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

bool ClipboardManager::setClipboardUtf8(const std::string& utf8Text) {
    if (utf8Text.empty()) return false;

    int wLen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, nullptr, 0);
    if (wLen <= 0) return false;

    if (!OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();

    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(wLen) * sizeof(wchar_t));
    if (!hGlobal) {
        CloseClipboard();
        return false;
    }

    wchar_t* wBuf = static_cast<wchar_t*>(GlobalLock(hGlobal));
    if (wBuf) {
        MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, wBuf, wLen);
        GlobalUnlock(hGlobal);
        SetClipboardData(CF_UNICODETEXT, hGlobal);
    } else {
        GlobalFree(hGlobal);
    }

    CloseClipboard();
    return true;
}

bool ClipboardManager::pollLocalChange(std::string& outNewText) {
    uint32_t seq = GetClipboardSequenceNumber();
    if (seq == lastSeqNumber_) {
        return false;
    }
    lastSeqNumber_ = seq;

    std::string txt = getClipboardUtf8();
    if (txt.empty() || txt.size() > 256 * 1024) {
        return false;
    }

    std::string h = CryptoUtils::sha256Hex(txt);
    if (h == lastAppliedHash_) {
        return false;
    }
    lastAppliedHash_ = h;
    outNewText = std::move(txt);
    return true;
}

void ClipboardManager::applyRemoteClipboard(const std::string& utf8Text) {
    if (utf8Text.empty()) return;
    lastAppliedHash_ = CryptoUtils::sha256Hex(utf8Text);
    setClipboardUtf8(utf8Text);
    lastSeqNumber_ = GetClipboardSequenceNumber();
}

// ---------------- FileTransferManager ----------------

FileTransferManager::FileTransferManager() {
    char profileDir[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, profileDir))) {
        std::filesystem::path dl = std::filesystem::path(profileDir) / "Downloads" / "CppDesk_Received";
        std::error_code ec;
        std::filesystem::create_directories(dl, ec);
        receiveDir_ = dl.string();
    } else {
        receiveDir_ = "CppDesk_Received";
        std::error_code ec;
        std::filesystem::create_directories(receiveDir_, ec);
    }
}

FileTransferManager::~FileTransferManager() {
    abortActiveTransfers();
}

void FileTransferManager::setReceiveDirectory(const std::string& dirPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    receiveDir_ = dirPath;
    std::error_code ec;
    std::filesystem::create_directories(receiveDir_, ec);
}

std::string FileTransferManager::receiveDirectory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return receiveDir_;
}

void FileTransferManager::openReceiveDirectoryInExplorer() const {
    std::string dir = receiveDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

uint32_t FileTransferManager::startOutgoingFile(const std::string& filePath, const SendPacketFn& sendPacket) {
    std::filesystem::path p(filePath);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        return 0;
    }

    uint64_t fsize = std::filesystem::file_size(p, ec);
    if (fsize > MAX_TRANSFER_FILE_BYTES) {
        return 0;
    }

    auto out = std::make_unique<ActiveOutgoing>();
    out->filePath = filePath;
    out->fileName = sanitizeFilename(p.filename().string());
    out->totalBytes = fsize;
    out->offset = 0;
    out->hasher = std::make_unique<IncrementalSha256>();
    out->stream.open(p, std::ios::binary);
    if (!out->stream.is_open()) {
        return 0;
    }

    uint32_t tid = 0;
    std::string fname;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tid = nextTransferId_++;
        out->transferId = tid;
        fname = out->fileName;

        FileTransferItem item;
        item.transferId = tid;
        item.fileName = fname;
        item.savedPath = filePath;
        item.totalBytes = fsize;
        item.transferredBytes = 0;
        item.isOutgoing = true;
        item.status = TransferStatus::InProgress;
        item.statusText = "Sending (0%)";
        items_.insert(items_.begin(), item);
        if (items_.size() > 150) {
            items_.pop_back();
        }

        outgoingQueue_.push_back(std::move(out));
    }

    if (sendPacket) {
        ByteWriter w;
        w.writeU32(tid);
        w.writeU64(fsize);
        w.writeString(fname);
        sendPacket(PacketType::FILE_OFFER, w.buffer());
    }
    return tid;
}

bool FileTransferManager::pumpOutgoingChunks(const SendPacketFn& sendPacket, int maxChunks) {
    if (!sendPacket) return false;
    bool didWork = false;

    for (int i = 0; i < maxChunks; ++i) {
        uint32_t tid = 0;
        uint64_t offset = 0;
        std::vector<uint8_t> chunkBuf;
        bool finished = false;
        std::string finalShaHex;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (outgoingQueue_.empty()) break;

            auto& active = outgoingQueue_.front();
            tid = active->transferId;
            offset = active->offset;

            if (active->offset < active->totalBytes && active->stream.good()) {
                size_t toRead = static_cast<size_t>(
                    std::min<uint64_t>(FILE_CHUNK_SIZE, active->totalBytes - active->offset)
                );
                chunkBuf.resize(toRead);
                active->stream.read(reinterpret_cast<char*>(chunkBuf.data()), static_cast<std::streamsize>(toRead));
                std::streamsize got = active->stream.gcount();
                if (got > 0) {
                    chunkBuf.resize(static_cast<size_t>(got));
                    if (active->hasher) {
                        active->hasher->update(chunkBuf.data(), chunkBuf.size());
                    }
                    active->offset += static_cast<uint64_t>(got);
                } else {
                    finished = true;
                }
            } else {
                finished = true;
            }

            if (active->offset >= active->totalBytes) {
                finished = true;
            }

            if (finished && active->hasher) {
                finalShaHex = active->hasher->finishHex();
            }

            for (auto& item : items_) {
                if (item.transferId == tid && item.isOutgoing) {
                    item.transferredBytes = active->offset;
                    if (finished) {
                        item.status = TransferStatus::Completed;
                        item.sha256Hex = finalShaHex;
                        item.statusText = "Sent (" + formatBytesSize(item.totalBytes) + ")";
                    } else {
                        int pct = static_cast<int>(item.progressFraction() * 100.0f);
                        item.statusText = "Sending (" + std::to_string(pct) + "%)";
                    }
                    break;
                }
            }

            if (finished) {
                active->stream.close();
                outgoingQueue_.erase(outgoingQueue_.begin());
            }
        }

        if (!chunkBuf.empty()) {
            ByteWriter w;
            w.writeU32(tid);
            w.writeU64(offset);
            w.writeU32(static_cast<uint32_t>(chunkBuf.size()));
            w.writeBytes(chunkBuf.data(), chunkBuf.size());
            sendPacket(PacketType::FILE_CHUNK, w.buffer());
            didWork = true;
        }

        if (finished) {
            ByteWriter w;
            w.writeU32(tid);
            w.writeString(finalShaHex);
            sendPacket(PacketType::FILE_COMPLETE, w.buffer());
            didWork = true;
        }
    }
    return didWork;
}

bool FileTransferManager::cancelTransfer(uint32_t transferId, const SendPacketFn& sendPacket) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = outgoingQueue_.begin(); it != outgoingQueue_.end(); ++it) {
            if ((*it)->transferId == transferId) {
                (*it)->stream.close();
                outgoingQueue_.erase(it);
                found = true;
                break;
            }
        }
        for (auto it = incomingStreams_.begin(); it != incomingStreams_.end(); ++it) {
            if ((*it)->transferId == transferId) {
                (*it)->stream.close();
                std::error_code ec;
                std::filesystem::remove((*it)->partPath, ec);
                incomingStreams_.erase(it);
                found = true;
                break;
            }
        }
        for (auto& item : items_) {
            if (item.transferId == transferId && item.status == TransferStatus::InProgress) {
                item.status = TransferStatus::Cancelled;
                item.statusText = "Cancelled";
                found = true;
                break;
            }
        }
    }
    if (found && sendPacket) {
        ByteWriter w;
        w.writeU32(transferId);
        sendPacket(PacketType::FILE_CANCEL, w.buffer());
    }
    return found;
}

void FileTransferManager::abortActiveTransfers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& out : outgoingQueue_) {
        out->stream.close();
    }
    outgoingQueue_.clear();

    for (auto& inc : incomingStreams_) {
        inc->stream.close();
        std::error_code ec;
        std::filesystem::remove(inc->partPath, ec);
    }
    incomingStreams_.clear();

    for (auto& item : items_) {
        if (item.status == TransferStatus::InProgress) {
            item.status = TransferStatus::Cancelled;
            item.statusText = "Interrupted";
        }
    }
}

void FileTransferManager::handleFileOffer(uint32_t transferId, uint64_t totalBytes, const std::string& fileName) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    std::filesystem::create_directories(receiveDir_, ec);

    std::string cleanName = sanitizeFilename(fileName);
    std::filesystem::path targetPath = std::filesystem::path(receiveDir_) / cleanName;

    // Verify file size limit and available disk space
    bool spaceOk = (totalBytes <= MAX_TRANSFER_FILE_BYTES);
    if (spaceOk) {
        ULARGE_INTEGER freeBytesAvailable{};
        if (GetDiskFreeSpaceExA(receiveDir_.c_str(), &freeBytesAvailable, nullptr, nullptr)) {
            if (freeBytesAvailable.QuadPart > 0 && totalBytes + (16ULL * 1024 * 1024) > freeBytesAvailable.QuadPart) {
                spaceOk = false;
            }
        }
    }

    if (!spaceOk) {
        FileTransferItem item;
        item.transferId = transferId;
        item.fileName = cleanName;
        item.totalBytes = totalBytes;
        item.isOutgoing = false;
        item.status = TransferStatus::Failed;
        item.statusText = "Rejected (Size/Disk Limit)";
        items_.insert(items_.begin(), item);
        return;
    }

    // Avoid overwriting existing files by appending (1), (2), ...
    if (std::filesystem::exists(targetPath, ec)) {
        std::string stem = targetPath.stem().string();
        std::string ext = targetPath.extension().string();
        for (int n = 1; n < 1000; ++n) {
            auto candidate = std::filesystem::path(receiveDir_) / (stem + " (" + std::to_string(n) + ")" + ext);
            if (!std::filesystem::exists(candidate, ec)) {
                targetPath = candidate;
                break;
            }
        }
    }

    std::string partPathStr = targetPath.string() + ".part";
    auto inc = std::make_unique<ActiveIncoming>();
    inc->transferId = transferId;
    inc->fileName = targetPath.filename().string();
    inc->partPath = partPathStr;
    inc->savePath = targetPath.string();
    inc->totalBytes = totalBytes;
    inc->receivedBytes = 0;
    inc->hasher = std::make_unique<IncrementalSha256>();
    inc->stream.open(partPathStr, std::ios::binary | std::ios::trunc);

    FileTransferItem item;
    item.transferId = transferId;
    item.fileName = inc->fileName;
    item.savedPath = inc->savePath;
    item.totalBytes = totalBytes;
    item.transferredBytes = 0;
    item.isOutgoing = false;
    if (inc->stream.is_open()) {
        item.status = TransferStatus::InProgress;
        item.statusText = (totalBytes == 0) ? "Receiving (0 B)" : "Receiving (0%)";
        incomingStreams_.push_back(std::move(inc));
    } else {
        item.status = TransferStatus::Failed;
        item.statusText = "Write Failed";
    }
    items_.insert(items_.begin(), item);
    if (items_.size() > 150) {
        items_.pop_back();
    }
}

void FileTransferManager::handleFileChunk(uint32_t transferId, uint64_t /*offset*/, const uint8_t* chunkData, size_t chunkLen) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& inc : incomingStreams_) {
        if (inc->transferId == transferId) {
            if (inc->stream.is_open() && chunkLen > 0) {
                if (inc->receivedBytes + chunkLen <= MAX_TRANSFER_FILE_BYTES) {
                    inc->stream.write(reinterpret_cast<const char*>(chunkData), static_cast<std::streamsize>(chunkLen));
                    if (inc->hasher) {
                        inc->hasher->update(chunkData, chunkLen);
                    }
                    inc->receivedBytes += chunkLen;
                }
            }
            for (auto& item : items_) {
                if (item.transferId == transferId && !item.isOutgoing) {
                    item.transferredBytes = inc->receivedBytes;
                    int pct = static_cast<int>(item.progressFraction() * 100.0f);
                    item.statusText = "Receiving (" + std::to_string(pct) + "%)";
                    break;
                }
            }
            break;
        }
    }
}

void FileTransferManager::handleFileComplete(uint32_t transferId, const std::string& sha256Hex) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string computedSha;
    std::string partPath;
    std::string finalPath;
    bool foundStream = false;

    for (auto it = incomingStreams_.begin(); it != incomingStreams_.end(); ++it) {
        if ((*it)->transferId == transferId) {
            (*it)->stream.flush();
            (*it)->stream.close();
            if ((*it)->hasher) {
                computedSha = (*it)->hasher->finishHex();
            }
            partPath = (*it)->partPath;
            finalPath = (*it)->savePath;
            foundStream = true;
            incomingStreams_.erase(it);
            break;
        }
    }

    bool hashValid = sha256Hex.empty() || (computedSha == sha256Hex);
    std::error_code ec;
    if (foundStream) {
        if (hashValid) {
            std::filesystem::remove(finalPath, ec);
            std::filesystem::rename(partPath, finalPath, ec);
        } else {
            std::filesystem::remove(partPath, ec);
        }
    }

    for (auto& item : items_) {
        if (item.transferId == transferId && !item.isOutgoing) {
            if (hashValid) {
                item.transferredBytes = item.totalBytes;
                item.status = TransferStatus::Completed;
                item.sha256Hex = computedSha;
                item.statusText = "Verified (" + formatBytesSize(item.totalBytes) + ")";
            } else {
                item.status = TransferStatus::Failed;
                item.statusText = "Failed (SHA-256 Mismatch)";
            }
            break;
        }
    }
}

void FileTransferManager::handleFileCancel(uint32_t transferId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = incomingStreams_.begin(); it != incomingStreams_.end(); ++it) {
        if ((*it)->transferId == transferId) {
            (*it)->stream.close();
            std::error_code ec;
            std::filesystem::remove((*it)->partPath, ec);
            incomingStreams_.erase(it);
            break;
        }
    }
    for (auto it = outgoingQueue_.begin(); it != outgoingQueue_.end(); ++it) {
        if ((*it)->transferId == transferId) {
            (*it)->stream.close();
            outgoingQueue_.erase(it);
            break;
        }
    }
    for (auto& item : items_) {
        if (item.transferId == transferId) {
            item.status = TransferStatus::Cancelled;
            item.statusText = "Cancelled";
            break;
        }
    }
}

void FileTransferManager::clearCompleted() {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.erase(
        std::remove_if(items_.begin(), items_.end(), [](const FileTransferItem& it) {
            return it.status != TransferStatus::InProgress;
        }),
        items_.end()
    );
}

std::vector<FileTransferItem> FileTransferManager::snapshotTransfers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

} // namespace cppdesk
