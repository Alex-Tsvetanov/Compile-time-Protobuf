// tests/test_nested.cpp

#include <cassert>
#include <cstdio>
#include <string_view>
#include <tuple>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string nested_proto = R"(
    syntax = "proto3";
    package nested_test;

    message Outer {
      message Middle {
        message Inner {
          int64 value = 1;
          enum State { UNKNOWN = 0; ACTIVE = 1; INACTIVE = 2; }
          State state = 2;
        }
        Inner inner = 1;
      }
      Middle middle = 1;
    }
)";

#if CTPB_HAS_P2996
// Zero-Macro Pure C++26 Static Reflection Message Definition:
consteval { ctpb::define_message<nested_proto, "Inner">(); }
using InnerMessage = ctpb::message_t<nested_proto, "Inner">;
#else
// C++20 Fallback for standard compilers without P2996:
using InnerSchema = ctpb::parse<nested_proto, "Inner">;
struct InnerMessage : ctpb::reflect_base<InnerSchema, InnerMessage> {
    std::int64_t value = 0;
    std::int32_t state = 0;

    auto reflect_members() { return std::tie(value, state); }
};
#endif

int main() {
    using namespace ctpb;

    // Test wire encode/decode with direct dot syntax
    InnerMessage in;
    in.value = 999;
    in.state = 1; // ACTIVE

    std::vector<std::byte> wire = ctpb::encode(in);
    assert(!wire.empty());

    auto decoded_in = ctpb::decode<InnerMessage>(wire);
    assert(decoded_in.has_value());
    assert(decoded_in->value == 999);
    assert(decoded_in->state == 1);

    std::printf("test_nested: OK\n");
    return 0;
}
