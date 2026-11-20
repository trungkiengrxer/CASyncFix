//
//  therm_logic.hpp
//  EETOff.kext
//
// Pure logic of the 0.3.0 MMIO power-limit sync, shared by the kext and Tests/therm_test.cpp.
//
// Why: on this Dell the CPU gets pinned at 800 MHz (iGPU 100 MHz) at 62-70 C, on AC and on battery, with MSR 0x610 at
// the user's 25 / 35 W and powermetrics saying "CPU LIMIT PL2_PL3". The package obeys the lower of two copies of the
// power limit: MSR 0x610 and the MMIO copy at MCHBAR + 0x59A0. The hypothesis is that the BIOS lowers the MMIO copy;
// on Windows Intel DTT rewrites it, on Linux `throttled` does. The kext raises the MMIO copy back to the MSR values
// (never lowers it, never touches it when its lock bit is set) and publishes both in the IORegistry.
//

#ifndef therm_logic_hpp
#define therm_logic_hpp

#include <stdint.h>

static constexpr uint32_t kMsrRaplPowerUnit = 0x606;
static constexpr uint32_t kMsrPkgPowerLimit = 0x610;
static constexpr uint32_t kMchbarPowerLimitOffset = 0x59A0;

// RAPL power unit: 1 / 2^(bits 3:0) W.
static inline uint32_t thermWattsToUnits(int milliwatts, uint64_t raplPowerUnit) {
    return (uint32_t)(((uint64_t)milliwatts << (raplPowerUnit & 0xF)) / 1000);
}
static inline int thermUnitsToMilliwatts(uint32_t units, uint64_t raplPowerUnit) {
    return (int)(((uint64_t)units * 1000) >> (raplPowerUnit & 0xF));
}

// PKG_POWER_LIMIT layout (MSR and MMIO): PL1 bits 14:0, enable 15; PL2 bits 46:32, enable 47; lock 63.
static inline uint32_t thermPl1Units(uint64_t pl) { return (uint32_t)(pl & 0x7FFF); }
static inline uint32_t thermPl2Units(uint64_t pl) { return (uint32_t)((pl >> 32) & 0x7FFF); }
static inline bool thermPl1Enabled(uint64_t pl) { return (pl >> 15) & 1; }
static inline bool thermPl2Enabled(uint64_t pl) { return (pl >> 47) & 1; }
static inline bool thermLocked(uint64_t pl) { return (pl >> 63) & 1; }

// The MMIO copy limits the package below the MSR limits (only enabled MSR limits count); a locked copy is never written.
static inline bool mmioNeedsSync(uint64_t mmio, uint64_t msr) {
    if (thermLocked(mmio)) return false;
    bool pl1 = thermPl1Enabled(msr) && (!thermPl1Enabled(mmio) || thermPl1Units(mmio) < thermPl1Units(msr));
    bool pl2 = thermPl2Enabled(msr) && (!thermPl2Enabled(mmio) || thermPl2Units(mmio) < thermPl2Units(msr));
    return pl1 || pl2;
}
// The MMIO copy with each limit that is lower than the MSR one raised to it; time windows and other bits kept.
static inline uint64_t mmioSynced(uint64_t mmio, uint64_t msr) {
    uint64_t v = mmio;
    if (thermPl1Enabled(msr) && (!thermPl1Enabled(mmio) || thermPl1Units(mmio) < thermPl1Units(msr)))
        v = (v & ~0x7FFFULL) | thermPl1Units(msr) | (1ULL << 15);
    if (thermPl2Enabled(msr) && (!thermPl2Enabled(mmio) || thermPl2Units(mmio) < thermPl2Units(msr)))
        v = (v & ~(0x7FFFULL << 32)) | ((uint64_t)thermPl2Units(msr) << 32) | (1ULL << 47);
    return v;
}

// MCHBAR from host bridge (PCI 0:0.0) config 0x48 (low) / 0x4C (high): bit 0 = enabled, base aligned to 32 KB.
// Only a base inside the chipset window 0xFE000000-0xFEFFFFFF is accepted (0xFED10000 on this platform).
static inline uint64_t mchbarBase(uint32_t lo, uint32_t hi) {
    if (!(lo & 1) || hi != 0) return 0;
    uint64_t base = lo & ~0x7FFFu;
    return base >= 0xFE000000ULL && base <= 0xFEFF8000ULL ? base : 0;
}

#endif /* therm_logic_hpp */
