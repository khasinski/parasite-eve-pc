"""M116 content regression: an open passage and the original alarm-light cycle."""
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
def light_curve(kind):
 values=[]
 for i in range(6):
  im=Image.open(a.previews/f'M116-g00-c1-p{i:02d}-{kind}.png').convert('RGB');s=im.width//320
  rgb=np.array(im)[61*s:74*s,180*s:231*s].astype(float).mean((0,1))
  values.append(float(rgb[0]-(rgb[1]+rgb[2])/2))
 return np.array(values)
native_light=light_curve('native');hd_light=light_curve('hd')
amplitude=float(np.ptp(hd_light)/max(np.ptp(native_light),1))
correlation=float(np.corrcoef(native_light,hd_light)[0,1]) if np.ptp(hd_light)>.01 else 0
lighting=dict(native_curve=native_light.tolist(),hd_curve=hd_light.tolist(),amplitude_ratio=amplitude,correlation=correlation,passed=.8<amplitude<1.25 and correlation>.98)
report=dict(passed=all(r['passed'] for r in records) and lighting['passed'],phases=records,lighting=lighting)
if a.report:a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(passed=report['passed'],phases=len(records),failed=[r['phase'] for r in records if not r['passed']],lighting=lighting)))
raise SystemExit(0 if report['passed'] else 1)
