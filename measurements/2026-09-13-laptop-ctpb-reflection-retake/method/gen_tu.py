import sys
n = int(sys.argv[1]); out = sys.argv[2]
fields = "\n".join(f"      int32 f{i} = {i};" for i in range(1, n+1))
src = f'''#include <cstdio>
#include <ctpb/ctpb.hpp>

inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    message S {{
{fields}
    }}
)";

#if !CTPB_HAS_P2996
#  error "this probe measures P2996 synthesis; the gate is 0"
#endif
consteval {{ ctpb::define_message<schema, "S">(); }}
using S = ctpb::message_t<schema, "S">;

int main() {{ S s{{}}; std::printf("gate=%d fields_synthesised=ok\\n", (int)CTPB_HAS_P2996); return 0; }}
'''
open(out, "w").write(src)
