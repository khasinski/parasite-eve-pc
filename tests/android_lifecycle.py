#!/usr/bin/env python3
"""Exercise repeated native sessions in a dedicated headless Android emulator.

Requires an installed debug APK and an already imported disc. Checks the actual
framebuffer and native process retirement, including a normally returning CLI.
"""
import argparse
import hashlib
import json
import shlex
import subprocess
import sys
import time
import uuid
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--disc', default='disc1.cue', help='Imported CUE basename')
p.add_argument('--tls-only', action='store_true', help='Check TLS rejection and preservation of an installed fixture')
p.add_argument('--hd-render', type=Path, help='Check native HD underlays in both aspects; save evidence in a new directory')
p.add_argument('--hd-pack', type=Path, help='Reviewed HD material pack required by --hd-render')
p.add_argument('--native-replay', nargs=argparse.REMAINDER,
               help='Run remaining native test executable arguments on Android')
a = p.parse_args()
if '/' in a.disc or '\\' in a.disc or not a.disc.lower().endswith('.cue'):
    p.error('--disc must be an imported CUE basename')
pkg = 'com.khasinski.parasiteeve'
files = '/data/user/0/' + pkg + '/files/'
owned = []


def adb(*args, check=True):
    return subprocess.run([a.adb, '-s', a.serial, *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def pid(name):
    return adb('shell', 'pidof', name, check=False).strip()


def await_exit(launcher):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        assert pid(pkg) == launcher, 'Native session killed/restarted the launcher process'
        if not pid(pkg+':game'):
            return
        time.sleep(.1)
    raise AssertionError('Native game process did not retire')


def download_tls():
    import http.server
    import io
    import ssl
    import tempfile
    import threading
    import zipfile

    class Server(http.server.ThreadingHTTPServer):
        daemon_threads = True
        def get_request(self):
            connection, address = super().get_request()
            self.connections += 1
            if self.context:
                connection.settimeout(10)
                try:
                    connection = self.context.wrap_socket(connection, server_side=True)
                except ssl.SSLError as error:
                    self.tls_errors.append(str(error)); connection.close(); raise
            return connection, address

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def do_GET(self):
            self.server.requests.append(self.path)
            payload = self.server.catalog if self.path.endswith('/latest') else archive
            self.send_response(200); self.send_header('Content-Length', str(len(payload))); self.end_headers()
            self.wfile.write(payload)

    profile = 'download-tls-'+uuid.uuid4().hex
    relative = 'files/diagnostics/'+profile
    servers = []
    forwarded = []
    try:
        with tempfile.TemporaryDirectory(prefix='pe-android-tls-') as folder:
            root = Path(folder)
            # Installer-only fixture: these bytes are not a rendered texture pack.
            key = '0123456789abcdef'
            payloads = {key+'.webp': b'test atlas', key+'.ref.png': b'test reference'}
            manifest = {'version': '0.1', 'atlases': [{'file': key+'.webp',
                'atlas_sha256': hashlib.sha256(payloads[key+'.webp']).hexdigest(),
                'reference_file': key+'.ref.png',
                'reference_sha256': hashlib.sha256(payloads[key+'.ref.png']).hexdigest()}]}
            payloads['manifest.json'] = json.dumps(manifest).encode()
            data = io.BytesIO()
            with zipfile.ZipFile(data, 'w') as zipped:
                for name, payload in payloads.items(): zipped.writestr(name, payload)
            archive = data.getvalue()
            config = root/'openssl.cnf'
            config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n'
                              '[dn]\nCN=Parasite Eve test only\n[ext]\nsubjectAltName=IP:127.0.0.1\n')
            subprocess.run(['openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-x509',
                            '-sha256', '-days', '1', '-config', str(config), '-keyout', str(root/'key.pem'),
                            '-out', str(root/'cert.pem')], check=True, capture_output=True, timeout=30)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(root/'cert.pem', root/'key.pem')
            for tls in (None, context):
                server = Server(('127.0.0.1', 0), Handler)
                server.context = tls; server.connections = 0; server.requests = []; server.tls_errors = []
                base = ('https' if tls else 'http')+'://127.0.0.1:'+str(server.server_port)
                server.catalog = json.dumps({'version': '0.1', 'url': base+'/upscale-v0.1.zip',
                    'sha256': hashlib.sha256(archive).hexdigest(), 'size': len(archive),
                    'format': 'webp-reference-png'}).encode()
                servers.append(server)
                threading.Thread(target=server.serve_forever, daemon=True).start()
                forward = 'tcp:'+str(server.server_port)
                assert not any(forward in line.split()[1:] for line in
                               adb('reverse', '--list').decode().splitlines()), 'ADB reverse mapping already exists'
                adb('reverse', forward, forward); forwarded.append(forward)
            adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000', '-n', pkg+'/.LauncherActivity')
            launcher = pid(pkg); assert launcher, 'Launcher did not start'
            def run(command, server):
                base = ('https' if server.context else 'http')+'://127.0.0.1:'+str(server.server_port)
                adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity', '--es', 'testProfile', profile,
                    '--es', 'testHdServer', base, '--esa', 'arguments', command)
                await_exit(launcher)
                return adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log').decode()
            first = run('--download-backgrounds', servers[0])
            assert 'Installed.' in first, first
            assert any(path.endswith('.zip') for path in servers[0].requests), 'Fixture was not downloaded'
            pointer_path = relative+'/mods/upscale/active.json'
            before = adb('exec-out', 'run-as', pkg, 'cat', pointer_path)
            pointer = json.loads(before)
            snapshot = {name: adb('exec-out', 'run-as', pkg, 'cat',
                        shlex.quote(pointer['directory']+'/'+name)) for name in payloads}
            assert snapshot == payloads, 'Installed fixture bytes differ'
            failed = run('--download-backgrounds', servers[1])
            print(json.dumps({'tls_connections': servers[1].connections,
                              'tls_handshake_errors': servers[1].tls_errors,
                              'tls_http_requests': servers[1].requests,
                              'native_result': failed}), flush=True)
            assert 'curl 60' in failed, 'Expected actual native certificate-verification failure: '+failed
            assert servers[1].connections > 0 and servers[1].tls_errors, 'No TLS handshake was observed'
            assert not servers[1].requests, 'Untrusted HTTPS endpoint received an HTTP request'
            assert before == adb('exec-out', 'run-as', pkg, 'cat', pointer_path), 'Active pointer changed'
            for name, expected in snapshot.items():
                assert expected == adb('exec-out', 'run-as', pkg, 'cat',
                                       shlex.quote(pointer['directory']+'/'+name)), name
            verified = run('--verify-backgrounds', servers[0])
            assert 'verified' in verified.lower(), verified
            print('PASS: native Android TLS rejects an untrusted certificate; previous pointer and every pack byte preserved')
    finally:
        adb('shell', 'am', 'force-stop', pkg, check=False)
        for forward in forwarded: adb('reverse', '--remove', forward, check=False)
        for server in servers: server.shutdown(); server.server_close()
        adb('shell', 'run-as', pkg, 'rm', '-rf', relative, check=False)


def native_replay(options, profile_files=None, max_seconds=None):
    """Adapter for the existing C replay harness; preserve its assertions."""
    # Accept only finite, prerecorded headless tests. Do not expose arbitrary
    # native commands or interactive control through this batch adapter.
    values = {'--disc', '--frame-limit', '--dump-vram', '--dump-frame', '--dump-audio',
              '--pad-at', '--internal-resolution', '--widescreen'}
    names = {'--dump-vram': 'frame.vram', '--dump-frame': 'frame.ppm', '--dump-audio': 'audio.pcm'}
    flags = {'--headless', '--headless-gpu'}
    profile = 'native-replay-'+uuid.uuid4().hex
    relative = 'files/diagnostics/'+profile
    absolute = '/data/user/0/'+pkg+'/'+relative
    outputs = {}
    remote = []
    limit = None
    mounted = False
    hidden = False
    index = 0
    while index < len(options):
        option = options[index]
        if option in flags:
            hidden = True
            index += 1
            continue
        if option not in values or index+1 >= len(options):
            raise ValueError('Unsupported replay option: '+option)
        value = options[index+1]
        index += 2
        if option == '--disc':
            if mounted:
                raise ValueError('Duplicate --disc')
            value = files+'discs/'+a.disc
            mounted = True
        elif option == '--frame-limit':
            if limit is not None or not value.isdecimal() or not 0 < int(value) <= 1000000:
                raise ValueError('Expected one finite frame limit, at most 1000000')
            limit = int(value)
        elif option in names:
            if option in outputs:
                raise ValueError('Duplicate output: '+option)
            outputs[option] = Path(value)
            value = absolute+'/'+names[option]
        if ',' in value or '\n' in value:
            raise ValueError('Intent array values cannot contain commas or newlines')
        remote += [option, value]
    if not mounted or not hidden or limit is None or not outputs:
        raise ValueError('Replay requires --disc, --headless, --frame-limit and a dump output')
    if Path('bu00').exists():
        raise ValueError('Run in a fresh test directory: bu00 already exists')
    remote += ['--headless-gpu', '--test-android-disc-dialogs']
    try:
        adb('shell', 'run-as', pkg, 'test', '-f', shlex.quote('files/discs/'+a.disc))
        if profile_files is not None:
            import io
            import tarfile
            data = io.BytesIO()
            with tarfile.open(fileobj=data, mode='w') as archive:
                for name, payload in profile_files(absolute).items():
                    assert not name.startswith('/') and '..' not in Path(name).parts
                    entry = tarfile.TarInfo(name)
                    entry.size = len(payload)
                    entry.mode = 0o644
                    archive.addfile(entry, io.BytesIO(payload))
            adb('shell', 'run-as', pkg, 'mkdir', '-p', relative)
            subprocess.run([a.adb, '-s', a.serial, 'shell', '-T', 'run-as', pkg,
                            'tar', '-xf', '-', '-C', relative], input=data.getvalue(),
                           check=True, capture_output=True, timeout=120)
        adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000',
            '-n', pkg+'/.LauncherActivity')
        launcher = pid(pkg)
        assert launcher, 'Launcher did not start'
        start = ['shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
                 '--es', 'testProfile', profile, '--esa', 'arguments', shlex.quote(','.join(remote))]
        if profile_files is not None:
            start += ['--ez', 'testHdBackgrounds', 'true']
        adb(*start)
        deadline = time.monotonic()+(max_seconds if max_seconds is not None else max(60, limit/100))
        while pid(pkg+':game'):
            assert pid(pkg) == launcher, 'Native replay killed the launcher'
            if time.monotonic() >= deadline:
                raise AssertionError('Native replay timed out')
            time.sleep(.25)
        assert pid(pkg) == launcher, 'Native replay killed the launcher'
        log = adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log').decode()
        states = [json.loads(line) for line in log.splitlines()
                  if line.startswith('{"event":"frame_limit"')]
        assert len(states) == 1 and states[0]['vblanks'] == limit, 'Replay did not reach its frame limit'
        for option, destination in outputs.items():
            name = names[option]
            payload = adb('exec-out', 'run-as', pkg, 'cat', relative+'/'+name)
            if option == '--dump-vram':
                assert len(payload) == 1024*512*2, 'Incomplete native VRAM'
            elif option == '--dump-frame':
                assert payload.startswith(b'P6\n'), 'Missing native framebuffer'
            else:
                assert payload and len(payload) % 4 == 0, 'Incomplete stereo PCM'
            destination.write_bytes(payload)
        # The desktop C harness checks the original phone-created save here.
        card = relative+'/memory-cards/bu00/BASLUS-006620000000A'
        # exec-out uses the raw protocol: a remote cat error may still return
        # success locally. Check existence through shell's exit-status protocol.
        exists = subprocess.run([a.adb, '-s', a.serial, 'shell', 'run-as', pkg,
                                 'test', '-f', card], capture_output=True, timeout=30)
        if exists.returncode not in (0, 1):
            raise RuntimeError('Could not inspect the replay card: '+exists.stderr.decode())
        result = (subprocess.run([a.adb, '-s', a.serial, 'exec-out', 'run-as',
                                  pkg, 'cat', card], check=True, capture_output=True, timeout=30)
                  if exists.returncode == 0 else None)
        if result is not None:
            assert len(result.stdout) == 8192, 'Incomplete native phone save'
            directory = Path('bu00')
            directory.mkdir(exist_ok=True)
            (directory/'BASLUS-006620000000A').write_bytes(result.stdout)
        print(log, end='')
        return states[0]
    finally:
        adb('shell', 'am', 'force-stop', pkg, check=False)
        adb('shell', 'run-as', pkg, 'rm', '-rf', relative, check=False)


def hd_render(output):
    """Original fresh-game route; only the display-only backdrop differs."""
    from hd_widescreen import synthetic_underlay
    output = output.resolve()
    output.mkdir()  # Preserve earlier evidence rather than overwrite it.
    pack = a.hd_pack.resolve()
    assert json.loads((pack/'manifest.json').read_text()).get('atlases'), 'HD material pack has no atlases'
    # A native opaque fallback hides a correctly drawn underlay completely.
    # Use the same reviewed material masks as the desktop integration test;
    # compare identical materials with/without the synthetic backdrop.
    materials = {'mods/upscale/fixture/'+source.name: source.read_bytes()
                 for source in pack.iterdir() if source.is_file()
                 and not source.name.endswith(('-wide.bin', '-under.bin'))}
    geometry = '6a8f3dab535c7a8e'  # US Disc 1, first theatre exterior (M002).
    timeline = [(10000, 8), (10010, 0), (11000, 16), (11010, 0), (11500, 16384), (11510, 0)]
    for tick in (17000, 17600, 18200, 18800, 19400, 20000, 20600, 21200, 21800):
        timeline.extend(((tick, 16384), (tick+10, 0)))
    for tick, mask in ((23000, 8), (23600, 8), (24200, 16384), (24800, 16384)):
        timeline.extend(((tick, mask), (tick+10, 0)))
    hashes = {}
    differences = {}
    for wide in (0, 1):
        states = []
        for under in (False, True):
            directory = output/f'{wide}-{int(under)}'
            directory.mkdir()
            def fixture(_absolute):
                # Headless replays deliberately ignore saved graphics options.
                # The debug-only intent opts into this fixed profile-local path.
                files = dict(materials)
                if under:
                    files.update({'mods/upscale/fixture/'+k: v for k, v in synthetic_underlay(geometry).items()})
                return files
            options = ['--disc', a.disc, '--headless-gpu', '--frame-limit', '25000',
                       '--internal-resolution', '4', '--widescreen', str(wide),
                       '--dump-frame', str(directory/'frame.ppm'),
                       '--dump-vram', str(directory/'vram.bin'), '--dump-audio', str(directory/'audio.pcm')]
            for tick, mask in timeline:
                options += ['--pad-at', f'{tick}:{mask}']
            # Software Vulkan at 4x can be slower than the ordinary 1x replay.
            state = native_replay(options, fixture, max_seconds=900)
            assert state['scene'] == 0xa8000148 and state['display']['rgb24'] == 0
            (directory/'state.json').write_text(json.dumps(state, indent=2)+'\n')
            states.append(state)
            hashes[wide, under] = {name: hashlib.sha256((directory/name).read_bytes()).hexdigest()
                                   for name in ('frame.ppm', 'vram.bin', 'audio.pcm')}
        assert states[0] == states[1], 'Underlay changed native game state'
        for name in ('vram.bin', 'audio.pcm'):
            assert hashes[wide, False][name] == hashes[wide, True][name], 'Underlay changed '+name
        assert hashes[wide, False]['frame.ppm'] != hashes[wide, True]['frame.ppm'], 'Underlay was not rendered'
        left = (output/f'{wide}-0/frame.ppm').read_bytes().split(b'\n', 3)
        right = (output/f'{wide}-1/frame.ppm').read_bytes().split(b'\n', 3)
        assert left[:3] == right[:3] and len(left[3]) == len(right[3])
        differences[wide] = sum(left[3][i:i+3] != right[3][i:i+3] for i in range(0, len(left[3]), 3))
        assert differences[wide] > 128, 'Underlay changed too few display pixels'
    proof = {'native_state_vram_pcm_equal': True, 'visible_underlay_both_aspects': True,
             'changed_pixels': differences, 'hashes': {f'{w}-{int(u)}': h for (w, u), h in hashes.items()}}
    (output/'proof.json').write_text(json.dumps(proof, indent=2)+'\n')
    print('PASS: Android HD underlays visible in 4:3 and 16:9; native state, VRAM and PCM unchanged')


if a.hd_render is not None:
    if a.tls_only or a.native_replay is not None: p.error('--hd-render runs separately')
    if a.hd_pack is None: p.error('--hd-render requires --hd-pack with reviewed material masks')
    hd_render(a.hd_render)
    sys.exit(0)

if a.hd_pack is not None:
    p.error('--hd-pack requires --hd-render')


if a.tls_only:
    if a.native_replay is not None: p.error('--tls-only cannot run a native replay')
    download_tls()
    sys.exit(0)

if a.native_replay is not None:
    native_replay(a.native_replay)
    sys.exit(0)


try:
    adb('shell', 'run-as', pkg, 'test', '-f', 'files/discs/' + a.disc)
    adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000',
        '-n', pkg+'/.LauncherActivity')
    launcher = pid(pkg)
    assert launcher, 'Launcher did not start'
    previous = set()
    hashes = []
    for index in range(3):
        name = 'lifecycle-'+uuid.uuid4().hex+'.ppm'
        owned.append(name)
        options = ['--disc', files+'discs/'+a.disc, '--headless-gpu',
                   '--frame-limit', '600', '--dump-frame', files+name]
        adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
            '--esa', 'arguments', ','.join(options))
        game = pid(pkg+':game')
        # A very fast run can already have exited after am start. The output
        # and preserved launcher below remain the authoritative completion proof.
        if game:
            assert game != launcher and game not in previous, 'Native process was reused'
            previous.add(game)
        await_exit(launcher)
        frame = adb('exec-out', 'run-as', pkg, 'cat', 'files/'+name)
        assert frame.startswith(b'P6\n'), 'No native RGB framebuffer'
        hashes.append(hashlib.sha256(frame).hexdigest())
        log = adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log').decode()
        state = next(json.loads(line) for line in log.splitlines()
                     if line.startswith('{"event":"frame_limit"'))
        assert state['vblanks'] == 600
    assert len(set(hashes)) == 1, 'Fresh sessions render different frames at the same native tick'
    # Exercise the player's real Back action as well as diagnostic exits.
    # This uses normal device timing/audio; the emulator itself has no window.
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity', '--es', 'disc', a.disc)
    time.sleep(.5)
    assert pid(pkg+':game'), 'Interactive game did not start'
    adb('shell', 'input', 'keyevent', '4')
    await_exit(launcher)
    top = adb('shell', 'dumpsys', 'activity', 'activities').decode()
    assert any('topResumedActivity' in line and '.LauncherActivity' in line
               for line in top.splitlines()), 'Back did not return to launcher'
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
        '--esa', 'arguments', '--help')
    await_exit(launcher)
    assert b'Usage:' in adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log')
    print('PASS: fresh sessions, identical framebuffers, player Back, normal return and preserved launcher PID')
finally:
    adb('shell', 'am', 'force-stop', pkg, check=False)
    for name in owned:
        adb('shell', 'run-as', pkg, 'rm', '-f', 'files/'+name, check=False)
