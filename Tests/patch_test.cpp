// Host-side test for the QuartzCore finish-interval patch.
//
// Run from the repository root:
//   clang++ -std=c++17 Tests/patch_test.cpp -o /tmp/casync_test && /tmp/casync_test

#include "../CASyncFix/casync_patch.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/sysctl.h>

// The real-page checks read fixed offsets of the 25G72 shared cache; any other build has other bytes there.
static bool onTargetBuild() {
    char build[32] = {};
    size_t len = sizeof build - 1;
    return sysctlbyname("kern.osversion", build, &len, nullptr, 0) == 0 && strcmp(build, "25G72") == 0;
}
static FILE *openTargetCache() {
    return onTargetBuild() ? fopen("/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h", "rb")
                           : nullptr;
}

static int failures = 0;

static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static const uint8_t kFind[] = {0x41, 0xF6, 0x87, 0x35, 0x0E, 0x00, 0x00, 0x04, 0x75, 0x0B, 0x41,
                                0xC7, 0x87, 0x38, 0x0E, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};

// Raster-order-group gate in new_metal_context (test dl,1 ; je ; mov rdi,[r15+0xee8]). Patching it (0.2.0)
// corrupted window corners on Intel, so the kext must never touch it.
static const uint8_t kRog[] = {0xF6, 0xC2, 0x01, 0x74, 0x28, 0x49, 0x8B, 0xBF, 0xE8, 0x0E, 0x00, 0x00};

// CA::OGL::Context::Context: mov [rdi+0x388], eax ; mov dword [rdi+0x3f8], 64 (flush_every) ; mov dword [rdi+0x5dc], ...
static const uint8_t kFlush[] = {0x89, 0x87, 0x88, 0x03, 0x00, 0x00, 0xC7, 0x87, 0xF8, 0x03, 0x00, 0x00,
                                 0x40, 0x00, 0x00, 0x00, 0xC7, 0x87, 0xDC, 0x05, 0x00, 0x00};

static void flushTests(size_t page) {
    const size_t at = 0x1c5;   // page offset in 25G72 (0x7ff80d0f41c5), immediate at +12
    {
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[at], kFlush, sizeof(kFlush));
        std::vector<uint8_t> want = p;
        want[at + 12] = 0x00; want[at + 13] = 0x01;   // 256, little endian
        check(flushFindPattern(p.data(), p.size()), "flush: finds the pattern");
        check(flushPatch(p.data(), p.size(), 256), "flush: reports a patch");
        check(p == want, "flush: only the immediate becomes 256");
        check(!flushPatch(p.data(), p.size(), 256) && p == want, "flush: idempotent on patched page");
    }
    {
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[at], kFlush, sizeof(kFlush));
        std::vector<uint8_t> want = p;
        want[at + 12] = 0x00;
        flushPatch(p.data(), p.size(), 0);
        check(p == want, "flush: value 0 disables the periodic flush");
    }
    {
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[at], kFlush, sizeof(kFlush));
        std::vector<uint8_t> orig = p;
        check(!flushPatch(p.data(), p.size(), 64) && p == orig, "flush: value 64 (stock) changes nothing");
    }
    {
        std::vector<uint8_t> p(page, 0x90), orig = p;
        check(!flushFindPattern(p.data(), p.size()) && !flushPatch(p.data(), p.size(), 256) && p == orig,
              "flush: unrelated page untouched");
    }
    {
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[page - 13], kFlush, 13);
        std::vector<uint8_t> orig = p;
        check(!flushPatch(p.data(), p.size(), 256) && p == orig, "flush: pattern cut at page end ignored");
    }
    check(!flushPatch(nullptr, page, 256), "flush: null page rejected");

    if (FILE *f = openTargetCache()) {
        std::vector<uint8_t> p(page);
        fseeko(f, 0xd0f4000, SEEK_SET);
        size_t got = fread(p.data(), 1, page, f);
        fclose(f);
        // Through the page cache the immediate may already be patched by an installed CASyncFix.
        // Accept the stock sequence or the same sequence with another immediate; anything else fails.
        const size_t imm = at + 12;
        bool full = got == page;
        bool sameOutsideImm = full && memcmp(&p[at], kFlush, 12) == 0 && memcmp(&p[imm + 4], &kFlush[16], 6) == 0;
        bool stock = sameOutsideImm && memcmp(&p[imm], &kFlush[12], 4) == 0;
        std::vector<uint8_t> want = p;
        if (stock) { want[imm] = 0x00; want[imm + 1] = 0x01; }
        flushPatch(p.data(), p.size(), 256);
        check(sameOutsideImm && p == want, "real 25G72 page: only the flush_every immediate at 0x1c5+12 changes (to 256)");
        if (!stock && sameOutsideImm) printf("     (page already patched by the installed kext: left as is)\n");
    } else {
        printf("SKIP real shared cache page for flush (not macOS 26.6 25G72, or file not found)\n");
    }
}

int main() {
    const size_t page = 4096, at = 0xcd3;
    flushTests(page);

    {   // the real location in 25G72: page offset 0xcd3
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[at], kFind, sizeof(kFind));
        std::vector<uint8_t> want = p;
        memset(&want[at + 17], 0, 4);
        check(caSyncFindPattern(p.data(), p.size()), "finds the pattern");
        check(caSyncPatch(p.data(), p.size()), "reports a patch");
        check(p == want, "only the 4-byte immediate becomes 0");
    }
    {   // unrelated page
        std::vector<uint8_t> p(page, 0x90), orig = p;
        check(!caSyncFindPattern(p.data(), p.size()), "no pattern in unrelated page");
        check(!caSyncPatch(p.data(), p.size()) && p == orig, "unrelated page untouched");
    }
    {   // already patched page must stay as is
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[at], kFind, sizeof(kFind));
        memset(&p[at + 17], 0, 4);
        std::vector<uint8_t> orig = p;
        check(!caSyncPatch(p.data(), p.size()) && p == orig, "idempotent on patched page");
    }
    {   // pattern cut by the page end: no match, no out-of-bounds read
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[page - 10], kFind, 10);
        std::vector<uint8_t> orig = p;
        check(!caSyncPatch(p.data(), p.size()) && p == orig, "pattern cut at page end ignored");
    }
    {   // two occurrences: both patched (cannot happen in 25G72, but must not stop half-way)
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[0x100], kFind, sizeof(kFind));
        memcpy(&p[0x800], kFind, sizeof(kFind));
        caSyncPatch(p.data(), p.size());
        check(p[0x100 + 17] == 0 && p[0x800 + 17] == 0, "every occurrence in the page patched");
    }
    check(!caSyncPatch(nullptr, page), "null page rejected");
    {   // both sequences in one page, as in 25G72: only the finish immediate may change
        std::vector<uint8_t> p(page, 0xCC);
        memcpy(&p[0x9d8], kRog, sizeof(kRog));
        memcpy(&p[at], kFind, sizeof(kFind));
        std::vector<uint8_t> want = p;
        memset(&want[at + 17], 0, 4);
        caSyncPatch(p.data(), p.size());
        check(p == want, "raster-order-group gate left untouched");
    }

    // Real data: the QuartzCore page of this machine's shared cache (25G72), if present.
    if (FILE *f = openTargetCache()) {
        std::vector<uint8_t> p(page);
        fseeko(f, 0xd2f5000, SEEK_SET);
        size_t got = fread(p.data(), 1, page, f);
        fclose(f);
        // Read through the page cache: if CASyncFix is installed, the page may already carry its patch
        // (and the raster-order-group gate may carry the reverted 0.2.0 patch until the next boot).
        const size_t imm = at + 17;
        bool preOk = got == page && (p[imm] == 0x80 || p[imm] == 0x00);
        std::vector<uint8_t> want = p;
        want[imm] = 0x00;
        caSyncPatch(p.data(), p.size());
        check(preOk && p == want, "real 25G72 page: only the immediate at 0xcd3+17 becomes 0");
    } else {
        printf("SKIP real shared cache page (not macOS 26.6 25G72, or file not found)\n");
    }

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
