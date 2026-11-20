/*
 * blob.c: window-freeze code injected into WindowServer (SkyLight cave, see ../freeze_sites.hpp).
 *
 * Built position independent and copied by the kext to static 0x7ff8097584d0 (fz_anchor, first byte of the blob).
 * The whole shared cache slides as one, so any static address X is reached at run time as
 * fz_anchor + (X - FZ_ANCHOR_STATIC). Nothing here may need a relocation or a writable global: state lives in a
 * private region allocated at a fixed address with a private VM tag.
 *
 * Host test mode (-DFZ_HOST_TEST) uses the real libsystem symbols instead, to unit-test fz_table().
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>

#include "../freeze_policy.h"
#include "../flat_policy.h"

#define FZ_ANCHOR_STATIC 0x7ff8097584d0ULL
#define FZ_CONFIG_MAGIC 0x46434647u      /* "FCFG" */
#define FZ_TAG 242
#ifndef FZ_TABLE_ADDR
#define FZ_TABLE_ADDR 0x500000000000ULL
#endif

typedef struct {
    uint32_t magic;
    uint32_t mode;          /* 1 measure only, 2 freeze (0 never reaches the blob: the kext does not patch) */
    uint64_t budgetNs;
    uint32_t animHz;        /* forced flatten: content refresh rate while animated, 0 = frozen (casyncflathz) */
    uint32_t resizeHz;      /* forced flatten: content refresh rate during live resize, 0 = leave resize alone */
} FzConfig;

/* Overwritten by the kext when it copies the blob. Lives in __text so it is part of the copied bytes. */
__attribute__((section("__TEXT,__text"), used, aligned(8)))
#ifndef FZ_RESIZE_HZ
#define FZ_RESIZE_HZ FP_RESIZE_HZ_DEFAULT       /* Tests/force_test.c builds with 15 to exercise the resize path */
#endif
const FzConfig fz_config = {FZ_CONFIG_MAGIC, 2, 6000000, FP_REFRESH_HZ_DEFAULT, FZ_RESIZE_HZ};

/* Read through volatile: the kext rewrites fz_config, so the compiler must not fold it into immediates. */
#define FZ_CFG ((const volatile FzConfig *)&fz_config)

typedef kern_return_t (*fz_region_fn)(vm_map_read_t, mach_vm_address_t *, mach_vm_size_t *, vm_region_flavor_t,
                                      vm_region_info_t, mach_msg_type_number_t *, mach_port_t *);
typedef kern_return_t (*fz_alloc_fn)(vm_map_t, mach_vm_address_t *, mach_vm_size_t, int);
typedef uint64_t (*fz_time_fn)(void);
typedef _Bool (*fz_prepare_fn)(void *, void *, _Bool);

typedef double (*fz_now_fn)(void);
typedef double (*fz_nextvbl_fn)(int);
typedef void (*fz_log_fn)(const void *, const void *, int, const char *, const uint8_t *, unsigned);

#ifndef FZ_PROBE
#define FZ_PROBE 0
#endif

#ifdef FZ_HOST_TEST
#include <mach/mach_time.h>
#define FZ_VM_REGION mach_vm_region
#define FZ_VM_ALLOCATE mach_vm_allocate
#define FZ_TIME mach_absolute_time
#define FZ_TASK mach_task_self()
double fz_test_now(void);
double fz_test_nextvbl(int display);
void fz_test_log(const void *dso, const void *log, int type, const char *fmt, const uint8_t *buf, unsigned size);
#define FZ_NOW fz_test_now
#define FZ_NEXTVBL fz_test_nextvbl
#define FZ_LOG fz_test_log
#define FZ_DSO ((const void *)0)
extern const void *fz_test_default_log;
#define FZ_DEFAULT_LOG fz_test_default_log
static const char fz_probe_format_host[] = "probe";
#define FZ_PROBE_FORMAT fz_probe_format_host
#else
/* thunk.s, offset 0 of the blob. Hidden: reached by a direct rip-relative lea, never through a GOT in __DATA. */
extern char fz_anchor[] __attribute__((visibility("hidden")));
#define FZ_RT(staticAddr) ((void *)(fz_anchor + ((staticAddr) - FZ_ANCHOR_STATIC)))
#define FZ_VM_REGION ((fz_region_fn)FZ_RT(0x7ff802e655b6ULL))
#define FZ_VM_ALLOCATE ((fz_alloc_fn)FZ_RT(0x7ff802e63706ULL))
#define FZ_TIME ((fz_time_fn)FZ_RT(0x7ff802e61ec7ULL))
#define FZ_TASK (*(mach_port_t *)FZ_RT(0x7ff8437c1e58ULL))          /* mach_task_self_ */
#define FZ_ALWAYS (*(uintptr_t *)FZ_RT(0x7ff840d30528ULL))          /* kCAWindowLayerFlattenModeAlways */
#define FZ_ORIG_PREPARE ((fz_prepare_fn)FZ_RT(0x7ff809335b16ULL))   /* ca_prepare_begin_window_update */
#define FZ_NOW ((fz_now_fn)FZ_RT(0x7ff809292003ULL))                 /* SLSCurrentRealTime */
#define FZ_NEXTVBL ((fz_nextvbl_fn)FZ_RT(0x7ff80949e75fULL))         /* WSDisplayScheduleGetNextVBL */
#define FZ_LOG ((fz_log_fn)FZ_RT(0x7ff809683a4cULL))                 /* SkyLight stub for _os_log_impl */
#define FZ_DSO ((const void *)FZ_RT(0x7ff8091fd000ULL))              /* SkyLight mach header (__dso_handle) */
#define FZ_DEFAULT_LOG (*(const void **)FZ_RT(0x7ff84380d9e0ULL))   /* SkyLight default_log */
/* SkyLight's own os_log format (logd decodes formats by offset in the image): "*** Solver: Testing proposed tile
   addition for spid = %llu {origin=(%f, %f), size=(%f, %f)}", 1 x llu + 4 x double, public scalars. */
#define FZ_PROBE_FORMAT ((const char *)FZ_RT(0x7ff80970dfa0ULL))

/* The compiler may emit calls to these for struct/array operations; keep them inside the blob. */
#undef memset
#undef memcpy
__attribute__((used)) void *memset(void *d, int c, unsigned long n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
__attribute__((used)) void *memcpy(void *d, const void *s, unsigned long n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return r;
}
#endif

/* Our private zero-filled region at `want` (it starts exactly there and carries FZ_TAG), else 0. Allocates it once
   and then sets *fresh. */
static void *fz_region_at(mach_vm_address_t want, mach_vm_size_t bytes, int *fresh) {
    mach_vm_address_t addr = want;
    mach_vm_size_t size = 0;
    vm_region_extended_info_data_t info;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = FZ_VM_REGION(FZ_TASK, &addr, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info, &count,
                                    &object);
    *fresh = 0;
    if (kr == KERN_SUCCESS && addr <= want) {
        if (addr == want && info.user_tag == FZ_TAG) return (void *)want;
        return 0;   /* something else maps this address: never touch it */
    }
    mach_vm_address_t a = want;
    mach_vm_size_t need = (bytes + 0xfff) & ~(mach_vm_size_t)0xfff;
    if (FZ_VM_ALLOCATE(FZ_TASK, &a, need, VM_FLAGS_FIXED | VM_MAKE_TAG(FZ_TAG)) != KERN_SUCCESS || a != want) return 0;
    *fresh = 1;
    return (void *)want;
}

/* The freeze table at addr, initialised on first use. */
static FzTable *fz_table_at(mach_vm_address_t want) {
    int fresh;
    FzTable *t = (FzTable *)fz_region_at(want, sizeof(FzTable), &fresh);
    if (t && fresh) fzInit(t, FZ_CFG->mode, FZ_CFG->budgetNs);
    return t;
}

/* ponytail: one mach_vm_region trap per call (~1 us, about 2 per window per frame) because the blob has no writable
   global to cache the pointer in; cache it in the table page header if it ever shows up in a profile. */
__attribute__((used)) FzTable *fz_table(void) {
    if (FZ_CFG->magic != FZ_CONFIG_MAGIC) return 0;
    return fz_table_at(FZ_TABLE_ADDR);
}

#ifndef FZ_HOST_TEST
/* Replaces `callq ca_prepare_begin_window_update` at call sites A and B (same signature). */
__attribute__((used)) _Bool fz_prepare_wrapper(void *window, void *state, _Bool flag) {
    FzTable *t = fz_table();
    if (!t) return FZ_ORIG_PREPARE(window, state, flag);
    uint64_t t0 = FZ_TIME();
    _Bool r = FZ_ORIG_PREPARE(window, state, flag);
    uintptr_t rec = *(uintptr_t *)((char *)window + 0x18);
    if (rec) fzRecord(t, rec, FZ_TIME() - t0);
    return r;
}

#endif

#if !defined(FZ_HOST_TEST) || defined(FZ_TEST_PRESENT)
/* Present time for CDDisplay::present_update (see ../ahead_patch.hpp). S is the vblank the update was scheduled for and
   every window's CoreAnimation time; presenting at S + 2 refreshes - 0.5 ms gives a fixed glass - S of 2 refreshes.
   The probe build logs one record per present: spid = mach_absolute_time, origin = (site, now), size = (S, target). */
static void fz_put8(uint8_t *p, const void *v) {
    for (int i = 0; i < 8; i++) p[i] = ((const uint8_t *)v)[i];
}

__attribute__((used)) double fz_present_target(int site, double S) {
    double target = S + (2.0 / 60.0 - 0.0005);
    const void *log = FZ_PROBE ? FZ_DEFAULT_LOG : 0;
    if (log) {   /* default_log is set up by SkyLight; never hand os_log a null log */
        uint64_t mt = FZ_TIME();
        double vals[4] = {(double)site, FZ_NOW(), S, target};
        uint8_t buf[2 + 5 * 10];
        buf[0] = 0x00;          /* flags */
        buf[1] = 5;             /* argument count */
        buf[2] = 0x00; buf[3] = 8;
        fz_put8(buf + 4, &mt);
        for (int i = 0; i < 4; i++) {   /* argument i + 1 at 2 + 10 * (i + 1) */
            buf[12 + 10 * i] = 0x00;
            buf[13 + 10 * i] = 8;
            fz_put8(buf + 14 + 10 * i, &vals[i]);
        }
        FZ_LOG(FZ_DSO, log, 0, FZ_PROBE_FORMAT, buf, sizeof buf);
    }
    return target;
}

/* +388 branch: S of this display (r14 = CDDisplay, vtable slot 0x30 = display_number). */
__attribute__((used)) double fz_present388(void *display) {
    int (*displayNumber)(void *) = (int (*)(void *))(*(void ***)display)[0x30 / 8];
    return fz_present_target(388, FZ_NEXTVBL(displayNumber(display)));
}
#endif

#ifndef FZ_HOST_TEST
/* Called by fz_apply_thunk with rec = r14 at ca_window_prep_ingestion_phase+11144, before SkyLight reads the dirty
   word [rec+0xc8] and applies the flatten properties. */
__attribute__((used)) void fz_apply_rec(uintptr_t rec) {
    FzTable *t = fz_table();
    if (!t || !rec) return;
    uintptr_t *mode = (uintptr_t *)(rec + 0x160);
    uint8_t *flags0 = (uint8_t *)(rec + 0x250);     /* bit 7: shouldFlatten */
    uint8_t *flags1 = (uint8_t *)(rec + 0x251);     /* bit 0: shouldReflatten */
    FzAction a;
    if (!fzApply(t, rec, *mode, (*flags0 >> 7) & 1, *flags1 & 1, FZ_ALWAYS, &a)) return;
    *mode = a.mode;
    *flags0 = (uint8_t)((*flags0 & 0x7f) | (a.flatten << 7));
    *flags1 = (uint8_t)((*flags1 & 0xfe) | a.reflatten);
    *(uint32_t *)(rec + 0xc8) |= a.dirty;
}
#endif

#if !defined(FZ_HOST_TEST) || defined(FZ_TEST_FORCE)
#ifndef FM_TABLE_ADDR
#define FM_TABLE_ADDR 0x500100000000ULL     /* far from FZ_TABLE_ADDR: same-tag neighbours could be coalesced */
#endif

/* The two SkyLight externals of the forced-flatten code; Tests/force_test.c replaces them with test doubles. */
#ifdef FZ_TEST_FORCE
extern uintptr_t fz_test_session;
_Bool fz_test_change_detected(void *policy, uint32_t *why);
#define FZ_SESSION_REF fz_test_session
#define FZ_CHANGE_DETECTED fz_test_change_detected
#else
#define FZ_SESSION_REF (*(uintptr_t *)FZ_RT(0x7ff843c013d8ULL))                               /* __sessionControlRef */
#define FZ_CHANGE_DETECTED ((_Bool (*)(void *, uint32_t *))FZ_RT(0x7ff809275174ULL))        /* FlatteningPolicyData::change_detected */
#endif

/* Mission Control / Expose is up, a space swipe is running (the same tests SkyLight's own flatten policy makes at
   ca_window_prep_ingestion_phase+7759 and +8969), or the window with id `wid` is being dragged. */
static int fz_session_animating(uint32_t wid) {
    uintptr_t sc = FZ_SESSION_REF;
    uintptr_t a = sc ? *(uintptr_t *)(sc + 0x20) : 0;
    if (!a) return 0;
    uintptr_t expose = *(uintptr_t *)(a + 0x90), pkg = *(uintptr_t *)(a + 0xd8);   /* CGXSessionPackageData */
    if (expose && (*(uintptr_t *)(expose + 0xe8) || *(uintptr_t *)(expose + 0xe0))) return 1;
    return pkg && (*(uintptr_t *)(pkg + 0x3e0) || fpDragged(wid, *(uint32_t *)(pkg + 0x10c)));
}

/* The window moved on screen since the last update we were asked about it (Show Desktop...). Costs one
   mach_vm_region trap to find the table, so callers ask only for windows SkyLight is about to re-render. */
static int fz_window_moving(uintptr_t rec, uintptr_t win, double now) {
    int fresh;
    FmTable *t = (FmTable *)fz_region_at(FM_TABLE_ADDR, sizeof(FmTable), &fresh);
    if (!t) return 0;
    if (fresh) t->magic = FM_MAGIC;     /* a fresh region is zero-filled: same state as fmInit, without the loop */
    if (t->magic != FM_MAGIC) return 0;
    /* [win+0x498]: the window's aggregate CATransform3D (_XGetWindowTransform key 0x7fffff00), the one
       ca_window_prep_ingestion_phase+17862 positions the window layer with; +0x60 / +0x68 = m41 / m42. The frame
       region [win+0x170] does not change during Show Desktop (0.8.1 watched it: never forced). */
    return fmUpdate(t, rec, *(double *)(win + 0x4f8), *(double *)(win + 0x500), now);
}

/* Called by fz_flat_thunk at ca_window_prep_ingestion_phase+9546 with the window's backing record and CGXWindow,
   right after SkyLight wrote its flatten decision. Forces normal-level windows to be drawn from their flattened image
   while they are animated, moved or resized as a whole, see ../flat_policy.h. */
__attribute__((used)) void fz_force(uintptr_t rec, uintptr_t win) {
    if (!rec || !win || *(int32_t *)(win + 0x20) != 0) return;         /* window level 0 only: not Dock, menu bar... */
    uint8_t *decision = (uint8_t *)(rec + 0x700);
    uint32_t *reason = (uint32_t *)(rec + 0x704);
    uint8_t d = *decision, prevD = *(uint8_t *)(rec + 0x670);          /* [rec+0x670..]: previous update's snapshot */
    int prevPending = (*(uint32_t *)(rec + 0x674) & FP_REASON_PENDING) != 0;
    *reason &= ~FP_REASON_PENDING;      /* SkyLight may have copied the previous reason, marker included */
    if (!fpEligible(d, *reason)) return;
    double now = *(double *)(rec + 0x710);
    int resizing = fpResizing(*(uint32_t *)(rec + 0xc8), FZ_CFG->resizeHz);
    if (!resizing && !*(uintptr_t *)(win + 0x878) && !fz_session_animating(*(uint32_t *)rec) &&   /* 0x878: mesh warp */
        (d == 3 || !fz_window_moving(rec, win, now))) {
        *decision = fpAfter(d, prevPending);
        return;
    }
    /* Content changed in this update: SkyLight said so (2 / 4), the window is being resized, or, for a glass
       window that SkyLight excluded before looking at changes (1), its own change test says so. */
    int changedNow = resizing || d == 2 || d == 4;
    if (d == 1 && !resizing) {
        uint32_t why = 0;
        changedNow = FZ_CHANGE_DETECTED((void *)(rec + 0x660), &why);
    }
    FpResult r = fpResolve(d, *reason, prevD, prevPending, changedNow, fpTick(now, resizing ? FZ_CFG->resizeHz : FZ_CFG->animHz));
    *decision = r.decision;
    if (r.pending) *reason |= FP_REASON_PENDING;
}
#endif
