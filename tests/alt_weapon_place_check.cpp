// v1191 pure check of the one-view weapon placement (src/weapon_draw_place.hpp Place::place_one_view): a held part drawn by the engine at
// its tick frame F is placed at the drawn hand's binding B of this render (current rows = VP * B * K), and its previous rows are this eye's
// previous render (two publishes back): prev_VP * B_own_prev * K. A gap in the publishes falls back to prev_VP * B * K (camera motion only).
#include <cmath>
#include <cstdio>
#include "../src/weapon_draw_place.hpp"
using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static Mat rigid(double yaw,double x,double y,double z) {Mat m=identity();const double c=std::cos(yaw),s=std::sin(yaw);m[0]=c;m[1]=-s;m[4]=s;m[5]=c;m[3]=x;m[7]=y;m[11]=z;return m;}
static std::array<float,15> raw_of(const Mat& m) { // model_of's layout: origin, three axis columns, three scales
    std::array<float,15> r{};for(int i=0;i<3;++i) r[static_cast<std::size_t>(i)]=static_cast<float>(m[static_cast<std::size_t>(i*4+3)]);
    for(int c=0;c<3;++c) for(int k=0;k<3;++k) r[static_cast<std::size_t>(3+c*3+k)]=static_cast<float>(m[static_cast<std::size_t>(k*4+c)]);
    r[12]=r[13]=r[14]=1;return r;
}
static double worst(const float* a,const Mat& b) {double w=0;for(int i=0;i<16;++i) w=std::max(w,std::abs(a[i]-b[static_cast<std::size_t>(i)]));return w;}
int main() {
    // a camera 0.4 m behind the hand looking along +y (rows of a projection * view, column vectors)
    Mat vp=identity();vp[0]=1.2;vp[5]=0;vp[6]=1.2;vp[9]=0;vp[10]=0;vp[11]=0.01;vp[12]=0;vp[13]=1;vp[14]=0;vp[15]=0.4;
    Mat prev_vp=vp;prev_vp[3]=0.02;                                      // this eye's previous render: the camera 1.7 cm aside
    const Mat k=rigid(0,0.05,0.02,-0.01);                                // the part's offset from the weapon frame (parts share its orientation, as place() requires)
    Mat b[3],f[3];
    for(int i=0;i<3;++i) {b[i]=rigid(0.1*i,0.01*i,0.2+0.03*i,0.05);f[i]=rigid(0.1*i-0.02,0.01*i-0.03,0.2+0.03*i,0.05);} // F lags B by ~3.6 cm
    for(int i=0;i<3;++i) {
        const auto raw=raw_of(f[i]);const std::array<const float*,2> raws{raw.data(),nullptr};const std::array<const Mat*,2> bounds{&b[i],nullptr};
        Place::publish_bound(100u+static_cast<std::uint32_t>(i),raws,bounds,{0.5,0},{0x1234,0});
    }
    // the engine drew the part at F of frame 102 (this render)
    const Mat drawn=mul(mul(vp,f[2]),k);float cur[16];for(int i=0;i<16;++i) cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);
    float now[16],before[16];bool own=false;
    const auto r=Place::place_one_view(vp,prev_vp,cur,now,before,own);
    CHECK(r.side==0 && own);
    const double e_now=worst(now,mul(mul(vp,b[2]),k)),e_prev=worst(before,mul(mul(prev_vp,b[0]),k));
    std::printf("placed: |now - VP*B*K| %.2g, |prev - prevVP*B_own_prev*K| %.2g\n",e_now,e_prev);
    CHECK(e_now<1e-5 && e_prev<1e-5);
    // a gap (frame 103 missing): the publish two frames back is not this eye's previous render -> camera motion only
    {const auto raw=raw_of(f[2]);const std::array<const float*,2> raws{raw.data(),nullptr};const std::array<const Mat*,2> bounds{&b[2],nullptr};
     Place::publish_bound(104u,raws,bounds,{0.5,0},{0x1234,0});}
    const auto r2=Place::place_one_view(vp,prev_vp,cur,now,before,own);
    CHECK(r2.side==0 && !own && worst(before,mul(mul(prev_vp,b[2]),k))<1e-5);
    // a far object with the same orientation is not the weapon (reach)
    const Mat far=mul(mul(vp,rigid(0.2-0.02,2,2,2)),k);float cur_far[16];for(int i=0;i<16;++i) cur_far[i]=static_cast<float>(far[static_cast<std::size_t>(i)]);
    CHECK(Place::place_one_view(vp,prev_vp,cur_far,now,before,own).side<0);
    if(failures) {std::printf("alt_weapon_place_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: alt_weapon_place_check (current at this render's binding, previous at this eye's previous binding, gap -> camera only, reach)\n");
    return 0;
}
