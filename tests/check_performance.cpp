// Compile with AOHUE_REFERENCE_HEADER pointing at src/core.h from baseline 88ab06d.
// The runner extracts it from git; no duplicated production implementation is maintained.
#include "../src/core.h"
#define aohue baseline_aohue
#include AOHUE_REFERENCE_HEADER
#undef aohue
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <cstdlib>

static size_t comparisons=0;
static void equal(const std::vector<float>& a,const std::vector<float>& b,const char* stage,int w,int h) {
    ++comparisons;
    if(a.size()!=b.size() || std::memcmp(a.data(),b.data(),a.size()*sizeof(float))) {
        for(size_t i=0;i<std::min(a.size(),b.size());++i) if(std::memcmp(&a[i],&b[i],sizeof(float))) {
            std::fprintf(stderr,"FAIL %s %dx%d at %zu: %.9g != %.9g\n",stage,w,h,i,a[i],b[i]);break;
        }
        std::exit(1);
    }
}
static void check() {
    std::mt19937 rng(7123);
    const int sizes[][2]={{1,1},{2,7},{7,2},{17,9},{31,33},{65,48},{127,65},{257,129},{8193,1},{1,8193}};
    for(const auto& size:sizes) {
        int w=size[0],h=size[1];
        for(int pattern=0;pattern<5;++pattern) {
            aohue::Field f(w,h);baseline_aohue::Field old(w,h);
            for(size_t i=0;i<f.d.size();++i) {
                f.d[i]=pattern==0?.5f:pattern==1?float(i%w>=size_t(w/2)):
                    std::uniform_real_distribution<float>(0,1)(rng);
                f.valid[i]=pattern<2?1.f:pattern==2?0.f:
                    (i%7==0?0.f:std::uniform_real_distribution<float>(0,1)(rng));
            }
            old.d=f.d;old.valid=f.valid;
            for(float r:{1.f,1.6f,2.5f})
                equal(aohue::smallDisc(f.d,f.valid,w,h,r),baseline_aohue::smallDisc(old.d,old.valid,w,h,r),"disc",w,h);
            for(int r:{0,1,4,296,1024}) for(bool horizontal:{false,true}) {
                auto a=f.d,b=f.d;
                aohue::boxPass(a,f.valid,w,h,r,horizontal);baseline_aohue::boxPass(b,f.valid,w,h,r,horizontal);
                equal(a,b,horizontal?"box H":"box V",w,h);
            }
            for(float radius:{0.f,1.f,6.f,24.f,512.f}) for(bool invert:{false,true}) {
                aohue::Options p;p.radius=radius;p.edge=pattern==3?0.f:pattern==4?1.f:.5f;
                p.contrast=pattern==0?.1f:pattern==3?4.f:1.f;
                baseline_aohue::Options q;q.radius=p.radius;q.edge=p.edge;q.contrast=p.contrast;
                equal(aohue::lineArt(f,p,invert),baseline_aohue::lineArt(old,q,invert),"lineArt",w,h);
            }
        }
    }
    for(int n:{0,1,7,8,17,257}) {
        std::vector<int> visits(n);
        aohue::parallelFor(n,[&](int first,int last){for(int i=first;i<last;++i) ++visits[i];});
        for(int v:visits) if(v!=1) std::exit(2);
    }
    bool caught=false;
    try {aohue::parallelFor(257,[](int,int){throw std::bad_alloc();});}
    catch(const std::bad_alloc&) {caught=true;}
    if(!caught) std::exit(3);
    std::printf("PASS: %zu bitwise comparisons; range coverage and exception propagation\n",comparisons);
}
static volatile float sink;
template<class F> static double timed(F&& f) {
    auto start=std::chrono::steady_clock::now();
    auto result=f();sink=result[result.size()/2];
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
static void bench(int w,int h) {
    aohue::Field f(w,h);baseline_aohue::Field old(w,h);
    std::mt19937 rng(123);
    for(size_t i=0;i<f.d.size();++i) {f.d[i]=std::uniform_real_distribution<float>(0,1)(rng);f.valid[i]=i%7?1.f:.3f;}
    old.d=f.d;old.valid=f.valid;
    aohue::Options p;baseline_aohue::Options q;
    auto current=[&]{return aohue::lineArt(f,p,false);};auto previous=[&]{return baseline_aohue::lineArt(old,q,false);};
    equal(current(),previous(),"benchmark",w,h);
    std::vector<double> a,b;
    for(int i=0;i<7;++i) {
        if(i%2) {a.push_back(timed(current));b.push_back(timed(previous));}
        else {b.push_back(timed(previous));a.push_back(timed(current));}
    }
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    std::printf("%dx%d radius=24 partial alpha: baseline %.3f ms, optimized %.3f ms, %.2fx (7-run median)\n",w,h,b[3],a[3],b[3]/a[3]);
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="--bench") {bench(argc>2?std::atoi(argv[2]):1920,argc>3?std::atoi(argv[3]):1080);}
    else check();
}
