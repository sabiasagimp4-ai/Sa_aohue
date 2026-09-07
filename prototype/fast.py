"""Persistent CUDA still/sequence experiment. No AE GPU integration.

python prototype/fast.py samples/demo01.jpg --out results/fast
"""
import argparse
import json
import time
from pathlib import Path
import numpy as np
from PIL import Image
import torch
import torch.nn.functional as F
from prototype import sheet, save


@torch.jit.script
def color(rgb: torch.Tensor, mask: torch.Tensor, amount: float):
    if amount <= 0:
        return rgb
    linear = torch.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055).pow(2.4))
    m1 = torch.tensor([[.4122214708,.5363325363,.0514459929],[.2119034982,.6806995451,.1073969566],[.0883024619,.2817188376,.6299787005]],device=rgb.device)
    m2 = torch.tensor([[.2104542553,.793617785,-.0040720468],[1.9779984951,-2.428592205,.4505937099],[.0259040371,.7827717662,-.808675766]],device=rgb.device)
    inv1 = torch.tensor([[4.0767416621,-3.3077115913,.2309699292],[-1.2684380046,2.6097574011,-.3413193965],[-.0041960863,-.7034186147,1.707614701]],device=rgb.device)
    inv2 = torch.tensor([[1.,.3963377774,.2158037573],[1.,-.1055613458,-.0638541728],[1.,-.0894841775,-1.291485548]],device=rgb.device)
    lab = (linear @ m1.t()).clamp_min(0).pow(1./3.) @ m2.t()
    lo = torch.ones_like(mask)
    hi = 1 + amount * mask
    for _ in range(12):
        gain = (lo + hi) * .5
        q = torch.cat((lab[...,:1],lab[...,1:] * gain.unsqueeze(-1)), -1)
        v = (q @ inv2.t()).pow(3) @ inv1.t()
        ok = ((v >= 0) & (v <= 1)).all(-1)
        lo = torch.where(ok,gain,lo)
        hi = torch.where(ok,hi,gain)
    q = torch.cat((lab[...,:1],lab[...,1:] * lo.unsqueeze(-1)), -1)
    v = (q @ inv2.t()).pow(3) @ inv1.t()
    out = torch.where(v <= .0031308,v*12.92,1.055*v.clamp_min(0).pow(1./2.4)-.055)
    return torch.where((mask > 0).unsqueeze(-1),out,rgb)


def smooth_depth(d, strength=1.):
    # Depth-only bilateral: RGB paint/shadows cannot guide the mask.
    h,w = d.shape
    patches = F.unfold(F.pad(d[None,None],(2,2,2,2),mode='replicate'),5).view(25,h,w)
    yy,xx = torch.meshgrid(torch.arange(-2,3,device=d.device),torch.arange(-2,3,device=d.device),indexing='ij')
    weight = torch.exp(-((patches-d)/.025).square()*.5-(xx.square()+yy.square()).reshape(25,1,1)/2.)
    filtered = (patches*weight).sum(0)/weight.sum(0)
    return d + strength*(filtered-d)


class Mask(torch.nn.Module):
    def __init__(self,h,w,radius,detail=.35,edge=.5,denoise=1.):
        super().__init__()
        self.detail,self.edge,self.denoise = detail,edge,denoise
        self.zscale = min(h,w)*.5
        y,x = torch.meshgrid(torch.arange(h),torch.arange(w),indexing='ij')
        v = 2**-.5
        directions = [(1,0),(v,v),(0,1),(-v,v),(-1,0),(-v,-v),(0,-1),(v,-v)]
        taps = [(ux*max(1,radius*scale*(s/6)**2),uy*max(1,radius*scale*(s/6)**2),max(1,radius*scale*(s/6)**2)) for scale in [1,.25] for ux,uy in directions for s in range(1,7)]
        t = torch.tensor(taps)
        xx,yy = x+t[:,0,None,None],y+t[:,1,None,None]
        self.register_buffer('valid',((xx>=0)&(xx<=w-1)&(yy>=0)&(yy<=h-1)).float())
        self.register_buffer('grid',torch.stack((xx*2/max(1,w-1)-1,yy*2/max(1,h-1)-1),-1).reshape(1,96*h,w,2))
        self.register_buffer('taps',t[:,:,None,None])

    def forward(self,d):
        if self.denoise > 0:
            d = smooth_depth(d,self.denoise)
        h,w = d.shape
        p = F.pad(d[None,None],(1,1,1,1),mode='replicate')[0,0]
        a,b = p[1:-1,2:]-d,d-p[1:-1,:-2]
        gx = torch.where(a.abs()<b.abs(),a,b)
        a,b = p[2:,1:-1]-d,d-p[:-2,1:-1]
        gy = torch.where(a.abs()<b.abs(),a,b)
        dz = F.grid_sample(d[None,None],self.grid,mode='bilinear',padding_mode='border',align_corners=True).reshape(96,h,w)-d
        slope = ((dz-gx*self.taps[:,0]-gy*self.taps[:,1])*self.zscale/self.taps[:,2]-.025).clamp_min(0)
        protect = 1-self.edge*((dz.abs()-.08)/.17).clamp(0,1)
        hor = (slope*torch.rsqrt(1+slope.square())*protect*self.valid).reshape(2,8,6,h,w).amax(2)
        occ = (hor[:,:4]*hor[:,4:]).sqrt().mean(1)
        mask = occ[0]*(1-self.detail)+occ[1]*self.detail
        if self.denoise > 0:
            # ponytail: suppress weak responses; very shallow/small recesses are also lost.
            mask = (mask-.025*self.denoise).clamp_min(0)/(1-.025*self.denoise)
            mask = F.avg_pool2d(F.pad(mask[None,None],(1,1,1,1),mode='replicate'),3,stride=1)[0,0]
            # A depth discontinuity is weak evidence of enclosure. Suppress its band.
            padded = F.pad(d[None,None],(2,2,2,2),mode='replicate')
            spread = F.max_pool2d(padded,5,stride=1)+F.max_pool2d(-padded,5,stride=1)
            confidence = (1-(spread[0,0]-.04)/.08).clamp(0,1)
            mask = mask*(1-self.denoise+self.denoise*confidence)
        return mask.clamp(0,1)


class FastPipeline:
    """One instance per processing session; reuse for all frames of the same size."""
    def __init__(self,shape,network_size=266,mask_size=256,radius=24,denoise=1.):
        if not torch.cuda.is_available():
            raise RuntimeError('This experiment requires CUDA PyTorch')
        from transformers import AutoModelForDepthEstimation
        model = Path(__file__).resolve().parents[1]/'models/depth-anything-v2-small'
        self.net = AutoModelForDepthEstimation.from_pretrained(model,local_files_only=True).eval().cuda().half()
        self.shape = tuple(shape)
        h,w = shape
        self.network_shape = tuple(max(14,round(v*network_size/max(h,w)/14)*14) for v in shape)
        self.mask_shape = tuple(max(2,round(v*min(1,mask_size/max(h,w)))) for v in shape)
        self.mean = torch.tensor([.485,.456,.406],device='cuda')[None,:,None,None]
        self.std = torch.tensor([.229,.224,.225],device='cuda')[None,:,None,None]
        # Radius remains in output pixels; z scale follows the same isotropic reduction.
        self.mask = Mask(*self.mask_shape,radius*max(self.mask_shape)/max(shape),denoise=denoise).cuda()

    @torch.inference_mode()
    def run(self,rgb,amount=1.):
        if tuple(rgb.shape) != (*self.shape,3):
            raise ValueError('RGB must match pipeline dimensions')
        x = torch.as_tensor(rgb,device='cuda',dtype=torch.float32)
        data = F.interpolate(x.permute(2,0,1)[None],size=self.network_shape,mode='bicubic',align_corners=False,antialias=True)
        pred = self.net(pixel_values=((data-self.mean)/self.std).half()).predicted_depth.float()
        d = F.interpolate(pred[:,None],size=self.mask_shape,mode='bilinear',align_corners=False)[0,0]
        low,high = torch.quantile(d,torch.tensor([.01,.99],device='cuda'))
        d = ((d-low)/(high-low).clamp_min(1e-6)).clamp(0,1)
        c = self.mask(d)
        c = F.interpolate(c[None,None],size=self.shape,mode='bilinear',align_corners=False)[0,0]
        out = color(x,c,amount)
        return d,c,out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('inputs',nargs='+');p.add_argument('--out',default='results/fast')
    p.add_argument('--network-size',type=int,default=266);p.add_argument('--mask-size',type=int,default=256)
    p.add_argument('--max-size',type=int,default=640);p.add_argument('--radius',type=float,default=24)
    p.add_argument('--denoise',type=float,default=1.);p.add_argument('--runs',type=int,default=20)
    a = p.parse_args()
    if min(a.network_size,a.mask_size,a.max_size,a.runs)<=0 or not 0<=a.denoise<=1 or a.radius<=0:
        p.error('sizes, runs and radius must be positive; denoise must be 0..1')
    root = Path(a.out);root.mkdir(parents=True,exist_ok=True)
    metrics=[];pipeline=None
    for filename in a.inputs:
        im = Image.open(filename).convert('RGB');im.thumbnail((a.max_size,a.max_size))
        rgb = np.asarray(im,dtype=np.float32)/255
        start=time.perf_counter()
        if pipeline is None or pipeline.shape != tuple(rgb.shape[:2]):
            pipeline=FastPipeline(rgb.shape[:2],a.network_size,a.mask_size,a.radius,a.denoise)
        d,c,comp=pipeline.run(rgb);torch.cuda.synchronize()
        cold=time.perf_counter()-start
        for _ in range(5):pipeline.run(rgb)
        torch.cuda.synchronize();times=[]
        for _ in range(a.runs):
            start=time.perf_counter();d,c,comp=pipeline.run(rgb)
            # Includes CPU input upload AND all output downloads. Excludes file I/O.
            dn,cn,on=d.cpu().numpy(),c.cpu().numpy(),comp.cpu().numpy()
            torch.cuda.synchronize();times.append((time.perf_counter()-start)*1000)
        dest=root/Path(filename).stem;dest.mkdir(exist_ok=True)
        full_depth=np.asarray(Image.fromarray(dn).resize(im.size,Image.Resampling.BILINEAR))
        sheet(dest/'comparison.png',[('RGB',rgb),('Fast depth',full_depth),('Fast cavity',cn),('Composite',on)])
        save(dest/'cavity.png',cn);save(dest/'composite.png',on)
        Image.fromarray(np.uint16(cn*65535+.5)).save(dest/'cavity16.png')
        Image.fromarray(np.uint16(full_depth*65535+.5)).save(dest/'depth.png')
        np.save(dest/'cavity.npy',cn)
        row=dict(input=filename,gpu=torch.cuda.get_device_name(),shape=list(rgb.shape),network_shape=pipeline.network_shape,mask_shape=pipeline.mask_shape,cold_seconds=cold,median_ms=float(np.median(times)),p95_ms=float(np.percentile(times,95)),runs=a.runs,denoise=a.denoise)
        metrics.append(row);print(row,flush=True)
    (root/'metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')


if __name__=='__main__':main()
