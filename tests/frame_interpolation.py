"""Real-game 60 Hz presentation: native state, VRAM and audio must remain exact.
Set PE_TEST_DISC; optional PE_TEST_HD_BACKGROUNDS adds HD widescreen coverage.
No visible windows are opened.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main(executable):
    disc = os.getenv('PE_TEST_DISC')
    if not disc:
        return 77
    out = Path(tempfile.mkdtemp(prefix='pe-60hz-'))
    timeline = [(10000,8),(10010,0),(11000,16),(11010,0),(11500,16384),(11510,0)]
    for tick in (17000,17600,18200,18800,19400,20000,20600,21200,21800):
        timeline.extend(((tick,16384),(tick+10,0)))
    for tick, mask in ((23000,8),(23600,8),(24200,16384),(24800,16384)):
        timeline.extend(((tick,mask),(tick+10,0)))

    def run(enabled, wide, pack, scale, geometry, aa):
        directory = out / f'{wide}-{scale}-{geometry}-{aa}-{enabled}'
        directory.mkdir()
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
                   PE_HD_BACKGROUNDS=pack, PE_CONFIG_HOME=str(directory/'config'))
        args = [str(Path(executable).resolve()), '--disc', str(Path(disc).resolve()),
                '--headless-gpu', '--internal-resolution', str(scale), '--widescreen', str(wide),
                '--geometry-correction', str(geometry), '--antialiasing', str(aa),
                '--frame-interpolation', str(enabled), '--frame-limit', '25100', '--control-at', '25000',
                '--dump-frame', str(directory/'frame.ppm'), '--dump-vram', str(directory/'vram.bin'),
                '--dump-audio', str(directory/'audio.pcm')]
        for tick, mask in timeline:
            args += ['--pad-at', f'{tick}:{mask}']
        child = subprocess.Popen(args, cwd=directory, env=env, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        snapshots = []
        try:
            with (directory/'game.log').open('w') as log:
                for line in child.stdout:
                    log.write(line)
                    if not line.startswith('{"event":"control"'):
                        continue
                    state = json.loads(line)
                    assert state['scene'] == 0xa8000148 and state['display']['rgb24'] == 0
                    frame = (directory/'frame.ppm').read_bytes()
                    (directory/f'{len(snapshots)}.ppm').write_bytes(frame)
                    snapshots.append((state, frame,
                        hashlib.sha256((directory/'vram.bin').read_bytes()).hexdigest(),
                        hashlib.sha256((directory/'audio.pcm').read_bytes()).hexdigest()))
                    child.stdin.write(('quit' if len(snapshots) == 9 else 'advance 1 0')+'\n')
                    child.stdin.flush()
            assert child.wait(timeout=10) == 0 and len(snapshots) == 9
            return snapshots
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()

    cases = [(0, '', 4, 1, 0), (0, '', 1, 0, 0)]
    pack = os.getenv('PE_TEST_HD_BACKGROUNDS')
    if pack:
        cases.append((1, str(Path(pack).resolve()), 4, 1, 1))
    for case in cases:
        original, smooth = run(0, *case), run(1, *case)
        for i, (a, b) in enumerate(zip(original, smooth)):
            assert a[0] == b[0] and a[2:] == b[2:], f'Native state/VRAM/PCM changed: {case}, {i}: {out}'
            if i % 2 == 0:
                assert a[1] == b[1], f'Original display frame changed: {case}, {i}: {out}'
            elif case[2] > 1:
                assert a[1] != b[1], f'Midpoint is missing: {case}, {i}: {out}'
            if i:
                assert (a[1] != original[i-1][1]) == (i % 2 == 0), f'Original cadence changed: {out}'
                if case[2] > 1:
                    assert b[1] != smooth[i-1][1], f'Interpolated frame repeated: {out}'
        print(f'PASS {case}: exact native frames/state/VRAM/PCM; additional midpoint frames', flush=True)
    (out/'proof.json').write_text(json.dumps(dict(passed=True, cases=cases, checkpoints=9,
        native_state_vram_pcm_equal=True, original_frames_exact=True, midpoint_cadence=True), indent=2)+'\n')
    print(f'60 Hz evidence: {out}')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
