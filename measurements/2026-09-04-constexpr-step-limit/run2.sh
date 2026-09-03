#!/bin/bash
# Where exactly does constant evaluation stop, and does the step-limit flag rescue it?
# The step limit is a deterministic instruction count, so this threshold is a property
# of the code and the compiler, not of the host or of emulation.
set -u
CXX=clang++
FLAGS="-O3 -std=c++26 -freflection -stdlib=libstdc++ -I/workspace/include"

gen_tu() { local n="$1" f="$2"
  { echo '#include <string_view>'; echo '#include <ctpb/ctpb.hpp>'
    echo 'constexpr std::string_view schema = R"PROTO('
    echo 'syntax = "proto3";'; echo "message S {"
    for i in $(seq 1 "$n"); do echo "  int32 f$i = $i;"; done; echo "}"
    echo ')PROTO";'
    echo 'consteval std::size_t field_count() { ctpb::parser p{schema}; auto f = p.parse_file(); return f.messages[0].fields.size(); }'
    echo "static_assert(field_count() == $n);"; echo 'int main() { return 0; }'
  } > "$f"; }

try() { # n, extra flags -> 0 pass / 1 step-limit / 2 other
  gen_tu "$1" /tmp/t.cpp
  if $CXX $FLAGS ${2:-} /tmp/t.cpp -o /tmp/t.out 2>/tmp/t.err; then return 0; fi
  grep -qi 'maximum step limit' /tmp/t.err && return 1; return 2; }

echo "=== 1. exact threshold at the DEFAULT step limit (bisection) ==="
lo=250; hi=500
while [ $((hi-lo)) -gt 1 ]; do
  mid=$(((lo+hi)/2)); try $mid; r=$?
  [ $r -eq 0 ] && lo=$mid || hi=$mid
  printf '  %4d fields -> %s\n' "$mid" "$([ $r -eq 0 ] && echo pass || ([ $r -eq 1 ] && echo STEP_LIMIT || echo other))"
done
echo "  => largest single message that compiles at the default limit: $lo fields (fails at $hi)"

echo
echo "=== 2. does -fconstexpr-steps rescue 500 and 1000 fields? ==="
for n in 500 1000; do
  for steps in 4194304 16777216 67108864 268435456; do
    try $n "-fconstexpr-steps=$steps"; r=$?
    printf '  %5d fields, -fconstexpr-steps=%-10s -> %s\n' "$n" "$steps" \
      "$([ $r -eq 0 ] && echo pass || ([ $r -eq 1 ] && echo STEP_LIMIT || echo "other: $(head -2 /tmp/t.err|tail -1|cut -c1-70)"))"
    [ $r -eq 0 ] && break
  done
done

echo
echo "=== 3. is the default limit what Clang documents (1048576)? ==="
$CXX $FLAGS -fconstexpr-steps=1048576 /tmp/t.cpp -o /tmp/t.out 2>/dev/null && echo "  explicit default accepted" || echo "  explicit default also fails (consistent with default==1048576)"
echo
echo "=== 4. does the project set any constexpr flag of its own? ==="
grep -rn 'constexpr-steps\|constexpr-ops\|constexpr-depth' /workspace/CMakeLists.txt /workspace/benchmarks/*.sh 2>/dev/null || echo "  none anywhere in the build"
