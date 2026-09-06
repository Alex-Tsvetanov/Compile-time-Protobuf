#!/usr/bin/env bash
# Corrected driver for the P5 re-run. NOT a modified rerun_all.sh -- that script is P5's
# committed method and is left untouched. This mirrors its five arms with the two defects
# fixed: the DERIVED image built from the repository's own Dockerfile, and the repository
# mounted at /workspace where every eval script actually reads.
set -uo pipefail

REPO="<repo>"
IMAGE="ctpb-dev:2026-09-06"
OUT="$1"
mkdir -p "$OUT"

run_in_image() {
  local label="$1" script="$2"
  echo
  echo "=== $label ==="
  if docker run --rm --platform=linux/amd64 -v "$REPO":/workspace -w /workspace "$IMAGE" \
        bash "$script" > "$OUT/$label.txt" 2>&1; then
    echo "ok -> $label.txt"
    tail -5 "$OUT/$label.txt"
  else
    echo "FAILED (exit $?) -> $label.txt" >&2
    tail -20 "$OUT/$label.txt" >&2
  fi
}

run_in_image runtime_comparison      benchmarks/eval_runtime_comparison.sh
run_in_image runtime_comparison_lite benchmarks/eval_runtime_comparison_lite.sh
run_in_image binary_size             benchmarks/eval_binary_size.sh
run_in_image compile_time_latency    benchmarks/eval_compile_time_latency.sh
if [ -f "$REPO/measurements/2026-09-04-constexpr-step-limit/run2.sh" ]; then
  run_in_image constexpr_step_limit measurements/2026-09-04-constexpr-step-limit/run2.sh
fi
echo
echo "driver done."
