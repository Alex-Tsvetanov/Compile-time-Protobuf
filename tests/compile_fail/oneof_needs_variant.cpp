// expect: a oneof member must be a std::variant
// A hand-written reflect_base struct must hold a oneof as a std::variant.

#include <cstdint>
#include <tuple>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message M { oneof v { int32 a = 1; string b = 2; } }
)";

struct Hand : ctpb::reflect_base<ctpb::parse<proto, "M">, Hand> {
    std::int32_t v = 0;
    auto reflect_members() { return std::tie(v); }
};

int main() {
    Hand h;
    return static_cast<int>(ctpb::encode(h).size());
}
