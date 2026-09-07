#include "../src/core.h"
extern "C" __declspec(dllexport) int mask(const float* depth,float* output,int w,int h,float radius,float contrast,float detail,float edge) {
    try {
        if(!depth||!output) return 1;
        aohue::Field f(w,h);for(size_t i=0;i<f.d.size();++i) f.d[i]=aohue::unit(depth[i]);
        aohue::Options p;p.radius=radius;p.contrast=contrast;p.detail=detail;p.edge=edge;
        aohue::Kernel k(f,p);for(int y=0;y<h;++y) for(int x=0;x<w;++x) output[size_t(y)*w+x]=k.at(f,x,y,p);
        return 0;
    } catch(...) {return 1;}
}
extern "C" __declspec(dllexport) void color(const float* input,float* output,int count,float amount) {
    for(int i=0;i<count;++i) {
        auto c=aohue::chroma({input[4*i],input[4*i+1],input[4*i+2]},input[4*i+3],amount,false);
        output[3*i]=c.r;output[3*i+1]=c.g;output[3*i+2]=c.b;
    }
}
