#!/bin/zsh
# Watches for the 800 MHz pinning for up to M minutes (default 60): every 5 s logs power source, CPU temperature (SMC)
# and the speed of one busy thread (tools/cpucontend, alu ~300 = full speed, ~62 = pinned). When pinned for 10 s it
# re-applies the user's own power limit once (`sudo -n voltageshift power 35 45`, allowed NOPASSWD) and logs whether
# that releases the pin. Output: traces/pinwatch-<time>.txt
cd "${0:A:h}/.."
M=${1:-60}; OUT=traces/pinwatch-$(date +%H%M%S).txt; END=$(( SECONDS + M * 60 )); low=0; lastFix=-999
speed() { ./tools/cpucontend 0.4 | awk '{printf "%.0f", $2}'; }
line() { echo "$(date +%H:%M:%S) $(pmset -g ps | head -1 | sed -e "s/Now drawing from //" -e "s/'//g") $(./tools/smctemp TC0P) alu=$1 $2" >> $OUT; }
while (( SECONDS < END )); do
  a=$(speed)
  if (( a < 90 )); then low=$((low + 1)); else low=0; fi
  if (( low >= 3 && SECONDS - lastFix > 120 )); then
    line $a "PINNED 10s -> voltageshift power 35 45: $(sudo -n /usr/local/bin/voltageshift power 35 45 2>&1 | grep Current | tr -s ' ')"
    sleep 2; line $(speed) "2 s after"; sleep 8; line $(speed) "10 s after"
    lastFix=$SECONDS; low=0
  else
    line $a
  fi
  sleep 5
done
