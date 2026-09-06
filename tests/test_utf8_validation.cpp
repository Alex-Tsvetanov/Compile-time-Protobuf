// tests/test_utf8_validation.cpp
//
// proto3 requires a `string` field to carry structurally valid UTF-8, and a message that
// breaks it to be rejected on parse. libprotobuf does this and spends about 7% of its
// decode doing it; this library did not do it at all until now, which is why the published
// decode comparison was not like-for-like.
//
// The cases below are the ones a validator gets wrong: an overlong encoding and a UTF-16
// surrogate half are both well-formed-looking multi-byte sequences that are not valid
// UTF-8, and a naive "high bit means continue" check accepts both.
//
// `bytes` is deliberately NOT validated and there is a case for that here. proto3 defines
// `bytes` as an arbitrary octet sequence with no encoding obligation, so validating it
// would reject valid messages. That exemption is asserted rather than left as an absence,
// because an absence reads as an oversight to the next person.

#include <cassert>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>
#include <type_traits>

#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string utf8_proto = R"(
    syntax = "proto3";
    package utf8test;

    message Doc {
      string title = 1;
      bytes  blob = 2;
      repeated string tags = 3;
    }
)";

#if CTPB_HAS_P2996
consteval { ctpb::define_message<utf8_proto, "Doc">(); }
using DocMessage = ctpb::message_t<utf8_proto, "Doc">;
#else
using DocSchema = ctpb::parse<utf8_proto, "Doc">;
struct DocMessage : ctpb::reflect_base<DocSchema, DocMessage> {
    std::string title;
    std::string blob;
    std::vector<std::string> tags;

    auto reflect_members() { return std::tie(title, blob, tags); }
};
#endif

namespace {

// A length-delimited field, built by hand so the test can put bytes on the wire that no
// encoder would produce. That is the point: the decoder must refuse input it did not
// write itself.
std::vector<std::byte> field_bytes(std::uint32_t number, std::string_view payload)
{
    std::vector<std::byte> out;
    const std::uint32_t tag = (number << 3) | 2u;  // wire type 2, length-delimited
    out.push_back(static_cast<std::byte>(tag));
    out.push_back(static_cast<std::byte>(payload.size()));
    for (char c : payload) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    return out;
}

bool decodes(std::span<const std::byte> bytes, ctpb::wire::decode_error& err)
{
    DocMessage msg{};
    msg.decode_from(bytes, err);
    return !err;
}

int failures = 0;

void check(const char* name, bool condition)
{
    if (!condition) { std::printf("  FAILED: %s\n", name); ++failures; }
    else            { std::printf("  ok: %s\n", name); }
}

}  // namespace

int main()
{
    // Which representation `bytes` has here decides whether the proto-type gate is
    // load-bearing: under P2996 it is std::vector<std::byte> and the type would suffice,
    // in the fallback it is std::string and the type would not.
    std::printf("  (CTPB_HAS_P2996=%d)\n", (int)CTPB_HAS_P2996);

    // --- the validator itself, at compile time and at run time -------------------------
    //
    // Both, because the runtime path takes a word-at-a-time ASCII pre-pass that constant
    // evaluation cannot, so the two paths are different code and a test of one is not a
    // test of the other.
    using ctpb::wire::valid_utf8;

    static_assert(valid_utf8(""));
    static_assert(valid_utf8("Alex I. Tsvetanov"));
    static_assert(valid_utf8("\xC3\xA9"));                     // U+00E9, two bytes
    static_assert(valid_utf8("\xE2\x82\xAC"));                 // U+20AC euro, three bytes
    static_assert(valid_utf8("\xF0\x9F\x92\xA9"));             // U+1F4A9, four bytes
    static_assert(!valid_utf8("\xC0\xAF"));                    // overlong '/'
    static_assert(!valid_utf8("\xED\xA0\x80"));                // UTF-16 surrogate half
    static_assert(!valid_utf8("\xE2\x82"));                    // truncated three-byte
    static_assert(!valid_utf8("\xFF"));                        // never valid

    check("valid ascii passes at run time", valid_utf8("atsvetanov@tu-sofia.bg"));
    check("valid multi-byte passes at run time", valid_utf8("na\xC3\xAFve caf\xC3\xA9"));
    check("overlong is refused at run time", !valid_utf8("\xC0\xAF"));
    check("surrogate is refused at run time", !valid_utf8("\xED\xA0\x80"));
    check("truncated is refused at run time", !valid_utf8("\xE2\x82"));
    // Longer than the eight-byte word stride, so the ASCII pre-pass runs and then hands
    // over. A string shorter than the stride never exercises the loop at all.
    check("a bad sequence after a full ascii word is still refused",
          !valid_utf8("12345678\xED\xA0\x80"));
    check("a good sequence after a full ascii word is accepted",
          valid_utf8("12345678\xE2\x82\xAC"));

    // --- the same, reached through an actual decode ------------------------------------
    {
        ctpb::wire::decode_error err{};
        auto bytes = field_bytes(1, "hello");
        check("a message with a valid string decodes", decodes(bytes, err));
    }
    {
        ctpb::wire::decode_error err{};
        auto bytes = field_bytes(1, "\xED\xA0\x80");
        check("a message with an invalid string is refused", !decodes(bytes, err));
        check("and the error names UTF-8 rather than truncation",
              err.err == ctpb::wire::decode_error::invalid_utf8);
    }
    {
        ctpb::wire::decode_error err{};
        auto bytes = field_bytes(3, "\xC0\xAF");
        check("a repeated string element is validated too", !decodes(bytes, err));
        check("and it too names UTF-8",
              err.err == ctpb::wire::decode_error::invalid_utf8);
    }
    {
        // THE EXEMPTION. The identical byte sequence that is refused in a string field
        // must be accepted in a bytes field.
        ctpb::wire::decode_error err{};
        auto bytes = field_bytes(2, "\xED\xA0\x80");
        check("the same bytes in a `bytes` field are accepted", decodes(bytes, err));
    }

    if (failures == 0) std::puts("test_utf8_validation: all checks passed");
    return failures == 0 ? 0 : 1;
}
