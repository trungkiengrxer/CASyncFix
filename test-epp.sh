#!/bin/zsh
# A/B test: does XCPM's EPP override lift the 1-core 3.7 GHz ceiling?
# Temporarily sets machdep.xcpm.epp_override, measures, then ALWAYS restores the original value.
# Usage: sudo ./test-epp.sh   -> writes test-epp.txt next to this script
cd "${0:A:h}"
OUT=test-epp.txt; : > $OUT
ORIG=$(sysctl -n machdep.xcpm.epp_override)
trap 'sysctl -w machdep.xcpm.epp_override=$ORIG >/dev/null; echo "restored epp_override=$(sysctl -n machdep.xcpm.epp_override)" >> $OUT' EXIT INT TERM

measure() {
  echo "######## $1 (epp_override=$(sysctl -n machdep.xcpm.epp_override))" >> $OUT
  ( ./cpu-clock-test; ./cpu-clock-test ) > clk.txt &
  sleep 1
  powermetrics --samplers cpu_power --show-extra-power-info -i 1000 -n 3 2>&1 \
    | grep -E 'System Average frequency|IA clipping|package power' >> $OUT
  wait
  echo "clock: $(sort -n clk.txt | tr '\n' ' ')" >> $OUT
  rm -f clk.txt
}

measure baseline
for v in 32 0; do
  sysctl -w machdep.xcpm.epp_override=$v >> $OUT 2>&1 || { echo "cannot set epp_override=$v" >> $OUT; continue; }
  sleep 1
  measure "override $v"
done
chown "${SUDO_USER:-$USER}" $OUT
echo "done -> $OUT"
