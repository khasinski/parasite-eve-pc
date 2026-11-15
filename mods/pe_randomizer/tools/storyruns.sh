#!/bin/sh
# Every run the story walk learns from, into <dir>: base runs of runs.txt and runs-may.txt (runlist.py
# without and with --may) with each battle played to a won end (win), then zone runs (zoneruns.py) and
# their chains, without and with press, then the walk. <dir>/zones.list is the --zones list in order (each
# may base file before its zone runs). About 26 minutes, 2.2M runs; fixpoint.sh then runs the places the
# walk reaches that runs.txt didn't have.
# Usage: storyruns.sh [dir=rwin]   (in build-mods/mods, PE_TEST_DISC set, test-mod-randomizer-run built)
set -e
T=$(dirname "$0")
EXE=./test-mod-randomizer-run.exe
D=${1:-rwin}
mkdir -p "$D"
# giveruns.py pairs runs lines with outputs line by line: a crashed run must be fixed in the test first.
runs() {
  sh "$T/runshards.sh" "$EXE" "$1" "$2" 4
  if [ -s "$2.crashed" ]; then echo "$2: runs crashed (listed in $2.crashed)"; exit 1; fi
}
sed "s/$/ win/" runs.txt > runs-win.txt
sed "s/$/ win/" runs-may.txt > runs-may-win.txt
runs runs-win.txt "$D/randomizer-run.txt"
runs runs-may-win.txt "$D/randomizer-run-may.txt"
zones=""
for base in sure may; do
  if [ $base = sure ]; then base_runs=runs-win.txt out=$D/randomizer-run.txt; else base_runs=runs-may-win.txt out=$D/randomizer-run-may.txt; zones="$zones,$out"; fi
  for press in "" --press; do
    tag=$base${press:+-p}
    python -I "$T/zoneruns.py" $press $base_runs "$out" > "$D/zr-$tag-1.txt"
    prev_runs=$base_runs prev_out=$out
    n=1
    while [ -s "$D/zr-$tag-$n.txt" ] && [ $n -le 8 ]; do
      runs "$D/zr-$tag-$n.txt" "$D/zo-$tag-$n.txt"
      echo "$tag level $n: $(wc -l < "$D/zr-$tag-$n.txt") runs, $(grep -c end=door "$D/zo-$tag-$n.txt") doors"
      zones="$zones,$D/zo-$tag-$n.txt"
      python -I "$T/zoneruns.py" "$D/zr-$tag-$n.txt" "$D/zo-$tag-$n.txt" $prev_runs $prev_out > "$D/zr-$tag-$((n+1)).txt"
      prev_runs=$D/zr-$tag-$n.txt prev_out=$D/zo-$tag-$n.txt
      n=$((n+1))
    done
  done
done
echo "${zones#,}" > "$D/zones.list"
python -I "$T/storywalk.py" story-graph.txt randomizer-exits.txt --spots "$T/../pe_rand_spots.h" \
  --states "$D/walk-states.txt" --runs "$D/randomizer-run.txt" --zones "${zones#,}" > "$D/walk.txt"
head -1 "$D/walk.txt"
