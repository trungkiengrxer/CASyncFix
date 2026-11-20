//
//  freeze_patch.hpp
//  CASyncFix.kext
//
// Page patches of the window-freeze feature, shared by the kernel plugin and the host test. Each function patches a
// page only when it is the expected page of the main cache file (fileOff) and its 16-byte pattern sits exactly at the
// expected page offset (rel32 values depend on the exact static address of the site); anything else is untouched.
// The file-offset check ties the call/apply patches to the one cache layout whose cave was verified to be free, so the
// redirected branches can never point at a cave that was not filled. All values are slide-invariant.
//

#ifndef freeze_patch_hpp
#define freeze_patch_hpp

#include <stddef.h>
#include <stdint.h>

#include "freeze_sites.hpp"
#include "freeze_blob.inc"
#include "flat_policy.h"

static constexpr size_t fzCaveOff = fzCaveStart & 0xfff;   // 0x4d0
static_assert(fzBlobLen <= fzCaveSize, "freeze blob does not fit the SkyLight cave");
static_assert(fzOffAnchor == 0, "fz_anchor must be byte 0 of the blob");

static inline uint64_t fzFileOff(const FzSite &s) { return (s.staticAddr - fzCacheBase) & ~0xfffULL; }

static inline bool fzAt(const uint8_t *page, size_t n, const FzSite &s) {
    if (page == nullptr || n < s.pageOff + s.len) return false;
    for (size_t i = 0; i < s.len; i++)
        if (page[s.pageOff + i] != s.find[i]) return false;
    return true;
}

static inline void fzPut32(uint8_t *p, int64_t v) {
    for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * i));
}

static inline void fzPut64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// Redirects `callq ca_prepare_begin_window_update` at call sites A and B to the blob's timing wrapper.
static inline bool fzPatchCalls(uint8_t *page, size_t n, uint64_t fileOff) {
    bool patched = false;
    const FzSite *sites[] = {&fzCallA, &fzCallB};
    for (const FzSite *s : sites) {
        if (fileOff != fzFileOff(*s) || !fzAt(page, n, *s)) continue;
        fzPut32(page + s->pageOff + 1, (int64_t)(fzCaveStart + fzOffWrapper) - (int64_t)(s->staticAddr + 5));
        patched = true;
    }
    return patched;
}

// Replaces `cmpl $0x0, 0xc8(%r14)` (8 bytes) with `jmp fz_apply_thunk` + 3 nops.
static inline bool fzPatchApply(uint8_t *page, size_t n, uint64_t fileOff) {
    if (fileOff != fzFileOff(fzApply) || !fzAt(page, n, fzApply)) return false;
    uint8_t *p = page + fzApply.pageOff;
    p[0] = 0xE9;
    fzPut32(p + 1, (int64_t)(fzCaveStart + fzOffThunk) - (int64_t)(fzApply.staticAddr + 5));
    for (size_t i = 5; i < fzApplyLen; i++) p[i] = 0x90;
    return true;
}

// Replaces the 8-byte `cmpq $-1, once(%rip)` at the flatten-decision convergence point with `jmp fz_flat_thunk` + 3 nops.
static inline bool fzPatchFlat(uint8_t *page, size_t n, uint64_t fileOff) {
    if (fileOff != fzFileOff(fzFlat) || !fzAt(page, n, fzFlat)) return false;
    uint8_t *p = page + fzFlat.pageOff;
    p[0] = 0xE9;
    fzPut32(p + 1, (int64_t)(fzCaveStart + fzOffFlatThunk) - (int64_t)(fzFlat.staticAddr + 5));
    for (size_t i = 5; i < fzFlatLen; i++) p[i] = 0x90;
    return true;
}

// Copies the blob into the zero tail of SkyLight's last __TEXT page, then writes the config (freeze mode and budget,
// forced-flatten refresh rates, see flat_policy.h) and the thunks' exit jmps.
static inline bool fzPatchCave(uint8_t *page, size_t n, uint64_t fileOff, uint32_t mode, uint64_t budgetNs,
                               uint32_t animHz = FP_REFRESH_HZ_DEFAULT, uint32_t resizeHz = FP_RESIZE_HZ_DEFAULT) {
    if (fileOff != fzFileOff(fzCave) || !fzAt(page, n, fzCave) || n < 0x1000) return false;
    for (size_t i = fzCaveOff; i < 0x1000; i++)
        if (page[i] != 0) return false;   // not the pristine cave (already patched, or a different page)
    uint8_t *c = page + fzCaveOff;
    for (size_t i = 0; i < fzBlobLen; i++) c[i] = fzBlob[i];
    fzPut32(c + fzOffConfig + 4, mode);
    fzPut64(c + fzOffConfig + 8, budgetNs);
    fzPut32(c + fzOffConfig + 16, animHz);
    fzPut32(c + fzOffConfig + 20, resizeHz);
    fzPut32(c + fzOffResumeJmp + 1, (int64_t)fzApplyResume - (int64_t)(fzCaveStart + fzOffResumeJmp + 5));
    fzPut32(c + fzOffFlatResumeJmp + 1, (int64_t)fzFlatResume - (int64_t)(fzCaveStart + fzOffFlatResumeJmp + 5));
    return true;
}

#endif /* freeze_patch_hpp */
