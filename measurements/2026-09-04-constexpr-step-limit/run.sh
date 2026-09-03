#!/bin/bash
# What does it actually cost the COMPILER to ingest a .proto schema?
#
# Declared before the run. The paper's Table "Compile-Time Parser Throughput and
# Latency" is produced by timing ctpb::parser with steady_clock in an -O3 runtime
# loop; steady_clock cannot be read during constant evaluation, so that table
# measures the parser executing as native code, not the compiler. Nothing in the
# project has ever measured compilation. This does.
#
# Method: for each field count N, compile one TU that forces the parse through
# constant evaluation with a static_assert on a consteval function, and record the
# compiler's wall time and peak RSS. A baseline TU includes the same header and
# parses a 1-field schema, so the difference isolates ingestion from the fixed cost
# of the header and the toolchain.
set -u
OUT=/out
mkdir -p "$OUT"
CXX=clang++
FLAGS="-O3 -std=c++26 -freflection -stdlib=libstdc++ -I/workspace/include"
TIME=/usr/bin/time

command -v $TIME >/dev/null || { echo "NO_GNU_TIME"; TIME=""; }

gen_proto() {  # name, n
  echo 'syntax = "proto3";'; echo "message $1 {"
  for i in $(seq 1 "$2"); do echo "  int32 f$i = $i;"; done
  echo "}"
}

gen_tu() {  # n, outfile
  local n="$1" f="$2"
  { echo '#include <string_view>'
    echo '#include <ctpb/ctpb.hpp>'
    echo 'constexpr std::string_view schema = R"PROTO('
    gen_proto S "$n"
    echo ')PROTO";'
    echo 'consteval std::size_t field_count() {'
    echo '    ctpb::parser p{schema};'
    echo '    auto f = p.parse_file();'
    echo '    return f.messages[0].fields.size();'
    echo '}'
    echo "static_assert(field_count() == $n);"
    echo 'int main() { return 0; }'
  } > "$f"
}

echo "# ctpb constant-evaluation cost: what the COMPILER pays to ingest a schema"
echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# clang: $($CXX --version 2>&1 | head -1)"
echo "# flags: $FLAGS"
echo "# each cell: one compilation, forced through constant evaluation by static_assert"
echo
printf '%-8s %-10s %-12s %-12s %s\n' fields status wall_s peak_rss_kb note

for N in 1 5 25 100 250 500 1000; do
  SRC=/tmp/ce_$N.cpp; gen_tu "$N" "$SRC"
  ERR=/tmp/ce_$N.err
  if [ -n "$TIME" ]; then
    $TIME -v $CXX $FLAGS "$SRC" -o /tmp/ce_$N.out 2>"$ERR"
    rc=$?
    wall=$(grep -o 'Elapsed (wall clock) time.*' "$ERR" | awk '{print $NF}')
    rss=$(grep -o 'Maximum resident set size.*' "$ERR" | awk '{print $NF}')
  else
    s=$(date +%s.%N); $CXX $FLAGS "$SRC" -o /tmp/ce_$N.out 2>"$ERR"; rc=$?
    wall=$(echo "$(date +%s.%N) - $s" | bc); rss=NA
  fi
  note=""
  grep -qi 'constexpr.*step\|maximum.*step\|constexpr evaluation hit' "$ERR" && note="STEP_LIMIT_HIT"
  grep -qi 'out of memory\|Killed\|cannot allocate' "$ERR" && note="OOM"
  [ $rc -ne 0 ] && [ -z "$note" ] && note="FAILED(see err)"
  [ $rc -eq 0 ] && note="${note:-ok}"
  printf '%-8s %-10s %-12s %-12s %s\n' "$N" "$([ $rc -eq 0 ] && echo pass || echo FAIL)" "${wall:-?}" "${rss:-?}" "$note"
  [ $rc -ne 0 ] && { echo "    --- first 6 lines of diagnostics ---"; head -6 "$ERR" | sed 's/^/    /'; }
  cp "$ERR" "$OUT/ce_$N.err" 2>/dev/null
done
