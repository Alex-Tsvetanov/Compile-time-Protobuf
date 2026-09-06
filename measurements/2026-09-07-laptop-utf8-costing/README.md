# What UTF-8 validation would cost ctpb — a costing, not an implementation

proto3 requires `string` fields to be checked for structurally valid UTF-8 on parse.
protobuf-lite does it and spends 7.37% inclusive doing so; ctpb omits it entirely, which is
why the P5 decode comparison is not like-for-like. This prices the gap by **measuring a
validator on the exact strings the P5 payload carries**, rather than estimating it.

**Nothing was implemented. No file in `include/` or `benchmarks/` was changed.**

## The measurement

Björn Höhrmann's table-driven DFA — the shape protobuf's own validator uses — over
`"Alex I. Tsvetanov"` (17 bytes) and `"atsvetanov@tu-sofia.bg"` (22 bytes), the two string
fields of the P5 `Person`. 2,000,000 iterations × 9 trials, `-O3 -DNDEBUG -std=c++23`,
machine pinned. Loop and sink overhead measured separately and subtracted, so what is
reported is the validator and not the harness.

| | ns per message | ns per byte |
|---|---|---|
| loop + sinks only (overhead, subtracted) | 0.484 | — |
| **plain DFA** | **39.956** | 1.0245 |
| **ASCII fast path, DFA fallback** | **3.894** | 0.0998 |

The fast path is a word-at-a-time check for any byte above 0x7F, falling back to the DFA
only when one appears. Both payload strings are pure ASCII, which is the common case for
this kind of field and the case the P5 comparison is about.

**The implementation choice is worth 10×**, and that dominates everything else here.

## What it does to the published result

P5 measured ctpb decode at **76.52 ns** against protobuf-lite's **79.83 ns** — ctpb ahead
by 4.2%. protobuf-lite's figure *already contains* its own validation.

| ctpb variant | decode ns | vs lite (79.83) |
|---|---|---|
| as published, no validation | 76.52 | **0.958× — ctpb wins by 4%** |
| + ASCII fast path | ~80.4 | **1.007× — a tie, ctpb marginally behind** |
| + plain DFA | ~116.5 | **1.46× — ctpb loses badly** |

**So the "ctpb validates and still wins" outcome is not available on this payload.** With a
competently written validator the honest result is a tie; with a naive one it is a clear
loss. That is a smaller claim than the paper currently makes and it is the one the
measurement supports.

Caveat on the arithmetic: this adds a standalone validator cost to a measured decode time.
A real implementation validates a string already in cache, just written by the decoder, so
the true cost is likely *below* 3.894 ns. The tie is therefore the pessimistic reading of
the fast-path row, not the optimistic one.

## Effort: an afternoon, and here is what decides it

**Scope is two call sites.** `reflection.hpp:420` (singular string) and `:475` (repeated
string element), both immediately after `r.read_string(sv)`. `bytes` fields correctly use
`read_length_delimited` and must NOT be validated, so they are untouched. Map string keys
and values need checking for a third site.

**The constexpr question, which was the one real risk, is answered: it does not arise.**
`decode_field_by_info` at `:409` is `inline bool`, not `constexpr`. The `consteval` uses in
this repository are schema definition and `.proto` parsing — `define_message`, the parser
tests — not wire decoding. So a validator using `__builtin_memcpy` word loads is fine at the
field level; had decode been constexpr-evaluated this would have been a week, because the
fast path would have needed a constexpr-compatible rewrite and the 10× would have been at
risk.

**Error reporting is one line plus plumbing.** `wire::decode_error::kind` at `wire.hpp:21`
has six variants; `invalid_utf8` is a seventh. `decode_field_by_info` returns `bool` and
does not carry the error, so a rejection surfaces generically unless the signature is
threaded — but returning `false` already rejects the message, which is the proto3-required
behaviour. The specific code is a nicety, not a correctness requirement.

**So: validator ~30 lines, two or three call sites, one enum variant, tests for valid
ASCII, valid multi-byte, overlong, surrogate and truncated sequences. An afternoon.**

The decision that needs care is not the effort. It is that implementing this converts a
published 4% win into a tie, and that is a result rather than a regression.
