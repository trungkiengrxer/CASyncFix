// Host test for the window-freeze page patches (CASyncFix/freeze_patch.hpp).
//
// Run from the repository root (after CASyncFix/freeze_blob/build_blob.sh):
//   clang++ -std=c++17 Tests/freeze_patch_test.cpp -o /tmp/fz_patch && /tmp/fz_patch

#include "../CASyncFix/freeze_patch.hpp"

#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <vector>
#include <sys/sysctl.h>

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static const size_t PAGE = 4096;
static const char *kCache = "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h";

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static std::vector<uint8_t> pageWith(const FzSite &s) {
    std::vector<uint8_t> p(PAGE, 0xCC);
    memcpy(&p[s.pageOff], s.find, s.len);
    return p;
}
static uint64_t fileOff(const FzSite &s) { return (s.staticAddr - fzCacheBase) & ~0xfffULL; }
// The patch sites are static addresses of the 25G72 shared cache; any other build has other bytes there.
static bool onTargetBuild() {
    char build[32] = {};
    size_t len = sizeof build - 1;
    return sysctlbyname("kern.osversion", build, &len, nullptr, 0) == 0 && strcmp(build, "25G72") == 0;
}
static size_t diffs(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i];
    return n;
}

int main() {
    const size_t caveOff = fzCaveStart & 0xfff;   // 0x4d0

    // calls: rel32 retargeted to the wrapper, nothing else changes
    for (const FzSite *s : {&fzCallA, &fzCallB}) {
        std::vector<uint8_t> p = pageWith(*s), orig = p;
        bool ok = fzPatchCalls(p.data(), p.size(), fileOff(fzCallA));
        int32_t want = (int32_t)(fzCaveStart + fzOffWrapper - (s->staticAddr + 5));
        check(ok && p[s->pageOff] == 0xE8 && rd32(&p[s->pageOff + 1]) == want && diffs(p, orig) <= 4,
              s == &fzCallA ? "call A retargeted to wrapper" : "call B retargeted to wrapper");
        std::vector<uint8_t> again = p;
        check(!fzPatchCalls(again.data(), again.size(), fileOff(fzCallA)) && again == p, "calls: idempotent");
    }
    {   // apply: E9 rel32 to the thunk + 3 nops over the 8-byte cmpl at +11144
        std::vector<uint8_t> p = pageWith(fzApply), orig = p;
        bool ok = fzPatchApply(p.data(), p.size(), fileOff(fzApply));
        int32_t want = (int32_t)(fzCaveStart + fzOffThunk - (fzApply.staticAddr + 5));
        const uint8_t *q = &p[fzApply.pageOff];
        check(ok && q[0] == 0xE9 && rd32(q + 1) == want && q[5] == 0x90 && q[6] == 0x90 && q[7] == 0x90 &&
              diffs(p, orig) <= 8, "apply: jmp to thunk + 3 nops");
        std::vector<uint8_t> again = p;
        check(!fzPatchApply(again.data(), again.size(), fileOff(fzApply)) && again == p, "apply: idempotent");
    }
    {   // wrong_file_offset_rejected: same bytes at another file offset are never patched
        std::vector<uint8_t> a = pageWith(fzCallA), ao = a, b = pageWith(fzApply), bo = b;
        std::vector<uint8_t> c(PAGE, 0);
        memcpy(&c[fzCave.pageOff], fzCave.find, fzCave.len);
        std::vector<uint8_t> co = c;
        bool any = fzPatchCalls(a.data(), PAGE, fileOff(fzCallA) + 0x1000) ||
                   fzPatchApply(b.data(), PAGE, fileOff(fzApply) + 0x1000) ||
                   fzPatchCave(c.data(), PAGE, fileOff(fzCave) + 0x1000, 2, 6000000);
        check(!any && a == ao && b == bo && c == co, "wrong_file_offset_rejected");
    }
    {   // cave: blob copied, config written, resume rel32 filled
        std::vector<uint8_t> p(PAGE, 0);
        memcpy(&p[fzCave.pageOff], fzCave.find, fzCave.len);
        bool ok = fzPatchCave(p.data(), p.size(), fileOff(fzCave), 1, 4000000);
        bool blobOk = true;
        for (size_t i = 0; i < fzBlobLen; i++) {
            if (i >= fzOffConfig && i < fzOffConfig + 24) continue;
            if (i >= fzOffResumeJmp + 1 && i < fzOffResumeJmp + 5) continue;
            if (i >= fzOffFlatResumeJmp + 1 && i < fzOffFlatResumeJmp + 5) continue;
            blobOk &= p[caveOff + i] == fzBlob[i];
        }
        uint32_t mode; uint64_t budget;
        memcpy(&mode, &p[caveOff + fzOffConfig + 4], 4);
        memcpy(&budget, &p[caveOff + fzOffConfig + 8], 8);
        int32_t want = (int32_t)(fzApplyResume - (fzCaveStart + fzOffResumeJmp + 5));
        check(ok && blobOk, "cave: blob copied at 0x4d0");
        check(mode == 1 && budget == 4000000, "cave: config mode/budget written");
        check(p[caveOff + fzOffResumeJmp] == 0xE9 && rd32(&p[caveOff + fzOffResumeJmp + 1]) == want,
              "cave: thunk resume jmp targets +16233");
        std::vector<uint8_t> again = p;
        check(!fzPatchCave(again.data(), again.size(), fileOff(fzCave), 1, 4000000) && again == p, "cave: idempotent");
    }
    {   // flat: the 8-byte cmpq at +9546 (where every flatten decision converges) becomes jmp fz_flat_thunk + 3 nops
        std::vector<uint8_t> p = pageWith(fzFlat), orig = p;
        bool ok = fzPatchFlat(p.data(), p.size(), fileOff(fzFlat));
        int32_t want = (int32_t)(fzCaveStart + fzOffFlatThunk - (fzFlat.staticAddr + 5));
        const uint8_t *q = &p[fzFlat.pageOff];
        check(ok && q[0] == 0xE9 && rd32(q + 1) == want && q[5] == 0x90 && q[6] == 0x90 && q[7] == 0x90 &&
              diffs(p, orig) <= 8 && memcmp(q + 8, fzFlat.find + 8, fzFlat.len - 8) == 0,
              "flat: jmp to thunk + 3 nops, the following jne untouched");
        std::vector<uint8_t> again = p;
        check(!fzPatchFlat(again.data(), again.size(), fileOff(fzFlat)) && again == p, "flat: idempotent");
        std::vector<uint8_t> w = pageWith(fzFlat), wo = w;
        check(!fzPatchFlat(w.data(), PAGE, fileOff(fzFlat) + 0x1000) && w == wo, "flat: wrong file offset rejected");
        check(!fzPatchFlat(nullptr, PAGE, fileOff(fzFlat)), "flat: null page rejected");
    }
    {   // cave: the flat thunk's resume jmp is filled in
        std::vector<uint8_t> p(PAGE, 0);
        memcpy(&p[fzCave.pageOff], fzCave.find, fzCave.len);
        bool ok = fzPatchCave(p.data(), p.size(), fileOff(fzCave), 2, 6000000);
        const uint8_t *c = &p[caveOff];
        check(ok && c[fzOffFlatResumeJmp] == 0xE9 &&
              rd32(c + fzOffFlatResumeJmp + 1) == (int32_t)(fzFlatResume - (fzCaveStart + fzOffFlatResumeJmp + 5)),
              "cave: flat thunk resumes at ca_window_prep_ingestion_phase+9554");
        check(rd32(c + fzOffConfig + 16) == 0 && rd32(c + fzOffConfig + 20) == FP_RESIZE_FREEZE,
              "cave: default refresh rates written (animations 0 = frozen, resize frozen until it pauses)");
        std::vector<uint8_t> q(PAGE, 0);
        memcpy(&q[fzCave.pageOff], fzCave.find, fzCave.len);
        check(fzPatchCave(q.data(), q.size(), fileOff(fzCave), 0, 6000000, 4, 30) &&
              rd32(&q[caveOff + fzOffConfig + 16]) == 4 && rd32(&q[caveOff + fzOffConfig + 20]) == 30,
              "cave: boot-arg refresh rates written");
        check(rd32(&fzBlob[fzOffConfig + 16]) == 0 && rd32(&fzBlob[fzOffConfig + 20]) == FP_RESIZE_FREEZE,
              "blob carries the same default rates");
    }
    {   // cave_rejects_nonzero_tail
        std::vector<uint8_t> p(PAGE, 0);
        memcpy(&p[fzCave.pageOff], fzCave.find, fzCave.len);
        p[0xfff] = 1;
        std::vector<uint8_t> orig = p;
        check(!fzPatchCave(p.data(), p.size(), fileOff(fzCave), 2, 6000000) && p == orig, "cave_rejects_nonzero_tail");
    }
    {   // absent patterns and short pages untouched
        std::vector<uint8_t> p(PAGE, 0x90), orig = p;
        int any = (int)fzPatchCalls(p.data(), p.size(), fileOff(fzCallA)) |
                  (int)fzPatchApply(p.data(), p.size(), fileOff(fzApply)) |
                  (int)fzPatchCave(p.data(), p.size(), fileOff(fzCave), 2, 6000000);
        check(!any && p == orig, "unrelated page untouched");
        std::vector<uint8_t> s = pageWith(fzApply), so = s;
        check(!fzPatchApply(s.data(), fzApply.pageOff + 8, fileOff(fzApply)) && s == so, "pattern cut at the buffer end ignored");
        check(!fzPatchCalls(nullptr, PAGE, fileOff(fzCallA)) && !fzPatchApply(nullptr, PAGE, fileOff(fzApply)) &&
              !fzPatchCave(nullptr, PAGE, fileOff(fzCave), 2, 1),
              "null page rejected");
    }

    // Real pages of this machine's shared cache: only the intended bytes change.
    // Reads go through the page cache, so with CASyncFix loaded a page may already carry its patch: skipped then.
    if (FILE *f = onTargetBuild() ? fopen(kCache, "rb") : nullptr) {
        struct { const FzSite *s; int kind; const char *name; } sites[] = {
            {&fzCallA, 0, "real page: calls"}, {&fzApply, 1, "real page: apply"}, {&fzCave, 2, "real page: cave"},
            {&fzFlat, 3, "real page: flat"}};
        for (auto &x : sites) {
            std::vector<uint8_t> p(PAGE);
            fseeko(f, (off_t)((x.s->staticAddr - fzCacheBase) & ~0xfffULL), SEEK_SET);
            bool got = fread(p.data(), 1, PAGE, f) == PAGE;
            bool stock = got && memcmp(&p[x.s->pageOff], x.s->find, x.s->len) == 0;
            if (stock && x.kind == 2) for (size_t i = caveOff; i < PAGE; i++) stock &= p[i] == 0;
            if (got && !stock) { printf("SKIP %s (page already patched by the loaded kext)\n", x.name); continue; }
            std::vector<uint8_t> orig = p;
            bool ok = got && (x.kind == 0 ? fzPatchCalls(p.data(), PAGE, fileOff(*x.s))
                            : x.kind == 1 ? fzPatchApply(p.data(), PAGE, fileOff(*x.s))
                            : x.kind == 3 ? fzPatchFlat(p.data(), PAGE, fileOff(*x.s))
                                          : fzPatchCave(p.data(), PAGE, fileOff(*x.s), 2, 6000000));
            size_t d = diffs(p, orig);
            size_t limit = x.kind == 0 ? 8 : x.kind == 1 ? 8 : x.kind == 3 ? 8 : fzBlobLen;
            bool outside = false;   // for the cave, nothing before 0x4d0 may change
            if (x.kind == 2) for (size_t i = 0; i < caveOff; i++) outside |= p[i] != orig[i];
            check(ok && d > 0 && d <= limit && !outside, x.name);
        }
        fclose(f);
    } else {
        printf("SKIP real shared cache pages (not macOS 26.6 25G72, or file not found)\n");
    }

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
