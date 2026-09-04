// benchmarks/bench_runtime.cpp
// Throughput for encode/decode on a representative message.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <ctpb/ctpb.hpp>

struct BenchPerson {
    std::string  name = "Alexander Tsvetanov";
    std::int32_t id = 42000;
    std::string  email = "alexander.tsvetanov@example.org";
    std::vector<std::int64_t> phone_numbers{111222333, 444555666, 777888999};

    void encode_into(std::vector<std::byte>& out) const {
        if (!name.empty()) {
            ctpb::wire::write_tag(out, 1, ctpb::wire::wire_type::len);
            ctpb::wire::write_string(out, name);
        }
        if (id != 0) {
            ctpb::wire::write_tag(out, 2, ctpb::wire::wire_type::varint);
            ctpb::wire::write_varint(out, static_cast<std::uint64_t>(id));
        }
        if (!email.empty()) {
            ctpb::wire::write_tag(out, 3, ctpb::wire::wire_type::len);
            ctpb::wire::write_string(out, email);
        }
        for (auto num : phone_numbers) {
            ctpb::wire::write_tag(out, 4, ctpb::wire::wire_type::varint);
            ctpb::wire::write_varint(out, static_cast<std::uint64_t>(num));
        }
    }

    void decode_from(std::span<const std::byte> bytes, ctpb::wire::decode_error& err) {
        ctpb::wire::reader r{bytes};
        phone_numbers.clear();
        while (!r.empty()) {
            std::int64_t num = 0;
            ctpb::wire::wire_type wt = ctpb::wire::wire_type::varint;
            if (!r.read_tag(num, wt)) { err.err = ctpb::wire::decode_error::bad_varint; return; }
            if (num == 1 && wt == ctpb::wire::wire_type::len) {
                std::string_view sv;
                r.read_string(sv);
                name = std::string(sv);
            } else if (num == 2 && wt == ctpb::wire::wire_type::varint) {
                std::uint64_t v = 0;
                r.read_varint(v);
                id = static_cast<std::int32_t>(v);
            } else if (num == 3 && wt == ctpb::wire::wire_type::len) {
                std::string_view sv;
                r.read_string(sv);
                email = std::string(sv);
            } else if (num == 4 && wt == ctpb::wire::wire_type::varint) {
                std::uint64_t v = 0;
                r.read_varint(v);
                phone_numbers.push_back(static_cast<std::int64_t>(v));
            } else {
                r.skip_field(wt);
            }
        }
    }
};

int main() {
    using clock = std::chrono::steady_clock;
    constexpr int N = 200000;

    BenchPerson person;
    std::vector<std::byte> sample_buf = ctpb::encode(person);
    std::size_t payload_bytes = sample_buf.size();

    // 1. Benchmark Encode
    auto t0 = clock::now();
    std::size_t total_encoded_bytes = 0;
    for (int i = 0; i < N; ++i) {
        std::vector<std::byte> out = ctpb::encode(person);
        total_encoded_bytes += out.size();
    }
    auto t1 = clock::now();
    auto enc_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double enc_ops_sec = (static_cast<double>(N) * 1e6) / enc_us;
    double enc_mb_sec = (static_cast<double>(total_encoded_bytes) / (1024.0 * 1024.0)) / (enc_us / 1e6);

    // 2. Benchmark Decode
    auto t2 = clock::now();
    std::size_t total_decoded_items = 0;
    for (int i = 0; i < N; ++i) {
        auto dec = ctpb::decode<BenchPerson>(sample_buf);
        if (dec.has_value()) {
            total_decoded_items++;
        }
    }
    auto t3 = clock::now();
    auto dec_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    double dec_ops_sec = (static_cast<double>(N) * 1e6) / dec_us;
    double dec_mb_sec = (static_cast<double>(total_decoded_items * payload_bytes) / (1024.0 * 1024.0)) / (dec_us / 1e6);

    std::printf("=====================================================\n");
    std::printf("  ctpb Wire Serialization & Deserialization Benchmark\n");
    std::printf("=====================================================\n");
    std::printf("Iterations           : %d\n", N);
    std::printf("Single Message Size  : %zu bytes\n", payload_bytes);
    std::printf("-----------------------------------------------------\n");
    std::printf("Encode Latency       : %.2f ns/op\n", (static_cast<double>(enc_us) * 1000.0) / N);
    std::printf("Encode Throughput    : %.2f M ops/sec (%.2f MiB/sec)\n", enc_ops_sec / 1e6, enc_mb_sec);
    std::printf("-----------------------------------------------------\n");
    std::printf("Decode Latency       : %.2f ns/op\n", (static_cast<double>(dec_us) * 1000.0) / N);
    std::printf("Decode Throughput    : %.2f M ops/sec (%.2f MiB/sec)\n", dec_ops_sec / 1e6, dec_mb_sec);
    std::printf("=====================================================\n");

    return 0;
}
