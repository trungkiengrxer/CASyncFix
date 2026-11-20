#!/bin/zsh
# Read-only: runs a 1-core load and records what XCPM requests vs. what limits the CPU.
# Usage: sudo ./measure-cpu-limit.sh   -> writes cpu-limit.txt next to this script
cd "${0:A:h}"
( for i in 1 2 3 4; do ./cpu-clock-test; done > load-clock.txt ) &
L=$!
sleep 1
powermetrics --samplers cpu_power --show-pstates --show-plimits --show-hwp-capability -i 1000 -n 5 > cpu-limit.txt 2>&1
wait $L
{ echo; echo "== clock during sampling"; cat load-clock.txt; } >> cpu-limit.txt
rm -f load-clock.txt
chown "${SUDO_USER:-$USER}" cpu-limit.txt
echo "done -> cpu-limit.txt"
