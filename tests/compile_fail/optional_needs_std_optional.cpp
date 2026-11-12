// expect: a proto3 optional field must be a std::optional member
// A hand-written reflect_base struct must keep presence for an optional field.

#include <cstdint>
#include <tuple>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message M { optional int32 a = 1; }
)";

struct Hand : ctpb::reflect_base<ctpb::parse<proto, "M">, Hand> {
    std::int32_t a = 0;
    auto reflect_members() { return std::tie(a); }
};

int main() {
    Hand h;
    return static_cast<int>(ctpb::encode(h).size());
}
