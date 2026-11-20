#!/bin/zsh
# Hands-off frame pacing test: tools/bgload.sh 4 while Mission Control x3 and App Exposé x3 open/close by command,
# then on-glass pacing from WindowServer's FrameLifetime signposts (tools/frames.py). No recording, no user input.
# Usage: ./tools/auto-frames.sh [workers=4] [app=Mail]   (Apple apps lag far more than third-party windows)
cd "${0:A:h}/.."
W=${1:-4}; APP=${2:-Mail}
MC="/System/Applications/Mission Control.app/Contents/MacOS/Mission Control"
osascript -e "tell application \"$APP\" to activate" >/dev/null 2>&1; sleep 2
T0=$(date '+%Y-%m-%d %H:%M:%S')
./tools/bgload.sh $W 16 &
sleep 1
for i in 1 2 3; do "$MC" & sleep 1.2; "$MC" & sleep 1.2; done
for i in 1 2 3; do "$MC" 2 & sleep 1.2; "$MC" 2 & sleep 1.2; done
wait
sleep 1
python3 tools/frames.py "$T0" "$(date '+%Y-%m-%d %H:%M:%S')"
