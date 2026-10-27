// The sphere grip (src/finger_sphere.hpp): the radius rule's values, the ball fixed in the palm's frame, and the fingers' curls that stay put while the held thing jitters.
#include "../src/finger_sphere.hpp"
#include "../src/full_body_driver.hpp"
#include "../src/hand_bind.hpp"
#include <cassert>
#include <iostream>
namespace r=d2vr::rig;
using d2vr::Vec3;
namespace {
constexpr double pi=3.14159265358979323846;
// The player's real right hand (the rig the full-body driver bends), as tests/finger_ik_check.cpp builds it.
struct Hand {r::HandGeometry geometry{};d2vr::WorldTrackedPose palm{};};
Hand real_hand() {
    namespace fb=d2vr::fullbody;
    const auto sk=fb::skeleton(fb::data::kRightArm);
    std::array<r::Transform,69> bind{};r::ArmHierarchy parents{};r::FingerChains chains{};
    for(unsigned j=0;j<69;++j) {bind[j]=fb::to_transform(sk.bind[j]);parents[j]=static_cast<std::int16_t>(sk.parent[j]);}
    const char* digits[5]={"thumb","index","medium","ring","pinky"};
    for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) chains[f][s]=static_cast<unsigned>(sk.find(std::string("right_")+digits[f]+"_0"+std::to_string(s+(f?2:1))+"_jnt"));
    const auto wrist=static_cast<unsigned>(sk.find("right_wrist_jnt"));
    std::array<r::Transform,4> knuckles{};for(unsigned i=0;i<4;++i) knuckles[i]=bind[chains[i+1][0]];
    const auto grip=r::hand_bind_grip(bind[wrist],knuckles);
    const r::HandJoints helpers{static_cast<unsigned>(sk.find("right_helper_underwrist_jnt")),static_cast<unsigned>(sk.find("right_helper_upperwrist_jnt"))};
    Hand h{};h.geometry=r::hand_geometry(bind,parents,chains,helpers,wrist,grip);
    h.palm={r::position(grip),{r::column(grip,0),r::column(grip,1),r::column(grip,2)}};
    return h;
}
bool near(double a,double b,double tol=1e-9) {return std::abs(a-b)<=tol;}
}
int main() {
    // The rule (user, after v641): the sphere round the whole box, every corner inside (no finger ever crosses the object), 1.5 cm .. 0.5 m.
    const auto hyp=[](double a,double b,double c){return std::sqrt(a*a+b*b+c*c);};
    assert(near(r::sphere_grip_radius({.09,.015,.004}),hyp(.09,.015,.004)));   // spoon: 9.1 cm
    assert(near(r::sphere_grip_radius({.15,.022,.022}),hyp(.15,.022,.022)));   // candlestick: 15.3 cm
    assert(near(r::sphere_grip_radius({.022,.15,.022}),hyp(.15,.022,.022)));   // (the order of the axes does not matter)
    assert(near(r::sphere_grip_radius({.11,.08,.02}),hyp(.11,.08,.02)));       // book 22 x 16 x 4: 13.7 cm
    assert(near(r::sphere_grip_radius({.09,.07,.10}),hyp(.09,.07,.10)));       // a head: 15.2 cm
    assert(near(r::sphere_grip_radius({.6,.1,.1}),r::sphere_grip_max));         // a plank: the 0.5 m cap
    assert(near(r::sphere_grip_radius({std::nan(""),.1,.1}),r::sphere_grip_unknown)); // unreadable: a middling ball, never NaN
    for(const Vec3 h:{Vec3{.09,.015,.004},Vec3{.11,.08,.02},Vec3{.15,.022,.022}}) {   // every corner of the box is inside the ball
        const double rad=r::sphere_grip_radius(h);
        for(int sx=-1;sx<=1;sx+=2) for(int sy=-1;sy<=1;sy+=2) for(int sz=-1;sz<=1;sz+=2) assert(hyp(sx*h.x,sy*h.y,sz*h.z)<=rad+1e-12);}
    assert(r::sphere_grip_radius({.001,.001,.001})>=r::sphere_grip_min);
    // A prop from its world box: the local half sides come back through the tilt (a candlestick turned 30 degrees about z still gives the ball round its own box, 15.3 cm, not the looser one round the tilted world box).
    {const double c=std::cos(pi/6),s=std::sin(pi/6);d2vr::WorldTrackedPose pose{{0,0,0},{{{c,s,0},{-s,c,0},{0,0,1}}}};
     const Vec3 half{.15,.022,.022};const Vec3 e{half.x*c+half.y*s,half.x*s+half.y*c,half.z};
     assert(std::abs(r::sphere_grip_radius(pose,d2vr::NativeGrabBounds{e*-1.,e})-std::sqrt(.15*.15+2*.022*.022))<1e-6);}
    // A bone: its radius clamped to what a hand wraps; no bone, a middling ball.
    assert(near(r::sphere_grip_radius_of_bone(-1,{}),r::sphere_grip_unknown));
    for(int bone=0;bone<17;++bone) {const double v=r::sphere_grip_radius_of_bone(bone,{});assert(v>=r::sphere_grip_min && v<=r::sphere_grip_max);}
    // sim v640: a dragged wrist (capsule 5 cm, box 30 x 4 x 4 cm: its x half is the forearm's length) wraps a 3 cm wrist, not the 6 cm cap
    assert(near(r::sphere_grip_radius_of_bone(6,d2vr::NativeGrabBounds{{-.15,-.04,-.04},{.15,.04,.04}}),.03));
    assert(near(r::sphere_grip_radius_of_bone(6,d2vr::NativeGrabBounds{{-.15,-.02,-.02},{.15,.02,.02}}),.02));   // a thinner box wins
    // The latch: the first reading of a hold stays, a new entity decides again.
    {r::SphereGripLatch latch;int calls=0;
     assert(near(latch.radius_for(7,[&]{++calls;return .03;}),.03));assert(near(latch.radius_for(7,[&]{++calls;return .05;}),.03) && calls==1);
     assert(near(latch.radius_for(8,[&]{++calls;return .05;}),.05) && calls==2);}
    // The ball lies on the palm skin in the palm's own frame: the same shape in that frame wherever the palm is.
    const auto hand=real_hand();
    const auto ball=r::sphere_grip_shape(hand.palm,hand.geometry,.03);
    assert(ball.half.x==0 && ball.half.y==0 && ball.half.z==0 && near(ball.radius,.03));
    assert(near(r::shape_distance(ball,hand.palm.position+r::palm_normal(hand.palm,hand.geometry)*r::held_palm_depth),0,1e-6)); // the skin point touches it
    // The fingers' contact on the ball does not move while the held thing jitters (the solver never sees the thing): 60 frames, one finger a frame as live, wants of a hold.
    {r::FingerIkState state{};auto wants=r::finger_wants(true,1.f,0.f);std::array<std::array<float,3>,5> last{};double worst=0;
     for(unsigned k=0;k<60;++k) {
         // the held thing's pose jitters by up to 12 mm / 8 degrees a frame; the ball is built from the palm alone
         const auto shape=r::sphere_grip_shape(hand.palm,hand.geometry,r::sphere_grip_radius({.11,.08,.02}));
         const auto c=r::finger_contact(hand.geometry,hand.palm,&shape,1,1/60.,state,r::held_curl,1,&wants);
         if(k>=30) for(unsigned f=0;f<5;++f) for(unsigned j=0;j<3;++j) worst=std::max(worst,static_cast<double>(std::abs(c.pose[f][j]-last[f][j])));
         last=c.pose;
     }
     std::cout<<"sphere grip: largest per-frame joint change after settling "<<worst*180/pi<<" deg\n";
     assert(worst<1e-6);
     // ...and it closes: every long finger touches the ball (a book's 4 cm), none hyperextended
     const auto shape=r::sphere_grip_shape(hand.palm,hand.geometry,.04);r::FingerIkState once{};
     const auto c=r::finger_contact(hand.geometry,hand.palm,&shape,1,0,once,r::held_curl,r::finger_solve_all,&wants);
     for(unsigned f=1;f<5;++f) assert(c.touching[f] && !c.hyperextended[f]);}
    std::cout<<"finger_sphere_check passed\n";
}
