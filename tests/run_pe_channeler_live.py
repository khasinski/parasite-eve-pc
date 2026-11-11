"""Native Channeler checks on isolated copied-card amphitheater fixtures.

Lower PE and equipment selection affect test copies only. No balance is asserted.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import uuid


def run_case(args,name,game,flows,card):
    on=name!='vanilla';gun=name=='channeler-gun'
    run=args.output/(name+'-'+uuid.uuid4().hex[:8]);assert not run.exists()
    copied=card.Card.load(args.card)
    maximum=struct.unpack_from('<i',copied.block,0x1c24)[0]
    assert maximum>0
    struct.pack_into('<i',copied.block,0x1c04,maximum//4)
    for slot in range(50):
        item=struct.unpack_from('<h',copied.block,0x168+slot*2)[0]
        if 0x100<=item<0x180:
            kind=copied.block[0x1cc+(item-0x100)*32+6]
            wanted=(1<=kind<8) if gun else kind==8
            if wanted:
                copied.block[0x140]=slot;break
    else:raise AssertionError('fixture lacks requested weapon')
    copied.fix_crc()
    fixture=args.output/('fixture-'+uuid.uuid4().hex[:8]);fixture.mkdir()
    fixture_card=fixture/args.card.name;copied.save(fixture_card)
    ini='Manage mods/All mods=1\n'
    for mod in ('quality of life','rebalance','economy','battle rewards','Mod Catalog','shops','collection','stacked items'):
        ini+=f'Manage mods/PE Enhancements: {mod}=0\n'
    ini+='Manage mods/PE Enhancements: wardrobe=1\nManage mods/Warp=0\nManage mods/Quick save=0\nManage mods/Cheats=0\n'
    ini+=f'PE Enhancements/Collection/Wardrobe/Costume abilities (prototype)={int(on)}\n'
    files={'pe_wardrobe.ini':b'format 1\nimported none\ncostume pe1/evening_dress\nworn pe1/evening_dress\n'}
    with game.Session(args.exe,run,cards=[fixture_card],disc1=str(args.disc1),disc2=str(args.disc2),mods_ini=ini,extra_files=files,log=lambda x:None) as s:
        flows.load_save(s,1)
        for step in range(120):
            if s.state.flags&2:break
            s.advance(10,flows.DIRECTIONS[(4+3*(step//25))%8]|game.CIRCLE)
        else:raise AssertionError('encounter did not start')
        samples=[];seen=False
        for i in range(1800):
            st=s.state;detail=st.raw.get('battle_detail',{});player=detail.get('player') or {}
            samples.append({'vblank':st.vblanks,'hp':st.hp,'flags':st.flags,'detail':detail})
            seen=seen or bool(detail.get('enemies'))
            if not st.flags&2 or (seen and st.flags&0x80 and not detail.get('enemies')):break
            # First collect native passive recovery; then issue ordinary attacks.
            buttons=game.CROSS if i>=150 and (player.get('atb',0)>=9000 or st.flags&4) and i%4<2 else 0
            s.advance(2,buttons)
        (run/'samples.json').write_text(json.dumps(samples),encoding='utf-8')
        s.shot('trace-end')
    events=[line for line in (run/'pe_play.log').read_text(errors='replace').splitlines() if line.startswith('costume_style ')]
    # Revision 5 Spellweave: completed damaging weapon queues add charges; no PE refund.
    charges=[int(re.search(r'charges=(\d+)$',line).group(1)) for line in events if 'spellweave charges=' in line]
    refunds=[line for line in events if 'refund=' in line]
    passive=[]
    for a,b in zip(samples,samples[1:]):
        x=a['detail'].get('player') or {};y=b['detail'].get('player') or {}
        if b['vblank']-a['vblank']!=2 or b['detail']['frame']-a['detail']['frame']!=1 or not x or not y:continue
        delta=y['pe_raw']-x['pe_raw'];step=y['pe_recovery_step']
        if 0<delta<=step and y['pe_raw']<y['pe_max_raw']:
            passive.append((delta,step))
    assert len(passive)>=10,'insufficient passive recovery samples'
    if on:
        assert any('begin=channeler' in line and 'ready=1' in line for line in events),events
        assert charges,'no Spellweave charge from a completed damaging action'
        assert charges==list(range(1,len(charges)+1)) and max(charges)<=3,charges
        assert not refunds,refunds
        assert all(abs(delta*2-step)<=1 for delta,step in passive),passive[:10]
    else:
        assert not charges and not refunds,events
        assert all(delta==step for delta,step in passive),passive[:10]
    return name,{'run':str(run),'samples':len(samples),'passive_samples':len(passive),'passive_examples':passive[:8],
        'charges':charges,'events':events,'coverage':'bounded functional trace; casts covered by unit tests; balance not asserted'}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','harness','card','disc1','disc2','output'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--cases',nargs='+',choices=('channeler-club','channeler-gun','vanilla'),default=['channeler-club','channeler-gun','vanilla'])
    args=ap.parse_args()
    for name in ('exe','harness','card','disc1','disc2'):setattr(args,name,getattr(args,name).resolve(strict=True))
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    original_hash=hashlib.sha256(args.card.read_bytes()).hexdigest()
    sys.path.insert(0,str(args.harness))
    from pedebug import game,flows,card
    with ThreadPoolExecutor(max_workers=3) as pool:
        results=dict(f.result() for f in [pool.submit(run_case,args,name,game,flows,card) for name in args.cases])
    assert hashlib.sha256(args.card.read_bytes()).hexdigest()==original_hash
    (args.output/'functional-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))

if __name__=='__main__':main()
