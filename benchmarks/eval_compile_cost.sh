#!/usr/bin/env bash
#
# What each approach costs at build time, and what it leaves in the binary.
#
# The runtime comparison answers half a question. ctpb moves work from run time
# to compile time, so the build has to be measured as well, and on both sides:
# protoc has to be invoked and its output compiled, while ctpb parses the schema
# during constant evaluation of the translation unit that uses it. Nothing here
# is a proxy for the other side's cost -- each arm is the whole path from a
# .proto file to a linked program, timed the same way.
#
# Arms, per schema size:
#   ctpb    : compile one TU that ingests the schema and encodes/decodes it.
#   protoc  : run protoc, compile the generated .pb.cc, compile the equivalent
#             user TU, and link, all counted.
#
# Also records exact section sizes from the section headers (not the `size`
# summary, whose `text` column folds .rodata and the unwind tables into one
# number), so a footprint table can name .text and mean it.
#
# Usage:  TRIALS=7 FIELDS="5 25 100" ./eval_compile_cost.sh
# Output: one `trial=` line per measurement on stdout, sizes at the end.
set -euo pipefail

TRIALS=${TRIALS:-7}
FIELDS=${FIELDS:-"5 25 100"}
CXX=${CXX:-clang++}
INCLUDE_DIR=${INCLUDE_DIR:-/workspace/include}
OPT=${OPT:--O3}
DIR=${DIR:-/tmp/eval_compile_cost}
PROTOC=${PROTOC:-protoc}

rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"

command -v "$PROTOC" >/dev/null || { echo "protoc not found" >&2; exit 1; }

# Wall-clock seconds for one command, to millisecond resolution. BSD date has
# no %N, so fall back to python3 there rather than silently timing in seconds.
if [ "$(date +%s%N)" = "$(date +%s)N" ]; then
    now_ns() { python3 -c 'import time; print(time.time_ns())'; }
else
    now_ns() { date +%s%N; }
fi

# A timing harness that swallows the exit status measures nothing and says so
# to nobody: this repository has already shipped one campaign that exited 0
# with an empty results directory. A failing step here stops the run and prints
# what the tool said.
time_cmd() {
    local start end status=0
    start=$(now_ns)
    "$@" > "$DIR/last_cmd.log" 2>&1 || status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAILED (exit $status): $*" >&2
        tail -20 "$DIR/last_cmd.log" >&2
        exit "$status"
    fi
    end=$(now_ns)
    awk -v s="$start" -v e="$end" 'BEGIN { printf "%.3f", (e - s) / 1000000000 }'
}

gen_proto() {   # $1 = field count
    local n="$1"
    {
        echo 'syntax = "proto3";'
        echo 'package bench;'
        echo "message Payload {"
        for i in $(seq 1 "$n"); do echo "  int32 f$i = $i;"; done
        echo "}"
    } > "$DIR/bench_$n.proto"
}

gen_ctpb_tu() { # $1 = field count
    local n="$1"
    {
        echo '#include <cstdio>'
        echo '#include <vector>'
        echo '#include <ctpb/ctpb.hpp>'
        echo "inline constexpr ctpb::fixed_string schema = R\"PROTO("
        cat "$DIR/bench_$n.proto"
        echo ')PROTO";'
        echo '#if CTPB_HAS_P2996'
        echo 'consteval { ctpb::define_message<schema, "Payload">(); }'
        echo 'using Payload = ctpb::message_t<schema, "Payload">;'
        echo '#else'
        echo 'using PayloadSchema = ctpb::parse<schema, "Payload">;'
        echo 'struct Payload : ctpb::reflect_base<PayloadSchema, Payload> {'
        for i in $(seq 1 "$n"); do echo "    std::int32_t f$i = 0;"; done
        printf '    auto reflect_members() { return std::tie('
        for i in $(seq 1 "$n"); do printf 'f%s' "$i"; [ "$i" -lt "$n" ] && printf ', '; done
        echo '); }'
        echo '};'
        echo '#endif'
        echo 'int main() {'
        echo '    Payload p{}; p.f1 = 7;'
        echo '    auto wire = ctpb::encode(p);'
        echo '    Payload back{};'
        echo '    auto err = ctpb::decode_into(back, wire);'
        echo '    std::printf("%zu %d %d\n", wire.size(), (int)back.f1, (int)!!err);'
        echo '}'
    } > "$DIR/ctpb_$n.cpp"
}

gen_protoc_tu() { # $1 = field count
    local n="$1"
    {
        echo '#include <cstdio>'
        echo '#include <string>'
        echo '#include "bench_'"$n"'.pb.h"'
        echo 'int main() {'
        echo '    bench::Payload p; p.set_f1(7);'
        echo '    std::string wire; p.SerializeToString(&wire);'
        echo '    bench::Payload back; bool ok = back.ParseFromString(wire);'
        echo '    std::printf("%zu %d %d\n", wire.size(), back.f1(), (int)!ok);'
        echo '}'
    } > "$DIR/protoc_$n.cpp"
}

CTPB_FLAGS=("$OPT" -std=c++26 -I"$INCLUDE_DIR")
if echo 'int main(){}' | "$CXX" -std=c++26 -freflection -x c++ - -o /dev/null 2>/dev/null; then
    CTPB_FLAGS+=(-freflection)
fi
PROTOBUF_CFLAGS=$(pkg-config --cflags protobuf 2>/dev/null || true)
PROTOBUF_LIBS=$(pkg-config --libs protobuf 2>/dev/null || echo -lprotobuf)

# Which standard library the protobuf arm has to be built against. On the
# P2996 toolchain the two arms cannot share one: <meta> ships only in libc++,
# while a distribution libprotobuf is built against libstdc++, so linking the
# generated code against libc++ fails on std::__1 symbols. Probe it rather
# than assume, and say which one was used -- it is a difference between the
# arms that a build-time comparison has to disclose.
PROTOBUF_STDLIB=""
probe_protobuf_link() {
    cat > "$DIR/probe.proto" <<'EOF'
syntax = "proto3";
package probe;
message P { int32 x = 1; }
EOF
    "$PROTOC" --cpp_out="$DIR" --proto_path="$DIR" "$DIR/probe.proto"
    cat > "$DIR/probe.cpp" <<'EOF'
#include "probe.pb.h"
int main() { probe::P p; p.set_x(1); return p.x() - 1; }
EOF
    for stdlib in "" "-stdlib=libstdc++"; do
        if $CXX $OPT -std=c++17 $stdlib $PROTOBUF_CFLAGS -I"$DIR" \
                "$DIR/probe.cpp" "$DIR/probe.pb.cc" $PROTOBUF_LIBS \
                -o "$DIR/probe.bin" > "$DIR/probe.log" 2>&1; then
            PROTOBUF_STDLIB="$stdlib"
            return 0
        fi
    done
    echo "cannot build a protobuf program with this compiler:" >&2
    tail -15 "$DIR/probe.log" >&2
    exit 1
}
probe_protobuf_link

echo "=== build cost: one .proto file to a linked program ==="
echo "compiler: $($CXX --version | head -1)"
echo "protoc:   $($PROTOC --version)"
echo "flags:    ${CTPB_FLAGS[*]}"
echo "protobuf arm stdlib: ${PROTOBUF_STDLIB:-compiler default}"

for n in $FIELDS; do
    gen_proto "$n"; gen_ctpb_tu "$n"; gen_protoc_tu "$n"

    for trial in $(seq 0 $((TRIALS - 1))); do
        # ctpb: one translation unit, schema ingested during compilation.
        ctpb_s=$(time_cmd "$CXX" "${CTPB_FLAGS[@]}" "$DIR/ctpb_$n.cpp" -o "$DIR/ctpb_$n.bin")

        # protoc: generator run, generated code compiled, user code compiled, linked.
        gen_s=$(time_cmd "$PROTOC" --cpp_out="$DIR" --proto_path="$DIR" "$DIR/bench_$n.proto")
        pbcc_s=$(time_cmd "$CXX" "$OPT" -std=c++17 $PROTOBUF_STDLIB $PROTOBUF_CFLAGS -I"$DIR" -c "$DIR/bench_$n.pb.cc" -o "$DIR/bench_$n.pb.o")
        user_s=$(time_cmd "$CXX" "$OPT" -std=c++17 $PROTOBUF_STDLIB $PROTOBUF_CFLAGS -I"$DIR" -c "$DIR/protoc_$n.cpp" -o "$DIR/protoc_$n.o")
        link_s=$(time_cmd "$CXX" $PROTOBUF_STDLIB "$DIR/protoc_$n.o" "$DIR/bench_$n.pb.o" $PROTOBUF_LIBS -o "$DIR/protoc_$n.bin")
        protoc_total=$(awk -v a="$gen_s" -v b="$pbcc_s" -v c="$user_s" -v d="$link_s" \
                           'BEGIN { printf "%.3f", a + b + c + d }')

        echo "trial=$trial fields=$n ctpb_build_s=$ctpb_s protoc_build_s=$protoc_total" \
             "protoc_generate_s=$gen_s protoc_compile_generated_s=$pbcc_s" \
             "protoc_compile_user_s=$user_s protoc_link_s=$link_s"
    done

    echo "generated_lines fields=$n ctpb=0 protoc=$(cat "$DIR/bench_$n.pb.h" "$DIR/bench_$n.pb.cc" | wc -l)"
done

# --- footprint, per section, from the section headers -----------------------
section_bytes() {  # $1 = binary, $2 = section name
    # `size -A` reports per-section sizes in decimal, which is the number the
    # footprint table wants. The plain `size` summary is not: its `text` column
    # is .text plus .rodata plus the unwind tables, so quoting it as .text
    # overstates the code by whatever constant data the program carries.
    if command -v size > /dev/null 2>&1 && size -A "$1" > /dev/null 2>&1; then
        size -A "$1" | awk -v s="$2" '$1 == s { print $2; found = 1 } END { if (!found) print "NA" }'
    else
        echo "NA"
    fi
}

echo "=== footprint (stripped, exact section sizes) ==="
for n in $FIELDS; do
    for arm in ctpb protoc; do
        bin="$DIR/${arm}_$n.bin"
        [ -f "$bin" ] || continue
        strip -s "$bin" -o "$bin.stripped" 2>/dev/null || cp "$bin" "$bin.stripped"
        file_bytes=$(wc -c < "$bin.stripped" | tr -d ' ')
        echo "footprint fields=$n arm=$arm file_bytes=$file_bytes" \
             "text_bytes=$(section_bytes "$bin.stripped" .text)" \
             "rodata_bytes=$(section_bytes "$bin.stripped" .rodata)" \
             "data_bytes=$(section_bytes "$bin.stripped" .data)"
    done
done
