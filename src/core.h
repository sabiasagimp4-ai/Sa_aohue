#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>
#include <thread>

namespace aohue {
// Splits [0,h) into row ranges and runs fn(y0,y1) on hardware_concurrency() threads (capped at
// 16). One spawn/join per call: this file calls it a handful of times per frame (not thousands
// like a real-time DoF), so a persistent thread pool would be needless complexity here.
template<class F> inline void parallelFor(int h,F&& fn) {
    unsigned n=std::thread::hardware_concurrency();if(n<1) n=1;if(n>16) n=16;
    if(n<=1||h<8) {fn(0,h);return;}
    int chunk=(h+int(n)-1)/int(n);
    std::vector<std::thread> workers;workers.reserve(n);
    for(unsigned t=0;t<n;++t) {
        int y0=int(t)*chunk,y1=std::min(y0+chunk,h);
        if(y0>=y1) break;
        workers.emplace_back(fn,y0,y1);
    }
    for(auto& w:workers) w.join();
}
inline float unit(float v) { return std::isfinite(v) ? std::clamp(v,0.f,1.f) : 0.f; }
struct RGB { float r,g,b; };
inline RGB toLab(RGB c) {
    float l=std::cbrt(.4122214708f*c.r+.5363325363f*c.g+.0514459929f*c.b);
    float m=std::cbrt(.2119034982f*c.r+.6806995451f*c.g+.1073969566f*c.b);
    float s=std::cbrt(.0883024619f*c.r+.2817188376f*c.g+.6299787005f*c.b);
    return {.2104542553f*l+.793617785f*m-.0040720468f*s,1.9779984951f*l-2.428592205f*m+.4505937099f*s,.0259040371f*l+.7827717662f*m-.808675766f*s};
}
inline RGB fromLab(RGB c) {
    float l=c.r+.3963377774f*c.g+.2158037573f*c.b;
    float m=c.r-.1055613458f*c.g-.0638541728f*c.b;
    float s=c.r-.0894841775f*c.g-1.291485548f*c.b;
    l=l*l*l; m=m*m*m; s=s*s*s;
    return {4.0767416621f*l-3.3077115913f*m+.2309699292f*s,-1.2684380046f*l+2.6097574011f*m-.3413193965f*s,-.0041960863f*l-.7034186147f*m+1.707614701f*s};
}
inline float decode(float v) { return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f); }
inline float encode(float v) { return v<=.0031308f?v*12.92f:1.055f*std::pow(v,1.f/2.4f)-.055f; }
inline bool gamut(RGB v) { return v.r>=0&&v.g>=0&&v.b>=0&&v.r<=1&&v.g<=1&&v.b<=1; }
inline RGB chroma(RGB rgb,float mask,float amount,bool linear) {
    // Preserve HDR, negative, and non-finite source colors verbatim in this SDR color model.
    if(amount<=0||mask<=0||!gamut(rgb)) return rgb;
    RGB c=linear?rgb:RGB{decode(rgb.r),decode(rgb.g),decode(rgb.b)};
    RGB lab=toLab(c); float lo=1,hi=1+amount*mask;
    for(int i=0;i<12;++i) {
        float g=(lo+hi)*.5f; RGB q=fromLab({lab.r,lab.g*g,lab.b*g});
        if(gamut(q)) lo=g; else hi=g;
    }
    if(lo==1) return rgb;
    c=fromLab({lab.r,lab.g*lo,lab.b*lo});
    return linear?c:RGB{encode(c.r),encode(c.g),encode(c.b)};
}
struct Field {
    int w,h; std::vector<float> d,valid;
    Field(int width,int height):w(width),h(height) {
        if(w<=0||h<=0||size_t(w)*h>size_t(268435456)) throw std::length_error("image dimensions");
        d.resize(size_t(w)*h);valid.assign(d.size(),1);
    }
    float sample(const std::vector<float>& a,float x,float y) const {
        x=std::clamp(x,0.f,float(w-1));y=std::clamp(y,0.f,float(h-1));
        int ix=int(x),iy=int(y),jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);
        float fx=x-ix,fy=y-iy;
        return (a[size_t(iy)*w+ix]*(1-fx)+a[size_t(iy)*w+jx]*fx)*(1-fy)+(a[size_t(jy)*w+ix]*(1-fx)+a[size_t(jy)*w+jx]*fx)*fy;
    }
};
// Edge-aware depth smoothing: averages a 5x5 neighborhood, down-weighting samples whose
// depth differs from the center (range) and samples farther away (spatial). Unlike a plain
// box blur this does not blur across a true depth edge, so it removes sensor/AI depth noise
// without eroding real silhouettes. Fixed 2px reach keeps boundary bias inside existing
// small-margin invariants (flat/slope fields stay exact near frame edges).
inline void denoise(Field& f) {
    auto b=f.d;
    parallelFor(f.h,[&](int y0,int y1) {
        for(int y=y0;y<y1;++y) for(int x=0;x<f.w;++x) {
            size_t i=size_t(y)*f.w+x; if(f.valid[i]<=0) continue;
            float z=f.d[i],sum=0,weight=0;
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                int xx=std::clamp(x+dx,0,f.w-1),yy=std::clamp(y+dy,0,f.h-1);size_t j=size_t(yy)*f.w+xx;
                if(f.valid[j]<=0) continue;
                float dd=f.d[j]-z,spatial=float(dx*dx+dy*dy);
                float wgt=std::exp(-dd*dd*800.f-spatial*.5f);
                sum+=f.d[j]*wgt;weight+=wgt;
            }
            b[i]=weight>0?sum/weight:z;
        }
    });
    f.d.swap(b);
}
// Post-filter on a computed cavity/AO mask buffer (same w*h as f): drops near-zero noise,
// smooths the remainder, and suppresses response near strong depth discontinuities (a sharp
// silhouette edge is weak evidence of enclosure, not a cavity) to cut contour haloing.
inline void cleanup(const Field& f, std::vector<float>& mask) {
    constexpr float dead=.025f;
    for(auto& v:mask) v=std::max(0.f,v-dead)/(1-dead);
    auto b=mask;
    parallelFor(f.h,[&](int y0,int y1) {
        for(int y=y0;y<y1;++y) for(int x=0;x<f.w;++x) {
            float sum=0;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int xx=std::clamp(x+dx,0,f.w-1),yy=std::clamp(y+dy,0,f.h-1);
                sum+=mask[size_t(yy)*f.w+xx];
            }
            b[size_t(y)*f.w+x]=sum/9.f;
        }
    });
    mask.swap(b);
    parallelFor(f.h,[&](int y0,int y1) {
    for(int y=y0;y<y1;++y) for(int x=0;x<f.w;++x) {
        float lo=1e9f,hi=-1e9f;
        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
            int xx=std::clamp(x+dx,0,f.w-1),yy=std::clamp(y+dy,0,f.h-1);
            float v=f.d[size_t(yy)*f.w+xx];lo=std::min(lo,v);hi=std::max(hi,v);
        }
        float confidence=1-unit((hi-lo-.04f)/.08f);
        mask[size_t(y)*f.w+x]*=confidence;
    }
    });
}
struct Options { float radius=24,contrast=1,detail=.35f,edge=.5f,amount=1; float sx=1,sy=1,par=1,zscale=0; };
struct Tap {float dx,dy,dist;};
struct Kernel {
    Tap taps[2][8][6]; float zscale;
    Kernel(const Field& f,const Options& p) {
        zscale=p.zscale>0?p.zscale:std::min(f.w*p.par/p.sx,f.h/p.sy)*.5f;
        for(int k=0;k<2;++k) for(int a=0;a<8;++a) for(int s=0;s<6;++s) {
            float r=std::max(1.f,p.radius*(k?.25f:1.f));float t=(s+1)/6.f;
            const float ux[8]={1,.7071067811865476f,0,-.7071067811865476f,-1,-.7071067811865476f,0,.7071067811865476f};
            const float uy[8]={0,.7071067811865476f,1,.7071067811865476f,0,-.7071067811865476f,-1,-.7071067811865476f};
            float dist=std::max(1.f,r*t*t);
            taps[k][a][s]={ux[a]*dist*p.sx/p.par,uy[a]*dist*p.sy,dist};
        }
    }
    float at(const Field& f,int x,int y,const Options& p) const {
        size_t i=size_t(y)*f.w+x;if(f.valid[i]<=0) return 0;
        float z=f.d[i];
        auto grad=[&](int dx,int dy) {
            float a=f.sample(f.d,float(x+dx),float(y+dy))-z,b=z-f.sample(f.d,float(x-dx),float(y-dy));
            bool va=f.sample(f.valid,float(x+dx),float(y+dy))>0,vb=f.sample(f.valid,float(x-dx),float(y-dy))>0;
            if(!va&&!vb) return 0.f;if(!va) return b;if(!vb) return a;
            return std::abs(a)<std::abs(b)?a:b;
        };
        float gx=grad(1,0),gy=grad(0,1),total=0;
        for(int k=0;k<2;++k) {
            float hor[8]={};
            for(int a=0;a<8;++a) for(const auto& t:taps[k][a]) {
                float xx=x+t.dx,yy=y+t.dy;
                if(xx<0||yy<0||xx>f.w-1||yy>f.h-1) continue;
                float dz=f.sample(f.d,xx,yy)-z;
                float slope=std::max(0.f,(dz-gx*t.dx-gy*t.dy)*zscale/t.dist-.025f);
                float protect=1-p.edge*unit((std::abs(dz)-.08f)/.17f);
                float v=slope/std::sqrt(1+slope*slope)*protect*f.sample(f.valid,xx,yy);
                hor[a]=std::max(hor[a],v);
            }
            // ponytail: bilateral height-field enclosure; calibrated GTAO needs camera/metric depth.
            float v=0;for(int a=0;a<4;++a) v+=std::sqrt(hor[a]*hor[a+4])*.25f;
            total+=(k?p.detail:1-p.detail)*v;
        }
        return std::pow(unit(total),1/std::max(.1f,p.contrast));
    }
};
// Vogel (golden-angle) disc sample pattern -- ported from vogel_pattern() in
// ../cool_blur/plugin/CoolBlur_Core.h. A fixed-count, deterministic, zero-meaned point set on
// the unit disc: low-discrepancy (no clumping, unlike uniform random), and centroid-subtracted
// so the kernel never shifts the image. Used by discBlur's large-radius path below.
struct VogelSample { float u,v; };
inline std::vector<VogelSample> makeVogelPattern(int count) {
    std::vector<VogelSample> s(count>0?size_t(count):size_t(0));
    constexpr float golden=2.39996322972865332f;
    double mu=0,mv=0;
    for(int i=0;i<count;++i) {
        float rn=std::sqrt((i+.5f)/count),th=i*golden;
        s[size_t(i)].u=rn*std::cos(th);s[size_t(i)].v=rn*std::sin(th);
        mu+=s[size_t(i)].u;mv+=s[size_t(i)].v;
    }
    mu/=count;mv/=count;
    for(size_t i=0;i<s.size();++i) {s[i].u-=float(mu);s[i].v-=float(mv);}
    return s;
}
inline const std::vector<VogelSample>& vogelPattern(int n) {
    static const std::vector<VogelSample> p64=makeVogelPattern(64);
    static const std::vector<VogelSample> p128=makeVogelPattern(128);
    static const std::vector<VogelSample> p256=makeVogelPattern(256);
    if(n<=64) return p64; if(n<=128) return p128; return p256;
}
inline float bilinearAt(const std::vector<float>& v,int w,int h,float x,float y) {
    x=std::clamp(x,0.f,float(w-1));y=std::clamp(y,0.f,float(h-1));
    int x0=int(x),y0=int(y),x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
    float fx=x-x0,fy=y-y0;
    return (v[size_t(y0)*w+x0]*(1-fx)+v[size_t(y0)*w+x1]*fx)*(1-fy)
          +(v[size_t(y1)*w+x0]*(1-fx)+v[size_t(y1)*w+x1]*fx)*fy;
}
// Valid-weighted, anti-aliased circular average -- ported from gather_channel() in
// ../cool_blur/plugin/CoolBlur_Core.h (edge=0: a flat disc kernel, no bokeh shaping).
// Isotropic and non-directional, unlike a square box blur. Small radius (<=~6px, matching
// cool_blur's own 169-tap cutoff) uses the exact O(radius^2) disc average with an
// anti-aliased rim; larger radius switches to the fixed-count Vogel spiral (bilinear taps),
// which stays O(w*h*N) with N in {64,128,256} regardless of radius -- cool_blur itself drops
// to an FFT engine past radius 8px for the same reason (paying O(radius^2) is a bad trade at
// that point), but a bounded-sample disc still looks far rounder/softer than a box blur, which
// is what this file needs since it is not real-time-critical in the same way as a live DoF.
inline std::vector<float> discBlur(const std::vector<float>& v,const std::vector<float>& valid,int w,int h,float radius) {
    if(radius<.5f) return v;
    std::vector<float> out(v.size());
    int ir=int(std::ceil(radius));
    bool exact=size_t(2*ir+1)*size_t(2*ir+1)<=169;
    const std::vector<VogelSample>* pat=exact?nullptr:&vogelPattern(radius<=16?64:radius<=40?128:256);
    parallelFor(h,[&](int y0,int y1) {
        for(int y=y0;y<y1;++y) for(int x=0;x<w;++x) {
            float acc=0,wsum=0;
            if(exact) {
                for(int dy=-ir;dy<=ir;++dy) for(int dx=-ir;dx<=ir;++dx) {
                    float rn2=(dx*dx+dy*dy)/(radius*radius);if(rn2>1) continue;
                    float aa=std::clamp((1-std::sqrt(rn2))*radius,0.f,1.f);if(aa<=0) continue;
                    int xx=std::clamp(x+dx,0,w-1),yy=std::clamp(y+dy,0,h-1);size_t j=size_t(yy)*w+xx;
                    float wgt=aa*valid[j];acc+=v[j]*wgt;wsum+=wgt;
                }
            } else {
                float cx=x+.5f,cy=y+.5f;
                for(const auto& s:*pat) {
                    float fx=cx+s.u*radius,fy=cy+s.v*radius;
                    float wgt=bilinearAt(valid,w,h,fx,fy);
                    acc+=bilinearAt(v,w,h,fx,fy)*wgt;wsum+=wgt;
                }
            }
            out[size_t(y)*w+x]=wsum>0?acc/wsum:v[size_t(y)*w+x];
        }
    });
    return out;
}
// Image-space (non-geometric) alternative map: difference-of-Gaussians line/edge detection,
// binarize, then blur -- no AI, no external layer, cheap enough to run live in AE. f.d must
// already hold a luminance-like field (same OKLab L extraction as RGB Approximation). Reuses
// Options.radius as the post-binarize blur spread, Options.contrast as the final gamma, and
// Options.edge (0..1, the "Edge Protect" slider repurposed here as "Line Threshold") as the
// DoG binarization cutoff: 0 flags almost any gradient as a line, 1 keeps only strong ones.
inline std::vector<float> lineArt(const Field& f,const Options& p,bool invert) {
    float threshold=std::clamp(p.edge,0.f,1.f)*.03f;
    auto small=discBlur(f.d,f.valid,f.w,f.h,1.f),large=discBlur(f.d,f.valid,f.w,f.h,1.6f);
    std::vector<float> line(f.d.size());
    for(size_t i=0;i<line.size();++i) {
        float dog=small[i]-large[i];
        line[i]=(invert?dog>threshold:dog<-threshold)&&f.valid[i]>0?1.f:0.f;
    }
    auto soft=discBlur(line,f.valid,f.w,f.h,p.radius);
    for(auto& v:soft) v=std::pow(unit(v),1/std::max(.1f,p.contrast));
    return soft;
}
}
