/*
 * freeze_policy.h
 * CASyncFix.kext
 *
 * Window-freeze policy, ported from macOS 27's WS::Throttling ("freeze-top-n-windows"), shared by the host tests and
 * the machine-code blob injected into WindowServer. Pure C, no libc, no allocation.
 *
 * Per frame, the blob records how long WindowServer spent preparing each window (fzRecord). When the frame's total
 * preparation exceeds the budget, up to FZ_TOP_N windows whose own time exceeds budget * 0.95 / windows are picked.
 * A picked window is then, at the flatten-apply point of each of its next updates (fzApply):
 *   FROZEN x2  : flattenMode = Always, shouldFlatten = 1, shouldReflatten = 0  -> CA reuses the old flattened image
 *   REFLATTEN  : shouldReflatten = 1                                           -> one fresh flatten
 *   RESTORE    : the window's own mode and flags back                          -> normal policy, 2 frames cooldown
 */
#ifndef freeze_policy_h
#define freeze_policy_h

#include <stdint.h>

#define FZ_MAGIC 0x465a5431u        /* "FZT1" */
#define FZ_MAX_WINDOWS 64
#define FZ_TOP_N 10
#define FZ_FREEZE_UPDATES 2
#define FZ_COOLDOWN_FRAMES 2
#define FZ_DROP_UNSEEN 8
#define FZ_DIRTY_FLATTEN 0x10000u       /* [rec+0xc8] bit 16: setShouldFlatten:   */
#define FZ_DIRTY_REFLATTEN 0x20000u     /* [rec+0xc8] bit 17: setShouldReflatten: */
#define FZ_DIRTY_MODE 0x10000000u       /* [rec+0xc8] bit 28: setFlattenMode:     */

enum { FZ_NORMAL = 0, FZ_FROZEN = 1, FZ_REFLATTEN = 2, FZ_RESTORE = 3 };

typedef struct {
    uintptr_t rec;          /* backing record [CGXWindow+0x18]; compared only, never dereferenced here */
    uint64_t dt;            /* preparation time in the last frame, ns */
    uintptr_t origMode;
    uint8_t state, left, cooldown, unseen, seen, origFlatten, origReflatten, pick;
} FzWin;

typedef struct {
    uint32_t magic;
    uint32_t mode;          /* 1 = measure only, 2 = freeze */
    uint64_t budgetNs;
    uint32_t n;
    FzWin w[FZ_MAX_WINDOWS];
} FzTable;

typedef struct {
    uintptr_t mode;
    uint8_t flatten, reflatten;
    uint32_t dirty;         /* bits to OR into [rec+0xc8] */
} FzAction;

static inline void fzInit(FzTable *t, uint32_t mode, uint64_t budgetNs) {
    uint8_t *p = (uint8_t *)t;
    for (unsigned long i = 0; i < sizeof(*t); i++) p[i] = 0;
    t->magic = FZ_MAGIC;
    t->mode = mode;
    t->budgetNs = budgetNs;
}

static inline FzWin *fzFind(FzTable *t, uintptr_t rec) {
    for (uint32_t i = 0; i < t->n; i++)
        if (t->w[i].rec == rec) return &t->w[i];
    return 0;
}

static inline void fzEndFrame(FzTable *t) {
    uint64_t total = 0;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < t->n; i++)
        if (t->w[i].seen) { total += t->w[i].dt; seen++; }

    if (t->mode == 2 && seen != 0 && total > t->budgetNs) {
        uint64_t alloted = t->budgetNs * 95 / 100 / seen;
        for (int k = 0; k < FZ_TOP_N; k++) {
            FzWin *best = 0;
            for (uint32_t i = 0; i < t->n; i++) {
                FzWin *w = &t->w[i];
                if (!w->seen || w->pick || w->state != FZ_NORMAL || w->cooldown || w->dt <= alloted) continue;
                if (!best || w->dt > best->dt) best = w;
            }
            if (!best) break;
            best->pick = 1;
        }
    }

    uint32_t j = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        FzWin w = t->w[i];
        if (w.seen) w.unseen = 0;
        else if (++w.unseen >= FZ_DROP_UNSEEN) continue;   /* window gone: drop */
        if (w.state == FZ_NORMAL && w.cooldown) w.cooldown--;
        w.seen = 0;
        t->w[j++] = w;
    }
    t->n = j;
}

static inline void fzRecord(FzTable *t, uintptr_t rec, uint64_t dtNs) {
    FzWin *w = fzFind(t, rec);
    if (w && w->seen) {             /* window prepared again: the previous frame is over */
        fzEndFrame(t);
        w = fzFind(t, rec);
    }
    if (!w) {
        if (t->n >= FZ_MAX_WINDOWS) return;
        w = &t->w[t->n++];
        w->rec = rec;
        w->origMode = 0;
        w->state = FZ_NORMAL;
        w->left = w->cooldown = w->pick = w->origFlatten = w->origReflatten = 0;
    }
    w->dt = dtNs;
    w->seen = 1;
    w->unseen = 0;
}

static inline int fzApply(FzTable *t, uintptr_t rec, uintptr_t curMode, uint8_t curFlatten, uint8_t curReflatten,
                          uintptr_t alwaysMode, FzAction *out) {
    FzWin *w = fzFind(t, rec);
    if (!w || t->mode != 2) return 0;
    if (w->pick) {
        w->pick = 0;
        w->origMode = curMode;
        w->origFlatten = curFlatten;
        w->origReflatten = curReflatten;
        w->state = FZ_FROZEN;
        w->left = FZ_FREEZE_UPDATES;
    }
    switch (w->state) {
    case FZ_FROZEN:
        out->mode = alwaysMode;
        out->flatten = 1;
        out->reflatten = 0;
        out->dirty = FZ_DIRTY_FLATTEN | FZ_DIRTY_REFLATTEN | FZ_DIRTY_MODE;
        if (--w->left == 0) w->state = FZ_REFLATTEN;
        return 1;
    case FZ_REFLATTEN:
        out->mode = curMode;
        out->flatten = curFlatten;
        out->reflatten = 1;
        out->dirty = FZ_DIRTY_REFLATTEN;
        w->state = FZ_RESTORE;
        return 1;
    case FZ_RESTORE:
        out->mode = w->origMode;
        out->flatten = w->origFlatten;
        out->reflatten = w->origReflatten;
        out->dirty = FZ_DIRTY_FLATTEN | FZ_DIRTY_REFLATTEN | FZ_DIRTY_MODE;
        w->state = FZ_NORMAL;
        w->cooldown = FZ_COOLDOWN_FRAMES;
        return 1;
    default:
        return 0;
    }
}

#endif /* freeze_policy_h */
