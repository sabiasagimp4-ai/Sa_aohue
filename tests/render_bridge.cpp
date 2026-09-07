// Offline harness: runs the REAL Sa_aohue.cpp render() path (no AE host) on an arbitrary
// image, so we can test against a user-supplied screenshot instead of launching AE.
#include "../src/Sa_aohue.cpp"
static PF_Err noAbort2(PF_ProgPtr) {return PF_Err_NONE;}
// mode: 1=RGB Approximation, 4=Line Art. r,g,b,a: w*h planar uint8 in/out buffers (alpha=255 for opaque input).
extern "C" __declspec(dllexport) int renderNative(
    unsigned char* r,unsigned char* g,unsigned char* b,unsigned char* a,int w,int h,
    int source,float radius,float contrast,float detail,float edge,int invert,
    float amount,int outputMode,int transfer,
    unsigned char* orr,unsigned char* org,unsigned char* orb,unsigned char* ora) {
    try {
        std::vector<PF_Pixel8> pixels(size_t(w)*h),output(size_t(w)*h);
        for(int i=0;i<w*h;++i) {
            pixels[i].alpha=a[i];pixels[i].red=r[i];pixels[i].green=g[i];pixels[i].blue=b[i];
        }
        PF_EffectWorld src{},dst{};
        src.width=dst.width=w;src.height=dst.height=h;
        src.rowbytes=dst.rowbytes=w*sizeof(PF_Pixel8);
        src.data=reinterpret_cast<PF_PixelPtr>(pixels.data());dst.data=reinterpret_cast<PF_PixelPtr>(output.data());
        PF_InData in{};in.width=w;in.height=h;in.downsample_x={1,1};in.downsample_y={1,1};in.pixel_aspect_ratio={1,1};in.inter.abort=noAbort2;
        PF_ParamDef v[COUNT]{};PF_ParamDef* p[COUNT];for(int i=0;i<COUNT;++i) p[i]=&v[i];
        v[RADIUS].u.fs_d.value=radius;v[CONTRAST].u.fs_d.value=contrast;v[DETAIL].u.fs_d.value=detail;
        v[EDGE].u.fs_d.value=edge;v[INVERT].u.bd.value=invert?TRUE:FALSE;v[SOURCE].u.pd.value=source;
        v[AMOUNT].u.fs_d.value=amount;v[OUTPUT_MODE].u.pd.value=outputMode;v[TRANSFER].u.pd.value=transfer;
        PF_Err e=render<PF_Pixel8>(&in,p,&src,nullptr,&dst,255);
        if(e) return int(e);
        for(int i=0;i<w*h;++i) {ora[i]=output[i].alpha;orr[i]=output[i].red;org[i]=output[i].green;orb[i]=output[i].blue;}
        return 0;
    } catch(...) {return -1;}
}
// 16bpc variant of renderNativeDep: r/g/b/a and dr/dg/db/da are uint16 in [0,32768] (AE's
// PF_Pixel16 convention), so a caller with genuinely high-precision source data (not crushed
// to 8 bits first) can test whether an artifact is a real algorithm issue or 8-bit quantization.
extern "C" __declspec(dllexport) int renderNativeDep16(
    unsigned short* r,unsigned short* g,unsigned short* b,unsigned short* a,
    unsigned short* dr,unsigned short* dg,unsigned short* db,unsigned short* da,int w,int h,
    int source,float radius,float contrast,float detail,float edge,int invert,
    float amount,int outputMode,int transfer,
    unsigned short* orr,unsigned short* org,unsigned short* orb,unsigned short* ora) {
    try {
        std::vector<PF_Pixel16> pixels(size_t(w)*h),depth(size_t(w)*h),output(size_t(w)*h);
        for(int i=0;i<w*h;++i) {
            pixels[i].alpha=a[i];pixels[i].red=r[i];pixels[i].green=g[i];pixels[i].blue=b[i];
            depth[i].alpha=da[i];depth[i].red=dr[i];depth[i].green=dg[i];depth[i].blue=db[i];
        }
        PF_EffectWorld src{},dep{},dst{};
        src.width=dep.width=dst.width=w;src.height=dep.height=dst.height=h;
        src.rowbytes=dep.rowbytes=dst.rowbytes=w*sizeof(PF_Pixel16);
        src.data=reinterpret_cast<PF_PixelPtr>(pixels.data());dep.data=reinterpret_cast<PF_PixelPtr>(depth.data());
        dst.data=reinterpret_cast<PF_PixelPtr>(output.data());
        PF_InData in{};in.width=w;in.height=h;in.downsample_x={1,1};in.downsample_y={1,1};in.pixel_aspect_ratio={1,1};in.inter.abort=noAbort2;
        PF_ParamDef v[COUNT]{};PF_ParamDef* p[COUNT];for(int i=0;i<COUNT;++i) p[i]=&v[i];
        v[RADIUS].u.fs_d.value=radius;v[CONTRAST].u.fs_d.value=contrast;v[DETAIL].u.fs_d.value=detail;
        v[EDGE].u.fs_d.value=edge;v[INVERT].u.bd.value=invert?TRUE:FALSE;v[SOURCE].u.pd.value=source;
        v[AMOUNT].u.fs_d.value=amount;v[OUTPUT_MODE].u.pd.value=outputMode;v[TRANSFER].u.pd.value=transfer;
        PF_Err e=render<PF_Pixel16>(&in,p,&src,&dep,&dst,32768);
        if(e) return int(e);
        for(int i=0;i<w*h;++i) {ora[i]=output[i].alpha;orr[i]=output[i].red;org[i]=output[i].green;orb[i]=output[i].blue;}
        return 0;
    } catch(...) {return -1;}
}
// Same as renderNative, but source=2 (External Depth) or source=3 (External Cavity) with an
// explicit depth/cavity layer (dr/dg/db/da, same w*h as the source image; only the red channel
// is read by render(), matching the plugin's documented convention).
extern "C" __declspec(dllexport) int renderNativeDep(
    unsigned char* r,unsigned char* g,unsigned char* b,unsigned char* a,
    unsigned char* dr,unsigned char* dg,unsigned char* db,unsigned char* da,int w,int h,
    int source,float radius,float contrast,float detail,float edge,int invert,
    float amount,int outputMode,int transfer,
    unsigned char* orr,unsigned char* org,unsigned char* orb,unsigned char* ora) {
    try {
        std::vector<PF_Pixel8> pixels(size_t(w)*h),depth(size_t(w)*h),output(size_t(w)*h);
        for(int i=0;i<w*h;++i) {
            pixels[i].alpha=a[i];pixels[i].red=r[i];pixels[i].green=g[i];pixels[i].blue=b[i];
            depth[i].alpha=da[i];depth[i].red=dr[i];depth[i].green=dg[i];depth[i].blue=db[i];
        }
        PF_EffectWorld src{},dep{},dst{};
        src.width=dep.width=dst.width=w;src.height=dep.height=dst.height=h;
        src.rowbytes=dep.rowbytes=dst.rowbytes=w*sizeof(PF_Pixel8);
        src.data=reinterpret_cast<PF_PixelPtr>(pixels.data());dep.data=reinterpret_cast<PF_PixelPtr>(depth.data());
        dst.data=reinterpret_cast<PF_PixelPtr>(output.data());
        PF_InData in{};in.width=w;in.height=h;in.downsample_x={1,1};in.downsample_y={1,1};in.pixel_aspect_ratio={1,1};in.inter.abort=noAbort2;
        PF_ParamDef v[COUNT]{};PF_ParamDef* p[COUNT];for(int i=0;i<COUNT;++i) p[i]=&v[i];
        v[RADIUS].u.fs_d.value=radius;v[CONTRAST].u.fs_d.value=contrast;v[DETAIL].u.fs_d.value=detail;
        v[EDGE].u.fs_d.value=edge;v[INVERT].u.bd.value=invert?TRUE:FALSE;v[SOURCE].u.pd.value=source;
        v[AMOUNT].u.fs_d.value=amount;v[OUTPUT_MODE].u.pd.value=outputMode;v[TRANSFER].u.pd.value=transfer;
        PF_Err e=render<PF_Pixel8>(&in,p,&src,&dep,&dst,255);
        if(e) return int(e);
        for(int i=0;i<w*h;++i) {ora[i]=output[i].alpha;orr[i]=output[i].red;org[i]=output[i].green;orb[i]=output[i].blue;}
        return 0;
    } catch(...) {return -1;}
}
