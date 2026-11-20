#!/bin/zsh
# Read-only: iGPU frequency + AGPM stats under a sustained Metal compute load on the Intel GPU.
# Usage: sudo ./tools/measure-gpu-sustained.sh   -> gpu-sustained.txt
cd "${0:A:h}/.."
OUT=gpu-sustained.txt
U=${SUDO_USER:-$USER}
( sleep 1; sudo -u "$U" ./tools/gpuload 8 > gpuload.txt 2>&1 ) &
powermetrics --samplers gpu_power,gpu_agpm_stats -i 250 -n 40 > $OUT 2>&1
wait
{ echo; echo "== gpuload throughput"; grep -v '^nvrm' gpuload.txt; } >> $OUT
rm -f gpuload.txt
chown "$U" $OUT
echo "done -> $OUT"
