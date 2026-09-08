#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <queue>
#include <random>
#include <stdexcept>
using uint = unsigned int;
#include "../ymm/Shaders/PointComponents.hlsli"
#include "../ymm/Shaders/LineControls.hlsli"
void require(bool ok) { if (!ok) throw std::runtime_error("YMM line control regression"); }
constexpr int W=19;
using Image = std::array<std::array<bool,W>,W>;
bool reference(const Image& a, int x, int y, int size)
{
    if (!a[y][x] || size == 0) return false;
    Image seen{};
    std::queue<std::pair<int,int>> q;
    q.emplace(x,y); seen[y][x]=true;
    int l=x,r=x,t=y,b=y;
    while(!q.empty()) {
        auto [cx,cy]=q.front();q.pop();
        l=std::min(l,cx);r=std::max(r,cx);t=std::min(t,cy);b=std::max(b,cy);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            int nx=cx+dx,ny=cy+dy;
            if(nx>=0&&ny>=0&&nx<W&&ny<W&&a[ny][nx]&&!seen[ny][nx]){
                seen[ny][nx]=true;q.emplace(nx,ny);
            }
        }
    }
    return r-l+1<=size&&b-t+1<=size;
}
bool production(const Image& a,int x,int y,int size)
{
    uint rows[7]{};
    for(int j=0;j<7;++j)for(int i=0;i<7;++i){
        int xx=x+i-3,yy=y+j-3;
        if(xx>=0&&yy>=0&&xx<W&&yy<W&&a[yy][xx])rows[j]|=1u<<i;
    }
    return IsSmallComponent(rows,size);
}
int main()
{
    std::mt19937 rng(1291);int checked=0;
    // Independent full-image BFS includes borders and long/cyclic components.
    for(int trial=0;trial<150;++trial){
        Image a{};float density=(trial%10+1)*.06f;
        for(auto& row:a)for(auto& value:row)value=(rng()%10000)/10000.f<density;
        for(int size=0;size<=3;++size)for(int y=0;y<W;++y)for(int x=0;x<W;++x){
            require(reference(a,x,y,size)==production(a,x,y,size));++checked;
        }
    }
    for(int size=1;size<=3;++size){
        Image a{};
        for(int y=5;y<5+size;++y)for(int x=7;x<7+size;++x)a[y][x]=true;
        for(int y=5;y<5+size;++y)for(int x=7;x<7+size;++x)require(production(a,x,y,size));
        Image line{};for(int i=0;i<W;++i)line[i][i]=true;
        for(int i=0;i<W;++i)require(!production(line,i,i,size));
    }
    const float cutoff=128.f/255*.08f;
    for(int influence=0;influence<=10;++influence)for(int stability=0;stability<=10;++stability){
        float previous=0;
        for(int i=0;i<=10000;++i){
            float response=-.1f+i*.00012f;
            float mask=LineWeight(response,cutoff,influence/10.f,stability/10.f);
            require(std::isfinite(mask)&&mask>=0&&mask<=1&&mask>=previous);previous=mask;
            if(influence==0&&stability==0)require(mask==(response>cutoff?1.f:0.f));
        }
    }
    require(LineWeight(.08f,cutoff,1,0)<LineWeight(.2f,cutoff,1,0));
    float hardJump=LineWeight(cutoff+.001f,cutoff,0,0)-LineWeight(cutoff-.001f,cutoff,0,0);
    float softJump=LineWeight(cutoff+.001f,cutoff,0,1)-LineWeight(cutoff-.001f,cutoff,0,1);
    require(hardJump==1&&softJump<.001f);
    for(int i=0;i<=1000;++i){
        float source=i/1000.f;
        require(SideWeight(source,.65f,0)==1);
        require(std::abs(SideWeight(source,.65f,1)+SideWeight(source,.65f,2)-1)<1e-6);
    }
    require(SideWeight(.8f,.65f,1)==1&&SideWeight(.3f,.65f,1)==0);
    require(SideWeight(.8f,.65f,2)==0&&SideWeight(.3f,.65f,2)==1);
    std::cout<<"PASS: "<<checked<<" component decisions vs global BFS; thin diagonals; strength monotonicity; legacy defaults; threshold jitter; complementary sides\n";
}
