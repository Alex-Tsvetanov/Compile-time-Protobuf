// Schemas the reflection path cannot synthesize faithfully. Each must fail to
// compile on that path, and check_refusals.sh accepts the failure only if the
// diagnostic names the refusal: a compile can fail for any reason, and a test
// that counts any failure as a pass is a test that cannot fail.
//
// Deliberately no #error when the gate is shut. That would make every case
// fail on the fallback for the wrong reason, which the driver would then have
// to be trusted to notice. The driver skips the fallback instead.
#include <ctpb/ctpb.hpp>

#if CASE == 1
inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    message Inner { int32 a = 1; }
    message Outer { int32 id = 1; Inner inner = 2; }
)";
#elif CASE == 2
inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    message Inner { int32 a = 1; }
    message Outer { int32 id = 1; repeated Inner items = 2; }
)";
#elif CASE == 3
inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    message Outer { int32 id = 1; map<string, int32> m = 2; }
)";
#elif CASE == 4
inline constexpr ctpb::fixed_string schema = R"(
    syntax = "proto3";
    message Outer { int32 id = 1; oneof pick { int32 x = 2; string y = 3; } }
)";
#endif

consteval { ctpb::define_message<schema, "Outer">(); }

int main() {}
