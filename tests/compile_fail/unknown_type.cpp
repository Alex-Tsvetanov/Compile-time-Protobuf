// expect: unknown field type
// A field type that is not a scalar and not a visible enum or message is an error.

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message M { Missing a = 1; }
)";

int main() {
    ctpb::message<proto, "M"> m;
    return static_cast<int>(ctpb::encode(m).size());
}
