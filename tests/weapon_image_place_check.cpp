// v864: WeaponPlace moves a tick-placed weapon frame by the change from the hand the tick read to the hand the render draws, keeps the
// weapon's offset in the hand exactly, never compounds while no tick places it again, and leaves a jump alone.
#include "../src/weapon_image_place.hpp"
#include <cstdio>
using namespace d2vr;using namespace d2vr::fullbody;
static int bad=0;
static void check(bool ok,const char* what) {std::printf("%s %s\n",ok?"ok  ":"FAIL",what);if(!ok) ++bad;}
static std::array<float,12> frame_of_pose(const Xf& x) {
    std::array<float,12> f{};f[0]=float(x.p.x);f[1]=float(x.p.y);f[2]=float(x.p.z);
    const Vec3 axes[3]={rotated(x.q,{1,0,0}),rotated(x.q,{0,1,0}),rotated(x.q,{0,0,1})};
    for(unsigned r=0;r<3;++r) {f[3+r*3]=float(axes[r].x);f[4+r*3]=float(axes[r].y);f[5+r*3]=float(axes[r].z);}
    return f;
}
static double gap(const std::array<float,12>& a,const std::array<float,12>& b) {double m=0;for(unsigned i=0;i<12;++i) m=std::max(m,double(std::abs(a[i]-b[i])));return m;}
int main() {
    const Xf grip{body::rot_z(.3),{.12,-.03,.05}};                    // the weapon in the hand
    const Xf read{body::rot_z(1.1),{3,4,1.2}};                         // the hand the tick placed it from
    const Xf image{body::mul(body::rot_z(.2),body::rot_z(1.1)),{3.08,4.03,1.21}};   // the hand the render draws (a fast turn's frame)
    WeaponPlace w;
    const auto tick_frame=frame_of_pose(body::mul(read,grip));
    const auto out=w.place(tick_frame,read,image);
    check(out && gap(*out,frame_of_pose(body::mul(image,grip)))<1e-5,"the weapon sits in the image's hand with its own grip");
    const Xf image2{body::mul(body::rot_z(.05),image.q),{3.1,4.05,1.21}};
    const auto again=w.place(*out,read,image2);                        // no tick since: the frame is our own write
    check(again && gap(*again,frame_of_pose(body::mul(image2,grip)))<1e-5,"a render without a tick moves it from the tick's base, not twice");
    const Xf read3{body::rot_z(1.4),{3.2,4.1,1.2}};
    const auto retick=w.place(frame_of_pose(body::mul(read3,grip)),read3,read3);
    check(retick && gap(*retick,frame_of_pose(body::mul(read3,grip)))<1e-5,"a new tick placement is the new base");
    check(!w.place(tick_frame,read,Xf{read.q,{4,4,1.2}}),"a jump of a metre is left as the tick placed it");
    if(bad) {std::printf("FAIL: %d\n",bad);return 1;}
    std::puts("PASS: a held weapon follows the hand the render draws");
    return 0;
}
