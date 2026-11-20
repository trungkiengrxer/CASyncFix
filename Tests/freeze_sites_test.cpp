// Host test: the freeze patch sites match this machine's shared cache (25G72) and every pattern is unique.
//
// Run from the repository root:
//   clang++ -std=c++17 -O2 Tests/freeze_sites_test.cpp -o /tmp/fz_sites && /tmp/fz_sites

#include "../CASyncFix/ahead_patch.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static const char *kCache = "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h";

static bool readPage(uint64_t staticAddr, std::vector<uint8_t> &page) {
    FILE *f = fopen(kCache, "rb");
    if (!f) return false;
    page.assign(4096, 0);
    fseeko(f, (off_t)((staticAddr - fzCacheBase) & ~0xfffULL), SEEK_SET);
    size_t got = fread(page.data(), 1, page.size(), f);
    fclose(f);
    return got == page.size();
}

// Counts occurrences of a pattern in one file, scanning 64 MiB chunks with overlap.
static size_t countInFile(const char *path, const uint8_t *pat, size_t len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t chunk = 64u << 20;
    std::vector<uint8_t> buf(chunk + len);
    size_t keep = 0, count = 0;
    for (;;) {
        size_t got = fread(buf.data() + keep, 1, chunk, f);
        size_t n = keep + got;
        if (n < len) break;
        for (const uint8_t *p = buf.data(), *end = buf.data() + n - len + 1; p < end;) {
            const void *hit = memmem(p, (size_t)(end - p) + len - 1, pat, len);
            if (!hit) break;
            count++;
            p = (const uint8_t *)hit + 1;
        }
        if (got == 0) break;
        keep = len - 1;
        memmove(buf.data(), buf.data() + n - keep, keep);
    }
    fclose(f);
    return count;
}

int main() {
    const FzSite *sites[] = {&fzCallA, &fzCallB, &fzApply, &fzCave, &ahPresent, &ahNow, &fzFlat};
    const char *names[] = {"call A", "call B", "apply", "cave", "present_update ahead", "present_update now", "flat"};
    const int nSites = 7;

    // sites_match_real_cache
    for (int i = 0; i < nSites; i++) {
        std::vector<uint8_t> page;
        bool ok = readPage(sites[i]->staticAddr, page);
        bool stock = ok && memcmp(page.data() + sites[i]->pageOff, sites[i]->find, sites[i]->len) == 0;
        char what[96];
        snprintf(what, sizeof what, "sites_match_real_cache: %s at page offset 0x%x", names[i], sites[i]->pageOff);
        if (!stock && ok) printf("     (bytes differ: may be already patched by an installed CASyncFix 0.4.0)\n");
        check(stock, what);
        if (sites[i] == &fzCave && ok) {
            bool zeros = true;
            for (size_t k = fzCaveStart - (fzCave.staticAddr & ~0xfffULL); k < 4096; k++) zeros &= page[k] == 0;
            check(zeros, "sites_match_real_cache: cave tail is all zero up to the page end");
        }
    }
    check(fzCaveStart + fzCaveSize == ((fzCave.staticAddr & ~0xfffULL) + 4096), "cave spans to the page end");
    check(fzApply.staticAddr == 0x7ff809333a84ULL && fzApplyResume == fzApply.staticAddr + 8,
          "apply site is the 8-byte cmpl at ca_window_prep_ingestion_phase+11144");
    check(ahPresent.staticAddr == 0x7ff80950f854ULL && ahPresentLen == 12 && ahPresent.len <= ahNow.staticAddr - ahPresent.staticAddr,
          "ahead site is cvtss2sd+subsd (12 bytes) at CDDisplay::present_update+374, pattern ends before +388");
    check(ahNow.staticAddr == 0x7ff80950f862ULL && ahNow.find[0] == 0xE8 &&
          (uint64_t)(ahNow.staticAddr + 5 + (int32_t)(ahNow.find[1] | ahNow.find[2] << 8 | ahNow.find[3] << 16 |
                                                      (uint32_t)ahNow.find[4] << 24)) == 0x7ff809292003ULL,
          "now site is callq SLSCurrentRealTime at CDDisplay::present_update+388");
    check(ahNextVBL == 0x7ff81341b75fULL - 0x9f7d000ULL, "WSDisplayScheduleGetNextVBL static address (dump - 0x9f7d000)");
    // flat: `cmpq $-1, UseCAWindowLayerFlattening()::once(%rip)` (8 bytes) at ca_window_prep_ingestion_phase+9546,
    // the point every flatten decision ([rec+0x700]) jumps to, followed by `jne +19000` (6 bytes).
    check(fzFlat.staticAddr == 0x7ff8132b0446ULL - 0x9f7d000ULL && fzFlat.find[0] == 0x48 && fzFlat.find[1] == 0x83 &&
          fzFlat.find[2] == 0x3D && fzFlat.find[7] == 0xFF && fzFlat.find[8] == 0x0F && fzFlat.find[9] == 0x85 &&
          (uint64_t)(fzFlat.staticAddr + 8 + (int32_t)(fzFlat.find[3] | fzFlat.find[4] << 8 | fzFlat.find[5] << 16 |
                                                       (uint32_t)fzFlat.find[6] << 24)) == fzFlatOnce,
          "flat site is the 8-byte cmpq at +9546 and reads UseCAWindowLayerFlattening()::once");
    check(fzFlatResume == fzFlat.staticAddr + 8 && fzFlatLen == 8, "flat thunk resumes at the jne right after the cmpq");
    check(fzWindowMeshOff == 0x878 && fzWindowLevelOff == 0x20 && fzWindowMoveOff == 0x498 + 12 * 8,
          "CGXWindow offsets: mesh warp (set_mesh_warp), level (WSWindowGetLevel), transform m41 (_XGetWindowTransform)");
    check(fzDraggedWindowOff == 0x10c, "dragged window id offset in the session package data (_forciblyEndWindowDrag)");
    check(fzChangeDetected == 0x7ff8131f2174ULL - 0x9f7d000ULL,
          "FlatteningPolicyData::change_detected static address (dump - 0x9f7d000)");
    check(fzSessionControlRef == 0x7ff8132afd52ULL + 0x3a8ce686ULL - 0x9f7d000ULL,
          "__sessionControlRef static address (rip-relative load at +7759)");

    // patterns_unique_in_all_cache_files
    for (int i = 0; i < nSites; i++) {
        size_t total = 0;
        char path[256];
        total += countInFile(kCache, sites[i]->find, sites[i]->len);
        for (int k = 1; k <= 6; k++) {
            snprintf(path, sizeof path, "%s.%02d", kCache, k);
            total += countInFile(path, sites[i]->find, sites[i]->len);
        }
        char what[96];
        snprintf(what, sizeof what, "patterns_unique_in_all_cache_files: %s (%zu hit)", names[i], total);
        check(total == 1, what);
    }

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
