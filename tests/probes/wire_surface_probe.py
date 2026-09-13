#!/usr/bin/env python3
"""Map which proto3 field kinds the reflection path gets right on the wire.

For each case: protoc encodes a text-format value into conformant bytes; ctpb
decodes those bytes; ctpb encodes its own value; protoc decodes ctpb's bytes.
A case is only correct if both directions agree with protoc. Reflection path
only -- each probe #errors if the gate is shut.
"""
import pathlib, subprocess

W = pathlib.Path("/tmp/wire_surface"); W.mkdir(exist_ok=True)
INNER = "message Inner { int32 a = 1; string b = 2; }"
ENUM = "enum Color { RED = 0; GREEN = 1; BLUE = 2; }"

CASES = [
    dict(name="nested message", decl=f"{INNER}\nmessage Outer {{ int32 id = 1; Inner inner = 2; }}",
         nested=True, text="id: 1 inner { a: 7 }", setv="o.inner = 7;",
         report='std::printf("  decoded inner=%d\\n",(int)o.inner);'),
    dict(name="repeated nested message", decl=f"{INNER}\nmessage Outer {{ int32 id = 1; repeated Inner items = 2; }}",
         nested=True, text="id: 1 items { a: 7 } items { a: 8 }", setv="o.items = {7, 8};",
         report='std::printf("  decoded items.size=%zu\\n", o.items.size());'),
    dict(name="enum", decl=f"{ENUM}\nmessage Outer {{ int32 id = 1; Color c = 2; }}",
         text="id: 1 c: GREEN", setv="o.c = 1;", report='std::printf("  decoded c=%d\\n",(int)o.c);'),
    dict(name="repeated enum", decl=f"{ENUM}\nmessage Outer {{ int32 id = 1; repeated Color cs = 2; }}",
         text="id: 1 cs: GREEN cs: BLUE", setv="o.cs = {1, 2};",
         report='std::printf("  decoded cs.size=%zu\\n", o.cs.size());'),
    dict(name="map", decl="message Outer { int32 id = 1; map<string, int32> m = 2; }",
         text='id: 1 m { key: "k" value: 5 }', member="m"),
    dict(name="oneof", decl="message Outer { int32 id = 1; oneof pick { int32 x = 2; string y = 3; } }",
         text="id: 1 x: 9", member="x"),
]

SRC = r'''#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include <ctpb/ctpb.hpp>
inline constexpr ctpb::fixed_string proto = R"PROTO(
syntax = "proto3";
@DECL@
)PROTO";
#if !CTPB_HAS_P2996
#  error "reflection path only"
#endif
@DEFINES@
using Outer = ctpb::message_t<proto, "Outer">;
template <class T> requires requires(T& t) { t.@MEM@; }
void member_report(T&) { std::printf("  struct member '@MEM@': present\n"); }
template <class T> void member_report(T&) { std::printf("  struct member '@MEM@': ABSENT\n"); }
int main() {
    Outer o{}; o.id = 1; @SET@
    std::vector<std::byte> buf(ctpb::byte_size(o));
    ctpb::encode_to_ptr(o, buf.data());
    FILE* f = std::fopen("ctpb.bin", "wb"); std::fwrite(buf.data(), 1, buf.size(), f); std::fclose(f);
    std::printf("  ctpb encodes     :"); for (auto b : buf) std::printf(" %02x", (unsigned)b); std::printf("\n");
    std::vector<std::byte> in;
    { FILE* g = std::fopen("conformant.bin", "rb"); int c; while ((c = std::fgetc(g)) != EOF) in.push_back(std::byte(c)); std::fclose(g); }
    Outer d{};
    auto err = ctpb::decode_into(d, std::span<const std::byte>(in));
    std::printf("  ctpb decodes conformant bytes: error=%d id=%d\n", (int)!!err, (int)d.id);
    { auto& o = d; (void)o; @REPORT@ }
    member_report(d);
    return 0;
}
'''

def run(cmd, stdin=None):
    return subprocess.run(cmd, cwd=W, input=stdin, capture_output=True)

def hexdump(b): return " ".join(f"{x:02x}" for x in b)

for c in CASES:
    print(f"\n=== {c['name']} ===")
    (W / "case.proto").write_text(f'syntax = "proto3";\n{c["decl"]}\n')
    enc = run(["protoc", "--encode=Outer", "case.proto"], c["text"].encode())
    if enc.returncode:
        print("  protoc --encode FAILED:", enc.stderr.decode().strip()); continue
    (W / "conformant.bin").write_bytes(enc.stdout)
    print(f"  protoc conformant: {hexdump(enc.stdout)}")
    defines = ('consteval { ctpb::define_message<proto, "Inner">(); }\n' if c.get("nested") else "") \
              + 'consteval { ctpb::define_message<proto, "Outer">(); }'
    src = (SRC.replace("@DECL@", c["decl"]).replace("@DEFINES@", defines)
              .replace("@MEM@", c.get("member", "id")).replace("@SET@", c.get("setv", ""))
              .replace("@REPORT@", c.get("report", "")))
    (W / "probe.cpp").write_text(src)
    cc = run(["clang++", "-O1", "-std=c++26", "-freflection", "-I/workspace/include", "probe.cpp", "-o", "probe"])
    if cc.returncode:
        err = [l for l in cc.stderr.decode().splitlines() if "error" in l][:3]
        print("  DOES NOT COMPILE on the reflection path:"); [print("    " + l[:160]) for l in err]; continue
    out = run(["./probe"])
    print(out.stdout.decode().rstrip())
    dec = run(["protoc", "--decode=Outer", "case.proto"], (W / "ctpb.bin").read_bytes())
    shown = dec.stdout.decode().strip().replace("\n", " | ") or "(empty)"
    print(f"  protoc decodes ctpb's bytes: exit={dec.returncode} -> {shown}")
