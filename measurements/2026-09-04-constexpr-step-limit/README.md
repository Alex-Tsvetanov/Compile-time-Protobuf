# What bounds a large schema is the constexpr STEP limit, not memory — and it binds at 301 fields

4 September 2026. Run in the project's own `ctpb-p2996` image
(clang 21.0.0git, bloomberg/clang-p2996 @ `2ea0a79f`), the same toolchain as the paper's benchmarks.

## Why this was run

The paper listed, as one of three limitations, that "monolithic schemas containing hundreds of
messages demand correspondingly generous compiler constant-evaluation **memory** limits, since the
entire AST is materialized during translation." No measurement stood behind it. Nothing in this
project has ever measured compilation: a grep of every script under `benchmarks/` for
`/usr/bin/time`, `-ftime-report` or `-fmem-report` returns nothing, and both compile-time benchmarks
(`bench_compile_time.cpp` and `eval_compile_time_latency.sh`) time `ctpb::parser` with
`std::chrono::steady_clock` in an `-O3` runtime loop. `steady_clock` cannot be read during constant
evaluation, so by construction those measure the parser executing as native code.

## Method

For each field count `N`, one translation unit is compiled that forces the parse through constant
evaluation and cannot be optimised away:

```cpp
consteval std::size_t field_count() {
    ctpb::parser p{schema};                 // schema is a constexpr string_view of N int32 fields
    auto f = p.parse_file();
    return f.messages[0].fields.size();
}
static_assert(field_count() == N);
```

A `static_assert` on a `consteval` call is the whole instrument: it succeeds only if the compiler
actually completed the parse during translation. Flags are the project's own:
`-O3 -std=c++26 -freflection -stdlib=libstdc++`. The threshold is then bisected.

## Result

| fields | outcome |
|---|---|
| 1, 5, 25, 100, 250, 281, 296, **300** | compiles |
| **301**, 302, 304, 312, 375, 500, 1000 | `error: constexpr evaluation hit maximum step limit` |

**The largest single message that compiles at Clang's default limit is 300 `int32` fields. 301 fails.**
The failure is the step limit, not memory: the diagnostic names it, and passing
`-fconstexpr-steps=1048576` explicitly reproduces the failure, which is consistent with that being the
default. Raising it to `4194304` (four times the default) compiles **both** a 500-field and a
1,000-field message. **The project sets no `-fconstexpr-steps` anywhere** — not in `CMakeLists.txt`,
not in any benchmark script.

## What this establishes, and what it does not

**Establishes.** The binding constraint on schema size is the constant-evaluation *step* limit, and it
binds at 301 fields in a *single message* — far below "hundreds of messages". The remedy is one
compiler flag, and the project does not set it or document it.

**Does not establish.** Anything about memory. GNU `time` is absent from the image, so peak compiler
RSS was not captured; memory may still grow as the limitation claimed, but it is not what fails first
and no number here bounds it. Nor anything about wall-clock cost: the image is `linux/amd64` running
under emulation on an arm64 host, so compile *times* from this run are not comparable to the paper's
Ryzen figures. **The step limit is unaffected by either** — it is a deterministic instruction count,
identical on any host running this compiler on this code, which is why the threshold is reported and
the timings are not.

**Nesting and message count were not varied.** Only field count within one message. A schema of many
small messages may behave differently and is untested.
