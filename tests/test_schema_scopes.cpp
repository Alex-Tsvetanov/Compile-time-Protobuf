// tests/test_schema_scopes.cpp
//
// Field types are resolved through the enclosing messages, then the top
// level, and may carry the package prefix. Before this, only top-level
// types matched, so a nested enum or message fell back to std::string.

#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string scoped_proto = R"(
    syntax = "proto3";
    package scoped;

    message Outer {
      enum Status { UNKNOWN = 0; ACTIVE = 1; }
      message Sub { int32 n = 1; }

      Status state = 1;
      Sub sub = 2;
      scoped.Other qualified = 3;
      Outer.Sub dotted = 4;
      scoped.Outer.Status full_enum = 5;
      repeated Sub subs = 6;
    }

    message Other { int32 m = 1; }
)";
using OuterSchema = ctpb::parse<scoped_proto, "Outer">;
using OuterMsg = ctpb::message<scoped_proto, "Outer">;

constexpr auto f_state = OuterSchema::get_field(0);
constexpr auto f_sub = OuterSchema::get_field(1);
constexpr auto f_qualified = OuterSchema::get_field(2);
constexpr auto f_dotted = OuterSchema::get_field(3);
constexpr auto f_full_enum = OuterSchema::get_field(4);

static_assert(f_state.is_enum && !f_state.is_submessage);
static_assert(f_sub.is_submessage && !f_sub.is_enum && f_sub.resolved_type == "Sub");
static_assert(f_qualified.is_submessage && f_qualified.resolved_type == "Other");
static_assert(f_dotted.is_submessage && f_dotted.resolved_type == "Outer.Sub");
static_assert(f_full_enum.is_enum);

int main() {
    OuterMsg m;
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(m.get<"state">())>, std::int32_t>);
    static_assert(!std::is_same_v<std::remove_cvref_t<decltype(m.get<"sub">())>, std::string>);

    m.set<"state">(1);
    m.get<"sub">().set<"n">(5);
    m.get<"qualified">().set<"m">(3);
    m.get<"dotted">().set<"n">(7);
    m.get<"full_enum">() = 1;

    const auto wire = ctpb::encode(m);
    // state: 08 01. sub: 12 02 08 05. qualified: 1A 02 08 03. dotted: 22 02 08 07. full_enum: 28 01.
    const unsigned char want[] = {0x08, 0x01, 0x12, 0x02, 0x08, 0x05, 0x1A, 0x02, 0x08, 0x03,
                                  0x22, 0x02, 0x08, 0x07, 0x28, 0x01};
    assert(wire.size() == sizeof(want));
    for (std::size_t i = 0; i < sizeof(want); ++i) {
        assert(static_cast<unsigned char>(wire[i]) == want[i]);
    }

    auto back = ctpb::decode<OuterMsg>(wire);
    assert(back.has_value());
    assert(back->get<"state">() == 1);
    assert(back->get<"sub">().get<"n">() == 5);
    assert(back->get<"qualified">().get<"m">() == 3);
    assert(back->get<"dotted">().get<"n">() == 7);
    assert(back->get<"full_enum">() == 1);

    std::printf("test_schema_scopes: OK\n");
    return 0;
}
