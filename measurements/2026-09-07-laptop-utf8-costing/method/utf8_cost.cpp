// Costing only. NOT a proposed implementation and not in the ctpb tree.
//
// Prices a table-driven UTF-8 validator over the exact strings the P5 payload carries,
// so "what would validation cost ctpb" is a measurement rather than an estimate.
// The DFA is Bjoern Hoehrmann's, which is the shape protobuf's own validator uses.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <algorithm>

static const std::uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
  0,1,2,3,5,8,7,1,1,1,4,6,1,1,1,1, 1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1,
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1, 1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1, 1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1,
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1, 1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

static inline bool valid_utf8(std::string_view s) noexcept {
    std::uint32_t state = 0;
    for (unsigned char c : s) {
        state = utf8d[256u + state * 16u + utf8d[c]];
        if (state == 1) return false;
    }
    return state == 0;
}

static volatile std::uint64_t sink_v;
static inline void sink(bool b) { sink_v += b ? 1u : 0u; }

// What protobuf actually does: a word-at-a-time ASCII check first, DFA only when a byte
// above 0x7F appears. Both payload strings are pure ASCII, which is the common case for
// this kind of field and the case the P5 comparison is actually about.
static inline bool valid_utf8_ascii_fast(std::string_view s) noexcept {
    const char* p = s.data();
    std::size_t n = s.size();
    while (n >= 8) {
        std::uint64_t w;
        __builtin_memcpy(&w, p, 8);
        if (w & 0x8080808080808080ull) return valid_utf8(std::string_view(p, n));
        p += 8; n -= 8;
    }
    while (n--) { if (static_cast<unsigned char>(*p++) & 0x80) return valid_utf8(s); }
    return true;
}

int main() {
    const std::string name  = "Alex I. Tsvetanov";       // 17 chars, the P5 payload
    const std::string email = "atsvetanov@tu-sofia.bg";  // 22 chars
    const std::size_t iters = 2'000'000;

    auto bench = [&](const char* label, auto&& fn) {
        std::vector<double> v;
        for (int trial = 0; trial < 9; ++trial) {
            auto t0 = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < iters; ++i) { fn(); }
            auto t1 = std::chrono::steady_clock::now();
            v.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / iters);
        }
        std::sort(v.begin(), v.end());
        std::printf("  %-34s median %7.3f  [%.3f .. %.3f]\n",
                    label, v[v.size()/2], v.front(), v.back());
        return v[v.size()/2];
    };

    std::printf("ns per MESSAGE (2 string fields, %zu bytes total), 9 trials\n",
                name.size() + email.size());
    // The harness itself, so the validator cost is the difference and not the total.
    double base = bench("loop + 2 sinks only (overhead)", [&]{ sink(true); sink(true); });
    double dfa  = bench("+ DFA on both strings",
                        [&]{ sink(valid_utf8(name)); sink(valid_utf8(email)); });
    double fast = bench("+ ASCII fast path, DFA fallback",
                        [&]{ sink(valid_utf8_ascii_fast(name)); sink(valid_utf8_ascii_fast(email)); });
    std::printf("\nvalidation cost per message, overhead subtracted:\n");
    std::printf("  plain DFA            %7.3f ns   (%.4f ns/byte)\n",
                dfa - base, (dfa - base) / (name.size() + email.size()));
    std::printf("  ASCII fast path      %7.3f ns   (%.4f ns/byte)\n",
                fast - base, (fast - base) / (name.size() + email.size()));
    return 0;
}
