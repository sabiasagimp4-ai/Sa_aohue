#define PF_DEEP_COLOR_AWARE 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_PluginData.h"
#include "SPBasic.h"
#include "core.h"
#include <cstring>
#include <memory>

enum {INPUT,AMOUNT,RADIUS,CONTRAST,OUTPUT_MODE,INVERT,EDGE,TRANSFER,COUNT};
static PF_Err setup(PF_InData* in_data,PF_OutData* out_data) {
    PF_ParamDef def{};
    PF_ADD_FLOAT_SLIDERX("Amount",0,400,0,200,100,PF_Precision_TENTHS,0,0,AMOUNT);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Radius",1,512,1,128,24,PF_Precision_TENTHS,0,0,RADIUS);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Contrast",.1,4,.1,4,1,PF_Precision_HUNDREDTHS,0,0,CONTRAST);
    AEFX_CLR_STRUCT(def); PF_ADD_POPUP("Output Mode",3,1,"Composite|Lines|Lines Inverted",OUTPUT_MODE);
    PF_ADD_CHECKBOXX("Invert Lines",FALSE,0,INVERT);
    AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Line Threshold",0,100,0,100,50,PF_Precision_TENTHS,0,0,EDGE);
    AEFX_CLR_STRUCT(def); PF_ADD_POPUP("RGB Encoding",2,1,"sRGB|Linear sRGB",TRANSFER);
    out_data->num_params=COUNT;return PF_Err_NONE;
}
// Checked-out parameter storage never escapes the selector.
struct Params {
    PF_InData* in; PF_ParamDef v[COUNT]{}; PF_ParamDef* p[COUNT]{}; bool checked[COUNT]{};
    explicit Params(PF_InData* i):in(i) {}
    PF_Err get(int n) {
        PF_Err e=PF_CHECKOUT_PARAM(in,n,in->current_time,in->time_step,in->time_scale,&v[n]);
        if(!e) {checked[n]=true;p[n]=&v[n];}return e;
    }
    ~Params() {for(int n=1;n<COUNT;++n) if(checked[n]) PF_CHECKIN_PARAM(in,&v[n]);}
};
static aohue::Options options(PF_ParamDef** p) {
    aohue::Options o;o.amount=float(p[AMOUNT]->u.fs_d.value)/100;
    o.radius=float(p[RADIUS]->u.fs_d.value);o.contrast=float(p[CONTRAST]->u.fs_d.value);
    o.edge=float(p[EDGE]->u.fs_d.value)/100;return o;
}
template<class Pixel> static Pixel* row(PF_EffectWorld* w,int y) {return reinterpret_cast<Pixel*>(reinterpret_cast<char*>(w->data)+ptrdiff_t(y)*w->rowbytes);}
template<class Pixel> static aohue::RGB straight(Pixel p,float) {
    float a=float(p.alpha);return a>0?aohue::RGB{float(p.red)/a,float(p.green)/a,float(p.blue)/a}:aohue::RGB{0,0,0};
}
template<class Pixel> static void write(Pixel& p,aohue::RGB c,float max) {
    using Channel=decltype(p.red);
    if(max==1) {p.red=Channel(c.r*p.alpha);p.green=Channel(c.g*p.alpha);p.blue=Channel(c.b*p.alpha);}
    else {p.red=Channel(aohue::unit(c.r)*p.alpha+.5f);p.green=Channel(aohue::unit(c.g)*p.alpha+.5f);p.blue=Channel(aohue::unit(c.b)*p.alpha+.5f);}
}
template<class Pixel> static PF_Err render(PF_InData* in,PF_ParamDef** p,PF_EffectWorld* src,PF_EffectWorld* dst,float max) {
    if(!dst||!dst->data) return PF_Err_NONE;
    if(!src||!src->data||src->width<=0||src->height<=0) {
        for(int y=0;y<dst->height;++y) std::fill_n(row<Pixel>(dst,y),dst->width,Pixel{});
        return PF_Err_NONE;
    }
    bool linear=p[TRANSFER]->u.pd.value==2;
    auto o=options(p);int mode=p[OUTPUT_MODE]->u.pd.value;
    // Composite Amount=0 is already an exact copy. Bypass before allocating the
    // luminance/validity fields or running any neighborhood/color calculations.
    if(mode==1&&o.amount==0) {
        for(int y=0;y<dst->height;++y) {
            if(PF_Err e=PF_ABORT(in)) return e;
            auto output=row<Pixel>(dst,y);
            int yy=y-in->output_origin_y;
            for(int x=0;x<dst->width;++x) {
                int xx=x-in->output_origin_x;
                output[x]=(xx<0||yy<0||xx>=src->width||yy>=src->height)?Pixel{}:row<Pixel>(src,yy)[xx];
            }
        }
        return PF_Err_NONE;
    }
    aohue::Field f(src->width,src->height);
    for(int y=0;y<f.h;++y) {
        if(PF_Err e=PF_ABORT(in)) return e;
        for(int x=0;x<f.w;++x) {
            size_t i=size_t(y)*f.w+x;Pixel q=row<Pixel>(src,y)[x];
            auto c=straight(q,max);f.valid[i]=std::isfinite(float(q.alpha))?aohue::unit(float(q.alpha)/max):0;
            if(!linear) c={aohue::decode(aohue::unit(c.r)),aohue::decode(aohue::unit(c.g)),aohue::decode(aohue::unit(c.b))};
            f.d[i]=aohue::unit(aohue::toLab(c).r);
        }
    }
    auto mb=aohue::lineArt(f,o,p[INVERT]->u.bd.value!=0);
    for(int y=0;y<dst->height;++y) {
        if(PF_Err e=PF_ABORT(in)) return e;
        auto output=row<Pixel>(dst,y);
        for(int x=0;x<dst->width;++x) {
            int xx=x-in->output_origin_x,yy=y-in->output_origin_y;
            if(xx<0||yy<0||xx>=src->width||yy>=src->height) {output[x]=Pixel{};continue;}
            Pixel original=row<Pixel>(src,yy)[xx];output[x]=original;
            if(mode==1&&(o.amount==0||!std::isfinite(float(original.alpha))||original.alpha<=0||!aohue::gamut(straight(original,max)))) continue;
            float mask=mb[size_t(yy)*f.w+xx];
            if(mode==1) {if(mask==0) continue;write(output[x],aohue::chroma(straight(original,max),mask,o.amount,linear),max);}
            else {float v=mode==2?1-mask:mask;write(output[x],{v,v,v},max);}
        }
    }return PF_Err_NONE;
}
static PF_Err preRender(PF_InData* in,PF_PreRenderExtra* extra) {
    PF_RenderRequest req=extra->input->output_request;
    // ponytail: full-frame checkout for stable neighborhood access; halo tiles when memory matters.
    req.rect={-1000000,-1000000,1000000,1000000};req.preserve_rgb_of_zero_alpha=TRUE;
    PF_CheckoutResult result{};
    PF_Err e=extra->cb->checkout_layer(in->effect_ref,INPUT,INPUT,&req,in->current_time,in->time_step,in->time_scale,&result);
    if(e) return e;
    extra->output->result_rect=result.result_rect;extra->output->max_result_rect=result.max_result_rect;
    extra->output->flags|=PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS;
    return PF_Err_NONE;
}
static PF_Err smartRender(PF_InData* in,PF_SmartRenderExtra* extra) {
    Params p(in);for(int n=1;n<COUNT;++n) {PF_Err e=p.get(n);if(e) return e;}
    PF_EffectWorld *src=nullptr,*dst=nullptr;
    PF_Err e=extra->cb->checkout_layer_pixels(in->effect_ref,INPUT,&src);if(e) return e;
    e=extra->cb->checkout_output(in->effect_ref,&dst);if(e) return e;
    const PF_WorldSuite2* suite=nullptr;
    e=in->pica_basicP->AcquireSuite(kPFWorldSuite,kPFWorldSuiteVersion2,reinterpret_cast<const void**>(&suite));if(e) return e;
    PF_PixelFormat fmt{},sf{};e=suite->PF_GetPixelFormat(dst,&fmt);
    if(!e&&src) e=suite->PF_GetPixelFormat(src,&sf);
    in->pica_basicP->ReleaseSuite(kPFWorldSuite,kPFWorldSuiteVersion2);
    if(e) return e;if(fmt!=sf) return PF_Err_BAD_CALLBACK_PARAM;
    switch(fmt) {
        case PF_PixelFormat_ARGB32:return render<PF_Pixel8>(in,p.p,src,dst,255);
        case PF_PixelFormat_ARGB64:return render<PF_Pixel16>(in,p.p,src,dst,32768);
        case PF_PixelFormat_ARGB128:return render<PF_PixelFloat>(in,p.p,src,dst,1);
        default:return PF_Err_BAD_CALLBACK_PARAM;
    }
}
extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr ptr,PF_PluginDataCB2 cb,SPBasicSuite*,const char*,const char*) {
    PF_Err result=PF_Err_INVALID_CALLBACK;
    PF_REGISTER_EFFECT_EXT2(ptr,cb,"Sa_aohue","Sabiasagi Sa_aohue","Sabiasagi",AE_RESERVED_INFO,"EffectMain","");
    return result;
}
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd,PF_InData* in,PF_OutData* out,PF_ParamDef** p,PF_LayerDef* output,void* extra) {
    try {
        switch(cmd) {
            case PF_Cmd_ABOUT:strcpy_s(out->return_msg,"Sa_aohue 0.1 - line-art guided OKLab chroma.");return PF_Err_NONE;
            case PF_Cmd_GLOBAL_SETUP:
                out->my_version=PF_VERSION(0,1,0,PF_Stage_DEVELOP,1);
                out->out_flags=PF_OutFlag_DEEP_COLOR_AWARE;
                out->out_flags2=PF_OutFlag2_SUPPORTS_SMART_RENDER|PF_OutFlag2_FLOAT_COLOR_AWARE;
                return PF_Err_NONE;
            case PF_Cmd_PARAMS_SETUP:return setup(in,out);
            case PF_Cmd_SMART_PRE_RENDER:return preRender(in,static_cast<PF_PreRenderExtra*>(extra));
            case PF_Cmd_SMART_RENDER:return smartRender(in,static_cast<PF_SmartRenderExtra*>(extra));
            case PF_Cmd_RENDER:
                return PF_WORLD_IS_DEEP(output)?render<PF_Pixel16>(in,p,&p[0]->u.ld,output,32768):render<PF_Pixel8>(in,p,&p[0]->u.ld,output,255);
            default:return PF_Err_NONE;
        }
    }catch(const std::bad_alloc&) {return PF_Err_OUT_OF_MEMORY;}
    catch(const std::length_error&) {return PF_Err_OUT_OF_MEMORY;}
    catch(...) {return PF_Err_INTERNAL_STRUCT_DAMAGED;}
}
