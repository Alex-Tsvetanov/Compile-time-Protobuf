# P5 single-host campaign, 2026-09-06 — every cell from one sitting

The re-measurement P5's table needs. Every figure below comes from **one host, one pinned
session, one build per arm**, with the benchmark's decode asymmetry corrected. It replaces a
published table whose cells came from a different machine under a harness this repository has
itself repudiated.

## The table

Medians of trials 1–6. **Trial 0 is excluded as warm-up** and the exclusion is stated because
it is not cosmetic: protobuf's arms run 4–6% slow on trial 0 while ctpb's move under 0.4%, so
including it inflates the reference implementation and flatters ctpb. Ranges are the full
min–max of the six, not a standard deviation — six points do not support an SD, and a median
paired with an SD is two statistics that do not belong together.

### vs `libprotobuf-lite`

| | ctpb | libprotobuf-lite | ratio | paper prints |
|---|---|---|---|---|
| **encode** | **8.27 ns** [8.24, 8.51] | 46.11 ns [45.97, 46.87] | **5.572×** | 2.81× |
| **decode** | **76.52 ns** [76.08, 77.73] | 79.83 ns [79.56, 81.04] | **1.043×** | 1.71× |

### vs full `libprotobuf`

| | ctpb | libprotobuf | ratio | paper prints |
|---|---|---|---|---|
| **encode** | **8.56 ns** [8.55, 8.56] | 45.64 ns [45.62, 45.65] | **5.334×** | 2.81× |
| **decode** | **76.33 ns** [75.75, 76.71] | 87.80 ns [87.78, 88.07] | **1.150×** | 1.71× |

47-byte payload, 10⁶ iterations per trial, 20,000-iteration warmup, seven trials.

### Footprint, exact bytes

| | bytes |
|---|---|
| `person_ctpb` stripped | **18,776** |
| `person_protoc` dynamic, stripped | 27,504 |
| `person_protoc` static, stripped | **1,759,368** |

**93.70×** against the static build, 1.46× against the dynamic one. The paper's 93.7× is
exactly right. It is stated here from byte counts rather than `ls -lh`, which is what the
audit asked for: the committed record held "19K" and "1.7M" and a reader cannot recover
93.70× from rounded kilobytes.

### Compile-time latency and the constexpr limit

| schema | median | range |
|---|---|---|
| small, 5 fields | 0.646 µs | [0.643, 0.648] |
| medium, 25 | 2.515 µs | [2.511, 2.519] |
| large, 100 | **9.465 µs** | [9.438, 9.484] |

Under the paper's "below 11.4 µs" claim. The constexpr step limit is consistent with the
documented 1048576 and the project sets no constexpr flag anywhere in its build.

## What moved, and the sentence that has to travel with it

**Encode roughly doubles the published claim** — 5.57× against lite where the paper prints
2.81×. **Decode does not reach the published 1.71×**, but it is no longer negative: 1.043×
against lite and 1.150× against full.

That decode figure exists because this campaign corrects an asymmetry in the benchmark. The
committed harness reused a destination message on the protobuf arm and constructed a fresh
`CtpbPerson` on every iteration of the ctpb arm — two `std::string`s above the SSO threshold,
so a malloc/free pair per iteration that protobuf did not pay. Decoding into a destination
allocated once per trial removes it. **No ctpb code changed**: `decode_from` is already a
public member that fills an existing object, and string fields go through `assign()`, which
reuses capacity.

And the caveat, which is not optional and is why this is a result worth having:

> Correcting a per-iteration allocation in the benchmark moves ctpb's decode from 0.98× to
> 1.04× against `libprotobuf-lite`; ctpb's decoder omits the UTF-8 validation the reference
> implementation performs, so the remaining margin is not a like-for-like comparison.

proto3 mandates UTF-8 validation of `string` fields. `libprotobuf-lite` spends 7.37%
inclusive doing it — `IsStructurallyValidUTF8`, `VerifyUtf8String`, `VerifyUTF8` — and `grep`
finds no `utf8` anywhere in ctpb's headers. The decode comparison now has the allocation
asymmetry removed and the correctness asymmetry still in place, pointing the other way.
**Printing 1.04× as a decode win without that sentence would be claiming a margin that partly
consists of work not done.** Encode and footprint carry no such caveat.

## Conditions

| | |
|---|---|
| host | AMD Ryzen 7 5800H, 16 logical CPUs, Linux 7.2.3-arch1-2, on AC |
| image | `ctpb-dev:2026-09-06`, `sha256:699868a0…703d`, built from this repo's `Dockerfile` |
| toolchain | clang 21.0.0git p2996 `2ea0a79f…`, protoc 3.21.12, `libprotobuf.so.32` |
| repo | `Compile-time-Protobuf` at `7207bcb` on `coordinator/ArchLinux` |
| clock | pinned 3.2 GHz, boost off, **41 samples across 16 CPUs: median 3200 MHz, min 3159, spread 0.97%** |
| restore | verified by read-back **against the baseline captured before pinning**, EPP included |
| gates | none — these scripts have no validity gates, so nothing was accepted or refused |

**The host still differs from the published table's Ryzen 5 3600**, so nanosecond cells are
not comparable with it cell-by-cell. What is comparable is the ratio within this run, and
every ratio above comes from one binary per arm in one sitting, which is what the table
needed and did not have.

**Declared gap, unchanged:** the environment is captured on the host while the timed work runs
in the container, so `virtualised: none` describes a host whose measurements came from Ubuntu
24.04 inside Docker. It belongs in P5's methodology section.

## Two instrument defects found and fixed before these numbers were read

A first run of this campaign completed all four arms and was **discarded**. Its clock sampler
wrote to a path that did not exist, so it produced no achieved-clock record; and its restore
check compared EPP against a hardcoded `performance` rather than the value captured before
pinning, so it reported `verification=ok` while leaving the machine at `performance` when it
had been found at `balance_performance`. A check that passes for a reason unrelated to what it
checks — the same defect this corpus has been cataloguing all day, in this record's own
tooling. Both are fixed here: the sampler produced 41 samples, and the restore compares
against the captured baseline and verified clean.

The footprint figure was also misparsed on first reading, giving 1.12× against the paper's
93.70×. The parser expected a tab where the file holds a literal `\t`. A new instrument
disagreeing wildly with the world is evidence about the instrument.

`method/` holds the campaign driver, the modified eval script and `diff-from-committed-eval.txt`
— the exact change from the committed harness. `raw/` holds every arm's output, the clock
samples and the host-state record.

```
837dc64f80bd9ba2b92a081ae2e1f1f6a73709fe656f84a7175c1362928662d8  method/campaign.sh
7c889f33c5ee5bb7b2d7e1a180dd49b3e66e1666d313291e1c860235cb225144  method/diff-from-committed-eval.txt
0b6df2bb5680f3f76b766cdb00a1fa7268c163d60dd1226366acc6afee830073  method/eval_runtime_prealloc.sh
31d17267f8a52e00d967d9f02182b8e8b4bd32dc04941c13ef912e3566256774  method/sampler.sh
bd51a001070858c721e97d37175dad93c835069b39b8ec6c88ac4431b4445c7b  raw/binary_size.txt
97fb7878d97f56596cc161866eb44c94513062e6ae79d962431c486bced597a9  raw/clock-during.txt
99df694fdda01f4b04930d52fd87875b47517e865d9e079399a6dd85e8f1aefe  raw/compile_time_latency.txt
cbbc7bce5ddef49570e7f6803742da23d786ce201efb63d72029d1b68673b384  raw/constexpr_step_limit.txt
667401e63192ddeec88236845136f0bef9340d68a6799eba997ba0455fdfac1d  raw/host-state.txt
852016f9889cc29ed8fd87ef4b163fd27cdfc2584d9efea1df1e39b088c4f83f  raw/runtime_prealloc.txt
```
