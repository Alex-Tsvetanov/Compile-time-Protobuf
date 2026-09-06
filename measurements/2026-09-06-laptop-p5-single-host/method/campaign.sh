#!/usr/bin/env bash
set -uo pipefail
W="<scratch>"
REPO=<repo>
IMG=ctpb-dev:2026-09-06
snap(){ echo "gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor) epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference) min=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq) max=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq) boost=$(cat /sys/devices/system/cpu/cpufreq/boost)"; }
B_GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
B_EPP=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference)
B_MIN=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq)
B_MAX=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq)
B_BOOST=$(cat /sys/devices/system/cpu/cpufreq/boost)
{ echo "BEFORE PIN: $(snap)"; } | tee "$W/host-state.txt"
restore(){ echo 1 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
  sudo cpupower frequency-set -g powersave -d 1102866 -u 4465261 >/dev/null 2>&1
  for c in /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference; do echo "$B_EPP" | sudo tee "$c" >/dev/null; done
  M=ok
  [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor|sort -u)" = "$B_GOV" ] || M="$M GOV"
  [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference|sort -u)" = "$B_EPP" ] || M="$M EPP"
  [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq|sort -u)" = "$B_MIN" ] || M="$M MIN"
  [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq|sort -u)" = "$B_MAX" ] || M="$M MAX"
  [ "$(cat /sys/devices/system/cpu/cpufreq/boost)" = "$B_BOOST" ] || M="$M BOOST"
  echo "AFTER RESTORE: $(snap)  verification=$M" | tee -a "$W/host-state.txt"; }
trap restore EXIT
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
sudo cpupower frequency-set -g performance -d 3200000 -u 3200000 >/dev/null 2>&1
echo "DURING: $(snap)" | tee -a "$W/host-state.txt"
"$W/sampler.sh" > "$W/clock-during.txt" 2>/dev/null & SP=$!
run(){ echo "=== $1 ==="; docker run --rm --platform=linux/amd64 -v "$REPO":/workspace -w /workspace \
        -v "$W":/variants "$IMG" bash "$2" > "$W/$1.txt" 2>&1 && echo "  ok" || echo "  FAILED"; }
run runtime_prealloc      /variants/eval_runtime_prealloc.sh
run binary_size           benchmarks/eval_binary_size.sh
run compile_time_latency  benchmarks/eval_compile_time_latency.sh
run constexpr_step_limit  measurements/2026-09-04-constexpr-step-limit/run2.sh
kill -9 $SP 2>/dev/null; wait $SP 2>/dev/null
echo "samples: $(wc -l < "$W/clock-during.txt")"
echo DONE > "$W/finished"
