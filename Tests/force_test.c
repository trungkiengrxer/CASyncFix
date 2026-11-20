// Host test for fz_force(): the function the blob runs in WindowServer at ca_window_prep_ingestion_phase+9546.
// Compiles blob.c in host mode with the three SkyLight externals replaced by test doubles, and lays out fake
// CGXWindow / backing record / session structures at the offsets the blob reads.
//
// Run from the repository root:
//   clang -std=c11 -DFZ_HOST_TEST -DFZ_TEST_FORCE -DFZ_TABLE_ADDR=0x580000000000ULL -DFM_TABLE_ADDR=0x580100000000ULL \
//         Tests/force_test.c -o /tmp/fz_force && /tmp/fz_force

#include <stdint.h>

uintptr_t fz_test_session;                             // value of __sessionControlRef
static _Bool testChanged;
_Bool fz_test_change_detected(void *policy, uint32_t *why) { (void)policy; (void)why; return testChanged; }


#include "../CASyncFix/freeze_blob/blob.c"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

// Fake structures, 8-byte aligned.
static uint64_t recBuf[0x100], winBuf[0x120], scBuf[8], aBuf[0x20], exposeBuf[0x20], pkgBuf[0x80];
#define REC ((uintptr_t)recBuf)
#define WIN ((uintptr_t)winBuf)
#define U8(p, off) (*(uint8_t *)((uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uintptr_t)(p) + (off)))
#define PTR(p, off) (*(uintptr_t *)((uintptr_t)(p) + (off)))
#define DBL(p, off) (*(double *)((uintptr_t)(p) + (off)))

static double now = 5000.0;
static void reset(void) {
    memset(recBuf, 0, sizeof recBuf); memset(winBuf, 0, sizeof winBuf); memset(scBuf, 0, sizeof scBuf);
    memset(aBuf, 0, sizeof aBuf); memset(exposeBuf, 0, sizeof exposeBuf); memset(pkgBuf, 0, sizeof pkgBuf);
    fz_test_session = (uintptr_t)scBuf;
    PTR(scBuf, 0x20) = (uintptr_t)aBuf;
    PTR(aBuf, 0x90) = (uintptr_t)exposeBuf;
    PTR(aBuf, 0xd8) = (uintptr_t)pkgBuf;
    U32(REC, 0) = 0x23;                         // window id
    DBL(WIN, 0x4f8) = 361; DBL(WIN, 0x500) = 312;   // aggregate transform, translation x / y
    testChanged = 0;
    now += 100;                                 // every scenario starts well after the previous one
}
// One update: SkyLight wrote decision / reason, then the thunk calls fz_force. Returns the decision left behind.
static uint8_t update(uint8_t d, uint32_t reason) {
    U8(REC, 0x700) = d;
    U32(REC, 0x704) = reason;
    DBL(REC, 0x710) = now;
    fz_force(REC, WIN);
    now += 1.0 / 60;
    uint8_t out = U8(REC, 0x700);
    memcpy((void *)(REC + 0x670), (void *)(REC + 0x700), 0x90);     // SkyLight: current snapshot -> previous (+10374)
    return out;
}

int main(void) {
    reset();
    check(update(1, FP_REASON_WSA_BACKDROP) == 1 && update(1, FP_REASON_WSA_BACKDROP) == 1 &&
          update(2, FP_REASON_CHANGE) == 2 && update(4, FP_REASON_EXPOSE) == 4,
          "idle desktop: SkyLight's decisions are left alone");

    reset();
    PTR(exposeBuf, 0xe0) = 1;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3 && update(1, FP_REASON_WSA_BACKDROP) == 3 && update(3, FP_REASON_EXPOSE) == 3,
          "Mission Control / Expose: a quiet glass window is flattened and reused");
    int refl = 0;
    for (int i = 0; i < 120; i++) refl += update(4, FP_REASON_EXPOSE) == 4;
    check(refl == 0, "Mission Control: content that keeps changing is not re-rendered (refresh rate 0 by default)");
    check(update(2, FP_REASON_CHANGE) == 3, "an unflatten-on-change decision is held back too");
    check(update(3, FP_REASON_EXPOSE) == 3 && update(3, FP_REASON_EXPOSE) == 3,
          "quiet updates during the animation do not render the held-back change either");
    testChanged = 1;    // glass window: SkyLight excludes it before looking at changes, the blob asks change_detected
    check(update(1, FP_REASON_WSA_BACKDROP) == 3 && update(1, FP_REASON_WSA_BACKDROP) == 3, "changing glass window: held back");
    testChanged = 0;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3 && update(1, FP_REASON_WSA_BACKDROP) == 3,
          "glass window quiet again: still drawn from the old image");
    PTR(exposeBuf, 0xe0) = 0;
    check(update(3, FP_REASON_EXPOSE) == 4, "after Mission Control: the first reuse of the stale image re-renders it");
    check(update(3, FP_REASON_EXPOSE) == 3 && (U32(REC, 0x704) & FP_REASON_PENDING) == 0, "and the marker is gone");
    PTR(exposeBuf, 0xe0) = 1;
    check(update(1, 0x1000) == 1 && update(1, 0x8000000) == 1 && update(0, 0x100) == 0 && update(2, 0x4) == 2,
          "Mission Control: structural exclusions, untracked and globally disabled flattening untouched");
    I32(WIN, 0x20) = 24;
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "non-zero window level (menu bar, Dock...) untouched");

    reset();
    PTR(exposeBuf, 0xe8) = 1;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3, "the second expose-animating field counts too");

    reset();
    PTR(WIN, 0x878) = 0x1234;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3 && update(2, FP_REASON_CHANGE) == 3, "mesh warp (Genie): forced");

    reset();
    PTR(pkgBuf, 0x3e0) = 1;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3, "space swipe: forced");

    reset();
    U32(pkgBuf, 0x10c) = 0x23;
    check(update(1, FP_REASON_WSA_BACKDROP) == 3 && update(1, FP_REASON_WSA_BACKDROP) == 3, "dragged window: forced, also while the mouse rests");
    U32(pkgBuf, 0x10c) = 0x42;
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "another window is being dragged: untouched");
    U32(pkgBuf, 0x10c) = 0;
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "drag over: SkyLight's decision is back");

    reset();    // Show Desktop: the translation of the window's aggregate transform changes update after update
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "moving window, first update: baseline");
    int forced = 0;
    for (int i = 0; i < 20; i++) { DBL(WIN, 0x4f8) -= 40; DBL(WIN, 0x500) += 30; forced += update(1, FP_REASON_WSA_BACKDROP) == 3; }
    check(forced == 20, "moving window: forced in every update in which it moved");
    check(update(1, FP_REASON_WSA_BACKDROP) == 3, "moving window: still forced right after the last move (hold)");
    now += 0.3;
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "moving window: SkyLight's decision is back once it rests");
    DBL(WIN, 0x4f8) += 5;
    check(update(3, FP_REASON_EXPOSE) == 3, "already flattened window that moves: nothing to do");

    reset();
    update(1, FP_REASON_WSA_BACKDROP);
    PTR(WIN, 0x170) = 0x1234;                   // 0.8.1 read the frame region here; Show Desktop never changes it
    check(update(1, FP_REASON_WSA_BACKDROP) == 1, "the frame region is not what tells a moving window");

    reset();    // live resize: the size-changed dirty bit is set in every update, content changes every update
    int r3 = 0, r4 = 0;
    for (int i = 0; i < 120; i++) {
        U32(REC, 0xc8) = 0x8;
        uint8_t d = update(2, FP_REASON_CHANGE);
        r3 += d == 3; r4 += d == 4;
    }
    check(r3 == 120 && r4 == 0, "live resize: drawn from the old image for as long as the size keeps changing");
    U32(REC, 0xc8) = 0x10000;
    check(update(3, FP_REASON_EXPOSE) == 4, "resize paused or over, SkyLight would reuse the image: re-rendered once");
    check(update(3, FP_REASON_EXPOSE) == 3, "and then reused");
    U32(REC, 0xc8) = 0x8;
    check(update(2, FP_REASON_CHANGE) == 3, "resize goes on: old image again");
    U32(REC, 0xc8) = 0x10000;
    check(update(2, FP_REASON_CHANGE) == 2, "no size change in this update: SkyLight's decision is back");

    reset();    // live resize of the frontmost glass window: SkyLight says 1 (wsa-backdrop), and its change test may
    testChanged = 0;                                // not see a change; the resize itself is the change
    r3 = r4 = 0;
    for (int i = 0; i < 120; i++) {
        U32(REC, 0xc8) = 0x8;
        uint8_t d = update(1, FP_REASON_WSA_BACKDROP);
        r3 += d == 3; r4 += d == 4;
    }
    check(r3 == 120 && r4 == 0, "live resize of a glass window: also drawn from the old image");

    fz_force(0, WIN); fz_force(REC, 0);
    check(1, "null record or window: no crash");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
