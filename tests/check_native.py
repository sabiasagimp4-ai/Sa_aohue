"""Compare compiled C++ with independent NumPy prototype; run after tests/build.ps1."""
import ctypes as C
import sys,time,json
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'prototype'))
from prototype import cavity,chroma
lib=C.CDLL(str(Path('build/core_bridge.dll').resolve()))
array=np.ctypeslib.ndpointer(dtype=np.float32,flags='C_CONTIGUOUS')
lib.mask.argtypes=[array,array,C.c_int,C.c_int,*([C.c_float]*4)];lib.mask.restype=C.c_int
lib.color.argtypes=[array,array,C.c_int,C.c_float]
def native(d,r=24,c=1,detail=.35,edge=.5):
    d=np.ascontiguousarray(d,dtype='float32');o=np.empty_like(d)
    assert lib.mask(d,o,d.shape[1],d.shape[0],r,c,detail,edge)==0
    return o
rng=np.random.default_rng(7123);y,x=np.mgrid[:64,:80]
cases=[np.full(x.shape,.5),.2+x*.002+y*.001,np.where(x<40,.2,.8),.6-.15*np.exp(-((x-40)**2+(y-32)**2)/70),rng.random(x.shape)]
errors=[]
for d in cases:
    d=d.astype('float32')
    for r in [1,6,24,64]:
        ref=cavity(d,r);actual=native(d,r)
        errors.append(float(np.max(abs(ref-actual))))
        assert np.max(abs(ref-actual))<2e-4,(r,errors[-1])
assert np.isfinite(native(np.full((8,8),np.nan))).all()
rgb=rng.uniform(.03,.97,(1000,1,3)).astype('float32');mask=rng.random((1000,1)).astype('float32')
inputs=np.ascontiguousarray(np.concatenate([rgb,mask[...,None]],axis=2));output=np.empty_like(rgb)
lib.color(inputs,output,1000,2)
color_error=float(np.max(abs(output-chroma(rgb,mask,2))))
assert color_error<.001,color_error
lib.color(inputs,output,1000,0);assert np.array_equal(output,rgb)
special=np.array([[-1,.2,.3,1],[4,2,1,1],[np.nan,.2,.3,1]],dtype='float32');sout=np.empty((3,3),dtype='float32')
lib.color(special,sout,3,2);assert np.array_equal(sout,special[:,:3],equal_nan=True)
sample=Path('results/demo04/depth.npy')
if sample.exists(): d=np.load(sample).astype('float32')
else:
    y,x=np.mgrid[:416,:640];d=(.6-.15*np.exp(-((x-320)**2+(y-208)**2)/900)).astype('float32')
start=time.perf_counter();native(d);secs=time.perf_counter()-start
metrics={'mask_max_abs_error':max(errors),'rgb_max_abs_error':color_error,'native_ao_seconds':secs,'dimensions':list(d.shape),'cases':len(errors)}
Path('results').mkdir(exist_ok=True);Path('results/native_metrics.json').write_text(json.dumps(metrics,indent=2));print('PASS',metrics)
