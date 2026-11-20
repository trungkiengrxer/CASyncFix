//
//  eet_logic.hpp
//  EETOff.kext
//
// Pure decision logic, shared by the kext and the host test.
//
// MSR_POWER_CTL (0x1FC) bit 19 = "Energy Efficient Turbo disable". With EET enabled (BIOS default on
// this Dell), the CPU picks a ~3.7 GHz "efficient" turbo although XCPM requests ratio 46 and no limit
// reason is set. Disabling it gives ratio 41-42, the same as Windows on this machine.
//

#ifndef eet_logic_hpp
#define eet_logic_hpp

#include <stdint.h>

static constexpr uint32_t kMsrPowerCtl = 0x1FC;
static constexpr uint64_t kEetDisableBit = 1ULL << 19;

static inline uint64_t eetDisabledValue(uint64_t powerCtl) { return powerCtl | kEetDisableBit; }
static inline bool eetIsDisabled(uint64_t powerCtl) { return (powerCtl & kEetDisableBit) != 0; }
// On unload: put back only bit 19 as it was at boot, keep every other bit as it is now.
static inline uint64_t eetRestoredValue(uint64_t now, uint64_t atBoot) {
    return (now & ~kEetDisableBit) | (atBoot & kEetDisableBit);
}


// CPUID leaf 1 EAX -> display family / model.
static inline uint32_t cpuidFamily(uint32_t eax) {
    uint32_t f = (eax >> 8) & 0xF;
    return f == 0xF ? f + ((eax >> 20) & 0xFF) : f;
}
static inline uint32_t cpuidModel(uint32_t eax) {
    uint32_t f = (eax >> 8) & 0xF;
    uint32_t m = (eax >> 4) & 0xF;
    return (f == 0x6 || f == 0xF) ? m | (((eax >> 16) & 0xF) << 4) : m;
}

// Only Kaby Lake / Coffee Lake / Whiskey Lake / Comet Lake, where bit 19 was verified on hardware.
static inline bool eetCpuSupported(bool genuineIntel, uint32_t family, uint32_t model) {
    if (!genuineIntel || family != 6) return false;
    switch (model) {
        case 0x8E: case 0x9E: case 0xA5: case 0xA6: return true;
        default: return false;
    }
}

#endif /* eet_logic_hpp */
