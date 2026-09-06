# P5 corrected re-run, 2026-09-06 — encode strengthens, **decode does not survive**

A2, dispatched by the Publishing coordinator after the 12:49 run of `rerun_all.sh` failed
without producing a figure (`measurements/2026-09-06-laptop-ctpb-rerun/`). All five arms
completed here.

## The headline

| paper's claim | committed measurement | this run, vs full libprotobuf | vs libprotobuf-lite |
|---|---|---|---|
| **encode 2.81×** | 2.80× median | **4.82×** (4.51–5.08) | **5.62×** (5.60–5.68) |
| **decode 1.71×** | 1.74× median | **1.05×** (1.05–1.10) | **0.97×** (0.97–1.04) |

**The encode claim survives with room to spare.** On the corrected harness `ctpb` encodes at
9.54 ns against libprotobuf's 46.01 ns. The ratio is far above the claimed 2.81×, but see
"what this run does not establish" below before reading the increase as a property of the
library.

**The decode claim does not survive.** Against full `libprotobuf` the 1.71× collapses to
1.05×, a 5% edge. Against `libprotobuf-lite` the median ratio is **0.97× — `ctpb` is
slower**, at 81.39 ns against 79.30 ns, in six of seven trials.

That last cell contradicts a sentence that appears in the abstract, the benchmark section and
the conclusion: that the margins "persist against the leaner `libprotobuf-lite` baseline."
For decode, on this evidence, the margin does not persist and is slightly negative.

The main arm at N=1,000,000 agrees independently: encode 8.65 ns vs 46.80 ns → **5.41×**;
decode 81.83 ns vs 89.57 ns → **1.09×**.

This is the outcome `rerun_all.sh`'s own header planned for, realised in one direction only:
it predicted the encode speedup might disappear, and instead the encode speedup grew while
the decode margin went.

**What this run does not establish.** Between the committed run and this one, the protobuf
side moved proportionally more than the `ctpb` side on decode. It is tempting to attribute
that to the defects the harness rewrite fixed — the missing compiler barrier and the
per-iteration allocation — but this run cannot separate them, for two reasons. The host
changed at the same time (Zen 2 → Zen 3, and the two codepaths need not scale alike), and the
barrier and the allocation do not push the same way: `rerun_all.sh`'s own header places the
missing barrier on the **`ctpb`** arm, which would have flattered `ctpb`, while per-iteration
allocation penalised the protobuf side. **Which defect produced the 1.71×, and how much of
the change is host, is not answered here.** Separating them needs the corrected harness run on
the original Ryzen 5 3600, if that machine is still available.

What this run does establish: **on the corrected harness, on this host, with the same clang
and the same protobuf version, decode is 1.05× against full `libprotobuf` and 0.97× against
`libprotobuf-lite`.**

## Figures that do survive

- **Binary size, 93.7×.** Exact bytes, which is what the committed `ls -lh` rounding hid:
  static stripped `protoc` 1,759,368 B against `ctpb` 18,776 B = **93.70×**. The paper's
  93.7× is exactly right.
- **Compile-time latency.** Large schema (100 fields) median **9.279 µs** (9.246–9.306),
  under the paper's "under 11.4 µs" claim, against 11.362 µs median on the committed run.
- **Constexpr step limit.** Default consistent with 1048576; the project sets no constexpr
  flag of its own anywhere in the build.

## The revision measured, and the toolchain

| | |
|---|---|
| repo HEAD during the run | `257129b` on `coordinator/ArchLinux` |
| source vs `90641ed` | **identical** — `git diff 90641ed 257129b -- . ':(exclude)measurements'` is empty |
| working tree | two untracked `benchmarks/results-*` directories, baked in by `COPY .` and then shadowed by the mount |
| run window | 12:59:41 – 13:01:09 EEST, between commits `257129b` (12:56:34) and `eb18fde` (13:03:34) |

The sentinel proves the eval scripts read the **live** checkout rather than the baked
snapshot. It does not by itself say **which** checkout, so the revision is named here: the
measured source is `90641ed`'s, unmodified. Only `measurements/` differs, and nothing under
it is compiled.

Toolchain, and this is what makes the comparison legitimate:

| | committed results | this run |
|---|---|---|
| clang | 21.0.0git, p2996 `2ea0a79fe7bb5f6fdb8c687ba0e21ab63696e7f7` | **identical** |
| protobuf | `3.21.12-8.2ubuntu0.3`, `libprotobuf.so.32` | **identical** |
| host | AMD Ryzen 5 3600 | AMD Ryzen 7 5800H |
| kernel | not recorded | Linux 7.2.3-arch1-2 x86_64 |

Compiler and protobuf are the same to the commit and the package revision. **The host is the
only declared difference in the toolchain axis**, which is what narrows the confound above to
host and harness rather than leaving it open across versions too.

## Declared deviation: this is NOT `rerun_all.sh` as committed

`rerun_all.sh` at `90641ed` **cannot run as written** — see the sibling record. It names the
base image and mounts the repository where no eval script looks. It was **not edited**: it
remains P5's committed method, and whether to repair it is a separate decision.

This run substitutes two things, and a reader must not mistake it for the committed method:

1. **The derived image**, built from this repository's own root `Dockerfile`
   (`docker build -t ctpb-dev:2026-09-06 .`), rather than the base
   `sleepingeight/clang-cpp-refl:latest` the script names.
2. **The mount corrected to `/workspace`** — `-v "$REPO":/workspace -w /workspace` — where
   every eval script actually reads, rather than the script's `/work`.

Both were required. Fixing only the image would have produced **numbers that looked valid and
were wrong**: `/workspace` would have held the `COPY`-time snapshot baked at build, the eval
scripts would have compiled that, and the live checkout mounted at `/work` would have gone
unread. The arms would all have passed.

The five arms, their labels and their order are otherwise identical to `rerun_all.sh`, as are
the iteration counts, trial counts and warmup values — those live in the eval scripts, which
were not touched. The driver is in `method/rerun_corrected.sh`; `method/run_pinned.sh` wraps
it with the pin, the clock sampler and a restore-on-trap.

**A third deviation, by omission:** the corrected driver does **not** reproduce
`rerun_all.sh`'s environment block, so this run has no auto-captured `uname`, clang version or
git commit. Those facts are recorded by hand in the table above instead. Anyone comparing the
two record directories should expect no `environment.txt` here.

## Verified before the numbers were trusted

Full output in `verification.txt`.

- **`protoc` inside the derived image is `libprotoc 3.21.12`**, `libprotobuf 3.21.12`,
  `libprotobuf.so.32`. The committed results record `Protobuf: 3.21.12-8.2ubuntu0.3` and
  `libprotobuf.so.32`. **The protobuf side is the same version**, which is what makes this a
  comparison rather than merely a replacement.
- **The mount serves the live checkout, not the snapshot.** A sentinel file written on the
  host was read back at `/workspace` **with** the mount and was **absent** without it —
  proving the two differ and that the mount shadows the baked copy. Sentinel removed after.
- **Image identity:** `sha256:699868a08613eb73eba8db97ad3d153aaad422123aac971f85d049abef17703d`,
  built locally 2026-09-06T12:58:50+03:00 from base
  `sha256:22cbbd30b8bdec714ba5d06e60c9ff43a30e7cb27b8bd4b9f29a249d6597c426`. Locally built, so
  it has no RepoDigest; the image ID is the record and the base digest pins what it came from.

## The clock, and a correction to the sibling record's caution

| point | governor | EPP | min/max kHz | boost |
|---|---|---|---|---|
| baseline, before pinning | `powersave` ×16 | `performance` | 1102866 / 4465261 | 1 |
| during the campaign | `performance` ×16 | `performance` | 3200000 / 3200000 | 0 |
| after restore, read back | `powersave` ×16 | `performance` | 1102866 / 4465261 | 1 |

Restore verified against the baseline field by field, EPP included — captured **before**
pinning this time, which the previous run missed.

**The pin did deliver 3.2 GHz here.** Sampled every 2 s across all 16 CPUs for the whole run,
45 samples: median **3200 MHz exactly**, per-sample medians 3169–3200, spread 0.97%, minimum
single reading 2718 MHz.

The sibling record cautions that this pin settles at ~3010 MHz, about 5.7% low. That figure
was taken under a **synthetic 16-thread spin** and is the worst case, not this workload:
these benchmarks are largely single-threaded, so the package sustains the full 3.2 GHz. Both
numbers are correct under their own load. Neither should be quoted without it.

## Two things a reader should not misread

- **The host changed.** The committed figures are from an **AMD Ryzen 5 3600**; this is an
  **AMD Ryzen 7 5800H**. Nanosecond cells are **not** comparable across the two tables, and
  the absolute improvements above owe something to the faster machine. What *is* comparable
  is the **ratio within each run**, because both arms are built once and interleaved in a
  single session on one host — which is exactly what the harness rewrite fixed and why the
  ratio is the quantity the paper states.
- **The declared gap stands.** The environment block is captured on the **host** while the
  timed work runs **in the container**, so `virtualised: none` describes a host whose
  measurements came from Ubuntu 24.04 inside Docker. This applies to the committed figures
  too, and belongs in P5's methodology section.

## What was not done

- `rerun_all.sh` was **not edited**, and neither was any eval script. The deviation lives
  entirely in `method/`, outside the repository's committed method.
- `rerun_all.sh:54`'s stale "This repository is public" claim (the repository is private) was
  left uncorrected — no dispatch to edit it.
- **Not pushed.** This remote is outside the standing push approval.
- The name and address in `raw/binary_size.txt` are the repository's own example payload,
  already committed at `examples/person.cpp`; nothing new is exposed by this record.

## Files

`raw/` is the campaign output verbatim. `method/` is the driver, the pin wrapper and the
sampler, path-redacted. `host-state/` holds the three clock records and the 45 in-run
samples. `verification.txt` is the protoc, image-identity and sentinel evidence.

```
88205e361d0d5d4520787941121840ebceea36cf7f14bba6ce5e5aff8c91c0a0  host-state/clock-during-run.txt
7c26e952e0b82693f94b9995c528d94204e4b736a3c48e423638a648270168b4  host-state/host-state-0-baseline.txt
63aa11c270d25e42fef42ef0c0fc06ecb85e87641310209fd1ec899fa55625d3  host-state/host-state-during.txt
2cb7a857aa0788a42dd76919e67736d99714321510fc4b45bdb861912571097c  host-state/host-state-restored.txt
9147ed4c0cdf259147ce4ef0f15ea00f0ac6cf57a32d194f630f16f75bdea9ee  method/clock_sampler.sh
3d9d8dd39b516338f42d5a1ec1af02662287f3951ba5d57d8cf600423cedf85c  method/rerun_corrected.sh
d53e6e4512d2e45c287387a49ce78e175e9cb5dc1daef0a2beb6ec5a4c60d485  method/run_pinned.sh
ef82cc287b0893e65320e4d5acc36233391de0f96cc4e03fd7f70a7bb750c298  raw/binary_size.txt
c6fe7216b4bdef66dfacef3c4c0eec5e546efc9466d88da3173661847b27b37c  raw/compile_time_latency.txt
cbbc7bce5ddef49570e7f6803742da23d786ce201efb63d72029d1b68673b384  raw/constexpr_step_limit.txt
9bff25c5857acdef5dc293bca49bf73d8453e42de58a45ae4e268360c9cde226  raw/runtime_comparison_lite.txt
e1eabaa5f74a6633a4fc303fbc2166ac59ecfd9a3be816d7485be83d3f85c803  raw/runtime_comparison.txt
ef2fc63025c9c851b4ec53337bfd0c866df00af09237c7553ac56f8b3bf9bb0a  verification.txt
```
