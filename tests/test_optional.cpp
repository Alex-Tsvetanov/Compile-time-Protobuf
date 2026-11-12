// tests/test_optional.cpp
//
// proto3 `optional` fields (explicit presence). The member is a
// std::optional<T>. An engaged value is always written, even when it is zero
// or empty. An absent value is not written. Decode engages the member when the
// field is on the wire. Both code paths run: message_impl always, and the
// define_message structs when the compiler has P2996 reflection.

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "test_support.hpp"

using test_support::at;
using test_support::bytes;
using test_support::encode_checked;

inline constexpr ctpb::fixed_string optional_proto = R"(
    syntax = "proto3";
    package opt;

    enum Mode { MODE_ZERO = 0; MODE_ONE = 1; }

    message Sub { int32 n = 1; }

    message O {
      optional int32 a = 1;
      optional string s = 2;
      optional bool b = 3;
      optional Mode mode = 4;
      optional sint32 z = 5;
      optional fixed64 f = 6;
      optional double d = 7;
      optional bytes raw = 8;
      optional Sub sub = 9;
      int32 plain = 10;
    }
)";

// Member indices of O, in declaration order.
enum : std::size_t { A, S, B, MODE, Z, F, D, RAW, SUB, PLAIN };

template <typename O, typename Sub>
void run_optional_tests() {
    // Nothing engaged and the plain field at zero: nothing on the wire.
    {
        O m{};
        REQUIRE(encode_checked(m).empty());
    }

    // Every optional engaged with its zero or empty value: each one is written.
    {
        O m{};
        at<A>(m) = 0;
        at<S>(m) = std::string{};
        at<B>(m) = false;
        at<MODE>(m) = 0;
        at<Z>(m) = 0;
        at<F>(m) = 0;
        at<D>(m) = 0.0;
        at<RAW>(m) = std::vector<std::byte>{};
        at<SUB>(m) = Sub{};
        const auto wire = encode_checked(m);
        REQUIRE(wire == bytes({0x08, 0x00,                                             // a = 0
                             0x12, 0x00,                                             // s = ""
                             0x18, 0x00,                                             // b = false
                             0x20, 0x00,                                             // mode = 0
                             0x28, 0x00,                                             // z = 0
                             0x31, 0, 0, 0, 0, 0, 0, 0, 0,                          // f = 0 (fixed64)
                             0x39, 0, 0, 0, 0, 0, 0, 0, 0,                          // d = 0.0
                             0x42, 0x00,                                             // raw = empty
                             0x4A, 0x00}));                                          // sub = {}

        auto back = ctpb::decode<O>(wire);
        REQUIRE(back.has_value());
        REQUIRE(at<A>(*back) == 0);
        REQUIRE(at<S>(*back) == std::string{});
        REQUIRE(at<B>(*back) == false);
        REQUIRE(at<MODE>(*back) == 0);
        REQUIRE(at<Z>(*back) == 0);
        REQUIRE(at<F>(*back) == 0u);
        REQUIRE(at<D>(*back) == 0.0);
        REQUIRE(at<RAW>(*back).has_value() && at<RAW>(*back)->empty());
        REQUIRE(at<SUB>(*back).has_value() && at<0>(*at<SUB>(*back)) == 0);
        REQUIRE(at<PLAIN>(*back) == 0);
    }

    // Non-zero values next to a plain field.
    {
        O m{};
        at<A>(m) = -1;
        at<S>(m) = std::string("hi");
        at<B>(m) = true;
        at<MODE>(m) = 1;
        at<Z>(m) = -1;
        at<F>(m) = 0x0102030405060708ull;
        at<D>(m) = 1.0;
        at<RAW>(m) = std::vector<std::byte>{std::byte{0xAB}};
        Sub sub{};
        at<0>(sub) = 7;
        at<SUB>(m) = sub;
        at<PLAIN>(m) = 3;
        const auto wire = encode_checked(m);
        REQUIRE(wire == bytes({0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01,  // a = -1
                             0x12, 0x02, 0x68, 0x69,                                        // s = "hi"
                             0x18, 0x01,                                                    // b = true
                             0x20, 0x01,                                                    // mode = 1
                             0x28, 0x01,                                                    // z = -1 (zigzag)
                             0x31, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,          // f
                             0x39, 0, 0, 0, 0, 0, 0, 0xF0, 0x3F,                            // d = 1.0
                             0x42, 0x01, 0xAB,                                              // raw
                             0x4A, 0x02, 0x08, 0x07,                                        // sub.n = 7
                             0x50, 0x03}));                                                 // plain = 3

        auto back = ctpb::decode<O>(wire);
        REQUIRE(back.has_value());
        REQUIRE(at<A>(*back) == -1);
        REQUIRE(at<S>(*back) == std::string("hi"));
        REQUIRE(at<B>(*back) == true);
        REQUIRE(at<MODE>(*back) == 1);
        REQUIRE(at<Z>(*back) == -1);
        REQUIRE(at<F>(*back) == 0x0102030405060708ull);
        REQUIRE(at<D>(*back) == 1.0);
        REQUIRE(at<RAW>(*back) == std::vector<std::byte>{std::byte{0xAB}});
        REQUIRE(at<SUB>(*back).has_value() && at<0>(*at<SUB>(*back)) == 7);
        REQUIRE(at<PLAIN>(*back) == 3);
    }

    // Decode: an absent field stays absent, a field that is on the wire is engaged.
    {
        auto back = ctpb::decode<O>(bytes({0x18, 0x00}));  // only b = false
        REQUIRE(back.has_value());
        REQUIRE(!at<A>(*back).has_value());
        REQUIRE(!at<S>(*back).has_value());
        REQUIRE(at<B>(*back).has_value() && *at<B>(*back) == false);
        REQUIRE(!at<SUB>(*back).has_value());
    }

    // Decode: the last occurrence wins, also when it is zero.
    {
        auto back = ctpb::decode<O>(bytes({0x08, 0x05, 0x08, 0x00}));
        REQUIRE(back.has_value());
        REQUIRE(at<A>(*back).has_value() && *at<A>(*back) == 0);
    }

    // Decode: field 1 with the wrong wire type (length-delimited) is skipped and
    // does not engage the member.
    {
        auto back = ctpb::decode<O>(bytes({0x0A, 0x01, 0x07, 0x50, 0x02}));
        REQUIRE(back.has_value());
        REQUIRE(!at<A>(*back).has_value());
        REQUIRE(at<PLAIN>(*back) == 2);
    }

    // Decode: a truncated value is an error.
    {
        auto back = ctpb::decode<O>(bytes({0x12, 0x05, 0x68}));
        REQUIRE(!back.has_value());
    }
}

// --- Template path (message_impl) -------------------------------------------
using ImplO = ctpb::message<optional_proto, "O">;
using ImplSub = ctpb::message<optional_proto, "Sub">;
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplO&>().get<"a">())>, std::optional<std::int32_t>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplO&>().get<"f">())>, std::optional<std::uint64_t>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplO&>().get<"sub">())>, std::optional<ImplSub>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplO&>().get<"plain">())>, std::int32_t>);

// --- Hand-written struct with reflect_base ----------------------------------
inline constexpr ctpb::fixed_string small_proto = R"(
    syntax = "proto3";
    message P { optional int32 a = 1; string s = 2; }
)";
struct HandP : ctpb::reflect_base<ctpb::parse<small_proto, "P">, HandP> {
    std::optional<std::int32_t> a;
    std::string s;
    auto reflect_members() { return std::tie(a, s); }
};

void run_reflect_base_tests() {
    HandP p;
    p.a = 0;
    REQUIRE(encode_checked(p) == bytes({0x08, 0x00}));
    auto back = ctpb::decode<HandP>(bytes({0x12, 0x01, 0x78}));
    REQUIRE(back.has_value() && !back->a.has_value() && back->s == "x");
    back = ctpb::decode<HandP>(bytes({0x08, 0x00}));
    REQUIRE(back.has_value() && back->a.has_value() && *back->a == 0);
}

// --- P2996 path (define_message) --------------------------------------------
#if CTPB_HAS_P2996
consteval { ctpb::define_message<optional_proto, "Sub">(); }
consteval { ctpb::define_message<optional_proto, "O">(); }
using ReflO = ctpb::message_t<optional_proto, "O">;
using ReflSub = ctpb::message_t<optional_proto, "Sub">;
static_assert(std::is_same_v<decltype(ReflO::a), std::optional<int>>);
static_assert(std::is_same_v<decltype(ReflO::f), std::optional<unsigned long long>>);
static_assert(std::is_same_v<decltype(ReflO::sub), std::optional<ReflSub>>);
static_assert(std::is_same_v<decltype(ReflO::plain), int>);
#endif

int main() {
    run_optional_tests<ImplO, ImplSub>();
    run_reflect_base_tests();

    // decode_from resets an engaged optional that is not on the wire.
    {
        ImplO m;
        m.get<"a">() = 4;
        ctpb::wire::decode_error err{};
        m.decode_from(std::span<const std::byte>{}, err);
        REQUIRE(!err && !m.get<"a">().has_value());
    }

#if CTPB_HAS_P2996
    run_optional_tests<ReflO, ReflSub>();
    std::printf("test_optional: OK (template path and P2996 path)\n");
#else
    std::printf("test_optional: OK (template path only, no P2996 reflection)\n");
#endif
    return 0;
}
