#!/bin/zsh
# A/B: Genie x3 on an app window with EET on vs off; Metal System Trace + Time Profiler each.
# Always restores MSR_POWER_CTL (0x1FC) to 0x0024005F on exit.
# Usage: ./tools/ab-genie.sh [AppProcessName]   (default Mail) -> traces/ab-eet-{on,off}.trace
cd "${0:A:h}/.."
APP=${1:-Mail}
VS=/usr/local/bin/voltageshift
mkdir -p traces
trap '$VS write 0x1FC 0x24005F >/dev/null 2>&1' EXIT INT TERM

genie() {
  for i in 1 2 3; do
    osascript -e "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to true"; sleep 0.9
    osascript -e "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to false"; sleep 0.9
  done
}

n=0
for state in ${=SEQ:-on off}; do   # SEQ="on off on off" for interleaved repeats
  n=$((n + 1))
  [ $state = on ] && $VS write 0x1FC 0x24005F >/dev/null 2>&1 || $VS write 0x1FC 0x2C005F >/dev/null 2>&1
  osascript -e "tell application \"System Events\" to set frontmost of process \"$APP\" to true"; sleep 1.5
  out=traces/ab-$n-eet-$state.trace
  rm -rf $out
  ( sleep 1.5; genie ) &
  xctrace record --template 'Metal System Trace' --instrument 'Time Profiler' --all-processes \
    --time-limit 7s --output $out >/dev/null 2>&1
  wait
  echo "recorded $out (0x1FC=$($VS read 0x1FC | tail -1))"
done
