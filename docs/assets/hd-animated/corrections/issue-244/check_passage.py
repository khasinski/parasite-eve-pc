"""Content regression: M116's left passage is floor, never closed vertical bars."""
from pathlib import Path
import argparse,json
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('previews',type=Path);p.add_argument('--report',type=Path);a=p.parse_args()
def measure(path):
 im=Image.open(path).convert('RGB');scale=im.width//320
 assert scale and im.size==(320*scale,256*scale)
 rgb=np.array(im)[84*scale:118*scale,147*scale:168*scale].astype(float)
 luminance=rgb@np.array([.2126,.7152,.0722]);column=luminance.mean(0);mean=float(luminance.mean())
 return dict(mean=mean,column_contrast=float(column.std()/max(mean,1)),edge_ratio=float(np.abs(np.diff(column)).max()/max(mean,1)))
records=[]
for control,count in [(0,4),(1,6)]:
 for phase in range(count):
  stem=f'M116-g00-c{control}-p{phase:02d}'
  native=measure(a.previews/(stem+'-native.png'));hd=measure(a.previews/(stem+'-hd.png'))
  # The native aperture has a gentle floor-lighting gradient. Repeated dark
  # bars add a strong column contrast and sharp steps absent from that source.
  assert native['column_contrast']<.2 and native['edge_ratio']<.15, 'source aperture is not the expected open passage'
  records.append(dict(phase=stem,native=native,hd=hd,passed=hd['column_contrast']<.2 and hd['edge_ratio']<.15))
report=dict(passed=all(r['passed'] for r in records),phases=records)
if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(passed=report['passed'],phases=len(records),failed=[r['phase'] for r in records if not r['passed']])))
raise SystemExit(0 if report['passed'] else 1)
