#!/usr/bin/env bash
# Sample the actual achieved clock DURING the timed work -- the thing the last run did not do.
while :; do
  printf '%s ' "$(date +%s)"
  awk '/MHz/{printf "%.0f ", $4}' /proc/cpuinfo
  echo
  sleep 2
done
