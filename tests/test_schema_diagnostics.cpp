// tests/test_schema_diagnostics.cpp
//
// Schemas that ctpb cannot turn into types must give a clear error instead
// of a wrong type. parse<>::diagnose() returns the text that the compiler
// shows. The checks run at compile time. The parser itself never throws, so
// it stays usable at run time.

#include <cstdio>
#include <string_view>

#include <ctpb/ctpb.hpp>

consteval bool says(const ctpb::schema_diagnostic& d, std::string_view part) {
    return d.failed && std::string_view(d.data(), d.size()).contains(part);
}

inline constexpr ctpb::fixed_string good_proto = R"(
    syntax = "proto3";
    message M { int32 a = 1; }
)";
static_assert(!ctpb::parse<good_proto, "M">::diagnose().failed);

// A missing field number: reported with line and column.
inline constexpr ctpb::fixed_string missing_number = R"(
    syntax = "proto3";
    message M {
      int32 a = ;
    }
)";
static_assert(says(ctpb::parse<missing_number, "M">::diagnose(), "line 4"));
static_assert(says(ctpb::parse<missing_number, "M">::diagnose(), "expected field number"));

// A missing semicolon.
inline constexpr ctpb::fixed_string missing_semicolon = R"(
    syntax = "proto3";
    message M { int32 a = 1 }
)";
static_assert(says(ctpb::parse<missing_semicolon, "M">::diagnose(), "expected field semicolon"));

// 'extend' is not supported and is reported, not silently dropped.
inline constexpr ctpb::fixed_string has_extend = R"(
    syntax = "proto3";
    message M { int32 a = 1; }
    extend Foo { int32 b = 100; }
    message Late { int32 c = 1; }
)";
static_assert(says(ctpb::parse<has_extend, "M">::diagnose(), "unsupported top-level 'extend'"));

// 'service' is skipped, so messages after it are still read.
inline constexpr ctpb::fixed_string has_service = R"(
    syntax = "proto3";
    message M { int32 a = 1; }
    service S { rpc Get (M) returns (M); }
    message Late { int32 c = 1; string d = 2; }
)";
static_assert(!ctpb::parse<has_service, "Late">::diagnose().failed);
static_assert(ctpb::parse<has_service, "Late">::field_count() == 2);

// oneof, map and proto3 optional are supported now. Each one is a single
// member of the generated type, in declaration order.
inline constexpr ctpb::fixed_string has_oneof = R"(
    syntax = "proto3";
    message M { int32 x = 5; oneof body { string a = 1; int32 b = 2; } }
)";
static_assert(!ctpb::parse<has_oneof, "M">::diagnose().failed);
static_assert(ctpb::parse<has_oneof, "M">::member_count() == 2);
static_assert(ctpb::parse<has_oneof, "M">::get_member(1).kind == ctpb::ast::member_kind::oneof);
static_assert(ctpb::parse<has_oneof, "M">::get_member(1).oneof_size == 2);
static_assert(ctpb::parse<has_oneof, "M">::get_member_part(1, 1).number == 2);

inline constexpr ctpb::fixed_string has_map = R"(
    syntax = "proto3";
    message M { map<string, int32> tags = 1; }
)";
static_assert(!ctpb::parse<has_map, "M">::diagnose().failed);
static_assert(ctpb::parse<has_map, "M">::get_member(0).kind == ctpb::ast::member_kind::map);
static_assert(ctpb::parse<has_map, "M">::get_member_part(0, 0).number == 1);
static_assert(ctpb::parse<has_map, "M">::get_member_part(0, 1).number == 2);

inline constexpr ctpb::fixed_string has_optional = R"(
    syntax = "proto3";
    message M { optional int32 a = 1; }
)";
static_assert(!ctpb::parse<has_optional, "M">::diagnose().failed);
static_assert(ctpb::parse<has_optional, "M">::get_member(0).is_optional);

// A map key must be an integer type, bool or string, as in protobuf.
inline constexpr ctpb::fixed_string bad_map_key = R"(
    syntax = "proto3";
    message M { map<double, int32> tags = 1; }
)";
static_assert(says(ctpb::parse<bad_map_key, "M">::diagnose(), "map key must be an integer type, bool or string"));

// Only the message that is asked for is checked.
static_assert(!ctpb::parse<bad_map_key, "Nothing">::diagnose().failed);

// The parser reports the first error and still returns without throwing.
consteval bool parser_records_error() {
    ctpb::parser p(R"(message M { int32 a = ; })");
    (void)p.parse_file();
    return p.failed() && p.error_location().line == 1;
}
static_assert(parser_records_error());

int main() {
    std::printf("test_schema_diagnostics: OK\n");
    return 0;
}
