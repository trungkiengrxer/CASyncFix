#!/bin/zsh
# Marks a 20 s window of real finger swipes (no recording: WindowServer's own signposts and the probe log are read
# afterwards). First 10 s idle machine, last 10 s with tools/bgload.sh 4. Prints the times for tools/frames.py,
# tools/probe.py and the input-frame analysis.
# Usage: ./tools/swipe-window.sh
cd "${0:A:h}/.."
echo "Open Mail. Press Enter, then swipe into/out of Mission Control nonstop for 20 s (load starts after 10 s)."
read "?Press Enter to start... "
for i in 3 2 1; do echo "$i..."; sleep 1; done
T0=$(date '+%Y-%m-%d %H:%M:%S'); echo ">>> SWIPE (no load)"
sleep 10
TM=$(date '+%Y-%m-%d %H:%M:%S'); ./tools/bgload.sh 4 10 & echo ">>> KEEP SWIPING (under load)"
wait
T1=$(date '+%Y-%m-%d %H:%M:%S')
echo "$T0|$TM|$T1" > traces/swipe-window.txt
echo ">>> DONE ($T0 -> $TM -> $T1)"
