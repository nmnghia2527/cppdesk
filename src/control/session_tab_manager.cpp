#include "session_tab_manager.hpp"
#include "../core/crypto_identity.hpp"

namespace cppdesk {

SessionTabManager::SessionTabManager() = default;
SessionTabManager::~SessionTabManager() = default;

uint32_t SessionTabManager::createTab(uint64_t deskId, const std::string& targetInput, const std::string& title) {
    SessionTab tab;
    tab.id = nextTabId_++;
    tab.deskId = deskId;
    tab.targetInput = targetInput;
    if (!title.empty()) {
        tab.title = title;
    } else if (deskId != 0) {
        tab.title = CryptoUtils::formatDeskId(deskId);
    } else {
        tab.title = targetInput.empty() ? ("Session " + std::to_string(tab.id)) : targetInput;
    }

    tabs_.push_back(std::move(tab));
    activeTabId_ = tabs_.back().id;
    return activeTabId_;
}

bool SessionTabManager::closeTab(uint32_t tabId) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [tabId](const SessionTab& t) {
        return t.id == tabId;
    });
    if (it == tabs_.end()) {
        return false;
    }

    size_t idx = std::distance(tabs_.begin(), it);
    bool wasActive = (activeTabId_ == tabId);

    tabs_.erase(it);

    if (tabs_.empty()) {
        activeTabId_ = 0;
    } else if (wasActive) {
        if (idx < tabs_.size()) {
            activeTabId_ = tabs_[idx].id;
        } else {
            activeTabId_ = tabs_.back().id;
        }
    }

    return true;
}

void SessionTabManager::closeAllTabs() {
    tabs_.clear();
    activeTabId_ = 0;
}

bool SessionTabManager::selectTab(uint32_t tabId) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [tabId](const SessionTab& t) {
        return t.id == tabId;
    });
    if (it != tabs_.end()) {
        activeTabId_ = tabId;
        return true;
    }
    return false;
}

uint32_t SessionTabManager::nextTab() {
    if (tabs_.empty()) return 0;
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [this](const SessionTab& t) {
        return t.id == activeTabId_;
    });
    size_t curIdx = (it != tabs_.end()) ? std::distance(tabs_.begin(), it) : 0;
    size_t nextIdx = (curIdx + 1) % tabs_.size();
    activeTabId_ = tabs_[nextIdx].id;
    return activeTabId_;
}

uint32_t SessionTabManager::prevTab() {
    if (tabs_.empty()) return 0;
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [this](const SessionTab& t) {
        return t.id == activeTabId_;
    });
    size_t curIdx = (it != tabs_.end()) ? std::distance(tabs_.begin(), it) : 0;
    size_t prevIdx = (curIdx + tabs_.size() - 1) % tabs_.size();
    activeTabId_ = tabs_[prevIdx].id;
    return activeTabId_;
}

SessionTab* SessionTabManager::activeTab() {
    return getTab(activeTabId_);
}

const SessionTab* SessionTabManager::activeTab() const {
    return getTab(activeTabId_);
}

SessionTab* SessionTabManager::getTab(uint32_t tabId) {
    for (auto& t : tabs_) {
        if (t.id == tabId) return &t;
    }
    return nullptr;
}

const SessionTab* SessionTabManager::getTab(uint32_t tabId) const {
    for (const auto& t : tabs_) {
        if (t.id == tabId) return &t;
    }
    return nullptr;
}

SessionTab* SessionTabManager::findTabByDeskId(uint64_t deskId) {
    if (deskId == 0) return nullptr;
    for (auto& t : tabs_) {
        if (t.deskId == deskId) return &t;
    }
    return nullptr;
}

SessionTab* SessionTabManager::findTabByTarget(const std::string& target) {
    if (target.empty()) return nullptr;
    for (auto& t : tabs_) {
        if (t.targetInput == target) return &t;
    }
    return nullptr;
}

void SessionTabManager::cacheActiveTabFrame(const uint8_t* bgra, int width, int height, uint64_t frameSeq, const CursorState& cursor) {
    cacheTabFrame(activeTabId_, bgra, width, height, frameSeq, cursor);
}

void SessionTabManager::cacheTabFrame(uint32_t tabId, const uint8_t* bgra, int width, int height, uint64_t frameSeq, const CursorState& cursor) {
    SessionTab* tab = getTab(tabId);
    if (!tab) return;

    if (bgra && width > 0 && height > 0) {
        size_t bytes = static_cast<size_t>(width) * height * 4;
        tab->cachedFrameBgra.assign(bgra, bgra + bytes);
        tab->cachedW = width;
        tab->cachedH = height;
        tab->lastFrameSeq = frameSeq;
        tab->cursor = cursor;
    }
}

} // namespace cppdesk
