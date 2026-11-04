"""Check a storywalk --states file against play logs: every room change a player made must be an edge
of the walk. Prints the missing ones.
The route is the scene column of every row in order (the room rows' from= alone skip rooms: the logger
samples the room now and then, so a room passed through quickly, M372 between M003 and M004, only shows
in other rows). A trip over the New York map (M0000) joins the rooms either side, as the walk's map
moves do; a new launch (session), a loaded save (cardload) or a scene that isn't a room (M5000 title,
M8888, none yet) breaks the route.
Usage: python -I logcheck.py <walk-states.txt> <pe_play_log.csv> [...]"""
import sys


def main():
    states = {}
    edges = set()
    rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1])]
    for f in rows: states[int(f[0])] = int(f[1][1:])
    for f in rows:
        a = int(f[1][1:])
        for x in f[5].split():
            edges.add((a, states[int(x.split("[")[0])]))
    seen = {}
    for path in sys.argv[2:]:
        last = None
        for line in open(path):
            c = line.rstrip("\n").split(",")
            if len(c) < 5 or c[0] == "kind": continue
            if c[0] in ("session", "cardload"): last = None; continue
            scene = c[4]
            if scene == "M0000I": continue
            if not (len(scene) == 6 and scene[0] == "M" and scene[5] == "I" and scene[1:5].isdigit()) or scene[1] != "0":
                last = None; continue
            room = int(scene[1:5])
            if last is not None and room != last: seen.setdefault((last, room), set()).add(c[2])
            last = room
    miss = [(k, v) for k, v in sorted(seen.items()) if k not in edges]
    print(f"{len(seen)} room changes in the logs, {len(seen) - len(miss)} in the walk, {len(miss)} missing")
    for (a, b), days in miss: print(f"missing\tM{a:03d} -> M{b:03d}\tday {','.join(sorted(days))}")


if __name__ == "__main__":
    main()
