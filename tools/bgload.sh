#!/bin/zsh
# Fixed background CPU load for comparable measurements: N busy-loop workers for S seconds at normal priority.
# Usage: ./tools/bgload.sh [workers=4] [seconds=15]
N=${1:-4}; S=${2:-15}
for i in $(seq 1 $N); do perl -e 'my $e = time + shift; 1 while time < $e' $S & done
wait
