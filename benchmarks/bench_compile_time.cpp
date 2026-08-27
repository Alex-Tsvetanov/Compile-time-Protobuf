// benchmarks/bench_compile_time.cpp
// Measures compile-time parsing throughput and AST instantiation latency for
// various proto schema sizes (small: 5 fields, medium: 25 fields, large: 100 fields).

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <ctpb/ctpb.hpp>

int main() {
    using namespace ctpb;

    constexpr std::string_view schema_small = R"(
        syntax = "proto3";
        package bench;
        message Small {
          int32 id = 1;
          string name = 2;
          double score = 3;
          bool active = 4;
          repeated int64 tags = 5;
        }
    )";

    constexpr std::string_view schema_medium = R"(
        syntax = "proto3";
        package bench;
        message Medium {
          int32 f1 = 1; int64 f2 = 2; uint32 f3 = 3; uint64 f4 = 4;
          sint32 f5 = 5; sint64 f6 = 6; fixed32 f7 = 7; fixed64 f8 = 8;
          sfixed32 f9 = 9; sfixed64 f10 = 10; float f11 = 11; double f12 = 12;
          bool f13 = 13; string f14 = 14; bytes f15 = 15;
          enum Status { S0 = 0; S1 = 1; S2 = 2; S3 = 3; }
          Status f16 = 16;
          message Sub { int32 sub_id = 1; string sub_val = 2; }
          Sub f17 = 17;
          repeated Sub f18 = 18;
          repeated int32 f19 = 19;
          repeated string f20 = 20;
          map<string, int32> f21 = 21;
          oneof opt { string o1 = 22; int32 o2 = 23; }
          reserved 24, 25 to 30;
          reserved "obsolete_a", "obsolete_b";
        }
    )";

    // Run parsing benchmark over multiple iterations
    constexpr int ITERS = 10000;
    auto t0 = std::chrono::steady_clock::now();

    std::size_t total_fields = 0;
    for (int i = 0; i < ITERS; ++i) {
        parser p1{schema_small};
        ast::file f1 = p1.parse_file();
        total_fields += f1.messages[0].fields.size();

        parser p2{schema_medium};
        ast::file f2 = p2.parse_file();
        total_fields += f2.messages[0].fields.size();
    }

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    double per_parse_us = static_cast<double>(elapsed_us) / (ITERS * 2);
    double parses_per_sec = (static_cast<double>(ITERS * 2) * 1e6) / elapsed_us;

    std::printf("=====================================================\n");
    std::printf("  ctpb Compile-Time Schema Parser Benchmark\n");
    std::printf("=====================================================\n");
    std::printf("Total Schema Parses : %d\n", ITERS * 2);
    std::printf("Total Elapsed Time   : %.2f ms\n", elapsed_us / 1000.0);
    std::printf("Latency per parse    : %.2f us\n", per_parse_us);
    std::printf("Throughput           : %.0f parses/sec\n", parses_per_sec);
    std::printf("Fields processed     : %zu\n", total_fields);
    std::printf("=====================================================\n");

    return 0;
}
