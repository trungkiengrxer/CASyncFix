#!/bin/zsh
# Records Genie x3 on an app window twice: System Trace (to count WindowServer finish() waits) and
# Metal System Trace (frame pacing). Same procedure as the pre-fix traces.
# Usage: ./tools/verify-casync.sh [AppProcessName] [tag]   -> traces/<tag>-sys.trace, traces/<tag>-metal.trace
cd "${0:A:h}/.."
APP=${1:-Mail}; TAG=${2:-after}
mkdir -p traces

genie() {
  for i in 1 2 3; do
    osascript -e "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to true"; sleep 0.9
    osascript -e "tell application \"System Events\" to tell process \"$APP\" to set value of attribute \"AXMinimized\" of window 1 to false"; sleep 0.9
  done
}

for kind in sys metal; do
  [ $kind = sys ] && template='System Trace' || template='Metal System Trace'
  osascript -e "tell application \"System Events\" to set frontmost of process \"$APP\" to true"; sleep 1.5
  out=traces/$TAG-$kind.trace; rm -rf $out
  ( sleep 1.5; genie ) &
  xctrace record --template "$template" --all-processes --time-limit 7s --output $out >/dev/null 2>&1
  wait
  echo "recorded $out"
done
