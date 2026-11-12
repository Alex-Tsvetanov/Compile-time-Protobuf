// tests/test_support.hpp
//
// Small helpers for the feature tests (optional, oneof, map). The checks
// abort with the line number, so they also work when NDEBUG is defined.

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

#include <ctpb/ctpb.hpp>

namespace test_support {

inline void check(bool ok, const char* what, const char* file, int line) {
    if (!ok) {
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, what);
        std::abort();
    }
}

#define REQUIRE(expr) ::test_support::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

inline std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

// Encodes `msg` three ways and checks that they agree: byte_size, the raw
// pointer encoder (which must not write past byte_size) and encode().
template <typename M>
std::vector<std::byte> encode_checked(const M& msg) {
    constexpr std::size_t guard = 16;
    const std::size_t n = ctpb::byte_size(msg);
    std::vector<std::byte> buf(n + guard, std::byte{0xEE});
    std::byte* end = ctpb::encode_to_ptr(msg, buf.data());
    REQUIRE(static_cast<std::size_t>(end - buf.data()) == n);
    for (std::size_t i = n; i < buf.size(); ++i) REQUIRE(buf[i] == std::byte{0xEE});
    buf.resize(n);
    REQUIRE(ctpb::encode(msg) == buf);
    return buf;
}

// Member I of a generated message, on either code path: message_impl
// (templates over the parsed schema) or a define_message struct (P2996).
template <std::size_t I, typename M>
decltype(auto) at(M& msg) {
    using Plain = std::remove_const_t<M>;
#if CTPB_HAS_P2996
    if constexpr (ctpb::HasSchemaTraits<Plain>) {
        return (msg.[:ctpb::reflect::get_member_i<Plain, I>():]);
    } else
#endif
    {
        return msg.template field<I>();
    }
}

}  // namespace test_support
