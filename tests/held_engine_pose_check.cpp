// v1254 lab: the engine render frame of a held prop (held_engine_pose.hpp). The publisher packs the pose its draws are placed at (B, a column-vector
// 4x4) into the engine's 15-float model frame (origin 3, three axis ROWS, scales 3) and writes it to model+0x1860 once per render. The check pins the
// two properties the write relies on: (1) a frame read by weapon_draw::model_of packs back into itself (axes and scales kept, so a model that natively
// carries a scale does not grow or shrink), and (2) a frame that is not a rigid finite pose is refused instead of written, and an unchanged frame is a
// no-op. Nothing here touches the game.
#define NOMINMAX
#include "../src/held_engine_pose.hpp"
#include "../src/weapon_draw_place.hpp"
#include <array>
#include <cmath>
#include <cstdio>
using namespace d2vr;
using d2vr::weapon_draw::Mat;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)

// An engine model frame: origin, three unit axis ROWS (the columns of a yaw * pitch rotation), three scales.
static std::array<float,15> frame(double px,double py,double pz,double yaw,double pitch,double sx=1,double sy=1,double sz=1) {
    const double cy=std::cos(yaw),sy_=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const double ax[3][3]={{cy,sy_*cp,-sy_*sp},{-sy_,cy*cp,-cy*sp},{0,sp,cp}};   // ax[c] = column c
    std::array<float,15> f{};
    f[0]=float(px);f[1]=float(py);f[2]=float(pz);
    for(unsigned c=0;c<3;++c) {f[3+c*3]=float(ax[c][0]);f[3+c*3+1]=float(ax[c][1]);f[3+c*3+2]=float(ax[c][2]);f[12+c]=float(c==0?sx:c==1?sy:sz);}
    return f;
}
int main() {
    {   // (1) a native frame packs back into itself: origin, axes and scale 1
        const auto f=frame(1.25,-.5,.9,.7,-.2);
        const Mat m=d2vr::weapon_draw::model_of(f.data());
        std::array<float,15> out{};
        CHECK(HeldEnginePose::frame_from_pose(m.data(),out.data()));
        CHECK(HeldEnginePose::frame_gap(f.data(),out.data())<1e-5);
        CHECK(std::abs(out[12]-1.f)<1e-6 && std::abs(out[13]-1.f)<1e-6 && std::abs(out[14]-1.f)<1e-6);
    }
    {   // a model that natively carries a scale keeps it (a shrunken mesh must not be written back at 1.0)
        const auto f=frame(0,0,0,.3,.4,.25,2.,1.);
        const Mat m=d2vr::weapon_draw::model_of(f.data());
        std::array<float,15> out{};
        CHECK(HeldEnginePose::frame_from_pose(m.data(),out.data()));
        CHECK(HeldEnginePose::frame_gap(f.data(),out.data())<1e-5);
    }
    {   // a pose B (the drawn hand): unit axes, exact origin
        Mat b=d2vr::weapon_draw::identity();b[3]=-2.;b[7]=.75;b[11]=1.5;
        std::array<float,15> out{};
        CHECK(HeldEnginePose::frame_from_pose(b.data(),out.data()));
        CHECK(std::abs(out[0]+2.f)<1e-6 && std::abs(out[1]-.75f)<1e-6 && std::abs(out[2]-1.5f)<1e-6);
        CHECK(std::abs(out[12]-1.f)<1e-6 && std::abs(out[13]-1.f)<1e-6 && std::abs(out[14]-1.f)<1e-6);
    }
    {   // a non-rigid / broken pose is refused, never written: empty column, NaN, absurd scale
        std::array<float,15> out{};
        Mat z=d2vr::weapon_draw::identity();z[0]=z[1]=z[2]=0;
        CHECK(!HeldEnginePose::frame_from_pose(z.data(),out.data()));
        Mat n=d2vr::weapon_draw::identity();n[3]=std::nan("");
        CHECK(!HeldEnginePose::frame_from_pose(n.data(),out.data()));
        Mat big=d2vr::weapon_draw::identity();big[0]=big[5]=big[10]=1000.;
        CHECK(!HeldEnginePose::frame_from_pose(big.data(),out.data()));
        CHECK(!HeldEnginePose::frame_from_pose(nullptr,out.data()));
    }
    {   // the write guards: unchanged = no-op, a 1 cm move = write, another object = refused by distance
        const auto a=frame(1,2,3,.1,.1),same=a;
        CHECK(HeldEnginePose::frame_gap(a.data(),same.data())<HeldEnginePose::write_eps);
        auto moved=a;moved[1]+=.01f;
        CHECK(HeldEnginePose::frame_gap(a.data(),moved.data())>HeldEnginePose::write_eps);
        CHECK(HeldEnginePose::origin_gap(a.data(),moved.data())<HeldEnginePose::max_move_m);
        auto other=a;other[0]+=3.f;
        CHECK(HeldEnginePose::origin_gap(a.data(),other.data())>HeldEnginePose::max_move_m);
        auto micro=a;micro[0]+=.00005f;   // 0.05 mm: below the write epsilon, not worth a write
        CHECK(HeldEnginePose::frame_gap(a.data(),micro.data())<HeldEnginePose::write_eps);
    }
    if(failures) {std::printf("held_engine_pose_check FAILED (%d)\n",failures);return 1;}
    std::printf("held_engine_pose_check ok\n");
    return 0;
}
