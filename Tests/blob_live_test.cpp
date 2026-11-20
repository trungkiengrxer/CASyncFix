// Live test of the forced-flatten blob: copies the exact bytes the kext would write (fzPatchCave) into this process's
// private copy of SkyLight's cave page, at the blob's real address, and calls fz_force there. Unlike force_test.c this
// runs the shipped machine code with the real externals: __sessionControlRef (pointed at a fake session for the
// test), FlatteningPolicyData::change_detected, mach_vm_region / allocate.
// It proves the anchor-relative addresses, the -Oz code and its stack alignment; it does not prove WindowServer's
// structure offsets (those come from the disassembly) nor the thunk that jumps back into SkyLight.
// Only this process is touched: the page is made copy-on-write first.
//
// Run from the repository root (after ./build.sh or CASyncFix/freeze_blob/build_blob.sh):
//   clang++ -std=c++17 Tests/blob_live_test.cpp -F/System/Library/PrivateFrameworks -framework SkyLight \
//           -framework CoreGraphics -o /tmp/blob_live && /tmp/blob_live

#include "../CASyncFix/freeze_patch.hpp"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

// int fz_test_enter_thunk(void *thunk, uintptr_t rec, void *rbp): sets r14 / rbp like the host function, reaches the
// thunk with rsp 16-aligned at its first instruction (a call from rsp = 8 mod 16 stands in for the host's jmp), and
// returns 1 when the flags at the resume point say "not equal".
extern "C" int fz_test_enter_thunk(void *thunk, uintptr_t rec, void *rbp);
__asm__(
    ".text\n"
    ".globl _fz_test_enter_thunk\n"
    "_fz_test_enter_thunk:\n"
    "    pushq %rbp\n"
    "    pushq %r14\n"
    "    pushq %rbx\n"
    "    movq  %rsp, %rbx\n"
    "    movq  %rsi, %r14\n"
    "    movq  %rdx, %rbp\n"
    "    andq  $-16, %rsp\n"
    "    subq  $8, %rsp\n"
    "    callq *%rdi\n"
    "    setne %al\n"
    "    movzbl %al, %eax\n"
    "    movq  %rbx, %rsp\n"
    "    popq  %rbx\n"
    "    popq  %r14\n"
    "    popq  %rbp\n"
    "    retq\n");

static int failures = 0;
static void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static uint64_t recBuf[0x100], winBuf[0x120], scBuf[8], aBuf[0x20], exposeBuf[0x20], pkgBuf[0x80];
#define REC ((uintptr_t)recBuf)
#define WIN ((uintptr_t)winBuf)
template <typename T> static T &at(void *p, size_t off) { return *(T *)((uintptr_t)p + off); }

static void (*force)(uintptr_t, uintptr_t);
static double now = 7000.0;
static void moveTo(double x, double y) { at<double>(winBuf, 0x4f8) = x; at<double>(winBuf, 0x500) = y; }
static void reset() {
    memset(recBuf, 0, sizeof recBuf); memset(winBuf, 0, sizeof winBuf);
    memset(exposeBuf, 0, sizeof exposeBuf); memset(pkgBuf, 0, sizeof pkgBuf);
    at<uint32_t>(recBuf, 0) = 0x23;
    moveTo(361, 312);
    now += 100;
}
// change_detected compares the current snapshot with the previous one; +0xbc / +0x2c of the policy data (rec+0x660)
// is the context change seed.
static void setChanged(bool changed) { at<uint32_t>(recBuf, 0x660 + 0xbc) = changed ? 7 : 0; at<uint32_t>(recBuf, 0x660 + 0x2c) = 0; }
static uint8_t update(uint8_t d, uint32_t reason) {
    at<uint8_t>(recBuf, 0x700) = d;
    at<uint32_t>(recBuf, 0x704) = reason;
    at<double>(recBuf, 0x710) = now;
    force(REC, WIN);
    now += 1.0 / 60;
    uint8_t out = at<uint8_t>(recBuf, 0x700);
    uint32_t seedCur = at<uint32_t>(recBuf, 0x660 + 0xbc), seedPrev = at<uint32_t>(recBuf, 0x660 + 0x2c);
    memcpy((void *)(REC + 0x670), (void *)(REC + 0x700), 0x90);     // SkyLight: current snapshot -> previous
    at<uint32_t>(recBuf, 0x660 + 0xbc) = seedCur;                   // keep the test's own "changed" setting
    at<uint32_t>(recBuf, 0x660 + 0x2c) = seedPrev;
    return out;
}

int main() {
    dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_NOW);
    intptr_t slide = 0;
    bool found = false;
    for (uint32_t i = 0; i < _dyld_image_count(); i++)
        if (strstr(_dyld_get_image_name(i), "/SkyLight.framework/")) { slide = _dyld_get_image_vmaddr_slide(i); found = true; }
    if (!found) { printf("SKIP SkyLight not loaded\n"); return 0; }

    // The page exactly as the kext would leave it: pristine cave (zeros), then fzPatchCave.
    const size_t caveOff = fzCaveStart & 0xfff;
    uint8_t *page = (uint8_t *)((fzCaveStart & ~0xfffULL) + slide);
    uint8_t buf[4096];
    memcpy(buf, page, sizeof buf);
    memset(buf + caveOff, 0, sizeof buf - caveOff);
    bool patched = fzPatchCave(buf, sizeof buf, fzFileOff(fzCave), 0, 6000000, FP_REFRESH_HZ_DEFAULT, 15);   // casyncresizehz=15
    check(patched, "the cave page of this machine's SkyLight accepts the blob");
    if (!patched) return 1;
    kern_return_t kr = mach_vm_protect(mach_task_self(), (mach_vm_address_t)page, 4096, false,
                                       VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    check(kr == KERN_SUCCESS, "private writable copy of the cave page");
    if (kr != KERN_SUCCESS) return 1;
    memcpy(page + caveOff, buf + caveOff, sizeof buf - caveOff);
    kr = mach_vm_protect(mach_task_self(), (mach_vm_address_t)page, 4096, false, VM_PROT_READ | VM_PROT_EXECUTE);
    check(kr == KERN_SUCCESS && memcmp(page + caveOff, fzBlob, 16) == 0, "blob bytes at their real address, executable again");
    if (kr != KERN_SUCCESS) return 1;
    force = (void (*)(uintptr_t, uintptr_t))(page + caveOff + fzOffForce);

    // Point SkyLight's __sessionControlRef (null outside WindowServer) at a fake session for the duration of the test.
    uintptr_t *sessionRef = (uintptr_t *)(fzSessionControlRef + slide);
    uintptr_t savedSession = *sessionRef;
    at<void *>(scBuf, 0x20) = aBuf;
    at<void *>(aBuf, 0x90) = exposeBuf;
    at<void *>(aBuf, 0xd8) = pkgBuf;
    *sessionRef = (uintptr_t)scBuf;

    const uint32_t WSA = FP_REASON_WSA_BACKDROP, EXPOSE = FP_REASON_EXPOSE, CHANGE = FP_REASON_CHANGE;

    reset();
    check(update(1, WSA) == 1 && update(2, CHANGE) == 2, "idle desktop: decisions untouched (real change_detected, real origin lookup)");

    reset();
    at<uintptr_t>(exposeBuf, 0xe0) = 1;
    check(update(1, WSA) == 3 && update(1, WSA) == 3, "Mission Control flag: quiet glass window flattened and reused");
    setChanged(true);
    check(update(1, WSA) == 3 && update(1, WSA) == 3, "real change_detected reports a change: held back");
    setChanged(false);
    check(update(1, WSA) == 3 && update(1, WSA) == 3, "content quiet again: still drawn from the old image");
    check(update(4, EXPOSE) == 3, "reflatten decision held back");
    at<uintptr_t>(exposeBuf, 0xe0) = 0;
    check(update(3, EXPOSE) == 4 && update(3, EXPOSE) == 3, "after the animation: stale image refreshed once");
    at<int32_t>(winBuf, 0x20) = 24;
    at<uintptr_t>(exposeBuf, 0xe0) = 1;
    check(update(1, WSA) == 1, "non-zero window level untouched");

    reset();
    at<uintptr_t>(winBuf, 0x878) = 0x1234;
    check(update(1, WSA) == 3, "mesh warp (Genie): forced");

    reset();
    at<uintptr_t>(pkgBuf, 0x3e0) = 1;
    check(update(1, WSA) == 3, "space swipe: forced");

    reset();
    at<uint32_t>(pkgBuf, 0x10c) = 0x23;
    check(update(1, WSA) == 3 && update(1, WSA) == 3, "dragged window: forced");
    at<uint32_t>(pkgBuf, 0x10c) = 0x42;
    check(update(1, WSA) == 1, "another window dragged: untouched");

    reset();    // Show Desktop: the transform's translation changes, real table allocation
    at<uint32_t>(pkgBuf, 0x10c) = 0;
    check(update(1, WSA) == 1, "moving window, first update: baseline");
    int forced = 0;
    for (int i = 1; i <= 20; i++) { moveTo(361 - 40 * i, 312 + 30 * i); forced += update(1, WSA) == 3; }
    check(forced == 20, "moving window: forced in every update in which its transform moved it");
    now += 0.3;
    check(update(1, WSA) == 1, "moving window at rest: decision back");
    mach_vm_address_t addr = 0x500100000000ULL;
    mach_vm_size_t size = 0;
    vm_region_extended_info_data_t info;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_port_t object = MACH_PORT_NULL;
    kr = mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info, &count, &object);
    check(kr == KERN_SUCCESS && addr == 0x500100000000ULL && info.user_tag == 242 && *(uint32_t *)addr == FM_MAGIC,
          "the blob allocated its motion table at 0x500100000000 with its own tag");

    reset();    // live resize
    int r3 = 0, r4 = 0;
    for (int i = 0; i < 120; i++) {
        at<uint32_t>(recBuf, 0xc8) = 0x8;
        uint8_t d = update(2, CHANGE);
        r3 += d == 3; r4 += d == 4;
    }
    check(r3 + r4 == 120 && r4 >= 20 && r4 <= 40, "live resize: about 15 re-renders per second, reuse in between");

    force(0, WIN); force(REC, 0);
    check(true, "null record or window: no crash");

    // The thunk itself, entered the way SkyLight enters it: r14 = record, [rbp-0x318] = window, rsp 16-aligned, by
    // jmp. It must call fz_force, re-execute `cmpq $-1, once(%rip)` and jmp to +9554; in this process's private copy
    // +9554 is a `ret`, so the jmp returns here with the cmpq's flags.
    uint8_t *resume = (uint8_t *)(fzFlatResume + slide);
    kr = mach_vm_protect(mach_task_self(), (mach_vm_address_t)resume & ~0xfffULL, 4096, false,
                         VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr == KERN_SUCCESS) {
        resume[0] = 0xC3;
        kr = mach_vm_protect(mach_task_self(), (mach_vm_address_t)resume & ~0xfffULL, 4096, false, VM_PROT_READ | VM_PROT_EXECUTE);
    }
    check(kr == KERN_SUCCESS, "private copy of the page holding +9554");
    if (kr == KERN_SUCCESS) {
        static uint64_t frame[0x100];
        void *rbp = &frame[0x80];
        at<uintptr_t>(rbp, -0x318) = WIN;
        reset();
        at<uintptr_t>(winBuf, 0x878) = 0x1234;                      // mesh warp: fz_force must rewrite 1 -> 3
        at<uint8_t>(recBuf, 0x700) = 1;
        at<uint32_t>(recBuf, 0x704) = WSA;
        at<double>(recBuf, 0x710) = now;
        int notEqual = fz_test_enter_thunk(page + caveOff + fzOffFlatThunk, REC, rbp);
        bool onceIsMinus1 = *(int64_t *)(fzFlatOnce + slide) == -1;
        check(at<uint8_t>(recBuf, 0x700) == 3, "thunk: fz_force ran with r14 and [rbp-0x318]");
        check((notEqual != 0) == !onceIsMinus1, "thunk: flags of the re-executed cmpq reach the resume point");
    }

    *sessionRef = savedSession;
    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
