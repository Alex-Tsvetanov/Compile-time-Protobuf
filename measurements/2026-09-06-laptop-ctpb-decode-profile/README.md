# Decode profile, 2026-09-06 — where ctpb's 81.4 ns goes, and a harness asymmetry

Dispatched to answer whether the decode gap against `libprotobuf-lite` (0.97×, `ctpb`
slower) is structural to the compile-time approach or a fixable hot path.

**Nothing in ctpb was changed. This is a profile, not an optimisation.**

## The headline: the decode comparison is not like-for-like

`eval_runtime_comparison_lite.sh` reuses buffers on three of its four timed loops and
**not on the fourth**, which is the ctpb decode arm:

```cpp
double google_dec_ns = ... [&]{
    dec_msg.Clear(); dec_msg.ParseFromString(google_wire);   // dec_msg reused across iterations
    sink(&dec_msg, sizeof(dec_msg)); });
double ctpb_dec_ns   = ... [&]{
    auto d = ctpb::decode<CtpbPerson>(ctpb_wire);            // fresh CtpbPerson every iteration
    sink(&d, sizeof(d)); });
```

`CtpbPerson` holds two `std::string`s. The payload's `name` is 17 characters and `email` is
22 — **both above libstdc++'s 15-character SSO threshold**, so each iteration heap-allocates
two strings and frees them. The protobuf arm does not: `Clear()` on a message retains the
string capacity, so it overwrites buffers it already owns.

The script's own comment says buffers "are allocated once per trial and reused… before this
each lambda built a fresh string, vector or message every iteration, so both arms were partly
an allocator benchmark". That fix landed on encode and on protobuf's decode. It did not land
on ctpb's decode.

The profile is consistent with this. `malloc` 2.65% and `cfree` 2.33% of all samples — about
5% of the whole four-loop run — against protobuf's reuse path
`ArenaStringPtr::ClearToEmpty()` at 2.38% plus 1.63% through the PLT, which frees nothing.

**Caveat, stated because it limits the claim.** DWARF unwinding stops at the libc frame, so
the profile does **not** prove the allocations are on ctpb's side; it shows allocation traffic
consistent with a source that reuse would have removed. The decisive test is below and has
not been run.

## What ctpb's decode actually spends, from the profile

18K samples, 37.8 G cycles, symbols resolved through a symfs built from the image.

| symbol | self | inclusive |
|---|---|---|
| `ctpb::reflect_base<…>::decode_from` | 7.96% | 11.47% |
| the field-dispatch lambda inside it | 8.07% | 11.71% |
| `ctpb::detail::decode_field_by_info<std::string>` | **6.03%** | 6.47% |
| `ctpb::detail::decode_field_by_info<int>` | 1.41% | — |

The single largest ctpb decode leaf is the string field decoder. On the protobuf side,
`Person::_InternalParse` is 13.86% inclusive and `MergeFromImpl<false>` 10.27%.

## The finding that cuts the other way, and it is the important one

**protobuf-lite does work on decode that ctpb does not do at all**, and still wins:

| symbol | inclusive |
|---|---|
| `internal::IsStructurallyValidUTF8` | 7.37% |
| `WireFormatLite::VerifyUtf8String` | 5.55% |
| `internal::VerifyUTF8` | 4.90% |

proto3 mandates UTF-8 validation of `string` fields. ctpb's decoder performs no such check —
`decode_field_by_info<std::string>` copies bytes. So the comparison currently credits ctpb
with skipping a correctness obligation the reference implementation honours, and ctpb is
*still* 2.1 ns slower. Removing the harness's allocation asymmetry might close the gap; adding
the validation ctpb owes would widen it again. **Both belong in the paper's discussion.**

## Warm-up is asymmetric, and trial 0 should be excluded

Recomputed from the committed corrected run. Trial 0 against the median of trials 1–6:

| arm | trial 0 deviation | range of trials 1–6 |
|---|---|---|
| `google_dec` vs lite | **+6.34%** | 0.051 ns |
| `google_enc` vs full | +4.97% | 1.032 ns |
| `google_dec` vs full | +4.36% | 1.241 ns |
| `ctpb_dec` vs lite | −0.23% | 1.036 ns |
| `ctpb_enc` vs lite | −0.19% | 0.039 ns |

The 20,000-iteration warm-up is **not enough for protobuf** and is ample for ctpb. Including
trial 0 inflates every protobuf figure by 4–6% and therefore flatters ctpb. Any published
median must say trial 0 is excluded, or the comparison is biased in ctpb's favour.

Note also the stability difference in trials 1–6: protobuf's lite decode spans **0.051 ns**
while ctpb's spans **1.036 ns**, twenty times wider — the variance signature of an allocator
in the loop.

## What was not run, and what it would settle

Two experiments follow from this and neither was performed, per the dispatch:

1. **Reuse the destination on the ctpb decode arm** — decode into a `CtpbPerson` allocated
   once per trial, matching what the protobuf arm already does. This is a change to the
   *harness*, not to ctpb. If the gap is the allocation asymmetry it should close most of
   2.1 ns; if it does not, the gap is real and structural.
2. **Payload below the SSO threshold** — the same comparison with `name` and `email` under 15
   characters removes heap allocation from both arms without changing either decoder. It
   tests the same hypothesis independently and needs no code change at all, only different
   input.

Neither result may be quoted against the paper's figures: both are diagnostics, and a decode
figure taken with a reused destination is not comparable with the committed 0.97×.

## Conditions

| | |
|---|---|
| what | `perf record -g --call-graph=dwarf,8192 -F 1999`, 25 s attached to `bench_lite` |
| repo | `Compile-time-Protobuf` at `0ccbe49`, source identical to `90641ed` |
| image | `ctpb-dev:2026-09-06`, id `sha256:699868a0…703d`, protoc 3.21.12 |
| clock | **unpinned, boost on** — this is a profile, not a timed record |
| gates | none. `rerun_all.sh` and the eval scripts have no validity gates at all |

The clock was left free deliberately: relative attribution is what a profile is for. An
unpinned 8-trial companion run is kept in `raw/unpinned-companion-run.txt` to show the finding
does not depend on the pin. Medians of trials 1–7:

| | pinned, committed run | unpinned companion |
|---|---|---|
| decode vs `libprotobuf-lite` | 0.97× | **0.945×** |
| decode vs full `libprotobuf` | 1.05× | 1.001× |
| encode vs `libprotobuf-lite` | 5.62× | 5.64× |
| encode vs full `libprotobuf` | 4.82× | 5.10× |

Same direction and size throughout: ctpb decodes slower than `libprotobuf-lite` on a free
clock too, and encode stays above 5×. Absolute nanoseconds in the companion run are **not**
comparable with the committed run — different clock conditions — and it is not a record.

`raw/perf-flat.txt` and `raw/perf-cumulative.txt` are the reports. The schema string literal
that ctpb encodes into its template arguments has been collapsed to `PROTO_SCHEMA` for
legibility; nothing else is altered. `perf.data` itself is 158 MB and is not committed.

```
5c9d2b184bda453e24410fe38501f6acfb3e672321a8617449ff618be19bd93d  raw/perf-cumulative.txt
59ce16f729ed470ae814e1f9f6d51fccd4c64bcc308f2cdba0ee84eeac27e7cf  raw/perf-flat.txt
4aa066ba13f739285b7458b074f2d7c486a990374ecefcc7f5d609c11e19a15a  raw/unpinned-companion-run.txt
```
