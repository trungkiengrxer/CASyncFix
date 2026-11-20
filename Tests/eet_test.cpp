// Host-side test for the EETOff decision logic.
//
// Run from the repository root:
//   clang++ -std=c++17 Tests/eet_test.cpp -o /tmp/eet_test && /tmp/eet_test

#include "../EETOff/eet_logic.hpp"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

int main() {
    // Value read on this machine (Precision 5540, BIOS default).
    check(eetDisabledValue(0x0024005FULL) == 0x002C005FULL, "sets only bit 19 on 0x0024005F");
    check(eetDisabledValue(0x002C005FULL) == 0x002C005FULL, "idempotent when EET already disabled");
    check(eetDisabledValue(0xFFFFFFFFFFF7FFFFULL) == 0xFFFFFFFFFFFFFFFFULL, "keeps every other bit");
    check(eetIsDisabled(0x002C005FULL) && !eetIsDisabled(0x0024005FULL), "reads bit 19");

    // Restore on stop: only bit 19 goes back to the boot value, other bits keep their current state.
    check(eetRestoredValue(0x002C005FULL, 0x0024005FULL) == 0x0024005FULL, "restores EET enabled");
    check(eetRestoredValue(0x002C0001ULL, 0x0024005FULL) == 0x00240001ULL, "keeps bits changed after boot");
    check(eetRestoredValue(0x002C005FULL, 0x002C005FULL) == 0x002C005FULL, "keeps EET disabled if it was at boot");

    // CPUID gate: GenuineIntel, family 6, KBL/CFL/WHL/CML only.
    check(eetCpuSupported(true, 6, 0x9E), "i7-9850H (CFL-H, 0x9E) supported");
    check(eetCpuSupported(true, 6, 0x8E), "KBL/WHL-U (0x8E) supported");
    check(eetCpuSupported(true, 6, 0xA5) && eetCpuSupported(true, 6, 0xA6), "CML (0xA5/0xA6) supported");
    check(!eetCpuSupported(true, 6, 0x97), "ADL (0x97) rejected");
    check(!eetCpuSupported(true, 6, 0x3C), "Haswell (0x3C) rejected");
    check(!eetCpuSupported(false, 6, 0x9E), "non-Intel rejected");
    check(!eetCpuSupported(true, 15, 0x9E), "other family rejected");

    // CPUID leaf 1 EAX decoding: 0x906ED -> family 6, model 0x9E.
    check(cpuidFamily(0x000906ED) == 6 && cpuidModel(0x000906ED) == 0x9E, "decodes 0x906ED");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
