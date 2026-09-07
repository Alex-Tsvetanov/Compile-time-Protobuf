# P5 re-take, through the repaired driver — 2026-09-07

One host, one sitting, one binary per arm, clock pinned and restored against a captured
baseline. **Trial 0 is excluded as warm-up throughout and the exclusion is stated rather
than silent.** Median with full range, not mean with standard deviation.

**This supersedes `measurements/2026-09-06-laptop-p5-single-host`.** That set was taken
before ctpb validated UTF-8 and before the driver was repaired.

> ## Do not reconcile these absolutes with the published ones
>
> P5 published decode as 76.52 ns against 79.83; this session measures ~62 against ~69 on
> the same comparison. Different session, different thermal state, same code path. **Only
> the ratios and the within-run comparisons transfer.** The absolutes are re-taken here, not
> adjusted from there, and the two sets must not be mixed.

## Results

### Runtime, against libprotobuf-lite — the fair baseline

| | ctpb | libprotobuf-lite | ratio |
|---|---|---|---|
| **encode** | **6.614 ns** [6.268–6.698] | 36.992 ns [35.691–37.087] | **5.593× faster** |
| **decode** | **62.218 ns** [58.114–62.859] | 68.553 ns [64.715–69.242] | **1.102× faster** |

### Runtime, against full libprotobuf

| | ctpb | libprotobuf | ratio |
|---|---|---|---|
| encode | 6.466 ns [6.416–6.831] | 34.755 ns [34.675–36.755] | 5.375× faster |
| decode | 58.434 ns [57.334–59.064] | 63.552 ns [63.401–68.705] | 1.088× faster |

**The decode figures carry no UTF-8 caveat, because the premise of that caveat is gone.**
ctpb now validates `string` fields as proto3 requires, so this is like-for-like. Every
previous version of this number was attached to a sentence saying it was not; that sentence
comes off.

### Footprint, from exact bytes

| | bytes (stripped) | ratio |
|---|---|---|
| ctpb | **18,776** | — |
| protoc, dynamic | 27,504 | 1.46× |
| protoc, static | 1,759,368 | **93.70×** |

Read from the `Exact sizes (bytes):` block. Note that block writes a **literal backslash-t**
rather than a tab: a parser expecting a real tab reads the line as one field and reports the
footprint as 1.12× instead of 93.70×. That has happened once already.

### Compile-time decode latency

| schema | median | range |
|---|---|---|
| small | 0.492 µs | 0.489–0.500 |
| medium | 1.938 µs | 1.920–1.968 |
| large | 7.239 µs | 7.227–7.256 |

### constexpr step limit

Default limit confirmed as Clang's documented 1,048,576; the project sets no constexpr flag
of its own anywhere in the build. Bisection puts the threshold at 304 fields.

## The driver this ran through had to be repaired first

`benchmarks/rerun_all.sh` **exited 0 having measured nothing**, by three faults compounding:

1. `IMAGE` was the base reflection image, which carries no protobuf, so every runtime
   comparison failed to compile.
2. The mount was `/work` while every eval script hardcodes `-I/workspace/include`.
3. `run_in_image` printed `FAILED` to stderr and **returned success**, so the script exited
   0 with an empty results directory and `rerun_all.sh && echo fine` printed `fine`.

The third is why the first two were invisible. Repaired at `be4497e`: derived image built
rather than pulled, mount at `/workspace`, a failure counter, and a non-zero exit saying the
output directory is incomplete. **Verified by breaking it** — pointed back at the base image
the repaired script exits 1 and reports 4 failed steps, where the old code exited 0.

## Validity

Pinned before, restored after, verified against the captured baseline
(`governor=powersave epp=balance_performance`). Host reported as apparatus only — CPU model,
core count, kernel release, no machine identity. Load average at start 0.56. Image id
printed by the driver so the record names what produced it rather than a tag that may move.

No stray processes before or after.
