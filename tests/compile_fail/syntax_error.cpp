// expect: proto syntax error at line 4, column 17: expected field number
// A syntax error in the proto source stops compilation with its position.

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string proto = R"(
    syntax = "proto3";
    message M {
      int32 a = ;
    }
)";

int main() {
    ctpb::message<proto, "M"> m;
    return static_cast<int>(ctpb::encode(m).size());
}
