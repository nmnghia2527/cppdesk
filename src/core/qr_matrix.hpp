#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppdesk {

class QrMatrix {
public:
    QrMatrix() = default;
    explicit QrMatrix(int size);

    int size() const { return size_; }
    bool getModule(int x, int y) const;
    void setModule(int x, int y, bool isDark);

    const std::vector<uint8_t>& modules() const { return modules_; }

    // Generates a standard QR code matrix (Versions 1-6, Level L)
    static QrMatrix generate(const std::string& text, int minVersion = 1, int maxVersion = 6);

private:
    int                  size_ = 0;
    std::vector<uint8_t> modules_; // size * size elements, 1 = dark, 0 = light
};

} // namespace cppdesk
