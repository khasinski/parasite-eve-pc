#!/bin/sh
# Runs every line of a runs file through test-mod-randomizer-run, restarting after a run that crashes:
# the crashed line goes into <out>.crashed (one runs line each), the rest into <out>.
# Usage: runall.sh <test-mod-randomizer-run.exe> <runs.txt> <out.txt>   (PE_TEST_DISC set)
exe=$1 runs=$2 out=$3
: > "$out"; : > "$out.crashed"
total=$(grep -c . "$runs")
from=1
while [ "$from" -le "$total" ]; do
    "$exe" "$out" "$runs" "$from" > /dev/null 2>&1 && break
    done_lines=$(grep -c '^run' "$out")
    crashed=$((done_lines + $(grep -c . "$out.crashed") + 1))
    sed -n "${crashed}p" "$runs" >> "$out.crashed"
    from=$((crashed + 1))
done
echo "runs: $(grep -c '^run' "$out") done, $(grep -c . "$out.crashed") crashed"
