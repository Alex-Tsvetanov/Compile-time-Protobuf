#!/usr/bin/env bash
while :; do printf '%s ' "$(date +%s)"; awk '/MHz/{printf "%.0f ", $4}' /proc/cpuinfo; echo; sleep 2; done
