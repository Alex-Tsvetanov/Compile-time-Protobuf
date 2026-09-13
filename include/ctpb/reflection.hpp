#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "parser.hpp"
#include "wire.hpp"

// Is P2996 static reflection actually usable in this translation unit?
//
// Not "does the compiler have a reflection branch" — the Bloomberg clang-p2996
// build this project is developed against defines no reflection feature-test
// macro at all, so gating on __cpp_reflection silently selected the C++20
// fallback everywhere, including in the benchmark builds. Two things are
// needed and both are checked: the compiler must have reflection enabled
// (-freflection), and the standard library in use must ship <meta> — it lives
// in the libc++ tree, so a build that asks for -stdlib=libstdc++ does not get
// it. Define CTPB_HAS_P2996 yourself to override the decision.
#if defined(__has_feature)
#  define CTPB_DETECT_REFLECTION __has_feature(reflection)
#else
#  define CTPB_DETECT_REFLECTION 0
#endif
#if defined(__has_include)
#  if __has_include(<meta>)
#    define CTPB_DETECT_META_HEADER 1
#  else
#    define CTPB_DETECT_META_HEADER 0
#  endif
#else
#  define CTPB_DETECT_META_HEADER 0
#endif

#if !defined(CTPB_HAS_P2996)
#  if (defined(__cpp_reflection) || CTPB_DETECT_REFLECTION) && CTPB_DETECT_META_HEADER
#    define CTPB_HAS_P2996 1
#  else
#    define CTPB_HAS_P2996 0
#  endif
#endif

#if CTPB_HAS_P2996
#include <meta>
#endif

namespace ctpb {

// ---------------------------------------------------------------------------
// Fixed String Literal Type (NTTP parameter in C++20/C++26)
// ---------------------------------------------------------------------------
template <std::size_t N>
struct fixed_string {
    char buf[N]{};

    // to_fixed_string() and anything else that fills the buffer afterwards
    // needs this; without it `fixed_string<64>{}` does not compile, which took
    // ctpb::message<> and any derived schema name with it.
    constexpr fixed_string() = default;

    constexpr fixed_string(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            buf[i] = str[i];
        }
    }

    // Trailing NULs are padding, not content: to_fixed_string() fills a fixed
    // 64-byte buffer, and trimming only the last one left a name like
    // "Inner\0\0..." that no message lookup could ever match.
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        std::size_t len = N;
        while (len > 0 && buf[len - 1] == '\0') --len;
        return std::string_view(buf, len);
    }
};

template <std::size_t N>
fixed_string(const char (&str)[N]) -> fixed_string<N>;

template <std::size_t N>
constexpr auto to_fixed_string(std::string_view sv) {
    fixed_string<N> fs{};
    std::size_t len = sv.size() < N ? sv.size() : N - 1;
    for (std::size_t i = 0; i < len; ++i) {
        fs.buf[i] = sv[i];
    }
    fs.buf[len] = '\0';
    return fs;
}

// Forward declaration of field_info
struct field_info {
    std::string_view name;
    std::int64_t     number = 0;
    std::string_view type;
    bool             is_repeated = false;
    bool             is_optional = false;
    bool             is_enum = false;
    bool             is_submessage = false;
    // A map<K,V> field: on the wire a repeated entry message with the key at
    // field 1 and the value at field 2. `type` is empty for one of these.
    bool             is_map = false;
    std::string_view key_type;
    std::string_view value_type;
    // Set for a field declared inside a oneof. The branches share the storage
    // but each is an ordinary field on the wire.
    bool             is_oneof_branch = false;
    std::string_view oneof_name;
};

// ---------------------------------------------------------------------------
// Constexpr Schema Trait Generator
// ---------------------------------------------------------------------------
template <fixed_string ProtoSource, fixed_string MessageName>
struct parse {
    // The template arguments, readable by callers that need to build a sibling
    // schema — the view layer resolves a nested message type this way.
    static constexpr auto proto_source = ProtoSource;
    static constexpr auto message_name = MessageName;

    static consteval ast::file get_file() {
        return parser(ProtoSource.view()).parse_file();
    }

    static consteval const ast::message* find_msg_recursive(const ast::message& m, std::string_view name) {
        if (m.name == name) return &m;
        if (name.starts_with(m.name) && name.size() > m.name.size() + 1 && name[m.name.size()] == '.') {
            std::string_view rest = name.substr(m.name.size() + 1);
            for (std::size_t i = 0; i < m.nested_messages.size(); ++i) {
                if (const auto* found = find_msg_recursive(m.nested_messages[i], rest)) return found;
            }
        }
        for (std::size_t i = 0; i < m.nested_messages.size(); ++i) {
            if (const auto* found = find_msg_recursive(m.nested_messages[i], name)) return found;
        }
        return nullptr;
    }

    static consteval const ast::message* find_msg_in(const ast::file& f, std::string_view name) {
        // 1. Exact or recursive match across root messages
        for (std::size_t i = 0; i < f.messages.size(); ++i) {
            if (const auto* found = find_msg_recursive(f.messages[i], name)) return found;
        }

        // 2. Package-prefixed match: package.Message -> Message
        if (f.package.has_value()) {
            std::string_view pkg = *f.package;
            if (name.starts_with(pkg)) {
                std::string_view sub = name.substr(pkg.size());
                if (sub.starts_with('.')) sub.remove_prefix(1);
                for (std::size_t i = 0; i < f.messages.size(); ++i) {
                    if (const auto* found = find_msg_recursive(f.messages[i], sub)) return found;
                }
            }
        }

        return nullptr;
    }

    // Declared fields only. Type synthesis and the C++20 member mapping both
    // index by this, so it deliberately excludes maps and oneof branches.
    static consteval std::size_t field_count() {
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        return m ? m->fields.size() : 0;
    }

    // Everything that occupies a field number on the wire: the declared
    // fields, then the map fields, then the oneof branches. A decoder that
    // walks this sees a map or a oneof branch as the field it is, rather than
    // as an unknown field to be skipped unchecked.
    static consteval std::size_t total_field_count() {
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        if (!m) return 0;
        std::size_t n = m->fields.size() + m->maps.size();
        for (std::size_t i = 0; i < m->oneofs.size(); ++i) n += m->oneofs[i].branches.size();
        return n;
    }

    static consteval std::string_view type_leaf(std::string_view t) {
        const auto pos = t.rfind('.');
        return pos == std::string_view::npos ? t : t.substr(pos + 1);
    }

    static consteval bool enum_declared_in(const ast::message& m, std::string_view leaf) {
        for (std::size_t i = 0; i < m.nested_enums.size(); ++i) {
            if (m.nested_enums[i].name == leaf) return true;
        }
        for (std::size_t i = 0; i < m.nested_messages.size(); ++i) {
            if (enum_declared_in(m.nested_messages[i], leaf)) return true;
        }
        return false;
    }

    // Resolves a field's type name to enum or message. Searching only the
    // file's top-level declarations, as this once did, left every type
    // declared inside a message unclassified: a nested enum then decoded as a
    // length-delimited blob and a nested message was typed as std::string.
    static consteval void classify_type(const ast::file& f, const ast::message* owner,
                                        std::string_view type_name, field_info& info) {
        const std::string_view leaf = type_leaf(type_name);
        if (owner) {
            for (std::size_t i = 0; i < owner->nested_enums.size(); ++i) {
                if (owner->nested_enums[i].name == leaf) { info.is_enum = true; return; }
            }
            for (std::size_t i = 0; i < owner->nested_messages.size(); ++i) {
                if (owner->nested_messages[i].name == leaf) { info.is_submessage = true; return; }
            }
        }
        for (std::size_t i = 0; i < f.top_level_enums.size(); ++i) {
            if (f.top_level_enums[i].name == leaf) { info.is_enum = true; return; }
        }
        for (std::size_t i = 0; i < f.messages.size(); ++i) {
            if (enum_declared_in(f.messages[i], leaf)) { info.is_enum = true; return; }
        }
        for (std::size_t i = 0; i < f.messages.size(); ++i) {
            if (find_msg_recursive(f.messages[i], leaf)) { info.is_submessage = true; return; }
        }
    }

    // Every declared field from a single parse. get_field() re-parses the
    // schema on each call, so asking it once per field costs N parses inside
    // one constant evaluation: type synthesis for a 25-field message ran out
    // of the compiler's default step budget that way.
    static consteval std::vector<field_info> declared_fields() {
        std::vector<field_info> out;
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        if (!m) return out;
        for (std::size_t i = 0; i < m->fields.size(); ++i) {
            const auto& fld = m->fields[i];
            field_info info{};
            info.name = fld.name.value_or("");
            info.number = fld.number;
            info.type = fld.type.name;
            info.is_repeated = (fld.label == ast::field_label::repeated);
            info.is_optional = fld.proto3_optional;
            classify_type(f, m, fld.type.name, info);
            out.push_back(info);
        }
        return out;
    }

    static consteval field_info get_field(std::size_t idx) {
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        if (!m) return field_info{};

        if (idx < m->fields.size()) {
            const auto& fld = m->fields[idx];
            field_info info{};
            info.name = fld.name.value_or("");
            info.number = fld.number;
            info.type = fld.type.name;
            info.is_repeated = (fld.label == ast::field_label::repeated);
            info.is_optional = fld.proto3_optional;
            classify_type(f, m, fld.type.name, info);
            return info;
        }

        std::size_t rest = idx - m->fields.size();
        if (rest < m->maps.size()) {
            const auto& mp = m->maps[rest];
            field_info info{};
            info.name = mp.name;
            info.number = mp.number;
            info.is_repeated = true;
            info.is_map = true;
            info.key_type = mp.key_type.name;
            info.value_type = mp.value_type.name;
            return info;
        }
        rest -= m->maps.size();

        for (std::size_t i = 0; i < m->oneofs.size(); ++i) {
            const auto& o = m->oneofs[i];
            if (rest < o.branches.size()) {
                const auto& br = o.branches[rest];
                field_info info{};
                info.name = br.name;
                info.number = br.number;
                info.type = br.type.name;
                info.is_oneof_branch = true;
                info.oneof_name = o.name;
                classify_type(f, m, br.type.name, info);
                return info;
            }
            rest -= o.branches.size();
        }
        return field_info{};
    }
};

// The schema's fields as a constant table.
//
// Every consteval accessor on `parse` re-parses the .proto text, because a
// constexpr function cannot cache anything across calls. Reading one field per
// call therefore costs one parse per field, which is quadratic in the schema
// and is what used to exhaust the compiler's step budget on a 25-field
// message. A variable template is initialised once per schema per translation
// unit, so everything that needs per-field metadata indexes this instead.
template <typename Schema, std::size_t N>
consteval std::array<field_info, N> make_field_table() {
    std::array<field_info, N> table{};
    const auto fields = Schema::declared_fields();
    for (std::size_t i = 0; i < N && i < fields.size(); ++i) table[i] = fields[i];
    return table;
}

template <typename Schema>
inline constexpr auto field_table = make_field_table<Schema, Schema::field_count()>();

template <typename Schema, std::size_t I>
inline constexpr field_info field_at = field_table<Schema>[I];

// Forward declaration of message_impl
template <fixed_string ProtoSource, fixed_string CanonicalName>
class message_impl;

// Canonical alias for user consumption
template <fixed_string ProtoSource, fixed_string MessageName>
using message = message_impl<
    ProtoSource,
    to_fixed_string<64>(MessageName.view())
>;

// ---------------------------------------------------------------------------
// Concepts for Encodable / Decodable types
// ---------------------------------------------------------------------------
template <typename T>
concept HasByteSize = requires(const T& t) {
    { t.byte_size() } -> std::same_as<std::size_t>;
};

template <typename T>
concept HasEncodeToPtr = requires(const T& t, std::byte* dest) {
    { t.encode_to_ptr(dest) } -> std::same_as<std::byte*>;
};

template <typename T>
concept CustomEncodable = requires(const T& t, std::vector<std::byte>& out) {
    { t.encode_into(out) } -> std::same_as<void>;
};

template <typename T>
concept CustomDecodable = requires(T& t, std::span<const std::byte> b, wire::decode_error& err) {
    { t.decode_from(b, err) } -> std::same_as<void>;
};

// ---------------------------------------------------------------------------
// Detail helpers
// ---------------------------------------------------------------------------
namespace detail {

template <typename T>
concept IsVector = requires(T t) {
    typename T::value_type;
    { t.push_back(std::declval<typename T::value_type>()) } -> std::same_as<void>;
    { t.clear() } -> std::same_as<void>;
};

template <typename T>
concept IsString = std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, std::string_view>;

// A singular `bytes` field is stored as vector<byte> directly (not wrapped in an
// outer vector the way a `repeated` field is), so this type alone identifies it
// and lets it take the length-delimited-blob path instead of the repeated-element path.
template <typename T>
concept IsByteBlob = std::is_same_v<std::decay_t<T>, std::vector<std::byte>>;

// proto3 packs repeated scalar numerics (and enums) into one length-delimited
// field by default; strings, bytes and submessages are never packed.
template <typename T>
concept IsPackableScalar = std::is_integral_v<T> || std::is_enum_v<T> || std::is_floating_point_v<T>;

} // namespace detail

// Forward declaration of top-level byte_size and encode
template <typename T>
std::size_t byte_size(const T& msg);

template <typename T>
std::byte* encode_to_ptr(const T& msg, std::byte* dest);

namespace detail {

// Wire size / write helpers for one element of a packed repeated scalar field
// (proto3 packs repeated int/enum/float/double into a single length-delimited
// field; strings, bytes and submessages never pack, and are handled elsewhere).
// proto3 gives a scalar four possible encodings, and the C++ member type does
// not distinguish them: int32, sint32 and sfixed32 are all std::int32_t, while
// their wire forms are a varint, a zigzag varint and four raw bytes. Every
// size, write and read decision below therefore keys on the declared proto
// type. Reading it off the C++ type alone is what made fixed32, sfixed32 and
// sfixed64 fields encode as varints and decode as zero.
enum class scalar_enc { varint, zigzag32, zigzag64, fix32, fix64 };

constexpr scalar_enc enc_of(std::string_view t) noexcept {
    if (t == "sint32") return scalar_enc::zigzag32;
    if (t == "sint64") return scalar_enc::zigzag64;
    if (t == "fixed32" || t == "sfixed32") return scalar_enc::fix32;
    if (t == "fixed64" || t == "sfixed64") return scalar_enc::fix64;
    return scalar_enc::varint;
}

constexpr wire::wire_type wire_type_of(scalar_enc e) noexcept {
    switch (e) {
        case scalar_enc::fix32: return wire::wire_type::fixed32;
        case scalar_enc::fix64: return wire::wire_type::fixed64;
        default:                return wire::wire_type::varint;
    }
}

constexpr bool is_signed_fixed(std::string_view t) noexcept {
    return t == "sfixed32" || t == "sfixed64";
}

template <typename Elem>
inline std::size_t packed_elem_size(const field_info& info, const Elem& item) {
    if constexpr (std::is_floating_point_v<Elem>) {
        return sizeof(Elem) == 4 ? 4 : 8;
    } else {
        switch (enc_of(info.type)) {
            case scalar_enc::zigzag32:
                return wire::varint_size(wire::encode_zigzag32(static_cast<std::int32_t>(item)));
            case scalar_enc::zigzag64:
                return wire::varint_size(wire::encode_zigzag64(static_cast<std::int64_t>(item)));
            case scalar_enc::fix32: return 4;
            case scalar_enc::fix64: return 8;
            default: return wire::varint_size(static_cast<std::uint64_t>(item));
        }
    }
}

template <typename Elem>
inline std::byte* write_packed_elem_to(std::byte* dest, const field_info& info, const Elem& item) {
    if constexpr (std::is_floating_point_v<Elem>) {
        if constexpr (sizeof(Elem) == 4) return wire::write_float_to(dest, static_cast<float>(item));
        else return wire::write_double_to(dest, static_cast<double>(item));
    } else {
        switch (enc_of(info.type)) {
            case scalar_enc::zigzag32:
                return wire::write_zigzag32_to(dest, static_cast<std::int32_t>(item));
            case scalar_enc::zigzag64:
                return wire::write_zigzag64_to(dest, static_cast<std::int64_t>(item));
            case scalar_enc::fix32:
                return wire::write_fixed32_to(dest, static_cast<std::uint32_t>(item));
            case scalar_enc::fix64:
                return wire::write_fixed64_to(dest, static_cast<std::uint64_t>(item));
            default:
                return wire::write_varint_to(dest, static_cast<std::uint64_t>(item));
        }
    }
}

template <typename T>
inline std::size_t field_byte_size(const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T>) {
        if (!value.empty()) {
            return wire::tag_size(info.number) + wire::varint_size(value.size()) + value.size();
        }
        return 0;
    } else if constexpr (IsString<T>) {
        if (!value.empty()) {
            return wire::tag_size(info.number) + wire::varint_size(value.size()) + value.size();
        }
        return 0;
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (value != 0) {
            switch (enc_of(info.type)) {
                case scalar_enc::zigzag32:
                    return wire::tag_size(info.number)
                         + wire::varint_size(wire::encode_zigzag32(static_cast<std::int32_t>(value)));
                case scalar_enc::zigzag64:
                    return wire::tag_size(info.number)
                         + wire::varint_size(wire::encode_zigzag64(static_cast<std::int64_t>(value)));
                case scalar_enc::fix32: return wire::tag_size(info.number) + 4;
                case scalar_enc::fix64: return wire::tag_size(info.number) + 8;
                default:
                    return wire::tag_size(info.number)
                         + wire::varint_size(static_cast<std::uint64_t>(value));
            }
        }
        return 0;
    } else if constexpr (std::is_floating_point_v<T>) {
        if (value != 0.0) {
            return wire::tag_size(info.number) + (sizeof(T) == 4 ? 4 : 8);
        }
        return 0;
    } else if constexpr (IsVector<T>) {
        using Elem = typename T::value_type;
        if constexpr (CustomEncodable<Elem> || HasByteSize<Elem>) {
            std::size_t sz = 0;
            for (const auto& item : value) {
                std::size_t sub_sz = byte_size(item);
                sz += wire::tag_size(info.number) + wire::varint_size(sub_sz) + sub_sz;
            }
            return sz;
        } else if constexpr (IsString<Elem> || IsByteBlob<Elem>) {
            std::size_t sz = 0;
            for (const auto& item : value) {
                sz += wire::tag_size(info.number) + wire::varint_size(item.size()) + item.size();
            }
            return sz;
        } else if constexpr (detail::IsPackableScalar<Elem>) {
            // Packed: one tag, one length, then every element back to back.
            if (value.empty()) return 0;
            std::size_t payload = 0;
            for (const auto& item : value) payload += packed_elem_size(info, item);
            return wire::tag_size(info.number) + wire::varint_size(payload) + payload;
        }
        return 0;
    } else if constexpr (CustomEncodable<T> || HasByteSize<T>) {
        std::size_t sub_sz = byte_size(value);
        return wire::tag_size(info.number) + wire::varint_size(sub_sz) + sub_sz;
    }
    return 0;
}

template <typename T>
inline std::byte* encode_field_to_ptr(std::byte* dest, const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T>) {
        if (!value.empty()) {
            dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
            dest = wire::write_length_delimited_to(dest, std::span<const std::byte>(value.data(), value.size()));
        }
    } else if constexpr (IsString<T>) {
        if (!value.empty()) {
            dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
            dest = wire::write_string_to(dest, value);
        }
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (value != 0) {
            const auto enc = enc_of(info.type);
            dest = wire::write_tag_to(dest, info.number, wire_type_of(enc));
            switch (enc) {
                case scalar_enc::zigzag32:
                    dest = wire::write_zigzag32_to(dest, static_cast<std::int32_t>(value)); break;
                case scalar_enc::zigzag64:
                    dest = wire::write_zigzag64_to(dest, static_cast<std::int64_t>(value)); break;
                case scalar_enc::fix32:
                    dest = wire::write_fixed32_to(dest, static_cast<std::uint32_t>(value)); break;
                case scalar_enc::fix64:
                    dest = wire::write_fixed64_to(dest, static_cast<std::uint64_t>(value)); break;
                default:
                    dest = wire::write_varint_to(dest, static_cast<std::uint64_t>(value)); break;
            }
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (value != 0.0) {
            if constexpr (sizeof(T) == 4) {
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::fixed32);
                dest = wire::write_float_to(dest, static_cast<float>(value));
            } else {
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::fixed64);
                dest = wire::write_double_to(dest, static_cast<double>(value));
            }
        }
    } else if constexpr (IsVector<T>) {
        using Elem = typename T::value_type;
        if constexpr (CustomEncodable<Elem> || HasByteSize<Elem>) {
            for (const auto& item : value) {
                std::size_t sub_sz = byte_size(item);
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
                dest = wire::write_varint_to(dest, sub_sz);
                dest = encode_to_ptr(item, dest);
            }
        } else if constexpr (IsString<Elem>) {
            for (const auto& item : value) {
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
                dest = wire::write_string_to(dest, item);
            }
        } else if constexpr (IsByteBlob<Elem>) {
            for (const auto& item : value) {
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
                dest = wire::write_length_delimited_to(dest, std::span<const std::byte>(item.data(), item.size()));
            }
        } else if constexpr (detail::IsPackableScalar<Elem>) {
            // Packed: one tag, one length, then every element back to back.
            if (!value.empty()) {
                std::size_t payload = 0;
                for (const auto& item : value) payload += packed_elem_size(info, item);
                dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
                dest = wire::write_varint_to(dest, payload);
                for (const auto& item : value) dest = write_packed_elem_to(dest, info, item);
            }
        }
    } else if constexpr (CustomEncodable<T> || HasByteSize<T>) {
        std::size_t sub_sz = byte_size(value);
        dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
        dest = wire::write_varint_to(dest, sub_sz);
        dest = encode_to_ptr(value, dest);
    }
    return dest;
}

template <typename T>
inline void encode_field_by_info(std::vector<std::byte>& out, const field_info& info, const T& value) {
    std::size_t sz = field_byte_size(info, value);
    if (sz > 0) {
        std::size_t old_sz = out.size();
        out.resize(old_sz + sz);
        encode_field_to_ptr(out.data() + old_sz, info, value);
    }
}

inline bool read_packed_elem(wire::reader& r, const field_info& info, auto& out) {
    using Elem = std::decay_t<decltype(out)>;
    if constexpr (std::is_floating_point_v<Elem>) {
        if constexpr (sizeof(Elem) == 4) {
            float v = 0; if (!r.read_float(v)) return false; out = v; return true;
        } else {
            double v = 0; if (!r.read_double(v)) return false; out = v; return true;
        }
    } else {
        switch (enc_of(info.type)) {
            case scalar_enc::zigzag32: {
                std::int32_t v = 0; if (!r.read_zigzag32(v)) return false; out = static_cast<Elem>(v); return true;
            }
            case scalar_enc::zigzag64: {
                std::int64_t v = 0; if (!r.read_zigzag64(v)) return false; out = static_cast<Elem>(v); return true;
            }
            case scalar_enc::fix32: {
                std::uint32_t v = 0; if (!r.read_fixed32(v)) return false;
                out = is_signed_fixed(info.type) ? static_cast<Elem>(static_cast<std::int32_t>(v))
                                                 : static_cast<Elem>(v);
                return true;
            }
            case scalar_enc::fix64: {
                std::uint64_t v = 0; if (!r.read_fixed64(v)) return false;
                out = is_signed_fixed(info.type) ? static_cast<Elem>(static_cast<std::int64_t>(v))
                                                 : static_cast<Elem>(v);
                return true;
            }
            default: {
                std::uint64_t v = 0; if (!r.read_varint(v)) return false; out = static_cast<Elem>(v); return true;
            }
        }
    }
}

template <typename T>
inline bool decode_field_by_info(wire::reader& r, wire::wire_type wt, const field_info& info, T& value,
                                 wire::decode_error::kind* why = nullptr) {
    if constexpr (IsByteBlob<T>) {
        if (wt == wire::wire_type::len) {
            std::span<const std::byte> b;
            if (!r.read_length_delimited(b)) return false;
            value.assign(b.begin(), b.end());
            return true;
        }
    } else if constexpr (IsString<T>) {
        if (wt == wire::wire_type::len) {
            std::string_view sv;
            if (!r.read_string(sv)) return false;
            // proto3 requires a `string` field to carry structurally valid UTF-8 and a
            // message that breaks it to be rejected on parse.
            //
            // Gated on the PROTO type, not on the C++ type, and that distinction is
            // load-bearing rather than pedantic. Under P2996 a `bytes` field is generated
            // as std::vector<std::byte> and takes the IsByteBlob branch above, so the C++
            // type would have been enough. In the C++20 fallback the user declares the
            // member themselves and a `bytes` field is ordinarily a std::string, which is
            // indistinguishable here from a `string` field by type alone -- so validating
            // on IsString would reject perfectly valid `bytes` payloads on that path only.
            // A test caught this; the comment that preceded it did not.
            //
            // `bytes` is an arbitrary octet sequence with no encoding obligation. It is
            // exempt deliberately, not by omission.
            if (info.type == "string" && !wire::valid_utf8(sv)) {
                if (why) *why = wire::decode_error::invalid_utf8;
                return false;
            }
            value = std::string(sv);
            return true;
        }
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        const auto enc = enc_of(info.type);
        // A field whose wire type does not match its declared type falls
        // through to skip_field below, as an unknown field would.
        if (wt == wire::wire_type::fixed32 && enc == scalar_enc::fix32) {
            std::uint32_t v = 0;
            if (!r.read_fixed32(v)) return false;
            value = is_signed_fixed(info.type) ? static_cast<T>(static_cast<std::int32_t>(v))
                                               : static_cast<T>(v);
            return true;
        } else if (wt == wire::wire_type::fixed64 && enc == scalar_enc::fix64) {
            std::uint64_t v = 0;
            if (!r.read_fixed64(v)) return false;
            value = is_signed_fixed(info.type) ? static_cast<T>(static_cast<std::int64_t>(v))
                                               : static_cast<T>(v);
            return true;
        } else if (wt == wire::wire_type::varint && enc == scalar_enc::zigzag32) {
            std::int32_t v = 0;
            if (!r.read_zigzag32(v)) return false;
            value = static_cast<T>(v);
            return true;
        } else if (wt == wire::wire_type::varint && enc == scalar_enc::zigzag64) {
            std::int64_t v = 0;
            if (!r.read_zigzag64(v)) return false;
            value = static_cast<T>(v);
            return true;
        } else if (wt == wire::wire_type::varint && enc == scalar_enc::varint) {
            std::uint64_t v = 0;
            if (!r.read_varint(v)) return false;
            value = static_cast<T>(v);
            return true;
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == 4) {
            if (wt == wire::wire_type::fixed32) {
                float v = 0;
                if (!r.read_float(v)) return false;
                value = v;
                return true;
            }
        } else {
            if (wt == wire::wire_type::fixed64) {
                double v = 0;
                if (!r.read_double(v)) return false;
                value = v;
                return true;
            }
        }
    } else if constexpr (IsVector<T>) {
        using Elem = typename T::value_type;
        if constexpr (CustomDecodable<Elem>) {
            if (wt == wire::wire_type::len) {
                std::span<const std::byte> sub_bytes;
                if (!r.read_length_delimited(sub_bytes)) return false;
                Elem item{};
                wire::decode_error err{};
                item.decode_from(sub_bytes, err);
                if (err) return false;
                value.push_back(std::move(item));
                return true;
            }
        } else if constexpr (IsString<Elem>) {
            if (wt == wire::wire_type::len) {
                std::string_view sv;
                if (!r.read_string(sv)) return false;
                // Same obligation, and the same proto-type gate: see the singular case
                // above for why the C++ type is not sufficient to make this decision.
                if (info.type == "string" && !wire::valid_utf8(sv)) {
                    if (why) *why = wire::decode_error::invalid_utf8;
                    return false;
                }
                value.push_back(std::string(sv));
                return true;
            }
        } else if constexpr (IsByteBlob<Elem>) {
            if (wt == wire::wire_type::len) {
                std::span<const std::byte> b;
                if (!r.read_length_delimited(b)) return false;
                value.push_back(Elem(b.begin(), b.end()));
                return true;
            }
        } else if constexpr (detail::IsPackableScalar<Elem>) {
            const wire::wire_type unpacked_wt = std::is_floating_point_v<Elem>
                ? (sizeof(Elem) == 4 ? wire::wire_type::fixed32 : wire::wire_type::fixed64)
                : wire_type_of(enc_of(info.type));
            if (wt == wire::wire_type::len) {
                // Packed: one blob holding every element back to back (the proto3 default).
                std::span<const std::byte> sub_bytes;
                if (!r.read_length_delimited(sub_bytes)) return false;
                wire::reader sub{sub_bytes};
                while (!sub.empty()) {
                    Elem item{};
                    if (!read_packed_elem(sub, info, item)) return false;
                    value.push_back(item);
                }
                return true;
            } else if (wt == unpacked_wt) {
                // Unpacked: one field occurrence per element (accepted for backward compatibility).
                Elem item{};
                if (!read_packed_elem(r, info, item)) return false;
                value.push_back(item);
                return true;
            }
        }
    } else if constexpr (CustomDecodable<T>) {
        if (wt == wire::wire_type::len) {
            std::span<const std::byte> sub_bytes;
            if (!r.read_length_delimited(sub_bytes)) return false;
            wire::decode_error err{};
            value.decode_from(sub_bytes, err);
            return !err;
        }
    }
    return r.skip_field(wt);
}

template <fixed_string ProtoSource, typename Schema, std::size_t I>
struct schema_field_type {
    static consteval auto get_type_tag() {
        constexpr auto f = Schema::get_field(I);
        if constexpr (f.type == "string") return std::string{};
        else if constexpr (f.type == "int32" || f.type == "sint32" || f.type == "sfixed32") return std::int32_t{};
        else if constexpr (f.type == "uint32" || f.type == "fixed32") return std::uint32_t{};
        else if constexpr (f.type == "int64" || f.type == "sint64" || f.type == "sfixed64") return std::int64_t{};
        else if constexpr (f.type == "uint64" || f.type == "fixed64") return std::uint64_t{};
        else if constexpr (f.type == "float") return float{};
        else if constexpr (f.type == "double") return double{};
        else if constexpr (f.type == "bool") return bool{};
        else if constexpr (f.type == "bytes") return std::vector<std::byte>{};
        else if constexpr (f.is_enum) return std::int32_t{};
        else if constexpr (f.is_submessage) {
            constexpr auto sub_name = to_fixed_string<64>(f.type);
            return message_impl<ProtoSource, sub_name>{};
        }
        else return std::string{};
    }

    using scalar_type = decltype(get_type_tag());
    using type = std::conditional_t<Schema::get_field(I).is_repeated, std::vector<scalar_type>, scalar_type>;
};

template <fixed_string ProtoSource, typename Schema, std::size_t I>
using schema_field_type_t = typename schema_field_type<ProtoSource, Schema, I>::type;

template <fixed_string ProtoSource, typename Schema, typename Indices>
struct message_storage_helper;

template <fixed_string ProtoSource, typename Schema, std::size_t... Is>
struct message_storage_helper<ProtoSource, Schema, std::index_sequence<Is...>> {
    using type = std::tuple<schema_field_type_t<ProtoSource, Schema, Is>...>;
};

template <fixed_string ProtoSource, typename Schema>
using message_storage_t = typename message_storage_helper<
    ProtoSource,
    Schema,
    std::make_index_sequence<Schema::field_count()>
>::type;

template <typename Schema, fixed_string FieldName>
consteval std::size_t field_index_by_name() {
    constexpr auto count = Schema::field_count();
    for (std::size_t i = 0; i < count; ++i) {
        if (Schema::get_field(i).name == FieldName.view()) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

} // namespace detail

// ---------------------------------------------------------------------------
// 1. Semi-automatic reflection base class (for custom user structs)
// ---------------------------------------------------------------------------
template <typename Schema, typename Derived>
struct reflect_base {
    [[nodiscard]] std::size_t byte_size() const {
        const auto& self = static_cast<const Derived&>(*this);
        auto members = const_cast<Derived&>(self).reflect_members();
        constexpr auto member_count = std::tuple_size_v<decltype(members)>;
        constexpr auto schema_field_count = Schema::field_count();
        std::size_t total = 0;

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if constexpr (Is < schema_field_count) {
                    constexpr auto field = field_at<Schema, Is>;
                    total += detail::field_byte_size(field, std::get<Is>(members));
                }
            }(), ...);
        }(std::make_index_sequence<member_count>{});

        return total;
    }

    std::byte* encode_to_ptr(std::byte* dest) const {
        const auto& self = static_cast<const Derived&>(*this);
        auto members = const_cast<Derived&>(self).reflect_members();
        constexpr auto member_count = std::tuple_size_v<decltype(members)>;
        constexpr auto schema_field_count = Schema::field_count();

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if constexpr (Is < schema_field_count) {
                    constexpr auto field = field_at<Schema, Is>;
                    dest = detail::encode_field_to_ptr(dest, field, std::get<Is>(members));
                }
            }(), ...);
        }(std::make_index_sequence<member_count>{});

        return dest;
    }

    void encode_into(std::vector<std::byte>& out) const {
        std::size_t sz = byte_size();
        out.resize(sz);
        if (sz > 0) {
            encode_to_ptr(out.data());
        }
    }

    void decode_from(std::span<const std::byte> bytes, wire::decode_error& err) {
        auto& self = static_cast<Derived&>(*this);
        auto members = self.reflect_members();
        constexpr auto member_count = std::tuple_size_v<decltype(members)>;
        constexpr auto schema_field_count = Schema::field_count();

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                using MemberType = std::decay_t<decltype(std::get<Is>(members))>;
                if constexpr (detail::IsVector<MemberType>) {
                    std::get<Is>(members).clear();
                }
            }(), ...);
        }(std::make_index_sequence<member_count>{});

        wire::reader r{bytes};
        while (!r.empty()) {
            std::int64_t num = 0;
            wire::wire_type wt = wire::wire_type::varint;
            if (!r.read_tag(num, wt)) {
                err.err = wire::decode_error::bad_varint;
                return;
            }

            bool matched = false;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    if constexpr (Is < schema_field_count) {
                        constexpr auto field = field_at<Schema, Is>;
                        if (num == field.number) {
                            auto why = wire::decode_error::truncated;
                            if (!detail::decode_field_by_info(r, wt, field, std::get<Is>(members), &why)) {
                                err.err = why;
                            }
                            matched = true;
                        }
                    }
                }(), ...);
            }(std::make_index_sequence<member_count>{});

            if (!matched) {
                if (!r.skip_field(wt)) {
                    err.err = wire::decode_error::truncated;
                    return;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// 2. Fully-Generated Message Type
// ---------------------------------------------------------------------------
template <fixed_string ProtoSource, fixed_string CanonicalName>
class message_impl {
public:
    using schema_type = parse<ProtoSource, CanonicalName>;
    static constexpr std::size_t field_count = schema_type::field_count();
    using storage_type = detail::message_storage_t<ProtoSource, schema_type>;

    constexpr message_impl() = default;

    template <fixed_string Name>
    [[nodiscard]] constexpr decltype(auto) get() const {
        constexpr std::size_t idx = detail::field_index_by_name<schema_type, Name>();
        static_assert(idx != static_cast<std::size_t>(-1), "Field name not found in message schema");
        return std::get<idx>(fields_);
    }

    template <fixed_string Name>
    [[nodiscard]] constexpr decltype(auto) get() {
        constexpr std::size_t idx = detail::field_index_by_name<schema_type, Name>();
        static_assert(idx != static_cast<std::size_t>(-1), "Field name not found in message schema");
        return std::get<idx>(fields_);
    }

    template <fixed_string Name, typename ValT>
    constexpr void set(ValT&& val) {
        constexpr std::size_t idx = detail::field_index_by_name<schema_type, Name>();
        static_assert(idx != static_cast<std::size_t>(-1), "Field name not found in message schema");
        std::get<idx>(fields_) = std::forward<ValT>(val);
    }

    template <std::size_t Index>
    [[nodiscard]] constexpr decltype(auto) field() const {
        static_assert(Index < field_count, "Field index out of bounds");
        return std::get<Index>(fields_);
    }

    template <std::size_t Index>
    [[nodiscard]] constexpr decltype(auto) field() {
        static_assert(Index < field_count, "Field index out of bounds");
        return std::get<Index>(fields_);
    }

    [[nodiscard]] std::size_t byte_size() const {
        std::size_t total = 0;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                constexpr auto info = schema_type::get_field(Is);
                total += detail::field_byte_size(info, std::get<Is>(fields_));
            }(), ...);
        }(std::make_index_sequence<field_count>{});
        return total;
    }

    std::byte* encode_to_ptr(std::byte* dest) const {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                constexpr auto info = schema_type::get_field(Is);
                dest = detail::encode_field_to_ptr(dest, info, std::get<Is>(fields_));
            }(), ...);
        }(std::make_index_sequence<field_count>{});
        return dest;
    }

    void encode_into(std::vector<std::byte>& out) const {
        std::size_t sz = byte_size();
        out.resize(sz);
        if (sz > 0) {
            encode_to_ptr(out.data());
        }
    }

    void decode_from(std::span<const std::byte> bytes, wire::decode_error& err) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                using MemberType = std::decay_t<decltype(std::get<Is>(fields_))>;
                if constexpr (detail::IsVector<MemberType>) {
                    std::get<Is>(fields_).clear();
                }
            }(), ...);
        }(std::make_index_sequence<field_count>{});

        wire::reader r{bytes};
        while (!r.empty()) {
            std::int64_t num = 0;
            wire::wire_type wt = wire::wire_type::varint;
            if (!r.read_tag(num, wt)) {
                err.err = wire::decode_error::bad_varint;
                return;
            }

            bool matched = false;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    constexpr auto info = schema_type::get_field(Is);
                    if (num == info.number) {
                        auto why = wire::decode_error::truncated;
                        if (!detail::decode_field_by_info(r, wt, info, std::get<Is>(fields_), &why)) {
                            err.err = why;
                        }
                        matched = true;
                    }
                }(), ...);
            }(std::make_index_sequence<field_count>{});

            if (!matched) {
                if (!r.skip_field(wt)) {
                    err.err = wire::decode_error::truncated;
                    return;
                }
            }
        }
    }

private:
    storage_type fields_{};
};

template <typename T>
struct message_schema_traits;

template <typename T>
concept HasSchemaTraits = requires {
    typename message_schema_traits<T>::Schema;
};

#if CTPB_HAS_P2996
namespace reflect {

template <fixed_string ProtoSource, fixed_string MessageName>
consteval auto complete_message_struct(std::meta::info incomplete_type) {
    using Schema = parse<ProtoSource, MessageName>;

    std::vector<std::meta::info> members;

    for (const field_info& info : Schema::declared_fields()) {
        std::meta::info type_info = ^^int;
        if (info.type == "string") {
            type_info = ^^std::string;
        } else if (info.type == "int32" || info.type == "sint32" || info.type == "sfixed32") {
            type_info = ^^int;
        } else if (info.type == "uint32" || info.type == "fixed32") {
            type_info = ^^unsigned int;
        } else if (info.type == "int64" || info.type == "sint64" || info.type == "sfixed64") {
            type_info = ^^long long;
        } else if (info.type == "uint64" || info.type == "fixed64") {
            type_info = ^^unsigned long long;
        } else if (info.type == "float") {
            type_info = ^^float;
        } else if (info.type == "double") {
            type_info = ^^double;
        } else if (info.type == "bool") {
            type_info = ^^bool;
        } else if (info.type == "bytes") {
            type_info = ^^std::vector<std::byte>;
        }

        // An enum field keeps the default `int` above: proto3 encodes enums as
        // varints, and the synthesised aggregate has no enum type to name.

        if (info.is_repeated) {
            type_info = std::meta::substitute(^^std::vector, {type_info});
        }

        members.push_back(std::meta::data_member_spec(
            type_info,
            {.name = info.name}
        ));
    }

    return std::meta::define_aggregate(incomplete_type, members);
}

template <typename T>
consteval auto get_members() {
    auto vec = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current());
    std::array<std::meta::info, message_schema_traits<T>::Schema::field_count()> arr{};
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = vec[i];
    }
    return arr;
}

template <typename T, std::size_t I>
consteval std::meta::info get_member_i() {
    return get_members<T>()[I];
}

template <typename T, std::size_t I>
inline std::size_t field_byte_size_ith(const T& msg) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto f_info = field_at<Schema, I>;
    constexpr auto m_info = get_member_i<T, I>();
    return detail::field_byte_size(f_info, msg.[:m_info:]);
}

template <typename T, std::size_t I>
inline std::byte* encode_ith_field_to_ptr(const T& msg, std::byte* dest) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto f_info = field_at<Schema, I>;
    constexpr auto m_info = get_member_i<T, I>();
    return detail::encode_field_to_ptr(dest, f_info, msg.[:m_info:]);
}

template <typename T, std::size_t I>
inline bool decode_ith_field(T& msg, std::int64_t num, wire::wire_type wt, wire::reader& r, wire::decode_error& err) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto f_info = field_at<Schema, I>;
    constexpr auto m_info = get_member_i<T, I>();
    if (num == f_info.number) {
        auto why = wire::decode_error::truncated;
        if (!detail::decode_field_by_info(r, wt, f_info, msg.[:m_info:], &why)) {
            err.err = why;
        }
        return true;
    }
    return false;
}

template <typename T, std::size_t I>
inline void clear_ith_field(T& msg) {
    constexpr auto m_info = get_member_i<T, I>();
    using MemberType = std::decay_t<decltype(msg.[:m_info:])>;
    if constexpr (detail::IsVector<MemberType>) {
        msg.[:m_info:].clear();
    }
}

} // namespace reflect

template <fixed_string ProtoSource, fixed_string MessageName>
struct message_storage;

template <fixed_string ProtoSource, fixed_string MessageName>
struct message_schema_traits<message_storage<ProtoSource, MessageName>> {
    using Schema = parse<ProtoSource, MessageName>;
};

template <fixed_string ProtoSource, fixed_string MessageName>
using message_t = message_storage<ProtoSource, MessageName>;

template <fixed_string ProtoSource, fixed_string MessageName>
consteval void define_message() {
    ::ctpb::reflect::complete_message_struct<ProtoSource, MessageName>(^^message_storage<ProtoSource, MessageName>);
}

template <typename Type, fixed_string ProtoSource, fixed_string MessageName>
consteval void define_message() {
    ::ctpb::reflect::complete_message_struct<ProtoSource, MessageName>(^^Type);
}

#define CTPB_DECLARE_MESSAGE(Type, ProtoSource, MessageName) \
    struct Type; \
    template <> struct ::ctpb::message_schema_traits<Type> { \
        using Schema = ::ctpb::parse<ProtoSource, MessageName>; \
    }; \
    consteval { ::ctpb::define_message<Type, ProtoSource, MessageName>(); }
#endif

// ---------------------------------------------------------------------------
// Unified byte_size, encode_to_ptr, and encode
// ---------------------------------------------------------------------------
template <typename T>
inline std::size_t byte_size(const T& msg) {
    if constexpr (HasByteSize<T>) {
        return msg.byte_size();
    }
#if CTPB_HAS_P2996
    else if constexpr (HasSchemaTraits<T>) {
        using Schema = typename message_schema_traits<T>::Schema;
        constexpr auto count = Schema::field_count();
        std::size_t total = 0;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                total += reflect::field_byte_size_ith<T, Is>(msg);
            }(), ...);
        }(std::make_index_sequence<count>{});
        return total;
    }
#endif
    else {
        return 0;
    }
}

template <typename T>
inline std::byte* encode_to_ptr(const T& msg, std::byte* dest) {
    if constexpr (HasEncodeToPtr<T>) {
        return msg.encode_to_ptr(dest);
    }
#if CTPB_HAS_P2996
    else if constexpr (HasSchemaTraits<T>) {
        using Schema = typename message_schema_traits<T>::Schema;
        constexpr auto count = Schema::field_count();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                dest = reflect::encode_ith_field_to_ptr<T, Is>(msg, dest);
            }(), ...);
        }(std::make_index_sequence<count>{});
        return dest;
    }
#endif
    else {
        return dest;
    }
}

template <typename T>
inline std::size_t encode_to(const T& msg, std::span<std::byte> dest) {
    std::size_t sz = byte_size(msg);
    if (dest.size() < sz) return 0;
    encode_to_ptr(msg, dest.data());
    return sz;
}

template <typename T>
requires (HasByteSize<T> || CustomEncodable<T> || HasSchemaTraits<T>)
inline std::vector<std::byte> encode(const T& msg) {
    if constexpr (HasByteSize<T> || HasSchemaTraits<T>) {
        std::size_t sz = byte_size(msg);
        std::vector<std::byte> out(sz);
        if (sz > 0) {
            encode_to_ptr(msg, out.data());
        }
        return out;
    } else if constexpr (CustomEncodable<T>) {
        std::vector<std::byte> out;
        msg.encode_into(out);
        return out;
    } else {
        return {};
    }
}

// Decoding into a message the caller already owns.
//
// `decode<T>()` is the ordinary way in and returns the message by value. That
// is not available everywhere: on the Bloomberg clang-p2996 branch this
// project is developed against, returning a reflection-generated message by
// value crashes the compiler outright (an assertion in
// ClassTemplateSpecializationDecl::setPointOfInstantiation) as soon as the
// message holds a repeated field, and so does wrapping it in std::expected.
// Filling an existing object compiles and runs there, so every decoder is
// written in this form and `decode<T>()` is a thin wrapper over it.
template <typename T>
requires (CustomDecodable<T>)
inline wire::decode_error decode_into(T& msg, std::span<const std::byte> bytes) {
    wire::decode_error err{};
    msg.decode_from(bytes, err);
    return err;
}

template <typename T>
requires (CustomDecodable<T>)
inline std::expected<T, wire::decode_error> decode(std::span<const std::byte> bytes) {
    T msg{};
    if (const auto err = decode_into(msg, bytes); err) {
        return std::unexpected(err);
    }
    return msg;
}

#if CTPB_HAS_P2996
template <typename T>
requires (HasSchemaTraits<T> && !CustomDecodable<T>)
inline wire::decode_error decode_into(T& msg, std::span<const std::byte> bytes) {
    using Schema = typename message_schema_traits<T>::Schema;
    wire::decode_error err{};
    constexpr auto count = Schema::field_count();

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (reflect::clear_ith_field<T, Is>(msg), ...);
    }(std::make_index_sequence<count>{});

    wire::reader r{bytes};
    while (!r.empty()) {
        std::int64_t num = 0;
        wire::wire_type wt = wire::wire_type::varint;
        if (!r.read_tag(num, wt)) {
            err.err = wire::decode_error::bad_varint;
            return err;
        }

        bool matched = false;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if (!matched) {
                    matched = reflect::decode_ith_field<T, Is>(msg, num, wt, r, err);
                }
            }(), ...);
        }(std::make_index_sequence<count>{});

        if (!matched) {
            if (!r.skip_field(wt)) {
                err.err = wire::decode_error::truncated;
                return err;
            }
        }
    }

    return err;
}

// Returns the message by value. On a toolchain with the defect described above
// this overload cannot be instantiated for a message with a repeated field;
// use decode_into() there.
template <typename T>
requires (HasSchemaTraits<T> && !CustomDecodable<T>)
inline std::expected<T, wire::decode_error> decode(std::span<const std::byte> bytes) {
    T msg{};
    if (const auto err = decode_into(msg, bytes); err) {
        return std::unexpected(err);
    }
    return msg;
}
#endif

} // namespace ctpb
