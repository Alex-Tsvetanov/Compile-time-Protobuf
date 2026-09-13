#pragma once

// ctpb/view.hpp — zero-copy, read-only lazy views over wire-format bytes.
//
// Instead of materialising `ctpb::decode<T>` results, `ctpb::decode_view<S>`
// validates the wire payload once (full upfront validation: framing, varints,
// packed batches, UTF-8) and returns a `msg_view` borrowing the input buffer.
// Fields are decoded on demand:
//
//   auto v = ctpb::decode_view<PersonSchema>(wire_bytes);
//   if (v) {
//       std::string_view n = v->get<"name">();            // zero-copy
//       for (std::int32_t x : v->get<"lucky_numbers">())  // lazy range
//           ...
//   }
//
// Semantics: "last one wins" for singular fields; repeated fields concatenate
// occurrences, including mixed packed/unpacked batches. Unknown fields are
// skipped.
//
// Cost model: singular access scans the message bytes O(message size);
// repeated fields expose forward iterators that decode each element exactly
// once per traversal. Nothing is allocated and nothing is copied except the
// values actually read.
//
// The view is non-owning: the input buffer must outlive any use of the view
// or values taken from it (same contract as std::string_view).
//
// Deviations from ctpb::decode: nested messages are validated structurally
// only (type-aware nested validation needs sub-schema lookup); varints wider
// than a 32-bit destination truncate silently (protobuf decoder leniency).

#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ctpb/reflection.hpp>

namespace ctpb {

template <typename Schema>
class msg_view;

class msg_blob; // defined below; element type for submessage fields

namespace view_detail {

// Generic string_view extraction: covers std::string_view and this library's
// fixed_string (which exposes .view()).
template <typename T>
[[nodiscard]] constexpr std::string_view as_sv(const T& v) noexcept {
    if constexpr (requires { { v.view() } -> std::convertible_to<std::string_view>; }) {
        return v.view();
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return std::string_view{v};
    } else {
        return std::string_view{v.data(), v.size()};
    }
}

// ---------------------------------------------------------------------------
// Minimal wire cursor (self-contained; no dependence on wire::reader)
// ---------------------------------------------------------------------------
struct cur {
    const std::byte* b = nullptr;
    const std::byte* e = nullptr;
};

[[nodiscard]] inline bool rd_varint(cur& c, std::uint64_t& out) noexcept {
    std::uint64_t result = 0;
    int shift = 0;
    const std::byte* p = c.b;
    while (p < c.e && shift < 64) {
        const auto byte = static_cast<std::uint8_t>(*p++);
        result |= (static_cast<std::uint64_t>(byte & 0x7Fu) << shift);
        if ((byte & 0x80u) == 0) {
            out = result;
            c.b = p;
            return true;
        }
        shift += 7;
    }
    return false;
}

[[nodiscard]] inline bool rd_tag(cur& c, std::int64_t& num, int& wt3) noexcept {
    std::uint64_t raw = 0;
    if (!rd_varint(c, raw)) return false;
    num = static_cast<std::int64_t>(raw >> 3);
    wt3 = static_cast<int>(raw & 7u);
    return num > 0; // field number 0 is not legal
}

[[nodiscard]] inline bool rd_fix32(cur& c, std::uint32_t& out) noexcept {
    if (c.e - c.b < 4) return false;
    const auto u8 = [](const std::byte* p) { return static_cast<std::uint8_t>(*p); };
    out = static_cast<std::uint32_t>(u8(c.b))
        | (static_cast<std::uint32_t>(u8(c.b + 1)) << 8)
        | (static_cast<std::uint32_t>(u8(c.b + 2)) << 16)
        | (static_cast<std::uint32_t>(u8(c.b + 3)) << 24);
    c.b += 4;
    return true;
}

[[nodiscard]] inline bool rd_fix64(cur& c, std::uint64_t& out) noexcept {
    std::uint32_t lo = 0, hi = 0;
    if (!rd_fix32(c, lo) || !rd_fix32(c, hi)) return false;
    out = (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
    return true;
}

[[nodiscard]] inline bool rd_blob(cur& c, std::span<const std::byte>& out) noexcept {
    std::uint64_t len = 0;
    if (!rd_varint(c, len)) return false;
    const auto avail = static_cast<std::uint64_t>(c.e - c.b);
    if (len > avail) return false;
    const auto n = static_cast<std::ptrdiff_t>(len);
    out = std::span<const std::byte>{c.b, c.b + n};
    c.b += n;
    return true;
}

// wt3: 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32 (groups unsupported)
[[nodiscard]] inline bool rd_skip(cur& c, int wt3) noexcept {
    switch (wt3) {
        case 0: { std::uint64_t u = 0;                return rd_varint(c, u); }
        case 1: { std::uint64_t u = 0;                return rd_fix64(c, u);  }
        case 2: { std::span<const std::byte> s{};     return rd_blob(c, s);   }
        case 5: { std::uint32_t u = 0;                return rd_fix32(c, u);  }
        default: return false;
    }
}

[[nodiscard]] constexpr std::int32_t zz32(std::uint32_t u) noexcept {
    return static_cast<std::int32_t>((u >> 1) ^ (~(u & 1u) + 1u));
}

[[nodiscard]] constexpr std::int64_t zz64(std::uint64_t u) noexcept {
    return static_cast<std::int64_t>((u >> 1) ^ (~(u & 1ull) + 1ull));
}

// Strict UTF-8 check (RFC 3629: no overlongs, no surrogates, <= U+10FFFF).
// Self-contained: wire::valid_utf8 is declared but not defined in this header
// tree, so the view layer cannot reference it without a link dependency.
[[nodiscard]] inline bool valid_utf8_bytes(std::string_view s) noexcept {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const unsigned char b = p[i];
        if (b < 0x80) { ++i; continue; }
        std::size_t extra;
        unsigned char lo, hi;
        if      (b >= 0xC2 && b <= 0xDF) { extra = 1; lo = 0x80; hi = 0xBF; }
        else if (b == 0xE0)              { extra = 2; lo = 0xA0; hi = 0xBF; }
        else if (b >= 0xE1 && b <= 0xEC) { extra = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xED)              { extra = 2; lo = 0x80; hi = 0x9F; }
        else if (b >= 0xEE && b <= 0xEF) { extra = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF0)              { extra = 3; lo = 0x90; hi = 0xBF; }
        else if (b >= 0xF1 && b <= 0xF3) { extra = 3; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF4)              { extra = 3; lo = 0x80; hi = 0x8F; }
        else return false;
        if (i + extra >= n) return false;
        if (p[i + 1] < lo || p[i + 1] > hi) return false;
        for (std::size_t k = 2; k <= extra; ++k) {
            if (p[i + k] < 0x80 || p[i + k] > 0xBF) return false;
        }
        i += extra + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Field classification: proto type-name string + is_enum/is_submessage flags
// ---------------------------------------------------------------------------
enum class field_cat {
    varint32, varint64,       // int32 / int64   (plain varint)
    uvarint32, uvarint64,     // uint32 / uint64
    zigzag32, zigzag64,       // sint32 / sint64
    fixed32u, fixed32s,       // fixed32 / sfixed32
    fixed64u, fixed64s,       // fixed64 / sfixed64
    flt, dbl,                 // float / double
    boolean, enumeration,     // bool / proto enum
    utf8, bytes, submessage,  // string / bytes / nested message
};

[[nodiscard]] constexpr field_cat categorize(bool is_submessage,
                                             bool is_enum,
                                             std::string_view t) noexcept {
    if (is_submessage)   return field_cat::submessage;
    if (is_enum)         return field_cat::enumeration;
    if (t == "string")   return field_cat::utf8;
    if (t == "bytes")    return field_cat::bytes;
    if (t == "bool")     return field_cat::boolean;
    if (t == "int32")    return field_cat::varint32;
    if (t == "int64")    return field_cat::varint64;
    if (t == "uint32")   return field_cat::uvarint32;
    if (t == "uint64")   return field_cat::uvarint64;
    if (t == "sint32")   return field_cat::zigzag32;
    if (t == "sint64")   return field_cat::zigzag64;
    if (t == "fixed32")  return field_cat::fixed32u;
    if (t == "sfixed32") return field_cat::fixed32s;
    if (t == "fixed64")  return field_cat::fixed64u;
    if (t == "sfixed64") return field_cat::fixed64s;
    if (t == "float")    return field_cat::flt;
    if (t == "double")   return field_cat::dbl;
    return field_cat::submessage; // unresolved custom type -> opaque blob
}

[[nodiscard]] constexpr bool is_packable(field_cat c) noexcept {
    return !(c == field_cat::utf8 || c == field_cat::bytes || c == field_cat::submessage);
}

// Numeric wire type of a single (non-batch) occurrence: 0 varint, 1 fixed64, 5 fixed32.
[[nodiscard]] constexpr int single_wt3(field_cat c) noexcept {
    switch (c) {
        case field_cat::fixed32u: case field_cat::fixed32s: case field_cat::flt: return 5;
        case field_cat::fixed64u: case field_cat::fixed64s: case field_cat::dbl: return 1;
        default: return 0;
    }
}

[[nodiscard]] constexpr std::size_t fixed_width(field_cat c) noexcept {
    switch (c) {
        case field_cat::fixed32u: case field_cat::fixed32s: case field_cat::flt: return 4;
        case field_cat::fixed64u: case field_cat::fixed64s: case field_cat::dbl: return 8;
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// Element types produced by reads
// ---------------------------------------------------------------------------
template <field_cat C> struct elem_type;
template <> struct elem_type<field_cat::varint32>    { using type = std::int32_t; };
template <> struct elem_type<field_cat::varint64>    { using type = std::int64_t; };
template <> struct elem_type<field_cat::uvarint32>   { using type = std::uint32_t; };
template <> struct elem_type<field_cat::uvarint64>   { using type = std::uint64_t; };
template <> struct elem_type<field_cat::zigzag32>    { using type = std::int32_t; };
template <> struct elem_type<field_cat::zigzag64>    { using type = std::int64_t; };
template <> struct elem_type<field_cat::fixed32u>    { using type = std::uint32_t; };
template <> struct elem_type<field_cat::fixed32s>    { using type = std::int32_t; };
template <> struct elem_type<field_cat::fixed64u>    { using type = std::uint64_t; };
template <> struct elem_type<field_cat::fixed64s>    { using type = std::int64_t; };
template <> struct elem_type<field_cat::flt>         { using type = float; };
template <> struct elem_type<field_cat::dbl>         { using type = double; };
template <> struct elem_type<field_cat::boolean>     { using type = bool; };
template <> struct elem_type<field_cat::enumeration> { using type = std::int32_t; };
template <> struct elem_type<field_cat::utf8>        { using type = std::string_view; };
template <> struct elem_type<field_cat::bytes>       { using type = std::span<const std::byte>; };
template <> struct elem_type<field_cat::submessage>  { using type = ::ctpb::msg_blob; };

template <field_cat C>
using elem_type_t = typename elem_type<C>::type;

// ---------------------------------------------------------------------------
// Scalar element decoding (payload pre-validated)
// ---------------------------------------------------------------------------
template <field_cat C>
[[nodiscard]] inline bool read_scalar(cur& c, elem_type_t<C>& out) noexcept {
    if constexpr (C == field_cat::varint32 || C == field_cat::varint64 ||
                  C == field_cat::uvarint32 || C == field_cat::uvarint64) {
        std::uint64_t u = 0;
        if (!rd_varint(c, u)) return false;
        out = static_cast<elem_type_t<C>>(u);
    } else if constexpr (C == field_cat::zigzag32) {
        std::uint64_t u = 0;
        if (!rd_varint(c, u)) return false;
        out = zz32(static_cast<std::uint32_t>(u));
    } else if constexpr (C == field_cat::zigzag64) {
        std::uint64_t u = 0;
        if (!rd_varint(c, u)) return false;
        out = zz64(u);
    } else if constexpr (C == field_cat::fixed32u || C == field_cat::fixed32s) {
        std::uint32_t u = 0;
        if (!rd_fix32(c, u)) return false;
        out = static_cast<elem_type_t<C>>(u);
    } else if constexpr (C == field_cat::fixed64u || C == field_cat::fixed64s) {
        std::uint64_t u = 0;
        if (!rd_fix64(c, u)) return false;
        out = static_cast<elem_type_t<C>>(u);
    } else if constexpr (C == field_cat::flt) {
        std::uint32_t u = 0;
        if (!rd_fix32(c, u)) return false;
        static_assert(sizeof(float) == sizeof(u));
        std::memcpy(&out, &u, sizeof(u));
    } else if constexpr (C == field_cat::dbl) {
        std::uint64_t u = 0;
        if (!rd_fix64(c, u)) return false;
        static_assert(sizeof(double) == sizeof(u));
        std::memcpy(&out, &u, sizeof(u));
    } else if constexpr (C == field_cat::boolean) {
        std::uint64_t u = 0;
        if (!rd_varint(c, u)) return false;
        out = (u != 0);
    } else if constexpr (C == field_cat::enumeration) {
        // Negative enum values arrive as 64-bit-widened varints.
        std::uint64_t u = 0;
        if (!rd_varint(c, u)) return false;
        out = static_cast<std::int32_t>(static_cast<std::uint32_t>(u));
    } else {
        static_assert(C != C, "read_scalar: not a scalar category");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// Schema-less structural walk used for nested message payloads.
[[nodiscard]] inline wire::decode_error::kind validate_structure(
        std::span<const std::byte> bytes) noexcept {
    cur c{bytes.data(), bytes.data() + bytes.size()};
    while (c.b < c.e) {
        std::int64_t num = 0;
        int wt3 = 0;
        if (!rd_tag(c, num, wt3)) return wire::decode_error::bad_varint;
        if (!rd_skip(c, wt3))     return wire::decode_error::truncated;
    }
    return wire::decode_error::ok;
}

template <field_cat cat>
[[nodiscard]] inline wire::decode_error::kind validate_packable(
        cur& c, int wt3, bool repeated) noexcept {
    if (wt3 == single_wt3(cat)) {
        elem_type_t<cat> tmp{};
        return read_scalar<cat>(c, tmp) ? wire::decode_error::ok
                                        : wire::decode_error::bad_varint;
    }
    // Packed batch: decoders must accept wt-2 for any packable repeated field.
    if (wt3 == 2 && repeated) {
        std::span<const std::byte> blob{};
        if (!rd_blob(c, blob)) return wire::decode_error::truncated;
        constexpr std::size_t w = fixed_width(cat);
        if constexpr (w != 0) {
            if (blob.size() % w != 0) return wire::decode_error::truncated;
        } else {
            cur inner{blob.data(), blob.data() + blob.size()};
            elem_type_t<cat> tmp{};
            while (inner.b < inner.e) {
                if (!read_scalar<cat>(inner, tmp)) return wire::decode_error::bad_varint;
            }
        }
        return wire::decode_error::ok;
    }
    return wire::decode_error::bad_wire_type;
}

// Validates one occurrence of field I (cursor positioned after the tag).
template <typename Schema, std::size_t I>
[[nodiscard]] inline wire::decode_error::kind validate_occurrence(cur& c, int wt3) noexcept {
    constexpr auto info = Schema::get_field(I);
    constexpr field_cat cat = categorize(info.is_submessage, info.is_enum, as_sv(info.type));
    if constexpr (is_packable(cat)) {
        return validate_packable<cat>(c, wt3, info.is_repeated);
    } else {
        if (wt3 != 2) return wire::decode_error::bad_wire_type;
        std::span<const std::byte> blob{};
        if (!rd_blob(c, blob)) return wire::decode_error::truncated;
        if constexpr (cat == field_cat::utf8) {
            const std::string_view sv{reinterpret_cast<const char*>(blob.data()), blob.size()};
            if (!valid_utf8_bytes(sv)) return wire::decode_error::invalid_utf8;
        } else if constexpr (cat == field_cat::submessage) {
            if (const auto k = validate_structure(blob); k != wire::decode_error::ok) return k;
        }
        return wire::decode_error::ok;
    }
}

template <typename Schema>
[[nodiscard]] inline wire::decode_error::kind validate_payload(
        std::span<const std::byte> bytes) noexcept {
    constexpr std::size_t count = Schema::field_count();
    cur c{bytes.data(), bytes.data() + bytes.size()};
    while (c.b < c.e) {
        std::int64_t num = 0;
        int wt3 = 0;
        if (!rd_tag(c, num, wt3)) return wire::decode_error::bad_varint;

        bool matched = false;
        wire::decode_error::kind result = wire::decode_error::ok;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&] {
                if (matched) return;
                constexpr auto info = Schema::get_field(Is);
                if (num == info.number) {
                    matched = true;
                    result = validate_occurrence<Schema, Is>(c, wt3);
                }
            }(), ...);
        }(std::make_index_sequence<count>{});

        if (!matched) {
            if (!rd_skip(c, wt3)) return wire::decode_error::truncated;
        } else if (result != wire::decode_error::ok) {
            return result;
        }
    }
    return wire::decode_error::ok;
}

} // namespace view_detail

// ---------------------------------------------------------------------------
// msg_blob — opaque nested-message payload with typed re-interpretation.
// ---------------------------------------------------------------------------
class msg_blob {
    std::span<const std::byte> bytes_{};
public:
    constexpr msg_blob() = default;
    constexpr explicit msg_blob(std::span<const std::byte> b) : bytes_(b) {}

    [[nodiscard]] constexpr std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }

    // Reinterpret as a typed message view. No re-validation; the enclosing
    // buffer was already validated structurally.
    template <typename SubSchema>
    [[nodiscard]] constexpr msg_view<SubSchema> view() const noexcept {
        return msg_view<SubSchema>{bytes_};
    }
};

// ---------------------------------------------------------------------------
// field_view<Schema, I> — lazy accessor for one field.
// ---------------------------------------------------------------------------
template <typename Schema, std::size_t I>
class field_view {
public:
    using schema_type = Schema;
    static constexpr std::size_t field_index = I;
    static constexpr auto info     = Schema::get_field(I);
    static constexpr auto category = view_detail::categorize(
        info.is_submessage, info.is_enum, view_detail::as_sv(info.type));
    static constexpr bool repeated = info.is_repeated;

    using element_type = view_detail::elem_type_t<category>;

    constexpr field_view() = default;
    constexpr explicit field_view(std::span<const std::byte> payload) noexcept
        : payload_(payload) {}

    // ------------------------------------------------------------------
    // Repeated access: lazy forward range
    // ------------------------------------------------------------------
    class iterator {
    public:
        using iterator_concept  = std::forward_iterator_tag;
        using value_type        = element_type;
        using difference_type   = std::ptrdiff_t;

        constexpr iterator() noexcept = default; // end()

        const value_type& operator*()  const noexcept { return cur_; }
        const value_type* operator->() const noexcept { return &cur_; }

        iterator& operator++()    { advance(); return *this; }
        iterator  operator++(int) { iterator tmp = *this; advance(); return tmp; }

        friend bool operator==(const iterator& a, const iterator& b) noexcept {
            if (a.done_ != b.done_) return false;
            if (a.done_) return true;
            return a.scan_.b == b.scan_.b && a.seg_.b == b.seg_.b && a.seg_.e == b.seg_.e;
        }
        friend bool operator!=(const iterator& a, const iterator& b) noexcept { return !(a == b); }

    private:
        friend class field_view;

        explicit iterator(std::span<const std::byte> payload) noexcept
            : scan_{payload.data(), payload.data() + payload.size()}, done_(false) {
            advance();
        }

        void advance() {
            // Continue an active packed batch first.
            if constexpr (view_detail::is_packable(category)) {
                if (seg_.b < seg_.e) {
                    if (view_detail::read_scalar<category>(seg_, cur_)) return;
                    done_ = true;
                    return; // unreachable post-validation
                }
            }

            while (scan_.b < scan_.e) {
                std::int64_t num = 0;
                int wt3 = 0;
                if (!view_detail::rd_tag(scan_, num, wt3)) break;
                if (num != info.number) {
                    static_cast<void>(view_detail::rd_skip(scan_, wt3));
                    continue;
                }
                if constexpr (view_detail::is_packable(category)) {
                    if (wt3 == view_detail::single_wt3(category)) {
                        static_cast<void>(view_detail::read_scalar<category>(scan_, cur_));
                        return;
                    }
                    if (wt3 == 2) {
                        std::span<const std::byte> blob{};
                        if (!view_detail::rd_blob(scan_, blob)) break;
                        if (blob.empty()) continue; // empty packed batch
                        seg_.b = blob.data();
                        seg_.e = blob.data() + blob.size();
                        if (view_detail::read_scalar<category>(seg_, cur_)) return;
                        break;
                    }
                    break; // wrong wire type — validation rejected this already
                } else {
                    if (wt3 != 2) break;
                    std::span<const std::byte> blob{};
                    if (!view_detail::rd_blob(scan_, blob)) break;
                    if constexpr (category == view_detail::field_cat::utf8) {
                        cur_ = value_type{reinterpret_cast<const char*>(blob.data()), blob.size()};
                    } else if constexpr (category == view_detail::field_cat::bytes) {
                        cur_ = blob;
                    } else {
                        cur_ = value_type{blob}; // msg_blob
                    }
                    return;
                }
            }
            done_ = true;
        }

        view_detail::cur scan_{};
        view_detail::cur seg_{};
        value_type cur_{};
        bool done_ = true;
    };

    [[nodiscard]] iterator begin() const { return iterator{payload_}; }
    [[nodiscard]] iterator end()   const { return iterator{}; }

    [[nodiscard]] bool empty() const { return begin() == end(); }

    // O(n) — walks the wire on each call.
    [[nodiscard]] std::size_t size() const {
        std::size_t n = 0;
        for (auto it = begin(); it != end(); ++it) ++n;
        return n;
    }

    // O(n) linear fetch — laziness precludes a random-access index.
    [[nodiscard]] element_type operator[](std::size_t idx) const {
        std::size_t n = 0;
        for (auto it = begin(); it != end(); ++it, ++n) {
            if (n == idx) return *it;
        }
        return element_type{};
    }

    // ------------------------------------------------------------------
    // Singular access
    // ------------------------------------------------------------------
    [[nodiscard]] element_type value() const {
        static_assert(!repeated, "repeated field: iterate the view instead of calling value()");
        return scan_singular();
    }

    operator element_type() const { return value(); }

    // Presence on the wire (proto3 implicit presence: absent => default value).
    [[nodiscard]] bool present() const {
        view_detail::cur c{payload_.data(), payload_.data() + payload_.size()};
        while (c.b < c.e) {
            std::int64_t num = 0;
            int wt3 = 0;
            if (!view_detail::rd_tag(c, num, wt3)) return false;
            if (num == info.number) return true;
            static_cast<void>(view_detail::rd_skip(c, wt3));
        }
        return false;
    }

    [[nodiscard]] constexpr std::span<const std::byte> wire_bytes() const noexcept { return payload_; }

    // Comparison sugar so `view.get<"name">() == "Nikola"` works.
    friend bool operator==(const field_view& h, const element_type& v) { return h.value() == v; }
    friend bool operator!=(const field_view& h, const element_type& v) { return !(h == v); }

    // Binding point for message views and (P2996) generated dot-syntax structs.
    constexpr void bind(std::span<const std::byte> b) noexcept { payload_ = b; }

private:
    element_type scan_singular() const {
        element_type out{};
        view_detail::cur c{payload_.data(), payload_.data() + payload_.size()};
        while (c.b < c.e) {
            std::int64_t num = 0;
            int wt3 = 0;
            if (!view_detail::rd_tag(c, num, wt3)) break;
            if (num != info.number) {
                static_cast<void>(view_detail::rd_skip(c, wt3));
                continue;
            }
            if constexpr (view_detail::is_packable(category)) {
                if (wt3 == view_detail::single_wt3(category)) {
                    static_cast<void>(view_detail::read_scalar<category>(c, out)); // last one wins
                } else {
                    static_cast<void>(view_detail::rd_skip(c, wt3));
                }
            } else {
                if (wt3 != 2) break;
                std::span<const std::byte> blob{};
                if (!view_detail::rd_blob(c, blob)) break;
                if constexpr (category == view_detail::field_cat::utf8) {
                    out = element_type{reinterpret_cast<const char*>(blob.data()), blob.size()};
                } else if constexpr (category == view_detail::field_cat::bytes) {
                    out = blob;
                } else {
                    out = element_type{blob}; // msg_blob
                }
                // last one wins
            }
        }
        return out;
    }

    std::span<const std::byte> payload_{};
};

// ---------------------------------------------------------------------------
// msg_view<Schema> — the top-level borrowed message view.
// ---------------------------------------------------------------------------
namespace view_detail {

template <typename Schema, fixed_string Name>
[[nodiscard]] consteval std::size_t find_field() noexcept {
    constexpr std::size_t count = Schema::field_count();
    const std::string_view wanted = as_sv(Name);
    for (std::size_t i = 0; i < count; ++i) {
        if (as_sv(Schema::get_field(i).name) == wanted) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace view_detail

template <typename Schema>
class msg_view {
public:
    using schema_type = Schema;
    static constexpr std::size_t field_count = Schema::field_count();

    constexpr msg_view() = default;
    constexpr explicit msg_view(std::span<const std::byte> payload) : payload_(payload) {}

    template <fixed_string Name>
    [[nodiscard]] auto get() const {
        constexpr std::size_t idx = view_detail::find_field<Schema, Name>();
        static_assert(idx != static_cast<std::size_t>(-1), "Field name not found in message schema");
        return field_view<Schema, idx>{payload_};
    }

    template <std::size_t Index>
    [[nodiscard]] auto field() const {
        static_assert(Index < field_count, "Field index out of bounds");
        return field_view<Schema, Index>{payload_};
    }

    [[nodiscard]] constexpr std::span<const std::byte> wire_bytes() const noexcept { return payload_; }

private:
    std::span<const std::byte> payload_{};
};

// ---------------------------------------------------------------------------
// Schema extraction & entry point
// ---------------------------------------------------------------------------

// Accepts a schema type directly, a generated C++20 message exposing
// ::schema_type, or (P2996) a type specialising message_schema_traits.
template <typename T>
consteval auto view_schema_pick() noexcept {
    if constexpr (requires { typename T::schema_type; }) {
        return std::type_identity<typename T::schema_type>{};
    } else {
        return std::type_identity<T>{}; // assume a schema type was passed
    }
}

template <typename T>
using view_schema_t = typename decltype(view_schema_pick<T>())::type;

template <typename T>
[[nodiscard]] inline std::expected<msg_view<view_schema_t<T>>, wire::decode_error>
decode_view(std::span<const std::byte> bytes) {
    using S = view_schema_t<T>;
    if (const auto k = view_detail::validate_payload<S>(bytes); k != wire::decode_error::ok) {
        wire::decode_error err{};
        err.err = k;
        return std::unexpected(err);
    }
    return msg_view<S>{bytes};
}

// ---------------------------------------------------------------------------
// P2996 dot-syntax view: a generated aggregate of field_view members.
// ---------------------------------------------------------------------------
#if defined(__cpp_impl_reflection)
namespace reflect_detail {

template <typename V, std::size_t I>
consteval std::meta::info view_member_i() {
    constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^V, std::meta::access_context::current()));
    static_assert(I < members.size());
    return members[I];
}

template <typename Schema>
consteval auto complete_view_struct(std::meta::info incomplete_type) {
    constexpr std::size_t count = Schema::field_count();
    std::vector<std::meta::info> specs;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        ([&] {
            constexpr auto info = Schema::get_field(Is);
            const auto member_type = std::meta::substitute(
                ^^::ctpb::field_view, {^^Schema, std::meta::reflect_constant(Is)});
            specs.push_back(std::meta::data_member_spec(member_type, {.name = info.name}));
        }(), ...);
    }(std::make_index_sequence<count>{});
    return std::meta::define_aggregate(incomplete_type, specs);
}

} // namespace reflect_detail

template <fixed_string ProtoSource, fixed_string MessageName>
struct dot_view_holder {
    using schema_type = ctpb::parse<ProtoSource, MessageName>;
    struct impl;
    consteval {
        reflect_detail::complete_view_struct<schema_type>(^^impl);
    }
    using type = impl;
};

// Message-shaped aggregate of lazy field handles: enables dot syntax on
// P2996 toolchains, e.g. `dv->name == "Nikola"` / `for (auto p : dv->phones)`.
template <fixed_string ProtoSource, fixed_string MessageName>
using dot_view = typename dot_view_holder<ProtoSource, MessageName>::type;

template <fixed_string ProtoSource, fixed_string MessageName>
[[nodiscard]] inline std::expected<dot_view<ProtoSource, MessageName>, wire::decode_error>
decode_dot_view(std::span<const std::byte> bytes) {
    using SchemaT = ctpb::parse<ProtoSource, MessageName>;
    using V       = dot_view<ProtoSource, MessageName>;
    if (const auto k = view_detail::validate_payload<SchemaT>(bytes);
        k != wire::decode_error::ok) {
        wire::decode_error err{};
        err.err = k;
        return std::unexpected(err);
    }
    V v{};
    constexpr std::size_t count = SchemaT::field_count();
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        ([&] {
            constexpr auto mi = reflect_detail::view_member_i<V, Is>();
            v.[:mi:].bind(bytes);
        }(), ...);
    }(std::make_index_sequence<count>{});
    return v;
}
#endif // defined(__cpp_impl_reflection)

} // namespace ctpb
