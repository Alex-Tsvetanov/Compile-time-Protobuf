// tests/test_wire_groups.cpp
//
// Unknown fields with wire types 3 and 4 (groups) must be skipped, as the
// reference parser does. Field number 0 and wire types 6 and 7 are invalid
// and must be rejected when the tag is read. Expected bytes are written by
// hand from the protobuf wire format description.

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string group_proto = R"(
    syntax = "proto3";
    message Probe { int32 a = 1; string s = 2; }
)";
using ProbeSchema = ctpb::parse<group_proto, "Probe">;
struct ProbeMsg : ctpb::reflect_base<ProbeSchema, ProbeMsg> {
    std::int32_t a = 0;
    std::string s;
    auto reflect_members() { return std::tie(a, s); }
};

static std::vector<std::byte> bytes(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    for (int x : v) out.push_back(static_cast<std::byte>(x));
    return out;
}

int main() {
    using ctpb::wire::reader;
    using ctpb::wire::wire_type;

    // Unknown group 5 holding a varint and a nested group 6, then a known field.
    {
        auto in = bytes({0x2B, 0x08, 0x96, 0x01, 0x33, 0x08, 0x01, 0x34, 0x2C, 0x08, 0x07});
        auto res = ctpb::decode<ProbeMsg>(in);
        assert(res.has_value());
        assert(res->a == 7);
    }
    // A known field before the group is kept.
    {
        auto in = bytes({0x08, 0x09, 0x2B, 0x10, 0x01, 0x2C});
        auto res = ctpb::decode<ProbeMsg>(in);
        assert(res.has_value());
        assert(res->a == 9);
    }
    // The end tag must carry the same field number as the start tag.
    {
        auto in = bytes({0x2B, 0x08, 0x01, 0x34});
        assert(!ctpb::decode<ProbeMsg>(in).has_value());
    }
    // A group that never ends is an error.
    {
        auto in = bytes({0x2B, 0x08, 0x01});
        assert(!ctpb::decode<ProbeMsg>(in).has_value());
    }
    // A stray end-group tag is an error.
    {
        auto in = bytes({0x2C});
        assert(!ctpb::decode<ProbeMsg>(in).has_value());
    }
    // Field number 0.
    {
        auto in = bytes({0x00, 0x01});
        assert(!ctpb::decode<ProbeMsg>(in).has_value());
    }
    // Wire types 6 and 7.
    {
        assert(!ctpb::decode<ProbeMsg>(bytes({0x0E})).has_value());
        assert(!ctpb::decode<ProbeMsg>(bytes({0x0F})).has_value());
    }
    // The reader rejects the same tags directly.
    {
        std::int64_t num = 0;
        wire_type wt = wire_type::varint;
        auto zero = bytes({0x00});
        auto six = bytes({0x0E});
        auto ok = bytes({0x08});
        reader r0{zero}, r6{six}, rok{ok};
        assert(!r0.read_tag(num, wt));
        assert(!r6.read_tag(num, wt));
        assert(rok.read_tag(num, wt) && num == 1 && wt == wire_type::varint);
    }

    std::printf("test_wire_groups: OK\n");
    return 0;
}
