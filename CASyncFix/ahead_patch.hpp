//
//  ahead_patch.hpp
//  CASyncFix.kext
//
// Present every frame exactly two refreshes after its animation time (SkyLight, macOS 26.6 25G72).
//
// Every window's CoreAnimation time for an update is S = WSDisplayScheduleGetPrevNextVBL().next
// (ca_window_prep_ingestion_phase+1557..1612 on a non-VRR, non-APT display), the vblank the update was scheduled for.
// CDDisplay::present_update asks CoreDisplay for the first vblank at or after S - 0.5 ms; glass - S then varies between
// 0 and 1 refresh (timer vs app-commit updates), which moves things on screen by a refresh while frames are on time.
// Both present_update branches now call the freeze blob, which returns S + 2 refreshes - 0.5 ms
// (fz_present_target, freeze_blob/blob.c); the probe build also logs S, now and the target for every present.
//
//   present: CDDisplay::present_update+374 (vsync branch, xmm0 = S)
//            cvtss2sd cgdebug::updater_wakeup_delay(%rip), %xmm1 ; subsd %xmm1, %xmm0
//         -> callq fz_present374_thunk ; nopl 0(%rax)  (7 bytes)
//   now:     CDDisplay::present_update+388 (other branch): callq SLSCurrentRealTime -> callq fz_present388_thunk
//            (S of this display: r14 = this, callee-saved from +23 on).
//   Both branches merge at +393 after a call on one side, so no caller-saved register is live across the change, and
//   both are 16-byte aligned call sites.
//
// The blob is only copied with the freeze patch (casyncfreeze != 0); the kext enables these sites only then, and only
// on the file page layout whose cave was verified free (fzFileOff), like the freeze sites.
//
// History: 0.5.0 added 1 refresh to S (no effect on timer frames); 0.5.2 used present call time + 1 refresh (untied
// from S, Music got worse); 0.6.0 added 2 refreshes to S with hand-written thunks; 0.6.1 moves the math into the blob.
// ponytail: the refresh is fixed at 60 Hz (this panel); WSDisplayScheduleGetPrevNextVBL's interval is the upgrade path.
//

#ifndef ahead_patch_hpp
#define ahead_patch_hpp

#include "freeze_patch.hpp"

// 14 bytes: stops before the callq at +388 so either site can be patched first.
static constexpr uint8_t ahPresentFind[] = {0xF3, 0x0F, 0x5A, 0x0D, 0x0C, 0xCE, 0x6E, 0x3A,
                                            0xF2, 0x0F, 0x5C, 0xC1, 0xEB, 0x05};
static constexpr FzSite ahPresent {0x7ff80950f854ULL, 0x854, ahPresentFind, sizeof(ahPresentFind)};
static constexpr size_t ahPresentLen = 12;

static constexpr uint8_t ahNowFind[] = {0xE8, 0x9C, 0x27, 0xD8, 0xFF, 0x48, 0x83, 0x3D,
                                        0x71, 0xE3, 0x2F, 0x3A, 0xFF, 0x0F, 0x85, 0x19};
static constexpr FzSite ahNow {0x7ff80950f862ULL, 0x862, ahNowFind, sizeof(ahNowFind)};
static constexpr uint64_t ahNextVBL = 0x7ff80949e75fULL;   // WSDisplayScheduleGetNextVBL (used by the blob)

static inline bool ahPatchPresent(uint8_t *page, size_t n, uint64_t fileOff) {
    if (fileOff != fzFileOff(ahPresent) || !fzAt(page, n, ahPresent)) return false;
    uint8_t *p = page + ahPresent.pageOff;
    p[0] = 0xE8;                                                           // callq fz_present374_thunk
    fzPut32(p + 1, (int64_t)(fzCaveStart + fzOffPresent374) - (int64_t)(ahPresent.staticAddr + 5));
    p[5] = 0x0F; p[6] = 0x1F; p[7] = 0x80; p[8] = p[9] = p[10] = p[11] = 0;  // nopl 0(%rax) (7 bytes)
    return true;
}

static inline bool ahPatchNow(uint8_t *page, size_t n, uint64_t fileOff) {
    if (fileOff != fzFileOff(ahNow) || !fzAt(page, n, ahNow)) return false;
    fzPut32(page + ahNow.pageOff + 1, (int64_t)(fzCaveStart + fzOffPresent388) - (int64_t)(ahNow.staticAddr + 5));
    return true;
}

#endif /* ahead_patch_hpp */
