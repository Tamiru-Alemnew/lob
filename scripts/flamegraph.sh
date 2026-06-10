#!/usr/bin/env bash
# perf flame graph of the matcher replaying the workload 20 times; stacks outside the replay loop
# (file loading, engine construction) are dropped.
# Needs perf and a checkout of https://github.com/brendangregg/FlameGraph (FLAMEGRAPH_DIR).
set -euo pipefail
BUILD=${BUILD:-build-prof}
WORKLOAD=${1:-build/workload_42_10000000.bin}
FG=${FLAMEGRAPH_DIR:-../FlameGraph}
PERF=${PERF:-perf}

cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DLOB_TESTS=OFF -DLOB_BENCH=OFF \
  -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer" > /dev/null
cmake --build "$BUILD" --target replay -j > /dev/null
"$PERF" record -F 2999 -g -o "$BUILD/perf.data" -- "$BUILD/replay" "$WORKLOAD" --loops 20
"$PERF" script -i "$BUILD/perf.data" | "$FG/stackcollapse-perf.pl" | grep -v -E "OrderBook::OrderBook|read_workload|munmap|memset|_dl_|ios_base" > "$BUILD/folded.txt"
"$FG/flamegraph.pl" --title "lob replay ($WORKLOAD)" --width 1100 "$BUILD/folded.txt" > results/flamegraph.svg
echo "wrote results/flamegraph.svg"
