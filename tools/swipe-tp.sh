#!/bin/zsh
# One 20 s window of real finger swipes with Time Profiler (all processes): 10 s idle, 10 s with tools/bgload.sh 4.
# Writes traces/swipe-tp.trace and traces/swipe-window.txt (start|load start|end) for the signpost analysis.
# Usage: ./tools/swipe-tp.sh
cd "${0:A:h}/.."
OUT=traces/swipe-tp.trace; rm -rf $OUT
echo "Open Mail. Press Enter, then swipe into/out of Mission Control nonstop for 20 s (load starts after 10 s)."
read "?Press Enter to start... "
xctrace record --template 'Time Profiler' --all-processes --time-limit 22s --output $OUT >/dev/null 2>&1 &
XP=$!
sleep 2
T0=$(date '+%Y-%m-%d %H:%M:%S'); echo ">>> SWIPE (no load)"
sleep 10
TM=$(date '+%Y-%m-%d %H:%M:%S'); ./tools/bgload.sh 4 10 & echo ">>> KEEP SWIPING (under load)"
sleep 10
T1=$(date '+%Y-%m-%d %H:%M:%S')
echo "$T0|$TM|$T1" > traces/swipe-window.txt
echo ">>> STOP SWIPING, saving the trace..."
for s in $(seq 1 90); do kill -0 $XP 2>/dev/null || break; sleep 1; done
kill -0 $XP 2>/dev/null && { kill $XP; echo "xctrace hung"; exit 1; }
echo ">>> DONE ($T0 -> $TM -> $T1)"
