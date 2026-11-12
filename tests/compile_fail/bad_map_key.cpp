// expect: a map key must be an integer type, bool or string
// protobuf does not allow float, double, bytes, enum or message map keys.

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message M { map<double, int32> m = 1; }
)";

int main() {
    ctpb::message<proto, "M"> m;
    return static_cast<int>(ctpb::encode(m).size());
}
