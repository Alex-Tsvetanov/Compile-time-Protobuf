# UTF-8 validation, measured — 2026-09-07

The costing said this would turn a 4% win into a tie. **It did not, and the estimate was
wrong in the direction I flagged rather than the direction that would have mattered.**

`measurements/2026-09-07-laptop-utf8-costing` priced a standalone validator at 3.894 ns per
message and added it to a measured decode time, giving ~80.4 ns against libprotobuf-lite's
79.83 — a tie. I noted at the time that the arithmetic was pessimistic, because a real
implementation validates a string already in cache that the decoder has just written. That
caveat turns out to be the whole story.

## The A/B

Two git worktrees, HEAD and HEAD~1, so no stash could silently fail. Verified different
before measuring: `reflection.hpp` has 4 references to `valid_utf8` in one tree and 0 in the
other, different sha256. Eight rounds per side, interleaved without/with so drift is shared.
Each round is the committed `eval_runtime_comparison_lite.sh`, 7 internal trials, run in the
pinned container. Machine pinned throughout, restored after, verified against the captured
baseline.

| round | without: ctpb | lite | ratio | with: ctpb | lite | ratio |
|---|---|---|---|---|---|---|
| 1 | 59.425 | 61.144 | 0.9719 | 60.048 | 64.293 | 0.9340 |
| 2 | 63.592 | 70.150 | 0.9065 | 63.556 | 65.479 | 0.9706 |
| 3 | 59.893 | 60.799 | 0.9851 | 60.545 | 60.527 | 1.0003 |
| 4 | 63.259 | 69.188 | 0.9143 | 63.840 | 66.894 | 0.9543 |
| 5 | 63.220 | 65.973 | 0.9583 | 59.150 | 63.338 | 0.9339 |
| 6 | 59.526 | 63.632 | 0.9355 | 59.174 | 65.469 | 0.9038 |
| 7 | 59.584 | 64.258 | 0.9273 | 59.686 | 62.706 | 0.9518 |
| 8 | 59.555 | 62.774 | 0.9487 | 58.687 | 62.547 | 0.9383 |

|  | ctpb decode ns (median) | range | ratio vs lite |
|---|---|---|---|
| **without validation** | **59.739** | 59.425 – 63.592 | **0.9421** |
| **with validation** | **59.867** | 58.687 – 63.840 | **0.9451** |

## The result

**Measured cost: +0.129 ns on a ~60 ns decode. The ranges overlap completely.**

The cost of UTF-8 validation is **below the resolution of this instrument on this host**.
Round-to-round spread is about 4 ns (7%); the effect is 0.13 ns (0.2%). No claim to a
precise cost is available from this data and none is made — only that it is smaller than
anything this rig can see.

**ctpb still beats libprotobuf-lite on decode with validation in place**, by about 5% here.
The ratio moves 0.9421 → 0.9451, which is itself within the noise.

So the coordinator's first outcome — *ctpb validates and still wins* — is the one that
happened. I predicted it was not available on this payload. **That prediction was wrong**,
and the reason is the caveat I attached to it: adding a standalone validator's cost to a
measured decode overstates it by roughly thirty-fold, because in place the validator walks
a short string that is already in L1 and was written microseconds earlier by the decoder
itself. 3.894 ns measured cold and alone; ~0.1 ns measured in situ.

## What this record does NOT establish

* **The absolute numbers are not P5's.** P5 published 76.52 ns against 79.83; this run sees
  ~60 against ~64 on the same code. Different session, different thermal state. Only the
  *ratio* and the *within-run A/B* transfer, and the P5 record's own numbers must be
  re-taken rather than adjusted.
* **The instrument's noise exceeds the effect**, so "0.129 ns" is a point estimate with no
  interval behind it. The defensible statement is "below measurement resolution", not "0.13
  nanoseconds".
* Bimodal clustering around 59.5 and 63.5 ns is visible in both columns and is this host's
  known thermal behaviour, not a property of either build.

## Reproducing

    git worktree add <dir> <commit-without>          # verify: grep -c valid_utf8 differs
    docker run --rm -v <tree>:/workspace -w /workspace ctpb-dev:2026-09-06 \
      bash benchmarks/eval_runtime_comparison_lite.sh

Raw per-trial output for all sixteen rounds is in `raw/`.
