#!/bin/zsh
# Intel GPU utilization while the user swipes Mission Control: 10 s without load, then 10 s with tools/bgload.sh 4.
# Usage: ./tools/record-gpu.sh <tag>   -> traces/gpu-<tag>.txt (ms util util_at_cur_pstate), phase marks inside
cd "${0:A:h}/.."
OUT=traces/gpu-${1:-mc}.txt; mkdir -p traces
echo "Focus Music. Press Enter, then swipe into/out of Mission Control nonstop for 20 s (first 10 s no load, last 10 s under load)."
read "?Press Enter to start... "
for i in 3 2 1; do echo "$i..."; sleep 1; done
echo "PHASE noload" > $OUT
echo ">>> SWIPE NOW (no load) <<<"
./tools/gpuutil.sh 10 >> $OUT
echo "PHASE load" >> $OUT
./tools/bgload.sh 4 11 &
echo ">>> KEEP SWIPING (under load) <<<"
./tools/gpuutil.sh 10 >> $OUT
wait
echo ">>> DONE. $OUT"
