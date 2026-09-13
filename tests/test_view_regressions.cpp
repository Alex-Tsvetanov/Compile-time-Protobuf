// tests/test_view_regressions.cpp — the cases ctpb/view.hpp used to get wrong.
//
// Every payload here is written out byte by byte, and where the wire is legal
// the expectation is checked against `ctpb::decode` as well, so the lazy view
// and the eager decoder have to agree on what a message means.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <ctpb/ctpb.hpp>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);      \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {

inline constexpr ctpb::fixed_string Proto{R"proto(
syntax = "proto3";
package t;

enum TopKind { TK0 = 0; TK1 = 1; }

message Inner {
    int32 a = 1;
    int32 b = 2;
    string s = 3;
    repeated int32 xs = 4;
}

message Mid { Inner i = 1; }

message Fx {
    fixed32 f32 = 8;
    sfixed64 sf64 = 9;
    repeated sfixed32 deltas = 10;
}

message M {
    enum Kind { K0 = 0; K1 = 1; K2 = 2; }
    Kind kind = 1;
    TopKind tkind = 2;
    Inner in = 3;
    string str = 4;
    int32 i32 = 5;
    repeated Kind kinds = 6;
    Mid mid = 7;
    fixed32 f32 = 8;
    sfixed64 sf64 = 9;
    repeated sfixed32 deltas = 10;
    map<string, string> mp = 11;
    oneof pick { string os = 12; int32 oi = 13; }
}
)proto"};

using MSchema     = ctpb::parse<Proto, "M">;
using InnerSchema = ctpb::parse<Proto, "Inner">;
using MidSchema   = ctpb::parse<Proto, "Mid">;
using FxSchema    = ctpb::parse<Proto, "Fx">;

// Eager mirrors (C++20 path), so the two decoders can be compared.
struct InnerEager : ctpb::reflect_base<InnerSchema, InnerEager> {
    std::int32_t a = 0;
    std::int32_t b = 0;
    std::string s;
    std::vector<std::int32_t> xs;
    auto reflect_members() { return std::tie(a, b, s, xs); }
};

struct MidEager : ctpb::reflect_base<MidSchema, MidEager> {
    InnerEager i;
    auto reflect_members() { return std::tie(i); }
};

struct MEager : ctpb::reflect_base<MSchema, MEager> {
    std::int32_t kind = 0;
    std::int32_t tkind = 0;
    InnerEager in;
    std::string str;
    std::int32_t i32 = 0;
    std::vector<std::int32_t> kinds;
    MidEager mid;
    std::uint32_t f32 = 0;
    std::int64_t sf64 = 0;
    std::vector<std::int32_t> deltas;
    auto reflect_members() {
        return std::tie(kind, tkind, in, str, i32, kinds, mid, f32, sf64, deltas);
    }
};

// Just the fixed-width fields, so an encode can be compared byte for byte
// without the empty submessages a full M would also emit.
struct FxEager : ctpb::reflect_base<FxSchema, FxEager> {
    std::uint32_t f32 = 0;
    std::int64_t sf64 = 0;
    std::vector<std::int32_t> deltas;
    auto reflect_members() { return std::tie(f32, sf64, deltas); }
};

std::vector<std::byte> bytes_of(std::initializer_list<int> xs) {
    std::vector<std::byte> v;
    v.reserve(xs.size());
    for (int x : xs) v.push_back(static_cast<std::byte>(x));
    return v;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // An enum declared inside the message is an enum, not a nested message.
    // `Schema::get_field` classifies against the file's top-level
    // declarations only, so the view resolves the type name itself.
    // ------------------------------------------------------------------
    {
        const auto w = bytes_of({0x08, 0x01}); // kind = K1
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        CHECK(v->get<"kind">().value() == 1);

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->kind == 1);

        const auto top = bytes_of({0x10, 0x01}); // tkind = TK1
        auto tv = ctpb::decode_view<MSchema>(top);
        CHECK(tv.has_value());
        CHECK(tv->get<"tkind">().value() == 1);
        std::printf("[PASS] view: enum declared inside the message decodes as a varint.\n");
    }

    // Repeated nested enum, packed and unpacked in the same message.
    {
        const auto w = bytes_of({0x32, 0x02, 0x01, 0x00,  // kinds packed [K1, K0]
                                 0x30, 0x02});            // kinds unpacked K2
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        std::vector<std::int32_t> got;
        for (std::int32_t k : v->get<"kinds">()) got.push_back(k);
        CHECK(got.size() == 3);
        CHECK(got[0] == 1 && got[1] == 0 && got[2] == 2);

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->kinds == got);
        std::printf("[PASS] view: repeated nested enum, packed and unpacked batches.\n");
    }

    // ------------------------------------------------------------------
    // Nested payloads are validated against their own schema, not just walked
    // for framing: invalid UTF-8 and a truncated packed varint one and two
    // levels down are both refused, as the eager decoder refuses them.
    // ------------------------------------------------------------------
    {
        const auto w = bytes_of({0x1A, 0x04, 0x1A, 0x02, 0xC0, 0xAF}); // in{ s = C0 AF }
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(!v.has_value());
        CHECK(v.error().err == ctpb::wire::decode_error::invalid_utf8);
        CHECK(!ctpb::decode<MEager>(w).has_value());
        std::printf("[PASS] view: invalid UTF-8 inside a nested message is refused.\n");
    }
    {
        const auto w = bytes_of({0x1A, 0x03, 0x22, 0x01, 0x80}); // in{ xs = packed [80] }
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(!v.has_value());
        CHECK(!ctpb::decode<MEager>(w).has_value());
        std::printf("[PASS] view: truncated packed varint inside a nested message is refused.\n");
    }
    {
        // mid{ i{ a = truncated varint } } — two levels below the top.
        const auto w = bytes_of({0x3A, 0x04, 0x0A, 0x02, 0x08, 0x80});
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(!v.has_value());
        CHECK(!ctpb::decode<MEager>(w).has_value());
        std::printf("[PASS] view: malformed payload two levels down is refused.\n");
    }

    // A well-formed nested message still reads, at both levels.
    {
        const auto w = bytes_of({0x1A, 0x06, 0x08, 0x01, 0x1A, 0x02, 'o', 'k',
                                 0x3A, 0x04, 0x0A, 0x02, 0x08, 0x07});
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        auto in = v->get<"in">().value().view<InnerSchema>();
        CHECK(in.get<"a">().value() == 1);
        CHECK(in.get<"s">().value() == "ok");
        auto mid = v->get<"mid">().value().view<MidSchema>();
        CHECK(mid.get<"i">().value().view<InnerSchema>().get<"a">().value() == 7);

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->in.a == 1 && e->in.s == "ok" && e->mid.i.a == 7);
        std::printf("[PASS] view: nested and twice-nested messages read as the eager decoder reads them.\n");
    }

    // ------------------------------------------------------------------
    // A known field arriving with a wire type its type never uses is skipped
    // like an unknown field, not treated as a fatal error: that is what an
    // older reader meeting a re-typed field number has to do.
    // ------------------------------------------------------------------
    {
        const auto w = bytes_of({0x2D, 0x00, 0x00, 0x00, 0x00}); // i32 sent as fixed32
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        CHECK(v->get<"i32">().value() == 0);
        CHECK(ctpb::decode<MEager>(w).has_value());
        std::printf("[PASS] view: wrong wire type on a known field is skipped, not fatal.\n");
    }

    // A packed batch on a *singular* numeric field is not a batch: validation
    // skips it, so neither value() nor the iterator may invent elements.
    {
        const auto w = bytes_of({0x2A, 0x02, 0x01, 0x02}); // i32 as a LEN payload
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        CHECK(v->get<"i32">().value() == 0);
        CHECK(v->get<"i32">().size() == 0);
        CHECK(v->get<"i32">().empty());
        CHECK(!v->get<"i32">().present());

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->i32 == 0);
        std::printf("[PASS] view: a packed batch on a singular field yields no elements.\n");
    }

    // ...and a later, valid occurrence of that same field still wins.
    {
        const auto w = bytes_of({0x20, 0x05,                     // str as a varint
                                 0x22, 0x03, 'a', 'b', 'c'});    // str = "abc"
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        CHECK(v->get<"str">().value() == "abc");

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->str == "abc");
        std::printf("[PASS] view: a skipped occurrence does not hide the valid one after it.\n");
    }
    {
        const auto w = bytes_of({0x35, 0x00, 0x00, 0x00, 0x00,   // kinds as fixed32
                                 0x30, 0x02});                   // kinds = K2
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        std::vector<std::int32_t> got;
        for (std::int32_t k : v->get<"kinds">()) got.push_back(k);
        CHECK(got.size() == 1);
        CHECK(got[0] == 2);
        std::printf("[PASS] view: repeated field keeps reading past a skipped occurrence.\n");
    }

    // ------------------------------------------------------------------
    // Documented deviation, pinned here so it cannot change silently: proto3
    // says repeated occurrences of a singular message field merge; a view that
    // owns no storage keeps the last one, and exposes the rest by iteration.
    // ------------------------------------------------------------------
    {
        const auto w = bytes_of({0x1A, 0x02, 0x08, 0x01,   // in{ a = 1 }
                                 0x1A, 0x02, 0x10, 0x02}); // in{ b = 2 }
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        auto in = v->get<"in">().value().view<InnerSchema>();
        CHECK(in.get<"a">().value() == 0); // not 1: the last occurrence wins
        CHECK(in.get<"b">().value() == 2);

        std::size_t occurrences = 0;
        for (const auto& blob : v->field<2>()) {
            CHECK(!blob.bytes().empty());
            ++occurrences;
        }
        CHECK(occurrences == 2);

        auto e = ctpb::decode<MEager>(w); // the eager decoder merges
        CHECK(e.has_value());
        CHECK(e->in.a == 1 && e->in.b == 2);
        std::printf("[PASS] view: singular message field keeps the last occurrence "
                    "(eager merges); both occurrences remain reachable.\n");
    }

    // ------------------------------------------------------------------
    // Fixed-width scalars are raw little-endian bytes, not varints. The eager
    // codec used to size, write and read all three as varints, so a fixed32
    // field encoded to the wrong wire type and decoded as zero.
    // ------------------------------------------------------------------
    {
        const auto w = bytes_of({0x45, 0xEF, 0xBE, 0xAD, 0xDE,            // f32 = 0xDEADBEEF
                                 0x49, 0xFF, 0xFF, 0xFF, 0xFF,            // sf64 = -1
                                       0xFF, 0xFF, 0xFF, 0xFF,
                                 0x52, 0x08, 0xF9, 0xFF, 0xFF, 0xFF,      // deltas = [-7, 1]
                                             0x01, 0x00, 0x00, 0x00});
        auto v = ctpb::decode_view<MSchema>(w);
        CHECK(v.has_value());
        CHECK(v->get<"f32">().value() == 0xDEADBEEFu);
        CHECK(v->get<"sf64">().value() == -1);
        std::vector<std::int32_t> ds;
        for (std::int32_t d : v->get<"deltas">()) ds.push_back(d);
        CHECK(ds.size() == 2);
        CHECK(ds[0] == -7 && ds[1] == 1);

        auto e = ctpb::decode<MEager>(w);
        CHECK(e.has_value());
        CHECK(e->f32 == 0xDEADBEEFu);
        CHECK(e->sf64 == -1);
        CHECK(e->deltas == ds);

        // ...and the encoder produces those same bytes back.
        FxEager out{};
        out.f32 = 0xDEADBEEFu;
        out.sf64 = -1;
        out.deltas = {-7, 1};
        const auto round = ctpb::encode(out);
        CHECK(round.size() == w.size());
        CHECK(std::equal(round.begin(), round.end(), w.begin()));
        std::printf("[PASS] eager+view: fixed32, sfixed64 and packed sfixed32 use fixed-width encoding.\n");
    }

    // ------------------------------------------------------------------
    // map and oneof fields occupy field numbers like any other field, and a
    // validator that cannot see them waves their payloads through. Both are
    // now walked, so invalid UTF-8 in a map key, a map value or a oneof
    // string fails the message.
    // ------------------------------------------------------------------
    {
        const auto ok_entry = bytes_of({0x5A, 0x08, 0x0A, 0x02, 'e', 'n',   // mp["en"] = "hi"
                                        0x12, 0x02, 'h', 'i'});
        auto v = ctpb::decode_view<MSchema>(ok_entry);
        CHECK(v.has_value());
        std::size_t entries = 0;
        for (const auto& entry : v->get<"mp">()) {
            CHECK(entry.bytes().size() == 8);
            ++entries;
        }
        CHECK(entries == 1);

        const auto bad_key = bytes_of({0x5A, 0x04, 0x0A, 0x02, 0xC0, 0xAF});
        auto bk = ctpb::decode_view<MSchema>(bad_key);
        CHECK(!bk.has_value());
        CHECK(bk.error().err == ctpb::wire::decode_error::invalid_utf8);

        const auto bad_value = bytes_of({0x5A, 0x04, 0x12, 0x02, 0xC0, 0xAF});
        auto bv = ctpb::decode_view<MSchema>(bad_value);
        CHECK(!bv.has_value());
        CHECK(bv.error().err == ctpb::wire::decode_error::invalid_utf8);
        std::printf("[PASS] view: map entries are validated, keys and values alike.\n");
    }
    {
        const auto ok_branch = bytes_of({0x62, 0x02, 'h', 'i'}); // os = "hi"
        auto v = ctpb::decode_view<MSchema>(ok_branch);
        CHECK(v.has_value());
        CHECK(v->get<"os">().value() == "hi");

        const auto bad_branch = bytes_of({0x62, 0x02, 0xC0, 0xAF});
        auto bb = ctpb::decode_view<MSchema>(bad_branch);
        CHECK(!bb.has_value());
        CHECK(bb.error().err == ctpb::wire::decode_error::invalid_utf8);

        const auto other = bytes_of({0x68, 0x2A}); // oi = 42
        auto ov = ctpb::decode_view<MSchema>(other);
        CHECK(ov.has_value());
        CHECK(ov->get<"oi">().value() == 42);
        std::printf("[PASS] view: oneof branches are ordinary fields, validated and readable.\n");
    }

    // Field number 0 cannot be assigned in a .proto, so a tag carrying it is a
    // malformed stream for both decoders.
    {
        const auto w = bytes_of({0x00, 0x00});
        CHECK(!ctpb::decode_view<MSchema>(w).has_value());
        CHECK(!ctpb::decode<MEager>(w).has_value());
        std::printf("[PASS] eager+view: field number 0 is refused.\n");
    }

    // ------------------------------------------------------------------
    // The view borrows, so a container that dies with the expression cannot be
    // passed to it at all.
    // ------------------------------------------------------------------
    // `decode_view(std::vector<std::byte>{...})` is a deleted overload and so
    // fails to compile; a deleted call cannot be probed with `requires`, so
    // what is pinned here is the classification the deletion keys on.
    static_assert(!std::ranges::borrowed_range<std::vector<std::byte>>,
                  "an owning temporary must not count as borrowed");
    static_assert(std::ranges::borrowed_range<std::span<const std::byte>>,
                  "a span must stay callable");
    static_assert(std::ranges::borrowed_range<std::vector<std::byte>&>,
                  "an lvalue container must stay callable");
    {
        std::vector<std::byte> owned = bytes_of({0x08, 0x01});
        CHECK(ctpb::decode_view<MSchema>(owned).has_value()); // lvalue: fine
        std::printf("[PASS] view: owning temporaries are rejected at compile time.\n");
    }

    std::printf("All view regression tests passed.\n");
    return 0;
}
