#!/usr/bin/env bash
#
# Re-take every measured figure in the paper with the corrected harness.
#
# Why this exists: commit d27f4f7 rewrote eval_runtime_comparison.sh and
# eval_runtime_comparison_lite.sh, naming three defects in the old versions "each of which
# could account for the whole reported margin" -- no compiler barrier on the ctpb arm while
# protobuf's opaque library call had one, per-iteration allocation on both arms, and
# non-interleaved arms. That commit ends "Neither script has been compiled." Everything under
# benchmarks/results/ predates the rewrite, so the paper's throughput numbers come from the
# harness its own repository has repudiated.
#
# Plan for 2.81x and 1.71x not surviving. The standalone protobuf-side check in d27f4f7 moved
# encode 90.3 -> 30.1 ns and decode 218.6 -> 41.0 ns under the corrected patterns. If the ctpb
# arm does not move by a comparable factor, the encode speedup does not shrink, it disappears.
# That is a result either way and the paper is written to carry it.
#
# Two further figures are re-taken because their committed output cannot produce the numbers
# the paper prints:
#   - binary size: commit b457526 added a `stat -c %s` block for exactly this reason, and the
#     committed result predates it, carrying only `ls -lh` rounding (19K, 1.7M).
#   - the constexpr step limit: only the coarse sweep is committed, not the bisection that
#     locates 300 pass / 301 fail.
#
# Results go to a NEW dated directory. Nothing under benchmarks/results/ is overwritten: the
# old numbers stay visible beside the new ones so the two can be compared and the change
# explained in the paper.
#
# HOST CHANGE, DECLARE IT IN THE PAPER. The committed results were taken on an AMD Ryzen 5
# 3600 under Ubuntu 24.04 inside the Docker image. Running here moves the measurement to a
# different machine, so the new numbers are not comparable cell-by-cell with the old ones.
# That is acceptable and is the point: the old harness is repudiated, so this is a
# replacement rather than a diff. What makes the new numbers internally valid is that both
# arms are built once and interleaved in a single session on one host, which is what the
# rewrite fixed. The environment block below is captured so the host can be named.
#
# Usage:  bash benchmarks/rerun_all.sh
# Needs:  docker (Arch: sudo pacman -S docker && sudo systemctl start docker), and the
#         repository checked out. Run from the repository root.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAMP="$(date +%Y-%m-%d)"
OUT="$REPO/benchmarks/results-$STAMP"
IMAGE="sleepingeight/clang-cpp-refl:latest"

mkdir -p "$OUT"
echo "writing to $OUT"

# ---------------------------------------------------------------- environment, first
# Captured before anything runs, so the host is named even if a later step fails.
#
# This repository is public and the paper cites it for reproduction, so nothing here may
# carry machine identity. `uname -srmo` is deliberate: it gives kernel, release, machine and
# OS and NOT the nodename. Do not change it to `uname -a`, which adds the hostname. Hardware
# model and OS version are apparatus and belong here; hostname, username, addresses and home
# paths do not. The benchmark scripts write under /tmp inside the container, so their output
# carries container paths rather than anyone's home directory.
{
  echo "# Environment for the re-run of $STAMP"
  echo
  echo "## Host"
  echo "uname:        $(uname -srmo)"
  echo "cpu model:    $(awk -F': ' '/model name/{print $2; exit}' /proc/cpuinfo 2>/dev/null || echo unknown)"
  echo "cpu cores:    $(nproc 2>/dev/null || echo unknown)"
  echo "governor:     $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unreadable)"
  echo "load at start:$(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null || echo unknown)"
  echo "virtualised:  $(systemd-detect-virt 2>/dev/null || echo 'no systemd-detect-virt')"
  echo
  echo "## Toolchain, on the host"
  echo "docker:       $(docker --version 2>/dev/null || echo absent)"
  echo "git commit:   $(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "git dirty:    $(test -n "$(git -C "$REPO" status --porcelain 2>/dev/null)" && echo yes || echo no)"
  echo
  echo "## Toolchain, inside the image"
  docker run --rm --platform=linux/amd64 "$IMAGE" bash -lc \
    'echo "clang:        $(clang++ --version | head -1)"; \
     echo "protobuf:     $(protoc --version 2>/dev/null || echo absent)"; \
     echo "libprotobuf:  $(pkg-config --modversion protobuf 2>/dev/null || echo unknown)"; \
     echo "image arch:   $(uname -m)"' 2>&1 || echo "image not reachable"
} > "$OUT/environment.txt"
cat "$OUT/environment.txt"

# The image is linux/amd64. On an x86_64 host it runs natively; on Apple Silicon it runs
# under emulation, which is how the committed constexpr record was taken. Wall time and RSS
# are meaningless under emulation, so this warns rather than silently producing numbers.
if [ "$(uname -m)" != "x86_64" ]; then
  echo
  echo "WARNING: host is $(uname -m), not x86_64. The image will run emulated." >&2
  echo "         Step counts stay valid (deterministic), wall time and RSS do not." >&2
  echo "         Re-run on an x86_64 host before quoting any timing." >&2
fi

run_in_image() {           # run_in_image <label> <script-path-relative-to-repo>
  local label="$1" script="$2"
  echo
  echo "=== $label ==="
  if docker run --rm --platform=linux/amd64 -v "$REPO":/work -w /work "$IMAGE" \
        bash "$script" > "$OUT/$label.txt" 2>&1; then
    echo "ok -> $OUT/$label.txt"
    tail -5 "$OUT/$label.txt"
  else
    echo "FAILED, see $OUT/$label.txt" >&2
    tail -20 "$OUT/$label.txt" >&2
  fi
}

run_in_image runtime_comparison      benchmarks/eval_runtime_comparison.sh
run_in_image runtime_comparison_lite benchmarks/eval_runtime_comparison_lite.sh
run_in_image binary_size             benchmarks/eval_binary_size.sh
run_in_image compile_time_latency    benchmarks/eval_compile_time_latency.sh

# The constexpr bisection lives with the measurement rather than with the benchmarks.
if [ -f "$REPO/measurements/2026-09-04-constexpr-step-limit/run2.sh" ]; then
  run_in_image constexpr_step_limit measurements/2026-09-04-constexpr-step-limit/run2.sh
fi

echo
echo "done. Raw output is in $OUT"
echo
echo "Next, and none of it is automatic:"
echo "  1. Diff each figure against benchmarks/results/ and write down what moved."
echo "  2. If 2.81x or 1.71x did not survive, the abstract, benchmark section and both"
echo "     conclusions change. That is the expected outcome, not a failure of the run."
echo "  3. Declare the host change: the committed numbers are from a different machine."
echo "  4. Commit $OUT in full, raw output included, before editing any prose."
