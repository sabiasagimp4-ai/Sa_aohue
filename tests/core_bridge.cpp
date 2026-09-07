#include "../src/core.h"
extern "C" __declspec(dllexport) int line(const float* luma,float* output,int w,int h,float radius,float contrast,float edge,int invert) {
    try {
        if(!luma||!output) return 1;
        aohue::Field f(w,h);for(size_t i=0;i<f.d.size();++i) f.d[i]=aohue::unit(luma[i]);
        aohue::Options p;p.radius=radius;p.contrast=contrast;p.edge=edge;
        auto m=aohue::lineArt(f,p,invert!=0);
        for(size_t i=0;i<m.size();++i) output[i]=m[i];
        return 0;
    } catch(...) {return 1;}
}
extern "C" __declspec(dllexport) void color(const float* input,float* output,int count,float amount) {
    for(int i=0;i<count;++i) {
        auto c=aohue::chroma({input[4*i],input[4*i+1],input[4*i+2]},input[4*i+3],amount,false);
        output[3*i]=c.r;output[3*i+1]=c.g;output[3*i+2]=c.b;
    }
}
