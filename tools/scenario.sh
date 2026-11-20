#!/bin/zsh
# Records one animation scenario twice: System Trace (WindowServer finish() waits) and Metal System Trace
# (frame pacing). Usage: ./tools/scenario.sh <scenario> <tag>
#   genie:<App>     Genie minimize/restore x3 on window 1 of <App> (AX process name)
#   mc[:<App>]      Mission Control enter/exit x3, <App> frontmost
#   expose[:<App>]  App Exposé enter/exit x3 on <App>
#   tile            arrange Mail, Safari, Music, Weather in four quadrants (no recording)
# Output: traces/<tag>-sys.trace, traces/<tag>-metal.trace
cd "${0:A:h}/.."
SC=$1; TAG=$2; mkdir -p traces
MC="/System/Applications/Mission Control.app/Contents/MacOS/Mission Control"
ax() { osascript -e "tell application \"System Events\" to $1" >/dev/null 2>&1; }

if [ "$SC" = tile ]; then
  # screen 1920x1080, menu bar ~30 px
  i=0
  for app in Mail Safari Music Weather; do
    open -a $app; sleep 2
    x=$(( (i % 2) * 960 )); y=$(( 30 + (i / 2) * 525 ))
    ax "tell process \"$app\" to set value of attribute \"AXMinimized\" of window 1 to false"
    ax "tell process \"$app\" to set position of window 1 to {$x, $y}"
    ax "tell process \"$app\" to set size of window 1 to {960, 525}"
    i=$((i + 1))
  done
  echo "tiled"; exit 0
fi

kind=${SC%%:*}; app=${SC#*:}; [ "$app" = "$SC" ] && app=Safari

action() {
  for i in 1 2 3; do
    case $kind in
      genie)
        ax "tell process \"$app\" to set value of attribute \"AXMinimized\" of window 1 to true"; sleep 0.9
        ax "tell process \"$app\" to set value of attribute \"AXMinimized\" of window 1 to false"; sleep 0.9 ;;
      mc)     "$MC" & sleep 1.0; "$MC" & sleep 1.0 ;;
      expose) "$MC" 2 & sleep 1.0; "$MC" 2 & sleep 1.0 ;;
    esac
  done
}

# xctrace sometimes hangs at start (0% CPU, empty trace); kill and retry after 90 s, up to 3 attempts.
record() {
  local template=$1 out=$2
  for attempt in 1 2 3; do
    ax "set frontmost of process \"$app\" to true"; sleep 1.5
    rm -rf $out
    xctrace record --template "$template" --all-processes --time-limit 8s --output $out >/dev/null 2>&1 &
    local xp=$!
    ( sleep 1.5; action ) &
    local ap=$!
    for s in $(seq 1 90); do kill -0 $xp 2>/dev/null || break; sleep 1; done
    if kill -0 $xp 2>/dev/null; then
      kill $xp 2>/dev/null; pkill -f "xctrace record.*$out"; wait $ap 2>/dev/null
      [ $kind = genie ] && ax "tell process \"$app\" to set value of attribute \"AXMinimized\" of window 1 to false"
      echo "xctrace hung ($template), retry $attempt"; sleep 3; continue
    fi
    wait $ap 2>/dev/null
    echo "recorded $out"; return 0
  done
  echo "FAILED $out"; return 1
}

record 'System Trace' traces/$TAG-sys.trace
record 'Metal System Trace' traces/$TAG-metal.trace
