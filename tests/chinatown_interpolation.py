"""Replay a one-pixel camera step across the M163 restaurant in the retail game."""
import binascii
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOM = 0xA80261C8
CONTINUE = [(10000, 8), (11000, 16), (11500, 64), (11600, 16384),
            (13500, 16384), (14520, 16384), (15540, 4096), (15670, 16384)]


def restaurant_rows(frame, dx=0, dy=0):
    magic, dimensions, maximum, pixels = frame.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert magic == b'P6' and maximum == b'255' and len(pixels) == 3 * width * height
    left, right = width * 68 // 100 + dx, width * 80 // 100 + dx
    return b''.join(pixels[(y * width + left) * 3:(y * width + right) * 3]
                    for y in range(height // 10 + 12 + dy, height // 3 + dy))


def run(game, disc, pack, work):
    # Keep the native Continue path, changing only the destination and CRC of a copy.
    save = 'BASLUS-006620000000A'
    card = bytearray((Path(__file__).resolve().parents[1] / 'save-editor/samples/bu00' / save).read_bytes())
    struct.pack_into('<h', card, 0x15e, 163)
    struct.pack_into('<I', card, 0x1be8, ROOM)
    struct.pack_into('<I', card, 0x1c8c, 0)
    struct.pack_into('<I', card, 0x1c8c, binascii.crc_hqx(card, 0xffff) ^ 0xffff)
    (work / 'bu00').mkdir()
    (work / 'bu00' / save).write_bytes(card)
    image = work / 'frame.ppm'
    env = dict(os.environ, PE_CONFIG_HOME=str(work / 'config'), PE_HD_BACKGROUNDS=pack,
               SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video')
    args = [game, '--disc', disc, '--headless-gpu', '--internal-resolution', '3',
            '--frame-interpolation', '1', '--frame-limit', '21100', '--control-at', '21000',
            '--dump-frame', str(image)]
    for tick, button in CONTINUE:
        args.extend(['--pad-at', f'{tick}:{button}', '--pad-at', f'{tick + 10}:0'])
    frames = []
    with subprocess.Popen(args, cwd=work, env=env, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as child:
        try:
            for line in child.stdout:
                if not line.startswith('{"event":"control"'):
                    continue
                state = json.loads(line)
                assert state['scene'] == ROOM
                frames.append(image.read_bytes())
                command = ('advance 42 16', 'advance 1 16', 'advance 1 16', 'quit')[len(frames)-1]
                child.stdin.write(command + '\n')
                child.stdin.flush()
            assert child.wait() == 0 and len(frames) == 4
        finally:
            if child.poll() is None:
                child.kill()
    previous, midpoint, following = frames[1:]
    strip = restaurant_rows(midpoint)
    assert restaurant_rows(previous) != restaurant_rows(following), 'Camera did not scroll'
    assert any(restaurant_rows(previous, dy=offset) == strip for offset in (-2, -1, 1, 2)), \
        'Scrolling HD facade is not an intact translated image at the midpoint'


if __name__ == '__main__':
    disc, pack = os.getenv('PE_TEST_DISC'), os.getenv('PE_TEST_HD_BACKGROUNDS')
    if not disc or not pack:
        sys.exit(77)
    with tempfile.TemporaryDirectory(prefix='pe-m163-') as folder:
        run(str(Path(sys.argv[1]).resolve()), str(Path(disc).resolve()), str(Path(pack).resolve()), Path(folder))
    print('PASS: intact M163 HD facade during an odd-scale midpoint')
