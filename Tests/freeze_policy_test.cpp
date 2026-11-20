// Host test for the window-freeze policy (CASyncFix/freeze_policy.h).
//
// Run from the repository root:
//   clang++ -std=c++17 Tests/freeze_policy_test.cpp -o /tmp/fz_policy && /tmp/fz_policy

#include "../CASyncFix/freeze_policy.h"

#include <cstdio>
#include <cstring>

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static const uint64_t MS = 1000000;            // ns per ms
static const uintptr_t ALWAYS = 0xA1;          // stand-in for kCAWindowLayerFlattenModeAlways
static const uintptr_t DEFAULT_MODE = 0xD0;    // stand-in for the window's own mode
static FzTable t;

static int applyOnce(uintptr_t rec, FzAction *a) {
    memset(a, 0, sizeof *a);
    return fzApply(&t, rec, DEFAULT_MODE, 0, 0, ALWAYS, a);
}

static void frame(const uintptr_t *recs, const uint64_t *dts, int n) {
    for (int i = 0; i < n; i++) fzRecord(&t, recs[i], dts[i]);
    fzEndFrame(&t);
}

int main() {
    FzAction a;

    {   // idle_frame_no_freeze
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[] = {0x1000, 0x2000, 0x3000};
        uint64_t d[] = {3 * MS, 1 * MS, 1 * MS};   // 5 ms < 6 ms
        frame(r, d, 3);
        bool none = true;
        for (int i = 0; i < 3; i++) none &= applyOnce(r[i], &a) == 0;
        check(none, "idle_frame_no_freeze");
    }
    {   // selects_top_n_over_alloted
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[12]; uint64_t d[12];
        for (int i = 0; i < 12; i++) { r[i] = 0x1000 * (i + 1); d[i] = MS / 10; }
        d[0] = 3 * MS; d[1] = 2 * MS; d[2] = 1 * MS;   // alloted = 6*0.95/12 = 0.475 ms
        frame(r, d, 12);
        bool ok = applyOnce(r[0], &a) == 1 && applyOnce(r[1], &a) == 1 && applyOnce(r[2], &a) == 1;
        for (int i = 3; i < 12; i++) ok &= applyOnce(r[i], &a) == 0;
        check(ok, "selects_top_n_over_alloted");
    }
    {   // selection_caps_at_ten
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[20]; uint64_t d[20];
        for (int i = 0; i < 20; i++) { r[i] = 0x1000 * (i + 1); d[i] = MS; }   // 20 ms total, all over alloted
        frame(r, d, 20);
        int picked = 0;
        for (int i = 0; i < 20; i++) picked += applyOnce(r[i], &a);
        check(picked == 10, "selection_caps_at_ten");
    }
    {   // freeze_sequence
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[] = {0x1000};
        uint64_t d[] = {8 * MS};
        frame(r, d, 1);
        memset(&a, 0, sizeof a);
        bool ok = fzApply(&t, 0x1000, DEFAULT_MODE, 1, 0, ALWAYS, &a) == 1 &&
                  a.mode == ALWAYS && a.flatten == 1 && a.reflatten == 0 && a.dirty == 0x10030000;
        ok = ok && fzApply(&t, 0x1000, ALWAYS, 1, 0, ALWAYS, &a) == 1 && a.mode == ALWAYS && a.dirty == 0x10030000;
        ok = ok && fzApply(&t, 0x1000, ALWAYS, 1, 0, ALWAYS, &a) == 1 && a.reflatten == 1 && a.dirty == 0x20000 &&
             a.mode == ALWAYS && a.flatten == 1;
        ok = ok && fzApply(&t, 0x1000, ALWAYS, 1, 1, ALWAYS, &a) == 1 && a.mode == DEFAULT_MODE && a.flatten == 1 &&
             a.reflatten == 0 && a.dirty == 0x10030000;
        ok = ok && fzApply(&t, 0x1000, DEFAULT_MODE, 1, 0, ALWAYS, &a) == 0;
        check(ok, "freeze_sequence: frozen x2, reflatten, restore originals, then nothing");
        frame(r, d, 1);   // cooldown frame 1
        bool cool1 = applyOnce(0x1000, &a) == 0;
        frame(r, d, 1);   // cooldown frame 2
        bool cool2 = applyOnce(0x1000, &a) == 0;
        frame(r, d, 1);   // eligible again
        bool again = applyOnce(0x1000, &a) == 1;
        check(cool1 && cool2 && again, "freeze_sequence: not re-picked for 2 frames after restore");
    }
    {   // stale_entry_dropped
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[] = {0x1000, 0x2000};
        uint64_t d[] = {1 * MS, 1 * MS};
        frame(r, d, 2);
        uintptr_t r2[] = {0x2000};
        for (int i = 0; i < 8; i++) frame(r2, d, 1);
        check(t.n == 1 && t.w[0].rec == 0x2000, "stale_entry_dropped: unseen 8 frames");
    }
    {   // empty_frame_no_selection
        fzInit(&t, 2, 6 * MS);
        fzEndFrame(&t);
        fzEndFrame(&t);
        check(t.n == 0, "empty_frame_no_selection: no division by zero, nothing added");
    }
    {   // table_full_ignores_new
        fzInit(&t, 2, 6 * MS);
        uintptr_t r[FZ_MAX_WINDOWS]; uint64_t d[FZ_MAX_WINDOWS];
        for (int i = 0; i < FZ_MAX_WINDOWS; i++) { r[i] = 0x1000 * (i + 1); d[i] = MS / 100; }
        d[0] = 50 * MS;
        frame(r, d, FZ_MAX_WINDOWS);
        fzRecord(&t, 0xFFFF000, 50 * MS);
        bool ok = t.n == FZ_MAX_WINDOWS;
        for (int i = 0; i < FZ_MAX_WINDOWS; i++) ok &= t.w[i].rec != 0xFFFF000;
        ok &= applyOnce(r[0], &a) == 1 && applyOnce(0xFFFF000, &a) == 0;
        check(ok, "table_full_ignores_new: 65th window never stored, frozen entry kept");
    }
    {   // measure_only_never_writes
        fzInit(&t, 1, 6 * MS);
        uintptr_t r[] = {0x1000, 0x2000};
        uint64_t d[] = {20 * MS, 20 * MS};
        frame(r, d, 2);
        frame(r, d, 2);
        check(applyOnce(0x1000, &a) == 0 && applyOnce(0x2000, &a) == 0, "measure_only_never_writes");
    }
    {   // frame_boundary_on_repeat
        fzInit(&t, 2, 6 * MS);
        fzRecord(&t, 0x1000, 8 * MS);     // frame 1
        bool before = applyOnce(0x1000, &a) == 0;   // not selected yet: frame not ended
        fzRecord(&t, 0x1000, 8 * MS);     // repeat => frame 1 ends, selection runs, frame 2 starts
        bool after = applyOnce(0x1000, &a) == 1;
        check(before && after, "frame_boundary_on_repeat");
    }
    {   // unknown_rec_ignored
        fzInit(&t, 2, 6 * MS);
        check(applyOnce(0xDEAD000, &a) == 0, "unknown_rec_ignored");
    }

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
