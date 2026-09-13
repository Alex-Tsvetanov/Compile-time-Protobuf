#include <cstdio>
#include <cstdint>
#include <type_traits>
#include <ctpb/ctpb.hpp>
inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message Inner { int32 a = 1; string b = 2; }
    message Outer { int32 id = 1; Inner inner = 2; string tag = 3; }
)";
#if !CTPB_HAS_P2996
#  error "this probe is about the P2996 path"
#endif
consteval { ctpb::define_message<proto, "Inner">(); }
consteval { ctpb::define_message<proto, "Outer">(); }
using Inner = ctpb::message_t<proto, "Inner">;
using Outer = ctpb::message_t<proto, "Outer">;
#include <vector>
#include <span>
int main() {
    Outer o{};
    o.id = 1; o.inner = 7;
    std::vector<std::byte> buf(ctpb::byte_size(o));
    ctpb::encode_to_ptr(o, buf.data());
    std::printf("gate=%d  wire bytes (%zu):", (int)CTPB_HAS_P2996, buf.size());
    for (auto b : buf) std::printf(" %02x", (unsigned)b);
    std::printf("\n");
    std::printf("field 2 tag byte: expected 0x12 for a length-delimited submessage,"
                " 0x10 for a varint\n");
    return 0;
}
