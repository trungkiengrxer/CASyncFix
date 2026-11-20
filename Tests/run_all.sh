#!/bin/zsh
# Runs every host test. Run from anywhere: ./Tests/run_all.sh
# Tests that read this machine's shared cache or call the live SkyLight run only on macOS 26.6 (25G72), the build the
# SkyLight patch sites were taken from. Everything else runs on any Intel Mac (the blob code uses x86 inline asm).
set -euo pipefail
cd "${0:A:h}/.."
OUT=${TMPDIR:-/tmp}/casync-tests; mkdir -p $OUT
fail=0
run() { local name=$1; shift; echo "== $name"; if "$@"; then :; else echo "FAILED: $name"; fail=$((fail + 1)); fi; }
cc_run() { local name=$1; shift; run $name zsh -c "$* -o $OUT/$name && $OUT/$name"; }

./CASyncFix/freeze_blob/build_blob.sh
run blob_check ./Tests/blob_check.sh
cc_run patch_test         clang++ -std=c++17 Tests/patch_test.cpp
cc_run freeze_patch_test  clang++ -std=c++17 Tests/freeze_patch_test.cpp
cc_run freeze_policy_test clang++ -std=c++17 Tests/freeze_policy_test.cpp
cc_run flat_policy_test   clang -std=c11 Tests/flat_policy_test.c
cc_run force_test         clang -std=c11 -DFZ_HOST_TEST -DFZ_TEST_FORCE -DFZ_TABLE_ADDR=0x580000000000ULL \
                                -DFM_TABLE_ADDR=0x580100000000ULL Tests/force_test.c
cc_run freeze_table_test  clang -std=c11 -DFZ_HOST_TEST -DFZ_TABLE_ADDR=0x580000000000ULL Tests/freeze_table_test.c
cc_run probe_test         clang -std=c11 -DFZ_HOST_TEST -DFZ_TEST_PRESENT -DFZ_PROBE=1 \
                                -DFZ_TABLE_ADDR=0x580000000000ULL Tests/probe_test.c
cc_run eet_test           clang++ -std=c++17 Tests/eet_test.cpp
cc_run therm_test         clang++ -std=c++17 Tests/therm_test.cpp

if [[ $(sw_vers -buildVersion) == 25G72 ]]; then
  # File reads go through the page cache, where a loaded CASyncFix has already patched the pages it checks.
  if kextstat -l -b com.trungkien.CASyncFix 2>/dev/null | grep -q CASyncFix; then
    echo "SKIP freeze_sites_test (needs the stock shared cache: boot once with -casyncoff)"
  else
    cc_run freeze_sites_test clang++ -std=c++17 -O2 Tests/freeze_sites_test.cpp
  fi
  cc_run ahead_patch_test  clang++ -std=c++17 Tests/ahead_patch_test.cpp
  cc_run blob_live_test    clang++ -std=c++17 Tests/blob_live_test.cpp -F/System/Library/PrivateFrameworks \
                                 -framework SkyLight -framework CoreGraphics
else
  echo "SKIP freeze_sites_test, ahead_patch_test, blob_live_test (need macOS 26.6 25G72)"
fi

echo; (( fail == 0 )) && echo "ALL TESTS PASSED" || { echo "$fail test(s) FAILED"; exit 1; }
