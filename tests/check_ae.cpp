// Offline SDK adapter checks. Does not load or operate After Effects.
#include "../src/Sa_aohue.cpp"
#include <cassert>
#include <cstdio>
#include <limits>
static PF_Err noAbort(PF_ProgPtr) {return PF_Err_NONE;}
template<class P> void check(float max) {
    constexpr int w=48,h=32,stride=53;using Ch=decltype(P{}.red);
    std::vector<P> pixels(stride*h),depth(stride*h),output(stride*h),tile(stride*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        auto& q=pixels[y*stride+x];q.alpha=Ch(max*(x%3==0?.5f:1));q.red=Ch(q.alpha*.4f);q.green=Ch(q.alpha*.3f);q.blue=Ch(q.alpha*.2f);
        float d=.6f-.15f*std::exp(-float((x-24)*(x-24)+(y-16)*(y-16))/45);
        auto& z=depth[y*stride+x];z.alpha=Ch(max);z.red=z.green=z.blue=Ch(d*max);
    }
    PF_EffectWorld src{},dep{},dst{},crop{};
    src.width=dep.width=dst.width=crop.width=w;src.height=dep.height=dst.height=crop.height=h;
    src.rowbytes=dep.rowbytes=dst.rowbytes=crop.rowbytes=stride*sizeof(P);
    src.data=reinterpret_cast<PF_PixelPtr>(pixels.data());dep.data=reinterpret_cast<PF_PixelPtr>(depth.data());dst.data=reinterpret_cast<PF_PixelPtr>(output.data());crop.data=reinterpret_cast<PF_PixelPtr>(tile.data());
    PF_InData in{};in.width=w;in.height=h;in.downsample_x={1,1};in.downsample_y={1,1};in.pixel_aspect_ratio={1,1};in.inter.abort=noAbort;
    PF_ParamDef v[COUNT]{};PF_ParamDef* p[COUNT];for(int i=0;i<COUNT;++i) p[i]=&v[i];
    v[RADIUS].u.fs_d.value=12;v[CONTRAST].u.fs_d.value=1;v[DETAIL].u.fs_d.value=35;v[EDGE].u.fs_d.value=50;v[SOURCE].u.pd.value=2;v[OUTPUT_MODE].u.pd.value=1;v[TRANSFER].u.pd.value=1;
    assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
    assert(std::memcmp(pixels.data(),output.data(),pixels.size()*sizeof(P))==0);
    v[AMOUNT].u.fs_d.value=100;assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
    bool changed=false;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {size_t i=y*stride+x;assert(pixels[i].alpha==output[i].alpha);changed|=std::memcmp(&pixels[i],&output[i],sizeof(P))!=0;}
    assert(changed);
    // Tile adapter must equal a crop, with full input and translated output origin.
    crop.width=17;crop.height=11;in.output_origin_x=-7;in.output_origin_y=-6;
    assert(render<P>(&in,p,&src,&dep,&crop,max)==0);
    for(int y=0;y<crop.height;++y) for(int x=0;x<crop.width;++x) assert(std::memcmp(&tile[y*stride+x],&output[(y+6)*stride+x+7],sizeof(P))==0);
    in.output_origin_x=in.output_origin_y=0;
    v[OUTPUT_MODE].u.pd.value=2;assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
    v[OUTPUT_MODE].u.pd.value=3;assert(render<P>(&in,p,&src,&dep,&crop,max)==0);
    for(int y=0;y<crop.height;++y) for(int x=0;x<crop.width;++x) {int i=y*stride+x;assert(std::abs(float(output[i].red)+float(tile[i].red)-float(pixels[i].alpha))<=(max==1?1e-6f:1.f));}
    dep.width--;assert(render<P>(&in,p,&src,&dep,&dst,max)==PF_Err_BAD_CALLBACK_PARAM);dep.width++;
    v[SOURCE].u.pd.value=1;assert(render<P>(&in,p,&src,nullptr,&dst,max)==0);
    v[SOURCE].u.pd.value=3;v[OUTPUT_MODE].u.pd.value=3;
    assert(render<P>(&in,p,&src,nullptr,&dst,max)==PF_Err_BAD_CALLBACK_PARAM);
    assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        int i=y*stride+x;
        assert(std::abs(float(output[i].red)-float(depth[i].red)/max*pixels[i].alpha)<=(max==1?1e-6f:1.f));
    }
    // Baked path preserves translated crop and exact zero-Amount bypass.
    in.output_origin_x=-7;in.output_origin_y=-6;
    assert(render<P>(&in,p,&src,&dep,&crop,max)==0);
    for(int y=0;y<crop.height;++y) for(int x=0;x<crop.width;++x) assert(std::memcmp(&tile[y*stride+x],&output[(y+6)*stride+x+7],sizeof(P))==0);
    in.output_origin_x=in.output_origin_y=0;
    v[OUTPUT_MODE].u.pd.value=1;v[AMOUNT].u.fs_d.value=0;
    assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
    assert(std::memcmp(pixels.data(),output.data(),pixels.size()*sizeof(P))==0);
    v[AMOUNT].u.fs_d.value=100;
    // Line Art on a texture-less image (constant straight RGB, see pixel setup above) finds no
    // edge anywhere, so it must byte-match the flat/no-op behavior above with no Depth Layer.
    v[SOURCE].u.pd.value=4;v[OUTPUT_MODE].u.pd.value=1;
    assert(render<P>(&in,p,&src,nullptr,&dst,max)==0);
    assert(std::memcmp(pixels.data(),output.data(),pixels.size()*sizeof(P))==0);
    // Tile adapter must equal a crop here too.
    in.output_origin_x=-7;in.output_origin_y=-6;
    assert(render<P>(&in,p,&src,nullptr,&crop,max)==0);
    for(int y=0;y<crop.height;++y) for(int x=0;x<crop.width;++x) assert(std::memcmp(&tile[y*stride+x],&output[(y+6)*stride+x+7],sizeof(P))==0);
    in.output_origin_x=in.output_origin_y=0;
    v[SOURCE].u.pd.value=2;
    if(max==1) {
        pixels[0]={Ch(1),Ch(4),Ch(2),Ch(1)};
        pixels[1]={Ch(0),Ch(.3),Ch(.7),Ch(.8)};
        pixels[2]={Ch(1),Ch(std::numeric_limits<float>::quiet_NaN()),Ch(.2),Ch(.3)};
        pixels[3]={Ch(std::numeric_limits<float>::quiet_NaN()),Ch(.2),Ch(.3),Ch(.4)};
        v[SOURCE].u.pd.value=2;v[OUTPUT_MODE].u.pd.value=1;
        assert(render<P>(&in,p,&src,&dep,&dst,max)==0);
        assert(std::memcmp(pixels.data(),output.data(),4*sizeof(P))==0);
    }
    assert(render<P>(&in,p,nullptr,nullptr,&dst,max)==0);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {P zero{};assert(std::memcmp(&output[y*stride+x],&zero,sizeof(P))==0);}
}
// Line Art must actually react to a real edge, and Invert must flip which side it reacts to.
template<class P> void checkLineArtEdge(float max) {
    constexpr int w=40,h=24,stride=45;using Ch=decltype(P{}.red);
    std::vector<P> pixels(stride*h),output(stride*h),inverted(stride*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        auto& q=pixels[y*stride+x];q.alpha=Ch(max);
        float v=x<w/2?.08f:.85f; // a hard step edge at the vertical midline
        // Not gray: a chroma boost on r=g=b would be a no-op (zero OKLab a/b to begin with).
        q.red=Ch(v*max*.7f);q.green=Ch(v*max*.4f);q.blue=Ch(v*max*.15f);
    }
    PF_EffectWorld src{},dst{},dst2{};
    src.width=dst.width=dst2.width=w;src.height=dst.height=dst2.height=h;
    src.rowbytes=dst.rowbytes=dst2.rowbytes=stride*sizeof(P);
    src.data=reinterpret_cast<PF_PixelPtr>(pixels.data());dst.data=reinterpret_cast<PF_PixelPtr>(output.data());dst2.data=reinterpret_cast<PF_PixelPtr>(inverted.data());
    PF_InData in{};in.width=w;in.height=h;in.downsample_x={1,1};in.downsample_y={1,1};in.pixel_aspect_ratio={1,1};in.inter.abort=noAbort;
    PF_ParamDef v[COUNT]{};PF_ParamDef* p[COUNT];for(int i=0;i<COUNT;++i) p[i]=&v[i];
    v[RADIUS].u.fs_d.value=6;v[CONTRAST].u.fs_d.value=1;v[SOURCE].u.pd.value=4;v[OUTPUT_MODE].u.pd.value=1;v[TRANSFER].u.pd.value=1;v[AMOUNT].u.fs_d.value=100;
    assert(render<P>(&in,p,&src,nullptr,&dst,max)==0); // no Depth Layer required for Line Art
    bool changed=false;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {size_t i=y*stride+x;changed|=std::memcmp(&pixels[i],&output[i],sizeof(P))!=0;}
    assert(changed);
    v[INVERT].u.bd.value=TRUE;
    assert(render<P>(&in,p,&src,nullptr,&dst2,max)==0);
    assert(std::memcmp(output.data(),inverted.data(),output.size()*sizeof(P))!=0);
}
int main() {
    static_assert(PF_VERSION(0,1,0,PF_Stage_DEVELOP,1)==32769,"PiPL version");
    static_assert(PF_OutFlag_DEEP_COLOR_AWARE==0x02000000,"PiPL flags");
    static_assert((PF_OutFlag2_SUPPORTS_SMART_RENDER|PF_OutFlag2_FLOAT_COLOR_AWARE)==0x1400,"PiPL flags2");
    check<PF_Pixel8>(255);check<PF_Pixel16>(32768);check<PF_PixelFloat>(1);
    checkLineArtEdge<PF_Pixel8>(255);checkLineArtEdge<PF_Pixel16>(32768);checkLineArtEdge<PF_PixelFloat>(1);
    std::puts("PASS: 8/16/32 adapter, alpha, Amount=0 exact, translated crop exact, AO+Cavity, missing depth, RGB fallback, PiPL flags/version, Line Art");
}
