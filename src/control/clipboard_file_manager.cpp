#include "clipboard_file_manager.hpp"
#include "../core/crypto_identity.hpp"
#include "../capture/screen_capture.hpp"

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

// ---------------- ClipboardHistoryManager & Classify ----------------

static std::string classifyClipboardText(const std::string& text) {
    if (text.empty()) return "Text";
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start != std::string::npos) {
        std::string sub = text.substr(start);
        if (sub.rfind("http://", 0) == 0 || sub.rfind("https://", 0) == 0 || sub.rfind("www.", 0) == 0) {
            return "URL";
        }
    }
    if ((text.size() >= 3 && text[1] == ':' && (text[2] == '\\' || text[2] == '/')) ||
        (text.size() >= 2 && text[0] == '\\' && text[1] == '\\') ||
        (text.size() >= 1 && text[0] == '/' && text.find(' ') == std::string::npos)) {
        return "Path";
    }
    static const char* codeKeywords[] = {
        "{", "}", "class ", "struct ", "function ", "def ", "public:", "private:",
        "#include", "import ", "const ", "let ", "var ", "=>", "namespace ", "return;"
    };
    for (const char* kw : codeKeywords) {
        if (text.find(kw) != std::string::npos) {
            return "Code";
        }
    }
    return "Text";
}

void ClipboardHistoryManager::addItem(const std::string& text, bool isFromRemote) {
    if (text.empty()) return;
    bool allWs = std::all_of(text.begin(), text.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
    if (allWs) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!items_.empty() && items_.front().text == text) {
        return;
    }

    ClipboardHistoryItem item;
    item.id = nextId_++;
    item.text = text;
    item.typeBadge = classifyClipboardText(text);
    item.timestampMs = GetTickCount64();
    item.isFromRemote = isFromRemote;
    item.charCount = text.size();

    std::string preview;
    preview.reserve(std::min<size_t>(text.size(), 120));
    for (char c : text) {
        if (preview.size() >= 120) break;
        if (c == '\r' || c == '\n' || c == '\t') preview += ' ';
        else preview += c;
    }
    item.previewText = std::move(preview);

    items_.insert(items_.begin(), std::move(item));
    if (items_.size() > 50) {
        items_.pop_back();
    }
}

std::vector<ClipboardHistoryItem> ClipboardHistoryManager::items() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

std::vector<ClipboardHistoryItem> ClipboardHistoryManager::search(const std::string& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (query.empty()) return items_;

    std::string qLower = query;
    std::transform(qLower.begin(), qLower.end(), qLower.begin(), ::tolower);

    std::vector<ClipboardHistoryItem> res;
    for (const auto& it : items_) {
        std::string txtLower = it.text;
        std::transform(txtLower.begin(), txtLower.end(), txtLower.begin(), ::tolower);
        std::string typeLower = it.typeBadge;
        std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
        if (txtLower.find(qLower) != std::string::npos || typeLower.find(qLower) != std::string::npos) {
            res.push_back(it);
        }
    }
    return res;
}

bool ClipboardHistoryManager::copyItemToClipboard(uint32_t id) {
    std::string txt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& it : items_) {
            if (it.id == id) {
                txt = it.text;
                break;
            }
        }
    }
    if (txt.empty()) return false;
    return ClipboardManager::setClipboardUtf8(txt);
}

void ClipboardHistoryManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
}

bool ClipboardHistoryManager::deleteItem(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::remove_if(items_.begin(), items_.end(), [id](const ClipboardHistoryItem& item) {
        return item.id == id;
    });
    if (it != items_.end()) {
        items_.erase(it, items_.end());
        return true;
    }
    return false;
}

size_t ClipboardHistoryManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

// ---------------- ClipboardManager ----------------

ClipboardManager::ClipboardManager() {
    lastSeqNumber_ = GetClipboardSequenceNumber();
}

std::string ClipboardManager::getClipboardUtf8() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return {};
    }
    bool opened = false;
    for (int i = 0; i < 8; ++i) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        Sleep(5);
    }
    if (!opened) {
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

    bool opened = false;
    for (int i = 0; i < 8; ++i) {
        if (OpenClipboard(nullptr)) {
            opened = true;
            break;
        }
        Sleep(5);
    }
    if (!opened) {
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

bool ClipboardManager::getClipboardImageJpeg(std::vector<uint8_t>& outJpeg, uint32_t& outW, uint32_t& outH, int maxDim) {
    if (!IsClipboardFormatAvailable(CF_BITMAP)) {
        return false;
    }
    if (!OpenClipboard(nullptr)) {
        return false;
    }

    HBITMAP hBmp = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (!hBmp) {
        CloseClipboard();
        return false;
    }

    BITMAP bm{};
    if (!GetObjectW(hBmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        CloseClipboard();
        return false;
    }

    int srcW = bm.bmWidth;
    int srcH = bm.bmHeight;
    int dstW = srcW;
    int dstH = srcH;
    if (maxDim > 0 && (dstW > maxDim || dstH > maxDim)) {
        if (dstW >= dstH) {
            dstH = std::max(1, static_cast<int>(static_cast<double>(dstH) * maxDim / dstW));
            dstW = maxDim;
        } else {
            dstW = std::max(1, static_cast<int>(static_cast<double>(dstW) * maxDim / dstH));
            dstH = maxDim;
        }
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
    HDC hdcDst = CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOldSrc = SelectObject(hdcSrc, hBmp);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = dstW;
    bi.bmiHeader.biHeight = -dstH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmpDst = CreateDIBSection(hdcDst, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ hOldDst = nullptr;
    if (hBmpDst && pBits) {
        hOldDst = SelectObject(hdcDst, hBmpDst);
        SetStretchBltMode(hdcDst, HALFTONE);
        SetBrushOrgEx(hdcDst, 0, 0, nullptr);
        StretchBlt(hdcDst, 0, 0, dstW, dstH, hdcSrc, 0, 0, srcW, srcH, SRCCOPY);
    }

    std::vector<uint8_t> encoded;
    if (pBits) {
        encoded = TileCodec::encodeJpeg(static_cast<const uint8_t*>(pBits), dstW, dstH, 80);
    }

    if (hOldDst) SelectObject(hdcDst, hOldDst);
    if (hBmpDst) DeleteObject(hBmpDst);
    if (hOldSrc) SelectObject(hdcSrc, hOldSrc);
    DeleteDC(hdcDst);
    DeleteDC(hdcSrc);
    ReleaseDC(nullptr, hdcScreen);
    CloseClipboard();

    if (encoded.empty()) {
        return false;
    }
    outJpeg = std::move(encoded);
    outW = static_cast<uint32_t>(dstW);
    outH = static_cast<uint32_t>(dstH);
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
    history_.addItem(txt, false);
    outNewText = std::move(txt);
    return true;
}

void ClipboardManager::applyRemoteClipboard(const std::string& utf8Text) {
    if (utf8Text.empty()) return;
    lastAppliedHash_ = CryptoUtils::sha256Hex(utf8Text);
    setClipboardUtf8(utf8Text);
    history_.addItem(utf8Text, true);
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

uint32_t FileTransferManager::startOutgoingFile(const std::string& filePath, const SendPacketFn& sendPacket,
                                               FileOfferTarget targetHint, float dropNx, float dropNy) {
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
        w.writeU8(static_cast<uint8_t>(targetHint));
        w.writeF32(dropNx);
        w.writeF32(dropNy);
        sendPacket(PacketType::FILE_OFFER, w.buffer());
    }
    return tid;
}

int FileTransferManager::startOutgoingPath(const std::string& path, const SendPacketFn& sendPacket,
                                          FileOfferTarget targetHint, float dropNx, float dropNy) {
    std::filesystem::path p(path);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return 0;
    }

    if (std::filesystem::is_directory(p, ec)) {
        int queued = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(p, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec)) {
                if (startOutgoingFile(entry.path().string(), sendPacket, targetHint, dropNx, dropNy) > 0) {
                    ++queued;
                }
            }
        }
        return queued;
    }

    if (std::filesystem::is_regular_file(p, ec)) {
        return (startOutgoingFile(path, sendPacket, targetHint, dropNx, dropNy) > 0) ? 1 : 0;
    }

    return 0;
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

void FileTransferManager::handleFileOffer(uint32_t transferId, uint64_t totalBytes, const std::string& fileName,
                                         FileOfferTarget targetHint, float dropNx, float dropNy) {
    (void)dropNx;
    (void)dropNy;
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;

    std::string baseDir = receiveDir_;
    if (targetHint == FileOfferTarget::Desktop) {
        PWSTR pDesk = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &pDesk)) && pDesk) {
            std::filesystem::path deskPath(pDesk);
            CoTaskMemFree(pDesk);
            baseDir = deskPath.string();
        }
    }
    std::filesystem::create_directories(baseDir, ec);

    std::string cleanName = sanitizeFilename(fileName);
    std::filesystem::path targetPath = std::filesystem::path(baseDir) / cleanName;

    // Verify file size limit and available disk space
    bool spaceOk = (totalBytes <= MAX_TRANSFER_FILE_BYTES);
    if (spaceOk) {
        ULARGE_INTEGER freeBytesAvailable{};
        if (GetDiskFreeSpaceExA(baseDir.c_str(), &freeBytesAvailable, nullptr, nullptr)) {
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

    if (incomingStreams_.size() >= 10) {
        FileTransferItem item;
        item.transferId = transferId;
        item.fileName = cleanName;
        item.totalBytes = totalBytes;
        item.isOutgoing = false;
        item.status = TransferStatus::Failed;
        item.statusText = "Rejected (Queue Full)";
        items_.insert(items_.begin(), item);
        return;
    }

    // Avoid overwriting existing files by appending (1), (2), ...
    if (std::filesystem::exists(targetPath, ec)) {
        std::string stem = targetPath.stem().string();
        std::string ext = targetPath.extension().string();
        for (int n = 1; n < 1000; ++n) {
            auto candidate = std::filesystem::path(baseDir) / (stem + " (" + std::to_string(n) + ")" + ext);
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

    bool hashValid = !sha256Hex.empty() && (computedSha == sha256Hex);
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

bool FileTransferManager::hasActiveTransfers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& it : items_) {
        if (it.status == TransferStatus::InProgress) {
            return true;
        }
    }
    return !outgoingQueue_.empty() || !incomingStreams_.empty();
}

// ---------------- ClipboardFileTransferManager ----------------

ClipboardFileTransferManager::ClipboardFileTransferManager() {
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    stagingDir_ = std::wstring(tempPath) + L"CppDesk_Clipboard\\";
    std::error_code ec;
    std::filesystem::create_directories(stagingDir_, ec);
    lastSequenceNumber_ = GetClipboardSequenceNumber();
}

ClipboardFileTransferManager::~ClipboardFileTransferManager() {
    reset();
    purgeStaging();
}

void ClipboardFileTransferManager::purgeStaging() {
    std::error_code ec;
    if (std::filesystem::exists(stagingDir_, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(stagingDir_, ec)) {
            if (entry.path().extension() == ".part") {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}

bool ClipboardFileTransferManager::pollLocalClipboardFiles(const SendPacketFn& sendPacket) {
    uint32_t seq = GetClipboardSequenceNumber();
    if (seq == lastSequenceNumber_) return false;
    lastSequenceNumber_ = seq;

    std::vector<VirtualFileEntry> files;
    std::vector<std::wstring> fullPaths;
    if (!ShellClipboard::getLocalClipboardFiles(files, fullPaths)) {
        return false;
    }

    uint64_t totalBytes = 0;
    std::string manifest;
    for (const auto& f : files) {
        totalBytes += f.fileSize;
        manifest += std::to_string(f.fileSize) + ":" + std::string(f.fileName.begin(), f.fileName.end()) + ";";
    }

    constexpr uint64_t MAX_CAP = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2 GB
    if (totalBytes > MAX_CAP) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (manifest == outgoing_.manifestHash) {
        return false;
    }

    outgoing_.transferId = ++nextTransferId_;
    outgoing_.files = files;
    outgoing_.fullPaths = fullPaths;
    outgoing_.totalBytes = totalBytes;
    outgoing_.manifestHash = manifest;

    if (sendPacket) {
        std::vector<uint8_t> pkt;
        serializeClipboardFileList(outgoing_.transferId, outgoing_.files, pkt);
        sendPacket(PacketType::CLIPBOARD_FILE_LIST, pkt);
    }
    return true;
}

void ClipboardFileTransferManager::handleRemoteFileList(uint32_t transferId, const std::vector<VirtualFileEntry>& files, const SendPacketFn& sendPacket) {
    if (files.empty()) return;

    uint64_t totalBytes = 0;
    for (const auto& f : files) totalBytes += f.fileSize;

    constexpr uint64_t MAX_CAP = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    if (totalBytes > MAX_CAP) {
        if (sendPacket) {
            ClipboardFileCancelHeader cancelHdr{ transferId, 2 }; // 2 = size limit exceeded
            std::vector<uint8_t> pkt(sizeof(cancelHdr));
            std::memcpy(pkt.data(), &cancelHdr, sizeof(cancelHdr));
            sendPacket(PacketType::CLIPBOARD_FILE_CANCEL, pkt);
        }
        return;
    }

    std::vector<VirtualFileEntry> sanitizedFiles = files;
    for (auto& f : sanitizedFiles) {
        std::wstring clean = std::filesystem::path(f.fileName).filename().wstring();
        if (clean.empty() || clean == L"." || clean == L"..") {
            clean = L"received_file_" + std::to_wstring(f.fileIndex) + L".bin";
        }
        for (wchar_t& ch : clean) {
            if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' ||
                ch == L'/' || ch == L'\\' || ch == L'|' || ch == L'?' || ch == L'*' ||
                static_cast<uint16_t>(ch) < 32) {
                ch = L'_';
            }
        }
        size_t pos = 0;
        while ((pos = clean.find(L"..", pos)) != std::wstring::npos) {
            clean.replace(pos, 2, L"__");
            pos += 2;
        }
        if (clean.empty()) {
            clean = L"received_file_" + std::to_wstring(f.fileIndex) + L".bin";
        }
        f.fileName = clean;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_.transferId = transferId;
        incoming_.files = sanitizedFiles;
        incoming_.totalBytes = totalBytes;
        incoming_.transferredBytes = 0;
        incoming_.currentFileIndex = 0;
        incoming_.currentFileName = std::string(sanitizedFiles[0].fileName.begin(), sanitizedFiles[0].fileName.end());
        incoming_.inProgress = false;
        incoming_.cancelled = false;
        incoming_.startTimeMs = 0;
        incoming_.transferRateMBs = 0.0f;
        activeSendFn_ = sendPacket;
    }

    ShellClipboard::setVirtualClipboardFiles(sanitizedFiles, [this](uint32_t fIndex, uint64_t off, uint32_t len, std::vector<uint8_t>& out) {
        return this->fetchChunk(fIndex, off, len, out);
    });
}

bool ClipboardFileTransferManager::fetchChunk(uint32_t fileIndex, uint64_t offset, uint32_t length, std::vector<uint8_t>& outData) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (incoming_.cancelled) return false;

    incoming_.inProgress = true;
    incoming_.currentFileIndex = fileIndex;
    if (fileIndex < incoming_.files.size()) {
        const auto& f = incoming_.files[fileIndex];
        incoming_.currentFileName = std::string(f.fileName.begin(), f.fileName.end());
    }
    if (incoming_.startTimeMs == 0) {
        incoming_.startTimeMs = GetTickCount64();
    }

    pendingReq_.transferId = incoming_.transferId;
    pendingReq_.fileIndex = fileIndex;
    pendingReq_.offset = offset;
    pendingReq_.length = length;
    pendingReq_.completed = false;
    pendingReq_.failed = false;
    pendingReq_.data.clear();

    SendPacketFn sendFn = activeSendFn_;
    uint32_t tId = incoming_.transferId;
    lock.unlock();

    if (sendFn) {
        ClipboardFileRequestHeader reqHdr{};
        reqHdr.transferId = tId;
        reqHdr.fileIndex = fileIndex;
        reqHdr.offset = offset;
        reqHdr.length = length;
        std::vector<uint8_t> pkt(sizeof(reqHdr));
        std::memcpy(pkt.data(), &reqHdr, sizeof(reqHdr));
        sendFn(PacketType::CLIPBOARD_FILE_REQUEST, pkt);
    } else {
        return false;
    }

    lock.lock();
    bool ok = chunkCv_.wait_for(lock, std::chrono::seconds(15), [this]() {
        return pendingReq_.completed || pendingReq_.failed || incoming_.cancelled;
    });

    if (!ok || pendingReq_.failed || incoming_.cancelled) {
        return false;
    }

    outData = std::move(pendingReq_.data);
    incoming_.transferredBytes += outData.size();

    uint64_t nowMs = GetTickCount64();
    uint64_t elapsedMs = nowMs - incoming_.startTimeMs;
    if (elapsedMs > 200) {
        double sec = static_cast<double>(elapsedMs) / 1000.0;
        double mb = static_cast<double>(incoming_.transferredBytes) / (1024.0 * 1024.0);
        incoming_.transferRateMBs = static_cast<float>(mb / sec);
    }

    if (incoming_.transferredBytes >= incoming_.totalBytes) {
        incoming_.inProgress = false;
    }
    return true;
}

void ClipboardFileTransferManager::handleFileRequest(uint32_t transferId, uint32_t fileIndex, uint64_t offset, uint32_t length, const SendPacketFn& sendPacket) {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (transferId != outgoing_.transferId || fileIndex >= outgoing_.fullPaths.size()) {
            return;
        }
        path = outgoing_.fullPaths[fileIndex];
    }

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) {
        if (sendPacket) {
            ClipboardFileCancelHeader cancelHdr{ transferId, 3 }; // 3 = IO error
            std::vector<uint8_t> pkt(sizeof(cancelHdr));
            std::memcpy(pkt.data(), &cancelHdr, sizeof(cancelHdr));
            sendPacket(PacketType::CLIPBOARD_FILE_CANCEL, pkt);
        }
        return;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    uint32_t readSize = std::min<uint32_t>(length, 64 * 1024);
    std::vector<uint8_t> buf(readSize);
    file.read(reinterpret_cast<char*>(buf.data()), readSize);
    std::streamsize bytesRead = file.gcount();
    if (bytesRead < 0) bytesRead = 0;
    buf.resize(static_cast<size_t>(bytesRead));

    auto digest = CryptoUtils::sha256(buf.data(), buf.size());

    ClipboardFileChunkHeader chunkHdr{};
    chunkHdr.transferId = transferId;
    chunkHdr.fileIndex = fileIndex;
    chunkHdr.offset = offset;
    chunkHdr.dataLength = static_cast<uint32_t>(buf.size());
    std::memcpy(chunkHdr.sha256, digest.data(), 32);

    std::vector<uint8_t> pkt(sizeof(chunkHdr) + buf.size());
    std::memcpy(pkt.data(), &chunkHdr, sizeof(chunkHdr));
    if (!buf.empty()) {
        std::memcpy(pkt.data() + sizeof(chunkHdr), buf.data(), buf.size());
    }
    if (sendPacket) {
        sendPacket(PacketType::CLIPBOARD_FILE_CHUNK, pkt);
    }
}

void ClipboardFileTransferManager::handleFileChunk(uint32_t transferId, uint32_t fileIndex, uint64_t offset, const uint8_t* data, size_t len, const uint8_t sha256[32]) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transferId != incoming_.transferId || incoming_.cancelled) {
        return;
    }

    auto digest = CryptoUtils::sha256(data, len);
    if (std::memcmp(digest.data(), sha256, 32) != 0) {
        if (pendingReq_.transferId == transferId && pendingReq_.fileIndex == fileIndex && pendingReq_.offset == offset) {
            pendingReq_.failed = true;
            chunkCv_.notify_all();
        }
        return;
    }

    if (fileIndex < incoming_.files.size()) {
        std::wstring partName = stagingDir_ + L"transfer_" + std::to_wstring(transferId) + L"_part_" + std::to_wstring(fileIndex) + L".part";
        std::ofstream partFile(std::filesystem::path(partName), std::ios::binary | std::ios::in | std::ios::out);
        if (!partFile.is_open()) {
            partFile.open(std::filesystem::path(partName), std::ios::binary | std::ios::out);
        }
        if (partFile.is_open()) {
            partFile.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
            partFile.write(reinterpret_cast<const char*>(data), len);
        }
    }

    if (pendingReq_.transferId == transferId && pendingReq_.fileIndex == fileIndex && pendingReq_.offset == offset) {
        pendingReq_.data.assign(data, data + len);
        pendingReq_.completed = true;
        chunkCv_.notify_all();
    }
}

void ClipboardFileTransferManager::handleFileCancel(uint32_t transferId, uint32_t /*reasonCode*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.transferId == transferId) {
        incoming_.cancelled = true;
        incoming_.inProgress = false;
        pendingReq_.failed = true;
        chunkCv_.notify_all();
        purgeStaging();
        ShellClipboard::clearClipboard();
    }
}

void ClipboardFileTransferManager::cancelActiveTransfer(const SendPacketFn& sendPacket) {
    uint32_t tId = 0;
    SendPacketFn sendFn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_.cancelled = true;
        incoming_.inProgress = false;
        pendingReq_.failed = true;
        tId = incoming_.transferId;
        sendFn = sendPacket ? sendPacket : activeSendFn_;
        chunkCv_.notify_all();
        purgeStaging();
        ShellClipboard::clearClipboard();
    }

    if (sendFn && tId > 0) {
        ClipboardFileCancelHeader cancelHdr{ tId, 1 }; // 1 = user cancelled
        std::vector<uint8_t> pkt(sizeof(cancelHdr));
        std::memcpy(pkt.data(), &cancelHdr, sizeof(cancelHdr));
        sendFn(PacketType::CLIPBOARD_FILE_CANCEL, pkt);
    }
}

void ClipboardFileTransferManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    incoming_.cancelled = true;
    incoming_.inProgress = false;
    pendingReq_.failed = true;
    chunkCv_.notify_all();
    purgeStaging();
    incoming_ = {};
    outgoing_ = {};
    activeSendFn_ = nullptr;
}

bool ClipboardFileTransferManager::isTransferActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.inProgress && !incoming_.cancelled;
}

std::string ClipboardFileTransferManager::activeFileName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.currentFileName;
}

float ClipboardFileTransferManager::activeProgressFraction() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.totalBytes == 0) return 0.0f;
    return static_cast<float>(static_cast<double>(incoming_.transferredBytes) / static_cast<double>(incoming_.totalBytes));
}

float ClipboardFileTransferManager::activeTransferRateMBs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.transferRateMBs;
}

uint64_t ClipboardFileTransferManager::activeTransferredBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.transferredBytes;
}

uint64_t ClipboardFileTransferManager::activeTotalBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.totalBytes;
}

uint32_t ClipboardFileTransferManager::activeTransferId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return incoming_.transferId;
}

} // namespace cppdesk
