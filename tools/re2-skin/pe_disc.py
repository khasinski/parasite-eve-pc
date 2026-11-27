"""Read the Parasite Eve disc for the bridge's decoders, without its save state.

render_models.py takes three things from a PST save state: the PE.IMG base sector (RAM 0x800B0DD8)
and the room / image sector tables (0x80093378, 0x800930D8), which are the executable's own data.
install() builds that RAM from the disc (ISO 9660: SYSTEM.CNF -> the executable, PE.IMG's sector)
and puts stand-ins for pe_weapon_sim's helpers in sys.modules, so `import render_models` works.

    import pe_disc; pe_disc.install(); import render_models as R
"""
import os, struct, sys, types
from pathlib import Path

DISC = os.environ.get('PE_DISC_DIR', '.')                   # folder holding the disc image
NAME = os.environ.get('PE_DISC_NAME', 'Parasite Eve (USA) (Disc %d).bin')
BRIDGE = Path(os.environ.get('PE_BRIDGE_DIR', 'psxrecomp-decomp-bridge'))   # a checkout of the bridge


def sectors(f, start, count):
    out = []
    for lba in range(start, start + count):
        f.seek(lba * 2352); s = f.read(2352)
        if len(s) != 2352 or s[:12] != b'\0' + b'\xff' * 10 + b'\0': raise ValueError('invalid raw sector')
        skip = 24 if s[15] == 2 else 16
        out.append(s[skip:skip + 2048])
    return b''.join(out)


def word(blob, at):
    if at < 0 or at + 4 > len(blob): raise ValueError('word outside asset')
    return struct.unpack_from('<I', blob, at)[0]


def iso_files(f):
    """{NAME: (lba, size)} for every file on the disc (ISO 9660, any depth)."""
    pvd = sectors(f, 16, 1)
    root = pvd[156:156 + 34]
    out, todo = {}, [('', struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])]
    while todo:
        path, lba, size = todo.pop()
        data = sectors(f, lba, (size + 2047) // 2048)
        at = 0
        while at < len(data):
            n = data[at]
            if n == 0:
                at = (at // 2048 + 1) * 2048; continue
            elba, esize, flags = struct.unpack_from('<I', data, at + 2)[0], struct.unpack_from('<I', data, at + 10)[0], data[at + 25]
            name = data[at + 33:at + 33 + data[at + 32]].decode('ascii', 'replace').split(';')[0]
            if name not in ('\0', '\1'):
                full = path + '/' + name if path else name
                if flags & 2: todo.append((full, elba, esize))
                else: out[full.upper()] = (elba, esize)
            at += n
    return out


def build_ram(disc=1):
    f = open(os.path.join(DISC, NAME % disc), 'rb')
    files = iso_files(f)
    cnf_lba, cnf_size = files['SYSTEM.CNF']
    cnf = sectors(f, cnf_lba, 1)[:cnf_size].decode('ascii', 'replace')
    exe_name = cnf.split('cdrom:', 1)[1].lstrip('\\').split(';')[0].strip().upper().replace('\\', '/')
    lba, size = files[exe_name]
    exe = sectors(f, lba, (size + 2047) // 2048)
    base, length = struct.unpack_from('<II', exe, 24)
    ram = bytearray(0x200000)
    o = base & 0x1FFFFF
    ram[o:o + length] = exe[0x800:0x800 + length]
    img = [k for k in files if k.endswith('PE.IMG')]
    if not img: raise ValueError('PE.IMG not found; disc files: %s' % sorted(files))
    struct.pack_into('<I', ram, 0xB0DD8, files[img[0]][0])
    return bytes(ram)


class _Project:
    """PROJECT / 'parasite-eve-recomp/disc/<name>.bin' -> the disc in DISC."""
    def __truediv__(self, rel):
        return Path(DISC) / str(rel).rsplit('/', 1)[-1]


def install():
    if not (BRIDGE / 'tools' / 'pe_assets' / 'build_skin.py').is_file():
        sys.exit('PE_BRIDGE_DIR must point at a checkout of github.com/sirebacon/psxrecomp-decomp-bridge '
                 '(tools/pe_assets/build_skin.py not found under %s)' % BRIDGE)
    if not os.path.isfile(os.path.join(DISC, NAME % 1)):
        sys.exit('Parasite Eve disc 1 not found: set PE_DISC_DIR (and PE_DISC_NAME if it is named '
                 'differently from %r)' % NAME)
    pkg = types.ModuleType('pe_weapon_sim'); pkg.__path__ = []
    ees = types.ModuleType('pe_weapon_sim.extract_enemy_scripts')
    ees.word, ees.sectors, ees.PROJECT, ees.BRIDGE = word, sectors, _Project(), BRIDGE
    ea = types.ModuleType('pe_weapon_sim.extract_animations')
    ea.ram_from_pst = lambda path: build_ram(1)
    sys.modules.update({'pe_weapon_sim': pkg, 'pe_weapon_sim.extract_enemy_scripts': ees,
                        'pe_weapon_sim.extract_animations': ea})
    sys.path.insert(0, str(BRIDGE / 'tools' / 'pe_assets'))


if __name__ == '__main__':
    f = open(os.path.join(DISC, NAME % 1), 'rb')
    for k, v in sorted(iso_files(f).items()): print(k, v)
