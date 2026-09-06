#!/usr/bin/env bash
set -uo pipefail
W="<scratch>"
REPO=<repo>
snap(){ echo "gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor) epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference) min=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq) max=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq) boost=$(cat /sys/devices/system/cpu/cpufreq/boost)"; }
echo "BASELINE STATE (before pin): $(snap)" | tee "$W/host-state.txt"
restore(){ echo 1 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
           sudo cpupower frequency-set -g powersave -d 1102866 -u 4465261 >/dev/null 2>&1
           echo "AFTER RESTORE: $(snap)" | tee -a "$W/host-state.txt"; }
trap restore EXIT
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
sudo cpupower frequency-set -g performance -d 3200000 -u 3200000 >/dev/null 2>&1
echo "PINNED STATE (during all four runs): $(snap)" | tee -a "$W/host-state.txt"
for v in v0-baseline v1-prealloc-dest v2-short-payload v3-no-barrier-ctpb; do
  echo "### $v ###"
  docker run --rm --platform=linux/amd64 -v "$REPO":/workspace -w /workspace -v "$W":/variants \
    ctpb-dev:2026-09-06 bash "/variants/$v.sh" > "$W/$v.out" 2>&1
  echo "  exit=$? lines=$(grep -c '^trial=' "$W/$v.out")"
done
echo DONE > "$W/finished"
