#!/usr/bin/env python3
"""Builds the PC harness: the Dreamcast build of the game (dc_main.c, the
PsyZ Dreamcast backend, every game source) compiled for 32-bit x86, with
pthreads in place of KallistiOS threads, a released pad in place of Maple
and a software PVR (fakepvr.c).

usage: build.py DC_BUILD_DIR [OUT_DIR]
  DC_BUILD_DIR  a configured Dreamcast (Ninja) build: its compile commands
                pick the sources and flags
  OUT_DIR       where objects and the dchost binary go (default: ./pchost-out)
Needs gcc with -m32 support (gcc-multilib). See README.md."""
import json, os, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

here = os.path.dirname(os.path.abspath(__file__))
psyz_src = os.path.normpath(os.path.join(here, '..', '..', '..', 'third_party', 'psyz', 'psyz', 'src'))
if len(sys.argv) < 2:
    sys.exit(__doc__)
build = os.path.abspath(sys.argv[1])
out = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else 'pchost-out')
os.makedirs(os.path.join(out, 'obj'), exist_ok=True)
db = json.loads(subprocess.run(['ninja', '-C', build, '-t', 'compdb'], check=True,
                               capture_output=True, text=True).stdout)
replace = {'dc_os.c': os.path.join(here, 'host_os.c'), 'dc_input.c': os.path.join(here, 'host_input.c'),
           'dc_audio.c': os.path.join(here, 'host_audio.c'), 'dc_aica.c': os.path.join(here, 'host_aica.c')}
# DCHOST_SPU_REFERENCE=1: the portable software SPU makes the sound instead
reference = bool(os.environ.get('DCHOST_SPU_REFERENCE'))
if reference:
    replace['dc_spu.c'] = os.path.join(psyz_src, 'psyz', 'psyz_spu.c')
base_flags = ['gcc', '-m32', '-O2', '-g', '-I' + os.path.join(here, 'fake')]
jobs, objs = [], []
for e in db:
    f = e['file']
    if not f.endswith('.c') or f.endswith('dc_libc.c'):  # glibc has flockfile
        continue
    name = os.path.basename(f)
    f = replace.get(name, f)
    keep, skip = [], False
    for a in shlex.split(e['command'])[1:]:
        if skip:
            skip = False
        elif a in ('-MT', '-MF', '-o'):
            skip = True
        elif a in ('-c', '-MD', '-O3', '-fomit-frame-pointer') or a.startswith('--sysroot') or a == e['file']:
            pass
        else:
            keep.append(a)
    if name == 'dc_main.c':
        keep.append('-Dmain=dc_main')
    if name == 'dc_os.c':
        keep.append('-I' + psyz_src)
    if name == 'dc_audio.c' and reference:
        keep.append('-DHOST_AUDIO_REFERENCE')
    if name == 'dc_gpu.c' and os.environ.get('DCGPU_TRACE'):
        keep.append('-DDCGPU_TRACE')
    obj = os.path.join(out, 'obj', e['output'].replace('/', '_') + '.o')
    objs.append(obj)
    jobs.append((base_flags + keep + ['-c', f, '-o', obj], e['directory'], f, obj))
repo = os.path.normpath(os.path.join(here, '..', '..', '..'))
for extra in ('fakepvr.c', 'harness_main.c'):
    obj = os.path.join(out, 'obj', extra + '.o')
    objs.append(obj)
    inc = ['-I' + os.path.join(repo, 'include'), '-I' + os.path.join(repo, 'third_party/psyz/psyz/include'), '-D__psyz']
    jobs.append((base_flags + inc + ['-c', os.path.join(here, extra), '-o', obj], here, os.path.join(here, extra), obj))

def run(job):
    cmd, cwd, src, obj = job
    if os.path.exists(obj) and os.path.getmtime(obj) > os.path.getmtime(src):
        return None
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return (src, r.stderr) if r.returncode else None

fails = [x for x in ThreadPoolExecutor(os.cpu_count() or 2).map(run, jobs) if x]
for src, err in fails:
    print('FAILED', src)
    print('\n'.join(l for l in err.splitlines() if 'error' in l)[:2000])
if fails:
    sys.exit(1)
r = subprocess.run(['gcc', '-m32', '-o', os.path.join(out, 'dchost')] + objs + ['-lm', '-lpthread'],
                   capture_output=True, text=True)
sys.stdout.write(r.stderr[-3000:])
sys.exit(r.returncode)
