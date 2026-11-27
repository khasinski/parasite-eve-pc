"""ctest mod-randomizer-keysolve: Hard layouts of a fixed seed set (the generator's --layouts mode, seeds
1..N) checked by keysolve.py --hard over the story walk. Run in build-mods/mods:

  python -I keysolve_ctest.py <test-mod-randomizer-keys exe> <N> <keysolve.py's arguments, without --layouts>

(--zones @rwin/zones.list reads the comma list from that file.)

The walk's inputs (story-graph.txt, the runs and zones files: tools/storyruns.sh and fixpoint.sh, ~1 h)
stay in build-mods/mods and aren't in the repo; without them the test is skipped (exit 77). Exit 1 when
a layout fails (keysolve prints why)."""
import os, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))


def main():
    a = sys.argv[1:]
    if len(a) < 5: sys.exit(__doc__)
    gen, n, args = a[0], a[1], a[2:]
    if "--zones" in args and args[args.index("--zones") + 1].startswith("@"):   # @list: the file's comma list
        i = args.index("--zones") + 1
        if not os.path.isfile(args[i][1:]):
            print(f"keysolve: skipped, no {args[i][1:]} (the story walk's outputs)")
            sys.exit(77)
        args[i] = open(args[i][1:]).read().strip()
    files = [x for x in args[:3]] + [args[args.index(k) + 1] for k in ("--runs", "--hard") if k in args]
    if "--zones" in args: files += args[args.index("--zones") + 1].split(",")
    missing = [f for f in files if not os.path.isfile(f)]
    if missing:
        print(f"keysolve: skipped, no {missing[0]} (the story walk's outputs)")
        sys.exit(77)
    layouts = subprocess.run([gen, "--layouts", n], capture_output=True, text=True, check=True).stdout
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(layouts)
    try:
        sys.exit(subprocess.run([sys.executable, "-I", os.path.join(here, "keysolve.py"), *args, "--layouts", f.name]).returncode)
    finally:
        os.unlink(f.name)


if __name__ == "__main__":
    main()
