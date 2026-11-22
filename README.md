# Compile-time Protobuf via C++26 Reflection (`ctpb`)

[![C++26](https://img.shields.io/badge/C%2B%2B-26%20Draft-blue.svg)](https://isocpp.org/)
[![P2996](https://img.shields.io/badge/ISO%20WG21-P2996%20Static%20Reflection-green.svg)](https://wg21.link/p2996)
[![P1967](https://img.shields.io/badge/ISO%20WG21-P1967%20%23embed-orange.svg)](https://wg21.link/p1967)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modern, header-only C++26 library that ingests `.proto` files directly at compile time and generates type-safe C++ structs and wire serialization routines using C++26 static reflection ([P2996](https://wg21.link/p2996)) and `#embed` ([P1967](https://wg21.link/p1967)).

**Zero `protoc` code generation. Zero intermediate files on disk. Zero virtual dispatch. Outperforms Google `libprotobuf` across both serialization (2.81×) and deserialization (1.71×), a margin that holds against `libprotobuf-lite` too.**

---

## Key Highlights

- **Pure C++26 Static Reflection (Zero Macros):** Define message types programmatically inside `consteval` blocks (`ctpb::define_message<schema, "Person">()`) and access members using native dot syntax (`person.name`).
- **Single-Pass O(N) `constexpr` Parser:** Front-end parser executes in < 55 μs during compilation (> 18,800 schemas/sec), 300× to 800× faster than `protoc` process spawning.
- **Superior Runtime Performance:**
  - **Encode:** **32.11 ns** (1,396.0 MB/s, 31.15 MOps/s) vs Google Protobuf 90.29 ns → **2.81× faster**.
  - **Decode:** **127.99 ns** (350.2 MB/s, 7.81 MOps/s) vs Google Protobuf 218.60 ns → **1.71× faster**.
- **93.7× Smaller Static Binary Footprint:** Standalone static executable requires only **18.3 KB** compared to **1.72 MB** with static `protoc` / `libprotobuf` by eliminating runtime descriptor registries, arena allocators, and mutex locks.
- **Holds Up Against the Fairer Baseline:** re-measured against `libprotobuf-lite` (same machine, median of 7 trials, see `benchmarks/eval_runtime_comparison_lite.sh` and `benchmarks/results/`): **3.06× faster encoding, 1.69× faster decoding**, essentially the same margin as against full `libprotobuf`.
- **Reference & Transport Interoperability:** bidirectional wire-format fidelity with official `libprotobuf` (3.21.12), covering scalars, strings, nested messages, enums, `bytes` fields, and packed repeated scalar fields, plus `grpc::SerializationTraits<T>` transport integration.

---

## Project Status

| Component | Status | Description |
|---|:---:|---|
| **Zero-Copy `constexpr` Lexer** | ✅ | Identifiers, keywords, numbers, hex, comments, line/column diagnostics |
| **Recursive-Descent `constexpr` Parser** | ✅ | Full proto3 grammar: messages, enums, repeated, maps, oneofs, reserved tags |
| **P2996 Static Reflection Engine** | ✅ | Pure zero-macro `define_message` spliced structs with dot syntax |
| **Two-Pass Raw-Pointer Wire Engine** | ✅ | Exact compile-time pre-allocation (`byte_size()`) + non-allocating direct pointer encoding |
| **Comprehensive Test Suite (9 Suites)** | ✅ | Lexer, parser, wire encoding, roundtrips, nested messages, maps, oneofs, wire-format edge cases, Google interop |
| **Microbenchmark Suite** | ✅ | Ingestion latency, binary footprint evaluation, head-to-head runtime speedup vs `libprotobuf` and `libprotobuf-lite` |
| **Academic Publication Paper** | ✅ | 4-page TELECOM 2026 conference paper (`paper/main.pdf`), plus the 7-page extended version (`full_paper/main.pdf`) |
| **Conference Beamer Presentation** | ✅ | 12-slide 10-minute presentation (`presentation/presentation.pdf`) |

---

## Architecture

```
┌────────────────────────────────────────────────────────┐
│ Stage 1: Schema Ingestion                              │
│ #embed "addressbook.proto" / ctpb::fixed_string        │
└──────────────────────────┬─────────────────────────────┘
                           │ Raw string view
                           ▼
┌────────────────────────────────────────────────────────┐
│ Stage 2: Constexpr Front-End Parsing                   │
│ Single-pass recursive-descent AST (ctpb::ast::file)    │
└──────────────────────────┬─────────────────────────────┘
                           │ Constexpr AST
                           ▼
┌────────────────────────────────────────────────────────┐
│ Stage 3: C++26 Static Reflection & Splice (P2996)      │
│ consteval { ctpb::define_message<Schema, "Person">(); }│
│ Produces native aggregate struct Person                │
└──────────────────────────┬─────────────────────────────┘
                           │ Native C++ Type
                           ▼
┌────────────────────────────────────────────────────────┐
│ Stage 4: High-Performance Binary Wire Engine           │
│ Pre-sized single-buffer encode() & zero-dispatch decode│
└────────────────────────────────────────────────────────┘
```

---

## Quick Start / User API

### 1. Pure C++26 Static Reflection (Zero Macros)

```cpp
#include <ctpb/ctpb.hpp>
#include <print>

// Raw Protobuf schema embedded at compile time:
inline constexpr ctpb::fixed_string addressbook_proto = R"(
    syntax = "proto3";
    package tutorial;

    message Person {
      string name = 1;
      int32 id = 2;
      string email = 3;
      repeated string tags = 4;
    }
)";

// 1. Synthesize message struct via P2996 static reflection (Zero Macros!)
consteval { ctpb::define_message<addressbook_proto, "Person">(); }
using Person = ctpb::message_t<addressbook_proto, "Person">;

int main() {
    // 2. Direct dot-syntax field manipulation
    Person p{};
    p.name = "Alexander Tsvetanov";
    p.id = 42;
    p.email = "alexander.tsvetanov@tu-sofia.bg";
    p.tags.push_back("author");
    p.tags.push_back("systems-programming");

    // 3. Ultra-fast single-allocation serialization (2.81x faster than Google libprotobuf!)
    std::vector<std::byte> wire = ctpb::encode(p);

    // Optional zero-allocation serialization into existing buffer:
    // std::byte buffer[256];
    // std::size_t written = ctpb::encode_to(p, buffer);

    // 4. Strongly-typed deserialization (1.71x faster than Google libprotobuf!)
    auto decoded = ctpb::decode<Person>(wire);
    if (decoded) {
        std::println("Name:  {}", decoded->name);
        std::println("ID:    {}", decoded->id);
        std::println("Email: {}", decoded->email);
        std::println("Tags:  {}", decoded->tags.size());
    }
    return 0;
}
```

---

## Empirical Benchmarks

All microbenchmarks were executed inside the project's own `sleepingeight/clang-cpp-refl` Docker
image, on an AMD Ryzen 5 3600 (6 cores, x86_64, Ubuntu 24.04), Clang 21.0
(`-std=c++26 -freflection -O3`), Protobuf 3.21.12. Every number below is the median of 7 trials;
raw output for all three tables is checked in under `benchmarks/results/`, and each script that
produced it is in `benchmarks/`.

### 1. Runtime Serialization & Deserialization (1,000,000 iterations/trial, 7 trials)

Representative 3-field `Person` message, 47-byte wire payload:

| Operation | `ctpb` Latency | Google `libprotobuf` | `ctpb` Throughput | Google Throughput | Speedup |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Serialization (Encode)** | **32.11 ns** | 90.29 ns | **31.15 MOps/s** (1,396.0 MB/s) | 11.08 MOps/s (496.4 MB/s) | **2.81× faster** |
| **Deserialization (Decode)** | **127.99 ns** | 218.60 ns | **7.81 MOps/s** (350.2 MB/s) | 4.58 MOps/s (205.0 MB/s) | **1.71× faster** |

Full `libprotobuf` carries descriptor pools and reflection registries `ctpb` doesn't implement, so
the same comparison was repeated against `libprotobuf-lite`, same machine, same script
(`benchmarks/eval_runtime_comparison_lite.sh`):

| Operation | `ctpb` Throughput | `libprotobuf-lite` Throughput | Speedup |
| :--- | :---: | :---: | :---: |
| **Encode** | 1,371.9 MB/s | 448.6 MB/s | 3.06× |
| **Decode** | 337.9 MB/s | 200.4 MB/s | 1.69× |

The margin against the fairer baseline doesn't shrink: encode is slightly wider, decode is within noise.

### 2. Standalone Binary Footprint & Static Overhead

| Metric | `ctpb` (P2996) | `protoc` (Dynamic) | `protoc` (Fully Static) | Impact |
| :--- | :---: | :---: | :---: | :---: |
| **Generated Source Lines** | **0** | 754 lines | 754 lines | Zero disk I/O, no cache churn |
| **Stripped Binary Size** | **18.3 KB** | 26.9 KB | **1,718.1 KB** (1.72 MB) | **93.7× size reduction** |
| **Code Section (`.text`)** | **9.8 KB** | 16.0 KB | 1,691.4 KB | Minimal instruction cache footprint |
| **External Shared Dependencies**| **0** | `libprotobuf.so` | **0** | Zero shared library version mismatch |

### 3. Schema Ingestion Latency (20,000 iterations/trial, 7 trials)

Schemas scaled by field count alone (`int32` fields, matching the paper's methodology):

| Schema Complexity | Field Count | Ingestion Latency | Throughput | Speedup vs `protoc` |
| :--- | :---: | :---: | :---: | :---: |
| **Small Message** | 5 | 0.86 μs | 1,164,144 parses/s | ~8,150× faster |
| **Medium Message** | 25 | 3.04 μs | 329,164 parses/s | ~2,300× faster |
| **Large Message** | 100 | 11.36 μs | 88,013 parses/s | ~620× faster |

`protoc` process-spawn time for the same 3-field schema: 7 ms median of 7 trials
(`benchmarks/results/protoc_spawn_time_docker.txt`).

---

## Building and Testing

### Prerequisites
- Clang (19+ / 21+ with `-freflection`) or GCC (14+) with `-std=c++26`
- CMake 3.25+
- (Optional for Paper & Presentation) TeX Live / MacTeX with `pdflatex`, `bibtex`, `pgfplots`, `beamer`

### Build and Run Tests
```bash
# Configure with test suite and benchmarks enabled
cmake -S . -B build -DCMAKE_CXX_STANDARD=26 -DCTPB_BUILD_TESTS=ON -DCTPB_BUILD_BENCHMARKS=ON

# Compile everything
cmake --build build -j

# Execute all 8 unit tests and interoperability suites
ctest --test-dir build --output-on-failure
```

### Run Benchmarks
```bash
# Run runtime head-to-head benchmark vs libprotobuf
bash benchmarks/eval_runtime_comparison.sh

# Run binary size evaluation
bash benchmarks/eval_binary_size.sh
```

### Build Paper and Presentation
```bash
# Compile IEEE conference paper PDF
latexmk -pdf -outdir=paper paper/main.tex
# Output: paper/main.pdf

# Compile Beamer conference presentation PDF
latexmk -pdf -outdir=presentation presentation/presentation.tex
# Output: presentation/presentation.pdf
```

---

## Directory Structure

```
.
├── include/ctpb/           # Core header-only C++26 library
│   ├── ast.hpp             # Immutable constexpr AST nodes
│   ├── lexer.hpp           # Zero-allocation constexpr tokenizer
│   ├── parser.hpp          # Recursive-descent constexpr proto3 parser
│   ├── wire.hpp            # Binary wire format engine & raw-pointer serializers
│   ├── embed.hpp           # P1967 #embed wrapper
│   ├── reflection.hpp      # P2996 reflection drivers & define_message hooks
│   └── ctpb.hpp            # Umbrella include header
├── tests/                  # CTest test suite
│   ├── test_lexer.cpp      # Tokenizer verification
│   ├── test_parser.cpp     # AST & proto3 grammar validation
│   ├── test_encode_decode.cpp # Basic wire serialization tests
│   ├── test_roundtrip.cpp  # End-to-end roundtrip tests
│   ├── test_nested.cpp     # Hierarchical nested message resolution
│   ├── test_oneof.cpp      # proto3 oneof union serialization
│   ├── test_maps.cpp       # proto3 map<K,V> wire testing
│   └── test_interop_protobuf.cpp # Bidirectional Google protoc & gRPC parity
├── benchmarks/             # Microbenchmark evaluation scripts
│   ├── eval_runtime_comparison.sh # Head-to-head runtime benchmark
│   └── eval_binary_size.sh        # Binary footprint benchmark
├── examples/               # Example programs (person.cpp)
├── paper/                  # 4-page TELECOM 2026 conference paper
├── full_paper/             # 7-page extended version of the paper
└── presentation/           # 12-slide Beamer conference presentation
```

---

## Authors

- **Alex I. Tsvetanov**, Faculty of Computer Systems and Technologies, Technical University of Sofia (`atsvetanov@tu-sofia.bg`)

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Machine naming convention, to be applied when the prose is written

Recorded 2026-09-06 from Alex via the coordinator, and revised the same day as the open
questions were settled. **Not yet applied to any paper.** It is here so that whoever writes
the prose meets it before writing rather than after.

### The convention

Papers refer to **Machine 1, Machine 2 and Machine 3**, with an apparatus table giving CPU,
memory, OS and kernel version, and backend. Never a hostname, and never "the laptop" or "the
desktop" either. A nickname is anonymous but uninformative and does not scale to three hosts;
a number plus a hardware row is both more anonymous and more useful.

The number names the **hardware**, and the operating system is a column rather than part of
the identity. A machine may therefore have more than one row. Machine 2 has produced records
under Windows natively, under WSL2 as the end-to-end generator host, and under Ubuntu in
Docker for an earlier benchmark table, so "Machine 2 is the Windows one" would leave rows
homeless.

### The assignment

| | CPU | memory | OS / kernel | backends |
|---|---|---|---|---|
| Machine 1 | AMD Ryzen 7 5800H, 16 logical | 16,144,474,112 B (15.04 GiB) | Arch Linux, kernel `7.2.2-arch1-1`; a later campaign records 7.2.3 | epoll, io\_uring |
| Machine 2 | AMD Ryzen 5 3600 6-Core, 6 physical / 12 logical | 17,111,228,416 B (15.94 GiB) | Microsoft Windows 11 Pro N, 10.0.26200 | IOCP; WSL2 hosts the end-to-end generator; an earlier campaign ran Ubuntu 24.04 in Docker |
| Machine 3 | Apple M4 Pro | 25,769,803,776 B (24.00 GiB) | macOS 26, Darwin `25.6.0` | kqueue |

Machine 1 and Machine 3's rows come from the deposited environment captures. Counted across
the 37 `*.env.json` in the corpus: 20 carry `machine.node` for Machine 2 with kernel release
`11`, 13 carry Machine 1 with `7.2.2-arch1-1`, 2 carry Machine 3 with `25.6.0`, and 2 are the
redacted pair described below.

**Machine 2's memory is not from a record.** `memory.total_bytes` is `null` in all 20 of its
captures, so the figure above is a fresh reading taken on 2026-09-06, outside any campaign. A
reader tracing it will not find it in an `env.json` and should know why. Its CPU and OS strings
were confirmed against the records at the same time and do match them.

### What must not happen

**Do not edit `machine.node`, or any hostname, anywhere under `measurements/`.**

`machine.node` is one of the fields the environment fingerprint hashes. The corpus has already
run this experiment. `paper-io-portability/measurements/2026-09-04-laptop-wait-policy/` had
`machine.node` replaced with `"the laptop"` in its two `*.env.json` before publication, and
those two are exactly the manifests that reproduce their stored fingerprint under no key set.
One directory had the convention applied to its records and it is the only place in the archive
where verification is broken. Applying it to the remaining 35 would break every fingerprint to
gain an anonymity the prose already has.

The layers separate cleanly: **prose is anonymous, records are authentic, the apparatus table
is the bridge.** A reader verifying a figure fetches the record, recomputes the fingerprint,
which works because nothing was edited, and reads the apparatus table to learn which machine
it was. The hostname is never needed to check anything.

### Settled questions, so they are not reopened

**Record directory names keep their names, and the prose keeps citing them.** A record path is
an identifier, not prose: `measurements/2026-09-02-desktop-routing/` in a body sentence is
doing what a DOI does, and nobody anonymises a DOI to match a naming convention. Renaming would
break every path citation and every manifest that lists those directories, which is the
`machine.node` trap one level up. Note also that "desktop" there is a nickname and not a
hostname. The hostnames appear in no `.tex` at all. The convention governs how a sentence
refers to a machine, not the identifiers a sentence cites. Machine 2 in the prose, the real
path in the citation.

**`INDEX.md` is not the home for the mapping**, which is why this section is in each
repository's README. It is byte-identical across four repositories and absent from
`paper-methodology` and `paper-wake-defect`, and those two are among the three that carry the
nicknames a pass would have to fix.

### One question that resolves at publication, with its options

The repositories are intended to become public once the development and the texts are done.
All are private today, so there is no live exposure, and publication is months out: the
framework comes first, then re-measurement, then the papers. It is recorded rather than
resolved because it needs no answer yet.

The tension is real. 35 deposited `*.env.json` carry real hostnames, and the section above
forbids editing them because editing breaks the fingerprint that verifies them. Two options
are legitimate:

1. **Publish the records unedited.** Every fingerprint stays verifiable and a hostname appears
   in an artefact, which is ordinary in published deposits.
2. **Redact and ship the pre-redaction fingerprint** beside each altered manifest, which is
   what section 4 of the verification procedure prescribes. It has been applied to 2 of 37.

**Hand-editing hostnames without the companion fingerprint is the one option that is strictly
wrong**, and it is the one already applied to the two manifests in
`2026-09-04-laptop-wait-policy/`. Whichever option is chosen, those two need finishing.

### Scope of the eventual pass

Prose only, and smaller than it looks: 12 occurrences of "the laptop" or "the desktop" in
published prose across the corpus, in `paper-io-portability` (7), `paper-wake-defect` (3) and
`paper-methodology` (2). Further occurrences in `%` source comments are provenance, are never
published, and must be left alone: rewriting them to a prose convention would damage the thing
they exist for. No `.tex` file in any paper contains a hostname today.

## Which sections are provisional, and what would change them

Recorded 2026-09-06, before the framework re-measurement, so that a later pass knows what to
re-check rather than re-deriving it. This is a forecast of exposure, not a defect list: nothing
below is known to be wrong today.

One pending framework change was checked against this corpus and does not reach it. The harness
identity fix, which stops a mixed-implementation campaign being labelled Coroute, is
**prospective only**: no deposited record in any of the seven repositories names a non-Coroute
implementation, and every one carries a Coroute build commit. It gates cross-framework campaigns
that have not been run, and invalidates nothing already published.

**The benchmark section is provisional, but not because of the server framework's harness.**
That reason was checked and does not hold: `method/campaign.sh` and
`method/eval_runtime_prealloc.sh` in the 2026-09-06 record reference no part of that harness, no
adapter, no campaign driver and no `loadgen`. This library is measured by its own scripts in its
own Docker image, and a re-measurement of the web framework does not reach it.

**What does expose it** is in its own record: `gates: none`. Those scripts have no validity
gates, so nothing was accepted or refused, and every figure is one sitting on one host. If the
gating discipline the methodology paper describes is ever applied here, the campaign is re-taken
under a gated driver rather than re-labelled.

**Also worth knowing:** the decode figure depends on a per-iteration allocation fix that lives in
the record's `method/` as a diff against the committed evaluation script. Until that correction
is merged into the committed harness, re-running the repository's own script reproduces the old
asymmetry rather than the published number.

### Audit, 2026-09-07: what these manuscripts owe

**Claim.** Compile-time Protobuf without a code generator: schema parsing under 11.4\,µs, encode
about 5.6× the reference, decode at parity, and a 93.70× stripped-binary reduction.

**Record.** `measurements/2026-09-07-laptop-p5-retake`, which supersedes the single-host set.

**Is the code still the code that was measured? Yes, and it is the one tree where that answer
changed today.** UTF-8 validation landed in the merge at `dfed528`, and the retake followed it:
the retake's record commit is `b2bd0b4` at 03:02, and `include/ctpb/wire.hpp` carries the
validation on `main` now. Both manuscripts cite only the retake: six references, all to
`2026-09-07-laptop-p5-*`, and a scan for every superseded single-host figure returns nothing.

**One question the audit raises and cannot answer.** The stripped-binary footprint is
**byte-identical before and after validation landed**: 18,776 / 27,504 / 1,759,368 in the single-host
`raw/binary_size.txt` and the same three numbers in the retake's. The retake did re-measure, it
has its own `raw/binary_size.txt`, so this is not a transcription. But validation is code, and
code that is linked adds bytes.

Either the validator is not reachable from the footprint binary, which measures a 3-field
`Person` and may never exercise a `string` field's validation path, or it is header-only and
optimised away there. **Both are plausible and the record does not say which.** That matters for
the footprint claim only if a reader takes 18,776 bytes to include the validation the decode
figures now depend on. It is one sentence in the record either way, and it belongs to whoever took
the measurement rather than to this audit.
