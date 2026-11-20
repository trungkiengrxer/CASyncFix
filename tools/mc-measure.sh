#!/bin/zsh
# Mission Control enter/exit x3 with <App> frontmost, recorded twice:
#   traces/mc-<app>-metal.trace  (Metal System Trace: frame pacing)
#   traces/mc-<app>-tp.trace     (Time Profiler only: clean CPU breakdown)
# Usage: ./tools/mc-measure.sh Music Safari
cd "${0:A:h}/.."
MC="/System/Applications/Mission Control.app/Contents/MacOS/Mission Control"
mkdir -p traces

run() {   # run <template> <out> <app>
  for attempt in 1 2 3; do
    osascript -e "tell application \"System Events\" to set frontmost of process \"$3\" to true" >/dev/null 2>&1; sleep 2
    rm -rf $2
    xctrace record --template "$1" --all-processes --time-limit 8s --output $2 >/dev/null 2>&1 &
    local xp=$!
    ( sleep 1.5; for i in 1 2 3; do "$MC" & sleep 1.0; "$MC" & sleep 1.0; done ) &
    local ap=$!
    for s in $(seq 1 90); do kill -0 $xp 2>/dev/null || break; sleep 1; done
    wait $ap 2>/dev/null
    if kill -0 $xp 2>/dev/null; then kill $xp; pkill -f "xctrace record.*$2"; echo "hung, retry $attempt"; sleep 3; continue; fi
    echo "recorded $2"; return 0
  done
}

for app in "$@"; do
  open -a $app; sleep 3
  lower=${app:l}
  run 'Metal System Trace' traces/mc-$lower-metal.trace $app
  run 'Time Profiler' traces/mc-$lower-tp.trace $app
done
