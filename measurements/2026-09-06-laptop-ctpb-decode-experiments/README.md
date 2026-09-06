# Decode experiments, 2026-09-06 — the 0.97× was the harness, not the decoder

Three dispatched experiments plus a baseline, **all four run back to back in one pinned
session** so they are mutually comparable. Nothing in ctpb was changed in any of them.

## Result

Decode, vs `libprotobuf-lite`, median of trials 1–6 (trial 0 excluded as warm-up):

| variant | google | ctpb | ratio | vs baseline |
|---|---|---|---|---|
| baseline, the committed method | 79.71 | 81.38 | **0.979** | — |
| **V1 pre-allocated destination** | 80.98 | **77.23** | **1.049** | ctpb −4.15 ns |
| V3 barrier removed, ctpb arms | 81.40 | 80.72 | 1.008 | ctpb −0.66 ns |
| V2 short payload | 92.59 | 61.48 | *see below* | not comparable |

**V1 answers the question. The gap was the harness.** Removing the allocation asymmetry —
decoding into a `CtpbPerson` allocated once per trial, exactly as the protobuf arm already
reuses `dec_msg` — moves ctpb decode from 81.38 ns to 77.23 ns and **flips the sign**: ctpb
becomes 1.049× faster rather than 0.979× slower. The two ranges do not overlap
(`[81.02, 81.80]` against `[76.67, 77.52]`), so this is not a noise result.

**No ctpb change was needed.** `decode_from` is already a public member that fills an
existing object; `clear_ith_field` clears only vector members; and string fields go through
`assign()`, which reuses capacity. The API supported this all along — the harness simply did
not use it.

## The barrier is not the explanation, and this settles a live question

V3 reproduces the old harness's named defect on current silicon by removing `sink()` from the
ctpb arms only. It buys ctpb decode **0.66 ns** — about one sixth of what the allocation fix
buys. So the earlier 1.71× decode margin **cannot be described as substantially a barrier
artefact**, at least not on this host with this compiler.

The encode side came out backwards and it is worth recording. Without the barrier ctpb encode
got **slower**, 8.25 → 9.81 ns, with a trial range of 0.005 ns — reproducible, not noise. The
barrier is therefore not simply "prevents the optimiser deleting the work"; removing it
changed code generation unfavourably here. Any argument that the old numbers were inflated by
a missing barrier has to contend with that.

## V2 is an anomaly, not a result, and must not be quoted as one

The short-payload cross-check was meant to remove heap allocation from both arms without any
code change. It also changed the workload: **`payload_bytes` fell from 47 to 21**. Worse, the
protobuf arm got *slower on the smaller payload* — google decode 79.71 → 92.59 ns — which is
not explicable as a like-for-like effect and which I cannot account for.

So V2 confirms only the direction: ctpb decode without heap allocation is much faster
(81.38 → 61.48 ns). Its ratio of 1.506 is **uninterpretable** as a comparison, because the
denominator moved the wrong way for unknown reasons. It is published because it was run, and
it is labelled so nobody lifts the number.

## What this does and does not license

It licenses saying the committed 0.97× **measured an asymmetry in the benchmark rather than a
property of the decoder**, and that on a like-for-like comparison ctpb decodes slightly faster
than `libprotobuf-lite` on this host: 1.049×.

It does **not** license printing 1.049× as the paper's decode figure, for a reason that has
not gone away: **ctpb performs no UTF-8 validation**. proto3 mandates it for `string` fields,
protobuf-lite spends 7.37% inclusive doing it, and `grep` finds no `utf8` anywhere in ctpb's
headers. A decode comparison that fixes the allocation asymmetry and leaves the correctness
asymmetry is still not like-for-like — it has simply moved the unfairness to the other side.
Both belong in the same paragraph, and the honest summary of this work is:

> Correcting a per-iteration allocation in the benchmark moves ctpb's decode from 0.98× to
> 1.05× against `libprotobuf-lite`; ctpb's decoder omits the UTF-8 validation the reference
> implementation performs, so the remaining margin is not a like-for-like comparison.

## Caveats a reader needs

- **Encode figures must not be compared across variants.** V1 changed only the decode lambda,
  yet its ctpb encode moved 8.25 → 8.93 ns. Whole-program rebuilds shift the encode arm by up
  to ~1.5 ns. The decode deltas reported above are 4.15 ns with non-overlapping ranges and are
  well outside that; the encode deltas are not.
- **google's decode arm is the noisier one here** — trial ranges of 2.3–3.2 ns against ctpb's
  0.4–0.85 ns. The ratio changes above are driven by movement in the ctpb arm.
- These are **diagnostics, not records of the paper's method**. None of these numbers is
  comparable with the committed corrected run, whose method is the baseline row.

## Conditions

| | |
|---|---|
| method | four variants of `eval_runtime_comparison_lite.sh`, generated outside the repository |
| repo | `Compile-time-Protobuf` at `f643926`; `rerun_all.sh` and the eval scripts **untouched** |
| image | `ctpb-dev:2026-09-06`, id `sha256:699868a0…703d`, protoc 3.21.12 |
| clock | pinned 3.2 GHz, boost off, all four runs in one session; restore verified by read-back |
| gates | none — these scripts have no validity gates at all, so nothing was accepted or refused |

`method/` holds the four scripts and `diffs-from-baseline.txt`, which is the exact change made
for each variant. `raw/` holds the four run outputs and the clock record.

```
eca3663fcaefadbd872e21353b7083da2e4858a8cc2247b3dd299eb0410b3c13  method/diffs-from-baseline.txt
e1ded76ec577f4713af14ed9d47a8268ad48a044828b2e5561e8135c686aca06  method/run_all.sh
f123f54ca87a78aaa9cf79281f7ade3ea28bb531caeb53e3a9c697ba93023a0c  method/v0-baseline.sh
0b6df2bb5680f3f76b766cdb00a1fa7268c163d60dd1226366acc6afee830073  method/v1-prealloc-dest.sh
01827d1a7d7545eb5acc352d0ca50708c393e2d38d0ae3f8da55c913b75b2621  method/v2-short-payload.sh
d626ce11d4bd1605d868aec6e2ffe7f78e2a4460943b2fb9069b77da2b759489  method/v3-no-barrier-ctpb.sh
13f207a0d39896a36d97a79099a65e3611a999c9847e41d45c86718a45e038a7  raw/host-state.txt
216ae477db2bce2871dbcfd30580c2502266c5baae64875bd38c648da3db9ff2  raw/v0-baseline.out
3eb201570c70e216071a5d301a633a703f3f0db0e4be10c8c3c5283a2fefe038  raw/v1-prealloc-dest.out
e017aab8cb4b011a81d7af5264ccbae67fa4169cd4a21c0dd534355cd0fb30ac  raw/v2-short-payload.out
d190adc815e4406a2457c803d0e49254ea88cf115f4a82e38be89408910fe55b  raw/v3-no-barrier-ctpb.out
```
