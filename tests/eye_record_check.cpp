// v1366: the eye view records ObjectMotion places held things with (weapon_draw::Place through weapon_views).
// User, v1381 stereo: held props, the sticky grenade and the stun mine render badly. v1364 eye-records.csv (stereo sim on the user's save, head
// pitch -35): the engine's view Setup ENTRY already carries the new frame's stamp with the game's level camera; the entry snapshot filed that as
// the frame's eye record and a render thread's classify snapshot replaced it with the eye camera 3-5 ms later. Views built in between are cached
// for the frame: a held thing's draws (composed with the eye camera) were matched against the level camera, so about half the renders were not
// placed (or, with the v1320 stale-view match, placed through the level camera). v1366 takes the record at the Setup EXIT and rebuilds cached
// Views when a record of their frames changes.
// Here: per frame the engine pre-fills both eye views (stamp N, level camera), Setup entry, an early Views build (any caller in the window),
// Setup exit (eye camera), the classify snapshot, the publish, then a held prop's depth and colour draws in both eyes composed with the eye camera.
// Every render must be placed exactly at B * part with the defaults; the v1365 path (both legacy knobs) must miss them.
#include "../src/object_motion.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
using namespace d2vr::weapon_draw;
using d2vr::ObjectMotion;

static Mat rot(double ax,double ay,double az,double angle) {
    const double n=std::sqrt(ax*ax+ay*ay+az*az);ax/=n;ay/=n;az/=n;
    const double c=std::cos(angle),s=std::sin(angle),t=1-c;
    Mat m=identity();
    m[0]=t*ax*ax+c;m[1]=t*ax*ay-s*az;m[2]=t*ax*az+s*ay;
    m[4]=t*ax*ay+s*az;m[5]=t*ay*ay+c;m[6]=t*ay*az-s*ax;
    m[8]=t*ax*az-s*ay;m[9]=t*ay*az+s*ax;m[10]=t*az*az+c;
    return m;
}
static Mat rigid(Mat r,double x,double y,double z) {r[3]=x;r[7]=y;r[11]=z;return r;}
static Mat inv(const Mat& m) {Mat o{};CHECK(inverse(m,o));return o;}
static std::array<float,15> raw_of(const Mat& m) {
    std::array<float,15> f{};
    for(int r=0;r<3;++r) f[static_cast<std::size_t>(r)]=float(m[static_cast<std::size_t>(r*4+3)]);
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) f[static_cast<std::size_t>(3+c*3+r)]=float(m[static_cast<std::size_t>(r*4+c)]);
    f[12]=f[13]=f[14]=1;
    return f;
}
static std::array<float,16> floats(const Mat& m) {std::array<float,16> f{};for(std::size_t i=0;i<16;++i) f[i]=float(m[i]);return f;}

// The eye camera: world z up, looking along +x, pitched by `pitch` (radians, negative = down); eye 0/1 3.2 cm left/right.
static Mat camera(double pitch,unsigned eye) {
    Mat looking=identity();looking[0]=0;looking[2]=-1;looking[4]=-1;looking[5]=0;looking[6]=0;looking[9]=1;looking[10]=0;   // camera -Z = world +x
    const Mat head=mul(rigid(identity(),0,0,1.6),mul(looking,rot(1,0,0,pitch)));
    return mul(head,rigid(identity(),eye?.032:-.032,0,0));
}
static Mat vp_of(const Mat& pose) {   // invertible, constant clip w=1 (the placement algebra, not a perspective)
    Mat p{};p[0]=1.2;p[5]=1.6;p[10]=1;p[11]=.05;p[15]=1;
    return mul(p,inv(pose));
}
struct FakeView {std::uint32_t frame{};float view[16]{},vp[16]{};};
static FakeView fake[2];
static constexpr std::uintptr_t view_at[2]={0x10000000,0x20000000};
static bool fake_read(std::uintptr_t at,void* out,std::size_t n) {
    for(unsigned e=0;e<2;++e) {
        if(at==view_at[e]+0x77b4 && n==4) {std::memcpy(out,&fake[e].frame,4);return true;}
        if(at==view_at[e]+0x7450 && n==64) {std::memcpy(out,fake[e].view,64);return true;}
        if(at==view_at[e]+0x7510 && n==64) {std::memcpy(out,fake[e].vp,64);return true;}
    }
    return false;
}
static void set_view(unsigned e,std::uint32_t frame,const Mat& vp) {
    fake[e].frame=frame;const auto f=floats(vp);std::memcpy(fake[e].vp,f.data(),64);
    for(int i=0;i<16;++i) fake[e].view[i]=i%5==0?1.f:0.f;
}
static const Views* views_of(std::uint32_t frame,std::uint32_t p0,std::uint32_t p1) {return ObjectMotion::lab_views(frame,p0,p1);}

// One run of 40 renders; returns renders whose four held draws were all placed exactly.
static int run(bool legacy,int& early_level) {
    ObjectMotion::record_at_exit.store(!legacy);ObjectMotion::views_versioned.store(!legacy);
    Place::clear();
    const Mat level[2]={vp_of(camera(0,0)),vp_of(camera(0,1))},eye[2]={vp_of(camera(-35/57.29577951308232,0)),vp_of(camera(-35/57.29577951308232,1))};
    const Mat part=rigid(identity(),.06,0,.04),hold=rigid(rot(1,0,0,.08),.004,-.003,.002);   // B is the drawn hold, a few mm off F
    std::uint32_t frame=legacy?100:300;int placed_renders=0;early_level=0;
    for(int n=0;n<40;++n,++frame) {
        const Mat f=rigid(rot(.2,.3,1,.4+.002*n),.5,-.05,1.25),b=mul(f,hold);
        for(unsigned e=0;e<2;++e) set_view(e,frame,level[e]);                                  // the engine's pre-fill: new stamp, level camera
        for(unsigned e=0;e<2;++e) ObjectMotion::note_eye_view(e,view_at[e]);                   // Setup entry
        if(const Views* early=views_of(frame,0,frame-1)) {                                     // a Views build inside the window
            if(distance(early->vp[0],level[0])<1e-6) ++early_level;}
        for(unsigned e=0;e<2;++e) {set_view(e,frame,eye[e]);ObjectMotion::note_eye_setup_done(view_at[e]);}   // Setup exit: the eye camera
        for(unsigned e=0;e<2;++e) ObjectMotion::lab_snapshot(e);                               // a render thread's classify snapshot, later
        const auto raw=raw_of(f);std::array<const float*,2> rp{nullptr,raw.data()};std::array<const Mat*,2> bp{nullptr,&b};
        Place::stage(1,Place::kind_prop);Place::publish_bound(frame,rp,bp,{0.,.5},{0,0x2d7dc5b2210},n*.011);
        if(n<2) continue;
        bool all=true;
        for(unsigned e=0;e<2;++e) for(unsigned k=0;k<2;++k) {
            const auto c=floats(mul(eye[e],mul(f,part)));std::array<float,16> o{},q{};
            const Result r=Place::place(frame,views_of,c.data(),nullptr,o.data(),q.data(),-1,int(e),nullptr,k?0x1366cull:0x1366dull);
            const auto want=floats(mul(eye[e],mul(b,part)));
            double err=0;for(int i=0;i<16;++i) err=std::max(err,std::abs(double(o[static_cast<std::size_t>(i)])-double(want[static_cast<std::size_t>(i)])));
            if(!(r.side==1 && err<2e-4)) all=false;
        }
        placed_renders+=all;
    }
    return placed_renders;
}

int main() {
    ObjectMotion::lab_attach(fake_read);
    int early_old=0,early_new=0;
    const int old_placed=run(true,early_old);
    const int new_placed=run(false,early_new);
    ObjectMotion::record_at_exit.store(true);ObjectMotion::views_versioned.store(true);
    std::printf("v1366 eye records: renders with every held draw placed at the drawn hold: v1365 path %d of 38 (an early Views build saw the level camera in %d), "
        "v1366 %d of 38 (level camera seen early: %d)\n",old_placed,early_old,new_placed,early_new);
    CHECK(old_placed==0 && early_old>0);   // the v1365 path reproduces the sim: the window's Views keep the level camera for the render
    CHECK(new_placed==38 && early_new==0);
    std::printf(failures?"eye_record_check FAILED\n":"eye_record_check OK\n");
    return failures?1:0;
}
