#!/bin/zsh
# Read-only: iGPU frequency residency while doing Genie x3 on an app window.
# Usage: sudo ./tools/measure-gpu-freq.sh [AppProcessName]   (default Mail) -> gpu-freq.txt
cd "${0:A:h}/.."
APP=${1:-Mail}
OUT=gpu-freq.txt
U=${SUDO_USER:-$USER}
asuser() { sudo -u "$U" osascript -e "$1"; }
asuser "tell application \"System Events\" to set frontmost of process \"$APP\" to true"; sleep 1.5
( sleep 1; for i in 1 2 3; do
    asuser "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to true"; sleep 0.9
    asuser "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to false"; sleep 0.9
  done ) &
powermetrics --samplers gpu_power -i 200 -n 30 > $OUT 2>&1
wait
chown "$U" $OUT
echo "done -> $OUT"
