/*
 * flat_policy.h
 * CASyncFix.kext
 *
 * Forced window flattening while a window is being animated, moved or resized as a whole (Mission Control, Expose,
 * space swipe, Genie minimize, window drag, Show Desktop, live resize), shared by the host test and the blob injected
 * into WindowServer. Pure C, no libc.
 *
 * SkyLight decides per window and update how to draw it ([rec+0x700]): 1 never flatten, 2 unflatten, 3 draw from the
 * flattened image, 4 re-render the flattened image, with a reason bit at [rec+0x704]. Decisions 1 / 2 / 4 make
 * WindowServer walk and render the window's whole layer tree in that frame. While one of the activities above runs
 * the blob rewrites the decision of normal app windows to 3, so they are drawn from one image like windows without
 * glass. A content change is held back while the content keeps changing and rendered once when it settles; with a
 * non-zero refresh rate a changing window also re-renders at most that many times per second.
 */
#ifndef flat_policy_h
#define flat_policy_h

#include <stdint.h>

/* Content refresh rates (Hz). Animations: 0 = the image is frozen until the animation ends (boot-arg casyncflathz,
   0.8.0 used 4). Live resize: the content changes every frame, so it is re-rendered at this rate and the previous
   image is reused in between; 0 = live resize is left to SkyLight (boot-arg casyncresizehz). 0.8.1 shipped 15: the
   user saw resize stutter in some apps (the content only follows the window 15 times per second) and asked for the
   content to be redrawn only when the resize is over, hence FP_RESIZE_FREEZE. */
#define FP_REFRESH_HZ_DEFAULT 0
#define FP_RESIZE_FREEZE 61         /* resize rate above 60: no refresh, the old image until the resize pauses or ends */
#define FP_RESIZE_HZ_DEFAULT FP_RESIZE_FREEZE

/* FlatteningPolicyReason bits (order of the strings in FlatteningPolicyData::stringify, "unknown" = 1). */
#define FP_REASON_CHANGE        0x40u       /* CA context committed */
#define FP_REASON_ANIMATION     0x80u
#define FP_REASON_WSA_BACKDROP  0x4000u     /* WindowServer-aware backdrop: the frontmost window of apps with glass */
#define FP_REASON_EXPOSE        0x10000u
#define FP_REASON_CHAMELEON     0x200000u
#define FP_REASON_RESOLUTION    0x400000u
#define FP_REASON_GLASS_ATOP    0x2000000u
#define FP_REASON_EXTERNAL      0x4000000u

#define FP_GLASS_REASONS  (FP_REASON_WSA_BACKDROP | FP_REASON_GLASS_ATOP)
#define FP_CHANGE_REASONS (FP_REASON_CHANGE | FP_REASON_ANIMATION | FP_REASON_CHAMELEON | FP_REASON_RESOLUTION | \
                           FP_REASON_EXTERNAL)

#define FP_DIRTY_GEOMETRY 0x8u              /* [rec+0xc8] bit 3: the window's size changed in this update */

/* SkyLight's decision may be overridden: never-flatten for a structural reason (non-CA window, desktop, empty
   clip...), untracked windows and globally disabled flattening are left alone. */
static inline int fpEligible(uint8_t d, uint32_t reason) {
    return d == 3 || d == 4 || (d == 1 && (reason & FP_GLASS_REASONS)) || (d == 2 && (reason & FP_CHANGE_REASONS));
}

/* Marker the blob keeps in the reason word: "this window's image misses a content change we held back". SkyLight
   copies decision and reason to the previous-update snapshot ([rec+0x670] / [rec+0x674], memcpy at
   ca_window_prep_ingestion_phase+10374), which is how the blob reads it back in the next update. No
   FlatteningPolicyReason uses bit 31. */
#define FP_REASON_PENDING 0x80000000u

typedef struct {
    uint8_t decision;
    uint8_t pending;
} FpResult;

/* The decision to use instead of SkyLight's `d` for a normal-level window that is being animated.
   prevD / prevPending: what the previous update ended with; changedNow: the content changed in this update;
   tick: a refresh-rate tick frame. A change is rendered only on a tick; otherwise the old image is reused and the
   change stays pending until a tick or the end of the activity (fpAfter). */
static inline FpResult fpResolve(uint8_t d, uint32_t reason, uint8_t prevD, int prevPending, int changedNow, int tick) {
    FpResult r = {d, 0};
    if (!fpEligible(d, reason)) return r;
    int hasImage = prevD == 3 || prevD == 4;
    if (!hasImage) { r.decision = 3; return r; }            /* flattening now renders the current content */
    int stale = changedNow || prevPending;
    if (stale && tick) { r.decision = 4; return r; }
    r.decision = 3;
    r.pending = stale;
    return r;
}

/* After the activity: the first time SkyLight wants to reuse an image that misses a held-back change, re-render it. */
static inline uint8_t fpAfter(uint8_t d, int prevPending) {
    return d == 3 && prevPending ? 4 : d;
}

/* True during one 60 Hz refresh at the start of each 1 / hz period of WindowServer's clock (stateless: a period may
   see zero or two tick frames). hz = 0 or above 60 (FP_RESIZE_FREEZE): never. */
static inline int fpTick(double now, uint32_t hz) {
    if (hz - 1 >= 60 || !(now > 0)) return 0;
    double x = now * hz;
    return x - (double)(long long)x < hz / 60.0;
}

/* The window (id `wid`) is the one being dragged: SkyLight keeps the dragged window's id in its session package data
   ([pkg+0x10c], set by _prepareToDragWindow, cleared on mouse up), 0 when there is no drag. */
static inline int fpDragged(uint32_t wid, uint32_t draggedId) {
    return draggedId != 0 && wid == draggedId;
}

/* The window is being live-resized in this update. */
static inline int fpResizing(uint32_t dirty, uint32_t resizeHz) {
    return resizeHz != 0 && (dirty & FP_DIRTY_GEOMETRY) != 0;
}

/*
 * Motion tracking, for whatever moves windows without a mode flag (Show Desktop: the Dock changes the window's
 * transform). The blob keeps the last translation of the aggregate transform of the windows it is asked about; a
 * window counts as moving from the update in which the translation changed until FM_HOLD seconds without a change.
 */
#define FM_MAGIC 0x464d5431u        /* "FMT1" */
#define FM_MAX 64
#define FM_HOLD 0.15                /* s: bridges pauses between position changes */
#define FM_GAP 0.6                  /* s: not asked about for this long -> the stored origin is only a new baseline */
#define FM_FORGET 5.0               /* s: a slot not asked about for this long may be reused when the table is full */

typedef struct {
    uintptr_t rec;                  /* backing record, compared only */
    double x, y, movedAt, seenAt;
} FmWin;

typedef struct {
    uint32_t magic, n;
    FmWin w[FM_MAX];
} FmTable;

static inline void fmInit(FmTable *t) {
    uint8_t *p = (uint8_t *)t;
    for (unsigned long i = 0; i < sizeof(*t); i++) p[i] = 0;
    t->magic = FM_MAGIC;
}

/* Records the window's origin at time `now` and tells whether it is moving. */
static inline int fmUpdate(FmTable *t, uintptr_t rec, double x, double y, double now) {
    FmWin *w = 0, *oldest = 0;
    for (uint32_t i = 0; i < t->n && i < FM_MAX; i++) {
        if (t->w[i].rec == rec) { w = &t->w[i]; break; }
        if (!oldest || t->w[i].seenAt < oldest->seenAt) oldest = &t->w[i];
    }
    if (!w) {
        if (t->n < FM_MAX) w = &t->w[t->n++];
        else if (oldest && now - oldest->seenAt > FM_FORGET) w = oldest;
        else return 0;
        w->rec = rec; w->x = x; w->y = y; w->movedAt = 0; w->seenAt = now;
        return 0;
    }
    int fresh = now - w->seenAt <= FM_GAP;
    if (w->x != x || w->y != y) {
        w->x = x; w->y = y;
        w->movedAt = fresh ? now : 0;
    }
    w->seenAt = now;
    return w->movedAt > 0 && now - w->movedAt < FM_HOLD;
}

#endif /* flat_policy_h */
