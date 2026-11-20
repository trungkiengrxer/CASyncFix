#!/bin/zsh
# Screen video while the user swipes Mission Control: 8 s without load, then 8 s with tools/bgload.sh 4.
# Shows what WindowServer actually put on screen per frame (analyse with tools/framediff.sh).
# Usage: ./tools/record-screen.sh <tag>   -> traces/screen-<tag>.mov
cd "${0:A:h}/.."
OUT=traces/screen-${1:-mc}.mov; mkdir -p traces; rm -f $OUT
echo "Open any app (Mail...). Press Enter, then swipe into/out of Mission Control nonstop for 16 s (first 8 s no load, last 8 s under load)."
read "?Press Enter to start... "
for i in 3 2 1; do echo "$i..."; sleep 1; done
screencapture -v -V16 -x $OUT &
SC=$!
echo ">>> SWIPE NOW (no load) <<<"
sleep 8
./tools/bgload.sh 4 8 &
echo ">>> KEEP SWIPING (under load) <<<"
wait $SC
wait
[[ -s $OUT ]] && echo ">>> DONE. $OUT" || echo ">>> No video: grant Screen Recording permission to this terminal app"
