// The positive control for check_refusals.sh: every field kind the reflection
// path does support, synthesized and round-tripped. If the refusal were too
// broad this would stop compiling; if the compile command were wrong, the
// refusal cases would fail for the wrong reason and this would say so first.
#include <cstdio>
#include <vector>
#include <ctpb/ctpb.hpp>

#if !CTPB_HAS_P2996
#  error "accept_flat is only built with the P2996 gate open"
#endif

inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    enum Color { RED = 0; GREEN = 1; }
    message Flat {
      enum State { OFF = 0; ON = 1; }
      int32 id = 1;
      string name = 2;
      Color c = 3;
      repeated Color cs = 4;
      fixed32 f = 5;
      sint64 z = 6;
      double d = 7;
      State s = 8;
    }
)";

consteval { ctpb::define_message<schema, "Flat">(); }
using Flat = ctpb::message_t<schema, "Flat">;

int main() {
    Flat f{};
    f.id = 7; f.name = "x"; f.c = 1; f.cs = {1, 0}; f.f = 5u; f.z = -3; f.d = 2.5; f.s = 1;
    auto wire = ctpb::encode(f);
    Flat g{};
    auto err = ctpb::decode_into(g, wire);
    bool ok = !err && g.id == 7 && g.name == "x" && g.c == 1 && g.cs.size() == 2
              && g.f == 5u && g.z == -3 && g.d == 2.5 && g.s == 1;
    std::printf("CTPB_HAS_P2996=%d accept_flat=%s\n", (int)CTPB_HAS_P2996, ok ? "ok" : "MISMATCH");
    return ok ? 0 : 1;
}
