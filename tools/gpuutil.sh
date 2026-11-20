#!/bin/zsh
# Samples the Intel GPU utilization the driver publishes in ioreg (read only), every ~100 ms for S seconds.
# Usage: ./tools/gpuutil.sh [seconds=12] > out.txt      columns: ms  util%  util%_at_cur_pstate
S=${1:-12}; end=$(( $(date +%s) + S ))
while (( $(date +%s) < end )); do
  v=$(ioreg -r -c IntelAccelerator -d 1 -w0 | grep -oE '"Device Utilization %( at cur p-state)?"=[0-9]+' | cut -d= -f2 | tr '\n' ' ')
  print -- "$(( $(perl -MTime::HiRes=time -e 'printf "%d", time*1000') )) $v"
  sleep 0.1
done
