# P5 re-run of 2026-09-06 — the campaign ran, produced no figure, and exited 0

/ A2, dispatched by the Publishing coordinator. Taken on the Linux laptop.

## Verdict first

`benchmarks/rerun_all.sh` completed with **exit status 0** and **four of its five arms
failed**. No throughput number, no binary size and no compile-time latency was produced.

**2.81× and 1.71× are neither confirmed nor refuted by this run. They were not measured.**
The dispatch was written expecting those figures might not survive; that is still an open
question and nothing here bears on it.

The one arm that succeeded is the constexpr step limit, which needs no protobuf and no
mounted source: it confirms the default limit is consistent with 1048576 and that the
project sets no constexpr flag of its own.

## What ran

| | |
|---|---|
| script | `benchmarks/rerun_all.sh`, unmodified |
| repo commit | `90641ed940d5bb769d6790424f8b1bdede668960` |
| image | `sleepingeight/clang-cpp-refl@sha256:22cbbd30b8bdec714ba5d06e60c9ff43a30e7cb27b8bd4b9f29a249d6597c426` |
| image created | 2026-03-14, `linux/amd64`, native on this host — no emulation |
| when | 2026-09-06, 12:49 EEST |
| host | AMD Ryzen 7 5800H, 16 logical CPUs, Linux 7.2.3-arch1-2, on AC |

The image is recorded by digest deliberately. `rerun_all.sh` names the tag `:latest`, and a
tag is not a record: a result whose image cannot be named later has the same defect as a
result whose revision cannot be named.

## Why it failed: one mistake, two defects

`rerun_all.sh` runs the **base** image. The measurements were designed for an image **built
from** that base by this repository's own `Dockerfile`.

The `Dockerfile` at the repo root does exactly two things that matter:

    FROM --platform=linux/amd64 sleepingeight/clang-cpp-refl:latest
    RUN apt-get install -y ... libprotobuf-dev protobuf-compiler ...
    WORKDIR /workspace
    COPY . /workspace

Run the base instead of the built image and both of those are missing, which is precisely
the two failure modes observed:

**Symptom 1 — no protobuf.** `runtime_comparison` and `runtime_comparison_lite` both died
on `protoc: command not found`. Probed directly: the base image has no `protoc` on `PATH`,
no `protoc` binary anywhere on the filesystem, and no `/usr/include/google/protobuf`. The
script's own environment block says so in its output and the run continued anyway —
`protobuf: absent`, `libprotobuf: unknown`. Protobuf is installed by the `Dockerfile`'s
`apt-get` line, not by the base.

**Symptom 2 — source at the wrong path.** `binary_size` died on
`/workspace/examples/person.cpp: no such file` and `compile_time_latency` on
`'ctpb/ctpb.hpp' file not found`. Every eval script hardcodes `/workspace`
(`eval_binary_size.sh:10-11`, `eval_compile_time_latency.sh:73`,
`eval_runtime_comparison.sh:220`, `eval_runtime_comparison_lite.sh:157,160`) because the
`Dockerfile` puts the source there with `COPY . /workspace`. But `rerun_all.sh:99` mounts
the repository at **`/work`**:

    docker run --rm --platform=linux/amd64 -v "$REPO":/work -w /work "$IMAGE"

`/workspace` does exist in the base image — as an **empty directory**, the mount point the
`Dockerfile` later fills. So the compilers looked in a real but empty path rather than
erroring on a missing one.

Both symptoms share one mistake, but they are **two defects, and fixing the image alone
would hide the second one.** Build the `Dockerfile` and run `rerun_all.sh` unchanged, and
both arms start passing — because `COPY . /workspace` has baked a copy of the source into
the image at build time. The eval scripts would then compile `/workspace/include`, the
build-time snapshot, while `-v "$REPO":/work` mounts the live checkout somewhere nothing
reads. The numbers would look valid and would silently describe whatever source was present
when `docker build` ran.

A correct re-run therefore needs **both**: the derived image *and* the mount moved to
`-v "$REPO":/workspace -w /workspace`. Nothing here indicates the eval scripts are wrong.

## Exit 0 is itself the finding

`run_in_image()` captures each arm's failure, prints `FAILED`, and continues; the script
ends `done.` and returns 0. Anything consuming the exit status — CI, a wrapper, a person
scrolling to the end — is told the campaign succeeded.

This is worth stating plainly because it is the same shape as a defect found on this host
the same morning: `cpupower.service` is enabled and active, reads an `EnvironmentFile` that
does not exist, applies nothing, and exits 0. A component that reports success having done
nothing is the failure this corpus keeps meeting.

## The measurement image is derived, not the base — and is reconstructible

The committed results in `benchmarks/results/` name their environment as the **`ctpb-dev`
Docker image (`sleepingeight/clang-cpp-refl` base)**, dated 27–28 August 2026, recording
`protoc: libprotoc 3.21.12` and `libprotobuf.so.32`. That derived image is in no registry
this record can name, and it is not what `rerun_all.sh` runs.

**No evidence suggests the base tag itself moved.** The base was built 2026-03-14, which is
entirely consistent with being the same base `ctpb-dev` was built on in August. The problem
is not a shifted tag; it is that the base was never the measurement image.

The derived image does appear reconstructible. The base is Ubuntu 24.04.4 LTS, and
`apt-cache policy protobuf-compiler` inside it gives candidate **3.21.12-8.2ubuntu0.3** —
the same 3.21.12 the committed results record. So `docker build` from this repository's
`Dockerfile` should reproduce the protobuf toolchain the original measurements used, which
makes a corrected re-run comparable on that axis rather than merely a replacement.

## Gates, and the clock

**There were no gates.** `rerun_all.sh` does not use WebFrame's `validity.py`; it has no
frequency-drift rule, no governor precondition and no admission record. An unpinned run here
would not have been refused, only unlabelled. The pin below was applied by hand for that
reason, on the coordinator's dispatch.

| point | governor | min/max kHz | boost |
|---|---|---|---|
| boot, 12:39, before this session acted | `powersave` ×16 | 1102866 / 4465261 | 1 |
| after re-applying the performance governor | `performance` ×16 | 1102866 / 4465261 | 1 |
| during the campaign | `performance` ×16 | 3200000 / 3200000 | 0 |
| after restore, read back | `powersave` ×16 | 1102866 / 4465261 | 1 |

**Under all-core load the pin does not deliver 3.2 GHz.** Verified under a synthetic
16-thread spin before the campaign: 3019 MHz on ten cores, 3004 on six — uniform to 0.5%
across cores, which is the stability the pin exists for, but about 5.7% below the 3200 MHz
requested, because with boost disabled this package does not sustain base clock on all
sixteen threads.

Two limits on that figure. It was taken under a synthetic all-core spin, **not** during the
campaign, whose compiles are largely single-TU; the clock actually held during the timed
work was not sampled. And it does not contradict the 1.6% figure recorded for this host
elsewhere, which was measured under a different load. Read it as "the all-core sustained
clock under this pin is ~3010 MHz", not as "the pin never reaches 3.2 GHz" — and do not
quote this run as "pinned to 3.2 GHz" without the condition.

Restore was verified by reading the values back, not by assuming the command took. One knob
is **not** verifiable: `energy_performance_preference` was not captured before this session
acted, so there is nothing to compare it against. It now reads `performance`; the
`amd-pstate-epp` default under the `powersave` governor is normally `balance_performance`. It
was left as found rather than set to a guessed value. A reboot resets it.

Note also that the script's own environment block records `governor: performance` and
**nothing about the frequency pin** — no min, no max, no boost. The pin is invisible in this
repository's own record of the run. That is why `host-state/` exists.

## Declared gap: the environment block describes the wrong machine

The environment block is captured **on the host**, before anything runs. The timed work runs
**inside the container**. So `virtualised: none` is a true statement about the host, attached
to measurements that were taken under Ubuntu 24.04 inside Docker.

This is a declared gap, not a defect to be silently tolerated, and **it has to reach P5's
methodology section** rather than sitting in a record nobody reads. It applies to every
figure this harness has ever produced, including the committed ones, not only to this run.

## Two smaller facts, so they are not misread later

- `git dirty: yes` in the environment block is the script's **own untracked output
  directory**, created moments earlier. The tree was clean at launch and no source was
  modified. It is not evidence of a measurement taken on edited sources.
- `rerun_all.sh:54` still asserts "This repository is public". `gh` reports it PRIVATE. The
  redaction policy that comment justifies is right either way; its stated premise is not.
  **Left uncorrected and unedited** — different repository, no dispatch to edit it. This is
  the third recorded instance of that stale claim.

## What was refused, and what was not done

- Nothing was edited to make the campaign run. `rerun_all.sh` was executed exactly as
  committed, and the mount-point and image defects above were diagnosed, not patched.
- The corrected re-run — building this repository's `Dockerfile` and running the eval scripts
  against `/workspace` — was **not** performed. It changes the method the dispatch named, so
  it needs its own dispatch.
- **Not pushed.** This remote is outside the standing push approval; the push is a separate
  decision for Alex.
- Machine identity: home paths in `campaign-stdout.txt` were replaced with `<repo>` and
  `<home>`. The raw arm output and the environment block were checked and contained none —
  `uname -srmo` is deliberately used in place of `uname -a`, which would add the nodename.
  The CPU model is apparatus and stays.

## Files

`raw/` is the campaign's output verbatim. `host-state/` is the clock record the script does
not keep. `campaign-stdout.txt` is the full console output, path-redacted as described.

```
4693d04b162e8a042393cc8569b20d277fe92dde1d929135cf6568c571c147a2  campaign-stdout.txt
5a5a166d6121e6208e1ac2c6e6dcbdfa73d074876a35dedd25d53330f037d902  host-state/host-state-1-before.txt
38ed0972aa53475397eec925874c0f5ba473c207d21459c5a2235bbf17930dc6  host-state/host-state-2-during.txt
84437f404bd34e9e5c29e3b60b2ad8b8d83a6d733462c7aa7fab00cec1c070ad  host-state/host-state-3-after.txt
5ae52a040c08a5ed0503c44cce63fe8245f9fc7bdc1a71c7c062d034ca9d9349  host-state/host-state-3b-restored.txt
600c8f36e9620bd7bc10c5eee0e75868f07a583107ef07174634bf380473a29d  raw/binary_size.txt
8e4decbf92a1d27886963899b1e56339a115e4bb20d726c552bd53270c2259ce  raw/compile_time_latency.txt
85789b9d32e91817ccc095c8ee5219182648d7ae606c81e3a05d5d5c8fa5a950  raw/constexpr_step_limit.txt
4f5ce5848300d1e96439809327f26838b0caf26014b041e899bfd90ac91d5844  raw/environment.txt
e182d9db9cee6ca92376c24683ff1f7391ee4f13ee61e0368138d1e82a2c5f93  raw/runtime_comparison_lite.txt
431aaa786e5df798b9a01b35115c5eaae822d5c82599ec7cee2e4a2281ecd4f7  raw/runtime_comparison.txt
```
