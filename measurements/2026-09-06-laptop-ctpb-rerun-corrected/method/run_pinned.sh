#!/usr/bin/env bash
set -uo pipefail
S="<scratch>"
OUT="<repo>/benchmarks/results-2026-09-06-corrected"

restore() {
  echo 1 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
  sudo cpupower frequency-set -g powersave -d 1102866 -u 4465261 >/dev/null 2>&1
  {
    echo "# host CPU state: AFTER restore, read back"
    echo "  timestamp:  $(date -Is)"
    echo "  governor:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u | tr '\n' ' ')"
    echo "  epp:        $(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference | sort -u | tr '\n' ' ')"
    echo "  min_freq:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq | sort -u | tr '\n' ' ')"
    echo "  max_freq:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq | sort -u | tr '\n' ' ')"
    echo "  boost:      $(cat /sys/devices/system/cpu/cpufreq/boost)"
    M=ok
    [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u)" = powersave ] || M="$M GOV-MISMATCH"
    [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq | sort -u)" = 1102866 ] || M="$M MIN-MISMATCH"
    [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq | sort -u)" = 4465261 ] || M="$M MAX-MISMATCH"
    [ "$(cat /sys/devices/system/cpu/cpufreq/boost)" = 1 ] || M="$M BOOST-MISMATCH"
    [ "$(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference | sort -u)" = performance ] || M="$M EPP-MISMATCH"
    echo "  vs baseline (host-state-0): $M"
  } | tee "$S/host-state-restored.txt"
}
trap restore EXIT

echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost >/dev/null
sudo cpupower frequency-set -g performance -d 3200000 -u 3200000 >/dev/null 2>&1
{
  echo "# host CPU state: DURING the corrected campaign (pinned)"
  echo "  timestamp:  $(date -Is)"
  echo "  governor:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u | tr '\n' ' ')"
  echo "  epp:        $(cat /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference | sort -u | tr '\n' ' ')"
  echo "  min_freq:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq | sort -u | tr '\n' ' ')"
  echo "  max_freq:   $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq | sort -u | tr '\n' ' ')"
  echo "  boost:      $(cat /sys/devices/system/cpu/cpufreq/boost)"
  echo "  AC online:  $(cat /sys/class/power_supply/ADP0/online)"
} | tee "$S/host-state-during.txt"

"$S/clock_sampler.sh" > "$S/clock-during-run.txt" 2>/dev/null &
SAMPLER=$!
"$S/rerun_corrected.sh" "$OUT"
RC=$?
kill -9 "$SAMPLER" 2>/dev/null
wait "$SAMPLER" 2>/dev/null
echo "sampler stopped; $(wc -l < "$S/clock-during-run.txt") samples"
exit $RC
