#!/bin/zsh
# Records 10 s while the user reproduces the lag by hand (trackpad gestures).
# Usage: ./tools/record-manual.sh <tag> [metal|tp|both]   -> traces/manual-<tag>.trace
#   metal: Metal System Trace only (frame pacing, GPU intervals)
#   tp:    Time Profiler only (clean CPU breakdown, no kdebug inflation)
#   both:  Metal System Trace + Time Profiler (default, CPU numbers inflated ~30%)
# System Trace is avoided: it hangs intermittently on this machine.
cd "${0:A:h}/.."
TAG=${1:-mc}; MODE=${2:-both}; OUT=traces/manual-$TAG.trace
case $MODE in
  metal) ARGS=(--template 'Metal System Trace') ;;
  tp)    ARGS=(--template 'Time Profiler') ;;
  *)     ARGS=(--template 'Metal System Trace' --instrument 'Time Profiler') ;;
esac
mkdir -p traces; rm -rf $OUT
echo "Get ready: focus the app to measure (Music/Safari). After Enter, a 3 s countdown, then a 10 s recording."
echo "During those 10 s: swipe INTO Mission Control, wait 2 s, swipe OUT, wait 2 s. Repeat twice."
read "?Press Enter to start... "
for i in 3 2 1; do echo "$i..."; sleep 1; done
echo ">>> RECORDING, go now <<<"
xctrace record $ARGS --all-processes --time-limit 10s --output $OUT >/dev/null 2>&1 &
XP=$!
for s in $(seq 1 120); do kill -0 $XP 2>/dev/null || break; sleep 1; done
if kill -0 $XP 2>/dev/null; then kill $XP; echo "xctrace hung, run again"; exit 1; fi
echo ">>> DONE. Trace: $OUT"
