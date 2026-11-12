// tests/test_map_fields.cpp
//
// Generated map<K, V> members. The member is a std::map<K, V>. On the wire a
// map is a repeated message per entry, with the key as field 1 and the value
// as field 2. Both are always written. A missing key or value decodes as its
// default, and for a repeated key the last entry wins. Both code paths run:
// message_impl always, define_message structs with P2996.

#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "test_support.hpp"

using test_support::at;
using test_support::bytes;
using test_support::encode_checked;

inline constexpr ctpb::fixed_string map_proto = R"(
    syntax = "proto3";
    package map_test;

    enum Color { RED = 0; GREEN = 1; }

    message Sub { int32 n = 1; }

    message M {
      map<string, int32> counts = 1;
      map<sint64, string> names = 2;
      map<bool, Sub> subs = 3;
      map<fixed32, Color> colors = 4;
      int32 after = 5;
    }
)";

// Member indices of M.
enum : std::size_t { COUNTS, NAMES, SUBS, COLORS, AFTER };

template <typename M, typename Sub>
void run_map_tests() {
    // Empty maps write nothing.
    {
        M m{};
        REQUIRE(encode_checked(m).empty());
    }

    // One entry: tag of the map field, entry length, key (field 1), value (field 2).
    {
        M m{};
        at<COUNTS>(m)["a"] = 1;
        REQUIRE(encode_checked(m) == bytes({0x0A, 0x05, 0x0A, 0x01, 0x61, 0x10, 0x01}));
    }

    // A default key and a default value are still written.
    {
        M m{};
        at<COUNTS>(m)[""] = 0;
        REQUIRE(encode_checked(m) == bytes({0x0A, 0x04, 0x0A, 0x00, 0x10, 0x00}));
    }

    // std::map keeps the keys sorted, so the output order is fixed.
    {
        M m{};
        at<COUNTS>(m)["b"] = 2;
        at<COUNTS>(m)["a"] = 1;
        REQUIRE(encode_checked(m) == bytes({0x0A, 0x05, 0x0A, 0x01, 0x61, 0x10, 0x01,
                                          0x0A, 0x05, 0x0A, 0x01, 0x62, 0x10, 0x02}));
    }

    // Key and value keep their own proto encodings: sint64 key (zigzag), message
    // value, fixed32 key, enum value. Then a plain field.
    {
        M m{};
        at<NAMES>(m)[-1] = "x";
        at<SUBS>(m)[true] = Sub{};
        at<COLORS>(m)[7] = 1;
        at<AFTER>(m) = 1;
        const auto wire = encode_checked(m);
        REQUIRE(wire == bytes({0x12, 0x05, 0x08, 0x01, 0x12, 0x01, 0x78,              // names {-1: "x"}
                             0x1A, 0x04, 0x08, 0x01, 0x12, 0x00,                    // subs {true: {}}
                             0x22, 0x07, 0x0D, 0x07, 0x00, 0x00, 0x00, 0x10, 0x01,  // colors {7: GREEN}
                             0x28, 0x01}));                                         // after = 1

        auto back = ctpb::decode<M>(wire);
        REQUIRE(back.has_value());
        REQUIRE(at<NAMES>(*back).size() == 1 && at<NAMES>(*back).at(-1) == "x");
        REQUIRE(at<SUBS>(*back).size() == 1 && at<0>(at<SUBS>(*back).at(true)) == 0);
        REQUIRE(at<COLORS>(*back).size() == 1 && at<COLORS>(*back).at(7) == 1);
        REQUIRE(at<AFTER>(*back) == 1);
    }

    // Round trip of several entries per map.
    {
        M m{};
        at<COUNTS>(m)["apples"] = 50;
        at<COUNTS>(m)["pears"] = -3;
        at<NAMES>(m)[0] = "zero";
        at<NAMES>(m)[1LL << 40] = "big";
        Sub sub{};
        at<0>(sub) = 9;
        at<SUBS>(m)[false] = sub;
        auto back = ctpb::decode<M>(encode_checked(m));
        REQUIRE(back.has_value());
        REQUIRE(at<COUNTS>(*back) == at<COUNTS>(m));
        REQUIRE(at<NAMES>(*back) == at<NAMES>(m));
        REQUIRE(at<SUBS>(*back).size() == 1 && at<0>(at<SUBS>(*back).at(false)) == 9);
    }

    // Decode: a missing key or value is the default.
    {
        auto back = ctpb::decode<M>(bytes({0x0A, 0x02, 0x10, 0x05}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).size() == 1 && at<COUNTS>(*back).at("") == 5);
        back = ctpb::decode<M>(bytes({0x0A, 0x03, 0x0A, 0x01, 0x61}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).size() == 1 && at<COUNTS>(*back).at("a") == 0);
        back = ctpb::decode<M>(bytes({0x0A, 0x00}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).size() == 1 && at<COUNTS>(*back).at("") == 0);
        back = ctpb::decode<M>(bytes({0x1A, 0x02, 0x08, 0x01}));
        REQUIRE(back.has_value() && at<SUBS>(*back).size() == 1 && at<0>(at<SUBS>(*back).at(true)) == 0);
    }

    // Decode: for a repeated key the last entry wins.
    {
        auto back = ctpb::decode<M>(bytes({0x0A, 0x05, 0x0A, 0x01, 0x61, 0x10, 0x01,
                                           0x0A, 0x05, 0x0A, 0x01, 0x61, 0x10, 0x02}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).size() == 1 && at<COUNTS>(*back).at("a") == 2);
    }

    // Decode: value before key, and an unknown field inside the entry, are fine.
    {
        auto back = ctpb::decode<M>(bytes({0x0A, 0x07, 0x10, 0x01, 0x18, 0x09, 0x0A, 0x01, 0x61}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).size() == 1 && at<COUNTS>(*back).at("a") == 1);
    }

    // Decode: an entry with the wrong wire type is skipped.
    {
        auto back = ctpb::decode<M>(bytes({0x08, 0x01, 0x28, 0x02}));
        REQUIRE(back.has_value() && at<COUNTS>(*back).empty() && at<AFTER>(*back) == 2);
    }

    // Decode: a truncated entry is an error.
    {
        REQUIRE(!ctpb::decode<M>(bytes({0x0A, 0x05, 0x0A, 0x01})).has_value());
        REQUIRE(!ctpb::decode<M>(bytes({0x0A, 0x03, 0x0A, 0x05, 0x61})).has_value());
    }
}

// --- Template path (message_impl) -------------------------------------------
using ImplM = ctpb::message<map_proto, "M">;
using ImplSub = ctpb::message<map_proto, "Sub">;
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplM&>().get<"counts">())>,
                             std::map<std::string, std::int32_t>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplM&>().get<"names">())>,
                             std::map<std::int64_t, std::string>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplM&>().get<"subs">())>, std::map<bool, ImplSub>>);
static_assert(std::is_same_v<std::decay_t<decltype(std::declval<ImplM&>().get<"colors">())>,
                             std::map<std::uint32_t, std::int32_t>>);

// --- Hand-written struct with reflect_base: any map type with insert_or_assign
inline constexpr ctpb::fixed_string small_proto = R"(
    syntax = "proto3";
    message P { map<string, int32> stock = 1; }
)";
struct HandP : ctpb::reflect_base<ctpb::parse<small_proto, "P">, HandP> {
    std::unordered_map<std::string, std::int32_t> stock;
    auto reflect_members() { return std::tie(stock); }
};

void run_reflect_base_tests() {
    HandP p;
    p.stock["a"] = 1;
    REQUIRE(encode_checked(p) == bytes({0x0A, 0x05, 0x0A, 0x01, 0x61, 0x10, 0x01}));
    auto back = ctpb::decode<HandP>(bytes({0x0A, 0x02, 0x10, 0x05}));
    REQUIRE(back.has_value() && back->stock.size() == 1 && back->stock.at("") == 5);
}

// --- P2996 path (define_message) --------------------------------------------
#if CTPB_HAS_P2996
consteval { ctpb::define_message<map_proto, "Sub">(); }
consteval { ctpb::define_message<map_proto, "M">(); }
using ReflM = ctpb::message_t<map_proto, "M">;
using ReflSub = ctpb::message_t<map_proto, "Sub">;
static_assert(std::is_same_v<decltype(ReflM::counts), std::map<std::string, int>>);
static_assert(std::is_same_v<decltype(ReflM::names), std::map<long long, std::string>>);
static_assert(std::is_same_v<decltype(ReflM::subs), std::map<bool, ReflSub>>);
static_assert(std::is_same_v<decltype(ReflM::colors), std::map<unsigned int, int>>);
#endif

int main() {
    run_map_tests<ImplM, ImplSub>();
    run_reflect_base_tests();

    // decode_from clears a map that is not on the wire.
    {
        ImplM m;
        m.get<"counts">()["x"] = 1;
        ctpb::wire::decode_error err{};
        m.decode_from(std::span<const std::byte>{}, err);
        REQUIRE(!err && m.get<"counts">().empty());
    }

#if CTPB_HAS_P2996
    run_map_tests<ReflM, ReflSub>();
    std::printf("test_map_fields: OK (template path and P2996 path)\n");
#else
    std::printf("test_map_fields: OK (template path only, no P2996 reflection)\n");
#endif
    return 0;
}
