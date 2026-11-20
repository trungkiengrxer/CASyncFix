// Host test for the forced-flatten decision (CASyncFix/flat_policy.h).
//
// Run from the repository root:
//   clang -std=c11 Tests/flat_policy_test.c -o /tmp/flat_policy && /tmp/flat_policy

#include "../CASyncFix/flat_policy.h"

#include <stdio.h>

static int failures = 0;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

int main(void) {
    // Decisions SkyLight wrote at [rec+0x700]: 0 unevaluated, 1 never flatten, 2 unflatten, 3 flatten/reuse, 4 reflatten.
    // fpResolve(d, reason, prevD, prevPending, changedNow, tick) -> {decision, pending}
    #define R(d, reason, prevD, prevPending, changedNow, tick, wantD, wantPending) \
        (fpResolve(d, reason, prevD, prevPending, changedNow, tick).decision == (wantD) && \
         fpResolve(d, reason, prevD, prevPending, changedNow, tick).pending == (wantPending))
    check(R(1, FP_REASON_WSA_BACKDROP, 1, 0, 0, 0, 3, 0), "frontmost glass window (wsa-backdrop exclusion) is flattened");
    check(R(1, FP_REASON_GLASS_ATOP, 1, 0, 0, 0, 3, 0), "glass-atop-translucence exclusion is flattened");
    check(R(3, FP_REASON_EXPOSE, 3, 0, 0, 0, 3, 0), "already flattened and unchanged: reused");
    check(R(2, FP_REASON_CHANGE, 2, 0, 1, 0, 3, 0) && R(1, FP_REASON_WSA_BACKDROP, 1, 0, 1, 0, 3, 0),
          "first flatten of a changing window renders its current content: nothing pending");

    // A change while the window already has an image is held back and remembered...
    check(R(4, FP_REASON_EXPOSE, 3, 0, 1, 0, 3, 1) && R(2, FP_REASON_CHANGE, 3, 0, 1, 0, 3, 1) &&
          R(1, FP_REASON_WSA_BACKDROP, 3, 0, 1, 0, 3, 1), "changed content: the old image is reused, change pending");
    check(R(4, FP_REASON_EXPOSE, 3, 1, 1, 0, 3, 1), "still changing: still reused, still pending (frozen)");
    // ...and stays held back while the activity lasts, also in updates that find the content quiet: content that
    // changes every other frame would otherwise be re-rendered 30 times per second (Safari in Mission Control, 0.8.1).
    check(R(3, FP_REASON_EXPOSE, 3, 1, 0, 0, 3, 1) && R(1, FP_REASON_WSA_BACKDROP, 3, 1, 0, 0, 3, 1),
          "content quiet again with a change pending: still reused, still pending");
    check(R(3, FP_REASON_EXPOSE, 3, 1, 0, 1, 4, 0), "tick with a change pending: one re-render, nothing pending");
    check(R(3, FP_REASON_EXPOSE, 3, 0, 0, 0, 3, 0), "quiet with nothing pending: no render");

    // With a refresh rate, a changing window also re-renders on tick frames.
    check(R(4, FP_REASON_EXPOSE, 3, 0, 1, 1, 4, 0) && R(4, FP_REASON_EXPOSE, 3, 1, 1, 1, 4, 0),
          "tick + changed: one re-render, nothing pending");
    check(R(3, FP_REASON_EXPOSE, 3, 0, 0, 1, 3, 0), "tick without change: no render");

    // Structural exclusions, untracked windows and globally disabled flattening are never overridden.
    check(R(0, 0x100, 0, 0, 1, 1, 0, 0), "untracked window untouched");
    check(R(1, 0x8000000, 1, 0, 0, 0, 1, 0), "shimmed-buffered (non-CA) window untouched");
    check(R(1, 0x1000, 1, 0, 0, 0, 1, 0) && R(1, 0x800, 1, 0, 0, 0, 1, 0) && R(1, 0x40000, 1, 0, 0, 0, 1, 0) &&
          R(1, 0x200, 1, 0, 0, 0, 1, 0) && R(1, 0x800000, 1, 0, 0, 0, 1, 0),
          "desktop, background, empty-clip, tag-bit and other never-flatten reasons untouched");
    check(R(2, 0x4, 2, 0, 0, 0, 2, 0) && R(2, 0x40000000, 2, 0, 0, 0, 2, 0), "flattening switched off globally untouched");
    check(R(1, FP_REASON_WSA_BACKDROP | FP_REASON_PENDING, 1, 0, 0, 0, 3, 0),
          "a pending marker carried over in the reason does not change eligibility");

    // After the activity: an image we froze is refreshed the first time SkyLight wants to reuse it.
    check(fpAfter(3, 1) == 4, "idle, change pending, SkyLight reuses the image: one re-render");
    check(fpAfter(3, 0) == 3 && fpAfter(1, 1) == 1 && fpAfter(2, 1) == 2 && fpAfter(4, 1) == 4 && fpAfter(0, 1) == 0,
          "idle: every other decision already re-renders or is left alone");
    check(FP_REASON_PENDING == 0x80000000u, "pending marker is reason bit 31, above every FlatteningPolicyReason bit");

    // Tick: one refresh-long window every 1 / hz seconds; hz = 0 (the default) never refreshes.
    check(fpTick(100.0, 4) && fpTick(100.25, 4) && fpTick(100.5 + 0.01, 4), "tick at the start of each period");
    check(!fpTick(100.02, 4) && !fpTick(100.1, 4) && !fpTick(100.24, 4), "no tick during the rest of the period");
    int ticks = 0, never = 0;
    for (int f = 0; f < 600; f++) {
        ticks += fpTick(1234.5678 + f / 60.0, 4);
        never += fpTick(1234.5678 + f / 60.0, 0);
    }
    check(ticks >= 36 && ticks <= 44, "about hz ticks per second at 60 Hz (4 Hz, 10 s)");
    check(never == 0, "refresh rate 0: content is never re-rendered while animated");
    check(!fpTick(-1.0, 4) && !fpTick(0.0 / 0.0, 4), "negative or NaN time never ticks");
    check(FP_REFRESH_HZ_DEFAULT == 0, "refresh is off by default");

    // Window drag: the dragged window id SkyLight keeps in its session package data.
    check(fpDragged(0x23, 0x23) && !fpDragged(0x23, 0x42) && !fpDragged(0x23, 0) && !fpDragged(0, 0),
          "only the window whose id is the non-zero dragged id counts as dragged");

    // Live resize: the size-changed dirty bit, only when resize handling is on.
    check(fpResizing(0x8, 15) && fpResizing(0x10008, 15), "geometry dirty bit set: resizing");
    check(!fpResizing(0x10000, 15) && !fpResizing(0, 15) && !fpResizing(0x8, 0), "no geometry bit, or resize rate 0: not resizing");
    check(FP_RESIZE_HZ_DEFAULT == FP_RESIZE_FREEZE && FP_RESIZE_FREEZE > 60,
          "live resize by default: the old image until the resize pauses or ends (15 Hz looked jerky on 0.8.1)");
    int freezeTicks = 0;
    for (int f = 0; f < 600; f++) freezeTicks += fpTick(1234.5678 + f / 60.0, FP_RESIZE_FREEZE);
    check(freezeTicks == 0 && fpResizing(0x8, FP_RESIZE_FREEZE), "freeze rate: resize handling on, never a tick");
    check(fpEligible(1, FP_REASON_WSA_BACKDROP) && fpEligible(2, FP_REASON_CHANGE) && fpEligible(3, 0) && fpEligible(4, 0) &&
          !fpEligible(1, 0x1000) && !fpEligible(0, 0) && !fpEligible(2, 0x4), "eligibility matches fpDecide");

    // Motion tracking (Show Desktop moves windows by changing their position, frame after frame).
    static FmTable t;
    fmInit(&t);
    check(t.magic == FM_MAGIC && t.n == 0, "motion table starts empty");
    check(!fmUpdate(&t, 0x1000, 361, 312, 10.000), "first sight of a window: baseline, not moving");
    check(!fmUpdate(&t, 0x1000, 361, 312, 10.016), "same origin: not moving");
    check(fmUpdate(&t, 0x1000, 323, 335, 10.033), "origin changed between consecutive updates: moving");
    check(fmUpdate(&t, 0x1000, 5, 540, 10.050), "still moving");
    check(fmUpdate(&t, 0x1000, 5, 540, 10.100), "a short pause keeps it moving (hold)");
    check(!fmUpdate(&t, 0x1000, 5, 540, 10.050 + FM_HOLD + 0.01), "no change for longer than the hold: stopped");
    check(!fmUpdate(&t, 0x2000, 100, 100, 10.3) && !fmUpdate(&t, 0x1000, 5, 540, 10.3),
          "another window is tracked separately");
    check(fmUpdate(&t, 0x2000, 100, 101, 10.316) && !fmUpdate(&t, 0x1000, 5, 540, 10.316),
          "only the window that moved is moving");
    check(!fmUpdate(&t, 0x1000, 900, 900, 10.316 + FM_GAP + 0.1),
          "new origin after not being asked about for a while: new baseline, not moving");
    check(fmUpdate(&t, 0x1000, 901, 900, 10.316 + FM_GAP + 0.116), "and a change right after that is motion again");

    static FmTable full;
    fmInit(&full);
    for (int i = 0; i < FM_MAX; i++) fmUpdate(&full, 0x100 + i, i, i, 20.0);
    check(full.n == FM_MAX && !fmUpdate(&full, 0x9000, 1, 1, 20.1) && full.n == FM_MAX,
          "full table with recent entries: an unknown window is not tracked, nothing is overwritten");
    check(!fmUpdate(&full, 0x9000, 1, 1, 20.0 + FM_FORGET + 1) && fmUpdate(&full, 0x9000, 2, 1, 20.0 + FM_FORGET + 1.016),
          "full table with stale entries: the oldest slot is reused");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
