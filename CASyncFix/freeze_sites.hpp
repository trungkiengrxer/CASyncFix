//
//  freeze_sites.hpp
//  CASyncFix.kext
//
// Patch sites of the window-freeze feature in SkyLight, macOS 26.6 25G72 (static, unslid shared-cache addresses).
// The whole shared cache slides as one, so rip-relative distances between these sites are fixed.
//
//   call A / call B: callq WS::Updater::defenestrator3::ca_prepare_begin_window_update (0x7ff809335b16)
//                    inside WS::Updater::prepare_coreanimation -> redirected to the blob's timing wrapper
//   apply:           cmpl $0x0, 0xc8(%r14) at ca_window_prep_ingestion_phase+11144: SkyLight skips every property
//                    setter (setShouldFlatten:, setShouldReflatten:, setFlattenMode:, ...) when the dirty word is 0,
//                    and it dominates every later read of it -> jmp to thunk (8 bytes: jmp + 3 nops)
//   cave:            2864 zero bytes after SkyLight.__TEXT (end of __objc_methtype), executable, owned by no image
//

#ifndef freeze_sites_hpp
#define freeze_sites_hpp

#include <stddef.h>
#include <stdint.h>

struct FzSite {
    uint64_t staticAddr;   // address of the first pattern byte
    uint32_t pageOff;      // staticAddr & 0xfff
    const uint8_t *find;
    size_t len;
};

static constexpr uint64_t fzCacheBase = 0x7ff800000000ULL;   // static address of main cache file offset 0

static constexpr uint8_t fzCallAFind[] = {0xE8, 0xD9, 0x46, 0x01, 0x00, 0x84, 0xC0, 0x74,
                                          0x7A, 0x49, 0x8B, 0x7E, 0x30, 0x48, 0x8B, 0xB3};
static constexpr uint8_t fzCallBFind[] = {0xE8, 0x33, 0x42, 0x01, 0x00, 0x84, 0xC0, 0x0F,
                                          0x84, 0xD5, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x7D};
static constexpr uint8_t fzApplyFind[] = {0x41, 0x83, 0xBE, 0xC8, 0x00, 0x00, 0x00, 0x00,
                                          0x4C, 0x8B, 0xAD, 0xE8, 0xFC, 0xFF, 0xFF, 0x0F};
static constexpr uint8_t fzCaveFind[] = {0x30, 0x3A, 0x38, 0x7B, 0x3F, 0x3D, 0x49, 0x5B,
                                         0x38, 0x49, 0x5D, 0x49, 0x7D, 0x31, 0x36, 0x00};

static constexpr FzSite fzCallA {0x7ff809321438ULL, 0x438, fzCallAFind, sizeof(fzCallAFind)};
static constexpr FzSite fzCallB {0x7ff8093218deULL, 0x8de, fzCallBFind, sizeof(fzCallBFind)};
static constexpr FzSite fzApply {0x7ff809333a84ULL, 0xa84, fzApplyFind, sizeof(fzApplyFind)};
static constexpr FzSite fzCave  {0x7ff8097584b5ULL, 0x4b5, fzCaveFind, sizeof(fzCaveFind)};

// flat: ca_window_prep_ingestion_phase+9546, where every per-window flatten decision ([rec+0x700]) converges:
//   cmpq $-1, UseCAWindowLayerFlattening()::once(%rip) (8 bytes) -> jmp fz_flat_thunk + 3 nops; jne +19000 follows.
// The thunk lets the blob rewrite the decision (flat_policy.h), re-executes the cmpq and resumes at the jne.
// (0.7.0 / 0.7.1-0.7.2 patched single exclusion blocks instead: glass-atop-translucence +8603, wsa-backdrop +8537.)
static constexpr uint8_t fzFlatFind[] = {0x48, 0x83, 0x3D, 0x9A, 0xFF, 0x8C, 0x3A, 0xFF,
                                         0x0F, 0x85, 0xE0, 0x24, 0x00, 0x00, 0x48, 0x8B};
static constexpr FzSite fzFlat {0x7ff809333446ULL, 0x446, fzFlatFind, sizeof(fzFlatFind)};
static constexpr size_t   fzFlatLen    = 8;
static constexpr uint64_t fzFlatResume = 0x7ff80933344eULL;         // +9554: the jne after the replaced cmpq
static constexpr uint64_t fzFlatOnce   = 0x7ff843c033e8ULL;         // UseCAWindowLayerFlattening()::once
static constexpr uint64_t fzSessionControlRef = 0x7ff843c013d8ULL;  // __sessionControlRef (read by the blob)
static constexpr uint64_t fzChangeDetected = 0x7ff809275174ULL;     // FlatteningPolicyData::change_detected (called by the blob)
static constexpr uint32_t fzWindowLevelOff = 0x20;                  // CGXWindow level (WSWindowGetLevel)
static constexpr uint32_t fzWindowMeshOff  = 0x878;                 // CGXWindow mesh warp, non-null while warped (set_mesh_warp)
static constexpr uint32_t fzWindowMoveOff  = 0x4f8;                 // CGXWindow aggregate transform (+0x498), m41 / m42
static constexpr uint32_t fzDraggedWindowOff = 0x10c;               // [[[sc+0x20]+0xd8]+0x10c]: id of the dragged window

static constexpr uint64_t fzPrepareCallee = 0x7ff809335b16ULL;   // ca_prepare_begin_window_update
static constexpr uint64_t fzApplyResume   = 0x7ff809333a8cULL;   // instruction after the replaced 8-byte cmpl
static constexpr size_t   fzApplyLen      = 8;
static constexpr uint64_t fzCaveStart     = 0x7ff8097584d0ULL;   // 16-byte aligned start of usable cave
static constexpr size_t   fzCaveSize      = 0x7ff809759000ULL - 0x7ff8097584d0ULL;   // 2864

#endif /* freeze_sites_hpp */
