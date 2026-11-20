#!/bin/zsh
# Hands-off check of CASyncFix forced flatten (0.8.x): for each app, bring it to the front and print the flatten
# state of its windows (WindowServer's own dump, tools/wsdebug dump-flatten) on the desktop and during Mission
# Control, App Expose, Show Desktop, Genie minimize / restore and a scripted live resize. No user input.
# Expected with 0.8.1: NeverFlatten on the desktop for apps with glass, Flattened (or Reflattened) during every
# activity, and the stock state again a moment after it ends.
# Usage: ./tools/verify-flat.sh [app ...]      (default: Mail Notes Music Safari "Microsoft Edge")
cd "${0:A:h}/.."
MC="/System/Applications/Mission Control.app/Contents/MacOS/Mission Control"
nap() { perl -e "select(undef, undef, undef, $1)"; }
# state <app>: "wid:WSState" of the app's on-screen windows (the menu bar window of the front app is level 24: skip
# ids that every app reports by keeping only windows that are not NeverFlatten on every line is not possible, so
# print them all and read the app's main window).
state() {
  ./tools/wsdebug dump-flatten >/dev/null; nap 0.08
  grep OrderedIn /tmp/WindowServer.finfo.out | grep -F "$1" | awk '{printf "%s:%s ", $2, $5}'
}
sample() {   # sample <app> <label> <delay>...
  local app=$1 label=$2; shift 2
  local out=""
  for d in "$@"; do nap $d; out+="[$(state "$app")] "; done
  echo "  $label: $out"
}

./tools/check_patch | tail -2
(( $# )) || set -- Mail Notes Music Safari "Microsoft Edge"
for app in "$@"; do
  open -a "$app"; nap 5
  echo "=== $app (front: $(osascript -e 'tell application "System Events" to get name of first process whose frontmost is true'))"
  sample "$app" "desktop        " 0
  "$MC" & sample "$app" "mission control" 0.15 0.3 0.6; "$MC" & wait; nap 1.5
  sample "$app" "after          " 0
  "$MC" 2 & sample "$app" "app expose     " 0.15 0.5; "$MC" 2 & wait; nap 1.5
  "$MC" 1 & sample "$app" "show desktop   " 0.05 0.15 0.3; "$MC" 1 & sample "$app" "desktop back   " 0.05 0.15 0.3; wait; nap 1.5
  sample "$app" "after          " 0
  if osascript -e "tell application \"$app\" to get miniaturized of window 1" >/dev/null 2>&1; then
    osascript -e "tell application \"$app\" to set miniaturized of window 1 to true" & sample "$app" "genie minimize " 0.1 0.15 0.2; wait; nap 1.2
    osascript -e "tell application \"$app\" to set miniaturized of window 1 to false" & sample "$app" "genie restore  " 0.1 0.15 0.2; wait; nap 1.5
    # live resize: grow and shrink the window in small steps
    B=$(osascript -e "tell application \"$app\" to get bounds of window 1" 2>/dev/null)
    if [[ -n $B ]]; then
      osascript -e "tell application \"$app\"
        set {l, t, r, b} to bounds of window 1
        repeat with i from 1 to 30
          set bounds of window 1 to {l, t, r + i * 4, b + i * 3}
        end repeat
        set bounds of window 1 to {l, t, r, b}
      end tell" >/dev/null 2>&1 &
      sample "$app" "resize         " 0.2 0.3 0.4; wait; nap 1.5
      sample "$app" "after          " 0
    fi
  else
    echo "  (window 1 is not scriptable: Genie and resize skipped)"
  fi
done
