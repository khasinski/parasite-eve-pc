"""Runs file for the mod-randomizer-run test from storywalk.py's places: one line per place (room,
V0x4a, V0x1) with the story bits that are surely set there.
Usage: python -I runlist.py <walk-states.txt> [--may] [--without <randomizer-run.txt>] > runs.txt
--may: the bits that may be set instead (every one at once: an upper bound, the bits may not all go
together), for what a room places only after an optional event (M041 spawns entity 8 on V0x2d&0x1000).
--without: only places (room, V0x4a, V0x1) that output has no run of (fixpoint.sh).
Then: test-mod-randomizer-run <randomizer-run.txt> runs.txt (PE_TEST_DISC set; headless by itself)."""
import sys


def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    may = "--may" in sys.argv[2:]
    seen = set()
    if "--without" in sys.argv[2:]:
        for line in open(sys.argv[sys.argv.index("--without") + 1]):
            f = line.split("	")
            seen.add((f[1], int(f[2].split("=")[1], 16), int(f[3].split("=")[1], 16)))
    for line in open(sys.argv[1]):
        f = line.rstrip("\n").split("\t")
        room, area, left = f[1], f[2].split("=")[1], f[3].split("=")[1]
        sure = []
        for b in f[4].split():
            var, masks = b.split("=")
            one, zero = (int(x, 16) for x in masks.split("/"))
            bits = one if may else one & ~zero
            if bits: sure.append(f"{var}={bits:#x}")
        run = f"{room} {area} {left}" + "".join(" " + s for s in sure)
        place = (f"M{int(room[1:]):03d}", int(area, 16), int(left, 16))
        if run not in seen and place not in seen:
            seen.add(run); print(run)


if __name__ == "__main__":
    main()
