#!/bin/zsh
# Read-only: package power and limit reason under 1-core vs 6-core load.
# Usage: sudo ./measure-cpu-limit2.sh   -> writes cpu-limit2.txt next to this script
cd "${0:A:h}"
OUT=cpu-limit2.txt; : > $OUT
for n in 1 6; do
  echo "######## $n-core load" >> $OUT
  for i in $(seq $n); do ( ./cpu-clock-test; ./cpu-clock-test ) > clk.$i & done
  sleep 1
  powermetrics --samplers cpu_power --show-plimits --show-extra-power-info -i 1000 -n 4 >> $OUT 2>&1
  wait
  echo "== clock ($n-core)"; cat clk.* | sort -n | tr '\n' ' ' >> $OUT; echo >> $OUT
  rm -f clk.*
done
chown "${SUDO_USER:-$USER}" $OUT
echo "done -> $OUT"
