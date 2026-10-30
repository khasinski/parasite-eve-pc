"""Capture Aya through the original game's headless Continue path."""
import argparse,json,os,shutil,subprocess,tempfile,hashlib
from pathlib import Path

def capture(game,disc,card,output,emit=True):
    game=game.resolve();disc=disc.resolve();card=card.resolve();output=output.resolve()
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='pe-lighting-capture-') as tmp:
        work=Path(tmp);(work/'bu00').mkdir()
        shutil.copyfile(card,work/'bu00/BASLUS-006620000000A')
        args=[str(game),'--disc',str(disc),'--headless-gpu','--internal-resolution','1',
              '--frame-limit','21000','--control-at','12500','--dump-vram',str(work/'native.vram'),'--dump-audio',str(work/'audio.pcm')]
        for tick,buttons in [(10000,8),(10010,0),(11000,16),(11010,0),(11500,64),(11510,0),(11600,16384),(11610,0)]:
            args+=['--pad-at',f'{tick}:{buttons}']
        env=dict(os.environ,SDL_AUDIODRIVER='dummy',PSYZ_AUDIO_CLOCK='video',PE_HD_BACKGROUNDS='',PE_CONFIG_HOME=str(work/'config'))
        commands=['advance 1000 0','advance 10 16384','advance 10 0','advance 1000 0','advance 10 16384',
                  'advance 10 0','advance 1000 0','advance 10 4096','advance 120 0','advance 10 16384',
                  'advance 10 0','advance 1000 0',f'lighting-export {output}' if emit else 'advance 1 0','quit']
        if len(str(output))>=500:raise ValueError('Output path too long for game control console')
        child=subprocess.Popen(args,cwd=work,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        index=0
        try:
            with output.with_suffix('.log' if emit else '.baseline.log').open('w') as log:
                for line in child.stdout:
                    log.write(line)
                    if line.startswith('{"event":"control"'):
                        state=json.loads(line)
                        if index==12 and not state['actors']:raise RuntimeError('Save did not load a field scene')
                        child.stdin.write(commands[index]+'\n');child.stdin.flush();index+=1
                if child.wait(timeout=10) or index!=len(commands):raise RuntimeError('Original game capture failed; see capture log')
        finally:
            if child.poll() is None:child.kill();child.wait()
        proof=dict(state=state,vram_sha256=hashlib.sha256((work/'native.vram').read_bytes()).hexdigest(),pcm_sha256=hashlib.sha256((work/'audio.pcm').read_bytes()).hexdigest())
    if not emit:return proof
    model=json.loads(output.read_text())
    assert model['vertices'] and model['faces'] and Path(str(output)+'.vram').stat().st_size==1048576
    print(f"Captured original Aya: {len(model['vertices'])} vertices, {len(model['faces'])} faces -> {output}")
    return proof

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,required=True);p.add_argument('--disc',type=Path,required=True)
    p.add_argument('--card',type=Path,required=True,help='A PsyZ save file with field Aya (opening black dress recommended)');p.add_argument('--output',type=Path,required=True)
    p.add_argument('--verify',action='store_true',help='Compare native state, VRAM and PCM with an unmodified replay')
    a=p.parse_args()
    baseline=capture(a.game,a.disc,a.card,a.output,False) if a.verify else None
    result=capture(a.game,a.disc,a.card,a.output)
    if baseline is not None:
        assert result==baseline,'Export changed original state, VRAM or PCM'
        a.output.with_suffix('.proof.json').write_text(json.dumps(dict(native_state_vram_pcm_equal=True,vram_sha256=result['vram_sha256'],pcm_sha256=result['pcm_sha256']),indent=2)+'\n')
        print('PASS: export preserves native game state, VRAM and PCM')
