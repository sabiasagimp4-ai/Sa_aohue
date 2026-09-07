"""Compare compiled C++ (Line Art + chroma) with an independent NumPy reference.
Run after tests/build.ps1."""
import ctypes as C
import sys,time,json
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'prototype'))
from prototype import chroma
lib=C.CDLL(str(Path('build/core_bridge.dll').resolve()))
array=np.ctypeslib.ndpointer(dtype=np.float32,flags='C_CONTIGUOUS')
lib.line.argtypes=[array,array,C.c_int,C.c_int,C.c_float,C.c_float,C.c_float,C.c_int];lib.line.restype=C.c_int
lib.color.argtypes=[array,array,C.c_int,C.c_float]

def small_disc(v,radius):
    h,w=v.shape;ir=int(np.ceil(radius))
    yy,xx=np.mgrid[:h,:w]
    acc=np.zeros((h,w));wsum=np.zeros((h,w))
    for dy in range(-ir,ir+1):
        for dx in range(-ir,ir+1):
            rn2=(dx*dx+dy*dy)/(radius*radius)
            if rn2>1: continue
            aa=np.clip((1-np.sqrt(rn2))*radius,0,1)
            if aa<=0: continue
            sx=np.clip(xx+dx,0,w-1);sy=np.clip(yy+dy,0,h-1)
            acc+=v[sy,sx]*aa;wsum+=aa
    return np.where(wsum>0,acc/wsum,v)

def box_pass_1d(v,r):
    n=len(v);csum=np.concatenate([[0.],np.cumsum(v)])
    idx=np.arange(n);lo=np.maximum(0,idx-r);hi=np.minimum(n-1,idx+r)
    return (csum[hi+1]-csum[lo])/(hi-lo+1)

def fast_blur(v,radius):
    if radius<.5: return v.copy()
    r=max(1,int(round(radius/np.sqrt(3))))
    v=v.copy()
    for _ in range(3):
        v=np.apply_along_axis(lambda row:box_pass_1d(row,r),1,v)
        v=np.apply_along_axis(lambda col:box_pass_1d(col,r),0,v)
    return v

def line_art(luma,radius=24.,contrast=1.,edge=.5,invert=False):
    threshold=np.clip(edge,0,1)*.03
    small=small_disc(luma,1.)
    large=small_disc(luma,1.6)
    dog=small-large
    line=np.where(dog>threshold,1.,0.) if invert else np.where(dog<-threshold,1.,0.)
    soft=np.clip(fast_blur(line,radius),0,1)
    return soft**(1/max(.1,contrast))

def native(luma,r=24,c=1,edge=.5,invert=0):
    luma=np.ascontiguousarray(luma,dtype='float32');o=np.empty_like(luma)
    assert lib.line(luma,o,luma.shape[1],luma.shape[0],r,c,edge,invert)==0
    return o

rng=np.random.default_rng(7123);y,x=np.mgrid[:64,:80]
cases=[np.full(x.shape,.5),.2+x*.002+y*.001,np.where(x<40,.2,.8),.6-.15*np.exp(-((x-40)**2+(y-32)**2)/70),rng.random(x.shape)]
mean_errors=[]
for d in cases:
    d=d.astype('float32')
    for r in [1,6,24,64]:
        for invert in (0,1):
            ref=line_art(d,r,invert=bool(invert));actual=native(d,r,invert=invert)
            diff=np.abs(ref-actual)
            mean_errors.append(float(diff.mean()))
            # Binarize-then-blur is threshold-sensitive: float32 (C++) vs float64 (NumPy) can flip
            # an occasional pixel right at the DoG threshold, so tolerate rare outliers but not
            # systematic divergence.
            assert diff.mean()<2e-3,(r,invert,diff.mean())
            assert (diff>.05).mean()<.01,(r,invert,(diff>.05).mean())
assert np.isfinite(native(np.full((8,8),np.nan,dtype='float32'))).all()
rgb=rng.uniform(.03,.97,(1000,1,3)).astype('float32');mask=rng.random((1000,1)).astype('float32')
inputs=np.ascontiguousarray(np.concatenate([rgb,mask[...,None]],axis=2));output=np.empty_like(rgb)
lib.color(inputs,output,1000,2)
color_error=float(np.max(abs(output-chroma(rgb,mask,2))))
assert color_error<.001,color_error
lib.color(inputs,output,1000,0);assert np.array_equal(output,rgb)
special=np.array([[-1,.2,.3,1],[4,2,1,1],[np.nan,.2,.3,1]],dtype='float32');sout=np.empty((3,3),dtype='float32')
lib.color(special,sout,3,2);assert np.array_equal(sout,special[:,:3],equal_nan=True)
y,x=np.mgrid[:416,:640];d=(.6-.15*np.exp(-((x-320)**2+(y-208)**2)/900)).astype('float32')
start=time.perf_counter();native(d);secs=time.perf_counter()-start
metrics={'line_art_mean_abs_error':max(mean_errors),'rgb_max_abs_error':color_error,'native_line_art_seconds':secs,'dimensions':list(d.shape),'cases':len(mean_errors)}
Path('results').mkdir(exist_ok=True);Path('results/native_metrics.json').write_text(json.dumps(metrics,indent=2));print('PASS',metrics)
