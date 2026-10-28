#include "../src/note_tablet.hpp"
#include "../src/full_body_driver.hpp"
#include "../src/hand_bind.hpp"
#include <cassert>
#include <iostream>
#include <limits>
namespace r=d2vr::rig;
using d2vr::Vec3;
struct Hand {r::HandGeometry geometry{};d2vr::WorldTrackedPose palm{};};
Hand real_left_hand() {
    namespace fb=d2vr::fullbody;
    const auto sk=fb::skeleton(fb::data::kLeftArm);
    std::array<r::Transform,69> bind{};r::ArmHierarchy parents{};r::FingerChains chains{};
    for(unsigned j=0;j<69;++j) {bind[j]=fb::to_transform(sk.bind[j]);parents[j]=static_cast<std::int16_t>(sk.parent[j]);}
    const char* digits[5]={"thumb","index","medium","ring","pinky"};
    for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) chains[f][s]=static_cast<unsigned>(sk.find(std::string("left_")+digits[f]+"_0"+std::to_string(s+(f?2:1))+"_jnt"));
    const auto wrist=static_cast<unsigned>(sk.find("left_wrist_jnt"));
    std::array<r::Transform,4> knuckles{};for(unsigned i=0;i<4;++i) knuckles[i]=bind[chains[i+1][0]];
    const auto grip=r::hand_bind_grip(bind[wrist],knuckles);
    const r::HandJoints helpers{static_cast<unsigned>(sk.find("left_helper_underwrist_jnt")),static_cast<unsigned>(sk.find("left_helper_upperwrist_jnt"))};
    Hand h{};h.geometry=r::hand_geometry(bind,parents,chains,helpers,wrist,grip);
    h.palm={r::position(grip),{r::column(grip,0),r::column(grip,1),r::column(grip,2)}};
    return h;
}
template<class F> bool rejects(F f) {try {f();}catch(const std::invalid_argument&) {return true;}return false;}
int main() {
    const auto h=real_left_hand();const auto tablet=r::note_tablet(h.geometry,h.palm);r::FingerIkState state{};
    const auto grip=r::note_tablet_hand(h.geometry);
    const auto turns=r::note_tablet_fingers(h.geometry,h.palm,tablet,state,0);
    const auto world=[&](Vec3 p) {return h.palm.position+h.palm.basis[0]*p.x+h.palm.basis[1]*p.y+h.palm.basis[2]*p.z;};
    assert(length(cross(tablet.right,tablet.up)-tablet.front)<1e-6);
    assert(dot(world(h.geometry.finger[1].pivot)-world(h.geometry.finger[4].pivot),tablet.up)>.05); // thumb/index side is the page's top
    assert(std::abs(r::shape_distance(tablet.shape,tablet.centre))<1e-7);
    for(int x:{-1,1}) for(int y:{-1,1}) {
        const auto corner=tablet.centre+tablet.right*(x*r::note_tablet_width*.5)+tablet.up*(y*r::note_tablet_height*.5);
        assert(std::abs(r::shape_distance(tablet.shape,corner))<1e-7); // displayed face and physical slab have the same boundary
    }
    double least=1e9;double lo=1e9,hi=-1e9;
    for(unsigned f=0;f<5;++f) {
        const std::array<double,3> t{turns[f][0],turns[f][1],turns[f][2]};const auto p=r::finger_points(grip.finger[f],t);
        const double clearance=r::finger_clearance(grip.finger[f],grip.thickness[f],t,world,[&](Vec3 q){return r::shape_distance(tablet.shape,q);},f?r::arc_all:0u,12);
        assert(clearance>=-r::contact_tolerance);assert(clearance<.003);least=std::min(least,clearance);
        const auto tip=world(p[3])-tablet.centre;const double front=dot(tip,tablet.front),inside=dot(tip,tablet.right)+r::note_tablet_width*.5;
        assert(inside>0 && inside<r::note_tablet_width && std::abs(dot(tip,tablet.up))<r::note_tablet_height*.5);
        if(f==0) {assert(front>=grip.thickness[0]-r::contact_tolerance && front<grip.thickness[0]+.003);assert(inside<.02);}
        else {assert(front<=-r::note_tablet_thickness+1e-6);const auto y=dot(tip,tablet.up);lo=std::min(lo,y);hi=std::max(hi,y);} // thumb opposed on the front, all four fingertips behind
        for(unsigned j=0;j<3;++j) assert(std::isfinite(turns[f][j]));
    }
    const auto bind_span=std::abs(dot(world(r::finger_points(h.geometry.finger[1],{0,0,0})[3])-world(r::finger_points(h.geometry.finger[4],{0,0,0})[3]),tablet.up));
    assert(hi-lo<.8*bind_span); // close the native bind fan instead of leaving a spread resting hand
    // Arbitrary rotation/translation carries the entire panel, its contact shape and the same finger pose, including wrist roll.
    const auto q=d2vr::normalized(d2vr::Quaternion{.3,-.5,.2,.7});const Vec3 shift{2,-1,.4};
    const auto rotate=[&](Vec3 p) {return d2vr::rotated(q,p);};auto moved=h.palm;
    moved.position=shift+rotate(h.palm.position);for(unsigned a=0;a<3;++a)moved.basis[a]=rotate(h.palm.basis[a]);
    const auto carried=r::note_tablet(h.geometry,moved);const auto carried_turns=r::note_tablet_fingers(h.geometry,moved,carried,state,1/72.);
    assert(length(carried.centre-shift-rotate(tablet.centre))<1e-6);
    assert(length(carried.right-rotate(tablet.right))<1e-6 && length(carried.up-rotate(tablet.up))<1e-6 && length(carried.front-rotate(tablet.front))<1e-6);
    assert(state.solved==0); // rigid carry reuses the exact contact memo
    for(unsigned f=0;f<5;++f)for(unsigned j=0;j<3;++j)assert(std::abs(carried_turns[f][j]-turns[f][j])<1e-6);
    // Text scale changes expand from the held edge, so resizing does not detach the hand.
    for(double scale:{50./65,1.,100./65}) {
        const auto sized=r::note_tablet(h.geometry,h.palm,r::note_tablet_width*scale,r::note_tablet_height*scale);
        assert(length((sized.centre-sized.right*(r::note_tablet_width*scale*.5))-(tablet.centre-tablet.right*(r::note_tablet_width*.5)))<1e-6);
        r::FingerIkState fresh{};const auto t=r::note_tablet_fingers(h.geometry,h.palm,sized,fresh,0);
        for(unsigned f=0;f<5;++f) {
            const std::array<double,3> a{t[f][0],t[f][1],t[f][2]};
            const double clearance=r::finger_clearance(grip.finger[f],grip.thickness[f],a,world,[&](Vec3 p){return r::shape_distance(sized.shape,p);},f?r::arc_all:0u,12);
            assert(clearance>=-r::contact_tolerance);
            const auto tip=world(r::finger_points(grip.finger[f],a)[3])-sized.centre;
            assert(std::abs(dot(tip,sized.right))<sized.shape.half.x && std::abs(dot(tip,sized.up))<sized.shape.half.y);
            if(!f) assert(dot(tip,sized.front)>0);else assert(dot(tip,sized.front)<-r::note_tablet_thickness);
        }
    }
    assert(rejects([&]{r::note_tablet(h.geometry,h.palm,0,.25);}));
    assert(rejects([&]{r::note_tablet(h.geometry,h.palm,.45,std::numeric_limits<double>::quiet_NaN());}));
    auto invalid=h.palm;invalid.basis[0]=invalid.basis[1];assert(rejects([&]{r::note_tablet(h.geometry,invalid);}));
    assert(rejects([&]{r::note_tablet_fingers(h.geometry,h.palm,tablet,state,-1);}));
    std::cout<<"native fingertip span "<<(hi-lo)*1000<<" mm vs bind "<<bind_span*1000<<" mm; ";
    std::cout<<"note_tablet_check passed; bone-arc worst clearance "<<least*1000<<" mm (solver tolerance "<<r::contact_tolerance*1000<<" mm)\n";
}
