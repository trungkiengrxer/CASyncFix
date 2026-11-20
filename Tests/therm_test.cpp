// Host-side test for EETOff 0.3.0 power-limit logic (EETOff/therm_logic.hpp): decoding PKG_POWER_LIMIT and syncing the
// MMIO copy (MCHBAR + 0x59A0) up to the MSR 0x610 limits, like ThrottleStop "Sync MMIO" / Linux `throttled`.
//
// Run from the repository root:
//   clang++ -std=c++17 Tests/therm_test.cpp -o /tmp/therm_test && /tmp/therm_test

#include "../EETOff/therm_logic.hpp"

#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

int main() {
    const uint64_t pu = 0x000A0E03ULL;                       // RAPL power unit 1/8 W (this CPU)
    check(thermWattsToUnits(35000, pu) == 280 && thermUnitsToMilliwatts(280, pu) == 35000, "1/8 W units");

    const uint64_t msr = 0x00428118004280C8ULL;              // MSR 0x610: PL1 25 W (0xC8), PL2 35 W (0x118), enabled
    check(thermPl1Units(msr) == 200 && thermPl2Units(msr) == 280, "decodes PL1 25 W / PL2 35 W");
    check(!thermLocked(msr) && thermLocked(msr | (1ULL << 63)), "lock bit 63");

    // MMIO limit lowered by the BIOS to ~9 W / 9 W (0x48 units), windows of its own.
    const uint64_t low = 0x00DE8048005E8048ULL;
    check(mmioNeedsSync(low, msr), "MMIO below MSR: needs sync");
    uint64_t s = mmioSynced(low, msr);
    check(thermPl1Units(s) == 200 && thermPl2Units(s) == 280, "synced: PL1 / PL2 raised to the MSR values");
    check((s & (1ULL << 15)) && (s & (1ULL << 47)), "synced: both limits enabled");
    check((s & 0x00FE0000ULL) == (low & 0x00FE0000ULL) && (s & 0x00FE000000000000ULL) == (low & 0x00FE000000000000ULL),
          "synced: MMIO time windows kept");
    check(!mmioNeedsSync(s, msr), "after sync: nothing to do");

    // MMIO already at or above the MSR limits (BIOS default 45 W / 56 W): left alone, never lowered.
    const uint64_t high = 0x004281C000DD8168ULL;
    check(!mmioNeedsSync(high, msr), "MMIO above MSR: left alone");
    check(mmioNeedsSync(low | (1ULL << 63), msr) == false, "locked MMIO: never written");
    uint64_t pl1Disabled = msr & ~(1ULL << 15);              // PL1 disabled in the MSR: do not push 0 into MMIO
    check(!mmioNeedsSync(low, pl1Disabled & ~(1ULL << 47)), "MSR limits disabled: no sync");

    // MCHBAR from PCI 0:0.0 config 0x48 / 0x4C: bit 0 enable, base aligned to 32 KB, must be in the chipset window.
    check(mchbarBase(0xFED10001u, 0) == 0xFED10000ULL, "MCHBAR base 0xFED10000");
    check(mchbarBase(0xFED10000u, 0) == 0, "MCHBAR disabled -> 0");
    check(mchbarBase(0x00000001u, 0) == 0 && mchbarBase(0xFED10001u, 1) == 0, "MCHBAR outside 0xFE000000-0xFEFFFFFF -> 0");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
