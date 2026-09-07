"""Run: python prototype/check.py. Synthetic invariants, not real 3D ground truth."""
import json
from pathlib import Path
import numpy as np
from prototype import cavity,chroma,lab,sheet,infer
from PIL import Image

out=Path('results/check');out.mkdir(parents=True,exist_ok=True)
y,x=np.mgrid[:128,:192]; flat=np.full(x.shape,.6)
small=.6-.15*np.exp(-((x-48)**2+(y-64)**2)/36)
large=.6-.15*np.exp(-((x-136)**2+(y-64)**2)/900)
d=small+large-.6
c6=cavity(d,6);c32=cavity(d,32)
assert cavity(flat).max()==0
assert cavity(.2+x*.001+y*.001)[3:-3,3:-3].max()<1e-6
assert c32[64,136]>c6[64,136]*2
assert c6[64,48]>c6[64,136]*2
step=np.where(x<96,.2,.8)
assert cavity(step)[4:-4,4:-4].max()<1e-6
rgb=np.broadcast_to([.4,.3,.2],(*x.shape,3)).copy()
rgb[(x>20)&(x<50)&(y>15)&(y<110)]*=.05
rgb*=np.where(x[...,None]>100,.35,1.)
assert np.array_equal(chroma(rgb,c32,0),rgb)
result=chroma(rgb,c32,2)
l0,l1=lab(rgb),lab(result)
Lerror=float(np.max(abs(l0[...,0]-l1[...,0])))
herror=float(np.max(abs(np.arctan2(l0[...,2],l0[...,1])-np.arctan2(l1[...,2],l1[...,1]))))
assert Lerror<1e-6 and herror<1e-5
assert np.isfinite(cavity(np.full((8,8),np.nan))).all()
sheet(out/'synthetic.png',[('Dark pattern / shadow',rgb),('Known flat depth',flat),('Known flat cavity',cavity(flat)),('Two recesses',d),('Radius 6',c6),('Radius 32',c32)])
# AI out-of-distribution challenge: a flat colored card with black paint and shadow.
ai,_=infer(Image.fromarray(np.uint8(rgb*255)))
ca=cavity(ai,24)
sheet(out/'ai_flat_challenge.png',[('Flat card',rgb),('AI depth',ai),('AI cavity (should be black)',ca)])
metrics={'small_center_r6':float(c6[64,48]),'large_center_r6':float(c6[64,136]),'large_center_r32':float(c32[64,136]),'L_error':Lerror,'hue_radians_error':herror,'AI_flat_false_positive_mean':float(ca.mean()),'AI_flat_fraction_above_0.1':float((ca>.1).mean())}
(out/'metrics.json').write_text(json.dumps(metrics,indent=2));print('PASS',metrics)
