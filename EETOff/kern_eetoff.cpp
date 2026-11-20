//
//  kern_eetoff.cpp
//  EETOff.kext
//
// 1. Disables Energy Efficient Turbo (MSR_POWER_CTL 0x1FC bit 19) at boot and after every wake, so single-core
//    turbo reaches ratio 41-42 instead of the ~3.7 GHz EET picks. Restored when the kext stops. Boot-arg -eetoff
//    disables the whole kext.
// 2. Since 0.3.0: keeps the MMIO copy of the package power limit (MCHBAR + 0x59A0) at least as high as MSR 0x610,
//    checking once a second, and publishes both in the IORegistry (see therm_logic.hpp for why). Never lowers the
//    MMIO copy, never writes it when locked; it is not restored on stop (that would put back a throttle).
//    Boot-args: -eetnommio (no MMIO access at all), -eetmmiosync (also write; default since 0.3.1 is read-only).
//
// MSRs are touched only after CPUID confirms an Intel family 6 KBL/CFL/WHL/CML core, where they exist (reading a
// missing MSR #GPs and panics; that is how VoltageShift panicked here). MCHBAR is read from the host bridge
// (PCI 0:0.0, Intel, class 0x0600) and accepted only inside the chipset window, see mchbarBase().
//

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <pexpert/pexpert.h>

#include "eet_logic.hpp"
#include "therm_logic.hpp"

static inline uint64_t readMsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static inline void writeMsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" : : "c"(msr), "a"(static_cast<uint32_t>(value)), "d"(static_cast<uint32_t>(value >> 32)));
}

static inline void cpuid(uint32_t leaf, uint32_t &a, uint32_t &b, uint32_t &c, uint32_t &d) {
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(0));
}

class EETOff : public IOService {
    OSDeclareDefaultStructors(EETOff)

    IONotifier *wakeNotifier {nullptr};
    uint64_t original {0};
    bool touched {false};

    // MMIO power-limit sync
    bool mmioOn {false}, mmioWrite {false};
    IOWorkLoop *workLoop {nullptr};
    IOTimerEventSource *timer {nullptr};
    IOMemoryMap *mchMap {nullptr};
    volatile uint32_t *plReg {nullptr};     // MCHBAR + 0x59A0 (low dword), +0x59A4 (high dword)
    uint64_t raplUnit {0}, lastMmio {~0ULL}, lastMsr {~0ULL};
    uint32_t syncs {0}, lookups {0};

    static IOReturn powerHandler(void *target, void *, UInt32 messageType, IOService *, void *, vm_size_t);
    void apply(const char *when);
    bool mapMchbar();
    void tick(IOTimerEventSource *sender);
    void publish(uint64_t mmio, uint64_t msr);

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(EETOff, IOService)

void EETOff::apply(const char *when) {
    uint64_t now = readMsr(kMsrPowerCtl);
    if (eetIsDisabled(now))
        return;
    writeMsr(kMsrPowerCtl, eetDisabledValue(now));
    IOLog("EETOff: %s: POWER_CTL 0x%llx -> 0x%llx\n", when, now, readMsr(kMsrPowerCtl));
}

IOReturn EETOff::powerHandler(void *target, void *, UInt32 messageType, IOService *, void *, vm_size_t) {
    // POWER_CTL is package-scoped, so one write on whichever CPU runs this is enough.
    if (messageType == kIOMessageSystemHasPoweredOn)
        static_cast<EETOff *>(target)->apply("wake");
    return kIOReturnSuccess;
}

// Finds the host bridge and maps the MCHBAR page that holds the power limit. PCI may not be published yet at
// start, so the timer retries (for about a minute).
bool EETOff::mapMchbar() {
    OSIterator *it = IOService::getMatchingServices(IOService::serviceMatching("IOPCIDevice"));
    if (it == nullptr)
        return false;
    uint64_t base = 0;
    while (OSObject *o = it->getNextObject()) {
        IOPCIDevice *dev = OSDynamicCast(IOPCIDevice, o);
        if (dev == nullptr || dev->getBusNumber() != 0 || dev->getDeviceNumber() != 0 || dev->getFunctionNumber() != 0)
            continue;
        if (dev->configRead16(kIOPCIConfigVendorID) != 0x8086 || (dev->configRead32(kIOPCIConfigRevisionID) >> 16) != 0x0600)
            continue;
        base = mchbarBase(dev->configRead32(0x48), dev->configRead32(0x4C));
        break;
    }
    it->release();
    if (base == 0)
        return false;
    // 0.3.0 panicked here: withAddressRange(..., kernel_task) treats the address as a kernel VIRTUAL address, so the
    // "mapping" was 0xFED15000 itself and the first read page-faulted. withPhysicalAddress maps the physical page.
    const IOPhysicalAddress page = (IOPhysicalAddress)(base + (kMchbarPowerLimitOffset & ~0xFFFu));
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(page, 0x1000, kIODirectionInOut);
    if (md == nullptr)
        return false;
    mchMap = md->map(kIOMapInhibitCache);
    md->release();
    if (mchMap == nullptr)
        return false;
    // Never dereference unless the mapping really is that physical page and is not the physical address itself.
    if (mchMap->getPhysicalAddress() != page || mchMap->getVirtualAddress() == page || mchMap->getLength() < 0x1000) {
        IOLog("EETOff: MCHBAR mapping check failed (phys 0x%llx virt 0x%llx), MMIO off\n",
              (uint64_t)mchMap->getPhysicalAddress(), (uint64_t)mchMap->getVirtualAddress());
        mchMap->release();
        mchMap = nullptr;
        return false;
    }
    plReg = reinterpret_cast<volatile uint32_t *>(mchMap->getVirtualAddress() + (kMchbarPowerLimitOffset & 0xFFF));
    setProperty("MCHBAR", base, 64);
    IOLog("EETOff: MCHBAR 0x%llx, MMIO power limit 0x%llx\n", base, (uint64_t)plReg[1] << 32 | plReg[0]);
    return true;
}

void EETOff::publish(uint64_t mmio, uint64_t msr) {
    if (mmio == lastMmio && msr == lastMsr)
        return;
    lastMmio = mmio; lastMsr = msr;
    setProperty("MMIO PowerLimit", mmio, 64);
    setProperty("MSR PowerLimit", msr, 64);
    setProperty("MMIO PL1 mW", (unsigned long long)thermUnitsToMilliwatts(thermPl1Units(mmio), raplUnit), 32);
    setProperty("MMIO PL2 mW", (unsigned long long)thermUnitsToMilliwatts(thermPl2Units(mmio), raplUnit), 32);
    setProperty("MSR PL1 mW", (unsigned long long)thermUnitsToMilliwatts(thermPl1Units(msr), raplUnit), 32);
    setProperty("MSR PL2 mW", (unsigned long long)thermUnitsToMilliwatts(thermPl2Units(msr), raplUnit), 32);
    setProperty("MMIO Locked", thermLocked(mmio));
}

void EETOff::tick(IOTimerEventSource *sender) {
    if (plReg == nullptr) {
        if (!mapMchbar()) {
            if (++lookups < 60)
                sender->setTimeoutMS(1000);
            else
                IOLog("EETOff: host bridge / MCHBAR not found, MMIO sync off\n");
            return;
        }
    }
    uint64_t mmio = (uint64_t)plReg[1] << 32 | plReg[0];
    uint64_t msr = readMsr(kMsrPkgPowerLimit);
    if (mmioWrite && mmioNeedsSync(mmio, msr)) {
        uint64_t v = mmioSynced(mmio, msr);
        plReg[1] = (uint32_t)(v >> 32);     // high dword (PL2) first, then low (PL1)
        plReg[0] = (uint32_t)v;
        uint64_t after = (uint64_t)plReg[1] << 32 | plReg[0];
        if (++syncs <= 20)
            IOLog("EETOff: MMIO power limit 0x%llx -> 0x%llx (MSR 0x%llx)\n", mmio, after, msr);
        setProperty("MMIO Syncs", syncs, 32);
        mmio = after;
    }
    publish(mmio, msr);
    sender->setTimeoutMS(1000);
}

bool EETOff::start(IOService *provider) {
    if (!IOService::start(provider))
        return false;

    char arg[8];
    if (PE_parse_boot_argn("-eetoff", arg, sizeof(arg))) {
        IOLog("EETOff: disabled by -eetoff\n");
        return false;
    }

    uint32_t a, b, c, d;
    cpuid(0, a, b, c, d);
    bool intel = b == 0x756E6547 && d == 0x49656E69 && c == 0x6C65746E;  // "GenuineIntel"
    cpuid(1, a, b, c, d);
    if (!eetCpuSupported(intel, cpuidFamily(a), cpuidModel(a))) {
        IOLog("EETOff: unsupported CPU (cpuid 0x%x), not touching MSRs\n", a);
        return false;
    }

    // Without wake notifications EET would silently return after sleep, so do nothing at all then.
    wakeNotifier = registerPrioritySleepWakeInterest(powerHandler, this);
    if (wakeNotifier == nullptr) {
        IOLog("EETOff: cannot register for wake notifications, not touching MSRs\n");
        return false;
    }
    original = readMsr(kMsrPowerCtl);
    touched = true;
    apply("boot");

    mmioOn = !PE_parse_boot_argn("-eetnommio", arg, sizeof(arg));
    // Read-only by default since the 0.3.0 panic: writing needs -eetmmiosync.
    mmioWrite = mmioOn && PE_parse_boot_argn("-eetmmiosync", arg, sizeof(arg));
    if (mmioOn) {
        raplUnit = readMsr(kMsrRaplPowerUnit);
        workLoop = IOWorkLoop::workLoop();
        if (workLoop != nullptr)
            timer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &EETOff::tick));
        if (timer != nullptr && workLoop->addEventSource(timer) == kIOReturnSuccess) {
            timer->setTimeoutMS(1000);
            IOLog("EETOff: MMIO power-limit sync %s\n", mmioWrite ? "on" : "read-only");
        } else {
            IOLog("EETOff: cannot create the MMIO sync timer\n");
        }
    }
    setProperty("MMIO Sync", mmioWrite ? "on" : mmioOn ? "read-only" : "off");
    registerService();
    return true;
}

void EETOff::stop(IOService *provider) {
    if (timer != nullptr) {
        timer->cancelTimeout();
        if (workLoop != nullptr)
            workLoop->removeEventSource(timer);
        timer->release();
        timer = nullptr;
    }
    if (workLoop != nullptr) {
        workLoop->release();
        workLoop = nullptr;
    }
    if (mchMap != nullptr) {
        plReg = nullptr;
        mchMap->release();
        mchMap = nullptr;
    }
    if (wakeNotifier != nullptr) {
        wakeNotifier->remove();
        wakeNotifier = nullptr;
    }
    if (touched) {
        uint64_t now = readMsr(kMsrPowerCtl), back = eetRestoredValue(now, original);
        if (back != now)
            writeMsr(kMsrPowerCtl, back);
    }
    IOService::stop(provider);
}
