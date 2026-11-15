#!/bin/sh
# runall.sh in n parallel shards: splits the runs file by lines, runs each shard, then joins the outputs
# (and the .crashed lists) in runs order.
# Usage: runshards.sh <test-mod-randomizer-run.exe> <runs.txt> <out.txt> [n=4]   (PE_TEST_DISC set)
exe=$1 runs=$2 out=$3 n=${4:-4}
here=$(dirname "$0")
rm -f "$out.shard-"*
split -n "l/$n" -d -a 2 "$runs" "$out.shard-"
for s in "$out.shard-"??; do sh "$here/runall.sh" "$exe" "$s" "$s.out" > "$s.log" 2>&1 & done
wait
cat "$out.shard-"??.out > "$out"
cat "$out.shard-"??.out.crashed > "$out.crashed"
echo "runs: $(grep -c '^run' "$out") done, $(grep -c . "$out.crashed") crashed"
rm -f "$out.shard-"*
