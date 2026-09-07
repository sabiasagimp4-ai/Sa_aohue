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
};
struct Options { float radius=24,contrast=1,edge=.5f,amount=1; };
// Exact anti-aliased disc average. Only used below for the ~1-2px DoG kernels, where sub-pixel
// radius precision matters (radius 1.0 vs 1.6 must differ) and the O(radius^2) cost is trivial
// either way -- unlike the large user-controlled blur further down, this one never needs to scale.
inline std::vector<float> smallDisc(const std::vector<float>& v,const std::vector<float>& valid,int w,int h,float radius) {
    std::vector<float> out(v.size());
    int ir=int(std::ceil(radius));
    parallelFor(h,[&](int y0,int y1) {
        for(int y=y0;y<y1;++y) for(int x=0;x<w;++x) {
            float acc=0,wsum=0;
            for(int dy=-ir;dy<=ir;++dy) for(int dx=-ir;dx<=ir;++dx) {
                float rn2=(dx*dx+dy*dy)/(radius*radius);if(rn2>1) continue;
                float aa=std::clamp((1-std::sqrt(rn2))*radius,0.f,1.f);if(aa<=0) continue;
                int xx=std::clamp(x+dx,0,w-1),yy=std::clamp(y+dy,0,h-1);size_t j=size_t(yy)*w+xx;
                float wgt=aa*valid[j];acc+=v[j]*wgt;wsum+=wgt;
            }
            out[size_t(y)*w+x]=wsum>0?acc/wsum:v[size_t(y)*w+x];
        }
    });
    return out;
}
// One axis of a valid-weighted box average via a running sum (moving window): O(len) per line
// regardless of radius, unlike a per-pixel disc/kernel sum. This is the same trick AE's own
// "Fast Box Blur" effect uses for its speed.
inline void boxPass(std::vector<float>& v,const std::vector<float>& valid,int w,int h,int r,bool horiz) {
    if(r<1) return;
    int len=horiz?w:h,lines=horiz?h:w;
    size_t stride=horiz?size_t(1):size_t(w),lineStride=horiz?size_t(w):size_t(1);
    std::vector<float> out(v.size());
    parallelFor(lines,[&](int l0,int l1) {
        std::vector<float> sumV(len+1),sumW(len+1);
        for(int l=l0;l<l1;++l) {
            size_t base=size_t(l)*lineStride;
            sumV[0]=sumW[0]=0;
            for(int i=0;i<len;++i) {
                size_t idx=base+size_t(i)*stride;
                sumV[i+1]=sumV[i]+v[idx]*valid[idx];sumW[i+1]=sumW[i]+valid[idx];
            }
            for(int i=0;i<len;++i) {
                int lo=std::max(0,i-r),hi=std::min(len-1,i+r);
                float wsum=sumW[hi+1]-sumW[lo];size_t idx=base+size_t(i)*stride;
                out[idx]=wsum>0?(sumV[hi+1]-sumV[lo])/wsum:v[idx];
            }
        }
    });
    v.swap(out);
}
// AE "Fast Box Blur"-style blur: three box-average passes (horizontal+vertical each) at
// radius/sqrt(3) approximate a Gaussian of the requested radius, while staying O(w*h) per pass
// regardless of how large radius is -- replaces the old disc/Vogel sampling (which grew with
// radius) for the one blur here whose radius is a user-facing slider.
inline std::vector<float> fastBlur(const std::vector<float>& src,const std::vector<float>& valid,int w,int h,float radius) {
    if(radius<.5f) return src;
    std::vector<float> v=src;
    int r=std::max(1,int(std::round(radius/std::sqrt(3.f))));
    for(int pass=0;pass<3;++pass) {boxPass(v,valid,w,h,r,true);boxPass(v,valid,w,h,r,false);}
    return v;
}
// Image-space line/edge detection: difference-of-Gaussians, binarize, then blur. f.d must
// already hold a luminance-like field (OKLab L). Reuses Options.radius as the post-binarize
// blur spread, Options.contrast as the final gamma, and Options.edge (0..1, the "Line
// Threshold" slider) as the DoG binarization cutoff: 0 flags almost any gradient as a line, 1
// keeps only strong ones.
inline std::vector<float> lineArt(const Field& f,const Options& p,bool invert) {
    float threshold=std::clamp(p.edge,0.f,1.f)*.03f;
    auto small=smallDisc(f.d,f.valid,f.w,f.h,1.f),large=smallDisc(f.d,f.valid,f.w,f.h,1.6f);
    std::vector<float> line(f.d.size());
    for(size_t i=0;i<line.size();++i) {
        float dog=small[i]-large[i];
        line[i]=(invert?dog>threshold:dog<-threshold)&&f.valid[i]>0?1.f:0.f;
    }
    auto soft=fastBlur(line,f.valid,f.w,f.h,p.radius);
    for(auto& v:soft) v=std::pow(unit(v),1/std::max(.1f,p.contrast));
    return soft;
}
}
