//
//  kern_start.cpp
//  CASyncFix.kext
//
// Removes CoreAnimation's periodic CPU wait on non-Apple-Silicon GPUs.
//
// On GPUs without MTLGPUFamilyApple3 (every Intel/AMD/NVIDIA Mac), QuartzCore sets
// MetalContext::finish_every = 128, and MetalContext::set_surface calls finish()
// (-[MTLCommandBuffer waitUntilCompleted]) every 128 render-target switches. AppKit vibrancy and
// Liquid Glass switch targets ~1300 times per second during Genie/Expose, so WindowServer's main
// thread blocks ~10 times per second for the whole queued GPU work and drops to 30 fps.
//
// This plugin zeroes that 128 in the dyld shared cache page as it is paged in (same mechanism as
// RestrictEvents/FeatureUnlock), which disables the periodic wait. The flush every 64 switches stays.
//
// Second patch (other page): flush_every 64 -> casyncflush=N (default 256, 64 = stock, 0 = only end-of-frame
// commit). Fewer mid-frame command-buffer commits through the Intel host scheduler.
//
// Third patch (SkyLight, 3 sites + a code cave): freeze the costliest windows for 2 updates when WindowServer's
// per-frame window preparation exceeds its budget, like macOS 27's WS::Throttling. See freeze_*.hpp and
// docs/superpowers/specs/2026-11-15-window-freeze-design.md. casyncfreeze=0 off (default since 0.7.0), 1 measure only,
// 2 on; casyncfreezems=N budget in ms (default 6).
//
// Fifth patch (SkyLight, 1 site + the same cave): forced flatten during whole-window animations, see glassMode
// below and flat_policy.h. casyncglass=0 off, 1 on (default); casyncflathz=N, casyncresizehz=N refresh rates.
//
// Fourth patch (SkyLight present_update -> freeze blob): present each frame exactly two refreshes after its
// CoreAnimation time, so motion is even and CPU + GPU get at least one full refresh. See ahead_patch.hpp.
// casyncahead=1 on, 0 off (default since 0.6.2: no effect on Intel, see aheadOn); needs the freeze blob. A probe build (FZ_PROBE=1 ./build.sh)
// logs S, now and the present target of every frame through SkyLight's os_log.
//
// Load order: after Lilu, BEFORE SharedCacheWire (which must stay last to wire patched pages).
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>

#include <mach/memory_object_types.h>
#include <sys/vnode.h>

#include "casync_patch.hpp"
#include "ahead_patch.hpp"

#define MODULE_SHORT "casync"

static mach_vm_address_t originalCsValidatePage {};
static bool loggedPatch {false};
static bool loggedFlush {false};
static uint32_t flushEvery {256};
// Off by default since 0.7.0: with a 2 ms budget the freeze made the Dock flicker and nothing smoother, and its blob
// wrapper costs 3.5-6.4% of WindowServer's main thread (one mach_vm_region per window prepare). casyncfreeze=2 to enable.
static uint32_t freezeMode {0};
static uint32_t freezeBudgetMs {6};
static bool loggedFreeze[3] {};
// Forced flatten (0.8.x; boot-arg name kept from the 0.7.x "glass flatten"): SkyLight never flattens a window with
// WindowServer-aware backdrops, which the frontmost window of an app with active glass has, and unflattens a window
// whose content changes, so Mission Control / Expose / space swipes / Genie / window drags / Show Desktop / live
// resize re-render whole layer trees every frame while windows without glass (Edge...) are drawn from their
// flattened image. 1 (default): at the point where every flatten decision converges, the blob forces normal-level
// windows that are being animated, moved or resized as a whole to draw from their flattened image; a content change
// is held back and rendered once when the content settles (flat_policy.h, freeze_blob/blob.c fz_force); 0: off.
static uint32_t glassMode {1};
static uint32_t flatAnimHz {FP_REFRESH_HZ_DEFAULT};     // casyncflathz=N (0-60): content refresh while animated, 0 = frozen
static uint32_t flatResizeHz {FP_RESIZE_HZ_DEFAULT};    // casyncresizehz=N: 0 = stock resize, 1-60 = refresh rate, 61 = old image until the resize pauses
static bool loggedGlass {false};
// Off by default: the 0.6.1 probe showed CoreDisplay on the Intel path ignores the present time (every frame goes up at
// the next vblank, glass == S for 99% of frames), so this patch has no effect here. casyncahead=1 to enable.
static uint32_t aheadOn {0};   // effective only with freezeMode != 0: the present code lives in the freeze blob
static bool loggedAhead[2] {};

// The SkyLight sites sit at fixed page offsets, so the pre-check is O(1).
static bool skylightCandidate(const uint8_t *page) {
    if (glassMode != 0 && (fzAt(page, PAGE_SIZE, fzFlat) || fzAt(page, PAGE_SIZE, fzCave)))
        return true;
    return freezeMode != 0 && (fzAt(page, PAGE_SIZE, fzCallA) || fzAt(page, PAGE_SIZE, fzCallB) ||
                               fzAt(page, PAGE_SIZE, fzApply) || fzAt(page, PAGE_SIZE, fzCave) ||
                               (aheadOn && (fzAt(page, PAGE_SIZE, ahPresent) || fzAt(page, PAGE_SIZE, ahNow))));
}

static void patchedCsValidatePage(vnode_t vnode, memory_object_t pager, memory_object_offset_t pageOffset,
                                  const void *data, int *validated, int *tainted, int *nx) {
    FunctionCast(patchedCsValidatePage, originalCsValidatePage)(vnode, pager, pageOffset, data, validated, tainted, nx);

    // Hot path: almost every page misses here, before any path lookup.
    auto page = static_cast<uint8_t *>(const_cast<void *>(data));
    if (vnode == nullptr ||
        !(caSyncFindPattern(page, PAGE_SIZE) || (flushEvery != flushStock && flushFindPattern(page, PAGE_SIZE)) ||
          skylightCandidate(page)))
        return;

    char path[PATH_MAX];
    int pathLen = PATH_MAX;
    if (vn_getpath(vnode, path, &pathLen) != 0 || !UserPatcher::matchSharedCachePath(path))
        return;

    if (caSyncPatch(page, PAGE_SIZE) && !__atomic_exchange_n(&loggedPatch, true, __ATOMIC_RELAXED)) {
        SYSLOG(MODULE_SHORT, "patched QuartzCore finish_every 128 -> 0 in %s at page 0x%llx", path, pageOffset);
    }
    if (flushPatch(page, PAGE_SIZE, flushEvery) && !__atomic_exchange_n(&loggedFlush, true, __ATOMIC_RELAXED)) {
        SYSLOG(MODULE_SHORT, "patched QuartzCore flush_every 64 -> %u in %s at page 0x%llx", flushEvery, path, pageOffset);
    }
    if (freezeMode != 0 || glassMode != 0) {   // the cave holds the code of both features
        static const char *what[3] {"call sites", "apply site", "code cave"};
        bool done[3] {freezeMode != 0 && fzPatchCalls(page, PAGE_SIZE, pageOffset),
                      freezeMode != 0 && fzPatchApply(page, PAGE_SIZE, pageOffset),
                      fzPatchCave(page, PAGE_SIZE, pageOffset, freezeMode, (uint64_t)freezeBudgetMs * 1000000ULL,
                                  flatAnimHz, flatResizeHz)};
        for (int i = 0; i < 3; i++)
            if (done[i] && !__atomic_exchange_n(&loggedFreeze[i], true, __ATOMIC_RELAXED))
                SYSLOG(MODULE_SHORT, "patched SkyLight window freeze %s in %s at page 0x%llx", what[i], path, pageOffset);
    }
    if (glassMode != 0 && fzPatchFlat(page, PAGE_SIZE, pageOffset) &&
        !__atomic_exchange_n(&loggedGlass, true, __ATOMIC_RELAXED)) {
        SYSLOG(MODULE_SHORT, "patched SkyLight forced flatten site in %s at page 0x%llx", path, pageOffset);
    }
    if (freezeMode != 0 && aheadOn) {   // calls into the freeze blob (fz_present374/388_thunk)
        static const char *what[2] {"present_update +374", "present_update +388"};
        bool done[2] {ahPatchPresent(page, PAGE_SIZE, pageOffset), ahPatchNow(page, PAGE_SIZE, pageOffset)};
        for (int i = 0; i < 2; i++)
            if (done[i] && !__atomic_exchange_n(&loggedAhead[i], true, __ATOMIC_RELAXED))
                SYSLOG(MODULE_SHORT, "patched SkyLight present-ahead %s in %s at page 0x%llx", what[i], path, pageOffset);
    }
}

static void pluginStart() {
    uint32_t value = 0;
    if (PE_parse_boot_argn("casyncflush", &value, sizeof(value)))
        flushEvery = value;
    SYSLOG(MODULE_SHORT, "flush_every target %u%s", flushEvery, flushEvery == flushStock ? " (stock, not patched)" : "");
    if (PE_parse_boot_argn("casyncfreeze", &value, sizeof(value)) && value <= 2)
        freezeMode = value;
    if (PE_parse_boot_argn("casyncfreezems", &value, sizeof(value)) && value >= 1 && value <= 100)
        freezeBudgetMs = value;
    SYSLOG(MODULE_SHORT, "window freeze mode %u, budget %u ms", freezeMode, freezeBudgetMs);
    if (PE_parse_boot_argn("casyncglass", &value, sizeof(value)) && value <= 1)
        glassMode = value;
    if (PE_parse_boot_argn("casyncflathz", &value, sizeof(value)) && value <= 60)
        flatAnimHz = value;
    if (PE_parse_boot_argn("casyncresizehz", &value, sizeof(value)) && value <= FP_RESIZE_FREEZE)
        flatResizeHz = value;
    SYSLOG(MODULE_SHORT, "forced flatten mode %u, refresh %u Hz, resize refresh %u Hz", glassMode, flatAnimHz, flatResizeHz);
    if (PE_parse_boot_argn("casyncahead", &value, sizeof(value)))
        aheadOn = value != 0;
    SYSLOG(MODULE_SHORT, "present ahead %s%s", freezeMode != 0 && aheadOn ? "on" : "off", fzProbe ? " (probe log)" : "");
    lilu.onPatcherLoadForce([](void *, KernelPatcher &patcher) {
        if ((lilu.getRunMode() & LiluAPI::RunningNormal) == 0)
            return;
        KernelPatcher::RouteRequest request("_cs_validate_page", patchedCsValidatePage, originalCsValidatePage);
        if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &request, 1))
            SYSLOG(MODULE_SHORT, "failed to route _cs_validate_page");
    });
}

static const char *bootargOff[] {
    "-casyncoff"
};

static const char *bootargDebug[] {
    "-casyncdbg"
};

static const char *bootargBeta[] {
    "-casyncbeta"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::BigSur,
    KernelVersion::Tahoe,
    pluginStart
};
