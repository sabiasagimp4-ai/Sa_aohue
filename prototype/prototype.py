"""Still-image experiment; RGB is sRGB, depth is relative height (white near)."""
import argparse
import json
import time
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter, map_coordinates

M1 = np.array([[.4122214708,.5363325363,.0514459929],[.2119034982,.6806995451,.1073969566],[.0883024619,.2817188376,.6299787005]])
M2 = np.array([[.2104542553,.793617785,-.0040720468],[1.9779984951,-2.428592205,.4505937099],[.0259040371,.7827717662,-.808675766]])
def linear(rgb):
    return np.where(rgb <= .04045, rgb/12.92, ((np.maximum(rgb,0)+.055)/1.055)**2.4)
def encoded(rgb):
    return np.where(rgb <= .0031308, rgb*12.92, 1.055*np.maximum(rgb,0)**(1/2.4)-.055)
def lab(rgb):
    return np.cbrt(linear(rgb) @ M1.T) @ M2.T
def unlab(v):
    return (v @ np.linalg.inv(M2).T)**3 @ np.linalg.inv(M1).T
def chroma(rgb, cavity, amount=1.):
    if amount == 0: return rgb.copy()
    v = lab(rgb)
    lo=np.ones(rgb.shape[:2]); hi=1+amount*cavity
    # Fixed-L/hue gamut search: reduce requested gain, never clip RGB channels.
    for _ in range(12):
        gain=(lo+hi)*.5
        q=v.copy(); q[...,1:]*=gain[...,None]
        out=unlab(q)
        ok=np.all((out>=0)&(out<=1),axis=-1)
        lo=np.where(ok,gain,lo); hi=np.where(ok,hi,gain)
    q=v.copy(); q[...,1:]*=lo[...,None]
    return np.where((cavity>0)[...,None],encoded(unlab(q)),rgb)

def cavity(depth, radius=24., contrast=1., detail=.35, edge=.5):
    d=np.clip(np.nan_to_num(depth),0,1).astype(np.float64)
    h,w=d.shape; y,x=np.mgrid[:h,:w]
    zscale=min(h,w)*.5
    def sample(dx,dy):
        return map_coordinates(d,[y+dy,x+dx],order=1,mode='nearest',prefilter=False)
    # Tangent plane correction rejects a sloped plane, unlike local darkness.
    def derivative(dx,dy):
        a=sample(dx,dy)-d; b=d-sample(-dx,-dy)
        return np.where(np.abs(a)<np.abs(b),a,b)
    gx=derivative(1,0); gy=derivative(0,1)
    total=np.zeros_like(d)
    for scale,weight in [(1,1-detail),(.25,detail)]:
        r=max(1,radius*scale); horizons=[]
        v=2**-.5
        for ux,uy in [(1,0),(v,v),(0,1),(-v,v),(-1,0),(-v,-v),(0,-1),(v,-v)]:
            horizon=np.zeros_like(d)
            for step in range(1,7):
                dist=max(1,r*(step/6)**2); dx=ux*dist; dy=uy*dist
                dz=sample(dx,dy)-d
                slope=np.maximum(0,(dz-gx*dx-gy*dy)*zscale/dist-.025)
                protection=1-edge*np.clip((np.abs(dz)-.08)/.17,0,1)
                value=slope/np.sqrt(1+slope*slope)*protection
                valid=(x+dx>=0)&(x+dx<=w-1)&(y+dy>=0)&(y+dy<=h-1)
                horizon=np.maximum(horizon,np.where(valid,value,0))
            horizons.append(horizon)
        # Cavity emphasis: bilateral enclosure suppresses isolated silhouettes.
        # ponytail: height-field heuristic, replace with calibrated GTAO for metric depth.
        occ=sum(np.sqrt(horizons[i]*horizons[i+4]) for i in range(4))/4
        total+=weight*occ
    return np.clip(total,0,1)**(1/max(.1,contrast))

def denoise(d):
    # Edge-aware (bilateral) depth smoothing: mirrors src/core.h aohue::denoise.
    # Averages a 5x5 neighborhood, down-weighting samples whose depth differs from the
    # center (range) and samples farther away (spatial), so it removes AI/sensor depth
    # noise without blurring across a true silhouette edge.
    h,w=d.shape; padded=np.pad(d,2,mode='edge')
    num=np.zeros_like(d); den=np.zeros_like(d)
    for dy in range(-2,3):
        for dx in range(-2,3):
            shifted=padded[2+dy:2+dy+h,2+dx:2+dx+w]
            wgt=np.exp(-((shifted-d)**2)*800-(dx*dx+dy*dy)*.5)
            num+=shifted*wgt; den+=wgt
    return num/den

def cleanup(d, mask):
    # Post-filter on a computed cavity/AO map: mirrors src/core.h aohue::cleanup.
    # Drops near-zero noise, smooths the remainder, and suppresses response near strong
    # depth discontinuities (a sharp silhouette edge is weak evidence of enclosure).
    dead=.025; mask=np.maximum(mask-dead,0)/(1-dead)
    h,w=mask.shape; padded=np.pad(mask,1,mode='edge')
    mask=sum(padded[1+dy:1+dy+h,1+dx:1+dx+w] for dy in (-1,0,1) for dx in (-1,0,1))/9
    padded_d=np.pad(d,2,mode='edge')
    lo=np.full_like(d,1e9); hi=np.full_like(d,-1e9)
    for dy in range(-2,3):
        for dx in range(-2,3):
            v=padded_d[2+dy:2+dy+h,2+dx:2+dx+w]; lo=np.minimum(lo,v); hi=np.maximum(hi,v)
    confidence=1-np.clip((hi-lo-.04)/.08,0,1)
    return mask*confidence

def infer(image, model=None):
    import torch
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation
    local=Path(__file__).resolve().parents[1]/'models/depth-anything-v2-small'
    name=str(local) if (local/'model.safetensors').exists() else 'depth-anything/Depth-Anything-V2-Small-hf'
    if model is None:
        processor=AutoImageProcessor.from_pretrained(name,use_fast=False)
        net=AutoModelForDepthEstimation.from_pretrained(name).eval().to('cuda' if torch.cuda.is_available() else 'cpu')
    else: processor,net=model
    with torch.inference_mode():
        data=processor(images=image,return_tensors='pt').to(net.device)
        pred=net(**data).predicted_depth
        pred=torch.nn.functional.interpolate(pred[:,None],size=(image.height,image.width),mode='bicubic',align_corners=False)[0,0].float().cpu().numpy()
    low,high=np.percentile(pred,[1,99])
    return np.clip((pred-low)/max(float(high-low),1e-6),0,1),(processor,net)

def save(path,a):
    Image.fromarray(np.uint8(np.clip(a,0,1)*255+.5)).save(path)
def sheet(path,items):
    w,h=items[0][1].shape[1],items[0][1].shape[0]
    canvas=Image.new('RGB',(w*len(items),h+26),'#181818'); draw=ImageDraw.Draw(canvas)
    for i,(label,a) in enumerate(items):
        im=Image.fromarray(np.uint8(np.clip(a,0,1)*255+.5)).convert('RGB')
        canvas.paste(im,(i*w,26)); draw.text((i*w+6,6),label,fill='white')
    canvas.save(path)

def main():
    p=argparse.ArgumentParser(); p.add_argument('input'); p.add_argument('--depth'); p.add_argument('--out',default='results'); p.add_argument('--radius',type=float,default=24); p.add_argument('--amount',type=float,default=1); p.add_argument('--max-size',type=int,default=0)
    a=p.parse_args(); out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    im=Image.open(a.input).convert('RGB')
    if a.max_size>0: im.thumbnail((a.max_size,a.max_size))
    rgb=np.asarray(im)/255.
    start=time.perf_counter()
    if a.depth:
        raw=Image.open(a.depth); d=np.asarray(raw,dtype=float)
        if d.ndim!=2: raise ValueError('depth must be grayscale')
        if raw.mode not in ['L','I;16','I;16L','I;16B','I']: raise ValueError('depth must be an 8/16-bit grayscale image')
        d=d/(255 if raw.mode=='L' else 65535)
        d=np.asarray(Image.fromarray(d.astype('float32')).resize(im.size,Image.Resampling.BILINEAR))
    else: d,_=infer(im)
    infer_seconds=time.perf_counter()-start
    np.save(out/'depth.npy',d); Image.fromarray(np.uint16(d*65535+.5)).save(out/'depth.png')
    start=time.perf_counter()
    dd=denoise(np.clip(np.nan_to_num(d),0,1))
    c=cleanup(dd,cavity(dd,a.radius)); comp=chroma(rgb,c,a.amount)
    fallback=gaussian_filter(lab(rgb)[...,0],2)
    base=cleanup(fallback,cavity(fallback,a.radius))
    sheet(out/'comparison.png',[('RGB',rgb),('AI / external depth',d),('Cavity',c),('Composite',comp),('Luma baseline cavity',base)])
    save(out/'cavity.png',c); save(out/'ao.png',1-c); save(out/'composite.png',comp); save(out/'input.png',rgb)
    sheet(out/'radius.png',[(f'Radius {r}',cleanup(dd,cavity(dd,r))) for r in [6,24,64]])
    metrics={'depth_seconds':infer_seconds,'ao_color_seconds':time.perf_counter()-start,'cavity_mean':float(c.mean()),'max_oklab_L_error':float(np.max(np.abs(lab(comp)[...,0]-lab(rgb)[...,0])))}
    (out/'metrics.json').write_text(json.dumps(metrics,indent=2)); print(metrics)
if __name__=='__main__': main()
