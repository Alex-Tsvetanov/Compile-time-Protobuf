#!/bin/bash
# Compile-time constexpr parser latency/throughput, Small (5 fields) / Medium (25
# fields) / Large (100 fields), matching the paper's Table III shape. The checked-in
# benchmarks/bench_compile_time.cpp only exercises two fixed, differently-shaped
# schemas and averages them together, so it can't reproduce that table — this script
# generates field-count-scaled schemas directly and measures each in isolation.
#
# Reports min/median/max over multiple trials, not a single point estimate.
set -e

TRIALS=${TRIALS:-7}
ITERS=${ITERS:-20000}
DIR="/tmp/eval_compile_time"
rm -rf "$DIR"
mkdir -p "$DIR"

gen_schema() {
    local name="$1" n="$2"
    echo 'syntax = "proto3";'
    echo "message $name {"
    for i in $(seq 1 "$n"); do
        echo "  int32 f$i = $i;"
    done
    echo "}"
}

gen_schema Small 5   > "$DIR/small.proto.txt"
gen_schema Medium 25 > "$DIR/medium.proto.txt"
gen_schema Large 100 > "$DIR/large.proto.txt"

cat << EOF > "$DIR/bench.cpp"
#include <chrono>
#include <cstdio>
#include <string_view>
#include <ctpb/ctpb.hpp>

constexpr std::string_view small_schema = R"PROTO($(cat "$DIR/small.proto.txt"))PROTO";
constexpr std::string_view medium_schema = R"PROTO($(cat "$DIR/medium.proto.txt"))PROTO";
constexpr std::string_view large_schema = R"PROTO($(cat "$DIR/large.proto.txt"))PROTO";

template <std::string_view const& Schema>
double time_one(int iters) {
    // Warmup
    for (int i = 0; i < 2000; ++i) {
        ctpb::parser p{Schema};
        volatile auto f = p.parse_file();
        (void)f;
    }
    auto t0 = std::chrono::steady_clock::now();
    std::size_t sink = 0;
    for (int i = 0; i < iters; ++i) {
        ctpb::parser p{Schema};
        auto f = p.parse_file();
        sink += f.messages[0].fields.size();
    }
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1000.0;
    std::fprintf(stderr, "sink=%zu\n", sink); // prevent optimizing the loop away
    return us / iters;
}

int main() {
    for (int trial = 0; trial < ${TRIALS}; ++trial) {
        double small_us  = time_one<small_schema>(${ITERS});
        double medium_us = time_one<medium_schema>(${ITERS});
        double large_us  = time_one<large_schema>(${ITERS});
        std::printf("trial=%d small_us=%.3f medium_us=%.3f large_us=%.3f\n", trial, small_us, medium_us, large_us);
    }
    return 0;
}
EOF

clang++ -O3 -std=c++26 -freflection -I/workspace/include "$DIR/bench.cpp" -o "$DIR/bench" 2>"$DIR/stderr.log" || { cat "$DIR/stderr.log"; exit 1; }
# stderr carries the benchmark's own anti-optimisation sink line. Discarding it
# hides whatever the program has to say about itself, which is how this
# repository lost a campaign to a harness that exited 0 having measured nothing.
"$DIR/bench"
