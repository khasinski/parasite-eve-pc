"""Real-disc, hidden-GPU regression: install/enable HD in an already loaded scene."""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import struct
import shutil
import sys
import tempfile


def check_inactive_m054(executable, disc, pack, output, timeline, atlas_file, atlas_key):
    """M054's filtered background exposes atlas-placement rounding errors.

    Select its original views through the existing functions in a debug build;
    no new production command or copyrighted test fixture is needed.
    """
    debugger=shutil.which('lldb')
    if not debugger:
        print('M054 inactive-atlas regression requires LLDB and a debug build')
        return 77
    geometry='982a4b5ce1b0a5d8'
    base=output/'m054-base';base.mkdir()
    for source in pack.iterdir():
        if source.is_file() and source.name!=geometry+'-wide.bin':
            try: os.link(source,base/source.name)
            except OSError: shutil.copyfile(source,base/source.name)
    extra=output/'m054-inactive';extra.mkdir()
    for source in base.iterdir():
        try: os.link(source,extra/source.name)
        except OSError: shutil.copyfile(source,extra/source.name)
    shutil.copyfile(atlas_file,extra/atlas_file.name)
    (extra/(geometry+'-wide.bin')).write_bytes(struct.pack('<4sIIIQiiiihh',
        b'PEW1',1,255,1,atlas_key,0,16,0,16,0,0))

    rejected=output/'m054-rejected';rejected.mkdir()
    for source in extra.iterdir():
        if source.name!=geometry+'-wide.bin':
            try: os.link(source,rejected/source.name)
            except OSError: shutil.copyfile(source,rejected/source.name)
    # Allocate the first optional atlas, then reject a missing second one.
    # Recovery must restore the original material allocation and bindings.
    descriptor=(extra/(geometry+'-wide.bin')).read_bytes()
    descriptor=descriptor[:4]+struct.pack('<I',2)+descriptor[8:]
    descriptor+=struct.pack('<IIQiiiihh',254,1,atlas_key+1,0,16,0,16,0,0)
    (rejected/(geometry+'-wide.bin')).write_bytes(descriptor)

    def capture(name, selected):
        work=output/name;work.mkdir()
        (work/'input.txt').write_text('advance 600 0\nadvance 30 0\nadvance 240 0\nquit\n')
        commands=f'''settings set target.disable-aslr false
breakpoint set --name Boot_WriteHostCaptures --one-shot true
process launch --stdin {work}/input.txt
expression -- ((unsigned int *)(void *)&g_ScriptVariableBank)[74] = 536
expression -- ({{ int target = (int)0xa8005248; int *args[] = {{&target}}; ((int (*)(int **))Script_EncodeBarcode)(args); }})
breakpoint set --name Boot_WriteHostCaptures --one-shot true
continue
expression -- (int)Render_LoadGeomState(1)
breakpoint set --name Boot_WriteHostCaptures --one-shot true
continue
expression -- (int)Render_LoadGeomState(0)
expression -- (int)Render_SetScrollMode(160,120,1,0)
breakpoint set --name Boot_WriteHostCaptures --one-shot true
continue
expression -- (unsigned int)g_GeomScroll.state.position.groupSelect
continue
'''
        (work/'debug.lldb').write_text(commands)
        args=[debugger,'-b','-s',str(work/'debug.lldb'),'-o','quit','--',str(Path(executable).resolve()),
              '--disc',str(Path(disc).resolve()),'--headless-gpu','--internal-resolution','4',
              '--widescreen','1','--frame-interpolation','0','--geometry-correction','1',
              '--antialiasing','0','--model-filtering','0','--dynamic-lighting','0',
              '--frame-limit','30000','--control-at','25000','--dump-frame',str(work/'frame.ppm'),
              '--dump-vram',str(work/'vram.bin'),'--dump-audio',str(work/'audio.pcm')]
        for tick,mask in timeline:args+=['--pad-at',f'{tick}:{mask}']
        env=dict(os.environ,SDL_AUDIO_DRIVER='dummy',PSYZ_AUDIO_CLOCK='video',
                 PE_CONFIG_HOME=str(work/'config'),PE_HD_BACKGROUNDS=str(selected))
        with (work/'game.log').open('w') as log:
            result=subprocess.run(args,cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        text=(work/'game.log').read_text()
        states=[json.loads(line) for line in text.splitlines() if line.startswith('{"event":"control"')]
        assert result.returncode==0 and len(states)==4 and states[-1]['scene']==0xa8005248,work
        assert f'HD background geometry {geometry}' in text,work
        groups=re.findall(r'\(unsigned int\) \$\d+ = (\d+)',text)
        assert groups and groups[-1]=='0',f'Expected original view 0: {work}'
        return states,(work/'vram.bin').read_bytes(),(work/'frame.ppm').read_bytes(),hashlib.sha256((work/'audio.pcm').read_bytes()).hexdigest()

    before=capture('m054-before',base);after=capture('m054-after',extra)
    fallback=capture('m054-rejected-capture',rejected)
    assert 'Ignoring incomplete HD wide geometry' in (output/'m054-rejected-capture'/'game.log').read_text()
    checks=dict(states_equal=before[0]==after[0],native_vram_equal=before[1]==after[1],
                rendered_pixels_equal=before[2]==after[2],pcm_equal=before[3]==after[3],
                rejected_sidecar_preserves_original=before==fallback)
    (output/'m054-atlas-order-proof.json').write_text(json.dumps(checks,indent=2)+'\n')
    assert all(checks.values()),f'Inactive M054 atlas changed output: {checks}: {output}'
    print(f'M054 inactive atlas: pixels, native state, VRAM and PCM exact: {output}')
    return 0


def main(executable, inactive_only=False):
    disc, pack = os.getenv('PE_TEST_DISC'), os.getenv('PE_TEST_HD_BACKGROUNDS')
    if not disc or not pack:
        return 77
    pack = str(Path(pack).resolve())
    output = Path(tempfile.mkdtemp(prefix='pe-hd-hot-', dir=Path(executable).resolve().parent))
    # An atlas belonging only to an unused view must not move the original
    # materials within the shared GPU texture and change filtered colours.
    unused = output/'inactive-view-pack'; unused.mkdir()
    for source in Path(pack).iterdir():
        if source.is_file():
            try: os.link(source, unused/source.name)
            except OSError: shutil.copyfile(source, unused/source.name)
    manifest=json.loads((Path(pack)/'manifest.json').read_text())
    geometry=next(r['geometry_key'] for r in manifest['widescreen']
                  if r['room']=='M001' and r['group']==0)
    sidecar=unused/(geometry+'-wide.bin')
    descriptor=sidecar.read_bytes()
    assert descriptor[:4] in (b'PEW1',b'PEW2')
    atlas_key=0xa71a5f00d0c0ffee
    width,height=1088,1088 # 128 cells, including the unused atlas padding
    pixels=bytes((41,73,19))*(width*height)
    bmp=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)
    bmp+=struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)
    (unused/f'{atlas_key:016x}.bmp').write_bytes(bmp+pixels)
    record=struct.pack('<IIQiiii',255,1,atlas_key,0,16,0,16)
    if descriptor[:4]==b'PEW2': record+=struct.pack('<I',0)
    record+=struct.pack('<hh',0,0)
    replacement=descriptor[:4]+struct.pack('<I',struct.unpack_from('<I',descriptor,4)[0]+1)+descriptor[8:]+record
    # Replace the hardlink, never rewrite the reviewed source pack in place.
    sidecar.unlink(); sidecar.write_bytes(replacement)
    timeline = [(10000,8),(10010,0),(11000,16),(11010,0),(11500,16384),(11510,0)]
    for tick in (17000,17600,18200,18800,19400,20000,20600,21200,21800):
        timeline.extend(((tick,16384),(tick+10,0)))
    for tick, mask in ((23000,8),(23600,8),(24200,16384),(24800,16384)):
        timeline.extend(((tick,mask),(tick+10,0)))

    def run(scene, late, selected_pack=pack, suffix=''):
        directory = output / f'{scene}-{late}{suffix}'
        directory.mkdir()
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
                   PE_CONFIG_HOME=str(directory/'config'), PE_HD_BACKGROUNDS='' if late else selected_pack)
        checkpoint = 25000 if scene == 'field' else 9900
        args = [str(Path(executable).resolve()), '--disc', str(Path(disc).resolve()),
                '--headless-gpu', '--internal-resolution', '4', '--widescreen', '1',
                '--frame-limit', str(checkpoint+100), '--control-at', str(checkpoint),
                '--dump-frame', str(directory/'frame.ppm'), '--dump-vram', str(directory/'vram.bin'),
                '--dump-audio', str(directory/'audio.pcm')]
        if scene == 'field':
            for tick, mask in timeline:
                args.extend(('--pad-at', f'{tick}:{mask}'))
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
                    assert state['display']['rgb24'] == (scene == 'title')
                    if scene == 'field':
                        assert state['scene'] == 0xa8000148
                    snapshots.append((state, (directory/'frame.ppm').read_bytes(),
                                      hashlib.sha256((directory/'vram.bin').read_bytes()).hexdigest(),
                                      hashlib.sha256((directory/'audio.pcm').read_bytes()).hexdigest()))
                    step = len(snapshots)
                    command = 'quit' if step == 4 else (
                        f'backgrounds {0 if step == 2 else 1} {selected_pack}' if late else 'advance 8 0')
                    child.stdin.write(command+'\n')
                    child.stdin.flush()
            assert child.wait(timeout=10) == 0 and len(snapshots) == 4
            return snapshots
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()

    if inactive_only:
        return check_inactive_m054(executable,disc,Path(pack),output,timeline,
                                  unused/f'{atlas_key:016x}.bmp',atlas_key)
    baseline=run('field',False)
    inactive=run('field',False,str(unused),'-inactive')
    assert baseline==inactive, f'Unused view changed rendered pixels, state, VRAM or PCM: {output}'
    field_baseline=baseline
    for scene in ('title', 'field'):
        baseline, late = (field_baseline if scene=='field' else run(scene, False)), run(scene, True)
        for i, (a, b) in enumerate(zip(baseline, late)):
            assert a[0] == b[0] and a[2:] == b[2:], f'{scene}: native state/VRAM/PCM changed at {i}: {output}'
            assert (a[1] == b[1]) == (i in (1,3)), f'{scene}: HD activation/toggle ineffective at {i}: {output}'
    (output/'proof.json').write_text(json.dumps(dict(passed=True, scenes=['title','field'],
        late_activation_matches_startup=True, off_on_effective=True, native_state_vram_pcm_equal=True,
        inactive_view_preserves_rendered_pixels=True), indent=2))
    print(f'Late HD activation and off/on pass on title and field; native state/VRAM/PCM exact: {output}')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1],len(sys.argv)>2 and sys.argv[2]=='--inactive-atlas-only'))
