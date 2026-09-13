#!/usr/bin/env bash
#
# Throughput, ctpb against libprotobuf, with each arm in its own program.
#
# Why this replaces eval_runtime_comparison.sh: that harness puts both arms in
# one translation unit, which forces one standard library on both. libc++ is
# where <meta> lives, so the P2996 gate closes and the ctpb arm silently
# becomes the C++20 fallback -- which is what every published throughput number
# in this repository actually measured. A distribution libprotobuf is built
# against libstdc++ and does not link against libc++, so there is no flag that
# fixes the single-TU version. The arms have to be separate programs.
#
# What that costs, stated rather than hidden: the old loop interleaved the four
# blocks so that frequency or thermal drift landed evenly on both arms. Separate
# processes cannot interleave at that granularity. Instead the script alternates
# whole process runs (ctpb, protobuf, ctpb, protobuf, ...) across REPS and the
# summariser takes a median over all rounds of all reps, so drift is spread
# across both arms rather than accumulating on whichever ran last.
#
# What is identical by construction: the timed loop, which is bench_harness.hpp
# compiled into both programs, and the barrier applied to both arms.
#
# What differs and must be disclosed: the C++ runtime. ctpb links libc++, the
# protobuf arm libstdc++, because neither will build the other way.
#
# Usage:  REPS=5 ROUNDS=20 ./eval_runtime_split.sh | tee runtime_raw.txt
set -euo pipefail

REPS=${REPS:-5}
ROUNDS=${ROUNDS:-20}
PER_ROUND=${PER_ROUND:-50000}
WARMUP=${WARMUP:-20000}
CXX=${CXX:-clang++}
OPT=${OPT:--O3}
REPO=${REPO:-/workspace}
DIR=${DIR:-/tmp/eval_runtime_split}

mkdir -p "$DIR"
rm -f "$DIR"/*.bin "$DIR"/*.o 2>/dev/null || true
cd "$DIR"

command -v protoc > /dev/null || { echo "protoc not found" >&2; exit 1; }

cat > "$DIR/person.proto" <<'EOF'
syntax = "proto3";
package tutorial;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
EOF

protoc --cpp_out="$DIR" --proto_path="$DIR" "$DIR/person.proto"

# --- the ctpb arm -----------------------------------------------------------
cat > "$DIR/arm_ctpb.cpp" <<'EOF'
#include <cassert>
#include <cstdio>
#include <span>
#include <string>
#include <vector>
#include <ctpb/ctpb.hpp>
#include "bench_harness.hpp"
#include "bench_config.hpp"

inline constexpr ctpb::fixed_string person_proto = R"PROTO(
syntax = "proto3";
package tutorial;
message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
)PROTO";

#if CTPB_HAS_P2996
consteval { ctpb::define_message<person_proto, "Person">(); }
using Person = ctpb::message_t<person_proto, "Person">;
#else
#  error "this arm exists to measure the reflection path; CTPB_HAS_P2996 is 0"
#endif

int main() {
    Person p{};
    p.name  = "Alex I. Tsvetanov";
    p.id    = 42000;
    p.email = "atsvetanov@tu-sofia.bg";

    std::vector<std::byte> wire = ctpb::encode(p);

    // Publish this arm's bytes so the other arm can decode them, and the script
    // can compare the two files. Wire parity across arms is the thing that makes
    // the comparison a comparison.
    if (std::FILE* f = std::fopen("wire_ctpb.bin", "wb")) {
        std::fwrite(wire.data(), 1, wire.size(), f);
        std::fclose(f);
    }

    // Encode into a buffer allocated once, and decode into a target reused
    // across iterations, so neither number is partly an allocator benchmark.
    std::vector<std::byte> out(wire.size());
    Person target{};

    auto enc = [&] {
        ctpb::encode_to_ptr(p, out.data());
        harness::sink(out.data(), out.size());
    };
    auto dec = [&] {
        target = Person{};
        auto err = ctpb::decode_into(target, std::as_bytes(std::span(wire)));
        harness::sink(&err, sizeof(err));
    };

    // Correctness before speed: a benchmark of a decoder that does not decode
    // is a benchmark of nothing.
    dec();
    assert(target.name == p.name && target.id == p.id && target.email == p.email);

    harness::measure("ctpb", enc, dec, BENCH_ROUNDS, BENCH_PER_ROUND,
                     BENCH_WARMUP, wire.size());
    return 0;
}
EOF

# --- the protobuf arm -------------------------------------------------------
cat > "$DIR/arm_protobuf.cpp" <<'EOF'
#include <cassert>
#include <cstdio>
#include <string>
#include "person.pb.h"
#include "bench_harness.hpp"
#include "bench_config.hpp"

int main() {
    tutorial::Person p;
    p.set_name("Alex I. Tsvetanov");
    p.set_id(42000);
    p.set_email("atsvetanov@tu-sofia.bg");

    std::string wire;
    p.SerializeToString(&wire);

    if (std::FILE* f = std::fopen("wire_protobuf.bin", "wb")) {
        std::fwrite(wire.data(), 1, wire.size(), f);
        std::fclose(f);
    }

    std::string out;
    out.resize(wire.size());
    tutorial::Person target;

    auto enc = [&] {
        p.SerializeToArray(out.data(), static_cast<int>(out.size()));
        harness::sink(out.data(), out.size());
    };
    auto dec = [&] {
        target.Clear();
        bool ok = target.ParseFromString(wire);
        harness::sink(&ok, sizeof(ok));
    };

    dec();
    assert(target.name() == p.name() && target.id() == p.id()
           && target.email() == p.email());

    harness::measure("protobuf", enc, dec, BENCH_ROUNDS, BENCH_PER_ROUND,
                     BENCH_WARMUP, wire.size());
    return 0;
}
EOF

cat > "$DIR/bench_config.hpp" <<EOF
#define BENCH_ROUNDS    $ROUNDS
#define BENCH_PER_ROUND $PER_ROUND
#define BENCH_WARMUP    $WARMUP
EOF

cp "$REPO/benchmarks/bench_harness.hpp" "$DIR/"

# --- build ------------------------------------------------------------------
# The ctpb arm must not be given -stdlib=libstdc++: that hides <meta> and the
# #error above fires. That is deliberate -- this arm refuses to be built as the
# fallback and call itself a reflection measurement.
CTPB_FLAGS=("$OPT" -std=c++26 -I"$REPO/include" -I"$DIR")
if echo 'int main(){}' | "$CXX" -std=c++26 -freflection -x c++ - -o /dev/null 2>/dev/null; then
    CTPB_FLAGS+=(-freflection)
fi

echo "=== throughput, one program per arm ==="
echo "compiler: $($CXX --version | head -1)"
echo "protoc:   $(protoc --version)"
echo "config:   reps=$REPS rounds=$ROUNDS per_round=$PER_ROUND warmup=$WARMUP opt=$OPT"

if ! "$CXX" "${CTPB_FLAGS[@]}" "$DIR/arm_ctpb.cpp" -o "$DIR/arm_ctpb" 2> "$DIR/ctpb_build.log"; then
    echo "FAILED to build the ctpb arm:" >&2; tail -25 "$DIR/ctpb_build.log" >&2; exit 1
fi

PROTOBUF_CFLAGS=$(pkg-config --cflags protobuf 2>/dev/null || true)
PROTOBUF_LIBS=$(pkg-config --libs protobuf 2>/dev/null || echo -lprotobuf)
PB_STDLIB=""
for stdlib in "" "-stdlib=libstdc++"; do
    if $CXX $OPT -std=c++17 $stdlib $PROTOBUF_CFLAGS -I"$DIR" \
            "$DIR/arm_protobuf.cpp" "$DIR/person.pb.cc" $PROTOBUF_LIBS \
            -o "$DIR/arm_protobuf" > "$DIR/pb_build.log" 2>&1; then
        PB_STDLIB="${stdlib:-compiler default}"
        break
    fi
done
if [ -z "$PB_STDLIB" ]; then
    echo "FAILED to build the protobuf arm:" >&2; tail -25 "$DIR/pb_build.log" >&2; exit 1
fi
echo "ctpb arm stdlib:     compiler default (libc++; -stdlib=libstdc++ would close the P2996 gate)"
echo "protobuf arm stdlib: $PB_STDLIB"

# --- run, alternating the arms ---------------------------------------------
for rep in $(seq 1 "$REPS"); do
    for arm in ctpb protobuf; do
        "$DIR/arm_$arm" | sed "s/^/rep=$rep /"
    done
done

# --- wire parity ------------------------------------------------------------
# Two programs are only comparable if they agree on the bytes. They encode the
# same message, so the files must be identical; if they are not, the throughput
# numbers describe different work and mean nothing.
if cmp -s "$DIR/wire_ctpb.bin" "$DIR/wire_protobuf.bin"; then
    echo "wire_parity=identical bytes=$(wc -c < "$DIR/wire_ctpb.bin" | tr -d ' ')"
else
    echo "wire_parity=DIFFERENT -- the arms are not encoding the same thing" >&2
    od -An -tx1 "$DIR/wire_ctpb.bin" | head -3 >&2
    od -An -tx1 "$DIR/wire_protobuf.bin" | head -3 >&2
    exit 1
fi
