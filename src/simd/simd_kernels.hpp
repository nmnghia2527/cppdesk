#pragma once

#include <cstdint>
#include <cstddef>

#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

namespace cppdesk {

extern "C" {

int cppdesk_avx2_tile_diff(
    const uint8_t* prevTilePtr,
    const uint8_t* currTilePtr,
    int strideBytes,
    int widthBytes,
    int heightRows);

uint64_t cppdesk_avx2_hash_tile(
    const uint8_t* tilePtr,
    int strideBytes,
    int widthBytes,
    int heightRows);

void cppdesk_avx2_xor_blocks32(
    uint8_t* data,
    const uint8_t* ksBuffer,
    size_t blocks32);

// Backward-compatible aliases
int aerodesk_avx2_tile_diff(
    const uint8_t* prevTilePtr,
    const uint8_t* currTilePtr,
    int strideBytes,
    int widthBytes,
    int heightRows);

uint64_t aerodesk_avx2_hash_tile(
    const uint8_t* tilePtr,
    int strideBytes,
    int widthBytes,
    int heightRows);

void aerodesk_avx2_xor_blocks32(
    uint8_t* data,
    const uint8_t* ksBuffer,
    size_t blocks32);

} // extern "C"

class SimdKernels {
public:
    static bool hasAvx2() {
        static const bool detected = detectAvx2Cpu();
        return detected;
    }

    static bool scalarTileDiff(
        const uint8_t* prevTilePtr,
        const uint8_t* currTilePtr,
        int strideBytes,
        int widthBytes,
        int heightRows)
    {
        for (int r = 0; r < heightRows; ++r) {
            const uint8_t* p = prevTilePtr + static_cast<size_t>(r) * strideBytes;
            const uint8_t* c = currTilePtr + static_cast<size_t>(r) * strideBytes;
            for (int i = 0; i < widthBytes; ++i) {
                if (p[i] != c[i]) return true;
            }
        }
        return false;
    }

private:
    static bool detectAvx2Cpu() {
#if defined(__x86_64__) || defined(_M_X64)
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            // Check OSXSAVE (bit 27) and AVX (bit 28)
            if ((ecx & (1U << 27)) && (ecx & (1U << 28))) {
                unsigned int xcr0Eax = 0, xcr0Edx = 0;
                __asm__ volatile("xgetbv" : "=a"(xcr0Eax), "=d"(xcr0Edx) : "c"(0));
                // Check XMM and YMM state enabled by OS (bits 1 and 2)
                if ((xcr0Eax & 0x6) == 0x6) {
                    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
                        return (ebx & (1U << 5)) != 0; // AVX2 bit 5
                    }
                }
            }
        }
#endif
        return false;
    }
};

} // namespace cppdesk
