// Host test for the present-ahead call-site patches (CASyncFix/ahead_patch.hpp). The present time itself is computed
// by fz_present_target in the freeze blob (Tests/probe_test.c, Tests/blob_check.sh).
//
// Run from the repository root (after CASyncFix/freeze_blob/build_blob.sh):
//   clang++ -std=c++17 Tests/ahead_patch_test.cpp -o /tmp/ah_patch && /tmp/ah_patch

#include "../CASyncFix/ahead_patch.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static const size_t PAGE = 4096;
static const char *kCache = "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h";

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint64_t pageOf(uint64_t staticAddr) { return (staticAddr - fzCacheBase) & ~0xfffULL; }
static size_t diffs(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i];
    return n;
}
static std::vector<uint8_t> sitePage() {
    std::vector<uint8_t> p(PAGE, 0xCC);
    memcpy(&p[ahPresent.pageOff], ahPresent.find, ahPresent.len);
    memcpy(&p[ahNow.pageOff], ahNow.find, ahNow.len);
    return p;
}
static bool callsIntoCave(uint64_t site, const uint8_t *q) {
    return q[0] == 0xE8 && site + 5 + rd32(q + 1) >= fzCaveStart && site + 5 + rd32(q + 1) < fzCaveStart + fzCaveSize;
}

int main() {
    const uint64_t off = pageOf(ahPresent.staticAddr);

    {   // +374 (xmm0 = S): cvtss2sd + subsd (12 bytes) -> callq fz_present374_thunk + 7-byte nop
        std::vector<uint8_t> p = sitePage(), orig = p;
        bool ok = ahPatchPresent(p.data(), p.size(), off);
        const uint8_t *q = &p[ahPresent.pageOff];
        check(ok && q[0] == 0xE8 && (uint64_t)(ahPresent.staticAddr + 5 + rd32(q + 1)) == fzCaveStart + fzOffPresent374,
              "present: calls fz_present374_thunk in the blob");
        check(q[5] == 0x0F && q[6] == 0x1F && q[7] == 0x80 && q[8] == 0 && q[9] == 0 && q[10] == 0 && q[11] == 0,
              "present: 7-byte nop fills the rest");
        check(q[12] == 0xEB && q[13] == 0x05 && diffs(p, orig) <= 12, "present: jmp after the site, rest untouched");
        std::vector<uint8_t> again = p;
        check(!ahPatchPresent(again.data(), again.size(), off) && again == p, "present: idempotent");
        std::vector<uint8_t> w = sitePage(), wo = w;
        check(!ahPatchPresent(w.data(), w.size(), off + PAGE) && w == wo, "present: wrong file offset rejected");
    }
    {   // +388: callq SLSCurrentRealTime -> callq fz_present388_thunk
        std::vector<uint8_t> p = sitePage(), orig = p;
        bool ok = ahPatchNow(p.data(), p.size(), off);
        const uint8_t *q = &p[ahNow.pageOff];
        check(ok && q[0] == 0xE8 && (uint64_t)(ahNow.staticAddr + 5 + rd32(q + 1)) == fzCaveStart + fzOffPresent388 &&
              diffs(p, orig) <= 4, "now: calls fz_present388_thunk in the blob");
        std::vector<uint8_t> again = p;
        check(!ahPatchNow(again.data(), again.size(), off) && again == p, "now: idempotent");
        std::vector<uint8_t> w = sitePage(), wo = w;
        check(!ahPatchNow(w.data(), w.size(), off + PAGE) && w == wo, "now: wrong file offset rejected");
    }
    {   // both sites on one page, either order
        std::vector<uint8_t> a = sitePage(), b = sitePage();
        check(ahPatchPresent(a.data(), a.size(), off) && ahPatchNow(a.data(), a.size(), off), "same page: present then now");
        check(ahPatchNow(b.data(), b.size(), off) && ahPatchPresent(b.data(), b.size(), off), "same page: now then present");
    }
    {   // thunks are inside the copied blob
        check(fzOffPresent374 + 19 <= fzBlobLen && fzOffPresent388 + 17 <= fzBlobLen, "thunks lie inside the blob");
    }
    if (FILE *f = fopen(kCache, "rb")) {   // real cache page (an installed CASyncFix patches pages as they are read)
        std::vector<uint8_t> p(PAGE);
        fseeko(f, (off_t)off, SEEK_SET);
        bool rd = fread(p.data(), 1, PAGE, f) == PAGE;
        fclose(f);
        const uint8_t *q = &p[ahPresent.pageOff];
        bool presentDone = (q[0] == 0xF2 && q[2] == 0x58) || callsIntoCave(ahPresent.staticAddr, q);
        bool nowDone = callsIntoCave(ahNow.staticAddr, &p[ahNow.pageOff]);
        if (presentDone || nowDone) printf("     (sites already patched by the installed CASyncFix)\n");
        check(rd && (presentDone || ahPatchPresent(p.data(), p.size(), off)), "real cache: +374 patches (or is patched)");
        check(rd && (nowDone || ahPatchNow(p.data(), p.size(), off)), "real cache: +388 patches (or is patched)");
    } else {
        check(false, "real cache readable");
    }

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
