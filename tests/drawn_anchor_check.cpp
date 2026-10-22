#include "drawn_anchor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
bool same(Vec3 a,Vec3 b,double tol=1e-12) {return near(a.x,b.x,tol) && near(a.y,b.y,tol) && near(a.z,b.z,tol);}
const Vec3 raw{1,2,3},anchor{1.2,2,3};
}
int main() {
    {   // blend_takes_quarter_second
        DrawnAnchor d;
        auto r=d.update(0,true,raw,anchor,10.);CHECK(r.weight==0);
        r=d.update(0,true,raw,anchor,10.125);CHECK(near(r.weight,.5,1e-9) && near(r.position.x,1.1,1e-9));
        r=d.update(0,true,raw,anchor,10.25);CHECK(near(r.weight,1,1e-9) && same(r.position,anchor,1e-9) && near(r.gap_m,0,1e-9));
        r=d.update(0,true,raw,anchor,10.30);CHECK(near(r.weight,1));
    }
    {   // bound_035
        DrawnAnchor d;const Vec3 far{2,2,3};
        double t=0;for(int i=0;i<10;++i) {d.update(0,true,raw,far,t);t+=.05;}
        const auto r=d.update(0,true,raw,far,t);
        CHECK(r.bounded && near(length(r.position-raw),DrawnAnchor::bound_m,1e-9) && near(r.gap_m,1-DrawnAnchor::bound_m,1e-9));
        CHECK(DrawnAnchor::bound_m==.35 && DrawnAnchor::blend_seconds==.25);
        DrawnAnchor e;t=0;for(int i=0;i<10;++i) {e.update(0,true,raw,anchor,t);t+=.05;}
        CHECK(!e.update(0,true,raw,anchor,t).bounded);
    }
    {   // release_blends_out: the anchor is gone but the hand leaves it over a quarter second
        DrawnAnchor d;double t=0;for(int i=0;i<8;++i) {d.update(0,true,raw,anchor,t);t+=.05;}
        auto r=d.update(0,true,raw,anchor,t);CHECK(near(r.weight,1));
        r=d.update(0,false,raw,Vec3{},t+.125);CHECK(near(r.weight,.5,1e-9) && near(r.position.x,1.1,1e-9));
        r=d.update(0,false,raw,Vec3{},t+.25);CHECK(r.weight==0 && same(r.position,raw,0));
        r=d.update(0,false,raw,Vec3{},t+.3);CHECK(r.weight==0 && r.position.x==raw.x);
    }
    {   // weight_zero_is_exactly_raw
        DrawnAnchor d;const Vec3 odd{.1+.2,1e-7,-3.3};
        const auto r=d.update(0,false,odd,anchor,1.);CHECK(r.weight==0 && r.position.x==odd.x && r.position.y==odd.y && r.position.z==odd.z && r.gap_m==0 && !r.bounded);
        const auto a=d.update(0,true,odd,anchor,1.);CHECK(a.position.x==odd.x && a.weight==0);
    }
    {   // non_finite_resets
        DrawnAnchor d;double t=0;for(int i=0;i<6;++i) {d.update(0,true,raw,anchor,t);t+=.05;}
        CHECK(d.update(0,true,raw,anchor,t).weight>0);
        auto r=d.update(0,true,Vec3{NAN,0,0},anchor,t+.05);CHECK(r.weight==0);
        r=d.update(0,true,raw,anchor,t+.1);CHECK(r.weight==0 && same(r.position,raw,0));
        for(int i=0;i<6;++i) {t+=.05;r=d.update(0,true,raw,anchor,t+.1);}
        CHECK(r.weight>0);
        r=d.update(0,true,raw,Vec3{0,INFINITY,0},t+.5);CHECK(r.weight==0 && same(r.position,raw,0));
        r=d.update(0,true,raw,anchor,NAN);CHECK(r.weight==0);
    }
    {   // hitch_resets
        DrawnAnchor d;double t=0;for(int i=0;i<8;++i) {d.update(0,true,raw,anchor,t);t+=.05;}
        CHECK(d.update(0,true,raw,anchor,t).weight>.9);
        auto r=d.update(0,true,raw,anchor,t+.3);CHECK(r.weight==0 && same(r.position,raw,0)); // 0.3 s step: no catch-up jump
        r=d.update(0,true,raw,anchor,t+.3+.05);CHECK(r.weight>0 && r.weight<.2);
        d.update(0,true,raw,anchor,t+1.);
        r=d.update(0,true,raw,anchor,t+.9);CHECK(r.weight==0); // the clock went backwards
    }
    {   // sides_independent
        DrawnAnchor d;double t=0;for(int i=0;i<8;++i) {d.update(0,true,raw,anchor,t);d.update(1,false,raw,anchor,t);t+=.05;}
        CHECK(d.update(0,true,raw,anchor,t).weight>.9);
        CHECK(d.update(1,false,raw,anchor,t).weight==0);
        d.reset(0);CHECK(d.update(0,true,raw,anchor,t+.01).weight==0);
        CHECK(d.update(5,true,raw,anchor,t).weight==0);
    }
    {   // no_teleport_on_bind: the first active frame draws the controller, whatever the anchor distance
        DrawnAnchor d;const auto r=d.update(1,true,raw,Vec3{5,5,5},42.);
        CHECK(same(r.position,raw,1e-12) && r.weight==0);
    }
    std::puts("drawn_anchor_check passed");
}
