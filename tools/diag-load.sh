#!/bin/zsh
# One-shot diagnosis of "everything gets slow under build load": is the CPU itself being slowed (frequency, power or
# thermal limit), and by which kind of load? Run once with sudo; about 3 minutes, no input needed:
#   sudo ./tools/diag-load.sh
# Three phases (no load, 24 busy loops, 24 parallel compiles). In each: powermetrics records per-core frequency,
# power limits and package power every second (root only; skipped without root), tools/cpucontend measures how fast
# one thread really runs, and one Mission Control / App Expose pass gives on-glass frame pacing.
# CPU and memory load only, nothing touches the GPU. Results: traces/diag-load-<time>/ (summary.txt first).
cd "${0:A:h}/.."
U=${SUDO_USER:-$USER}
asuser() { if [[ $EUID == 0 ]]; then sudo -u $U "$@"; else "$@"; fi; }
OUT=traces/diag-load-$(date +%H%M%S); asuser mkdir -p $OUT
[[ -x tools/cpucontend ]] || asuser clang -O2 -o tools/cpucontend tools/cpucontend.c

phase() {   # phase <name> <seconds> <load command...>
  local name=$1 secs=$2; shift 2
  echo "== $name" | tee -a $OUT/summary.txt
  local PM=
  if [[ $EUID == 0 ]]; then
    powermetrics --samplers cpu_power,thermal --show-plimits --show-extra-power-info -i 1000 -n $secs > $OUT/pm-$name.txt 2>&1 &
    PM=$!
  fi
  local L=
  if (( $# )); then asuser "$@" & L=$!; sleep 2; fi
  local t0=$SECONDS
  asuser ./tools/auto-frames.sh 0 Mail 2>&1 | grep -E "^all|buffer_count=3" | sed 's/^/  frames: /' | tee -a $OUT/summary.txt
  while (( SECONDS - t0 < secs - 5 )); do
    echo "  t=$((SECONDS - t0))s $(asuser ./tools/cpucontend 2)" | tee -a $OUT/summary.txt
  done
  [[ -n $L ]] && wait $L
  [[ -n $PM ]] && wait $PM
  pmset -g therm | grep -E "Limit|Available" | tr -d '\t' | sed 's/^/  /' >> $OUT/summary.txt
  if [[ -n $PM ]]; then
    # the lines that answer the question: package frequency, power, and any limit powermetrics names
    grep -E "System Average frequency|Package Power|Intel energy model|CPU Thermal level|limit|Limit|plimit|PROCHOT|throttl" $OUT/pm-$name.txt |
      sort | uniq -c | sort -rn | head -25 | sed 's/^/  pm: /' >> $OUT/summary.txt
  fi
  sleep 25      # cool down between phases
}

phase none 30
phase busy24 45 ./tools/bgload.sh 24 45
phase compile24 60 ./tools/heavyload.sh 24 60 0
[[ $EUID == 0 ]] && chown -R $U $OUT
echo "done: $OUT/summary.txt"
