#!/bin/zsh
# What do WindowServer, Dock and the front app lack under background load? No root, no tracing.
# Runs the tools/auto-frames.sh animation (Mission Control x3, App Expose x3, ~17 s) under one kind of load and prints
# on-glass frame pacing plus, for that window: per-process deltas of CPU time, context switches, page faults and
# page-ins (top), and system-wide VM deltas (vm_stat: faults, zero-fills, compressions, decompressions, page-ins).
# Usage: ./tools/loadstat.sh <label> none | busy <workers> | heavy <compile jobs> <io jobs>     [APP=Mail]
cd "${0:A:h}/.."
LABEL=$1; KIND=${2:-none}; APP=${APP:-Mail}
PIDS=($(pgrep -x WindowServer) $(pgrep -x Dock) $(pgrep -x "$APP" | head -1) 0)      # 0 = kernel_task
snap() {    # "<pid> <command> <cpu time s> <csw> <faults> <pageins>" per process, then "vm <name> <count>" lines
  local args=(); for p in $PIDS; do args+=(-pid $p); done
  top -l 1 -stats pid,command,time,csw,faults,pageins $args 2>/dev/null |
    awk '$1 ~ /^[0-9]+$/ && NF >= 6 { n = split($(NF-3), t, /[:.]/); s = t[n-2] * 60 + t[n-1] + t[n] / 100; if (n == 4) s += t[1] * 3600
           for (i = NF - 2; i <= NF; i++) gsub(/[+-]/, "", $i); print $1, $2, s, $(NF-2), $(NF-1), $NF }'
  for p in $PIDS[1,3]; do     # user / system CPU split (ps reads it for any process)
    ps -p $p -o utime=,stime= | awk -v p=$p 'function s(x, t, n) { n = split(x, t, /[:.]/); return t[n-2] * 60 + t[n-1] + t[n] / 100 }
                                             { print "us", p "|" s($1) "|" s($2) }'
  done
  vm_stat | awk -F: '/faults|zero filled|Compressions|Decompressions|Pageins|Pageouts|copy-on-write/ { gsub(/[ .]/, "", $2); gsub(/"/, "", $1); print "vm", $1 "|" $2 }'
}
open -a "$APP"; sleep 2
case $KIND in
  busy)  ./tools/bgload.sh ${3:-12} 22 & L=$! ;;
  heavy) ./tools/heavyload.sh ${3:-24} 22 ${4:-0} & L=$! ;;
  *)     L= ;;
esac
[[ -n $L ]] && sleep 3
A=$(snap)
FR=$(./tools/auto-frames.sh 0 "$APP" 2>&1 | grep -E "^all|buffer_count=3")
B=$(snap)
[[ -n $L ]] && wait $L
echo "=== $LABEL ($KIND ${@:3})"
echo "$FR" | sed 's/^/  /'
echo "  process          cpu s   ctx switches   page faults   page-ins"
paste -d' ' <(echo "$A" | grep -Ev '^(vm|us)') <(echo "$B" | grep -Ev '^(vm|us)') |
  awk '{ printf "  %-14s %7.2f %14d %13d %10d\n", $2, $9 - $3, $10 - $4, $11 - $5, $12 - $6 }'
paste -d'|' <(echo "$A" | grep '^us') <(echo "$B" | grep '^us') |
  awk -F'|' '{ sub(/^us /, "", $1); printf "  pid %-6s user %.2f s, system %.2f s\n", $1, $5 - $2, $6 - $3 }'
paste -d'|' <(echo "$A" | grep '^vm') <(echo "$B" | grep '^vm') |
  awk -F'|' '{ sub(/^vm /, "", $1); printf "  vm %-28s %12d\n", $1, $4 - $2 }'
