# thunk.s: first object of the window-freeze blob. fz_anchor is byte 0 of the blob (static 0x7ff8097584d0).
#
# fz_apply_thunk replaces `cmpl $0x0, 0xc8(%r14)` at ca_window_prep_ingestion_phase+11144 (via `jmp` + 3 nops).
# It is not a call boundary, so every register, the flags and the red zone of the host function are preserved.
# It ends with `jmp rel32` to the instruction after the replaced one; the kext writes that rel32 (fz_resume_jmp+1)
# from static addresses, which is slide-invariant.

    .text
    .private_extern _fz_anchor
    .globl _fz_anchor
_fz_anchor:

    .globl _fz_apply_thunk
    .p2align 4
_fz_apply_thunk:
    leaq    -128(%rsp), %rsp            # skip the host red zone
    pushfq
    pushq   %rax
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %rbx
    movq    %rsp, %rbx                  # rbx: callee-saved, keeps the pre-alignment rsp
    subq    $256, %rsp
    andq    $-16, %rsp
    movdqu  %xmm0, 0(%rsp)
    movdqu  %xmm1, 16(%rsp)
    movdqu  %xmm2, 32(%rsp)
    movdqu  %xmm3, 48(%rsp)
    movdqu  %xmm4, 64(%rsp)
    movdqu  %xmm5, 80(%rsp)
    movdqu  %xmm6, 96(%rsp)
    movdqu  %xmm7, 112(%rsp)
    movdqu  %xmm8, 128(%rsp)
    movdqu  %xmm9, 144(%rsp)
    movdqu  %xmm10, 160(%rsp)
    movdqu  %xmm11, 176(%rsp)
    movdqu  %xmm12, 192(%rsp)
    movdqu  %xmm13, 208(%rsp)
    movdqu  %xmm14, 224(%rsp)
    movdqu  %xmm15, 240(%rsp)
    movq    %r14, %rdi
    callq   _fz_apply_rec
    movdqu  0(%rsp), %xmm0
    movdqu  16(%rsp), %xmm1
    movdqu  32(%rsp), %xmm2
    movdqu  48(%rsp), %xmm3
    movdqu  64(%rsp), %xmm4
    movdqu  80(%rsp), %xmm5
    movdqu  96(%rsp), %xmm6
    movdqu  112(%rsp), %xmm7
    movdqu  128(%rsp), %xmm8
    movdqu  144(%rsp), %xmm9
    movdqu  160(%rsp), %xmm10
    movdqu  176(%rsp), %xmm11
    movdqu  192(%rsp), %xmm12
    movdqu  208(%rsp), %xmm13
    movdqu  224(%rsp), %xmm14
    movdqu  240(%rsp), %xmm15
    movq    %rbx, %rsp
    popq    %rbx
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rax
    popfq
    leaq    128(%rsp), %rsp
    cmpl    $0x0, 0xc8(%r14)            # the replaced instruction; its flags feed the host's next jcc
    .globl _fz_resume_jmp
_fz_resume_jmp:
    .byte   0xE9, 0, 0, 0, 0            # jmp rel32 to ca_window_prep_ingestion_phase+11152, filled in by the kext

# Present-time thunks, reached by `callq` from CDDisplay::present_update (see ../ahead_patch.hpp). Both sites are
# 16-byte aligned call sites inside the host function body with no live caller-saved register, so these are plain
# SysV calls: realign by 8 for the inner call, return the present time in xmm0.

    .globl _fz_present374_thunk         # no alignment padding on these thunks: the cave has 2864 bytes
_fz_present374_thunk:                   # +374: xmm0 = S
    subq    $8, %rsp
    movl    $374, %edi
    callq   _fz_present_target
    addq    $8, %rsp
    retq

    .globl _fz_present388_thunk
_fz_present388_thunk:                   # +388: r14 = CDDisplay this (callee-saved in present_update)
    subq    $8, %rsp
    movq    %r14, %rdi
    callq   _fz_present388
    addq    $8, %rsp
    retq

# fz_flat_thunk replaces `cmpq $-1, UseCAWindowLayerFlattening()::once(%rip)` (8 bytes, via `jmp` + 3 nops) at
# ca_window_prep_ingestion_phase+9546, the point every per-window flatten decision ([rec+0x700], reason [rec+0x704])
# jumps to before SkyLight turns it into CAWindowLayer flatten properties. fz_force (blob.c) may rewrite the
# decision: see ../flat_policy.h. (0.7.x patched single exclusion blocks, +8603 then +8537, instead.)
# Some paths reach +9546 straight after a call in the host (e.g. +8248 -> +8252 -> jmp), so no caller-saved register
# is live here and a plain SysV call is safe; r14 and rbp are callee-saved. The resume jmp is a `jmp rel32`
# placeholder written by the kext from static addresses (slide-invariant).

    .globl _fz_flat_thunk
_fz_flat_thunk:
    movq    %r14, %rdi                              # backing record
    movq    -0x318(%rbp), %rsi                      # CGXWindow (the host spills it there at +376)
    callq   _fz_force                               # rsp is 16-aligned here (host function body)
    cmpq    $-1, _fz_anchor + 0x3a4aaf18(%rip)      # the replaced instruction: UseCAWindowLayerFlattening()::once
                                                    # (static 0x7ff843c033e8); its flags feed the host's jne
    .globl _fz_flat_resume_jmp
_fz_flat_resume_jmp:
    .byte   0xE9, 0, 0, 0, 0                        # jmp +9554, filled in by the kext
