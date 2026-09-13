#!/usr/bin/env bash
# Asserts that the reflection path refuses, at compile time, every schema it
# cannot synthesize faithfully.
#
# Usage: check_refusals.sh <cxx> <include-dir> <work-dir> [compile flags...]
#
#   exit 0   gate open; each refusal fired with its own diagnostic; a flat
#            schema still builds and round-trips
#   exit 77  gate shut -- reported by ctest as Skipped, never as Passed. On the
#            fallback there is no refusal to test, and a green result there is
#            the failure that hid these defects through a whole campaign.
#   exit 1   anything else
set -uo pipefail
CXX=$1; INC=$2; WORK=$3; shift 3; FLAGS=("$@")
SRC=$(cd "$(dirname "$0")" && pwd)
MARK='ctpb_reflection_path_cannot_synthesize'
mkdir -p "$WORK"

cat > "$WORK/gate.cpp" <<'CPP'
#include <cstdio>
#include <ctpb/ctpb.hpp>
int main() { std::printf("%d\n", (int)CTPB_HAS_P2996); }
CPP
if ! "$CXX" "${FLAGS[@]}" -I"$INC" "$WORK/gate.cpp" -o "$WORK/gate" 2> "$WORK/gate.log"; then
    echo "FAIL: the gate probe does not compile"; cat "$WORK/gate.log"; exit 1
fi
gate=$("$WORK/gate")
echo "CTPB_HAS_P2996=$gate | $("$CXX" --version | head -1) | flags: ${FLAGS[*]}"
if [ "$gate" != "1" ]; then
    echo "SKIPPED: the P2996 gate is shut, so there is no refusal to test on this path."
    exit 77
fi

status=0
if "$CXX" "${FLAGS[@]}" -I"$INC" "$SRC/accept_flat.cpp" -o "$WORK/accept_flat" 2> "$WORK/accept_flat.log" \
        && out=$("$WORK/accept_flat"); then
    echo "PASS accept_flat: $out"
else
    echo "FAIL accept_flat: a supported schema did not build or round-trip"
    cat "$WORK/accept_flat.log"; status=1
fi

names=("" "message-typed field" "repeated message-typed field" "map field" "oneof")
for n in 1 2 3 4; do
    log="$WORK/refuse_$n.log"
    if "$CXX" "${FLAGS[@]}" -I"$INC" -DCASE=$n -fsyntax-only "$SRC/refuse.cpp" 2> "$log"; then
        echo "FAIL refuse ${names[$n]}: it compiled -- the reflection path accepted a schema it cannot encode"
        status=1
    elif grep -q "$MARK" "$log"; then
        echo "PASS refuse ${names[$n]}: $(grep -m1 "$MARK" "$log" | sed 's/^[[:space:]]*//')"
    else
        echo "FAIL refuse ${names[$n]}: did not compile, but not because of the refusal:"
        grep -m5 'error' "$log"; status=1
    fi
done
exit $status
