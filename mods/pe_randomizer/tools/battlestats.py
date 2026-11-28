"""Checks the battle randomizer's stat swap from test-mod-randomizer-run lines with stats (mod-randomizer-battlestats):
python -I battlestats.py <run output> <pe_rand_enemies.h> [--run test-mod-randomizer-run runs.txt]
(--run: runs the test binary on runs.txt into <run output> first; the ctest does that.)

Each combat= entry is an enemy slot's stat block after its setup (PE_RAND_STAT_CODES order).
- Every slot of a line without force= whose actor is in pe_rand_enemies.h sets what the table says, for
  each code the table has (scaled values at the scale 0x10000 of a game's start): the scan read the setup right.
- A line with force=5<M005:2 needs a line of its own room without force=, and one of M005 (the source's own
  fight, so its block is checked by the rule above). Actor 5's slots have the source's table value for each
  code the source has, and the plain line's value for the others; every other slot equals the plain line's.
- A forced actor that also runs the source's script (ai=5<M005:2, the AI switch) sets the codes the source's
  table lacks (rolled drops and steals) in that script's own setup: those aren't compared with the plain line.
- A line with ai= and no force= (a seed plan's, tools/foeruns.py): the other actors' slots are checked as on a
  plain line; the moved actors' stats are their own scripts' business.
- With force= lines, at least one forced slot ends up with numbers different from its plain run (the swap did something).
Exit 1 on a failure, 77 when an input is missing."""
import os, re, subprocess, sys

SCALE = 0x10000
# How FieldActorState stores each code (include/pe1/field_actor.h): bits, signed. A value is compared as stored.
WIDTH = {0x28: (16, 0), 0x2c: (32, 1), 0x2d: (32, 1), 0x3c: (32, 1), 0x3d: (16, 1), 0x3e: (16, 1), 0x3f: (8, 0),
         0x41: (8, 0), 0x42: (8, 0), 0x43: (8, 0), 0x44: (8, 0), 0x45: (16, 1), 0x5a: (16, 1), 0x5b: (16, 1),
         0x5c: (8, 1), 0x5d: (8, 1), 0x6d: (1, 0)}


def stored(code, v):
    bits, signed = WIDTH.get(code, (2, 0))
    v &= (1 << bits) - 1
    return v - (1 << bits) if signed and v >> (bits - 1) else v


def table(path):
    text = open(path).read()
    codes = [int(c, 16) for c in re.search(r"PE_RAND_STAT_CODES\[PE_RAND_STATS\] = \{([^}]*)\}", text).group(1).split(",")]
    rows = {}
    for room, ent, _, have, scaled, vals in re.findall(
            r"\{ (\d+), (\d+), (\d+), (0x[0-9a-f]+), (0x[0-9a-f]+), \{ ([-\d, ]+) \} \}", text):
        have, scaled, vals = int(have, 16), int(scaled, 16), [int(v) for v in vals.split(",")]
        rows[(int(room), int(ent))] = {i: stored(codes[i], stored(0x2c, SCALE * vals[i]) >> 16 if scaled >> i & 1 else vals[i])
                                       for i in range(len(codes)) if have >> i & 1}
    return codes, rows


def runs(path):
    out = []
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] != "run": continue
        kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        if "combat" not in kv: continue
        slots = []
        for e in filter(None, kv["combat"].split(";")):
            who, vals = e.split(":")
            slots.append((int(who[1:].split("#")[0]), [int(v) for v in vals.split(",")]))
        force = {}
        for x in filter(None, kv.get("force", "").split(",")):
            to, src = x.split("<M")
            room, ent = src.split(":")
            force[int(to)] = (int(room), int(ent))
        ai = {int(x.split("<")[0]) for x in filter(None, kv.get("ai", "").split(","))}
        out.append(dict(room=int(f[1][1:]), id=kv.get("id", ""), force=force, ai=ai, slots=slots))
    return out


def main():
    args = sys.argv[1:]
    run = None
    if "--run" in args:
        i = args.index("--run"); run = args[i + 1:i + 3]; del args[i:i + 3]
    if len(args) != 2 or (run is not None and len(run) != 2): sys.exit(__doc__)
    if run:
        env = dict(os.environ, PSYZ_HEADLESS="1", SDL_AUDIODRIVER="dummy")
        p = subprocess.run([run[0], args[0], run[1]], env=env, capture_output=True, text=True, timeout=600)
        if p.returncode == 77: print(p.stdout + p.stderr); sys.exit(77)
        if p.returncode or "Randomizer runs:" not in p.stdout:
            print(p.stdout + p.stderr[-3000:]); print(f"battlestats: the run test failed ({p.returncode})"); sys.exit(1)
        print(p.stdout.strip())
    if not all(os.path.exists(p) for p in args):
        print("battlestats: missing " + ", ".join(p for p in args if not os.path.exists(p))); sys.exit(77)
    codes, rows = table(args[1])
    lines = runs(args[0])
    bad, checked, swapped = [], 0, 0
    def name(i): return f"{codes[i]:#04x}"
    plain = {}
    for ln in lines:
        if ln["force"]: continue
        if not ln["ai"]: plain.setdefault(ln["room"], ln)
        for ent, vals in ln["slots"]:
            if ent in ln["ai"]: continue   # another foe's script (a seed plan's line) sets its own
            for i, want in rows.get((ln["room"], ent), {}).items():
                checked += 1
                if vals[i] != want: bad.append(f"M{ln['room']:03d} e{ent} ({ln['id']}): sets {name(i)}={vals[i]}, the table says {want}")
    for ln in lines:
        if not ln["force"]: continue
        base = plain.get(ln["room"])
        if not base: bad.append(f"M{ln['room']:03d} ({ln['id']}): no plain stats line of the room"); continue
        for src in ln["force"].values():
            if src[0] not in plain: bad.append(f"M{ln['room']:03d} ({ln['id']}): no plain stats line of the source M{src[0]:03d}")
            elif not any(e == src[1] for e, _ in plain[src[0]]["slots"]):
                bad.append(f"M{ln['room']:03d} ({ln['id']}): the source M{src[0]:03d} e{src[1]} took no slot in its own run")
        if [e for e, _ in ln["slots"]] != [e for e, _ in base["slots"]]:
            bad.append(f"M{ln['room']:03d} ({ln['id']}): slots {[e for e, _ in ln['slots']]}, plain run {[e for e, _ in base['slots']]}")
            continue
        forced_slots = 0
        for (ent, vals), (_, was) in zip(ln["slots"], base["slots"]):
            src = rows.get(ln["force"][ent]) if ent in ln["force"] else None
            forced_slots += src is not None
            for i in range(len(codes)):
                if src is not None and i not in src and ent in ln["ai"]: continue   # the source script's own roll
                want = src[i] if src is not None and i in src else was[i]
                checked += 1
                if vals[i] != want:
                    bad.append(f"M{ln['room']:03d} e{ent} ({ln['id']}): {name(i)}={vals[i]}, want {want}"
                               + (f" (M{ln['force'][ent][0]:03d} e{ln['force'][ent][1]}'s)" if src is not None and i in src else " (its own)"))
            if src is not None and vals != was: swapped += 1
        if not forced_slots: bad.append(f"M{ln['room']:03d} ({ln['id']}): no forced actor took a slot")
    if not swapped and any(ln["force"] for ln in lines): bad.append("no forced slot changed: the swap did nothing")
    for b in bad[:60]: print("FAIL " + b)
    print(f"Battle stats: {len(lines)} lines, {checked} values checked, {swapped} slots swapped, {len(bad)} problems")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
