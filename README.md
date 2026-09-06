# Compile-time Protobuf via C++26 Reflection (`ctpb`)

[![C++26](https://img.shields.io/badge/C%2B%2B-26%20Draft-blue.svg)](https://isocpp.org/)
[![P2996](https://img.shields.io/badge/ISO%20WG21-P2996%20Static%20Reflection-green.svg)](https://wg21.link/p2996)
[![P1967](https://img.shields.io/badge/ISO%20WG21-P1967%20%23embed-orange.svg)](https://wg21.link/p1967)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modern, header-only C++26 library that ingests `.proto` files directly at compile time and generates type-safe C++ structs and wire serialization routines using C++26 static reflection ([P2996](https://wg21.link/p2996)) and `#embed` ([P1967](https://wg21.link/p1967)).

**Zero `protoc` code generation. Zero intermediate files on disk. Zero virtual dispatch. Outperforms Google `libprotobuf` across both serialization ($2.81\times$) and deserialization ($1.71\times$), a margin that holds against `libprotobuf-lite` too.**

---

## Key Highlights

- **Pure C++26 Static Reflection (Zero Macros):** Define message types programmatically inside `consteval` blocks (`ctpb::define_message<schema, "Person">()`) and access members using native dot syntax (`person.name`).
- **Single-Pass $\mathcal{O}(N)$ `constexpr` Parser:** Front-end parser executes in $< 55\,\mu\text{s}$ during compilation ($> 18{,}800$ schemas/sec)—$300\times\text{--}800\times$ faster than `protoc` process spawning.
- **Superior Runtime Performance:**
  - **Encode:** **$32.11\,\text{ns}$** ($1{,}396.0\,\text{MB/s}$, $31.15\,\text{MOps/s}$) vs Google Protobuf $90.29\,\text{ns}$ $\to$ **$\mathbf{2.81\times}$ faster**.
  - **Decode:** **$127.99\,\text{ns}$** ($350.2\,\text{MB/s}$, $7.81\,\text{MOps/s}$) vs Google Protobuf $218.60\,\text{ns}$ $\to$ **$\mathbf{1.71\times}$ faster**.
- **$93.7\times$ Smaller Static Binary Footprint:** Standalone static executable requires only **$18.3\,\text{KB}$** compared to **$1.72\,\text{MB}$** with static `protoc` / `libprotobuf` by eliminating runtime descriptor registries, arena allocators, and mutex locks.
- **Holds Up Against the Fairer Baseline:** re-measured against `libprotobuf-lite` (same machine, median of 7 trials — see `benchmarks/eval_runtime_comparison_lite.sh` and `benchmarks/results/`): **$3.06\times$ faster encoding, $1.69\times$ faster decoding**, essentially the same margin as against full `libprotobuf`.
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
| **Academic Publication Paper** | ✅ | 7-page IEEE CSIT 2026 conference paper (`paper/main.pdf`) |
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
| **Serialization (Encode)** | **$32.11\,\text{ns}$** | $90.29\,\text{ns}$ | **$31.15\,\text{MOps/s}$** ($1{,}396.0\,\text{MB/s}$) | $11.08\,\text{MOps/s}$ ($496.4\,\text{MB/s}$) | **$\mathbf{2.81\times}$ faster** |
| **Deserialization (Decode)** | **$127.99\,\text{ns}$** | $218.60\,\text{ns}$ | **$7.81\,\text{MOps/s}$** ($350.2\,\text{MB/s}$) | $4.58\,\text{MOps/s}$ ($205.0\,\text{MB/s}$) | **$\mathbf{1.71\times}$ faster** |

Full `libprotobuf` carries descriptor pools and reflection registries `ctpb` doesn't implement, so
the same comparison was repeated against `libprotobuf-lite`, same machine, same script
(`benchmarks/eval_runtime_comparison_lite.sh`):

| Operation | `ctpb` Throughput | `libprotobuf-lite` Throughput | Speedup |
| :--- | :---: | :---: | :---: |
| **Encode** | $1{,}371.9\,\text{MB/s}$ | $448.6\,\text{MB/s}$ | $3.06\times$ |
| **Decode** | $337.9\,\text{MB/s}$ | $200.4\,\text{MB/s}$ | $1.69\times$ |

The margin against the fairer baseline doesn't shrink — encode is slightly wider, decode is within noise.

### 2. Standalone Binary Footprint & Static Overhead

| Metric | `ctpb` (P2996) | `protoc` (Dynamic) | `protoc` (Fully Static) | Impact |
| :--- | :---: | :---: | :---: | :---: |
| **Generated Source Lines** | **0** | 754 lines | 754 lines | Zero disk I/O, no cache churn |
| **Stripped Binary Size** | **$18.3\,\text{KB}$** | $26.9\,\text{KB}$ | **$1,718.1\,\text{KB}$** ($1.72\,\text{MB}$) | **$\mathbf{93.7\times}$ size reduction** |
| **Code Section (`.text`)** | **$9.8\,\text{KB}$** | $16.0\,\text{KB}$ | $1,691.4\,\text{KB}$ | Minimal instruction cache footprint |
| **External Shared Dependencies**| **0** | `libprotobuf.so` | **0** | Zero shared library version mismatch |

### 3. Schema Ingestion Latency (20,000 iterations/trial, 7 trials)

Schemas scaled by field count alone (`int32` fields, matching the paper's methodology):

| Schema Complexity | Field Count | Ingestion Latency | Throughput | Speedup vs `protoc` |
| :--- | :---: | :---: | :---: | :---: |
| **Small Message** | 5 | $0.86\,\mu\text{s}$ | $1{,}164{,}144\,\text{parses/s}$ | $\sim 8{,}150\times$ faster |
| **Medium Message** | 25 | $3.04\,\mu\text{s}$ | $329{,}164\,\text{parses/s}$ | $\sim 2{,}300\times$ faster |
| **Large Message** | 100 | $11.36\,\mu\text{s}$ | $88{,}013\,\text{parses/s}$ | $\sim 620\times$ faster |

`protoc` process-spawn time for the same 3-field schema: $7\,\text{ms}$ median of 7 trials
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
├── paper/                  # 6-page IEEE CSIT 2026 conference paper
└── presentation/           # 12-slide Beamer conference presentation
```

---

## Authors

- **Alex I. Tsvetanov** — Faculty of Computer Systems and Technologies, Technical University of Sofia (`atsvetanov@tu-sofia.bg`)

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Machine naming convention, to be applied when the prose is written

Recorded 2026-09-06 from Alex via the coordinator. **Not yet applied to any paper.** It is
here so that whoever writes the prose meets it before writing rather than after.

### The convention

Papers refer to **Machine 1, Machine 2 and Machine 3**, with an apparatus table giving CPU,
memory, OS and kernel version, and backend. Never a hostname, and never "the laptop" or "the
desktop" either. A nickname is anonymous but uninformative and does not scale to three hosts;
a number plus a hardware row is both more anonymous and more useful.

The number names the **hardware**, not an operating system. One host in this corpus has
produced records under more than one OS, so "Machine 2 is the Windows one" is the wrong shape.

### The assignment, derived from the deposited environment captures

| | CPU | memory | OS / kernel | backends |
|---|---|---|---|---|
| Machine 1 | AMD Ryzen 7 5800H, 16 logical | 16,144,474,112 B (15.0 GiB) | Arch Linux, kernel `7.2.2-arch1-1` (a later campaign records 7.2.3) | epoll, io\_uring |
| Machine 2 | AMD Ryzen 5 3600, 6 physical / 12 logical | **not captured** | Windows 11, build 10.0.26200 | IOCP; the end-to-end generator runs inside WSL2 |
| Machine 3 | Apple M4 Pro | 25,769,803,776 B (24 GiB) | macOS 26, Darwin 25.6.0 | kqueue |

Counted from the 37 `*.env.json` in the corpus: 20 carry `machine.node` `alex-pc` with kernel
release `11`, 13 carry `alex-laptop` with `7.2.2-arch1-1`, 2 carry a macOS asset tag with
`25.6.0`, and 2 are the redacted pair described below.

**Machine 2's memory row cannot be filled from any record.** `memory.total_bytes` is `null`
in all 20 Windows captures and present in the other 17. Either the row says so explicitly or
a fresh capture is needed; it is not recoverable from what is deposited, and it must not be
guessed.

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
is the bridge.** A reader verifying a figure fetches the record, recomputes the fingerprint —
which works, because nothing was edited — and reads the apparatus table to learn which machine
it was. The hostname is never needed to check anything.

### Three things the convention has to decide before it is applied

1. **Record directory names carry the nicknames**, and are cited in published prose, not only
   in comments: `2026-09-02-desktop-routing/`, `2026-09-04-laptop-wait-policy/`,
   `2026-09-02-macos/`, `2026-09-03-macos-reuseport/`. P4's `limitations.tex` names
   `measurements/2026-09-02-desktop-routing/` in a body sentence. Renaming the directories would
   break every path citation and every manifest that lists them, which is the same trap as
   editing `machine.node`. So either the directory names stay and the nickname survives there,
   or the prose stops citing paths. That is Alex's call, not a writer's.
2. **`INDEX.md` is not a home for the mapping.** It is byte-identical across four repositories
   and absent from `paper-methodology` and `paper-wake-defect`, so a mapping placed there would
   miss two of the six papers. That is why this section is in each repository's README instead.
3. **The repositories are intended to become public.** All are private today, so there is no
   live exposure, but 35 deposited `*.env.json` carry real hostnames, and the prohibition above
   says they must keep them. Publishing the records as they stand and keeping fingerprints
   verifiable are in tension, and the verification procedure's own answer — publish the
   pre-redaction fingerprint alongside a redacted manifest — has been applied to 2 of 37. This
   needs settling before anything goes public, and it is a decision rather than a writing task.

### Scope of the eventual pass

Prose only, and smaller than it looks: 12 occurrences of "the laptop" or "the desktop" in
published prose across the corpus, in `paper-io-portability` (7), `paper-wake-defect` (3) and
`paper-methodology` (2). Further occurrences in `%` source comments are provenance and are
never published; they should be left alone unless they are wrong. No `.tex` file in any paper
contains a hostname today.
