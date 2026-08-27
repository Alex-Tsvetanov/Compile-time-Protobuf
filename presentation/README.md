### Slide Deck Overview & 10-Minute Timing Guide

| Slide | Title & Content | Target Time |
| :---: | :--- | :---: |
| **1** | **Title Slide:** *Zero-Codegen Protocol Buffers via C++26 Static Reflection and Compile-Time Metaprogramming* | 0:00 – 0:45 |
| **2** | **Motivation & Problem with `protoc`:** External toolchain dependencies, build DAG friction, disk I/O, ABI version skew, and 750+ LOC boilerplate per message. | 0:45 – 1:30 |
| **3** | **C++26 Enablers:** P1967 `#embed` (zero-copy file inclusion) and P2996 Static Reflection (`std::meta::info`, `define_aggregate`). | 1:30 – 2:15 |
| **4** | **Architecture: Three-Stage Compile-Time Pipeline:** TikZ diagram covering Resource Embedding $\to$ `constexpr` Parse $\to$ P2996 Type Synthesis $\to$ Wire Engine. | 2:15 – 3:15 |
| **5** | **Zero-Allocation `constexpr` Parser:** LL(1) recursive-descent parser, zero heap allocations (string views), full proto3 grammar coverage, and compile-time diagnostics. | 3:15 – 4:00 |
| **6** | **P2996 Programmatic Type Synthesis:** How `define_aggregate` populates standard C++ members, mapping proto3 types to `std::int32_t`, `std::string`, `std::vector`, `std::variant`, `std::unordered_map`. | 4:00 – 4:50 |
| **7** | **Developer Experience (Clean 2-Column Code):** Zero-macro declaration (`consteval { ctpb::define_message<...>(); }`) + direct dot-syntax member access (`p.name = "Alex"`) and reflective serialization. | 4:50 – 5:50 |
| **8** | **High-Performance Wire Format Engine:** Non-allocating byte streams, Varint 7-bit packing, single-instruction Zigzag arithmetic, and bounds-checked `std::span` reader. | 5:50 – 6:40 |
| **9** | **Experimental Evaluation (Latency & Throughput):** Compile-time latency ($< 55\,\mu\text{s}$, **$300\times\text{--}800\times$ faster** than `protoc`) and runtime deserialization (**$84.81\,\text{MB/s}$**, $1.19\,\text{MOps/s}$). | 6:40 – 7:40 |
| **10** | **Binary Footprint Evaluation:** Side-by-side comparison table showing **$91\times$ static binary reduction** ($19\,\text{KB}$ vs $1.75\,\text{MB}$), 0 generated source files, and 0 external dynamic dependencies. | 7:40 – 8:40 |
| **11** | **Ecosystem Interoperability & gRPC Parity:** 100% bidirectional wire parity with Google Protobuf and drop-in integration via `grpc::SerializationTraits<T>`. | 8:40 – 9:20 |
| **12** | **Conclusion, Future Directions & Q&A:** Summary of contributions, future roadmap (Well-Known Types, async gRPC stubs), and contact info. | 9:20 – 10:00 |
