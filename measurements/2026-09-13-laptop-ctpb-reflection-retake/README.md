# The five measurements, on the reflection path — 2026-09-13, alex-laptop

**Step 0 first, because everything else is void without it: `CTPB_HAS_P2996` printed `1`.**
And the negative control printed `0`, so the diagnosis that produced this retake is
reproduced rather than trusted:

    clang++ -O3 -std=c++26 -freflection -I/workspace/include gate.cpp          → CTPB_HAS_P2996=1
    clang++ -O3 -std=c++26 -freflection -stdlib=libstdc++ -I... gate.cpp       → CTPB_HAS_P2996=0

Environment in `raw/environment.txt`. Native x86-64 in a container on 16 real cores, clang
21.0.0git `bloomberg/clang-p2996` @ `2ea0a79fe`, protoc 3.21.12. **The host toolchain cannot
participate: stock clang 22.1.8 rejects `-freflection` outright**, so there is no native
reflection arm and every paired figure below is a container figure.

## 1. Build cost — the crossover is the finding

`raw/compile_cost_raw.txt`, `raw/compile_cost_summary.txt`. Medians over 6 retained trials,
trial 0 dropped as warm-up.

| fields | ctpb (s) | protoc (s) | ratio |
|---|---|---|---|
| 5 | **2.09** [2.08, 2.10] | 2.49 [2.47, 2.50] | **0.84× — ctpb faster** |
| 25 | **2.95** [2.90, 2.99] | 2.63 [2.60, 2.66] | 1.12× — ctpb slower |
| 100 | **9.05** [8.59, 9.14] | 3.15 [3.12, 3.21] | **2.87× — ctpb slower** |

**Read the direction carefully: at 100 fields ctpb is 2.87× SLOWER to build, not faster.**
ctpb wins the build at a small schema and loses it badly at a large one, and **the crossover
lies between 5 and 25 fields** — 1.12× at 25 is already ctpb behind. (An earlier version of
this sentence said between 25 and 100, which the table on the line above contradicts; the
table was always right.) The two arms scale differently rather than differing by a
constant: ctpb goes 2.09 → 2.95 → 9.05 while protoc goes 2.49 → 2.63 → 3.15, because the
protoc arm's growth is confined to compiling more generated code (1.26 → 1.39 → 1.86 s) while
its other three steps are flat — `protoc` itself never exceeds **0.015 s** and the link never
exceeds 0.058 s.

Generated source lines, for the record: protoc emits 753 / 1,919 / 6,369 lines at 5 / 25 /
100 fields; ctpb emits 0.

## 2. Footprint — and the published advantage needs three numbers, not two

`raw/item2_binary_size.txt`, `raw/item2_sections.txt`. Exact sections from `size -A`, never
the `size` summary column.

| artefact | file (stripped) | `.text` | `.rodata` | `.data` |
|---|---|---|---|---|
| `person_ctpb` | 18,792 | **7,027** | 812 | 24 |
| `person_protoc_dyn` | 27,504 | **4,728** | 815 | 160 |
| `person_protoc_static` | 1,759,368 | **1,323,755** | 64,292 | 2,904 |

**ctpb's own `.text` is 7,027 B against the generated program's 4,728 B — 48.6% LARGER —
and this reproduces the emulated finding to the byte.** Identical numbers on different
hardware is expected rather than surprising: code size is a property of the compiler and
target, not of the host it ran on.

**But the honest statement needs the third row.** The 4,728 B binary does not contain a
protobuf implementation; it calls one. `ldd` confirms it: `person_protoc_dyn` loads
`libprotobuf.so.32`, and `person_protoc_static` loads no protobuf at all because it has
**1,323,755 B of `.text` inside it — 188× ctpb's**. So:

- **against a dynamically linked protobuf, ctpb's emitted code is larger** (7,027 vs 4,728),
  while the whole stripped file is still smaller (18,792 vs 27,504);
- **against a statically linked protobuf, ctpb is smaller by two orders of magnitude.**

A footprint claim that cites one of those without the other is quotable in either direction,
which is what makes it worth stating as three rows.

**Validation output is the reflection path**, checked as asked: the ctpb binary prints
*"Successfully decoded Person via compile-time reflection"*; the protoc binary prints *"via
protoc"*; both encode to 42 wire bytes.

## 3. Compile-time parser latency — and a correction to the brief

`raw/item3_compile_latency.txt`. Per-parse latency, median of 7 trials × 20,000 iterations:
**small (5 fields) 0.518 µs [0.515, 0.527], medium (25) 2.012 µs [1.984, 2.048], large
(100) 7.491 µs [7.442, 7.530].** All seven trials are in the raw file; this script prints no
summary of its own, so these medians are computed over all seven rather than dropping trial 0
the way the compile-cost summariser does — the warm-up loop is inside the binary here. **For a figure directly comparable with the
paper, which retains six trials, the same data gives 0.522 / 2.013 / 7.487 µs** — the two
conventions differ by less than one part in two hundred, and both are in the raw file.

**This script is not "now on the reflection path". It never was on it, because the gate does
not reach the code it measures.** It exercises `ctpb::parser::parse_file()`, and the P2996
gate selects between reflection and fallback in `reflection.hpp` / `view.hpp`, which a
parser-only program never instantiates. Demonstrated rather than argued: the same probe
compiled with the gate on and forced off produces **byte-identical `.text` of 18,061 B** and
the same parse result. So these numbers are the same quantity the published table measured,
and whatever else is wrong with that table, the gate is not it.

## 4. The synthesis step limit — bisected, and it is a compiler default rather than a bound

`raw/item4_bisect.txt`, method in `method/bisect_synthesis.sh` and `method/gen_tu.py`.

**317 fields compiles. 318 does not.** Ladder 25/100/300 passed, 400 failed, then binary
search closed it: 312 pass, 318 fail, 315 pass, 316 pass, 317 pass.

The diagnostic at 318 is not a synthesis error. It is the constant evaluator running out of
steps **inside the destructor of the `std::vector<std::meta::info>` that synthesis builds**:

    error: evaluating expression of a consteval block must be a constant expression
    note: constexpr evaluation hit maximum step limit; possible infinite loop?
      in call to '__destroy_at<meta::info, 0>(&{*new meta::info[512]#30}[181])'
      in call to 'this->__vec_.clear()' ... '~vector()'

**So the bound is `-fconstexpr-steps`, and raising it removes the bound entirely within
everything I tested:**

| fields | default steps | `-fconstexpr-steps=100000000` |
|---|---|---|
| 318 | fail | **pass, 3.6 s** |
| 400 | fail | **pass, 4.0 s** |
| 800 | fail | **pass, 6.2 s** |
| 1,600 | — | **pass, 10.9 s** |
| 3,200 | — | **pass, 19.8 s** |

**No ceiling was found up to 3,200 fields**, and compile time grows roughly linearly past the
default limit. Any claim of the form "ctpb supports N fields" is therefore a claim about
clang's default step budget and not about this library, and it should be stated with the flag
beside it.

Two further notes for whoever writes this up. **The synthesis bound (317) is LOOSER than the
published parser bound (300/301)**, so synthesis is not the tighter constraint the brief
assumed. And compile time at the bound is **3.6 s**, not the 38.3 s measured under emulation —
about an 11× emulation penalty, which is worth knowing before anyone treats emulated compile
seconds as a cost.

## 5. Throughput, one program per arm

`raw/runtime_raw.txt`, `raw/runtime_summary.txt`. 5 reps × 20 rounds × 50,000 ops, 47-byte
message, **wire bytes identical across arms (47)** — the script's own parity gate passed.

| operation | ctpb (ns/op) | libprotobuf (ns/op) | ratio |
|---|---|---|---|
| encode | **7.70** [7.10, 9.11] | 36.97 [33.57, 41.37] | 4.80× |
| decode | **50.38** [45.33, 52.77] | 70.39 [63.85, 77.37] | 1.40× |

**The drift mitigation is falsifiable, and it passed.** `raw/runtime_per_rep.txt` gives
per-rep medians so the reader does not have to take alternation on faith:

| rep | ctpb enc | pb enc | enc ratio | ctpb dec | pb dec | dec ratio |
|---|---|---|---|---|---|---|
| 1 | 7.543 | 34.331 | 4.55× | 47.796 | 65.731 | 1.38× |
| 2 | 7.505 | 36.971 | 4.93× | 47.660 | 69.663 | 1.46× |
| 3 | 7.766 | 35.232 | 4.54× | 52.029 | 65.745 | 1.26× |
| 4 | 7.530 | 37.422 | 4.97× | 50.261 | 72.359 | 1.44× |
| 5 | 8.266 | 37.967 | 4.59× | 51.629 | 73.898 | 1.43× |

**Relative spread is symmetric between the arms** — encode 10.09% of median for ctpb against
9.83% for protobuf, decode 8.69% against 11.72% — so neither arm is absorbing the drift, and
alternating whole processes did the job it was meant to do. Both arms also drift **in the same
direction** across reps, which is what a common-mode effect looks like and is the reason the
ratio survives it.

**But the ratio itself is not a point.** Per-rep it runs **4.54–4.97× on encode and
1.26–1.46× on decode.** The decode ratio should be quoted as a range or with its spread; a
bare 1.40× overstates what five reps support.

## An unpaired baseline-staleness check, and it cuts both ways

`raw/native36_raw.txt`. The paired figures above use protobuf **3.21.12**, a 2022 release, and
that invites the question "you beat an old protobuf". Built the protobuf arm **alone** against
the host's **36.0.0** with clang 22.1.8, same harness, same message:

    protobuf 36.0.0, native, 20 rounds:  encode 51.07 ns [48.60, 56.69]   decode 48.89 ns [47.29, 49.93]
    protobuf 3.21.12, container:         encode 36.97 ns                  decode 70.39 ns

**This is NOT a paired measurement and must not enter a comparison table**: different
compiler, different standard library configuration, native rather than containerised, and no
ctpb arm exists on that toolchain because the host clang has no reflection.

With that said plainly, it is informative in both directions. Newer protobuf is **slower to
encode** (51.1 against 37.0) and **faster to decode** (48.9 against 70.4). The second half is
the one that matters: **ctpb's decode is 50.38 ns, so against protobuf 36.0.0's 48.89 ns the
decode advantage would not merely shrink — it would disappear.** Nothing here establishes
that, because the measurement is unpaired. It does establish that **the 1.40× decode ratio is
contingent on the protobuf version** and should not be presented as a property of the
approach until someone builds 36.0.0 inside a reflection-capable toolchain and re-pairs it.
Encode's 4.80× is not at risk from this: 7.70 against 51.07 would be wider, not narrower.

## Two defects in the harness, reported rather than worked around

**`eval_binary_size.sh` and `eval_compile_time_latency.sh` are not executable** — mode
`100644` in git — so the brief's `./benchmarks/eval_binary_size.sh` fails with
`Permission denied` and exit 126. I ran them as `bash <script>`. The reason nobody noticed is
that `rerun_all.sh:139` invokes them as `bash "$script"`, which masks the missing bit; anyone
following a README or a message rather than that script hits it immediately.

**`eval_compile_time_latency.sh` discards its own stderr** (`"$DIR/bench" 2>/dev/null`),
which is where its anti-optimisation `sink=` line goes. Harmless today, but it also discards
anything the binary might say about a failure, and this repo has already had one harness exit
0 having measured nothing.

## What was deliberately not done

`rerun_all.sh` was not run — it reproduces the fallback numbers with a clean conscience, and
this session carries a standing note that it runs the base image rather than the derived one.
The old `eval_runtime_comparison{,_lite}.sh` were not run. Nothing in `paper/` was touched.
The governor was **found** at `performance` with EPP `performance` and **left untouched**;
restoring it is Alex's call and not this session's.
`sudo -n` succeeds on this host, so pinning and restoring are both possible for this user —
the machine is simply still in the state the 2026-09-07 sitting pinned it to, against a
captured baseline of `governor=powersave epp=balance_performance`. Restoring it is one
command and is not mine to run mid-campaign.


## A correction this record owes to its own method note

I told the writer that only one 6 September record carried restore evidence and named the
path. **That was wrong: four do** —
`2026-09-06-laptop-p5-single-host/raw/host-state.txt:3`,
`2026-09-06-laptop-ctpb-rerun-corrected/host-state/host-state-restored.txt`,
`2026-09-06-laptop-ctpb-decode-experiments/raw/host-state.txt`, and
`2026-09-06-laptop-ctpb-rerun/host-state/host-state-3b-restored.txt`, each with its own
`AFTER RESTORE` state read back.

The count was corrected to three on the second pass and is corrected to four here, which is
itself the point: the fix for a truncated search is to re-run it untruncated and count, not
to raise the number until it stops looking wrong. `grep -rl` over `measurements/` returns 11
files; four are restore artifacts and the rest are READMEs and method scripts that mention
the string.

*Superseded — kept so the correction below has something to correct.* This paragraph
originally read `2026-09-06-laptop-ctpb-decode-experiments/raw/host-state.txt` as an
incomplete restore and concluded that EPP had been left pinned since 6 September and the
governor since 7 September. Both halves were wrong. That file's own baseline was already
`epp=performance`, so it restored exactly what it captured; and the host rebooted on
2026-09-12, so nothing from either sitting can be standing. The live pinned state has a cause
this record does not know. See *Two more corrections* below.

**The cause is worth more than the correction.** The search that produced the wrong answer was

    grep -rn -i 'AFTER RESTORE\|verification=ok' measurements/ | head -5

and the same grep without the pipe returns **12 lines across 11 files**. `head -5` truncated
the evidence and I read the truncation as the result. This session already carries a standing
note that a capture which discards the discriminant — `| head`, `| tail`, `grep -o` — converts
a check into a guess; it was hit anyway, in the one command whose whole purpose was to count
occurrences.

**What does survive the correction:** the 2026-09-07 retake still has no restore artifact. Its
`raw/` holds `pin-state.txt` (the captured baseline) and `environment.txt` (`governor:
performance`, so the timed work did run pinned) and nothing recording a restore — which is
consistent with the live state I found six days later, and with the paper having dropped the
restore clause.


## Two more corrections, and the second one retires a story we both told

**The restore artifacts are FOUR, not three, and not one.** The fourth is
`2026-09-06-laptop-ctpb-rerun/host-state/host-state-3b-restored.txt`, which says
`restore verification: ok` rather than the `AFTER RESTORE … verification=ok` the other three
share.

**The sharper form of the lesson is the writer's, not mine.** My own untruncated
`grep -rln` had that file in its output; I then classified from the truncated `-n` view I had
already looked at and said three. **The fix for a truncated search is to re-run it untruncated
and count — not to raise the number until it stops looking wrong.** One, then three, both
wrong, by the same mechanism at two different stages.

**And the pinned-state story is wrong, mine and the writer's alike.** We both said the machine
had been sitting in the 2026-09-07 pinned state because nothing restored it. **The host
rebooted on 2026-09-12 at 13:32**, six days after that sitting and one day before this one, so
nothing from 7 September can be standing. Checked rather than assumed:
`cpupower.service` is enabled and ran, but `/etc/default/cpupower-service.conf` is entirely
comments, so it sets no governor, no EPP and no frequency and its script takes no branch;
`/proc/cmdline` carries no `cpufreq.default_governor`; the driver is `amd-pstate-epp` in
`active` mode; and no `tlp`, `tuned`, `power-profiles-daemon` or `auto-cpufreq` is present.
**So `performance` was applied during the current uptime by something the journal does not
record, and this record does not guess at what.** Details in
`raw/environment-ADDENDUM.txt`; `raw/environment.txt` is left byte-identical because it is a
capture, and its readings were never in question — only two interpretive comment lines in it.

**What survives:** every measurement here ran at `governor=performance` on all 16 CPUs, which
is the state a campaign wants, and this sitting changed nothing. The 2026-09-07 retake still
has no restore artifact, so the paper's dropped restore clause stands on its own evidence.
**What is retired:** the inference from a live reading back to a sitting six days earlier. The
2026-09-06 record we were both quoting already contained the sentence that should have stopped
it — *"a reboot resets it"*, written about EPP in the very file whose wording hid it from my
search.

**One correction to the writer's reading of my fourth file.**
`-ctpb-decode-experiments/raw/host-state.txt` is not an incomplete restore. Its own baseline
line reads `epp=performance` **before** the pin, and its restore returned EPP to exactly that,
so it restored what it captured. The EPP residue originates one sitting earlier, in
`-ctpb-rerun`, where EPP was **never captured**, ended at `performance` as a by-product of the
governor change, and was deliberately left rather than set to a guessed value. That record
says so explicitly. So EPP was not "left by an incomplete restore"; it was left by an
**uncaptured knob**, which is a different defect and the more instructive one — a restore can
only be as complete as its capture.
