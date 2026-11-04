#!/bin/sh
# After storyruns.sh: a base run (win) for every place the walk reaches that has none yet, then the walk
# again, until no new place appears. The base runs grow into <dir>/base-fix.txt (with their runs lines in
# <dir>/base-fix-runs.txt, for giveruns.py); the zone links stay the ones storyruns.sh learnt (a place
# with no zone run only keeps doors: sound). Output: <dir>/walk-fix.txt, <dir>/walk-states-fix.txt.
# Usage: fixpoint.sh [dir=rwin]   (in build-mods/mods, PE_TEST_DISC set)
set -e
T=$(dirname "$0")
EXE=./test-mod-randomizer-run.exe
D=${1:-rwin}
zones=$(cat "$D/zones.list")
[ -f "$D/base-fix.txt" ] || { cp "$D/randomizer-run.txt" "$D/base-fix.txt"; cp runs-win.txt "$D/base-fix-runs.txt"; }
n=1
while [ $n -le 12 ]; do
  python -I "$T/storywalk.py" story-graph.txt randomizer-exits.txt --spots "$T/../pe_rand_spots.h" \
    --states "$D/walk-states-fix.txt" --runs "$D/base-fix.txt" --zones "$zones" > "$D/walk-fix.txt"
  python -I "$T/runlist.py" "$D/walk-states-fix.txt" --without "$D/base-fix.txt" | sed "s/$/ win/" > "$D/runs-new.txt"
  new=$(grep -c . "$D/runs-new.txt" || true)
  echo "round $n: $(grep -c . "$D/walk-states-fix.txt") places, $new without a run; $(head -1 "$D/walk-fix.txt")"
  [ "$new" -eq 0 ] && break
  sh "$T/runshards.sh" "$EXE" "$D/runs-new.txt" "$D/runs-new-out.txt" 4
  if [ -s "$D/runs-new-out.txt.crashed" ]; then echo "runs crashed (listed in $D/runs-new-out.txt.crashed)"; exit 1; fi
  cat "$D/runs-new-out.txt" >> "$D/base-fix.txt"
  cat "$D/runs-new.txt" >> "$D/base-fix-runs.txt"
  n=$((n+1))
done
