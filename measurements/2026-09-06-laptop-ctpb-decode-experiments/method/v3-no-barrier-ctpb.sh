#!/bin/bash
# Runtime comparison: ctpb vs. Google libprotobuf (full) vs. Google libprotobuf-lite,
# all three on the same machine, same binary layout, same iteration count.
#
# This exists because a review (26 Aug 2026) pointed out that eval_runtime_comparison.sh
# only compares against full libprotobuf, which carries descriptor pools, reflection and
# arenas that ctpb doesn't implement — libprotobuf-lite, which strips those, is the fair
# baseline, and it's the one the paper's own Related Work names but never benchmarks.
#
# Reports min/median/max over multiple trials per arm, not a single point estimate.
set -e

DIR="/tmp/eval_runtime_lite"
rm -rf "$DIR"
mkdir -p "$DIR"
cd "$DIR"

TRIALS=${TRIALS:-7}
N=${N:-1000000}
WARMUP=${WARMUP:-20000}

# 1. Two schema variants: identical message shape, one full-runtime, one LITE_RUNTIME.
cat << EOF > "$DIR/person_full.proto"
syntax = "proto3";
package tutorial_full;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
EOF

cat << EOF > "$DIR/person_lite.proto"
syntax = "proto3";
package tutorial_lite;
option optimize_for = LITE_RUNTIME;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
EOF

protoc --cpp_out="$DIR" --proto_path="$DIR" "$DIR/person_full.proto"
protoc --cpp_out="$DIR" --proto_path="$DIR" "$DIR/person_lite.proto"

# 2. One comparison source, parameterized by which generated header/namespace/library to use.
make_bench_source() {
    local header="$1" ns="$2" out="$3"
    cat << EOF > "$out"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <cassert>

#include <ctpb/ctpb.hpp>
#include "${header}"

inline constexpr ctpb::fixed_string sample_proto = R"(
    syntax = "proto3";
    package tutorial;

    message Person {
      string name = 1;
      int32 id = 2;
      string email = 3;
    }
)";

#if CTPB_HAS_P2996
consteval { ctpb::define_message<sample_proto, "Person">(); }
using CtpbPerson = ctpb::message_t<sample_proto, "Person">;
#else
using PersonSchema = ctpb::parse<sample_proto, "Person">;
struct CtpbPerson : ctpb::reflect_base<PersonSchema, CtpbPerson> {
    std::string   name;
    std::int32_t  id = 0;
    std::string   email;
    auto reflect_members() { return std::tie(name, id, email); }
};
#endif

// Keeps a value observable to the compiler without doing anything to it. Without this,
// ctpb's encoder inlines completely and its result is assigned to a local nothing reads,
// so at -O3 part of the work may be dropped; protobuf's call into a shared library cannot
// be. The asymmetry ran one way only, in favour of the arm the optimiser could help.
inline void sink(const void* p, std::size_t n) {
    asm volatile("" :: "r"(p), "r"(n) : "memory");
}

double run_trial_encode_ns(const auto& make_ref, const auto& serialize) {
    for (int i = 0; i < ${WARMUP}; ++i) serialize();
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < ${N}; ++i) serialize();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / static_cast<double>(${N});
}

int main() {
    std::string name_val = "Alex I. Tsvetanov";
    std::int32_t id_val = 42000;
    std::string email_val = "atsvetanov@tu-sofia.bg";

    ${ns}::Person google_p;
    google_p.set_name(name_val);
    google_p.set_id(id_val);
    google_p.set_email(email_val);
    std::string google_wire;
    google_p.SerializeToString(&google_wire);

    CtpbPerson ctpb_p;
    ctpb_p.name = name_val;
    ctpb_p.id = id_val;
    ctpb_p.email = email_val;
    std::vector<std::byte> ctpb_wire = ctpb::encode(ctpb_p);
    assert(ctpb_wire.size() == google_wire.size());
    std::size_t payload_bytes = ctpb_wire.size();

    std::printf("payload_bytes=%zu\n", payload_bytes);

    std::string enc_buf;
    enc_buf.resize(google_wire.size());
    std::vector<std::byte> ctpb_buf(ctpb_wire.size());
    ${ns}::Person dec_msg;

    for (int trial = 0; trial < ${TRIALS}; ++trial) {
        // Buffers and messages are allocated once per trial and reused. Before this each
        // lambda built a fresh string, vector or message every iteration, so both arms
        // were partly an allocator benchmark rather than an encoder one.
        double google_enc_ns = run_trial_encode_ns(nullptr, [&]{
            google_p.SerializeToArray(enc_buf.data(), static_cast<int>(enc_buf.size()));
            sink(enc_buf.data(), enc_buf.size()); });
        double ctpb_enc_ns   = run_trial_encode_ns(nullptr, [&]{
            ctpb::encode_to_ptr(ctpb_p, ctpb_buf.data()); });
        double google_dec_ns = run_trial_encode_ns(nullptr, [&]{
            dec_msg.Clear(); dec_msg.ParseFromString(google_wire);
            sink(&dec_msg, sizeof(dec_msg)); });
        double ctpb_dec_ns   = run_trial_encode_ns(nullptr, [&]{
            auto d = ctpb::decode<CtpbPerson>(ctpb_wire); });
        std::printf("trial=%d google_enc_ns=%.3f ctpb_enc_ns=%.3f google_dec_ns=%.3f ctpb_dec_ns=%.3f\n",
                    trial, google_enc_ns, ctpb_enc_ns, google_dec_ns, ctpb_dec_ns);
    }
    return 0;
}
EOF
}

make_bench_source "person_full.pb.h" "tutorial_full" "$DIR/bench_full.cpp"
make_bench_source "person_lite.pb.h" "tutorial_lite" "$DIR/bench_lite.cpp"

clang++ -O3 -std=c++26 -freflection -stdlib=libstdc++ -I/workspace/include -I"$DIR" \
    "$DIR/person_full.pb.cc" "$DIR/bench_full.cpp" -lprotobuf -o "$DIR/bench_full"

clang++ -O3 -std=c++26 -freflection -stdlib=libstdc++ -I/workspace/include -I"$DIR" \
    "$DIR/person_lite.pb.cc" "$DIR/bench_lite.cpp" -lprotobuf-lite -o "$DIR/bench_lite"

echo "=== vs full libprotobuf ==="
"$DIR/bench_full"
echo ""
echo "=== vs libprotobuf-lite ==="
"$DIR/bench_lite"
