#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <map>
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

// P2996 names the library macro __cpp_reflection. GCC 16 defines only the
// core language macro __cpp_impl_reflection (with -freflection). Accept both.
#if defined(__cpp_reflection) || defined(__cpp_impl_reflection)
#include <meta>
#define CTPB_HAS_P2996 1
#else
#define CTPB_HAS_P2996 0
#endif

namespace ctpb {

// ---------------------------------------------------------------------------
// Fixed String Literal Type (NTTP parameter in C++20/C++26)
// ---------------------------------------------------------------------------
template <std::size_t N>
struct fixed_string {
    char buf[N]{};

    constexpr fixed_string() = default;

    constexpr fixed_string(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            buf[i] = str[i];
        }
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        // Stop at the first NUL: to_fixed_string pads short names with NULs.
        std::size_t n = 0;
        while (n < N && buf[n] != '\0') ++n;
        return std::string_view(buf, n);
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
    // For a message-typed field: the type name as found in the schema, without
    // a leading dot or package prefix (for example "Outer.Inner").
    std::string_view resolved_type;
    // What this member is. For a map, `type` and the flags above describe the
    // value and `key_type` names the key. For a oneof, `name` is the oneof
    // name and `oneof_size` is its number of alternatives.
    ast::member_kind kind = ast::member_kind::field;
    std::string_view key_type;
    std::size_t      oneof_size = 0;
};

// Passed as the part index to parse<>::get_member_part for "the member itself".
inline constexpr std::size_t whole_member = static_cast<std::size_t>(-1);

// Map keys may be any integer type, bool or string (the protobuf rule).
constexpr bool is_valid_map_key_type(std::string_view t) {
    for (std::string_view n : {"int32", "int64", "uint32", "uint64", "sint32", "sint64", "fixed32", "fixed64",
                               "sfixed32", "sfixed64", "bool", "string"}) {
        if (t == n) return true;
    }
    return false;
}

// A schema problem found while reading the proto source. The type is its own
// static_assert message, so the text shows in the compiler error.
struct schema_diagnostic {
    bool        failed = false;
    char        text[240]{};
    std::size_t len = 0;

    constexpr void add(std::string_view s) noexcept {
        for (char c : s) {
            if (len + 1 < sizeof(text)) text[len++] = c;
        }
    }
    constexpr void add(int v) noexcept {
        char tmp[12]{};
        int n = 0;
        if (v <= 0) { add("0"); return; }
        while (v > 0) { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; }
        while (n > 0) { add(std::string_view(&tmp[--n], 1)); }
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return len; }
    [[nodiscard]] constexpr const char* data() const noexcept { return text; }
};

// ---------------------------------------------------------------------------
// Constexpr Schema Trait Generator
// ---------------------------------------------------------------------------
template <fixed_string ProtoSource, fixed_string MessageName>
struct parse {
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

    static constexpr std::size_t max_scope_depth = 32;

    // Looks a type name up among the enums and messages declared in one scope.
    // Returns 1 for an enum, 2 for a message, 0 when not found. A dotted name
    // such as "Outer.Inner" descends through the nested messages.
    static consteval int lookup_in_scope(const std::vector<ast::enum_def>& enums,
                                         const std::vector<ast::message>& messages,
                                         std::string_view name) {
        const std::size_t dot = name.find('.');
        if (dot == std::string_view::npos) {
            for (std::size_t i = 0; i < enums.size(); ++i) {
                if (enums[i].name == name) return 1;
            }
            for (std::size_t i = 0; i < messages.size(); ++i) {
                if (messages[i].name == name) return 2;
            }
            return 0;
        }
        const std::string_view head = name.substr(0, dot);
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].name != head) continue;
            const int r = lookup_in_scope(messages[i].nested_enums, messages[i].nested_messages,
                                          name.substr(dot + 1));
            if (r != 0) return r;
        }
        return 0;
    }

    // Fills `chain` with the messages from the top level down to `target`.
    static consteval bool collect_chain(const ast::message& m, const ast::message* target,
                                        const ast::message** chain, std::size_t& n) {
        if (n >= max_scope_depth) return false;
        chain[n++] = &m;
        if (&m == target) return true;
        for (std::size_t i = 0; i < m.nested_messages.size(); ++i) {
            if (collect_chain(m.nested_messages[i], target, chain, n)) return true;
        }
        --n;
        return false;
    }

    // Drops a leading dot and the package prefix from a type name.
    static consteval std::string_view normalize_type(const ast::file& f, std::string_view name) {
        if (name.starts_with('.')) name.remove_prefix(1);
        if (f.package.has_value()) {
            const std::string_view pkg = *f.package;
            if (name.size() > pkg.size() && name.starts_with(pkg) && name[pkg.size()] == '.') {
                name.remove_prefix(pkg.size() + 1);
            }
        }
        return name;
    }

    // Resolves a field type like protoc does: the enclosing messages first,
    // innermost out, then the top level of the file.
    static consteval int resolve_type(const ast::file& f, const ast::message& owner,
                                      std::string_view name) {
        const ast::message* chain[max_scope_depth]{};
        std::size_t n = 0;
        for (std::size_t i = 0; i < f.messages.size() && n == 0; ++i) {
            collect_chain(f.messages[i], &owner, chain, n);
        }
        for (std::size_t i = n; i > 0; --i) {
            const int r = lookup_in_scope(chain[i - 1]->nested_enums, chain[i - 1]->nested_messages, name);
            if (r != 0) return r;
        }
        return lookup_in_scope(f.top_level_enums, f.messages, name);
    }

    // Reports what this library cannot turn into a type: syntax errors in the
    // proto source, and map fields whose key type protobuf does not allow.
    static consteval schema_diagnostic diagnose() {
        schema_diagnostic d;
        parser p(ProtoSource.view());
        const ast::file f = p.parse_file();
        if (p.failed()) {
            d.failed = true;
            d.add("ctpb: proto syntax error at line ");
            d.add(p.error_location().line);
            d.add(", column ");
            d.add(p.error_location().column);
            d.add(": ");
            d.add(p.error_prefix());
            d.add(p.error_context());
            return d;
        }
        const auto* m = find_msg_in(f, MessageName.view());
        if (m == nullptr) return d;
        for (std::size_t i = 0; i < m->maps.size(); ++i) {
            if (is_valid_map_key_type(m->maps[i].key_type.name)) continue;
            d.failed = true;
            d.add("ctpb: map field ");
            d.add(m->maps[i].name);
            d.add(" in message ");
            d.add(m->name);
            d.add(" has key type ");
            d.add(m->maps[i].key_type.name);
            d.add(", but a map key must be an integer type, bool or string");
            return d;
        }
        return d;
    }

    // Fills the type of `info` from a type name used inside message `owner`.
    static consteval void describe_type(const ast::file& f, const ast::message& owner,
                                        std::string_view raw, field_info& info) {
        info.type = raw;
        // Enum or message: search the enclosing scopes, then the top level.
        const std::string_view type_name = normalize_type(f, raw);
        const int kind = resolve_type(f, owner, type_name);
        info.is_enum = (kind == 1);
        info.is_submessage = (kind == 2);
        if (kind == 2) info.resolved_type = type_name;
    }

    static consteval field_info describe_field(const ast::file& f, const ast::message& owner,
                                               const ast::field& fld) {
        field_info info{};
        info.name = fld.name.value_or("");
        info.number = fld.number;
        info.is_repeated = (fld.label == ast::field_label::repeated);
        info.is_optional = fld.proto3_optional;
        describe_type(f, owner, fld.type.name, info);
        return info;
    }

    // Stops compilation with the diagnostic text when the schema is not usable.
    static consteval void require_supported() {
        static constexpr schema_diagnostic d = diagnose();
        static_assert(!d.failed, d);
    }

    static consteval std::size_t field_count() {
        require_supported();
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        return m ? m->fields.size() : 0;
    }

    static consteval field_info get_field(std::size_t idx) {
        require_supported();
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        if (!m || idx >= m->fields.size()) return field_info{};
        return describe_field(f, *m, m->fields[idx]);
    }

    // Members are what the generated struct holds, in declaration order: each
    // plain field, each oneof (one member for the whole group) and each map.
    // field_count() and get_field() above list only the plain fields.
    static consteval std::size_t member_count() {
        require_supported();
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        return m ? m->members.size() : 0;
    }

    static consteval field_info get_member(std::size_t idx) {
        require_supported();
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        if (!m || idx >= m->members.size()) return field_info{};
        const ast::member_ref ref = m->members[idx];
        if (ref.kind == ast::member_kind::field) return describe_field(f, *m, m->fields[ref.index]);

        field_info info{};
        info.kind = ref.kind;
        if (ref.kind == ast::member_kind::oneof) {
            const auto& od = m->oneofs[ref.index];
            info.name = od.name;
            info.oneof_size = od.branches.size();
        } else {
            const auto& mf = m->maps[ref.index];
            info.name = mf.name;
            info.number = mf.number;
            info.key_type = mf.key_type.name;
            describe_type(f, *m, mf.value_type.name, info);
        }
        return info;
    }

    // One part of a member, encoded like a plain singular field:
    // for a oneof, alternative `part`; for a map entry, the key (part 0, field
    // number 1) or the value (part 1, field number 2). For a plain field, or
    // with part == whole_member, the member itself.
    static consteval field_info get_member_part(std::size_t idx, std::size_t part) {
        const field_info whole = get_member(idx);
        if (part == whole_member || whole.kind == ast::member_kind::field) return whole;
        auto f = get_file();
        const auto* m = find_msg_in(f, MessageName.view());
        const ast::member_ref ref = m->members[idx];
        field_info info{};
        if (ref.kind == ast::member_kind::oneof) {
            const auto& branch = m->oneofs[ref.index].branches[part];
            info.name = branch.name;
            info.number = branch.number;
            describe_type(f, *m, branch.type.name, info);
        } else {
            const auto& mf = m->maps[ref.index];
            info.name = part == 0 ? "key" : "value";
            info.number = part == 0 ? 1 : 2;
            describe_type(f, *m, part == 0 ? mf.key_type.name : mf.value_type.name, info);
        }
        return info;
    }
};

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

// Schema traits mark a type whose fields are listed by static reflection
// (see define_message). Such a type has no member functions, so the free
// functions byte_size, encode_to_ptr and decode handle it.
template <typename T>
struct message_schema_traits;

template <typename T>
concept HasSchemaTraits = requires {
    typename message_schema_traits<T>::Schema;
};

#if CTPB_HAS_P2996
template <fixed_string ProtoSource, fixed_string MessageName>
struct message_storage;

namespace reflect {
template <typename T>
void decode_into(T& msg, std::span<const std::byte> bytes, wire::decode_error& err);
}
#endif

// ---------------------------------------------------------------------------
// Detail helpers
// ---------------------------------------------------------------------------
namespace detail {

// A field type that is a nested message: it has its own encoder, or it is
// a reflected struct.
template <typename T>
concept IsEncodableMessage = CustomEncodable<T> || HasByteSize<T> || HasSchemaTraits<T>;

template <typename T>
concept IsDecodableMessage = CustomDecodable<T> || HasSchemaTraits<T>;

template <typename T>
inline void decode_submessage(T& msg, std::span<const std::byte> bytes, wire::decode_error& err) {
    if constexpr (CustomDecodable<T>) {
        msg.decode_from(bytes, err);
    }
#if CTPB_HAS_P2996
    else {
        reflect::decode_into(msg, bytes, err);
    }
#endif
}

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

// fixed32/sfixed32 use 4 raw little-endian bytes, fixed64/sfixed64 use 8.
inline bool is_fixed32_type(const field_info& info) {
    return info.type == "fixed32" || info.type == "sfixed32";
}
inline bool is_fixed64_type(const field_info& info) {
    return info.type == "fixed64" || info.type == "sfixed64";
}

// Wire size / write helpers for one element of a packed repeated scalar field
// (proto3 packs repeated int/enum/float/double into a single length-delimited
// field; strings, bytes and submessages never pack, and are handled elsewhere).
template <typename Elem>
inline std::size_t packed_elem_size(const field_info& info, const Elem& item) {
    if constexpr (std::is_floating_point_v<Elem>) {
        return sizeof(Elem) == 4 ? 4 : 8;
    } else {
        if (is_fixed32_type(info)) return 4;
        if (is_fixed64_type(info)) return 8;
        if (info.type == "sint32") return wire::varint_size(wire::encode_zigzag32(static_cast<std::int32_t>(item)));
        if (info.type == "sint64") return wire::varint_size(wire::encode_zigzag64(static_cast<std::int64_t>(item)));
        return wire::varint_size(static_cast<std::uint64_t>(item));
    }
}

template <typename Elem>
inline std::byte* write_packed_elem_to(std::byte* dest, const field_info& info, const Elem& item) {
    if constexpr (std::is_floating_point_v<Elem>) {
        if constexpr (sizeof(Elem) == 4) return wire::write_float_to(dest, static_cast<float>(item));
        else return wire::write_double_to(dest, static_cast<double>(item));
    } else {
        if (is_fixed32_type(info)) return wire::write_fixed32_to(dest, static_cast<std::uint32_t>(item));
        if (is_fixed64_type(info)) return wire::write_fixed64_to(dest, static_cast<std::uint64_t>(item));
        if (info.type == "sint32") return wire::write_zigzag32_to(dest, static_cast<std::int32_t>(item));
        if (info.type == "sint64") return wire::write_zigzag64_to(dest, static_cast<std::int64_t>(item));
        return wire::write_varint_to(dest, static_cast<std::uint64_t>(item));
    }
}

template <typename>
inline constexpr bool always_false_v = false;

// --- One singular value, written with no default check -------------------
// A plain proto3 field skips zero and empty values. An engaged optional, the
// active oneof alternative and both halves of a map entry are always written,
// so they use these helpers directly.

// The wire type a singular value of C++ type T uses for the field `info`.
template <typename T>
inline wire::wire_type value_wire_type(const field_info& info) {
    if constexpr (IsByteBlob<T> || IsString<T> || IsEncodableMessage<T>) {
        return wire::wire_type::len;
    } else if constexpr (std::is_floating_point_v<T>) {
        return sizeof(T) == 4 ? wire::wire_type::fixed32 : wire::wire_type::fixed64;
    } else {
        if (is_fixed32_type(info)) return wire::wire_type::fixed32;
        if (is_fixed64_type(info)) return wire::wire_type::fixed64;
        return wire::wire_type::varint;
    }
}

// Bytes of the value after its tag (for len types: the length prefix and payload).
template <typename T>
inline std::size_t value_size(const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T> || IsString<T>) {
        return wire::varint_size(value.size()) + value.size();
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (is_fixed32_type(info)) return 4;
        if (is_fixed64_type(info)) return 8;
        if (info.type == "sint32") return wire::varint_size(wire::encode_zigzag32(static_cast<std::int32_t>(value)));
        if (info.type == "sint64") return wire::varint_size(wire::encode_zigzag64(static_cast<std::int64_t>(value)));
        return wire::varint_size(static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        return sizeof(T) == 4 ? 4 : 8;
    } else if constexpr (IsEncodableMessage<T>) {
        const std::size_t sub_sz = byte_size(value);
        return wire::varint_size(sub_sz) + sub_sz;
    } else {
        static_assert(always_false_v<T>, "ctpb: unsupported C++ type for a singular protobuf value");
        return 0;
    }
}

template <typename T>
inline std::byte* write_value_to(std::byte* dest, const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T>) {
        return wire::write_length_delimited_to(dest, std::span<const std::byte>(value.data(), value.size()));
    } else if constexpr (IsString<T>) {
        return wire::write_string_to(dest, value);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (is_fixed32_type(info)) return wire::write_fixed32_to(dest, static_cast<std::uint32_t>(value));
        if (is_fixed64_type(info)) return wire::write_fixed64_to(dest, static_cast<std::uint64_t>(value));
        if (info.type == "sint32") return wire::write_zigzag32_to(dest, static_cast<std::int32_t>(value));
        if (info.type == "sint64") return wire::write_zigzag64_to(dest, static_cast<std::int64_t>(value));
        return wire::write_varint_to(dest, static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == 4) return wire::write_float_to(dest, static_cast<float>(value));
        else return wire::write_double_to(dest, static_cast<double>(value));
    } else if constexpr (IsEncodableMessage<T>) {
        dest = wire::write_varint_to(dest, byte_size(value));
        return encode_to_ptr(value, dest);
    } else {
        static_assert(always_false_v<T>, "ctpb: unsupported C++ type for a singular protobuf value");
        return dest;
    }
}

// Tag plus value, always.
template <typename T>
inline std::size_t present_size(const field_info& info, const T& value) {
    return wire::tag_size(info.number) + value_size(info, value);
}

template <typename T>
inline std::byte* write_present_to(std::byte* dest, const field_info& info, const T& value) {
    dest = wire::write_tag_to(dest, info.number, value_wire_type<T>(info));
    return write_value_to(dest, info, value);
}

template <typename T>
inline std::size_t field_byte_size(const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T> || IsString<T>) {
        return value.empty() ? 0 : present_size(info, value);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        return value == 0 ? 0 : present_size(info, value);
    } else if constexpr (std::is_floating_point_v<T>) {
        return value == 0.0 ? 0 : present_size(info, value);
    } else if constexpr (IsVector<T>) {
        using Elem = typename T::value_type;
        if constexpr (IsEncodableMessage<Elem>) {
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
    } else if constexpr (IsEncodableMessage<T>) {
        return present_size(info, value);
    }
    return 0;
}

template <typename T>
inline std::byte* encode_field_to_ptr(std::byte* dest, const field_info& info, const T& value) {
    if constexpr (IsByteBlob<T> || IsString<T>) {
        if (!value.empty()) dest = write_present_to(dest, info, value);
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (value != 0) dest = write_present_to(dest, info, value);
    } else if constexpr (std::is_floating_point_v<T>) {
        if (value != 0.0) dest = write_present_to(dest, info, value);
    } else if constexpr (IsVector<T>) {
        using Elem = typename T::value_type;
        if constexpr (IsEncodableMessage<Elem>) {
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
    } else if constexpr (IsEncodableMessage<T>) {
        dest = write_present_to(dest, info, value);
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
        if (is_fixed32_type(info)) {
            std::uint32_t v = 0; if (!r.read_fixed32(v)) return false; out = static_cast<Elem>(v); return true;
        } else if (is_fixed64_type(info)) {
            std::uint64_t v = 0; if (!r.read_fixed64(v)) return false; out = static_cast<Elem>(v); return true;
        }
        if (info.type == "sint32") {
            std::int32_t v = 0; if (!r.read_zigzag32(v)) return false; out = static_cast<Elem>(v); return true;
        } else if (info.type == "sint64") {
            std::int64_t v = 0; if (!r.read_zigzag64(v)) return false; out = static_cast<Elem>(v); return true;
        } else {
            std::uint64_t v = 0; if (!r.read_varint(v)) return false; out = static_cast<Elem>(v); return true;
        }
    }
}

template <typename T>
inline bool decode_field_by_info(wire::reader& r, wire::wire_type wt, const field_info& info, T& value) {
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
            value = std::string(sv);
            return true;
        }
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (is_fixed32_type(info)) {
            if (wt == wire::wire_type::fixed32) {
                std::uint32_t v = 0;
                if (!r.read_fixed32(v)) return false;
                value = static_cast<T>(v);
                return true;
            }
        } else if (is_fixed64_type(info)) {
            if (wt == wire::wire_type::fixed64) {
                std::uint64_t v = 0;
                if (!r.read_fixed64(v)) return false;
                value = static_cast<T>(v);
                return true;
            }
        } else if (wt == wire::wire_type::varint) {
            if (info.type == "sint32") {
                std::int32_t v = 0;
                if (!r.read_zigzag32(v)) return false;
                value = static_cast<T>(v);
                return true;
            } else if (info.type == "sint64") {
                std::int64_t v = 0;
                if (!r.read_zigzag64(v)) return false;
                value = static_cast<T>(v);
                return true;
            } else {
                std::uint64_t v = 0;
                if (!r.read_varint(v)) return false;
                value = static_cast<T>(v);
                return true;
            }
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
        if constexpr (IsDecodableMessage<Elem>) {
            if (wt == wire::wire_type::len) {
                std::span<const std::byte> sub_bytes;
                if (!r.read_length_delimited(sub_bytes)) return false;
                Elem item{};
                wire::decode_error err{};
                decode_submessage(item, sub_bytes, err);
                if (err) return false;
                value.push_back(std::move(item));
                return true;
            }
        } else if constexpr (IsString<Elem>) {
            if (wt == wire::wire_type::len) {
                std::string_view sv;
                if (!r.read_string(sv)) return false;
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
                : is_fixed32_type(info) ? wire::wire_type::fixed32
                : is_fixed64_type(info) ? wire::wire_type::fixed64
                : wire::wire_type::varint;
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
    } else if constexpr (IsDecodableMessage<T>) {
        if (wt == wire::wire_type::len) {
            std::span<const std::byte> sub_bytes;
            if (!r.read_length_delimited(sub_bytes)) return false;
            wire::decode_error err{};
            decode_submessage(value, sub_bytes, err);
            return !err;
        }
    }
    return r.skip_field(wt, info.number);
}

// --- C++ types of the generated members ------------------------------------
// One mapping serves both code paths. Reflected = true is the P2996 path,
// where a message field holds a message_storage struct (define_message) and
// 64-bit fields keep the `long long` types that path has always used.

template <typename T>
struct is_std_optional : std::false_type {};
template <typename T>
struct is_std_optional<std::optional<T>> : std::true_type {};

template <typename T>
struct is_std_variant : std::false_type {};
template <typename... Ts>
struct is_std_variant<std::variant<Ts...>> : std::true_type {};

template <typename T>
concept IsMapLike = requires(T& m) {
    typename T::key_type;
    typename T::mapped_type;
    m.insert_or_assign(std::declval<typename T::key_type>(), std::declval<typename T::mapped_type>());
};

// The C++ type of one singular value: a plain field, a oneof alternative,
// or a map key or value. Returns std::type_identity<T>.
template <fixed_string ProtoSource, bool Reflected, typename Schema, std::size_t I, std::size_t Part>
consteval auto value_type_tag() {
    constexpr field_info f = Schema::get_member_part(I, Part);
    if constexpr (f.type == "string") return std::type_identity<std::string>{};
    else if constexpr (f.type == "int32" || f.type == "sint32" || f.type == "sfixed32")
        return std::type_identity<std::int32_t>{};
    else if constexpr (f.type == "uint32" || f.type == "fixed32") return std::type_identity<std::uint32_t>{};
    else if constexpr (f.type == "int64" || f.type == "sint64" || f.type == "sfixed64")
        return std::type_identity<std::conditional_t<Reflected, long long, std::int64_t>>{};
    else if constexpr (f.type == "uint64" || f.type == "fixed64")
        return std::type_identity<std::conditional_t<Reflected, unsigned long long, std::uint64_t>>{};
    else if constexpr (f.type == "float") return std::type_identity<float>{};
    else if constexpr (f.type == "double") return std::type_identity<double>{};
    else if constexpr (f.type == "bool") return std::type_identity<bool>{};
    else if constexpr (f.type == "bytes") return std::type_identity<std::vector<std::byte>>{};
    else if constexpr (f.is_enum) return std::type_identity<std::int32_t>{};
    else if constexpr (f.is_submessage) {
#if CTPB_HAS_P2996
        if constexpr (Reflected) {
            // The message type must already be defined: call define_message for it first,
            // with the same name the field uses (without package prefix).
            constexpr auto sub_name = to_fixed_string<f.resolved_type.size() + 1>(f.resolved_type);
            return std::type_identity<message_storage<ProtoSource, sub_name>>{};
        } else
#endif
        {
            static_assert(f.resolved_type.size() < 64, "ctpb: message type name is too long (limit 63 characters)");
            constexpr auto sub_name = to_fixed_string<64>(f.resolved_type);
            return std::type_identity<message_impl<ProtoSource, sub_name>>{};
        }
    } else {
        static_assert(f.is_enum, "ctpb: unknown field type: it is not a scalar, and no enum or message "
                                 "with that name is visible from the message (imports are not resolved)");
        return std::type_identity<std::string>{};
    }
}

template <fixed_string ProtoSource, bool Reflected, typename Schema, std::size_t I, std::size_t Part>
using value_type_t = typename decltype(value_type_tag<ProtoSource, Reflected, Schema, I, Part>())::type;

// The C++ type of member I:
//   plain field   T, std::vector<T> when repeated, std::optional<T> when `optional`
//   oneof         std::variant<std::monostate, T1, T2, ...> in declaration order
//   map<K, V>     std::map<K, V>
template <fixed_string ProtoSource, bool Reflected, typename Schema, std::size_t I>
consteval auto member_type_tag() {
    constexpr field_info f = Schema::get_member(I);
    if constexpr (f.kind == ast::member_kind::oneof) {
        return []<std::size_t... Js>(std::index_sequence<Js...>) {
            return std::type_identity<
                std::variant<std::monostate, value_type_t<ProtoSource, Reflected, Schema, I, Js>...>>{};
        }(std::make_index_sequence<f.oneof_size>{});
    } else if constexpr (f.kind == ast::member_kind::map) {
        return std::type_identity<std::map<value_type_t<ProtoSource, Reflected, Schema, I, 0>,
                                           value_type_t<ProtoSource, Reflected, Schema, I, 1>>>{};
    } else {
        using S = value_type_t<ProtoSource, Reflected, Schema, I, whole_member>;
        if constexpr (f.is_repeated) return std::type_identity<std::vector<S>>{};
        else if constexpr (f.is_optional) return std::type_identity<std::optional<S>>{};
        else return std::type_identity<S>{};
    }
}

template <fixed_string ProtoSource, bool Reflected, typename Schema, std::size_t I>
using member_type_t = typename decltype(member_type_tag<ProtoSource, Reflected, Schema, I>())::type;

// --- Encoding and decoding one member ---------------------------------------
// Every message type (message_impl, reflect_base and the P2996 structs) goes
// through these four functions, one call per member.

// Checks that the C++ member can hold schema member I, so that a hand-written
// reflect_base struct cannot silently lose presence or alternatives. Returns
// false after the static_assert, so the callers skip their body and the
// compiler shows only that message.
template <typename Schema, std::size_t I, typename V>
consteval bool check_member_type() {
    constexpr field_info info = Schema::get_member(I);
    if constexpr (info.kind == ast::member_kind::oneof) {
        constexpr bool is_variant = is_std_variant<V>::value;
        static_assert(is_variant, "ctpb: a oneof member must be a std::variant<std::monostate, ...>");
        if constexpr (is_variant) {
            constexpr bool sized = std::variant_size_v<V> == info.oneof_size + 1;
            static_assert(sized, "ctpb: a oneof variant needs std::monostate plus one alternative per oneof "
                                 "field, in declaration order");
            return sized;
        }
        return is_variant;
    } else if constexpr (info.kind == ast::member_kind::map) {
        static_assert(IsMapLike<V>, "ctpb: a map member must be a map type such as std::map<K, V>");
        return IsMapLike<V>;
    } else if constexpr (info.is_optional) {
        static_assert(is_std_optional<V>::value,
                      "ctpb: a proto3 optional field must be a std::optional member (explicit presence)");
        return is_std_optional<V>::value;
    } else {
        return true;
    }
}

template <typename Schema, std::size_t I, typename V>
inline std::size_t member_byte_size_unchecked(const V& value) {
    constexpr field_info info = Schema::get_member(I);
    if constexpr (info.kind == ast::member_kind::oneof) {
        // Only the active alternative is written, even when it holds a default value.
        std::size_t total = 0;
        auto one = [&]<std::size_t J>() {
            constexpr field_info alt = Schema::get_member_part(I, J);
            if (value.index() == J + 1) total = present_size(alt, std::get<J + 1>(value));
        };
        [&]<std::size_t... Js>(std::index_sequence<Js...>) {
            (one.template operator()<Js>(), ...);
        }(std::make_index_sequence<info.oneof_size>{});
        return total;
    } else if constexpr (info.kind == ast::member_kind::map) {
        // Each entry is a nested message: key as field 1, value as field 2, both always written.
        constexpr field_info key_info = Schema::get_member_part(I, 0);
        constexpr field_info val_info = Schema::get_member_part(I, 1);
        std::size_t total = 0;
        for (const auto& [k, v] : value) {
            const std::size_t entry = present_size(key_info, k) + present_size(val_info, v);
            total += wire::tag_size(info.number) + wire::varint_size(entry) + entry;
        }
        return total;
    } else if constexpr (info.is_optional) {
        // Explicit presence: an engaged value is written even when it is zero or empty.
        return value.has_value() ? present_size(info, *value) : 0;
    } else {
        return field_byte_size(info, value);
    }
}

template <typename Schema, std::size_t I, typename V>
inline std::byte* member_encode_to_ptr_unchecked(std::byte* dest, const V& value) {
    constexpr field_info info = Schema::get_member(I);
    if constexpr (info.kind == ast::member_kind::oneof) {
        auto one = [&]<std::size_t J>() {
            constexpr field_info alt = Schema::get_member_part(I, J);
            if (value.index() == J + 1) dest = write_present_to(dest, alt, std::get<J + 1>(value));
        };
        [&]<std::size_t... Js>(std::index_sequence<Js...>) {
            (one.template operator()<Js>(), ...);
        }(std::make_index_sequence<info.oneof_size>{});
        return dest;
    } else if constexpr (info.kind == ast::member_kind::map) {
        constexpr field_info key_info = Schema::get_member_part(I, 0);
        constexpr field_info val_info = Schema::get_member_part(I, 1);
        for (const auto& [k, v] : value) {
            const std::size_t entry = present_size(key_info, k) + present_size(val_info, v);
            dest = wire::write_tag_to(dest, info.number, wire::wire_type::len);
            dest = wire::write_varint_to(dest, entry);
            dest = write_present_to(dest, key_info, k);
            dest = write_present_to(dest, val_info, v);
        }
        return dest;
    } else if constexpr (info.is_optional) {
        return value.has_value() ? write_present_to(dest, info, *value) : dest;
    } else {
        return encode_field_to_ptr(dest, info, value);
    }
}

// Reads one map entry (key field 1, value field 2) into `map`. A missing key
// or value stays at its default. A repeated key replaces the earlier entry.
template <typename Map>
inline bool read_map_entry(std::span<const std::byte> bytes, const field_info& key_info,
                           const field_info& val_info, Map& map) {
    typename Map::key_type key{};
    typename Map::mapped_type val{};
    wire::reader r{bytes};
    while (!r.empty()) {
        std::int64_t num = 0;
        wire::wire_type wt = wire::wire_type::varint;
        if (!r.read_tag(num, wt)) return false;
        bool ok = false;
        if (num == 1) ok = decode_field_by_info(r, wt, key_info, key);
        else if (num == 2) ok = decode_field_by_info(r, wt, val_info, val);
        else ok = r.skip_field(wt, num);
        if (!ok) return false;
    }
    map.insert_or_assign(std::move(key), std::move(val));
    return true;
}

// Decodes one field occurrence when field number `num` belongs to member I.
// Returns false when it does not belong; `ok` turns false on a read error.
template <typename Schema, std::size_t I, typename V>
inline bool member_decode_unchecked(wire::reader& r, std::int64_t num, wire::wire_type wt, V& value, bool& ok) {
    constexpr field_info info = Schema::get_member(I);
    if constexpr (info.kind == ast::member_kind::oneof) {
        // The last alternative seen on the wire wins. A repeated alternative
        // decodes into the active value: a message alternative keeps the plain
        // fields that the second occurrence does not set, but its repeated,
        // optional, oneof and map members are replaced, not merged.
        bool matched = false;
        auto one = [&]<std::size_t J>() {
            constexpr field_info alt = Schema::get_member_part(I, J);
            using Alt = std::variant_alternative_t<J + 1, V>;
            if (matched || num != alt.number) return;
            matched = true;
            if (wt != value_wire_type<Alt>(alt)) {
                ok = r.skip_field(wt, num);
                return;
            }
            if (value.index() != J + 1) value.template emplace<J + 1>();
            ok = decode_field_by_info(r, wt, alt, std::get<J + 1>(value));
        };
        [&]<std::size_t... Js>(std::index_sequence<Js...>) {
            (one.template operator()<Js>(), ...);
        }(std::make_index_sequence<info.oneof_size>{});
        return matched;
    } else if constexpr (info.kind == ast::member_kind::map) {
        if (num != info.number) return false;
        if (wt != wire::wire_type::len) {
            ok = r.skip_field(wt, num);
            return true;
        }
        std::span<const std::byte> entry;
        ok = r.read_length_delimited(entry) &&
             read_map_entry(entry, Schema::get_member_part(I, 0), Schema::get_member_part(I, 1), value);
        return true;
    } else if constexpr (info.is_optional) {
        if (num != info.number) return false;
        using T = typename V::value_type;
        // Engage only on a read of the right wire type; anything else is skipped.
        if (wt != value_wire_type<T>(info)) {
            ok = r.skip_field(wt, num);
            return true;
        }
        if (!value.has_value()) value.emplace();
        ok = decode_field_by_info(r, wt, info, *value);
        return true;
    } else {
        if (num != info.number) return false;
        ok = decode_field_by_info(r, wt, info, value);
        return true;
    }
}

// Clears member I before a decode.
template <typename Schema, std::size_t I, typename V>
inline void member_reset_unchecked(V& value) {
    constexpr field_info info = Schema::get_member(I);
    if constexpr (info.kind == ast::member_kind::oneof) {
        value.template emplace<0>();
    } else if constexpr (info.kind == ast::member_kind::map) {
        value.clear();
    } else if constexpr (info.is_optional) {
        value.reset();
    } else if constexpr (IsVector<V>) {
        value.clear();
    }
}

template <typename Schema, std::size_t I, typename V>
inline std::size_t member_byte_size(const V& value) {
    if constexpr (check_member_type<Schema, I, V>()) return member_byte_size_unchecked<Schema, I>(value);
    else return 0;
}

template <typename Schema, std::size_t I, typename V>
inline std::byte* member_encode_to_ptr(std::byte* dest, const V& value) {
    if constexpr (check_member_type<Schema, I, V>()) return member_encode_to_ptr_unchecked<Schema, I>(dest, value);
    else return dest;
}

template <typename Schema, std::size_t I, typename V>
inline bool member_decode(wire::reader& r, std::int64_t num, wire::wire_type wt, V& value, bool& ok) {
    if constexpr (check_member_type<Schema, I, V>()) return member_decode_unchecked<Schema, I>(r, num, wt, value, ok);
    else return false;
}

template <typename Schema, std::size_t I, typename V>
inline void member_reset(V& value) {
    if constexpr (check_member_type<Schema, I, V>()) member_reset_unchecked<Schema, I>(value);
}

template <fixed_string ProtoSource, typename Schema, typename Indices>
struct message_storage_helper;

template <fixed_string ProtoSource, typename Schema, std::size_t... Is>
struct message_storage_helper<ProtoSource, Schema, std::index_sequence<Is...>> {
    using type = std::tuple<member_type_t<ProtoSource, false, Schema, Is>...>;
};

template <fixed_string ProtoSource, typename Schema>
using message_storage_t = typename message_storage_helper<
    ProtoSource,
    Schema,
    std::make_index_sequence<Schema::member_count()>
>::type;

template <typename Schema, fixed_string FieldName>
consteval std::size_t field_index_by_name() {
    constexpr auto count = Schema::member_count();
    for (std::size_t i = 0; i < count; ++i) {
        if (Schema::get_member(i).name == FieldName.view()) {
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
        constexpr auto schema_member_count = Schema::member_count();
        std::size_t total = 0;

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if constexpr (Is < schema_member_count) {
                    total += detail::member_byte_size<Schema, Is>(std::get<Is>(members));
                }
            }(), ...);
        }(std::make_index_sequence<member_count>{});

        return total;
    }

    std::byte* encode_to_ptr(std::byte* dest) const {
        const auto& self = static_cast<const Derived&>(*this);
        auto members = const_cast<Derived&>(self).reflect_members();
        constexpr auto member_count = std::tuple_size_v<decltype(members)>;
        constexpr auto schema_member_count = Schema::member_count();

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if constexpr (Is < schema_member_count) {
                    dest = detail::member_encode_to_ptr<Schema, Is>(dest, std::get<Is>(members));
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
        constexpr auto schema_member_count = Schema::member_count();

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if constexpr (Is < schema_member_count) {
                    detail::member_reset<Schema, Is>(std::get<Is>(members));
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
            bool ok = true;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    if constexpr (Is < schema_member_count) {
                        if (!matched) {
                            matched = detail::member_decode<Schema, Is>(r, num, wt, std::get<Is>(members), ok);
                        }
                    }
                }(), ...);
            }(std::make_index_sequence<member_count>{});

            if (!matched) ok = r.skip_field(wt, num);
            if (!ok) {
                err.err = wire::decode_error::truncated;
                return;
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
    // Number of members: plain fields, oneofs and maps (see parse<>::member_count).
    static constexpr std::size_t field_count = schema_type::member_count();
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
                total += detail::member_byte_size<schema_type, Is>(std::get<Is>(fields_));
            }(), ...);
        }(std::make_index_sequence<field_count>{});
        return total;
    }

    std::byte* encode_to_ptr(std::byte* dest) const {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                dest = detail::member_encode_to_ptr<schema_type, Is>(dest, std::get<Is>(fields_));
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
                detail::member_reset<schema_type, Is>(std::get<Is>(fields_));
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
            bool ok = true;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ([&]() {
                    if (!matched) {
                        matched = detail::member_decode<schema_type, Is>(r, num, wt, std::get<Is>(fields_), ok);
                    }
                }(), ...);
            }(std::make_index_sequence<field_count>{});

            if (!matched) ok = r.skip_field(wt, num);
            if (!ok) {
                err.err = wire::decode_error::truncated;
                return;
            }
        }
    }

private:
    storage_type fields_{};
};

#if CTPB_HAS_P2996
namespace reflect {

template <fixed_string ProtoSource, fixed_string MessageName>
consteval std::meta::info complete_message_struct_impl(std::meta::info incomplete_type,
                                                       std::vector<std::meta::info>& in_progress);

// Defines the struct for a message that a field uses, unless it is already
// complete or is being defined further up (a message that contains itself
// through a repeated field or a map).
template <fixed_string ProtoSource, fixed_string MessageName>
consteval void define_dependency(std::vector<std::meta::info>& in_progress) {
    constexpr std::meta::info type = ^^message_storage<ProtoSource, MessageName>;
    if (std::meta::is_complete_type(type)) return;
    for (const std::meta::info open : in_progress) {
        if (open == type) return;
    }
    complete_message_struct_impl<ProtoSource, MessageName>(type, in_progress);
}

// Defines the structs of every message type that member I holds by value or
// in a container: a plain field, each oneof alternative, or the map value.
template <fixed_string ProtoSource, typename Schema, std::size_t I>
consteval void define_member_dependencies(std::vector<std::meta::info>& in_progress) {
    constexpr field_info info = Schema::get_member(I);
    auto define_part = [&]<std::size_t Part>() {
        constexpr field_info part = Schema::get_member_part(I, Part);
        if constexpr (part.is_submessage) {
            constexpr auto sub_name = to_fixed_string<part.resolved_type.size() + 1>(part.resolved_type);
            define_dependency<ProtoSource, sub_name>(in_progress);
        }
    };
    if constexpr (info.kind == ast::member_kind::oneof) {
        [&]<std::size_t... Js>(std::index_sequence<Js...>) {
            (define_part.template operator()<Js>(), ...);
        }(std::make_index_sequence<info.oneof_size>{});
    } else if constexpr (info.kind == ast::member_kind::map) {
        define_part.template operator()<1>();
    } else {
        define_part.template operator()<whole_member>();
    }
}

template <fixed_string ProtoSource, fixed_string MessageName>
consteval std::meta::info complete_message_struct_impl(std::meta::info incomplete_type,
                                                       std::vector<std::meta::info>& in_progress) {
    using Schema = parse<ProtoSource, MessageName>;
    constexpr auto count = Schema::member_count();

    // Message types used by the fields are defined first, so define_message
    // works in any order.
    in_progress.push_back(incomplete_type);
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (define_member_dependencies<ProtoSource, Schema, Is>(in_progress), ...);
    }(std::make_index_sequence<count>{});

    std::vector<std::meta::info> members;

    auto append_member = [&]<std::size_t I>() {
        constexpr auto info = Schema::get_member(I);
        // Same member types as message_impl; a message field holds message_storage.
        members.push_back(std::meta::data_member_spec(
            ^^detail::member_type_t<ProtoSource, true, Schema, I>,
            {.name = info.name}
        ));
    };

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (append_member.template operator()<Is>(), ...);
    }(std::make_index_sequence<count>{});

    return std::meta::define_aggregate(incomplete_type, members);
}

template <fixed_string ProtoSource, fixed_string MessageName>
consteval std::meta::info complete_message_struct(std::meta::info incomplete_type) {
    std::vector<std::meta::info> in_progress;
    return complete_message_struct_impl<ProtoSource, MessageName>(incomplete_type, in_progress);
}

template <typename T>
consteval auto get_members() {
    auto vec = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current());
    std::array<std::meta::info, message_schema_traits<T>::Schema::member_count()> arr{};
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
    constexpr auto m_info = get_member_i<T, I>();
    return detail::member_byte_size<Schema, I>(msg.[:m_info:]);
}

template <typename T, std::size_t I>
inline std::byte* encode_ith_field_to_ptr(const T& msg, std::byte* dest) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto m_info = get_member_i<T, I>();
    return detail::member_encode_to_ptr<Schema, I>(dest, msg.[:m_info:]);
}

template <typename T, std::size_t I>
inline bool decode_ith_field(T& msg, std::int64_t num, wire::wire_type wt, wire::reader& r, bool& ok) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto m_info = get_member_i<T, I>();
    return detail::member_decode<Schema, I>(r, num, wt, msg.[:m_info:], ok);
}

template <typename T, std::size_t I>
inline void clear_ith_field(T& msg) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto m_info = get_member_i<T, I>();
    detail::member_reset<Schema, I>(msg.[:m_info:]);
}

// Decodes `bytes` into `msg`. Used for the top-level decode and for nested messages.
template <typename T>
inline void decode_into(T& msg, std::span<const std::byte> bytes, wire::decode_error& err) {
    using Schema = typename message_schema_traits<T>::Schema;
    constexpr auto count = Schema::member_count();

    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (clear_ith_field<T, Is>(msg), ...);
    }(std::make_index_sequence<count>{});

    wire::reader r{bytes};
    while (!r.empty()) {
        std::int64_t num = 0;
        wire::wire_type wt = wire::wire_type::varint;
        if (!r.read_tag(num, wt)) {
            err.err = wire::decode_error::bad_varint;
            return;
        }

        bool matched = false;
        bool ok = true;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&]() {
                if (!matched) {
                    matched = decode_ith_field<T, Is>(msg, num, wt, r, ok);
                }
            }(), ...);
        }(std::make_index_sequence<count>{});

        if (!matched) ok = r.skip_field(wt, num);
        if (!ok) {
            err.err = wire::decode_error::truncated;
            return;
        }
    }
}

} // namespace reflect

template <fixed_string ProtoSource, fixed_string MessageName>
struct message_schema_traits<message_storage<ProtoSource, MessageName>> {
    using Schema = parse<ProtoSource, MessageName>;
};

template <fixed_string ProtoSource, fixed_string MessageName>
using message_t = message_storage<ProtoSource, MessageName>;

template <fixed_string ProtoSource, fixed_string MessageName>
consteval void define_message() {
    // A message may already be defined as a dependency of an earlier define_message.
    constexpr std::meta::info type = ^^message_storage<ProtoSource, MessageName>;
    if (std::meta::is_complete_type(type)) return;
    ::ctpb::reflect::complete_message_struct<ProtoSource, MessageName>(type);
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
        constexpr auto count = Schema::member_count();
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
        constexpr auto count = Schema::member_count();
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

template <typename T>
requires (CustomDecodable<T>)
inline std::expected<T, wire::decode_error> decode(std::span<const std::byte> bytes) {
    T msg{};
    wire::decode_error err{};
    msg.decode_from(bytes, err);
    if (err) {
        return std::unexpected(err);
    }
    return msg;
}

#if CTPB_HAS_P2996
template <typename T>
requires (HasSchemaTraits<T> && !CustomDecodable<T>)
inline std::expected<T, wire::decode_error> decode(std::span<const std::byte> bytes) {
    T msg{};
    wire::decode_error err{};
    reflect::decode_into(msg, bytes, err);
    if (err) {
        return std::unexpected(err);
    }
    return msg;
}
#endif

} // namespace ctpb
