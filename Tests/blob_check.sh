#!/bin/zsh
# Static checks of the window-freeze blob (CASyncFix/freeze_blob). Run from the repository root:
#   ./Tests/blob_check.sh
set -uo pipefail
cd "${0:A:h}/.."
fail=0
check() { if eval "$2"; then echo "PASS $1"; else echo "FAIL $1"; fail=$((fail + 1)); fi; }

./CASyncFix/freeze_blob/build_blob.sh >/dev/null || { echo "FAIL build"; exit 1; }
OUT=${TMPDIR:-/tmp}/casync-freeze-blob
INC=CASyncFix/freeze_blob.inc
val() { awk -v k="$1" '$4==k {gsub(";","",$6); print $6}' $INC; }
LEN=$(val fzBlobLen); ANCHOR=$(val fzOffAnchor); RESUME=$(val fzOffResumeJmp); CONFIG=$(val fzOffConfig)
FIRST=$(( 16#$(nm $OUT/blob.out | awk '$3=="_fz_anchor" {print $1}') ))

check "blob fits the cave (<= 2864 bytes, got $LEN)" "(( LEN <= 2864 ))"
check "fz_anchor is byte 0" "(( ANCHOR == 0 ))"
check "no undefined symbols" "[[ -z \"\$(nm -u $OUT/blob.out)\" ]]"
check "resume jmp placeholder is E9 00000000" "[[ \$(xxd -s \$((RESUME)) -l 5 -p $OUT/blob.bin) == e900000000 ]]"
check "config magic FCFG, mode 2, budget 6 ms" \
  "[[ \$(xxd -s \$((CONFIG)) -l 16 -p $OUT/blob.bin) == 4746434602000000808d5b0000000000 ]]"

# The thunk re-executes the replaced instruction `cmpl $0x0, 0xc8(%r14)` (41 83 BE C8 00 00 00 00) before resuming.
check "thunk re-executes the replaced cmpl right before the resume jmp" \
  "[[ \$(xxd -s \$((RESUME - 8)) -l 8 -p $OUT/blob.bin) == 4183bec800000000 ]]"

# The config must be read from memory at run time (the kext rewrites it), never folded into immediates.
check "config not folded into code (no 6000000 / 0x2465a5431 immediates)" \
  "! otool -tv $OUT/blob.out | grep -qE '0x5b8d80|0x2465a5431'"

# Every direct call/jmp must land inside the blob (externals are reached only through fz_anchor-relative leas).
DIS=$(otool -tv $OUT/blob.out)
out_of_blob=$(echo "$DIS" | awk -v lo=$FIRST -v hi=$((FIRST + LEN)) '
  $2 ~ /^(callq|jmp|j[a-z]+)$/ && $3 ~ /^0x/ { t = strtonum($3); if (t < lo || t > hi) print }' 2>/dev/null)
if [[ -z "$out_of_blob" ]]; then
  out_of_blob=$(echo "$DIS" | while read -r a op t rest; do
    [[ $op == (callq|jmp|j[a-z]##) && $t == 0x* ]] || continue
    (( $t < FIRST || $t > FIRST + LEN )) && echo "$a $op $t"
  done)
fi
check "no direct call/jmp outside the blob" "[[ -z \"$out_of_blob\" ]]"

# Each external static address S must appear as a rip-relative target FIRST + (S - 0x7ff8097584d0).
# otool prints rip displacements, so resolve them: target = address of the next instruction + displacement.
RIP_TARGETS=$(echo "$DIS" | awk '/^[0-9a-f]+\t/ {print $1, $0}' | while read -r addr line; do
  if [[ -n ${pending:-} ]]; then printf "0x%x\n" $(( 16#$addr + pending )); pending=; fi
  if [[ $line =~ '(-?0x[0-9a-f]+)\(%rip\)' ]]; then pending=$(( ${match[1]} )); fi
done)
externals=("mach_vm_region 0x7ff802e655b6" "mach_vm_allocate 0x7ff802e63706" "mach_absolute_time 0x7ff802e61ec7"
           "mach_task_self_ 0x7ff8437c1e58" "kCAWindowLayerFlattenModeAlways 0x7ff840d30528"
           "ca_prepare_begin_window_update 0x7ff809335b16" "WSDisplayScheduleGetNextVBL 0x7ff80949e75f"
           "__sessionControlRef 0x7ff843c013d8" "FlatteningPolicyData::change_detected 0x7ff809275174"
           "UseCAWindowLayerFlattening_once 0x7ff843c033e8")
# The probe build (FZ_PROBE=1) also logs through SkyLight's own os_log path.
[[ ${FZ_PROBE:-0} == 1 ]] && externals+=("SLSCurrentRealTime 0x7ff809292003" "_os_log_impl_stub 0x7ff809683a4c"
  "SkyLight_mach_header 0x7ff8091fd000" "default_log 0x7ff84380d9e0" "solver_format 0x7ff80970dfa0")
for pair in $externals; do
  set -- ${=pair}
  target=$(printf "0x%x" $(( FIRST + $2 - 0x7ff8097584d0 )))
  delta=$(printf "0x%x" $(( $2 - 0x7ff8097584d0 )))
  # Either a resolved rip-relative target, or a displacement from the register holding fz_anchor
  # (the compiler loads fz_anchor once via `leaq <to anchor>(%rip)` and reuses it).
  check "external $1 reached anchor-relative ($target)" \
    "echo \"\$RIP_TARGETS\" | grep -qx \"$target\" || echo \"\$DIS\" | grep -q \"$delta(%r\""
done

# Present thunks (called from CDDisplay::present_update+374/+388, 16-byte aligned call sites, caller-saved registers
# dead): sub $8,%rsp; <arg setup>; callq <C function>; add $8,%rsp; ret
P374=$(val fzOffPresent374); P388=$(val fzOffPresent388)
check "present374 thunk: sub rsp; mov \$374,%edi; callq; add rsp; ret" \
  "[[ \$(xxd -s \$((P374)) -l 9 -p $OUT/blob.bin) == 4883ec08bf76010000 && \$(xxd -s \$((P374 + 9)) -l 1 -p $OUT/blob.bin) == e8 && \$(xxd -s \$((P374 + 14)) -l 5 -p $OUT/blob.bin) == 4883c408c3 ]]"
check "present388 thunk: sub rsp; mov %r14,%rdi; callq; add rsp; ret" \
  "[[ \$(xxd -s \$((P388)) -l 7 -p $OUT/blob.bin) == 4883ec084c89f7 && \$(xxd -s \$((P388 + 7)) -l 1 -p $OUT/blob.bin) == e8 && \$(xxd -s \$((P388 + 12)) -l 5 -p $OUT/blob.bin) == 4883c408c3 ]]"
# Flat thunk (jmp target of ca_window_prep_ingestion_phase+9546): mov %r14,%rdi (rec); mov -0x318(%rbp),%rsi (window);
# callq fz_force; then the replaced `cmpq $-1, once(%rip)` (48 83 3D disp32 FF) right before the E9 resume placeholder.
FT=$(val fzOffFlatThunk); FR=$(val fzOffFlatResumeJmp)
check "flat thunk: rec and window as arguments, then a call" \
  "[[ \$(xxd -s \$((FT)) -l 11 -p $OUT/blob.bin) == 4c89f7488bb5e8fcffffe8 ]]"
check "flat thunk re-executes the replaced cmpq right before the resume jmp" \
  "[[ \$(xxd -s \$((FR - 8)) -l 3 -p $OUT/blob.bin) == 48833d && \$(xxd -s \$((FR - 1)) -l 1 -p $OUT/blob.bin) == ff ]] && (( FR == FT + 23 ))"
check "flat resume jmp placeholder is E9 00000000" "[[ \$(xxd -s \$((FR)) -l 5 -p $OUT/blob.bin) == e900000000 ]]"
check "production build does not log (FZ_PROBE=0 has no _os_log_impl call)" \
  "[[ \${FZ_PROBE:-0} == 1 ]] || ! echo \"\$RIP_TARGETS\" | grep -qx \$(printf '0x%x' \$(( FIRST + 0x7ff809683a4c - 0x7ff8097584d0 )))"

echo
(( fail == 0 )) && echo "ALL PASS" || echo "$fail FAILED"
exit $(( fail != 0 ))
