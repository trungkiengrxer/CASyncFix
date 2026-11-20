#!/bin/zsh
# Heavy, build-like background load for S seconds: J parallel jobs that each keep compiling a template-heavy C++
# file at -O2 (process spawns, memory churn, file I/O), plus two jobs that keep writing and reading files.
# CPU, memory and disk only: nothing here touches the GPU. Everything lives in a temp dir that is removed at the end.
# Usage: ./tools/heavyload.sh [jobs=24] [seconds=20] [io jobs=2]      (jobs 0 = file I/O only, io jobs 0 = compiles only)
J=${1:-24}; S=${2:-20}; IO=${3:-2}
D=$(mktemp -d /tmp/heavyload.XXXXXX)
trap 'pkill -P $$ 2>/dev/null; rm -rf $D' EXIT INT TERM
cat > $D/src.cpp <<'EOF'
#include <algorithm>
#include <map>
#include <regex>
#include <string>
#include <vector>
template <int N> struct F { static std::map<std::string, std::vector<int>> go(int x) {
    auto m = F<N - 1>::go(x + N); m[std::to_string(N)].push_back(x); return m; } };
template <> struct F<0> { static std::map<std::string, std::vector<int>> go(int) { return {}; } };
int main(int c, char **v) { std::regex r("(a+)(b*)[c-f]+"); auto m = F<200>::go(c);
    return (int)m.size() + (int)std::regex_match(std::string(v[0]), r); }
EOF
END=$(( $(date +%s) + S ))
for ((i = 1; i <= J; i++)); do
  ( while (( $(date +%s) < END )); do clang++ -std=c++17 -O2 -c $D/src.cpp -o $D/o$i.o 2>/dev/null; done ) &
done
for ((i = 1; i <= IO; i++)); do
  ( while (( $(date +%s) < END )); do dd if=/dev/urandom of=$D/io$i bs=1m count=256 2>/dev/null; cat $D/io$i > /dev/null; done ) &
done
wait
