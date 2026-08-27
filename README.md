# Compile-time Protobuf via C++26 Reflection (`ctpb`)

[![C++26](https://img.shields.io/badge/C%2B%2B-26%20Draft-blue.svg)](https://isocpp.org/)
[![P2996](https://img.shields.io/badge/ISO%20WG21-P2996%20Static%20Reflection-green.svg)](https://wg21.link/p2996)
[![P1967](https://img.shields.io/badge/ISO%20WG21-P1967%20%23embed-orange.svg)](https://wg21.link/p1967)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A modern, header-only C++26 library that ingests `.proto` files directly at compile time and generates type-safe C++ structs and wire serialization routines using C++26 static reflection ([P2996](https://wg21.link/p2996)) and `#embed` ([P1967](https://wg21.link/p1967)).

**Zero `protoc` code generation. Zero intermediate files on disk. Zero virtual dispatch. Outperforms Google `libprotobuf` across both serialization ($2.21\times$) and deserialization ($2.26\times$).**

---

## Key Highlights

- **Pure C++26 Static Reflection (Zero Macros):** Define message types programmatically inside `consteval` blocks (`ctpb::define_message<schema, "Person">()`) and access members using native dot syntax (`person.name`).
- **Single-Pass $\mathcal{O}(N)$ `constexpr` Parser:** Front-end parser executes in $< 55\,\mu\text{s}$ during compilation ($> 18{,}800$ schemas/sec)—$300\times\text{--}800\times$ faster than `protoc` process spawning.
- **Superior Runtime Performance:**
  - **Encode:** **$27.51\,\text{ns}$** ($1{,}629.6\,\text{MB/s}$, $36.36\,\text{MOps/s}$) vs Google Protobuf $60.86\,\text{ns}$ $\to$ **$\mathbf{2.21\times}$ faster** ($+54.8\%$).
  - **Decode:** **$57.82\,\text{ns}$** ($775.2\,\text{MB/s}$, $17.29\,\text{MOps/s}$) vs Google Protobuf $130.83\,\text{ns}$ $\to$ **$\mathbf{2.26\times}$ faster** ($+55.8\%$).
- **$91\times$ Smaller Static Binary Footprint:** Standalone static executable requires only **$19\,\text{KB}$** compared to **$1.75\,\text{MB}$** with static `protoc` / `libprotobuf` by eliminating runtime descriptor registries, arena allocators, and mutex locks.
- **Reference & Transport Interoperability:** 100% bidirectional wire-format fidelity with official `libprotobuf` (v34.1) and seamless `grpc::SerializationTraits<T>` transport integration.

---

## Project Status

| Component | Status | Description |
|---|:---:|---|
| **Zero-Copy `constexpr` Lexer** | ✅ | Identifiers, keywords, numbers, hex, comments, line/column diagnostics |
| **Recursive-Descent `constexpr` Parser** | ✅ | Full proto3 grammar: messages, enums, repeated, maps, oneofs, reserved tags |
| **P2996 Static Reflection Engine** | ✅ | Pure zero-macro `define_message` spliced structs with dot syntax |
| **Two-Pass Raw-Pointer Wire Engine** | ✅ | Exact compile-time pre-allocation (`byte_size()`) + non-allocating direct pointer encoding |
| **Comprehensive Test Suite (8 Suites)** | ✅ | Lexer, parser, wire encoding, roundtrips, nested messages, maps, oneofs, Google interop |
| **Microbenchmark Suite** | ✅ | Ingestion latency, binary footprint evaluation, head-to-head runtime speedup vs `libprotobuf` |
| **Academic Publication Paper** | ✅ | 6-page IEEE CSIT 2026 conference paper (`paper/main.pdf`) |
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

    // 3. Ultra-fast single-allocation serialization (2.21x faster than Google libprotobuf!)
    std::vector<std::byte> wire = ctpb::encode(p);

    // Optional zero-allocation serialization into existing buffer:
    // std::byte buffer[256];
    // std::size_t written = ctpb::encode_to(p, buffer);

    // 4. Strongly-typed deserialization (2.26x faster than Google libprotobuf!)
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

All microbenchmarks were executed under Clang 21.0 (`-std=c++26 -freflection -O3`) on representative 3-field `Person` messages:

### 1. Runtime Serialization & Deserialization (1,000,000 Iterations)

| Operation | `ctpb` Latency | Google `libprotobuf` | `ctpb` Throughput | Google Throughput | Speedup |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Serialization (Encode)** | **$27.51\,\text{ns}$** | $60.86\,\text{ns}$ | **$36.36\,\text{MOps/s}$** ($1{,}629.6\,\text{MB/s}$) | $16.43\,\text{MOps/s}$ ($736.5\,\text{MB/s}$) | **$\mathbf{2.21\times}$ faster** ($+54.8\%$) |
| **Deserialization (Decode)** | **$57.82\,\text{ns}$** | $130.83\,\text{ns}$ | **$17.29\,\text{MOps/s}$** ($775.2\,\text{MB/s}$) | $7.64\,\text{MOps/s}$ ($342.6\,\text{MB/s}$) | **$\mathbf{2.26\times}$ faster** ($+55.8\%$) |

### 2. Standalone Binary Footprint & Static Overhead

| Metric | `ctpb` (P2996) | `protoc` (Dynamic) | `protoc` (Fully Static) | Impact |
| :--- | :---: | :---: | :---: | :---: |
| **Generated Source Lines** | **0** | 754 lines | 754 lines | Zero disk I/O, no cache churn |
| **Stripped Binary Size** | **$19\,\text{KB}$** | $27\,\text{KB}$ | **$1,752\,\text{KB}$** ($1.75\,\text{MB}$) | **$\mathbf{91\times}$ size reduction** |
| **Code Section (`.text`)** | **$10.1\,\text{KB}$** | $16.4\,\text{KB}$ | $1,731.9\,\text{KB}$ | Minimal instruction cache footprint |
| **External Shared Dependencies**| **0** | `libprotobuf.so` | **0** | Zero shared library version mismatch |

### 3. Schema Ingestion Latency

| Schema Complexity | Field Count | Ingestion Latency | Throughput | Speedup vs `protoc` |
| :--- | :---: | :---: | :---: | :---: |
| **Small Message** | 5 | $18.4\,\mu\text{s}$ | $54{,}340\,\text{parses/s}$ | $\sim 800\times$ faster |
| **Medium Message** | 25 | $53.1\,\mu\text{s}$ | $18{,}814\,\text{parses/s}$ | $\sim 300\times$ faster |
| **Large Message** | 100 | $194.2\,\mu\text{s}$ | $5{,}149\,\text{parses/s}$ | $\sim 100\times$ faster |

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
