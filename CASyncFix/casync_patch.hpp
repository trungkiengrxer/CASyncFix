//
//  casync_patch.hpp
//  CASyncFix.kext
//
// Pure patch logic, shared by the kernel plugin and the host test.
//
// 1. finish_every. QuartzCore's CA::OGL::new_metal_context sets MetalContext::finish_every (+0xe38) to 128 when
//    the GPU does not support MTLGPUFamilyApple3. MetalContext::set_surface then calls finish()
//    (waitUntilCompleted) every 128 render-target switches. Writing 0 disables that periodic wait.
//
//   41 F6 87 35 0E 00 00 04   test byte [r15+0xe35], 4      ; supportsFamily(Apple3)
//   75 0B                     jne  skip
//   41 C7 87 38 0E 00 00 80 00 00 00   mov dword [r15+0xe38], 0x80
//
// 2. flush_every. CA::OGL::Context::Context sets flush_every (+0x3f8) to 64, and set_surface commits the command
//    buffer every 64 render-target switches. On Intel each commit goes through the host GPU scheduler in the
//    kernel; in Mission Control these mid-frame commits were ~7% of WindowServer's main thread. A larger value
//    (0 = only the end-of-frame commit) trades that CPU for a later GPU start inside the frame.
//
//   89 87 88 03 00 00         mov [rdi+0x388], eax
//   C7 87 F8 03 00 00 40 00 00 00   mov dword [rdi+0x3f8], 64
//   C7 87 DC 05 00 00         mov dword [rdi+0x5dc], ...
//

#ifndef casync_patch_hpp
#define casync_patch_hpp

#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t caSyncFind[] = {0x41, 0xF6, 0x87, 0x35, 0x0E, 0x00, 0x00, 0x04, 0x75, 0x0B, 0x41,
                                         0xC7, 0x87, 0x38, 0x0E, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};
static constexpr size_t caSyncImmOffset = 17;  // the 4-byte immediate inside caSyncFind

static constexpr uint8_t flushFind[] = {0x89, 0x87, 0x88, 0x03, 0x00, 0x00, 0xC7, 0x87, 0xF8, 0x03, 0x00, 0x00,
                                        0x40, 0x00, 0x00, 0x00, 0xC7, 0x87, 0xDC, 0x05, 0x00, 0x00};
static constexpr size_t flushImmOffset = 12;   // the 4-byte immediate (64) inside flushFind
static constexpr uint32_t flushStock = 64;

static inline bool caMatchAt(const uint8_t *d, const uint8_t *find, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (d[i] != find[i]) return false;
    return true;
}

static inline bool caFind(const uint8_t *d, size_t n, const uint8_t *find, size_t len) {
    if (d == nullptr || n < len) return false;
    for (size_t i = 0; i + len <= n; i++)
        if (d[i] == find[0] && caMatchAt(d + i, find, len)) return true;
    return false;
}

// Writes value as the little-endian 32-bit immediate at off inside every occurrence of find.
static inline bool caPatchImm32(uint8_t *d, size_t n, const uint8_t *find, size_t len, size_t off, uint32_t value) {
    if (d == nullptr || n < len) return false;
    bool patched = false;
    for (size_t i = 0; i + len <= n; i++) {
        if (d[i] != find[0] || !caMatchAt(d + i, find, len)) continue;
        for (size_t k = 0; k < 4; k++) d[i + off + k] = static_cast<uint8_t>(value >> (8 * k));
        patched = true;
        i += len - 1;
    }
    return patched;
}

// True if the buffer holds the unpatched instruction sequence (cheap pre-check for the hot path).
static inline bool caSyncFindPattern(const uint8_t *d, size_t n) { return caFind(d, n, caSyncFind, sizeof(caSyncFind)); }

// Zeroes the finish_every immediate of every occurrence; returns true if anything changed.
static inline bool caSyncPatch(uint8_t *d, size_t n) {
    return caPatchImm32(d, n, caSyncFind, sizeof(caSyncFind), caSyncImmOffset, 0);
}

static inline bool flushFindPattern(const uint8_t *d, size_t n) { return caFind(d, n, flushFind, sizeof(flushFind)); }

// Sets flush_every to value; the stock value changes nothing.
static inline bool flushPatch(uint8_t *d, size_t n, uint32_t value) {
    if (value == flushStock) return false;
    return caPatchImm32(d, n, flushFind, sizeof(flushFind), flushImmOffset, value);
}

// Do NOT patch the raster-order-group gate (new_metal_context: test dl,1 ; je skip ; F6 C2 01 74 28 ...).
// Tried in 0.2.0: window corners showed squares of stale VRAM. The Intel driver drops fragment-shader texture
// writes into large textures that were render targets before (render compression), which CA's in-place copy
// relies on; no barrier or usage flag fixes it (tools/barriertest.swift, tools/fragwritetest.swift).

#endif /* casync_patch_hpp */
