// tests/test_fixed_width.cpp
//
// fixed32, sfixed32, fixed64 and sfixed64 must use wire types 5 and 1 with
// 4 or 8 raw little-endian bytes. Packed repeated forms hold the same raw
// bytes. The expected bytes below are written by hand from the protobuf spec.

#include <cassert>
#include <cstdio>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string fixed_proto = R"(
    syntax = "proto3";
    message Fixed {
        fixed32 a = 1;
        sfixed32 b = 2;
        fixed64 c = 3;
        sfixed64 d = 4;
        repeated fixed32 e = 5;
        repeated sfixed64 f = 6;
    }
)";
using FixedSchema = ctpb::parse<fixed_proto, "Fixed">;
struct FixedMsg : ctpb::reflect_base<FixedSchema, FixedMsg> {
    std::uint32_t a = 0;
    std::int32_t b = 0;
    std::uint64_t c = 0;
    std::int64_t d = 0;
    std::vector<std::uint32_t> e;
    std::vector<std::int64_t> f;
    auto reflect_members() { return std::tie(a, b, c, d, e, f); }
};

static std::vector<std::byte> bytes(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    for (int x : v) out.push_back(static_cast<std::byte>(x));
    return out;
}

int main() {
    FixedMsg m;
    m.a = 1;
    m.b = -2;
    m.c = 0x0102030405060708ULL;
    m.d = -1;
    m.e = {1, 0x01020304u};
    m.f = {-1};

    const auto expected = bytes({
        0x0D, 0x01, 0x00, 0x00, 0x00,
        0x15, 0xFE, 0xFF, 0xFF, 0xFF,
        0x19, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x21, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x2A, 0x08, 0x01, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
        0x32, 0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

    // Size and encode agree with each other and with the spec bytes.
    auto wire = ctpb::encode(m);
    assert(ctpb::byte_size(m) == expected.size());
    assert(wire == expected);

    // Decode of the spec bytes.
    auto back = ctpb::decode<FixedMsg>(expected);
    assert(back.has_value());
    assert(back->a == m.a);
    assert(back->b == m.b);
    assert(back->c == m.c);
    assert(back->d == m.d);
    assert(back->e == m.e);
    assert(back->f == m.f);

    // Unpacked repeated fixed32 (tag 0x2D, wire type 5) is also accepted.
    auto unpacked = ctpb::decode<FixedMsg>(bytes({
        0x2D, 0x07, 0x00, 0x00, 0x00, 0x2D, 0x08, 0x00, 0x00, 0x00}));
    assert(unpacked.has_value());
    assert((unpacked->e == std::vector<std::uint32_t>{7, 8}));

    std::printf("test_fixed_width: OK\n");
    return 0;
}
