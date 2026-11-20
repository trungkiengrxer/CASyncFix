#!/bin/zsh
# Hands-off test: Music frontmost, fixed background load (tools/bgload.sh), Time Profiler for 12 s while the script
# opens/closes Mission Control x3 and App Exposé x3 by command. No user input.
# Usage: ./tools/auto-mc.sh <tag> [workers=4]     -> traces/auto-<tag>.trace
cd "${0:A:h}/.."
TAG=$1; W=${2:-4}; OUT=traces/auto-$TAG.trace; mkdir -p traces
MC="/System/Applications/Mission Control.app/Contents/MacOS/Mission Control"
front() { osascript -e "tell application \"${APP:-Music}\" to activate" >/dev/null 2>&1; }

for attempt in 1 2 3; do
  front; sleep 2
  rm -rf $OUT
  ./tools/bgload.sh $W 16 &
  BG=$!
  xctrace record --template 'Time Profiler' --all-processes --time-limit 12s --output $OUT >/dev/null 2>&1 &
  XP=$!
  ( sleep 2; for i in 1 2 3; do "$MC" & sleep 1.2; "$MC" & sleep 1.2; done
             for i in 1 2 3; do "$MC" 2 & sleep 1.2; "$MC" 2 & sleep 1.2; done ) &
  AP=$!
  for s in $(seq 1 90); do kill -0 $XP 2>/dev/null || break; sleep 1; done
  if kill -0 $XP 2>/dev/null; then
    kill $XP $AP 2>/dev/null; pkill -f "xctrace record.*$OUT"; wait $BG 2>/dev/null
    echo "xctrace hung, retry $attempt"; sleep 5; continue
  fi
  wait $AP $BG 2>/dev/null
  echo "recorded $OUT"; exit 0
done
echo "xctrace failed 3 times"; exit 1
