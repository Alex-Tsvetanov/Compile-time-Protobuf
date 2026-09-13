#!/bin/bash
set -u
DIR=/tmp/bisect_syn; rm -rf $DIR; mkdir -p $DIR
CXX=clang++
FLAGS=(-O3 -std=c++26 -freflection -I/workspace/include)
TIMEOUT=${TIMEOUT:-900}
echo "flags: ${FLAGS[*]}   (no -stdlib=libstdc++, which would close the gate)"
echo "per-compile timeout: ${TIMEOUT}s"
try_n() {
  local n="$1" tu="$DIR/n$1.cpp" log="$DIR/n$1.log"
  python3 /out/gen_tu.py "$n" "$tu"
  local t0=$(date +%s.%N)
  timeout "$TIMEOUT" "$CXX" "${FLAGS[@]}" "$tu" -o "$DIR/n$1.bin" > "$log" 2>&1
  local rc=$? t1=$(date +%s.%N)
  local secs=$(awk -v a=$t0 -v b=$t1 'BEGIN{printf "%.1f", b-a}')
  if [ $rc -eq 0 ]; then echo "n=$n PASS compile_s=$secs"; return 0; fi
  if [ $rc -eq 124 ]; then echo "n=$n TIMEOUT ${TIMEOUT}s -- NOT a step limit, a wall-clock cap"; return 1; fi
  echo "n=$n FAIL rc=$rc compile_s=$secs"; grep -m4 -E 'error|exceeded|steps' "$log" | sed 's/^/    /'
  return 1
}
LAST_PASS=0; FIRST_FAIL=0
for n in 25 100 300 400 600 800 1200; do
  if try_n $n; then LAST_PASS=$n; else FIRST_FAIL=$n; break; fi
done
if [ "$FIRST_FAIL" = 0 ]; then echo "=== no failure up to 1200; bound is above the ladder ==="; exit 0; fi
echo "=== binary search in ($LAST_PASS pass, $FIRST_FAIL fail] ==="
lo=$LAST_PASS; hi=$FIRST_FAIL
while [ $((hi - lo)) -gt 1 ]; do
  mid=$(((lo + hi) / 2)); if try_n $mid; then lo=$mid; else hi=$mid; fi
done
echo "=== BOUND: n=$lo compiles, n=$hi does not ==="
echo "--- diagnostic at n=$hi, first 20 lines ---"; sed -n '1,20p' "$DIR/n$hi.log"
