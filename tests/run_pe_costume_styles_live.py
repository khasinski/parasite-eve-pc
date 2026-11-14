"""Optional native-game functional smoke test; requires the local pedebug harness.

Use the amphitheater Club 1 save fixture (slot 1), USA discs and generated Guard
assets. Runs in fresh isolated folders and never changes the original card.
These bounded scripted traces test mechanics, not fun, victory or final balance.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import sys
import uuid


def run_case(args, style, game, flows):
    run=args.output/(style+'-'+uuid.uuid4().hex[:10])
    assert not run.exists() and run.parent==args.output
    disabled=['quality of life','rebalance','economy','battle rewards','Mod Catalog','shops','collection','stacked items']
    ini='Manage mods/All mods=1\n'+''.join(f'Manage mods/PE Enhancements: {m}=0\n' for m in disabled)
    ini+='Manage mods/PE Enhancements: wardrobe=1\nManage mods/Warp=0\nManage mods/Quick save=0\nManage mods/Cheats=0\n'
    ini+=f'PE Enhancements/Combat styles/Style={ {"quickstep":1,"bulwark":2}.get(style,0) }\n'  # pe_styles choice
    key='pe1/t_shirt' if style=='quickstep' else 'pe1/guard'
    files={'pe_wardrobe.ini':f'format 1\nimported none\ncostume {key}\nworn {key}\n'.encode()}
    if key=='pe1/guard':
        files.update(('pe_skins/guard/'+p.name,p.read_bytes()) for p in args.guard.glob('*.bin'))
    with game.Session(args.exe,run,cards=[args.card],disc1=str(args.disc1),disc2=str(args.disc2),mods_ini=ini,extra_files=files) as session:
        flows.load_save(session,1)
        for step in range(120):
            if session.state.flags&2:break
            session.advance(10,flows.DIRECTIONS[(4+3*(step//25))%8]|game.CIRCLE)
        else:raise AssertionError('amphitheater encounter did not start')
        samples=[];seen=False
        for i in range(1700):
            state=session.state;detail=state.raw.get('battle_detail',{});player=detail.get('player') or {}
            samples.append({'vblank':state.vblanks,'flags':state.flags,'hp':state.hp,'detail':detail})
            seen=seen or bool(detail.get('enemies'))
            # 0x80 also appears during battle loading: never treat it alone as victory.
            if not state.flags&2 or (seen and state.flags&0x80 and not detail.get('enemies')):break
            buttons=game.CROSS if (player.get('atb',0)>=9000 or state.flags&4) and i%4<2 else 0
            session.advance(2,buttons)
        (run/'samples.json').write_text(json.dumps(samples),encoding='utf-8')
    events=[line for line in (run/'pe_play.log').read_text(errors='replace').splitlines() if line.startswith('costume_style ')]
    increments=[]
    for a,b in zip(samples,samples[1:]):
        x=(a['detail'].get('player') or {}).get('atb',0)
        y=(b['detail'].get('player') or {}).get('atb',0)
        if 0<x<y<9000 and y-x<1000 and b['vblank']-a['vblank']==2 and a['detail']['frame']+1==b['detail']['frame']:
            increments.append(y-x)
    assert increments, 'no eligible charge samples'
    result={'run':str(run),'samples':len(samples),'mean_charge_step':sum(increments)/len(increments),
            'common_charge_steps':Counter(increments).most_common(4),'hp_at_trace_end':samples[-1]['hp'],'events':events,
            'coverage':'bounded functional trace; victory and balance not asserted'}
    def pairs(tag):
        return [tuple(map(int,re.search(r'loss=(\d+)->(\d+)',line).groups())) for line in events if tag in line]
    if style=='quickstep':
        # Revision 6.4: half-HP breakpoint or non-final kill, 20% of a full bar
        # (less only when the bar clamps at full); Overextended x1.20 in the burst.
        assert any('begin=quickstep' in line for line in events),result
        refunds=[int(re.search(r'refund=(\d+)',line).group(1)) for line in events if 'quickstep refund=' in line]
        assert refunds and all(0<r<=1800 for r in refunds),result
        over=pairs('overextended loss=')
        assert all(after==max(1,(before*120+50)//100) for before,after in over),result
        result['quickstep_refunds']=refunds;result['overextended']=over
    elif style=='bulwark':
        # Revision 6.4 single guard: x0.75 guarded, x1.15 Exposed on a full bar.
        guarded=pairs('bulwark guarded loss=');exposed=pairs('bulwark exposed loss=')
        assert guarded,result
        assert all(after==max(1,(before*75+50)//100) for before,after in guarded),result
        assert all(after==max(1,(before*115+50)//100) for before,after in exposed),result
        stores=[int(re.search(r'store=(\d+)',line).group(1)) for line in events if 'bulwark guarded loss=' in line]
        assert stores and stores[0]>0,result
        # Counter within the +50% per-hit cap; Rally never heals more than the bonus.
        counters=[tuple(map(int,re.search(r'damage=(\d+)->(\d+)',line).groups())) for line in events if 'counter damage=' in line]
        assert all(before<after<=before+(before*50+99)//100 for before,after in counters),result
        rallies=[(int(re.search(r'damage=(\d+)->(\d+)',l).group(2))-int(re.search(r'damage=(\d+)',l).group(1)),int(re.search(r'rally=(\d+)',l).group(1))) for l in events if 'counter damage=' in l]
        assert all(heal<=bonus for bonus,heal in rallies),result
        result.update(guarded=guarded,exposed=exposed,counters=counters,rally=rallies)
    else:
        assert any('begin=original enabled=0' in line for line in events),result
        assert not any('refund=' in line or 'loss=' in line for line in events),result
    return style,result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for option in ('exe','harness','card','guard','disc1','disc2','output'):
        ap.add_argument('--'+option,required=True,type=Path)
    args=ap.parse_args()
    for option in ('exe','harness','card','guard','disc1','disc2'):
        setattr(args,option,getattr(args,option).resolve(strict=True))
    args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    before=hashlib.sha256(args.card.read_bytes()).hexdigest()
    sys.path.insert(0,str(args.harness))
    from pedebug import game,flows
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures=[pool.submit(run_case,args,name,game,flows) for name in ('quickstep','bulwark','vanilla')]
        results=dict(f.result() for f in futures)
    native=results['vanilla']['mean_charge_step']
    # Revision 6.4: no style changes AT charge speed.
    assert abs(results['bulwark']['mean_charge_step']/native-1)<0.005,results
    assert abs(results['quickstep']['mean_charge_step']/native-1)<0.005,results
    assert hashlib.sha256(args.card.read_bytes()).hexdigest()==before
    (args.output/'functional-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(results,indent=2))

if __name__=='__main__':main()
