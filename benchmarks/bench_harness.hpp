// Timing scaffold shared verbatim by both arms of the throughput comparison.
//
// The two arms cannot live in one translation unit: ctpb's reflection path
// needs libc++ for <meta>, while a distribution libprotobuf is built against
// libstdc++ and will not link against libc++. They are therefore separate
// programs, and the only way to keep the comparison honest is for the timed
// loop to be the same source text in both -- hence this header rather than two
// loops that are meant to match.
//
// Each arm supplies two callables and nothing else:
//   enc(): encode the prepared message into a preallocated buffer
//   dec(): decode the prepared wire bytes into a reused target
#ifndef CTPB_BENCH_HARNESS_HPP
#define CTPB_BENCH_HARNESS_HPP

#include <chrono>
#include <cstddef>
#include <cstdio>

namespace harness {

// Keeps a value observable to the compiler without doing anything to it. Both
// arms need it for the same reason: ctpb's encoder inlines completely and the
// optimiser may drop work whose result nothing reads, while protobuf's call
// into a shared library cannot be dropped. Without a barrier on both sides the
// asymmetry runs one way only.
inline void sink(const void* p, std::size_t n) {
    asm volatile("" :: "r"(p), "r"(n) : "memory");
}

// Per-round timings on stdout, one line each, so the summariser can take a
// median and a range instead of this program reporting a single mean that
// hides drift.
template <class Enc, class Dec>
void measure(const char* arm, Enc enc, Dec dec,
             int rounds, int per_round, int warmup, std::size_t payload_bytes) {
    using clock = std::chrono::steady_clock;

    for (int i = 0; i < warmup; ++i) { enc(); dec(); }

    for (int r = 0; r < rounds; ++r) {
        auto a0 = clock::now();
        for (int i = 0; i < per_round; ++i) enc();
        auto a1 = clock::now();
        for (int i = 0; i < per_round; ++i) dec();
        auto a2 = clock::now();

        double enc_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(a1 - a0).count()
                        / static_cast<double>(per_round);
        double dec_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(a2 - a1).count()
                        / static_cast<double>(per_round);

        std::printf("round=%d arm=%s enc_ns=%.4f dec_ns=%.4f payload_bytes=%zu\n",
                    r, arm, enc_ns, dec_ns, payload_bytes);
        std::fflush(stdout);
    }
}

}  // namespace harness

#endif
