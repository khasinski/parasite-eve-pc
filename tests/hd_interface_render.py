"""Headless real-disc UI/model pack comparison; native state/VRAM/PCM stay exact.

Set PE_TEST_DISC, PE_TEST_HD_BACKGROUNDS (the same pack without UI), and
PE_TEST_UI_PACK (combined pack with UI). Optional PE_TEST_UI_CARD is a copied
PsyZ bu00 fixture whose first slot is the Day 1 M020 save.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main(executable):
    disc, base, pack = (os.getenv(n) for n in ('PE_TEST_DISC','PE_TEST_HD_BACKGROUNDS','PE_TEST_UI_PACK'))
    if not all((disc,base,pack)):
        return 77
    root=Path(tempfile.mkdtemp(prefix='pe-ui-render-'))
    executable=str(Path(executable).resolve());disc=str(Path(disc).resolve())
    title=[(10000,8),(10010,0),(11000,16),(11010,0)]
    dialogue=title+[(11500,16384),(11510,0)]
    for tick in (17000,17600,18200,18800,19400,20000,20600,21200,21800):
        dialogue.extend(((tick,16384),(tick+10,0)))
    for tick,mask in ((23000,8),(23600,8),(24200,16384),(24800,16384)):
        dialogue.extend(((tick,mask),(tick+10,0)))
    menu=title+[(11500,64),(11510,0),(11600,16384),(11610,0)]
    menu_commands=['advance 1000 0','advance 10 16384','advance 10 0','advance 1000 0',
        'advance 10 16384','advance 10 0','advance 1000 0','advance 10 4096','advance 120 0',
        'advance 10 16384','advance 10 0','advance 1000 0','advance 10 4096','advance 200 0','quit']
    cases=[('title',11050,11100,title,['quit']),('dialogue',25000,25100,dialogue,['quit'])]
    card=os.getenv('PE_TEST_UI_CARD')
    if card:
        cases.append(('inventory',12500,20000,menu,menu_commands))

    model_pack=os.getenv('PE_TEST_MODEL_PACK')
    if model_pack and card:
        cases=[('model',12500,20000,menu,menu_commands[:12]+['quit'])]

    def run(name,enabled,at,limit,inputs,commands):
        directory=root/f'{name}-{enabled}';directory.mkdir()
        if name in ('inventory','model'):shutil.copytree(Path(card),directory/'bu00')
        selected=(model_pack if enabled else pack) if name=='model' else (pack if enabled else base)
        env=dict(os.environ,PE_CONFIG_HOME=str(directory/'config'),PE_HD_BACKGROUNDS=str(Path(selected).resolve()),
            SDL_AUDIODRIVER='dummy',PSYZ_AUDIO_CLOCK='video')
        args=[executable,'--disc',disc,'--headless-gpu','--internal-resolution','4','--frame-interpolation','0',
            '--control-at',str(at),'--frame-limit',str(limit),'--dump-frame',str(directory/'frame.ppm'),
            '--dump-vram',str(directory/'vram.bin'),'--dump-audio',str(directory/'audio.pcm')]
        if name=='model' and os.getenv('PE_TEST_MODEL_GRAPHICS')=='1':
            args+=['--geometry-correction','1','--antialiasing','1','--model-filtering','1']
        for tick,mask in inputs:args+=['--pad-at',f'{tick}:{mask}']
        child=subprocess.Popen(args,cwd=directory,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        state=None;index=0
        try:
            with (directory/'game.log').open('w') as log:
                for line in child.stdout:
                    log.write(line)
                    if line.startswith('{"event":"control"'):
                        state=json.loads(line)
                        assert index<len(commands)
                        child.stdin.write(commands[index]+'\n');child.stdin.flush();index+=1
            assert child.wait(timeout=10)==0 and index==len(commands) and state is not None
            return state,tuple(hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in ('vram.bin','audio.pcm')),(directory/'frame.ppm').read_bytes()
        finally:
            if child.poll() is None:child.kill();child.wait()

    for name,at,limit,inputs,commands in cases:
        a=run(name,0,at,limit,inputs,commands);b=run(name,1,at,limit,inputs,commands)
        assert a[:2]==b[:2], f'{name}: native state/VRAM/PCM changed: {root}'
        assert a[2]!=b[2], f'{name}: replacement artwork is missing: {root}'
        if name=='title':assert b[0]['display']['rgb24']==1
        elif name=='model':assert b[0]['actors'] and b[0]['menu'] is None
        elif name=='inventory':assert b[0]['menu'] and b[0]['menu']['selection']==0 and b[0]['actors']
        else:assert b[0]['actors'] and b[0]['display']['rgb24']==0
        print(f'PASS {name}: different rendered UI; exact native state/VRAM/PCM',flush=True)
    (root/'proof.json').write_text(json.dumps(dict(passed=True,cases=[c[0] for c in cases],native_state_vram_pcm_equal=True),indent=2)+'\n')
    print(f'UI renderer evidence: {root}')
    return 0


if __name__=='__main__':
    sys.exit(main(sys.argv[1]))
