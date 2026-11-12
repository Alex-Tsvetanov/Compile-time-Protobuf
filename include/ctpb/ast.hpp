// include/ctpb/ast.hpp
// Plain-struct AST produced by the parser and consumed by the code generator.
// All string members are std::string_view into the embedded source buffer.

#ifndef CTPB_AST_HPP
#define CTPB_AST_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace ctpb::ast {

enum class scalar_kind : std::uint8_t {
    int32, int64, uint32, uint64,
    sint32, sint64, fixed32, fixed64, sfixed32, sfixed64,
    float32, float64, boolean, string_, bytes_,
};

enum class field_label : std::uint8_t {
    singular,  // proto3 default
    repeated,  // `repeated T f = N;`
};

enum class field_kind : std::uint8_t {
    scalar,
    message,
    enum_,
    map,       // map<K,V>
    oneof,     // synthetic — entries are in ast::oneof_branch
};

struct type_ref {
    std::string_view name;   // e.g. "int32", "Person", "MyEnum"

    [[nodiscard]] constexpr bool is_scalar() const noexcept {
        return !name.empty() && name[0] != '.' && name.front() >= 'a' && name.front() <= 'z';
    }
};

struct field {
    std::optional<std::string_view> name;       // omitted for synthetic oneof branches
    field_label                     label = field_label::singular;
    field_kind                      kind  = field_kind::scalar;
    type_ref                        type;
    std::int64_t                    number = 0;
    std::optional<std::string_view> default_value;
    bool                            proto3_optional = false;
    bool                            is_packed = true; // proto3 default for repeated scalars
};

struct enum_value {
    std::string_view name;
    std::int32_t     number = 0;
};

struct enum_def {
    std::string_view              name;
    std::vector<enum_value>       values;
    std::vector<std::int32_t>     reserved_numbers;
    std::vector<std::string_view> reserved_names;
    bool                          allow_alias = false;

    [[nodiscard]] constexpr const enum_value* find_value_by_name(std::string_view val_name) const noexcept {
        for (const auto& v : values) {
            if (v.name == val_name) return &v;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const enum_value* find_value_by_number(std::int32_t num) const noexcept {
        for (const auto& v : values) {
            if (v.number == num) return &v;
        }
        return nullptr;
    }
};

struct oneof_branch {
    std::string_view name;       // branch field name
    type_ref         type;
    std::int64_t     number = 0;
};

struct oneof_def {
    std::string_view          name;
    std::vector<oneof_branch> branches;

    [[nodiscard]] constexpr const oneof_branch* find_branch_by_name(std::string_view branch_name) const noexcept {
        for (const auto& b : branches) {
            if (b.name == branch_name) return &b;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const oneof_branch* find_branch_by_number(std::int64_t num) const noexcept {
        for (const auto& b : branches) {
            if (b.number == num) return &b;
        }
        return nullptr;
    }
};

struct map_field {
    std::string_view name;
    type_ref         key_type;
    type_ref         value_type;
    std::int64_t     number = 0;
};

// One member of a message in declaration order: a plain field, a oneof or a
// map. `index` points into message::fields, message::oneofs or message::maps.
enum class member_kind : std::uint8_t { field, oneof, map };

struct member_ref {
    member_kind kind  = member_kind::field;
    std::size_t index = 0;
};

struct reserved_range {
    std::int64_t from = 0;
    std::optional<std::int64_t> to;  // nullopt means single number

    [[nodiscard]] constexpr bool contains(std::int64_t num) const noexcept {
        if (!to.has_value()) return num == from;
        return num >= from && num <= *to;
    }
};

struct message {
    std::string_view              name;
    std::vector<field>            fields;
    std::vector<message>          nested_messages;
    std::vector<enum_def>         nested_enums;
    std::vector<oneof_def>        oneofs;
    std::vector<map_field>        maps;
    std::vector<reserved_range>   reserved_field_numbers;
    std::vector<std::string_view> reserved_field_names;
    std::vector<field>            extensions;
    std::vector<member_ref>       members;  // fields, oneofs and maps in declaration order

    [[nodiscard]] constexpr const field* find_field_by_name(std::string_view fname) const noexcept {
        for (const auto& f : fields) {
            if (f.name && *f.name == fname) return &f;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const field* find_field_by_number(std::int64_t num) const noexcept {
        for (const auto& f : fields) {
            if (f.number == num) return &f;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr bool is_reserved_number(std::int64_t num) const noexcept {
        for (const auto& r : reserved_field_numbers) {
            if (r.contains(num)) return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool is_reserved_name(std::string_view rname) const noexcept {
        for (const auto& r : reserved_field_names) {
            if (r == rname) return true;
        }
        return false;
    }
};

struct import_stmt {
    std::string_view path;     // "other.proto"
    bool             is_public = false;
    bool             is_weak   = false;
};

struct option_stmt {
    std::string_view name;     // e.g. "java_package"
    std::string_view value;
};

struct file {
    std::optional<std::string_view> syntax;        // "proto3"
    std::optional<std::string_view> package;
    std::vector<import_stmt>        imports;
    std::vector<message>            messages;
    std::vector<enum_def>           top_level_enums;
    std::vector<option_stmt>        options;
    std::vector<field>              extensions_top;

    [[nodiscard]] constexpr const message* find_top_message(std::string_view mname) const noexcept {
        for (const auto& m : messages) {
            if (m.name == mname) return &m;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr const enum_def* find_top_enum(std::string_view ename) const noexcept {
        for (const auto& e : top_level_enums) {
            if (e.name == ename) return &e;
        }
        return nullptr;
    }
};

// Helpers used by the parser and code generator.
[[nodiscard]] constexpr bool is_scalar_name(std::string_view s) noexcept {
    return s == "int32" || s == "int64" || s == "uint32" || s == "uint64"
        || s == "sint32" || s == "sint64" || s == "fixed32" || s == "fixed64"
        || s == "sfixed32" || s == "sfixed64" || s == "float" || s == "double"
        || s == "bool" || s == "string" || s == "bytes";
}

[[nodiscard]] constexpr bool is_packable_scalar(std::string_view s) noexcept {
    return is_scalar_name(s) && s != "string" && s != "bytes";
}

[[nodiscard]] constexpr scalar_kind scalar_of(std::string_view s) noexcept {
    if (s == "int32")    return scalar_kind::int32;
    if (s == "int64")    return scalar_kind::int64;
    if (s == "uint32")   return scalar_kind::uint32;
    if (s == "uint64")   return scalar_kind::uint64;
    if (s == "sint32")   return scalar_kind::sint32;
    if (s == "sint64")   return scalar_kind::sint64;
    if (s == "fixed32")  return scalar_kind::fixed32;
    if (s == "fixed64")  return scalar_kind::fixed64;
    if (s == "sfixed32") return scalar_kind::sfixed32;
    if (s == "sfixed64") return scalar_kind::sfixed64;
    if (s == "float")    return scalar_kind::float32;
    if (s == "double")   return scalar_kind::float64;
    if (s == "bool")     return scalar_kind::boolean;
    if (s == "string")   return scalar_kind::string_;
    if (s == "bytes")    return scalar_kind::bytes_;
    return scalar_kind::int32;
}

} // namespace ctpb::ast

#endif // CTPB_AST_HPP
