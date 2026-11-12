// tests/test_oneof_variant.cpp
//
// Generated oneof members. A oneof is one member named after the oneof, of
// type std::variant<std::monostate, T1, T2, ...> with the alternatives in
// declaration order. Exactly the active alternative is written, even when it
// holds a zero value. On decode the last alternative on the wire wins. Two
// alternatives may share a C++ type, so they are told apart by index. Both
// code paths run: message_impl always, define_message structs with P2996.

#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "test_support.hpp"

using test_support::at;
using test_support::bytes;
using test_support::encode_checked;

inline constexpr ctpb::fixed_string oneof_proto = R"(
    syntax = "proto3";
    package variant_test;

    message Sub { int32 n = 1; }

    message R {
      int32 id = 1;
      oneof body {
        string text = 2;
        int32 code = 3;
        sint32 delta = 4;
        Sub sub = 5;
        bytes blob = 6;
      }
      string tail = 7;
    }
)";

// Member indices of R, and variant indices of the oneof `body`.
enum : std::size_t { ID, BODY, TAIL };
enum : std::size_t { NONE, TEXT, CODE, DELTA, SUB, BLOB };

template <typename R, typename Sub>
void run_oneof_tests() {
    // No alternative set: nothing is written.
    {
        R m{};
        REQUIRE(at<BODY>(m).index() == NONE);
        REQUIRE(encode_checked(m).empty());
    }

    // The active alternative is written even when it is zero or empty.
    {
        R m{};
        at<BODY>(m).template emplace<CODE>(0);
        REQUIRE(encode_checked(m) == bytes({0x18, 0x00}));
        at<BODY>(m).template emplace<DELTA>(0);
        REQUIRE(encode_checked(m) == bytes({0x20, 0x00}));
        at<BODY>(m).template emplace<TEXT>();
        REQUIRE(encode_checked(m) == bytes({0x12, 0x00}));
        at<BODY>(m).template emplace<SUB>();
        REQUIRE(encode_checked(m) == bytes({0x2A, 0x00}));
        at<BODY>(m).template emplace<BLOB>();
        REQUIRE(encode_checked(m) == bytes({0x32, 0x00}));
    }

    // code and delta have the same C++ type. The index picks the field number
    // and the encoding (delta is sint32, so zigzag).
    {
        R m{};
        at<BODY>(m).template emplace<DELTA>(-2);
        const auto wire = encode_checked(m);
        REQUIRE(wire == bytes({0x20, 0x03}));
        auto back = ctpb::decode<R>(wire);
        REQUIRE(back.has_value());
        REQUIRE(at<BODY>(*back).index() == DELTA);
        REQUIRE(std::get<DELTA>(at<BODY>(*back)) == -2);

        at<BODY>(m).template emplace<CODE>(-2);
        REQUIRE(encode_checked(m) == bytes({0x18, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}));
    }

    // Plain fields around the oneof, and a round trip of each alternative.
    {
        R m{};
        at<ID>(m) = 1;
        at<BODY>(m).template emplace<TEXT>("x");
        at<TAIL>(m) = "t";
        const auto wire = encode_checked(m);
        REQUIRE(wire == bytes({0x08, 0x01, 0x12, 0x01, 0x78, 0x3A, 0x01, 0x74}));
        auto back = ctpb::decode<R>(wire);
        REQUIRE(back.has_value());
        REQUIRE(at<ID>(*back) == 1);
        REQUIRE(at<BODY>(*back).index() == TEXT && std::get<TEXT>(at<BODY>(*back)) == "x");
        REQUIRE(at<TAIL>(*back) == "t");

        Sub sub{};
        at<0>(sub) = 1;
        at<BODY>(m).template emplace<SUB>(sub);
        const auto sub_wire = encode_checked(m);
        REQUIRE(sub_wire == bytes({0x08, 0x01, 0x2A, 0x02, 0x08, 0x01, 0x3A, 0x01, 0x74}));
        back = ctpb::decode<R>(sub_wire);
        REQUIRE(back.has_value() && at<BODY>(*back).index() == SUB);
        REQUIRE(at<0>(std::get<SUB>(at<BODY>(*back))) == 1);

        at<BODY>(m).template emplace<BLOB>(std::vector<std::byte>{std::byte{0x00}, std::byte{0xFF}});
        const auto blob_wire = encode_checked(m);
        REQUIRE(blob_wire == bytes({0x08, 0x01, 0x32, 0x02, 0x00, 0xFF, 0x3A, 0x01, 0x74}));
        back = ctpb::decode<R>(blob_wire);
        REQUIRE(back.has_value() && at<BODY>(*back).index() == BLOB);
        REQUIRE(std::get<BLOB>(at<BODY>(*back)).size() == 2);
    }

    // Decode: the last alternative on the wire wins.
    {
        auto back = ctpb::decode<R>(bytes({0x12, 0x01, 0x78, 0x18, 0x05}));
        REQUIRE(back.has_value() && at<BODY>(*back).index() == CODE);
        REQUIRE(std::get<CODE>(at<BODY>(*back)) == 5);

        back = ctpb::decode<R>(bytes({0x18, 0x05, 0x12, 0x01, 0x78}));
        REQUIRE(back.has_value() && at<BODY>(*back).index() == TEXT);
        REQUIRE(std::get<TEXT>(at<BODY>(*back)) == "x");

        back = ctpb::decode<R>(bytes({0x18, 0x05, 0x20, 0x03}));
        REQUIRE(back.has_value() && at<BODY>(*back).index() == DELTA);
        REQUIRE(std::get<DELTA>(at<BODY>(*back)) == -2);

        back = ctpb::decode<R>(bytes({0x18, 0x00}));
        REQUIRE(back.has_value() && at<BODY>(*back).index() == CODE);
        REQUIRE(std::get<CODE>(at<BODY>(*back)) == 0);
    }

    // Decode: the same message alternative twice decodes into the active value,
    // so a plain field set only by the first occurrence is kept.
    {
        auto back = ctpb::decode<R>(bytes({0x2A, 0x02, 0x08, 0x01, 0x2A, 0x00}));
        REQUIRE(back.has_value() && at<BODY>(*back).index() == SUB);
        REQUIRE(at<0>(std::get<SUB>(at<BODY>(*back))) == 1);
    }

    // Decode: an alternative with the wrong wire type is skipped and does not
    // select that alternative.
    {
        auto back = ctpb::decode<R>(bytes({0x1A, 0x00, 0x08, 0x01}));
        REQUIRE(back.has_value());
        REQUIRE(at<BODY>(*back).index() == NONE);
        REQUIRE(at<ID>(*back) == 1);
    }

    // Decode: a truncated alternative is an error.
    {
        auto back = ctpb::decode<R>(bytes({0x12, 0x04, 0x78}));
        REQUIRE(!back.has_value());
    }
}

// --- Template path (message_impl) -------------------------------------------
using ImplR = ctpb::message<oneof_proto, "R">;
using ImplSub = ctpb::message<oneof_proto, "Sub">;
static_assert(ImplR::field_count == 3);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplR&>().get<"body">())>,
                             std::variant<std::monostate, std::string, std::int32_t, std::int32_t, ImplSub,
                                          std::vector<std::byte>>>);

// --- Hand-written struct with reflect_base ----------------------------------
inline constexpr ctpb::fixed_string small_proto = R"(
    syntax = "proto3";
    message P { oneof v { int32 a = 1; int32 b = 2; } }
)";
struct HandP : ctpb::reflect_base<ctpb::parse<small_proto, "P">, HandP> {
    std::variant<std::monostate, std::int32_t, std::int32_t> v;
    auto reflect_members() { return std::tie(v); }
};

void run_reflect_base_tests() {
    HandP p;
    p.v.emplace<2>(0);
    REQUIRE(encode_checked(p) == bytes({0x10, 0x00}));
    auto back = ctpb::decode<HandP>(bytes({0x10, 0x07, 0x08, 0x03}));
    REQUIRE(back.has_value() && back->v.index() == 1 && std::get<1>(back->v) == 3);
}

// --- P2996 path (define_message) --------------------------------------------
#if CTPB_HAS_P2996
consteval { ctpb::define_message<oneof_proto, "Sub">(); }
consteval { ctpb::define_message<oneof_proto, "R">(); }
using ReflR = ctpb::message_t<oneof_proto, "R">;
using ReflSub = ctpb::message_t<oneof_proto, "Sub">;
static_assert(std::is_same_v<decltype(ReflR::body),
                             std::variant<std::monostate, std::string, int, int, ReflSub, std::vector<std::byte>>>);
static_assert(std::is_same_v<decltype(ReflR::tail), std::string>);
#endif

int main() {
    run_oneof_tests<ImplR, ImplSub>();
    run_reflect_base_tests();

    // decode_from resets the oneof when it is not on the wire.
    {
        ImplR m;
        m.get<"body">().emplace<CODE>(4);
        ctpb::wire::decode_error err{};
        m.decode_from(std::span<const std::byte>{}, err);
        REQUIRE(!err && m.get<"body">().index() == NONE);
    }

#if CTPB_HAS_P2996
    run_oneof_tests<ReflR, ReflSub>();
    std::printf("test_oneof_variant: OK (template path and P2996 path)\n");
#else
    std::printf("test_oneof_variant: OK (template path only, no P2996 reflection)\n");
#endif
    return 0;
}
