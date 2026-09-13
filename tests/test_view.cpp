// tests/test_view.cpp — zero-copy lazy view: correctness, laziness, error surfacing.
//
// Self-contained: wire bytes are hand-encoded below (no dependence on the
// eager encoder), so this test exercises only ctpb/view.hpp + the schema parser.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include <ctpb/ctpb.hpp>

namespace {

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
static constexpr ctpb::fixed_string PersonProto{R"proto(
syntax = "proto3";
package tutorial;

message Person {
    string name = 1;
    int32 id = 2;
    string email = 3;
    message PhoneNumber {
        string number = 1;
        int32 type = 2;
    }
    repeated PhoneNumber phones = 4;
    bytes avatar = 5;
    repeated int32 lucky_numbers = 6;
    sint32 delta = 7;
    repeated sint64 ratios = 8;
    double score = 9;
    repeated fixed32 coeffs = 10;
}
)proto"};

using PersonSchema = ctpb::parse<PersonProto, "tutorial.Person">;
using PhoneSchema  = ctpb::parse<PersonProto, "tutorial.Person.PhoneNumber">;

// ---------------------------------------------------------------------------
// Minimal hand-rolled wire writer for building test payloads
// ---------------------------------------------------------------------------
void out_varint(std::vector<std::byte>& v, std::uint64_t x) {
    do {
        auto b = static_cast<std::uint8_t>(x & 0x7Fu);
        x >>= 7;
        if (x) b |= 0x80u;
        v.push_back(static_cast<std::byte>(b));
    } while (x);
}

void out_tag(std::vector<std::byte>& v, std::int64_t num, int wt3) {
    out_varint(v, (static_cast<std::uint64_t>(num) << 3) | static_cast<std::uint64_t>(wt3));
}

void out_blob(std::vector<std::byte>& v, std::span<const std::byte> payload) {
    out_varint(v, payload.size());
    v.insert(v.end(), payload.begin(), payload.end());
}

void out_fix32(std::vector<std::byte>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFFu));
}

void out_fix64(std::vector<std::byte>& v, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFFu));
}

void out_string(std::vector<std::byte>& v, std::string_view s) {
    out_blob(v, std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(s.data()), s.size()});
}

std::vector<std::byte> phone(std::string_view number, std::int32_t type) {
    std::vector<std::byte> v;
    out_tag(v, 1, 2); out_string(v, number);
    out_tag(v, 2, 0); out_varint(v, static_cast<std::uint32_t>(type));
    return v;
}

std::vector<std::byte> person_wire() {
    std::vector<std::byte> v;

    out_tag(v, 1, 2); out_string(v, "Nikola Tesla");
    out_tag(v, 2, 0); out_varint(v, 1856);
    out_tag(v, 3, 2); out_string(v, "tesla@wardenclyffe.org");

    auto p1 = phone("+1-555-0100", 0);
    out_tag(v, 4, 2); out_blob(v, p1);
    auto p2 = phone("+1-555-0199", 2);
    out_tag(v, 4, 2); out_blob(v, p2);

    const std::byte avatar[] = {std::byte{0x00}, std::byte{0x01}, std::byte{0xDE},
                                std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    out_tag(v, 5, 2); out_blob(v, avatar);

    // lucky_numbers: two packed batches [3,1,4,1,5],[2,6] plus one unpacked 42.
    std::vector<std::byte> pack1;
    for (std::uint32_t n : {3u, 1u, 4u, 1u, 5u}) out_varint(pack1, n);
    out_tag(v, 6, 2); out_blob(v, pack1);
    std::vector<std::byte> pack2;
    for (std::uint32_t n : {2u, 6u}) out_varint(pack2, n);
    out_tag(v, 6, 2); out_blob(v, pack2);
    out_tag(v, 6, 0); out_varint(v, 42); // mixed unpacked form must merge in

    // delta: sint32(-5) = zigzag 9
    out_tag(v, 7, 0); out_varint(v, 9);

    // ratios: sint64 packed [-1, 300] -> zigzag [1, 600]
    std::vector<std::byte> ratios;
    out_varint(ratios, 1); out_varint(ratios, 600);
    out_tag(v, 8, 2); out_blob(v, ratios);

    // score: double 0.5 = 0x3FE0000000000000
    out_tag(v, 9, 1); out_fix64(v, 0x3FE0000000000000ull);

    // coeffs: repeated fixed32 packed [7, 11]
    std::vector<std::byte> coeffs;
    out_fix32(coeffs, 7); out_fix32(coeffs, 11);
    out_tag(v, 10, 2); out_blob(v, coeffs);

    // unknown field — must be skipped without affecting anything else
    out_tag(v, 99, 0); out_varint(v, 1);

    return v;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

} // namespace

int main() {
    const auto wire = person_wire();

    // ------------------------------------------------------------------
    // Happy path: full upfront validation succeeds, fields read lazily
    // ------------------------------------------------------------------
    auto v = ctpb::decode_view<PersonSchema>(wire);
    CHECK(v.has_value());

    CHECK(v->get<"name">() == "Nikola Tesla");
    CHECK(v->get<"name">().present());
    CHECK(static_cast<std::int32_t>(v->get<"id">()) == 1856);
    CHECK(v->get<"email">() == "tesla@wardenclyffe.org");
    CHECK(v->get<"delta">().value() == -5);
    CHECK(v->get<"score">().value() == 0.5);

    // Zero-copy proof: the returned string_view points inside `wire`.
    const std::string_view name_sv = v->get<"name">();
    const auto* lo = reinterpret_cast<const char*>(wire.data());
    const auto* hi = lo + wire.size();
    CHECK(name_sv.data() >= lo && name_sv.data() < hi);

    // bytes field
    const auto avatar = v->get<"avatar">().value();
    CHECK(avatar.size() == 6);
    CHECK(avatar[2] == std::byte{0xDE});

    // repeated submessages: lazy iteration + typed nested views
    auto phones = v->get<"phones">();
    CHECK(phones.size() == 2);
    CHECK(!phones.empty());
    {
        const char* expect_numbers[] = {"+1-555-0100", "+1-555-0199"};
        const std::int32_t expect_types[] = {0, 2};
        std::size_t i = 0;
        for (const auto& blob : phones) {
            auto pv = blob.view<PhoneSchema>();
            CHECK(pv.get<"number">() == expect_numbers[i]);
            CHECK(static_cast<std::int32_t>(pv.get<"type">()) == expect_types[i]);
            ++i;
        }
        CHECK(i == 2);
    }
    CHECK(phones[1].view<PhoneSchema>().get<"number">() == "+1-555-0199");

    // repeated int32 across 2 packed batches + 1 unpacked occurrence
    {
        auto nums = v->get<"lucky_numbers">();
        const std::int32_t expect[] = {3, 1, 4, 1, 5, 2, 6, 42};
        CHECK(nums.size() == 8);
        std::size_t i = 0;
        for (std::int32_t n : nums) CHECK(n == expect[i++]);
        CHECK(nums[7] == 42);
        CHECK(nums[0] == 3);
    }

    // repeated sint64 packed: [-1, 300]
    {
        auto ratios = v->get<"ratios">();
        CHECK(ratios.size() == 2);
        auto it = ratios.begin();
        CHECK(*it == -1); ++it;
        CHECK(*it == 300); ++it;
        CHECK(it == ratios.end());
    }

    // repeated fixed32 packed: [7, 11]
    {
        auto coeffs = v->get<"coeffs">();
        CHECK(coeffs.size() == 2);
        CHECK(coeffs[0] == 7u);
        CHECK(coeffs[1] == 11u);
    }

    std::printf("[PASS] view: full lazy decode of all field kinds.\n");

    // ------------------------------------------------------------------
    // Absent fields / defaults: decode an empty message
    // ------------------------------------------------------------------
    {
        auto empty = ctpb::decode_view<PersonSchema>(std::span<const std::byte>{});
        CHECK(empty.has_value());
        CHECK(empty->get<"name">() == "");
        CHECK(!empty->get<"name">().present());
        CHECK(static_cast<std::int32_t>(empty->get<"id">()) == 0);
        CHECK(empty->get<"phones">().empty());
        CHECK(empty->get<"lucky_numbers">().size() == 0);
        CHECK(empty->get<"score">().value() == 0.0);
        std::printf("[PASS] view: absent fields read as proto3 defaults.\n");
    }

    // ------------------------------------------------------------------
    // Truncated payload rejected up front
    // ------------------------------------------------------------------
    {
        // Cut 4 bytes into the coeffs packed blob (declared len 8, half missing)
        // plus the trailing unknown field. (-3 would remove exactly the whole
        // unknown-field record, leaving a valid message.)
        const auto truncated = std::span<const std::byte>{wire.data(), wire.size() - 5};
        auto bad = ctpb::decode_view<PersonSchema>(truncated);
        CHECK(!bad.has_value());
        CHECK(bad.error().err == ctpb::wire::decode_error::truncated);
        std::printf("[PASS] view: truncated payload -> decode_error::truncated.\n");
    }

    // ------------------------------------------------------------------
    // Invalid UTF-8 in a string field surfaces at decode_view time
    // ------------------------------------------------------------------
    {
        std::vector<std::byte> bad_utf8;
        out_tag(bad_utf8, 1, 2);
        out_varint(bad_utf8, 2);
        bad_utf8.push_back(std::byte{0xC0}); // invalid 2-byte lead (overlong)
        bad_utf8.push_back(std::byte{0xAF});
        auto bad = ctpb::decode_view<PersonSchema>(bad_utf8);
        CHECK(!bad.has_value());
        CHECK(bad.error().err == ctpb::wire::decode_error::invalid_utf8);
        std::printf("[PASS] view: invalid UTF-8 -> decode_error::invalid_utf8.\n");
    }

    // ------------------------------------------------------------------
    // Wrong wire type for a declared field is rejected
    // ------------------------------------------------------------------
    {
        std::vector<std::byte> wrong;
        out_tag(wrong, 2, 2); // id declared varint, arrives length-delimited
        out_varint(wrong, 2);
        wrong.push_back(std::byte{0x00});
        wrong.push_back(std::byte{0x01});
        auto bad = ctpb::decode_view<PersonSchema>(wrong);
        CHECK(!bad.has_value());
        std::printf("[PASS] view: wrong wire type rejected.\n");
    }

#if defined(__cpp_impl_reflection)
    // ------------------------------------------------------------------
    // P2996 dot-syntax view
    // ------------------------------------------------------------------
    {
        auto dv = ctpb::decode_dot_view<PersonProto, "tutorial.Person">(wire);
        CHECK(dv.has_value());
        CHECK(dv->name == "Nikola Tesla");
        CHECK(dv->id == 1856);
        std::size_t count = 0;
        for (const auto& ph : dv->phones) {
            auto pv = ph.view<PhoneSchema>();
            CHECK(!pv.get<"number">().value().empty());
            ++count;
        }
        CHECK(count == 2);
        std::int32_t expect_nums[] = {3, 1, 4, 1, 5, 2, 6, 42};
        std::size_t i = 0;
        for (std::int32_t n : dv->lucky_numbers) CHECK(n == expect_nums[i++]);
        std::printf("[PASS] view: dot-syntax lazy view (P2996).\n");
    }
#endif

    std::printf("All view tests passed.\n");
    return 0;
}
