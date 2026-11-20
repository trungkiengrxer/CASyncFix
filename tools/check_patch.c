// Reads the QuartzCore instruction CASyncFix patches, in this process's mapping of the shared cache.
// Shared cache text pages are shared, so if the kext patched the page, every process sees it.
// Build: clang -O2 -framework QuartzCore -o tools/check_patch tools/check_patch.c
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <mach-o/dyld.h>

int main(void) {
    const uint64_t staticAddr = 0x7ff80d2f5cdd;   // movl $imm, 0xe38(%r15) in 25G72 (UUID F7DEA623...)
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *n = _dyld_get_image_name(i);
        if (!strstr(n, "/QuartzCore.framework/")) continue;
        const uint8_t *p = (const uint8_t *)(staticAddr + _dyld_get_image_vmaddr_slide(i));
        printf("QuartzCore slide 0x%lx, bytes:", (long)_dyld_get_image_vmaddr_slide(i));
        for (int k = 0; k < 11; k++) printf(" %02X", p[k]);
        printf("\n");
        if (p[0] != 0x41 || p[1] != 0xC7 || p[3] != 0x38) { printf("UNEXPECTED: not the target instruction\n"); return 2; }
        unsigned imm = p[7] | p[8] << 8 | p[9] << 16 | (unsigned)p[10] << 24;
        printf("finish_every immediate = %u -> %s\n", imm, imm == 0 ? "PATCHED (periodic finish disabled)" : "NOT patched");
        // test dl,1 ; je +0x28 in new_metal_context (raster order group gate). Must stay unpatched: the 0.2.0
        // patch corrupted window corners on Intel and was removed.
        const uint8_t *r = p - (staticAddr - 0x7ff80d2f59d8);
        printf("ROG gate bytes: %02X %02X %02X %02X %02X\n", r[0], r[1], r[2], r[3], r[4]);
        if (r[0] != 0xF6 || r[1] != 0xC2 || r[2] != 0x01) { printf("UNEXPECTED: not the ROG gate\n"); return 2; }
        bool rog = r[3] == 0x90 && r[4] == 0x90;
        // mov dword [rdi+0x3f8], imm in CA::OGL::Context::Context (flush_every, stock 64)
        const uint8_t *fl = p - (staticAddr - 0x7ff80d0f41cb);
        if (fl[0] != 0xC7 || fl[2] != 0xF8 || fl[3] != 0x03) { printf("UNEXPECTED: not the flush_every store\n"); return 2; }
        unsigned fe = fl[6] | fl[7] << 8 | fl[8] << 16 | (unsigned)fl[9] << 24;
        printf("flush_every immediate = %u -> %s\n", fe, fe == 64 ? "stock" : "PATCHED");
        printf("raster order group gate -> %s\n", rog ? "PATCHED: 0.2.0 still running, install 0.2.1 or boot with -casyncnorog" : "untouched (correct)");

        // Window freeze (0.4.0), SkyLight. The whole shared cache shares one slide, so QuartzCore's slide applies.
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const uint64_t cave = 0x7ff8097584d0, caveEnd = 0x7ff809759000;
        const uint64_t calls[2] = {0x7ff809321438, 0x7ff8093218de}, callee = 0x7ff809335b16, apply = 0x7ff809333a84;
        for (int k = 0; k < 2; k++) {
            const uint8_t *c = (const uint8_t *)(calls[k] + slide);
            int32_t rel; memcpy(&rel, c + 1, 4);
            uint64_t target = calls[k] + 5 + (int64_t)rel;
            printf("freeze call %c: %02X -> 0x%llx %s\n", 'A' + k, c[0], (unsigned long long)target,
                   c[0] != 0xE8 ? "UNEXPECTED" : target == callee ? "(stock)" :
                   (target >= cave && target < caveEnd) ? "(wrapper in cave: PATCHED)" : "UNEXPECTED");
        }
        const uint8_t *a = (const uint8_t *)(apply + slide);
        int32_t arel; memcpy(&arel, a + 1, 4);
        uint64_t atarget = apply + 5 + (int64_t)arel;
        printf("freeze apply: %02X %02X %02X %02X %02X %02X %02X %02X %s\n", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
               a[0] == 0x41 && a[1] == 0x83 && a[2] == 0xBE ? "(stock)" :
               a[0] == 0xE9 && atarget >= cave && atarget < caveEnd ? "(jmp to thunk in cave: PATCHED)" : "UNEXPECTED");
        const uint8_t *cv = (const uint8_t *)(cave + slide);
        int blob = 0; for (int k = 0; k < 64; k++) blob |= cv[k];
        printf("freeze cave: %s\n", blob ? "blob present" : "empty (stock)");

        // Present ahead (0.6.0): CDDisplay::present_update+374 cvtss2sd+subsd -> addsd const(%rip) + nop; +388
        // callq SLSCurrentRealTime -> callq thunk at cave page + 0xfd0 (S + const); const at the last 8 bytes of the
        // cave page (2/60 - 0.0005 = 0.032834 s; 0.5.0/0.5.1 used 0.016167 s).
        // 0.6.1: both present_update sites call thunks in the freeze blob (sub $8,%rsp; ...; callq; add; ret).
        const uint64_t present = 0x7ff80950f854, now = 0x7ff80950f862, realTime = 0x7ff809292003;
        const uint8_t *pr = (const uint8_t *)(present + slide), *nw = (const uint8_t *)(now + slide);
        int32_t prel, nrel; memcpy(&prel, pr + 1, 4); memcpy(&nrel, nw + 1, 4);
        uint64_t ptarget = present + 5 + (int64_t)prel, ntarget = now + 5 + (int64_t)nrel;
        const uint8_t *pt = (const uint8_t *)(ptarget + slide), *nt = (const uint8_t *)(ntarget + slide);
        bool pIn = pr[0] == 0xE8 && ptarget >= cave && ptarget < caveEnd, nIn = nw[0] == 0xE8 && ntarget >= cave && ntarget < caveEnd;
        printf("present ahead +374: %02X -> 0x%llx %s\n", pr[0], (unsigned long long)(pr[0] == 0xE8 ? ptarget : 0),
               pr[0] == 0xF3 && pr[1] == 0x0F && pr[2] == 0x5A ? "(stock)" :
               pIn && pt[0] == 0x48 && pt[4] == 0xBF ? "(blob thunk: PATCHED)" : pr[0] == 0xF2 ? "(old 0.6.0 form)" : "UNEXPECTED");
        printf("present ahead +388: %02X -> 0x%llx %s\n", nw[0], (unsigned long long)ntarget,
               nw[0] != 0xE8 ? "UNEXPECTED" : ntarget == realTime ? "(stock)" :
               nIn && nt[0] == 0x48 && nt[4] == 0x4C ? "(blob thunk: PATCHED)" : nIn ? "(old 0.5.x/0.6.0 thunk)" : "UNEXPECTED");
        // Forced flatten (0.8.x): ca_window_prep_ingestion_phase+9546, cmpq $-1, once(%rip) -> jmp fz_flat_thunk.
        // The blob's config follows the thunk code; its refresh rates are printed when the site is patched.
        // 0.7.0 patched +8603 (0x7ff809333097) and 0.7.1 / 0.7.2 patched +8537 (0x7ff809333055) instead.
        const uint8_t *old0 = (const uint8_t *)(0x7ff809333097 + slide), *old1 = (const uint8_t *)(0x7ff809333055 + slide);
        if (old0[0] == 0xE9 || old1[0] == 0xE9) printf("forced flatten: a 0.7.x glass site is patched: install 0.8.1\n");
        const uint64_t flat = 0x7ff809333446;
        const uint8_t *g = (const uint8_t *)(flat + slide);
        int32_t grel; memcpy(&grel, g + 1, 4);
        uint64_t gtarget = flat + 5 + (int64_t)grel;
        const uint8_t *gt = (const uint8_t *)(gtarget + slide);
        bool gIn = g[0] == 0xE9 && gtarget >= cave && gtarget < caveEnd;
        printf("forced flatten: %02X %02X %02X %02X %02X %s\n", g[0], g[1], g[2], g[3], g[4],
               g[0] == 0x48 && g[1] == 0x83 && g[2] == 0x3D ? "(stock)" :
               gIn && gt[0] == 0x4C && gt[1] == 0x89 && gt[2] == 0xF7 ? "(jmp to thunk in cave: PATCHED)" : "UNEXPECTED");
        // Blob config {magic "FCFG", freeze mode, budget ns, animHz, resizeHz}, 8-byte aligned somewhere in the cave.
        for (uint64_t off = 0; blob && off + 24 <= caveEnd - cave; off += 8) {
            uint32_t f[6]; memcpy(f, cv + off, sizeof f);
            if (f[0] != 0x46434647 || f[1] > 2 || f[4] > 60 || f[5] > 61) continue;   // skip the magic used as an immediate in code
            printf("blob config: freeze mode %u, content refresh %u Hz (0 = frozen while animated), resize refresh %u Hz (0 = stock resize, 61 = frozen until the resize pauses)\n",
                   f[1], f[4], f[5]);
            break;
        }
        return imm == 0 && !rog ? 0 : 1;
    }
    printf("QuartzCore not loaded\n");
    return 3;
}
