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
    // The grip ball (user, headset 2026-11-05: a small sphere at the grip place, native grip): the thing's THIN side, not the ball round its whole box.
    assert(near(r::sphere_grip_ball_radius({.09,.015,.004}),.015));                 // spoon: its 1.5 cm floor (a fist still closes on it)
    assert(near(r::sphere_grip_ball_radius({.15,.022,.022}),.022));                 // candlestick: its 2.2 cm shaft
    assert(near(r::sphere_grip_ball_radius({.11,.08,.02}),.02));                    // book: its 4 cm thickness
    assert(near(r::sphere_grip_ball_radius({.09,.07,.10}),r::sphere_grip_ball_max)); // a head: its 7 cm side capped at 4 cm
    assert(near(r::sphere_grip_ball_radius({.6,.1,.1}),r::sphere_grip_ball_max));    // a plank: the 4 cm cap, not v642's 50 cm
    assert(near(r::sphere_grip_ball_radius({std::nan(""),.1,.1}),r::sphere_grip_unknown));
    for(const Vec3 h:{Vec3{.09,.015,.004},Vec3{.11,.08,.02},Vec3{.15,.022,.022},Vec3{.3,.3,.3}})
        assert(r::sphere_grip_ball_radius(h)<=r::sphere_grip_ball_max && r::sphere_grip_ball_radius(h)>=r::sphere_grip_min);
    {   // a prop's grip ball from its world box: the candlestick turned 30 degrees still gives 2.2 cm (its own thin side), v642's rule 15.3 cm
        const double c=std::cos(pi/6),s=std::sin(pi/6);d2vr::WorldTrackedPose pose{{0,0,0},{{{c,s,0},{-s,c,0},{0,0,1}}}};
        const Vec3 half{.15,.022,.022};const Vec3 e{half.x*c+half.y*s,half.x*s+half.y*c,half.z};
        assert(std::abs(r::sphere_grip_ball_radius(pose,d2vr::NativeGrabBounds{e*-1.,e})-.022)<1e-6);
        assert(std::abs(r::sphere_grip_radius(pose,d2vr::NativeGrabBounds{e*-1.,e})-.153)<1e-3);}
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
    // The grip the user gets, same hand and solver: how far the four long fingers close on v642's ball and on the grip ball (mean flexion of their 12 joints).
    {const std::array<std::pair<const char*,Vec3>,6> things{{{"spoon",{.09,.015,.004}},{"candlestick",{.15,.022,.022}},{"book",{.11,.08,.02}},
            {"folder",{.16,.11,.01}},{"skull",{.09,.07,.10}},{"plank",{.6,.1,.1}}}};
     for(const auto& thing:things) {
         const auto report=[&](const char* rule,double rad) {
             const auto shape=r::sphere_grip_shape(hand.palm,hand.geometry,rad);r::FingerIkState st{};auto wants=r::finger_wants(true,1.f,0.f);
             const auto c=r::finger_contact(hand.geometry,hand.palm,&shape,1,0,st,r::held_curl,r::finger_solve_all,&wants);
             unsigned touching=0;double flexion=0;
             for(unsigned f=1;f<5;++f) {touching+=c.touching[f]?1u:0u;for(unsigned j=0;j<3;++j) flexion+=c.pose[f][j];}
             std::cout<<thing.first<<" ("<<rule<<" "<<rad*100<<" cm): "<<touching<<"/4 long fingers on the ball, mean flexion "
                 <<(flexion/12)*180/pi<<" deg\n";
         };
         report("whole box",r::sphere_grip_radius(thing.second));report("grip ball",r::sphere_grip_ball_radius(thing.second));
     }}
    // A wide, thin prop is not closed on (user, headset 2026-11-05: "grip is unnatural on books"): the fingers lie on its face. Wide and thin means at most
    // 3 cm on the thinnest side, at least 4 cm on the middle one, and the thin side at most half the middle one.
    assert(r::grip_is_flat(Vec3{.11,.08,.02}));     // book 22 x 16 x 4
    assert(r::grip_is_flat(Vec3{.16,.11,.01}));     // folder 32 x 22 x 2
    assert(r::grip_is_flat(Vec3{.6,.1,.01}));       // a thin plank
    assert(r::grip_is_flat(Vec3{.3,.3,.03}));       // a wide sheet
    assert(!r::grip_is_flat(Vec3{.15,.022,.022}));  // candlestick: a shaft to close on
    assert(!r::grip_is_flat(Vec3{.035,.035,.15}));  // bottle: a shaft
    assert(!r::grip_is_flat(Vec3{.09,.015,.004}));  // spoon: a 1.5 cm handle, not a face
    assert(!r::grip_is_flat(Vec3{.09,.07,.10}));    // head: closed on
    assert(!r::grip_is_flat(Vec3{.3,.3,.3}));       // crate
    assert(!r::grip_is_flat(Vec3{.6,.1,.1}));       // a thick plank
    assert(!r::grip_is_flat(Vec3{std::nan(""),.1,.1}));
    // A flat prop's grip is NO contact shape at all plus the relaxed reach (v1028): the hand lies on the book's face instead of closing through its cover.
    // Nothing is re-read per frame, so there is nothing to twitch and one shape fewer to solve than the ball.
    {const r::HeldShape none{};r::FingerIkState st{};auto wants=r::finger_wants(true,1.f,0.f);
     const auto c=r::finger_contact(hand.geometry,hand.palm,&none,0,0,st,r::grip_flat_reach,r::finger_solve_all,&wants);
     double flexion=0;for(unsigned f=1;f<5;++f) for(unsigned j=0;j<3;++j) flexion+=c.pose[f][j];
     std::cout<<"flat grip (relaxed reach "<<r::grip_flat_reach<<"): mean flexion "<<(flexion/12)*180/pi<<" deg\n";
     assert((flexion/12)*180/pi<40 && (flexion/12)*180/pi>5);   // a relaxed hand on a book, not a fist and not a flat board
     assert(r::grip_flat_reach>=.25f && r::grip_flat_reach<=.5f);
    }
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
    // The thumb and the grip ball: a REAL grip ball (>= 3.5 cm: a bottle's body) is wrapped - its IP joint and tip rest on it (v642). A small one (1.5-3 cm: a
    // candlestick, a spoon) is not (v1035, user: "thumb bone placed to the fingers, should be placed out of the fingers ... bent into an unrealistic position"):
    // the thumb stays out of the finger bodies, which is what thumb_clearance measures.
    for(const double radius:{.015,.02,.025,.03,.035,.04}) {
        const auto thumb_ball=r::sphere_grip_shape(hand.palm,hand.geometry,radius);r::FingerIkState st{};auto wants=r::finger_wants(true,1.f,0.f);
        const auto c=r::finger_contact(hand.geometry,hand.palm,&thumb_ball,1,0,st,r::held_curl,r::finger_solve_all,&wants);
        const auto t=r::finger_turns(hand.geometry,c,wants);const auto p=r::finger_points(hand.geometry.finger[0],{t[0][0],t[0][1],t[0][2]});
        const auto d0=thumb_ball.frame.position-hand.palm.position;const Vec3 centre{dot(d0,hand.palm.basis[0]),dot(d0,hand.palm.basis[1]),dot(d0,hand.palm.basis[2])};
        const double t0=hand.geometry.thickness[0];
        r::FingerAngles fa{};for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) fa[f][s]=t[f][s];
        const double clearance=r::thumb_clearance(hand.geometry,fa,{t[0][0],t[0][1],t[0][2]});
        std::cout<<"thumb on a "<<radius*100<<" cm ball: tip "<<(length(p[3]-centre)-radius)*1000<<" mm off the ball, IP/tip "
                 <<(dot(p[2],hand.geometry.palm_in)-r::held_palm_depth)*1000<<"/"<<(dot(p[3],hand.geometry.palm_in)-r::held_palm_depth)
                 <<" mm over the skin, clearance to the fingers "<<clearance*1000<<" mm"<<std::endl;
        assert(dot(p[3],hand.geometry.palm_in)-r::held_palm_depth>=t0-.002);   // the tip never sinks into the palm
        assert(clearance>=-.002);   // v1037: the thumb stays out of the fingers at every grip-ball size (no ball is wrapped now: the largest is the 4 cm cap)
        (void)radius;
    }
    // v642's ball round a book's whole box is knob-only now (build/legacy-sphere-grip-whole-object); printed, not asserted. (The old loop asserted .14, a ball
    // larger than that book's own 13.75 cm, and missed v646's thumb rule there; at the radius the rule really gives it still passes.)
    {const double rad=r::sphere_grip_radius({.11,.08,.02});
     const auto b=r::sphere_grip_shape(hand.palm,hand.geometry,rad);r::FingerIkState st{};auto wants=r::finger_wants(true,1.f,0.f);
     const auto c=r::finger_contact(hand.geometry,hand.palm,&b,1,0,st,r::held_curl,r::finger_solve_all,&wants);
     const auto t=r::finger_turns(hand.geometry,c,wants);const auto p=r::finger_points(hand.geometry.finger[0],{t[0][0],t[0][1],t[0][2]});
     const auto d0=b.frame.position-hand.palm.position;const Vec3 centre{dot(d0,hand.palm.basis[0]),dot(d0,hand.palm.basis[1]),dot(d0,hand.palm.basis[2])};
     std::cout<<"legacy whole-box ball "<<rad*100<<" cm: thumb tip "<<(length(p[3]-centre)-rad)*1000<<" mm off the ball (v646 asks "
         <<(hand.geometry.thickness[0]-.002)*1000<<" mm)\n";}
    {   // surface grip shape: a book is an oriented box, a bottle a capsule, a crate a box
        const d2vr::WorldTrackedPose id{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const auto of=[&](d2vr::Vec3 h) {return r::surface_grip_shape(id,d2vr::NativeGrabBounds{h*-1.,h},-1,id);};
        const auto book=of({.11,.02,.08}),bottle=of({.035,.125,.035}),crate=of({.2,.2,.2});
        assert(book.radius==0 && book.half.x==.11 && book.half.y==.02 && book.half.z==.08);
        assert(bottle.radius>.034 && bottle.radius<.036 && bottle.half.x>.08);
        assert(crate.radius==0 && crate.half.x==.2);
    }
    {   // v1063 natural seat: palm normal -z (the thing below the palm), fingers +x
        using d2vr::WorldTrackedPose;using d2vr::NativeGrabBounds;namespace rr=d2vr::rig;
        const Vec3 skin{0,0,0},fw{1,0,0},n{0,0,-1};
        const auto box_of=[](const WorldTrackedPose& p,Vec3 half) {Vec3 e{};const double hh[3]{half.x,half.y,half.z};
            for(unsigned i=0;i<3;++i) {e.x+=hh[i]*std::abs(p.basis[i].x);e.y+=hh[i]*std::abs(p.basis[i].y);e.z+=hh[i]*std::abs(p.basis[i].z);}
            return NativeGrabBounds{p.position-e,p.position+e};};
        const auto turned=[](double a,Vec3 k,Vec3 v) {return v*std::cos(a)+d2vr::cross(k,v)*std::sin(a)+k*(d2vr::dot(k,v)*(1-std::cos(a)));};
        // a book (22 x 16 x 4) under the palm, tilted 20 deg about the fingers and 15 deg off square: its cover comes flat on the palm, the skin on it
        const Vec3 half{.11,.08,.02};WorldTrackedPose book{{.01,.02,-.05},{{{1,0,0},{0,1,0},{0,0,1}}}};
        for(auto& r:book.basis) r=turned(.35,{1,0,0},turned(.26,{0,0,1},r));
        const auto seat=rr::natural_seat(book,box_of(book,half),skin,fw,n);assert(seat);
        assert(std::abs(std::abs(seat->basis[2].z)-1)<1e-6);   // cover flat on the palm (thin axis along the normal)
        assert(std::abs(seat->position.z-(-.02))<1e-6);         // the face on the skin
        // a plank 100 x 10 x 3 lying 35 deg off across the palm: turned across, then its wide face flat on the palm
        const Vec3 ph{.5,.05,.015};WorldTrackedPose plank{{0,.1,-.03},{{{0,1,0},{-1,0,0},{0,0,1}}}};
        for(auto& r:plank.basis) r=turned(.61,{0,0,1},r);
        const auto ps=rr::natural_seat(plank,box_of(plank,ph),skin,fw,n);assert(ps);
        const Vec3 across=d2vr::cross(n,fw);
        assert(std::abs(std::abs(d2vr::dot(ps->basis[0],across))-1)<1e-6 && std::abs(std::abs(ps->basis[2].z)-1)<1e-6);
        assert(std::abs(d2vr::dot(ps->position-skin,ps->basis[0]))>.05);   // held near its end: it does not slide to its middle
        // a plank along the fingers (80 deg off across) is not swung round
        WorldTrackedPose along{{.2,0,-.03},{{{1,0,0},{0,1,0},{0,0,1}}}};for(auto& r:along.basis) r=turned(.17,{0,0,1},r);
        const auto as=rr::natural_seat(along,box_of(along,ph),skin,fw,n);assert(as && std::abs(d2vr::dot(as->basis[0],fw))>.98);
    }
    std::cout<<"finger_sphere_check passed\n";
}
