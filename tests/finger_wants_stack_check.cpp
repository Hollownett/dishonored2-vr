#include "../src/finger_wants_stack.hpp"
#include "../src/finger_ik.hpp"
#include "../src/hand_bind.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
namespace r=d2vr::rig;
using d2vr::Vec3;
namespace {
bool near(float a,float b,float tol=1e-5f) {return std::abs(a-b)<=tol;}
r::WantsInput base(float touch=0) {r::WantsInput in;in.touch.fill(touch);return in;}
// The synthetic hand of tests/finger_ik_check.cpp: wrist at the origin, fingers along +Y, palm normal -mirror*Z.
struct Hand {r::HandGeometry geometry{};d2vr::WorldTrackedPose palm{};};
Hand synthetic_hand(float mirror) {
    r::ArmHierarchy parents{};std::array<r::Transform,69> bind{};r::FingerChains chains{};
    parents.fill(0);parents[0]=-1;
    for(auto& t:bind) t.f[0]=t.f[5]=t.f[10]=1;
    for(unsigned finger=0;finger<5;++finger) for(unsigned segment=0;segment<3;++segment) {
        const auto joint=5+finger*4+segment;chains[finger][segment]=joint;
        parents[joint]=segment?static_cast<std::int16_t>(joint-1):0;
        bind[joint].f[12]=mirror*(.05f-.02f*finger);bind[joint].f[13]=.1f+.025f*segment;
        if(finger && !segment) {parents[joint]=static_cast<std::int16_t>(joint-1);bind[joint-1]=bind[joint];bind[joint-1].f[13]=.075f;}
    }
    for(unsigned finger=0;finger<5;++finger) {
        const auto root=chains[finger][0],helper=30+finger;
        parents[helper]=static_cast<std::int16_t>(root);bind[helper]=bind[root];bind[helper].f[14]=.005f;
    }
    std::array<r::Transform,4> knuckles{};
    for(unsigned i=0;i<4;++i) knuckles[i]=bind[chains[i+1][0]];
    const auto grip=r::hand_bind_grip(bind[0],knuckles);
    Hand h{};h.geometry=r::hand_geometry(bind,parents,chains,{},0,grip);
    h.palm={r::position(grip),{r::column(grip,0),r::column(grip,1),r::column(grip,2)}};
    return h;
}
}
int main() {
    {   // precurl_scales_with_closeness: fingers 1-4 pre-curl to 0.5*squeeze*closeness, closeness 0 at 0.45 m and 1 at contact; the thumb is not pre-curled
        auto in=base();in.squeeze=.8f;in.hover_radius_bounds=.1;
        in.hover_distance=.45;auto o=r::compose_finger_wants(in);for(float c:o) assert(near(c,0));
        in.hover_distance=.6;o=r::compose_finger_wants(in);for(float c:o) assert(near(c,0));
        in.hover_distance=0;o=r::compose_finger_wants(in);assert(near(o[0],0));for(unsigned f=1;f<5;++f) assert(near(o[f],.4f));
        in.hover_distance=.225;o=r::compose_finger_wants(in);for(unsigned f=1;f<5;++f) assert(near(o[f],.2f));
        in.hover_distance.reset();o=r::compose_finger_wants(in);for(float c:o) assert(near(c,0)); // no candidate: no pre-curl
        auto curled=base(.7f);curled.squeeze=.8f;curled.hover_distance=0;curled.hover_radius_bounds=.1;
        o=r::compose_finger_wants(curled);for(float c:o) assert(near(c,.7f)); // a max: never opens what the touch curl closed
    }
    {   // pinch_only_below_3cm: a small item blends toward {0.6,0.55,0.15,0.15,0.15} by 0.3*closeness; 3 cm and up: no pinch
        constexpr std::array<float,5> pinch{.6f,.55f,.15f,.15f,.15f};
        auto in=base();in.hover_distance=0;in.hover_radius_bounds=.01;
        auto o=r::compose_finger_wants(in);
        for(unsigned f=0;f<5;++f) assert(near(o[f],.3f*pinch[f]));
        in.hover_radius_bounds=.03;o=r::compose_finger_wants(in);for(float c:o) assert(near(c,0));
        in.hover_radius_bounds=.0299;in.hover_distance=.225;o=r::compose_finger_wants(in);
        for(unsigned f=0;f<5;++f) assert(near(o[f],.15f*pinch[f]));
        in.hover_distance=.45;o=r::compose_finger_wants(in);for(float c:o) assert(near(c,0));
    }
    {   // take_close_timing: 0 -> 1 over 0.1 s after the press, 1 -> 0 over 0.25 s after the item is gone, monotonic each way
        r::TakeClose t;assert(t.weight(0)==0);
        t.press(10.);assert(near(static_cast<float>(t.weight(10.)),0) && near(static_cast<float>(t.weight(10.05)),.5f) && near(static_cast<float>(t.weight(10.1)),1) && t.weight(11.)==1.);
        double last=0;for(int i=0;i<=20;++i) {const double w=t.weight(10.+i*.01);assert(w>=last-1e-12);last=w;}
        t.gone(11.);assert(t.weight(11.)==1. && near(static_cast<float>(t.weight(11.125)),.5f) && t.weight(11.25)==0. && t.weight(12.)==0.);
        last=1;for(int i=0;i<=30;++i) {const double w=t.weight(11.+i*.01);assert(w<=last+1e-12);last=w;}
        // gone before the close finished: down from where it was, still 0 at 0.25 s after gone
        r::TakeClose u;u.press(0.);u.gone(.05);
        assert(near(static_cast<float>(u.weight(.05)),.5f) && u.weight(.3)==0. && u.weight(.1)<=.5 && u.weight(.1)>0);
        // a new press restarts the close from the weight it has
        u.press(1.);assert(u.weight(1.)==0. && near(static_cast<float>(u.weight(1.1)),1));
        r::TakeClose v;v.gone(0.);assert(v.weight(0.)==0. && v.weight(1.)==0.); // gone with no press: nothing
    }
    {   // the take close: max(., held_floor * take_weight) on every finger
        auto in=base(.1f);in.take_weight=.5;const auto o=r::compose_finger_wants(in);
        for(float c:o) assert(near(c,.3f));
        in.take_weight=1;for(float c:r::compose_finger_wants(in)) assert(near(c,.6f));
    }
    {   // held_overrides_precurl: a hold keeps its held wants (the touch input, as today), whatever the hover says
        auto in=base(.8f);in.held=true;in.squeeze=1;in.hover_distance=0;in.hover_radius_bounds=.01;in.touch[0]=.25f;
        const auto o=r::compose_finger_wants(in);assert(o==in.touch);
    }
    {   // priority_order: the take beats the pre-curl (applied after it), the hold beats the take
        auto in=base();in.squeeze=1;in.hover_distance=0;in.hover_radius_bounds=.1;in.take_weight=1;
        auto o=r::compose_finger_wants(in);for(unsigned f=1;f<5;++f) assert(near(o[f],.6f));
        in.held=true;in.touch.fill(.2f);o=r::compose_finger_wants(in);for(float c:o) assert(near(c,.2f));
    }
    {   // table_caps_precurl: a hand reaching a coin on a table keeps its fingers on the table: the contact (a slab under the fingers) caps the composed wants
        for(float mirror:{-1.f,1.f}) {
            const auto h=synthetic_hand(mirror);const auto& g=h.geometry;
            const Vec3 down=r::palm_normal(h.palm,g);
            const auto table=r::slab_shape(h.palm.position+down*.03,down*-1.);
            auto in=base(.3f);in.squeeze=1;in.hover_distance=.01;in.hover_radius_bounds=.012;in.take_weight=1;
            const auto wants=r::compose_finger_wants(in);
            for(unsigned f=1;f<5;++f) assert(wants[f]>=.49f);
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&table,1,0,st,1.f,r::finger_solve_all,&wants);
            const auto world=[&](Vec3 p) {return h.palm.position+h.palm.basis[0]*p.x+h.palm.basis[1]*p.y+h.palm.basis[2]*p.z;};
            for(unsigned f=1;f<5;++f) {
                assert(c.touching[f]);
                const auto a=r::finger_pose_angles(f,wants[f],c.pose[f]);
                const auto tip=world(r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],a))[3]);
                assert(r::shape_distance(table,tip)-g.thickness[f]>=-r::contact_tolerance-1e-3);
            }
        }
    }
    {   // bad input never throws a NaN into the solver: non-finite or out-of-range values are clamped or ignored
        auto in=base();in.touch[1]=std::nanf("");in.squeeze=2;in.hover_distance=std::nan("");in.take_weight=-3;
        const auto o=r::compose_finger_wants(in);for(float c:o) assert(std::isfinite(c) && c>=0 && c<=1);
    }
    std::cout<<"finger_wants_stack_check passed"<<std::endl;
    return 0;
}
