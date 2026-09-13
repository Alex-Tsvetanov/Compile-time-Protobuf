#!/bin/bash
set -e

DIR="/tmp/eval_runtime_comp"
rm -rf "$DIR"
mkdir -p "$DIR"
cd "$DIR"

# 1. Generate Protobuf schema
cat << 'EOF' > "$DIR/person.proto"
syntax = "proto3";
package tutorial;

message Person {
  string name = 1;
  int32 id = 2;
  string email = 3;
}
EOF

protoc --cpp_out="$DIR" --proto_path="$DIR" "$DIR/person.proto"

# 2. Write unified benchmark C++ source
cat << 'EOF' > "$DIR/bench_comparison.cpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <cassert>

#include <ctpb/ctpb.hpp>
#include "person.pb.h"

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

int main() {
    using clock = std::chrono::steady_clock;
    constexpr int N = 1000000;
    constexpr int WARMUP = 20000;

    std::string name_val = "Alex I. Tsvetanov";
    int32_t id_val = 42000;
    std::string email_val = "atsvetanov@tu-sofia.bg";

    // 1. Google Protobuf Instance
    tutorial::Person google_p;
    google_p.set_name(name_val);
    google_p.set_id(id_val);
    google_p.set_email(email_val);

    std::string google_wire;
    google_p.SerializeToString(&google_wire);

    // 2. ctpb Instance
    CtpbPerson ctpb_p;
    ctpb_p.name = name_val;
    ctpb_p.id = id_val;
    ctpb_p.email = email_val;

    std::vector<std::byte> ctpb_wire = ctpb::encode(ctpb_p);
    assert(ctpb_wire.size() == google_wire.size());
    std::size_t payload_bytes = ctpb_wire.size();

    // Verify roundtrip decoding parity
    tutorial::Person check_google;
    check_google.ParseFromArray(ctpb_wire.data(), ctpb_wire.size());
    assert(check_google.name() == name_val);

    auto check_ctpb = ctpb::decode<CtpbPerson>(std::as_bytes(std::span(google_wire)));
    assert(check_ctpb.has_value() && check_ctpb->name == name_val);

    // ==========================================
    // The four benchmarks, interleaved and barriered
    // ==========================================
    //
    // Three things this loop does that the earlier one did not, each of which could
    // account for the whole reported margin on a 47-byte message.
    //
    // A barrier on both arms. ctpb's encoder is a constexpr-derived function that
    // inlines completely, and its result was assigned to a local nothing read. At -O3
    // the optimiser may drop part of that work. protobuf's SerializeToString is an
    // opaque call into a shared library and cannot be dropped, so the asymmetry ran one
    // way only: the faster arm was the one the compiler was allowed to help.
    //
    // Preallocated output on both arms. SerializeToString built a fresh std::string per
    // iteration and ctpb::encode returned a fresh vector, so both arms were partly an
    // allocator benchmark. They allocate symmetrically, which is fairer than it looks,
    // but neither number was the encoder.
    //
    // Interleaving. The four blocks ran one after another, so any frequency or thermal
    // drift over the run landed unevenly: whichever arm ran last carried it. Now the
    // arms alternate inside one loop over rounds.

    // Keeps a value observable to the compiler without doing anything to it.
    auto sink = [](const void* p, std::size_t n) {
        asm volatile("" :: "r"(p), "r"(n) : "memory");
    };

    // Output buffers, allocated once and reused by both arms.
    std::string google_buf;
    google_buf.resize(google_wire.size());
    std::vector<std::byte> ctpb_buf(ctpb_wire.size());

    // Decode targets, constructed once and reused. A fresh protobuf message per
    // iteration costs arena and descriptor work that is not parsing.
    tutorial::Person google_dec;

    double google_enc_ns = 0, ctpb_enc_ns = 0, google_dec_ns = 0, ctpb_dec_ns = 0;

    constexpr int ROUNDS = 20;
    constexpr int PER_ROUND = N / ROUNDS;

    // Warmup, both arms, before either is timed.
    for (int i = 0; i < WARMUP; ++i) {
        google_p.SerializeToArray(google_buf.data(), static_cast<int>(google_buf.size()));
        sink(google_buf.data(), google_buf.size());
        ctpb::encode_to_ptr(ctpb_p, ctpb_buf.data());
        sink(ctpb_buf.data(), ctpb_buf.size());
        google_dec.Clear();
        google_dec.ParseFromString(google_wire);
        sink(&google_dec, sizeof(google_dec));
        auto d = ctpb::decode<CtpbPerson>(std::as_bytes(std::span(google_wire)));
        sink(&d, sizeof(d));
    }

    for (int r = 0; r < ROUNDS; ++r) {
        auto a0 = clock::now();
        for (int i = 0; i < PER_ROUND; ++i) {
            google_p.SerializeToArray(google_buf.data(), static_cast<int>(google_buf.size()));
            sink(google_buf.data(), google_buf.size());
        }
        auto a1 = clock::now();
        for (int i = 0; i < PER_ROUND; ++i) {
            ctpb::encode_to_ptr(ctpb_p, ctpb_buf.data());
            sink(ctpb_buf.data(), ctpb_buf.size());
        }
        auto a2 = clock::now();
        for (int i = 0; i < PER_ROUND; ++i) {
            google_dec.Clear();
            google_dec.ParseFromString(google_wire);
            sink(&google_dec, sizeof(google_dec));
        }
        auto a3 = clock::now();
        for (int i = 0; i < PER_ROUND; ++i) {
            auto d = ctpb::decode<CtpbPerson>(std::as_bytes(std::span(google_wire)));
            sink(&d, sizeof(d));
        }
        auto a4 = clock::now();

        google_enc_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(a1 - a0).count();
        ctpb_enc_ns   += std::chrono::duration_cast<std::chrono::nanoseconds>(a2 - a1).count();
        google_dec_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(a3 - a2).count();
        ctpb_dec_ns   += std::chrono::duration_cast<std::chrono::nanoseconds>(a4 - a3).count();
    }

    google_enc_ns /= static_cast<double>(ROUNDS * PER_ROUND);
    ctpb_enc_ns   /= static_cast<double>(ROUNDS * PER_ROUND);
    google_dec_ns /= static_cast<double>(ROUNDS * PER_ROUND);
    ctpb_dec_ns   /= static_cast<double>(ROUNDS * PER_ROUND);

    // Metrics computation
    double ctpb_enc_mops = 1e3 / ctpb_enc_ns;
    double ctpb_enc_mbs = (ctpb_enc_mops * payload_bytes * 1e6) / (1024.0 * 1024.0);

    double google_enc_mops = 1e3 / google_enc_ns;
    double google_enc_mbs = (google_enc_mops * payload_bytes * 1e6) / (1024.0 * 1024.0);

    double ctpb_dec_mops = 1e3 / ctpb_dec_ns;
    double ctpb_dec_mbs = (ctpb_dec_mops * payload_bytes * 1e6) / (1024.0 * 1024.0);

    double google_dec_mops = 1e3 / google_dec_ns;
    double google_dec_mbs = (google_dec_mops * payload_bytes * 1e6) / (1024.0 * 1024.0);

    std::printf("===================================================================\n");
    std::printf("RUNTIME PERFORMANCE COMPARISON: ctpb vs. Google libprotobuf\n");
    std::printf("===================================================================\n");
    std::printf("Benchmark Iterations : %d\n", N);
    std::printf("Message Payload Size : %zu bytes\n", payload_bytes);
    std::printf("-------------------------------------------------------------------\n");
    std::printf("1. SERIALIZATION / ENCODE:\n");
    std::printf("   - ctpb            : %6.2f ns/op | %5.2f MOps/s | %6.2f MiB/s\n", ctpb_enc_ns, ctpb_enc_mops, ctpb_enc_mbs);
    std::printf("   - Google Protobuf : %6.2f ns/op | %5.2f MOps/s | %6.2f MiB/s\n", google_enc_ns, google_enc_mops, google_enc_mbs);
    std::printf("   -> Relative Ratio : %.2fx\n", google_enc_ns / ctpb_enc_ns);
    std::printf("-------------------------------------------------------------------\n");
    std::printf("2. DESERIALIZATION / DECODE:\n");
    std::printf("   - ctpb            : %6.2f ns/op | %5.2f MOps/s | %6.2f MiB/s\n", ctpb_dec_ns, ctpb_dec_mops, ctpb_dec_mbs);
    std::printf("   - Google Protobuf : %6.2f ns/op | %5.2f MOps/s | %6.2f MiB/s\n", google_dec_ns, google_dec_mops, google_dec_mbs);
    std::printf("   -> Speedup (ctpb) : %.2fx (%.1f%% faster)\n", google_dec_ns / ctpb_dec_ns, ((google_dec_ns - ctpb_dec_ns) / google_dec_ns) * 100.0);
    std::printf("===================================================================\n");

    return 0;
}
EOF

# 3. Compile and execute
clang++ -O3 -std=c++26 -freflection -stdlib=libstdc++ -I/workspace/include -I"$DIR" \
    "$DIR/person.pb.cc" "$DIR/bench_comparison.cpp" -lprotobuf -o "$DIR/bench_comparison"

"$DIR/bench_comparison"
