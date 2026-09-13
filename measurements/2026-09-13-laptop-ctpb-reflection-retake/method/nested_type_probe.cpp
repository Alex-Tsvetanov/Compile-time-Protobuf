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
static_assert(std::is_same_v<decltype(Outer{}.inner), std::int32_t>);
static_assert(std::is_same_v<decltype(Outer{}.id), std::int32_t>);
static_assert(!std::is_same_v<decltype(Outer{}.inner), Inner>);
int main() {
    std::printf("gate=%d\n", (int)CTPB_HAS_P2996);
    std::printf("Outer.inner is int32_t, NOT Inner  -- sizeof(Outer)=%zu sizeof(Inner)=%zu\n",
                sizeof(Outer), sizeof(Inner));
    Outer o{}; o.inner = 7;           // compiles only because the field is a scalar
    std::printf("assigned o.inner = %d\n", (int)o.inner);
    return 0;
}
