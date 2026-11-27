#!/bin/sh
# hardwalk.py end to end in build-mods/mods (PE_TEST_DISC set): arrivals, zone runs plain and with press, every
# chain level, then the table (both states; HARDWALK_EITHER=1 for either; with --deny: Hard's proof, whose
# doors are the ctest's lines).
# Usage: hardwalk.sh <test-mod-randomizer-run.exe> [shards=4]   (~25 min; outputs walk-*.txt, hard-walk.txt)
exe=$1 n=${2:-4}
here=$(dirname "$0")
export PSYZ_HEADLESS=1 SDL_AUDIODRIVER=dummy
set -- doors.txt map-exits.txt randomizer-exits.txt windows.txt ../../mods/pe_randomizer/pe_rand_spots.h \
    ../../mods/pe_randomizer/pe_randomizer.c walk-states.txt "$here/windows-runs.txt" story-graph.txt
run() {   # walk-$1-runs.txt -> walk-$1.txt
    grep -v '^#' "walk-$1-runs.txt" > "walk-$1.in"
    sh "$here/runshards.sh" "$exe" "walk-$1.in" "walk-$1.txt" "$n" | sed "s/^/$1: /"
    rm -rf "walk-$1.in" "walk-$1.txt.shard-"*
}
python -I "$here/hardwalk.py" arrive "$@" > walk-arrive-runs.txt && run arrive
zones="" 
for p in "" "--press"; do
    s=${p:+p}
    python -I "$here/hardwalk.py" zones randomizer-rooms.txt walk-arrive-runs.txt walk-arrive.txt $p > "walk-zone$s-runs.txt" && run "zone$s"
    zones="$zones walk-zone$s-runs.txt walk-zone$s.txt"
    up=arrive cur=zone$s lvl=2
    while :; do
        python -I "$here/hardwalk.py" zones randomizer-rooms.txt "walk-$cur-runs.txt" "walk-$cur.txt" "walk-$up-runs.txt" "walk-$up.txt" $p > "walk-zone$s$lvl-runs.txt"
        [ "$(grep -vc '^#' "walk-zone$s$lvl-runs.txt")" -gt 0 ] || break
        run "zone$s$lvl"
        zones="$zones walk-zone$s$lvl-runs.txt walk-zone$s$lvl.txt"
        up=$cur cur=zone$s$lvl lvl=$((lvl + 1))
    done
done
python -I "$here/hardwalk.py" table "$@" hard-reach.txt "$here/gives-runs.txt" \
    walk-arrive-runs.txt walk-arrive.txt $zones --writes story-writes.txt > hard-walk.txt
HARDWALK_EITHER=1 python -I "$here/hardwalk.py" table "$@" hard-reach.txt "$here/gives-runs.txt" \
    walk-arrive-runs.txt walk-arrive.txt $zones --writes story-writes.txt > hard-walk-either.txt
HARDWALK_EITHER=1 HARDWALK_RUNS=hardwalk-runs.txt python -I "$here/hardwalk.py" table "$@" hard-reach.txt "$here/gives-runs.txt" \
    walk-arrive-runs.txt walk-arrive.txt $zones --writes story-writes.txt --deny "$here/hardwalk-deny.txt" > hard-walk-deny.txt
grep summary hard-walk.txt hard-walk-either.txt hard-walk-deny.txt
# spottable.py --hard-drop reads tools/hard-drop.txt: the FAIL pair rows of hard-walk-deny.txt (header kept).
# ctest mod-randomizer-hardwalk reads tools/hardwalk-runs.txt: copy hardwalk-runs.txt there after a rerun.
