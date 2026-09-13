// Measurements behind docs/KNOWN_ISSUES.md sections 2 and 3. Not part of CI.
//
//   g++ -std=c++17 -O2 -pthread -ffp-contract=off -I. -o hotspot tests/hotspot_study.cpp && ./hotspot
//
// No AE SDK. Reports (a) where per-frame time actually goes in render(), (b) what the
// unparallelized loops cost, (c) how much three candidate fixes to chroma() recover, and
// (d) the float32 error the running-sum blur accumulates against a double reference.
#include "../src/core.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace std::chrono;
static volatile float sink;
template<class F> static double best(F&& f, int n = 5) {
    double b = 1e18;
    for(int i = 0; i < n; ++i) {
        auto t = steady_clock::now(); f();
        b = std::min(b, duration<double,std::milli>(steady_clock::now() - t).count());
    }
    return b;
}

// chroma() with only the gamut search restructured to test the requested gain first,
// which is what ymm/Shaders/Composite.hlsl already does.
static aohue::RGB chromaEarly(aohue::RGB rgb, float mask, float amount, bool linear) {
    if(amount <= 0 || mask <= 0 || !aohue::gamut(rgb)) return rgb;
    aohue::RGB c = linear ? rgb : aohue::RGB{aohue::decode(rgb.r),aohue::decode(rgb.g),aohue::decode(rgb.b)};
    aohue::RGB lab = aohue::toLab(c);
    float target = 1 + amount * mask, lo = target, hi = target;
    if(!aohue::gamut(aohue::fromLab({lab.r,lab.g*target,lab.b*target}))) {
        lo = 1;
        for(int i = 0; i < 12; ++i) {
            float g = (lo + hi) * .5f;
            if(aohue::gamut(aohue::fromLab({lab.r,lab.g*g,lab.b*g}))) lo = g; else hi = g;
        }
    }
    if(lo == 1) return rgb;
    c = aohue::fromLab({lab.r,lab.g*lo,lab.b*lo});
    return linear ? c : aohue::RGB{aohue::encode(c.r),aohue::encode(c.g),aohue::encode(c.b)};
}
// The same, reusing the linear RGB that render() already computed for the luminance field
// instead of decoding the source pixel a second time.
static aohue::RGB chromaCached(aohue::RGB lin, float mask, float amount) {
    if(amount <= 0 || mask <= 0 || !aohue::gamut(lin)) return lin;
    aohue::RGB lab = aohue::toLab(lin);
    float target = 1 + amount * mask, lo = target, hi = target;
    if(!aohue::gamut(aohue::fromLab({lab.r,lab.g*target,lab.b*target}))) {
        lo = 1;
        for(int i = 0; i < 12; ++i) {
            float g = (lo + hi) * .5f;
            if(aohue::gamut(aohue::fromLab({lab.r,lab.g*g,lab.b*g}))) lo = g; else hi = g;
        }
    }
    if(lo == 1) return lin;
    aohue::RGB c = aohue::fromLab({lab.r,lab.g*lo,lab.b*lo});
    return {aohue::encode(c.r),aohue::encode(c.g),aohue::encode(c.b)};
}

static void frameBudget(int w, int h) {
    size_t n = size_t(w) * h;
    std::mt19937 rng(5);
    std::vector<float> r(n),g(n),b(n),a(n);
    for(size_t i = 0; i < n; ++i) {
        r[i] = std::uniform_real_distribution<float>(0,1)(rng);
        g[i] = std::uniform_real_distribution<float>(0,1)(rng);
        b[i] = std::uniform_real_distribution<float>(0,1)(rng);
        a[i] = i % 7 ? 1.f : .3f;
    }
    aohue::Field f(w,h);
    auto convert = [&](size_t i) {
        aohue::RGB c{aohue::decode(r[i]),aohue::decode(g[i]),aohue::decode(b[i])};
        f.d[i] = aohue::unit(aohue::toLab(c).r); f.valid[i] = a[i];
    };
    double t1 = best([&]{ for(size_t i = 0; i < n; ++i) convert(i); sink = f.d[n/2]; });
    double t1p = best([&]{
        aohue::parallelFor(h,[&](int y0,int y1){ for(size_t i = size_t(y0)*w; i < size_t(y1)*w; ++i) convert(i); });
        sink = f.d[n/2];
    });
    aohue::Options o;
    double t2 = best([&]{ auto m = aohue::lineArt(f,o,false); sink = m[n/2]; });
    auto mask = aohue::lineArt(f,o,false);
    auto m = mask;
    // The exponent is 1/max(.1f, contrast) at runtime, so it must not be a compile-time
    // constant here or the compiler folds pow(x, 1.f) away and the loop looks free.
    volatile float contrast = 1.f;
    double t3 = best([&]{ float c = contrast, e = 1.f/std::max(.1f,c); for(auto& v : m) v = std::pow(aohue::unit(v),e); sink = m[n/2]; });
    size_t hit = 0; for(size_t i = 0; i < n; ++i) if(mask[i] > 0) ++hit;
    double t4 = best([&]{
        aohue::RGB acc{0,0,0};
        for(size_t i = 0; i < n; ++i) { if(mask[i] <= 0) continue; acc = aohue::chroma({r[i],g[i],b[i]},mask[i],1.f,false); }
        sink = acc.r;
    });
    printf("=== %dx%d per-frame budget (mask>0 on %.0f%% of pixels) ===\n",w,h,100.0*hit/n);
    printf("  RGB->L convert   single %8.2f ms   parallel %8.2f ms  (%.1fx available)\n",t1,t1p,t1/t1p);
    printf("  lineArt          parallel already        %8.2f ms\n",t2);
    printf("  Contrast pow     single %8.2f ms   (Contrast=1 is pow(x,1))\n",t3);
    printf("  chroma composite single %8.2f ms\n",t4);
    printf("  -> %.1f ms total; docs/PERFORMANCE.md measures only the %.1f ms lineArt (%.0f%%)\n\n",
           t1+t2+t3+t4,t2,100*t2/(t1+t2+t3+t4));
}

static void chromaFixes() {
    int w = 1920, h = 1080; size_t n = size_t(w) * h;
    std::mt19937 rng(5);
    std::vector<float> r(n),g(n),b(n),lr(n),lg(n),lb(n),mask(n);
    for(size_t i = 0; i < n; ++i) {
        r[i] = std::uniform_real_distribution<float>(.15f,.85f)(rng);
        g[i] = std::uniform_real_distribution<float>(.15f,.85f)(rng);
        b[i] = std::uniform_real_distribution<float>(.15f,.85f)(rng);
        lr[i] = aohue::decode(r[i]); lg[i] = aohue::decode(g[i]); lb[i] = aohue::decode(b[i]);
    }
    printf("=== chroma() composite loop, 1920x1080, three candidate fixes ===\n");
    for(double cover : {1.0,0.5,0.2}) {
        for(size_t i = 0; i < n; ++i) mask[i] = std::uniform_real_distribution<float>(0,1)(rng) < cover ? .6f : 0.f;
        double a0 = best([&]{ aohue::RGB acc{0,0,0}; for(size_t i=0;i<n;++i){ if(mask[i]<=0) continue; acc=aohue::chroma({r[i],g[i],b[i]},mask[i],1.f,false);} sink=acc.r; });
        double a1 = best([&]{ aohue::RGB acc{0,0,0}; for(size_t i=0;i<n;++i){ if(mask[i]<=0) continue; acc=chromaEarly({r[i],g[i],b[i]},mask[i],1.f,false);} sink=acc.r; });
        double a2 = best([&]{ aohue::RGB acc{0,0,0}; for(size_t i=0;i<n;++i){ if(mask[i]<=0) continue; acc=chromaCached({lr[i],lg[i],lb[i]},mask[i],1.f);} sink=acc.r; });
        double a3 = best([&]{ aohue::parallelFor(h,[&](int y0,int y1){ aohue::RGB acc{0,0,0};
            for(size_t i=size_t(y0)*w;i<size_t(y1)*w;++i){ if(mask[i]<=0) continue; acc=chromaCached({lr[i],lg[i],lb[i]},mask[i],1.f);} sink=acc.r; }); });
        printf("  coverage %3.0f%%: current %7.2f  +test-target-gain-first %7.2f (%.2fx)"
               "  +reuse linear RGB %7.2f (%.2fx)  +threads %7.2f (%.2fx)\n",
               cover*100,a0,a1,a0/a1,a2,a0/a2,a3,a0/a3);
    }
    double diff = 0;
    for(size_t i = 0; i < 20000; ++i) {
        aohue::RGB in{r[i],g[i],b[i]};
        auto x = aohue::chroma(in,.6f,1.f,false), y = chromaEarly(in,.6f,1.f,false);
        diff = std::max({diff,(double)std::fabs(x.r-y.r),(double)std::fabs(x.g-y.g),(double)std::fabs(x.b-y.b)});
    }
    printf("  output change from testing the target gain first: max %.3e (one 8-bit step = %.3e)\n\n",diff,1.0/255);

    printf("=== gamut search never reaches the requested gain (12 halvings from lo=1) ===\n");
    for(float amount : {1.f,3.f}) for(float mask1 : {.25f,1.f}) {
        float target = 1 + amount * mask1, lo = 1, hi = target;
        for(int i = 0; i < 12; ++i) lo = (lo + hi) * .5f;
        printf("  amount=%.0f mask=%.2f: requested %.5f, applied %.5f, short by %.4f%%\n",
               amount,mask1,target,lo,(target-lo)/target*100);
    }
    printf("\n");
}

static void blurAccuracy() {
    auto passD = [](const std::vector<double>& v,const std::vector<float>& valid,std::vector<double>& out,
                    int w,int h,int r,bool horiz) {
        if(horiz) for(int y = 0; y < h; ++y) {
            size_t base = size_t(y)*w; std::vector<double> sv(w+1,0),sw(w+1,0);
            for(int x = 0; x < w; ++x) { sv[x+1]=sv[x]+v[base+x]*valid[base+x]; sw[x+1]=sw[x]+valid[base+x]; }
            for(int x = 0; x < w; ++x) { int lo=std::max(0,x-r),hi=std::min(w-1,x+r); double ws=sw[hi+1]-sw[lo];
                out[base+x] = ws>0 ? (sv[hi+1]-sv[lo])/ws : v[base+x]; }
        } else for(int x = 0; x < w; ++x) {
            std::vector<double> sv(h+1,0),sw(h+1,0);
            for(int y = 0; y < h; ++y) { size_t i=size_t(y)*w+x; sv[y+1]=sv[y]+v[i]*valid[i]; sw[y+1]=sw[y]+valid[i]; }
            for(int y = 0; y < h; ++y) { int lo=std::max(0,y-r),hi=std::min(h-1,y+r); double ws=sw[hi+1]-sw[lo];
                out[size_t(y)*w+x] = ws>0 ? (sv[hi+1]-sv[lo])/ws : v[size_t(y)*w+x]; }
        }
    };
    printf("=== fastBlurInPlace float32 error against a double-precision reference ===\n");
    printf("  input is the binary line plane lineArt actually blurs\n");
    std::mt19937 rng(4);
    for(auto d : {std::pair<int,int>{1920,1080},{3840,2160}}) for(float radius : {6.f,24.f,128.f}) {
        int w = d.first, h = d.second;
        std::vector<float> v(size_t(w)*h),valid(size_t(w)*h,1.f);
        for(auto& x : v) x = std::uniform_real_distribution<float>(0,1)(rng) < .3f ? 1.f : 0.f;
        std::vector<double> dv(v.begin(),v.end()),ds(dv.size());
        auto f = v; aohue::fastBlurInPlace(f,valid,w,h,radius);
        int r = std::max(1,int(std::round(radius/std::sqrt(3.f))));
        for(int p = 0; p < 3; ++p) { passD(dv,valid,ds,w,h,r,true); passD(ds,valid,dv,w,h,r,false); }
        double e = 0; for(size_t i = 0; i < f.size(); ++i) e = std::max(e,std::fabs(double(f[i])-dv[i]));
        printf("  %4dx%-4d Radius %3.0f: max error %.3e = %5.2f%% of an 8-bit step, %6.2f%% of a 16-bit step\n",
               w,h,radius,e,e/(1.0/255)*100,e/(1.0/32768)*100);
    }
    printf("\n=== fastBlur radius quantization: r = max(1, round(Radius/sqrt(3))) ===\n");
    for(float radius : {1.f,2.f,2.5f,2.6f,3.f,4.f,6.f,24.f}) {
        int r = std::max(1,int(std::round(radius/std::sqrt(3.f))));
        printf("  Radius %5.2f -> box r=%2d, effective sigma=sqrt(r*(r+1))=%7.3f\n",radius,r,std::sqrt(float(r)*(r+1)));
    }
    printf("  Radius 1.00-2.59 all produce the same output; sigma below 1.414 is unreachable.\n\n");
}

int main() {
    printf("threads reported by hardware_concurrency(): %u\n\n",std::thread::hardware_concurrency());
    frameBudget(1920,1080);
    frameBudget(3840,2160);
    chromaFixes();
    blurAccuracy();
    size_t bad = 0; std::mt19937 rng(9);
    for(int i = 0; i < 2000000; ++i) { float x = std::uniform_real_distribution<float>(0,1)(rng); if(std::pow(x,1.0f) != x) ++bad; }
    printf("=== pow(x,1.0f) != x over 2,000,000 samples: %zu ===\n",bad);
    printf("  Contrast=1 (the default) can skip the gamma loop with bit-identical output.\n");
    return 0;
}
