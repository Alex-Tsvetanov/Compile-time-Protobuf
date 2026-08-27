// include/ctpb/embed.hpp
// Wraps P1967 #embed for reading .proto source at compile time.

#ifndef CTPB_EMBED_HPP
#define CTPB_EMBED_HPP

#include <cstddef>
#include <span>
#include <string_view>

namespace ctpb {

// Result of #embed-ing a .proto file or reading a compile-time source buffer.
template <std::size_t N>
struct embedded_source {
    static constexpr std::size_t size = N;
    const char data[N > 0 ? N : 1]{};

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {data, N};
    }

    [[nodiscard]] constexpr std::span<const char> bytes() const noexcept {
        return {data, N};
    }
};

// Helper to construct compile-time embedded source from string literal
template <std::size_t N>
constexpr auto make_embedded_source(const char (&str)[N]) noexcept {
    embedded_source<N - 1> src{};
    for (std::size_t i = 0; i < N - 1; ++i) {
        const_cast<char&>(src.data[i]) = str[i];
    }
    return src;
}

} // namespace ctpb

#endif // CTPB_EMBED_HPP
