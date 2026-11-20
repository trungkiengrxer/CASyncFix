#!/bin/zsh
# Runs every verification scenario and prints the summary table.
# Usage: ./tools/run-all.sh [tag-prefix]   (default "fix"; use e.g. "base" for a -casyncoff boot)
cd "${0:A:h}/.."
P=${1:-fix}
for app in Mail Safari Music Weather; do open -a $app; done; sleep 6
./tools/scenario.sh genie:Mail   $P-mail-genie
./tools/scenario.sh genie:Safari $P-safari-genie
./tools/scenario.sh genie:Music  $P-music-genie
./tools/scenario.sh mc:Mail      $P-mission-control
./tools/scenario.sh expose:Safari $P-app-expose
./tools/scenario.sh tile x
./tools/scenario.sh genie:Safari $P-all4-genie
./tools/scenario.sh mc:Safari    $P-all4-mission-control
./tools/scenario.sh expose:Safari $P-all4-app-expose
python3 -I tools/summarize.py $P-mail-genie $P-safari-genie $P-music-genie $P-mission-control $P-app-expose \
  $P-all4-genie $P-all4-mission-control $P-all4-app-expose
