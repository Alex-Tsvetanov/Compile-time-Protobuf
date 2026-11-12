#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace ctpb::wire {

enum class wire_type : std::uint8_t {
    varint = 0,
    fixed64 = 1,
    len = 2,
    start_group = 3,
    end_group = 4,
    fixed32 = 5
};

struct decode_error {
    enum kind {
        ok = 0,
        truncated,
        bad_varint,
        bad_wire_type,
        field_mismatch,
        unknown_error
    } err = ok;

    constexpr explicit operator bool() const noexcept { return err != ok; }
};

// ---------------------------------------------------------------------------
// Size Calculation Helpers (Zero-Allocation Pre-sizing)
// ---------------------------------------------------------------------------
constexpr std::size_t varint_size(std::uint64_t v) noexcept {
    if (v < (1ULL << 7)) return 1;
    if (v < (1ULL << 14)) return 2;
    if (v < (1ULL << 21)) return 3;
    if (v < (1ULL << 28)) return 4;
    if (v < (1ULL << 35)) return 5;
    if (v < (1ULL << 42)) return 6;
    if (v < (1ULL << 49)) return 7;
    if (v < (1ULL << 56)) return 8;
    if (v < (1ULL << 63)) return 9;
    return 10;
}

constexpr std::size_t tag_size(std::int64_t field_num) noexcept {
    return varint_size(static_cast<std::uint64_t>(field_num) << 3);
}

// ---------------------------------------------------------------------------
// Zigzag encoding
// ---------------------------------------------------------------------------
constexpr std::uint32_t encode_zigzag32(std::int32_t n) noexcept {
    return (static_cast<std::uint32_t>(n) << 1) ^ static_cast<std::uint32_t>(n >> 31);
}

constexpr std::int32_t decode_zigzag32(std::uint32_t n) noexcept {
    return static_cast<std::int32_t>((n >> 1) ^ (~(n & 1) + 1));
}

constexpr std::uint64_t encode_zigzag64(std::int64_t n) noexcept {
    return (static_cast<std::uint64_t>(n) << 1) ^ static_cast<std::uint64_t>(n >> 63);
}

constexpr std::int64_t decode_zigzag64(std::uint64_t n) noexcept {
    return static_cast<std::int64_t>((n >> 1) ^ (~(n & 1) + 1));
}

// ---------------------------------------------------------------------------
// Direct Raw-Pointer Serialization (Zero Bounds-Check, Maximum Throughput)
// ---------------------------------------------------------------------------
inline std::byte* write_varint_to(std::byte* dest, std::uint64_t v) noexcept {
    while (v >= 0x80) {
        *dest++ = static_cast<std::byte>((v & 0x7F) | 0x80);
        v >>= 7;
    }
    *dest++ = static_cast<std::byte>(v & 0x7F);
    return dest;
}

inline std::byte* write_tag_to(std::byte* dest, std::int64_t field_num, wire_type wt) noexcept {
    std::uint64_t tag = (static_cast<std::uint64_t>(field_num) << 3) | static_cast<std::uint8_t>(wt);
    return write_varint_to(dest, tag);
}

inline std::byte* write_zigzag32_to(std::byte* dest, std::int32_t v) noexcept {
    return write_varint_to(dest, encode_zigzag32(v));
}

inline std::byte* write_zigzag64_to(std::byte* dest, std::int64_t v) noexcept {
    return write_varint_to(dest, encode_zigzag64(v));
}

inline std::byte* write_fixed32_to(std::byte* dest, std::uint32_t v) noexcept {
    dest[0] = static_cast<std::byte>(v & 0xFF);
    dest[1] = static_cast<std::byte>((v >> 8) & 0xFF);
    dest[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    dest[3] = static_cast<std::byte>((v >> 24) & 0xFF);
    return dest + 4;
}

inline std::byte* write_float_to(std::byte* dest, float v) noexcept {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    return write_fixed32_to(dest, u);
}

inline std::byte* write_fixed64_to(std::byte* dest, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        dest[i] = static_cast<std::byte>((v >> (i * 8)) & 0xFF);
    }
    return dest + 8;
}

inline std::byte* write_double_to(std::byte* dest, double v) noexcept {
    std::uint64_t u = 0;
    std::memcpy(&u, &v, sizeof(double));
    return write_fixed64_to(dest, u);
}

inline std::byte* write_string_to(std::byte* dest, std::string_view s) noexcept {
    dest = write_varint_to(dest, s.size());
    if (!s.empty()) {
        std::memcpy(dest, s.data(), s.size());
        dest += s.size();
    }
    return dest;
}

inline std::byte* write_length_delimited_to(std::byte* dest, std::span<const std::byte> payload) noexcept {
    dest = write_varint_to(dest, payload.size());
    if (!payload.empty()) {
        std::memcpy(dest, payload.data(), payload.size());
        dest += payload.size();
    }
    return dest;
}

// ---------------------------------------------------------------------------
// std::vector overloads (backward-compatible convenience API)
// ---------------------------------------------------------------------------
inline void write_varint(std::vector<std::byte>& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<std::byte>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<std::byte>(v & 0x7F));
}

inline void write_tag(std::vector<std::byte>& out, std::int64_t field_num, wire_type wt) {
    std::uint64_t tag = (static_cast<std::uint64_t>(field_num) << 3) | static_cast<std::uint8_t>(wt);
    write_varint(out, tag);
}

inline void write_zigzag32(std::vector<std::byte>& out, std::int32_t v) {
    write_varint(out, encode_zigzag32(v));
}

inline void write_zigzag64(std::vector<std::byte>& out, std::int64_t v) {
    write_varint(out, encode_zigzag64(v));
}

inline void write_fixed32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>(v & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
}

inline void write_float(std::vector<std::byte>& out, float v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    write_fixed32(out, u);
}

inline void write_fixed64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((v >> (i * 8)) & 0xFF));
    }
}

inline void write_double(std::vector<std::byte>& out, double v) {
    std::uint64_t u = 0;
    std::memcpy(&u, &v, sizeof(double));
    write_fixed64(out, u);
}

inline void write_length_delimited(std::vector<std::byte>& out, std::span<const std::byte> payload) {
    write_varint(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

inline void write_string(std::vector<std::byte>& out, std::string_view s) {
    write_varint(out, s.size());
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    out.insert(out.end(), p, p + s.size());
}

// ---------------------------------------------------------------------------
// Stream Reader
// ---------------------------------------------------------------------------
class reader {
public:
    constexpr explicit reader(std::span<const std::byte> buf) noexcept : buf_(buf), pos_(0) {}

    [[nodiscard]] constexpr bool empty() const noexcept { return pos_ >= buf_.size(); }
    [[nodiscard]] constexpr std::size_t remaining() const noexcept { return buf_.size() - pos_; }

    constexpr bool read_tag(std::int64_t& field_num, wire_type& wt) noexcept {
        std::uint64_t raw = 0;
        if (!read_varint(raw)) return false;
        const std::uint64_t kind = raw & 0x7;
        const std::uint64_t num = raw >> 3;
        // Wire types 6 and 7 do not exist, and field number 0 is invalid.
        if (kind > static_cast<std::uint64_t>(wire_type::fixed32) || num == 0) return false;
        wt = static_cast<wire_type>(kind);
        field_num = static_cast<std::int64_t>(num);
        return true;
    }

    constexpr bool read_varint(std::uint64_t& out) noexcept {
        std::uint64_t result = 0;
        int shift = 0;
        while (pos_ < buf_.size() && shift < 70) {
            std::byte b = buf_[pos_++];
            result |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(b) & 0x7F) << shift;
            if ((static_cast<std::uint8_t>(b) & 0x80) == 0) {
                out = result;
                return true;
            }
            shift += 7;
        }
        return false;
    }

    constexpr bool read_zigzag32(std::int32_t& out) noexcept {
        std::uint64_t raw = 0;
        if (!read_varint(raw)) return false;
        out = decode_zigzag32(static_cast<std::uint32_t>(raw));
        return true;
    }

    constexpr bool read_zigzag64(std::int64_t& out) noexcept {
        std::uint64_t raw = 0;
        if (!read_varint(raw)) return false;
        out = decode_zigzag64(raw);
        return true;
    }

    constexpr bool read_fixed32(std::uint32_t& out) noexcept {
        if (remaining() < 4) return false;
        out = static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf_[pos_])) |
              (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf_[pos_ + 1])) << 8) |
              (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf_[pos_ + 2])) << 16) |
              (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf_[pos_ + 3])) << 24);
        pos_ += 4;
        return true;
    }

    inline bool read_float(float& out) noexcept {
        std::uint32_t u = 0;
        if (!read_fixed32(u)) return false;
        std::memcpy(&out, &u, sizeof(float));
        return true;
    }

    constexpr bool read_fixed64(std::uint64_t& out) noexcept {
        if (remaining() < 8) return false;
        std::uint64_t val = 0;
        for (int i = 0; i < 8; ++i) {
            val |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(buf_[pos_ + i])) << (i * 8);
        }
        pos_ += 8;
        out = val;
        return true;
    }

    inline bool read_double(double& out) noexcept {
        std::uint64_t u = 0;
        if (!read_fixed64(u)) return false;
        std::memcpy(&out, &u, sizeof(double));
        return true;
    }

    constexpr bool read_length_delimited(std::span<const std::byte>& out) noexcept {
        std::uint64_t len = 0;
        if (!read_varint(len)) return false;
        if (remaining() < len) return false;
        out = buf_.subspan(pos_, len);
        pos_ += len;
        return true;
    }

    constexpr bool read_string(std::string_view& out) noexcept {
        std::span<const std::byte> b;
        if (!read_length_delimited(b)) return false;
        out = std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
        return true;
    }

    // Skips one field value. For a group, skips to the matching end-group tag.
    // Pass the group's field number to also check the end tag number (-1: any).
    constexpr bool skip_field(wire_type wt, std::int64_t group_field_num = -1) noexcept {
        switch (wt) {
            case wire_type::varint: {
                std::uint64_t dummy = 0;
                return read_varint(dummy);
            }
            case wire_type::fixed64:
                if (remaining() < 8) return false;
                pos_ += 8;
                return true;
            case wire_type::len: {
                std::span<const std::byte> dummy;
                return read_length_delimited(dummy);
            }
            case wire_type::fixed32:
                if (remaining() < 4) return false;
                pos_ += 4;
                return true;
            case wire_type::start_group:
                return skip_group(group_field_num);
            default:
                return false;
        }
    }

private:
    // Reads tags until the end-group tag that closes the group just opened.
    // Nested groups are tracked with a stack of their field numbers.
    constexpr bool skip_group(std::int64_t field_num) noexcept {
        constexpr std::size_t max_depth = 100;  // same limit as the reference parser
        std::int64_t open[max_depth];
        std::size_t depth = 0;
        open[depth++] = field_num;
        while (depth > 0) {
            std::int64_t num = 0;
            wire_type kind = wire_type::varint;
            if (!read_tag(num, kind)) return false;
            if (kind == wire_type::start_group) {
                if (depth == max_depth) return false;
                open[depth++] = num;
            } else if (kind == wire_type::end_group) {
                const std::int64_t want = open[--depth];
                if (want >= 0 && want != num) return false;
            } else if (!skip_field(kind)) {
                return false;
            }
        }
        return true;
    }

    std::span<const std::byte> buf_;
    std::size_t pos_ = 0;
};

} // namespace ctpb::wire
