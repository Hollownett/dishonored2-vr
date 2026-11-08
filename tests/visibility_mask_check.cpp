// Visibility-mask stats: hidden share of the FOV, and wasted share of a symmetric / canted render frustum.
#include "../src/visibility_stamp.hpp"
#include "../src/canted_fov.hpp"
#include <cmath>
#include <cstdio>
#define REQUIRE(x) do{if(!(x)){std::printf("FAIL %s (line %d)\n",#x,__LINE__);return 1;}}while(0)
using namespace d2vr;
int main() {
    const float q=0.7853981633974483f; // 45 deg: tangent 1
    // the left half of a [-1,1]^2 FOV hidden, rendered with exactly that FOV
    const float xy[]={-1,-1, 0,-1, 0,1, -1,1};
    const std::uint32_t tri[]={0,1,2, 0,2,3};
    RenderFrustum same{{0,0,0,1},1,1};
    auto s=visibility_mask_stats(xy,4,tri,6,-q,q,q,-q,same);
    REQUIRE(s.valid && std::abs(s.hidden_of_fov-.5)<1e-6 && std::abs(s.wasted_of_render-.5)<1e-6);
    REQUIRE(s.min_x==-1 && s.max_x==0);
    // a render frustum twice as wide as the FOV: area 8, FOV 4, mask 2 -> visible 2 of 8
    RenderFrustum wide{{0,0,0,1},2,1};
    s=visibility_mask_stats(xy,4,tri,6,-q,q,q,-q,wide);
    REQUIRE(s.valid && std::abs(s.hidden_of_fov-.5)<1e-6 && std::abs(s.wasted_of_render-.75)<1e-6);
    // canted frustum of an asymmetric eye covers the FOV: waste with an empty-area mask is >= 0 and below the symmetric waste
    const float l=-0.95f,r=0.75f,u=0.8f,d=-0.85f;
    const auto c=canted_fov(l,r,u,d);REQUIRE(c);
    RenderFrustum canted{c->rotation,std::tan(c->half_x),std::tan(c->half_y)};
    RenderFrustum sym{{0,0,0,1},std::tan(std::max(-l,r)),std::tan(std::max(u,-d))};
    const float dot[]={0,0, 0,0, 0,0};const std::uint32_t one[]={0,1,2};
    const auto sc=visibility_mask_stats(dot,3,one,3,l,r,u,d,canted),ss=visibility_mask_stats(dot,3,one,3,l,r,u,d,sym);
    REQUIRE(sc.valid && ss.valid && sc.wasted_of_render>=-1e-6 && sc.wasted_of_render<=ss.wasted_of_render+1e-6);
    // v1106 stamp: grid-sampled in NDC, for the canted and the symmetric frustum. Every point clearly inside the visible FOV quad
    // (and not in a mask triangle) is left alone; every point clearly outside the quad is stamped.
    {
        const float tl=std::tan(l),tr=std::tan(r),tu=std::tan(u),td=std::tan(d);
        const float mask[]={tl,td, tl+.3f,td, tl,td+.3f, tr,tu, tr-.2f,tu, tr,tu-.25f};
        const std::uint32_t mi[]={0,1,2,3,4,5};
        auto in_tri=[](const float* t,double x,double y) {
            auto side=[&](int i,int j) {return (t[2*j]-t[2*i])*(y-t[2*i+1])-(t[2*j+1]-t[2*i+1])*(x-t[2*i]);};
            const double a=side(0,1),b=side(1,2),c=side(2,0);
            return (a>=0 && b>=0 && c>=0) || (a<=0 && b<=0 && c<=0);
        };
        for(const auto& f:{canted,sym}) {
            const auto stamp=visibility_stamp_triangles(mask,6,mi,6,l,r,u,d,f,0);
            REQUIRE(stamp.size()>=12 && stamp.size()%6==0);
            using visibility_mask_detail::project;
            std::array<std::array<double,2>,4> qd{project(f,tl,td),project(f,tr,td),project(f,tr,tu),project(f,tl,tu)};
            for(auto& c2:qd) {c2[0]/=f.tan_x;c2[1]/=f.tan_y;}
            unsigned inside_ok=0,outside_ok=0;
            for(int gy=0;gy<=200;++gy) for(int gx=0;gx<=200;++gx) {
                const double x=-1+gx*.01,y=-1+gy*.01;
                double dmin=1e9;bool in=true;
                for(int i=0;i<4;++i) {
                    const auto& p0=qd[i];const auto& p1=qd[(i+1)%4];
                    const double ex=p1[0]-p0[0],ey=p1[1]-p0[1],len=std::sqrt(ex*ex+ey*ey);
                    const double sd=(ex*(y-p0[1])-ey*(x-p0[0]))/len; // > 0 inside for counter-clockwise corners
                    if(sd<0) in=false;
                    dmin=std::min(dmin,std::abs(sd));
                }
                if(dmin<2e-3) continue; // on the quad edge
                bool masked=false,banded=false;
                for(std::size_t t=0;t<stamp.size();t+=6) if(in_tri(&stamp[t],x,y)) {if(t<12) masked=true;else banded=true;}
                if(in) {REQUIRE(!banded);++inside_ok;}
                else {REQUIRE(banded);++outside_ok;}
                (void)masked;
            }
            REQUIRE(inside_ok>20000 && outside_ok>100);
            // a margin only shrinks the stamp: every stamped point with margin is stamped without it
            const auto wider=visibility_stamp_triangles(mask,6,mi,6,l,r,u,d,f,.01);
            for(int gy=0;gy<=100;++gy) for(int gx=0;gx<=100;++gx) {
                const double x=-1+gx*.02,y=-1+gy*.02;bool w=false,n=false;
                for(std::size_t t=12;t<wider.size();t+=6) w=w||in_tri(&wider[t],x,y);
                for(std::size_t t=0;t<stamp.size();t+=6) n=n||in_tri(&stamp[t],x,y);
                REQUIRE(!w || n);
            }
        }
    }
    // bad index is rejected
    const std::uint32_t bad[]={0,1,9};
    REQUIRE(!visibility_mask_stats(xy,4,bad,3,-q,q,q,-q,same).valid);
    std::printf("PASS visibility mask stats\n");
    return 0;
}
