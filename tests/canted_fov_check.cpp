// Canted symmetric frustum, headset-agnostic: for every FOV profile the canted frustum covers the whole
// optical frustum, never costs more pixels than the enclosing symmetric one, mirrors between eyes, is the
// identity for symmetric eyes, and is at least as good as a brute-force grid search.
#include "../src/canted_fov.hpp"
#include <cstdio>
#define REQUIRE(x) do{if(!(x)){std::printf("FAIL %s (line %d)\n",#x,__LINE__);return 1;}}while(0)
using d2vr::CantedFov;
// v' = q^-1 v q  (eye direction into the canted frame)
static std::array<double,3> into_canted(const CantedFov& c,std::array<double,3> v) {
    const double x=-c.rotation[0],y=-c.rotation[1],z=-c.rotation[2],w=c.rotation[3]; // conjugate
    const double tx=2*(y*v[2]-z*v[1]),ty=2*(z*v[0]-x*v[2]),tz=2*(x*v[1]-y*v[0]);
    return {v[0]+w*tx+(y*tz-z*ty),v[1]+w*ty+(z*tx-x*tz),v[2]+w*tz+(x*ty-y*tx)};
}
static bool covers(const CantedFov& c,float l,float r,float u,float d) {
    for(int i=0;i<=64;++i) for(int side=0;side<4;++side) {
        const double t=i/64.0;
        double ax=side<2?(side==0?l:r):l+(r-l)*t,ay=side<2?d+(u-d)*t:(side==2?d:u);
        auto v=into_canted(c,{std::tan(ax),std::tan(ay),-1});
        if(v[2]>=0 || std::abs(v[0]/-v[2])>std::tan(c.half_x)+1e-5 || std::abs(v[1]/-v[2])>std::tan(c.half_y)+1e-5) return false;
    }
    return true;
}
// Independent brute force: rotate the frustum's boundary into a turned frame (0.25 degree grid).
static double grid_best(float l,float r,float u,float d) {
    double best=1e9;
    for(double yd=-20;yd<=20;yd+=.25) for(double pd=-20;pd<=20;pd+=.25) {
        CantedFov c;const double hy=-yd*3.14159265358979/360,hp=pd*3.14159265358979/360;
        c.rotation={static_cast<float>(std::cos(hy)*std::sin(hp)),static_cast<float>(std::sin(hy)*std::cos(hp)),
                    static_cast<float>(-std::sin(hy)*std::sin(hp)),static_cast<float>(std::cos(hy)*std::cos(hp))};
        double tx=0,ty=0;bool ok=true;
        for(float ax:{l,r}) for(float ay:{d,u}) {auto v=into_canted(c,{std::tan(ax),std::tan(ay),-1});if(v[2]>=0) ok=false;else {tx=std::max(tx,std::abs(v[0]/-v[2]));ty=std::max(ty,std::abs(v[1]/-v[2]));}}
        if(ok) best=std::min(best,tx*ty);
    }
    return best;
}
int main() {
    // Right-eye optical FOVs (radians): reported/approximate values of several headsets plus synthetic extremes.
    struct Profile {const char* name;float l,r,u,d;};
    const Profile profiles[]{
        {"Quest 3 (VD)",-.698132f,.942478f,.767945f,-.959931f},
        {"Quest 2",-.80f,.87f,.82f,-.87f},
        {"Pico 4",-.82f,.90f,.86f,-.86f},
        {"Index-like",-.92f,.96f,.95f,-.96f},
        {"strong asymmetry",-.35f,1.15f,.6f,-1.1f},
        {"symmetric",-.8f,.8f,.7f,-.7f},
    };
    for(const auto& p:profiles) {
        auto c=d2vr::canted_fov(p.l,p.r,p.u,p.d);REQUIRE(c);
        REQUIRE(covers(*c,p.l,p.r,p.u,p.d));
        const double symmetric=std::max(std::tan(p.r),std::tan(-p.l))*std::max(std::tan(p.u),std::tan(-p.d));
        const double canted=std::tan(c->half_x)*std::tan(c->half_y);
        REQUIRE(canted<=symmetric*(1+1e-6));
        REQUIRE(canted<=grid_best(p.l,p.r,p.u,p.d)*1.001);
        auto m=d2vr::canted_fov(-p.r,-p.l,p.u,p.d); // left eye mirrors x
        REQUIRE(m && std::abs(m->yaw+c->yaw)<1e-5f && std::abs(m->pitch-c->pitch)<1e-5f && std::abs(m->half_x-c->half_x)<1e-5f && covers(*m,-p.r,-p.l,p.u,p.d));
        const auto size=d2vr::canted_size(2000,2000,p.l,p.r,p.u,p.d,*c);
        REQUIRE(size[0]%2==0 && size[1]%2==0);
        std::printf("%-17s turn %5.2f/%5.2f deg, pixels %.3f of enclosing symmetric, %ux%u for a 2000x2000 recommendation\n",
            p.name,c->yaw*57.29578f,c->pitch*57.29578f,canted/symmetric,size[0],size[1]);
    }
    auto s=d2vr::canted_fov(-.8f,.8f,.7f,-.7f);REQUIRE(s && s->yaw==0 && s->pitch==0 && std::abs(s->half_x-.8f)<1e-6f && std::abs(s->half_y-.7f)<1e-6f);
    REQUIRE(!d2vr::canted_fov(.1f,-.1f,.5f,-.5f) && !d2vr::canted_fov(-1.6f,.5f,.5f,-.5f));
    std::printf("PASS: canted frusta cover every optical frustum with no more pixels than the enclosing symmetric one\n");
    return 0;
}
