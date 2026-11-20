#!/bin/zsh
# A/B test: does disabling Energy Efficient Turbo (MSR_POWER_CTL 0x1FC bit 19) lift the 3.7 GHz 1-core ceiling?
# 0x1FC is package-scoped and was read safely before (0x0024005F). Only bit 19 is flipped, then ALWAYS restored.
# Usage: ./tools/test-eet.sh   (from the project root) -> writes test-eet.txt
cd "${0:A:h}/.."
VS=/usr/local/bin/voltageshift
CLK="./cpu-clock-test"
OUT=test-eet.txt; : > $OUT
ORIG=0x24005F
EET_OFF=0x2C005F   # ORIG | (1 << 19)

[ -x "$CLK" ] || { echo "missing $CLK (build: clang -O2 -o cpu-clock-test cpu-clock-test.c)"; exit 1; }
rd() { $VS read 0x1FC 2>&1 | tail -1; }
restore() { $VS write 0x1FC $ORIG >/dev/null 2>&1; echo "restored 0x1FC: $(rd)" >> $OUT; }
trap restore EXIT INT TERM

[ "$(rd)" = "(00000000 00000000 00000000 00000000 00000000 00100100 00000000 01011111)" ] \
  || { echo "0x1FC is not the expected 0x0024005F, aborting: $(rd)" >> $OUT; trap - EXIT; exit 1; }

echo "== baseline 0x1FC: $(rd)" >> $OUT
echo "clock: $($CLK | tr '\n' ' ')" >> $OUT

$VS write 0x1FC $EET_OFF >/dev/null 2>&1
echo "== EET disabled 0x1FC: $(rd)" >> $OUT
echo "clock: $($CLK | tr '\n' ' ')" >> $OUT
echo done
