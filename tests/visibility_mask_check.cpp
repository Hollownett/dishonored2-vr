// Visibility-mask stats: hidden share of the FOV, and wasted share of a symmetric / canted render frustum.
#include "../src/visibility_stamp.hpp"
#include "../src/canted_fov.hpp"
#include <cmath>
#include <cstdio>
#define REQUIRE(x) do{if(!(x)){std::printf("FAIL %s (line %d)\n",#x,__LINE__);return 1;}}while(0)
using namespace d2vr;
namespace {
bool in_triangle(const float* xy,const std::uint32_t* t,double x,double y) {
    auto side=[&](int i,int j) {
        const double ax=xy[2*t[i]],ay=xy[2*t[i]+1],bx=xy[2*t[j]],by=xy[2*t[j]+1];
        return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
    };
    const double a=side(0,1),b=side(1,2),c=side(2,0);
    return (a>=0 && b>=0 && c>=0) || (a<=0 && b<=0 && c<=0);
}
bool covered(const LensMaskFallback& m,double x,double y) {
    for(std::size_t t=0;t+2<m.indices.size();t+=3) if(in_triangle(m.xy.data(),&m.indices[t],x,y)) return true;
    return false;
}
// Plan 1.4: the built-in lens mask for runtimes with an empty hidden mesh. Proves, by grid sampling, that the fallback never reaches
// inside its ellipse (the inner region) or outside the FOV, covers the corners beyond it, stays off visible pixels of every lens outline
// in the margin study at the VDXR Quest 3 share, keeps that property through the depth stamp's canted / symmetric projection, and that
// it is used only for an empty runtime mesh.
int lens_mask_fallback_checks() {
    // Source rule: a real mesh always wins; a failed call or a missing extension never guesses; the kill switch turns the fallback off.
    REQUIRE(lens_mask_source(true,52,156,true)==LensMaskSource::runtime);
    REQUIRE(lens_mask_source(true,52,156,false)==LensMaskSource::runtime);
    REQUIRE(lens_mask_source(true,0,0,true)==LensMaskSource::fallback);
    REQUIRE(lens_mask_source(true,0,0,false)==LensMaskSource::none);
    REQUIRE(lens_mask_source(false,0,0,true)==LensMaskSource::none);
    struct Fov {const char* name;double l,r,u,d;};
    const Fov fovs[]={{"VDXR Quest 3 left",-0.933,1.000,1.111,-1.192},{"VDXR Quest 3 right",-1.000,0.933,1.111,-1.192},
                      {"symmetric 90 deg",-1,1,1,-1},{"wide asymmetric",-1.4,0.8,0.9,-1.3}};
    for(const auto& f:fovs) for(const double scale:{LensMaskFallback::default_scale,1.1,1.2,1.4}) {
        const float left=static_cast<float>(std::atan(f.l)),right=static_cast<float>(std::atan(f.r)),up=static_cast<float>(std::atan(f.u)),down=static_cast<float>(std::atan(f.d));
        const auto m=lens_mask_fallback(left,right,up,down,scale);
        REQUIRE(m.xy.size()==2*4*(8+3) && m.indices.size()==3*4*(8+1));
        for(const auto i:m.indices) REQUIRE(i<m.xy.size()/2);
        const double l=std::tan(left),r=std::tan(right),u=std::tan(up),d=std::tan(down),cx=(l+r)/2,cy=(u+d)/2,hx=(r-l)/2,hy=(u-d)/2;
        unsigned inner=0,corner=0;
        for(int gy=0;gy<=240;++gy) for(int gx=0;gx<=240;++gx) {
            const double x=l-.1*hx+(r-l+.2*hx)*gx/240.,y=d-.1*hy+(u-d+.2*hy)*gy/240.;
            const double nx=(x-cx)/hx,ny=(y-cy)/hy,nr=std::hypot(nx,ny);
            const bool in_rect=std::abs(nx)<=1+1e-6 && std::abs(ny)<=1+1e-6;
            const bool c=covered(m,x,y);
            if(nr<scale-1e-6) {REQUIRE(!c);++inner;}           // never inside the ellipse: the inner region stays untouched
            if(!in_rect) REQUIRE(!c);                          // never outside the FOV (the stamp's own outside-FOV pieces do that)
            if(in_rect && std::abs(nx)<1-1e-4 && std::abs(ny)<1-1e-4 && nr>scale*1.001) {REQUIRE(c);++corner;} // the corners are covered
        }
        REQUIRE(inner>20000 && (scale>1.39 || corner>20));
        const auto s=visibility_mask_stats(m.xy.data(),m.xy.size()/2,m.indices.data(),m.indices.size(),left,right,up,down,RenderFrustum{{0,0,0,1},1,1});
        REQUIRE(s.valid);
        if(scale==LensMaskFallback::default_scale) std::printf("lens mask fallback %-18s scale %.2f: %zu triangles, hides %.2f%% of the FOV\n",f.name,scale,m.indices.size()/3,s.hidden_of_fov*100);
        if(f.name[0]=='V' && scale==LensMaskFallback::default_scale) REQUIRE(s.hidden_of_fov>0.005 && s.hidden_of_fov<0.012); // study: 0.91 %
    }
    REQUIRE(lens_mask_fallback(-.5f,.5f,.5f,-.5f,1.0).xy.empty() && lens_mask_fallback(-.5f,.5f,.5f,-.5f,1.5).xy.empty()); // out of range: no mesh
    REQUIRE(lens_mask_fallback(.5f,-.5f,.5f,-.5f).xy.empty()); // inverted FOV: no mesh
    // The margin study's outlines at the VDXR Quest 3 share inside the FOV (7.1 %): the default fallback covers none of their visible
    // pixels. Round lenses about the optical axis (decentred by up to 0.04 tangent) and an ellipse on the rectangle centre.
    {
        const Fov f=fovs[0];const double cx=(f.l+f.r)/2,cy=(f.u+f.d)/2,hx=(f.r-f.l)/2,hy=(f.u-f.d)/2;
        const auto m=lens_mask_fallback(static_cast<float>(std::atan(f.l)),static_cast<float>(std::atan(f.r)),static_cast<float>(std::atan(f.u)),static_cast<float>(std::atan(f.d)));
        constexpr int n=300;
        struct Outline {double ox,oy;bool ellipse;};
        const Outline outlines[]={{0,0,false},{.04,.04,false},{-.04,-.04,false},{.04,-.04,false},{-.04,.04,false},{0,0,true}};
        for(const auto& o:outlines) {
            auto radius=[&](double x,double y) {return o.ellipse?std::hypot((x-cx)/hx,(y-cy)/hy):std::hypot(x-o.ox,y-o.oy);};
            double lo=.5,hi=2.5; // the outline's size for a 7.1 % hidden share
            for(int it=0;it<40;++it) {
                const double mid=(lo+hi)/2;unsigned hidden=0;
                for(int gy=0;gy<n;++gy) for(int gx=0;gx<n;++gx) hidden+=radius(f.l+(f.r-f.l)*(gx+.5)/n,f.d+(f.u-f.d)*(gy+.5)/n)>mid;
                (static_cast<double>(hidden)/(n*n)>.071?lo:hi)=mid;
            }
            unsigned visible=0;
            for(int gy=0;gy<n;++gy) for(int gx=0;gx<n;++gx) {
                const double x=f.l+(f.r-f.l)*(gx+.5)/n,y=f.d+(f.u-f.d)*(gy+.5)/n;
                if(radius(x,y)<=lo) {REQUIRE(!covered(m,x,y));++visible;}
            }
            REQUIRE(visible>n*n*9/10);
        }
    }
    // Through the depth stamp: the fallback mesh plus the outside-FOV pieces, projected into the canted and the symmetric render frustum.
    // No stamped NDC point maps (back into the eye's tangent space) inside the ellipse and the FOV.
    {
        const float l=std::atan(-0.933f),r=std::atan(1.000f),u=std::atan(1.111f),d=std::atan(-1.192f);
        const auto m=lens_mask_fallback(l,r,u,d);
        const auto c=canted_fov(l,r,u,d);REQUIRE(c);
        const RenderFrustum canted{c->rotation,std::tan(c->half_x),std::tan(c->half_y)},sym{{0,0,0,1},std::tan(std::max(-l,r)),std::tan(std::max(u,-d))};
        const double tl=std::tan(l),tr=std::tan(r),tu=std::tan(u),td=std::tan(d),cx=(tl+tr)/2,cy=(tu+td)/2,hx=(tr-tl)/2,hy=(tu-td)/2;
        for(const auto& f:{canted,sym}) {
            const auto stamp=visibility_stamp_triangles(m.xy.data(),m.xy.size()/2,m.indices.data(),m.indices.size(),l,r,u,d,f,.01);
            REQUIRE(stamp.size()>=6*36);
            const RenderFrustum back{{-f.rotation[0],-f.rotation[1],-f.rotation[2],f.rotation[3]},1,1}; // render -> eye
            auto in_ndc_tri=[](const float* t,double x,double y) {
                auto side=[&](int i,int j) {return (t[2*j]-t[2*i])*(y-t[2*i+1])-(t[2*j+1]-t[2*i+1])*(x-t[2*i]);};
                const double a=side(0,1),b=side(1,2),c2=side(2,0);
                return (a>=0 && b>=0 && c2>=0) || (a<=0 && b<=0 && c2<=0);
            };
            unsigned inner=0,stamped_corner=0;
            for(int gy=0;gy<=200;++gy) for(int gx=0;gx<=200;++gx) {
                const double x=-1+gx*.01,y=-1+gy*.01;
                const auto e=visibility_mask_detail::project(back,x*f.tan_x,y*f.tan_y);
                if(std::isnan(e[0])) continue;
                const double nx=(e[0]-cx)/hx,ny=(e[1]-cy)/hy;
                bool stamped=false;for(std::size_t t=0;t<stamp.size() && !stamped;t+=6) stamped=in_ndc_tri(&stamp[t],x,y);
                if(std::abs(nx)<.99 && std::abs(ny)<.99 && std::hypot(nx,ny)<LensMaskFallback::default_scale-1e-3) {REQUIRE(!stamped);++inner;}
                if(std::abs(nx)<.99 && std::abs(ny)<.99 && std::hypot(nx,ny)>LensMaskFallback::default_scale*1.002 && stamped) ++stamped_corner;
            }
            REQUIRE(inner>20000 && stamped_corner>20);
        }
    }
    return 0;
}
} // namespace
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
    if(const int failed=lens_mask_fallback_checks()) return failed;
    std::printf("PASS visibility mask stats\n");
    return 0;
}
