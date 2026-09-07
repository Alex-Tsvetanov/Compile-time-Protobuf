#!/bin/bash
# Pin the CPU for a timed run, and restore it afterwards against the state that was
# actually captured -- never against a hardcoded expectation.
#
# That distinction is the whole point of this file. An earlier version of this cycle
# compared the restored EPP against the literal "performance" and printed verification=ok
# while leaving the machine in a state it had not started in, because the baseline here is
# balance_performance and the check could not tell the difference between "restored" and
# "still pinned". A check that cannot fail is not a check.
set -euo pipefail

STATE_FILE="${2:-/tmp/wf-pin-state.txt}"

capture() {
    echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
    echo "epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference)"
    echo "boost=$(cat /sys/devices/system/cpu/cpufreq/boost)"
}

case "${1:-}" in
pin)
    capture > "$STATE_FILE"
    echo "captured baseline:"; sed 's/^/  /' "$STATE_FILE"
    for c in /sys/devices/system/cpu/cpu*/cpufreq; do
        echo performance | sudo -n tee "$c/scaling_governor" > /dev/null
        echo performance | sudo -n tee "$c/energy_performance_preference" > /dev/null
    done
    echo "pinned:"; capture | sed 's/^/  /'
    ;;
restore)
    if [ ! -f "$STATE_FILE" ]; then
        echo "no captured baseline at $STATE_FILE; refusing to guess" >&2
        exit 1
    fi
    gov=$(grep '^governor=' "$STATE_FILE" | cut -d= -f2)
    epp=$(grep '^epp=' "$STATE_FILE" | cut -d= -f2)
    for c in /sys/devices/system/cpu/cpu*/cpufreq; do
        echo "$gov" | sudo -n tee "$c/scaling_governor" > /dev/null
        echo "$epp" | sudo -n tee "$c/energy_performance_preference" > /dev/null
    done
    now_gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
    now_epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference)
    echo "baseline was governor=$gov epp=$epp"
    echo "now is     governor=$now_gov epp=$now_epp"
    # Compared against what was captured, which is the only comparison that can fail.
    if [ "$now_gov" = "$gov" ] && [ "$now_epp" = "$epp" ]; then
        echo "verification=ok"
    else
        echo "verification=FAILED -- the machine is not in the state it started in" >&2
        exit 1
    fi
    ;;
*)
    echo "usage: $0 {pin|restore} [state-file]" >&2; exit 2 ;;
esac
