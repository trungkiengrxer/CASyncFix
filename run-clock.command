#!/bin/zsh
cd "/Users/trungkien/Library/Application Support/Claude/scratch-workspaces/97ad72a1-e86f-412f-9416-45fc92fcc139/86f50f56-3d7d-4a13-ba08-1b12e863d8f1/scratch-2026-11-14-6f5d6c"
{ echo "== 1 core (Terminal, foreground)"; ./cpu-clock-test | tr '\n' ' '; echo
  echo "== 6 cores"; for i in 1 2 3 4 5 6; do ./cpu-clock-test > ct.$i & done; wait; cat ct.* | sort -n | tr '\n' ' '; rm -f ct.*; echo; } > clock-terminal.txt 2>&1
echo done >> clock-terminal.txt
