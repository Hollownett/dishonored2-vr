#include "../src/finger_snapshot.hpp"
#include "../src/full_body_driver.hpp"
#include "../src/hand_bind.hpp"
#include <cassert>
#include <iostream>
namespace r=d2vr::rig;
using d2vr::Vec3;
namespace {
struct Hand {r::ArmHierarchy parents{};std::array<r::Transform,69> bind{};r::FingerChains chains{};r::Transform grip{};r::HandGeometry geometry{};d2vr::WorldTrackedPose palm{};};
// The player's real rig, as tests/finger_ik_check.cpp builds it.
Hand real_hand(bool right) {
    namespace fb=d2vr::fullbody;
    const auto sk=fb::skeleton(right?fb::data::kRightArm:fb::data::kLeftArm);
    const std::string side=right?"right_":"left_";
    Hand h{};h.parents.fill(0);
    assert(sk.local.size()==69);
    for(unsigned j=0;j<69;++j) {h.bind[j]=fb::to_transform(sk.bind[j]);h.parents[j]=static_cast<std::int16_t>(sk.parent[j]);}
    const char* digits[5]={"thumb","index","medium","ring","pinky"};
    for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) h.chains[f][s]=static_cast<unsigned>(sk.find(side+digits[f]+"_0"+std::to_string(s+(f?2:1))+"_jnt"));
    const auto wrist=static_cast<unsigned>(sk.find(side+"wrist_jnt"));
    std::array<r::Transform,4> knuckles{};
    for(unsigned i=0;i<4;++i) knuckles[i]=h.bind[h.chains[i+1][0]];
    h.grip=r::hand_bind_grip(h.bind[wrist],knuckles);
    const r::HandJoints helpers{static_cast<unsigned>(sk.find(side+"helper_underwrist_jnt")),static_cast<unsigned>(sk.find(side+"helper_upperwrist_jnt"))};
    h.geometry=r::hand_geometry(h.bind,h.parents,h.chains,helpers,wrist,h.grip);
    h.palm={r::position(h.grip),{r::column(h.grip,0),r::column(h.grip,1),r::column(h.grip,2)}};
    return h;
}
bool throws(const std::string& text) {try {r::parse_snapshot(text);} catch(const std::invalid_argument&) {return true;} return false;}
bool near(double a,double b,double tolerance=1e-6) {return std::abs(a-b)<=tolerance;}
bool near(Vec3 a,Vec3 b,double tolerance=1e-6) {return near(a.x,b.x,tolerance) && near(a.y,b.y,tolerance) && near(a.z,b.z,tolerance);}
}
int main() {
    // ---- a record survives the text: every field the lab needs, quotes and control characters in the strings
    {
        r::FingerSnapshot s{};
        s.tick=123456789012ull;s.side=1;s.reason="start \"now\"";s.source="native\\hold";
        s.palm={{1.5,-2.25,3.125},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}};s.grip={{4,5,6},{Vec3{0,1,0},Vec3{0,0,1},Vec3{1,0,0}}};
        s.dt=.0111;s.squeeze=.75f;s.index=.25f;s.reach=.8f;s.holding=true;
        s.shapes.push_back({{.1,.2,.3},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}},{.12,0,0},.035,"held"});
        s.shapes.push_back({{-1,-2,-3},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}},{.1,.2,.3},0,"slab"});
        for(unsigned f=0;f<5;++f) {for(unsigned j=0;j<3;++j) {s.contact.pose[f][j]=.1f*(f+1)+.01f*j;s.turns[f][j]=-.2f*(f+1)+.03f*j;} s.contact.finger[f]=.1f*f;s.contact.touching[f]=f%2==0;}
        s.contact.resting=.3f;s.cost_us=12.5;
        s.held.present=true;s.held.entity=0x1234abcd5678ull;
        s.held.have_first=true;s.held.first_box={1,2,3,0,4,5,6,0};s.held.first_origin={7,8,9};s.held.first_axes={1,0,0,0,1,0,0,0,1};
        s.held.have_now=true;s.held.box={-1,-2,-3,0,-4,-5,-6,0};s.held.origin={.5,.25,.125};s.held.axes={0,1,0,0,0,1,1,0,0};
        s.held.gate_distance=.0345;s.held.gate_ok=false;s.held.cylinder_set_frames=7;s.held.gate_fail_frames=99;
        s.held.cylinder_ok=true;s.held.cylinder_axis=2;s.held.cylinder_half=.11;s.held.cylinder_radius=.03;
        s.held.touched=true;s.held.touch_along=.4;s.held.touch_roll=-1.25;s.held.touch_gap=.05;s.held.touch_error="Invalid held touch";s.held.touch_source="probe";s.held.have_extents=true;s.held.extents={.0693,.0693,.2065};
        s.held.have_pre=true;s.held.pre_box={1,2,3,0,4,5,6,0};s.held.pre_origin={7,8,9};s.held.pre_axes={1,0,0,0,1,0,0,0,1};s.held.pre_age=120;
        s.held.have_press=true;s.held.press_position={4,5,6};s.held.press_basis={Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}};s.held.press_age=35;s.held.press_squeeze=.1;
        s.held.seen_entity={0xabc123456789ull,0};s.held.seen_age={120,-1};
        s.held.held_class=".?AVarkMovable@@";s.held.match="same";
        s.held.have_fit=true;s.held.fit_shift=.04;s.held.fit_slide=-.016;s.held.fit_plain=.0418;s.held.fit_fitted=.0227;s.held.fit_tried=8;s.held.seen_class={std::string(".?AVarkMovable@@"),std::string()};
        s.held.seen_centre={Vec3{1,2,3},Vec3{}};s.held.seen_half={Vec3{.05,.06,.06},Vec3{}};
        s.held.profile.half=.2065;s.held.profile.axis=2;for(unsigned i=0;i<r::profile_stations;++i) {s.held.profile.plus[i]=i<5?.0693f:.025f;s.held.profile.minus[i]=i==3?0.f:(i<5?.0693f:.025f);}
        s.shapes[0].radius_end=.02;
        s.shapes[1].rim=.008;s.held.touch_mode=3;s.held.touch_rotation=.35;
        s.held.seated=true;s.held.seat_origin={1,2,3};s.held.seat_axes={Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}};s.held.published={3,2,1};s.held.centre={.01,.02,.03};
        const auto line=r::snapshot_json(s);
        assert(line.find('\n')==std::string::npos);
        const auto t=r::parse_snapshot(line);
        assert(t.tick==s.tick && t.side==1 && t.reason==s.reason && t.source==s.source && t.holding);
        assert(near(t.palm.position,s.palm.position) && near(t.palm.basis[1],s.palm.basis[1]) && near(t.grip.position,s.grip.position) && near(t.grip.basis[2],s.grip.basis[2]));
        assert(near(t.dt,s.dt) && near(t.squeeze,s.squeeze) && near(t.index,s.index) && near(t.reach,s.reach));
        assert(t.shapes.size()==2 && t.shapes[0].kind=="held" && t.shapes[1].kind=="slab");
        assert(near(t.shapes[0].position,s.shapes[0].position) && near(t.shapes[0].radius,.035) && near(t.shapes[0].half,s.shapes[0].half));
        assert(near(t.shapes[1].basis[0],Vec3{0,0,1}) && near(t.shapes[1].half,Vec3{.1,.2,.3}));
        for(unsigned f=0;f<5;++f) {
            for(unsigned j=0;j<3;++j) {assert(near(t.contact.pose[f][j],s.contact.pose[f][j]) && near(t.turns[f][j],s.turns[f][j]));}
            assert(near(t.contact.finger[f],s.contact.finger[f]) && t.contact.touching[f]==s.contact.touching[f]);
        }
        assert(near(t.contact.resting,.3) && near(t.cost_us,12.5));
        const auto& h=t.held;
        assert(h.present && h.entity==s.held.entity && h.have_first && h.have_now);
        assert(h.first_box==s.held.first_box && h.first_origin==s.held.first_origin && h.first_axes==s.held.first_axes);
        assert(h.box==s.held.box && h.origin==s.held.origin && h.axes==s.held.axes);
        assert(near(h.gate_distance,.0345) && !h.gate_ok && h.cylinder_set_frames==7 && h.gate_fail_frames==99);
        assert(h.cylinder_ok && h.cylinder_axis==2 && near(h.cylinder_half,.11) && near(h.cylinder_radius,.03));
        assert(h.touched && near(h.touch_along,.4) && near(h.touch_roll,-1.25) && near(h.touch_gap,.05) && h.touch_error==s.held.touch_error && h.touch_source=="probe");
        assert(h.have_extents && near(h.extents,Vec3{.0693,.0693,.2065}));
        assert(h.have_pre && h.pre_box==s.held.pre_box && h.pre_origin==s.held.pre_origin && h.pre_axes==s.held.pre_axes && near(h.pre_age,120));
        assert(h.have_press && near(h.press_position,Vec3{4,5,6}) && near(h.press_basis[0],Vec3{0,0,1}) && near(h.press_basis[2],Vec3{0,1,0}) && near(h.press_age,35) && near(h.press_squeeze,.1));
        assert(h.seen_entity[0]==0xabc123456789ull && h.seen_entity[1]==0 && near(h.seen_age[0],120) && near(h.seen_age[1],-1));
        assert(h.held_class==".?AVarkMovable@@" && h.match=="same" && h.seen_class[0]==".?AVarkMovable@@" && h.seen_class[1].empty());
        assert(h.have_fit && near(h.fit_shift,.04) && near(h.fit_slide,-.016) && near(h.fit_plain,.0418) && near(h.fit_fitted,.0227) && h.fit_tried==8);
        assert(near(h.seen_centre[0],Vec3{1,2,3}) && near(h.seen_half[0],Vec3{.05,.06,.06}) && near(h.seen_half[1],Vec3{}));
        assert(near(h.profile.half,.2065) && h.profile.axis==2 && h.profile.measured()==r::profile_stations && h.profile.valid() && near(h.profile.plus[6],.025,1e-6) && h.profile.minus[3]==0.f);
        assert(near(t.shapes[0].radius_end,.02) && t.shapes[1].radius_end<0);
        assert(near(t.shapes[1].rim,.008) && t.shapes[0].rim<0 && h.touch_mode==3 && near(h.touch_rotation,.35));
        assert(h.seated && near(h.seat_origin,s.held.seat_origin) && near(h.seat_axes[0],Vec3{0,0,1}) && near(h.published,s.held.published) && near(h.centre,s.held.centre));
        // a record with no hold, and a non-finite number written as 0
        r::FingerSnapshot e{};e.palm=s.palm;e.grip=s.grip;e.cost_us=std::numeric_limits<double>::infinity();
        const auto u=r::parse_snapshot(r::snapshot_json(e));
        assert(!u.held.present && u.shapes.empty() && u.cost_us==0);
    }
    // ---- JSONL: the last line by default, an index from either end, blank lines skipped
    {
        const std::string text="{\"a\":1}\r\n\n{\"a\":2}\n{\"a\":3}\n\n";
        assert(r::snapshot_line(text)=="{\"a\":3}" && r::snapshot_line(text,0)=="{\"a\":1}" && r::snapshot_line(text,1)=="{\"a\":2}" && r::snapshot_line(text,-2)=="{\"a\":2}");
        bool bad=false;try {r::snapshot_line(text,3);} catch(const std::invalid_argument&) {bad=true;} assert(bad);
        bad=false;try {r::snapshot_line(text,-4);} catch(const std::invalid_argument&) {bad=true;} assert(bad);
        bad=false;try {r::snapshot_line("\n\n");} catch(const std::invalid_argument&) {bad=true;} assert(bad);
    }
    // ---- bad input is refused, not guessed
    // ---- the finger geometry the solver was given, as the game and the lab write it for diffing: five fingers, the hinges, the thumb's bind
    {
        const auto h=real_hand(true);
        const auto axes=r::hinge_axes(h.geometry,h.grip);
        const auto wrist=static_cast<unsigned>(h.parents[h.chains[0][0]]);
        const auto line=r::geometry_json(1,h.geometry,axes,h.grip,{h.bind[wrist],h.bind[h.chains[0][0]],h.bind[h.chains[0][1]],h.bind[h.chains[0][2]]});
        assert(line.find('\n')==std::string::npos);
        const auto g=r::parse_json(line);
        assert(g.get("side") && g.get("side")->n==1 && g.get("fingers") && g.get("fingers")->a.size()==5 && g.get("bind") && g.get("bind")->a.size()==4 && g.get("grip")->a.size()==16);
        const auto& thumb=g.get("fingers")->a[0];
        for(unsigned s=0;s<3;++s) {
            const auto hinge=thumb.get("hinge")->a[s],axis=thumb.get("axes")->a[s];
            assert(near(Vec3{hinge.a[0].n,hinge.a[1].n,hinge.a[2].n},h.geometry.finger[0].hinge[s],1e-6) && near(Vec3{axis.a[0].n,axis.a[1].n,axis.a[2].n},axes[0][s],1e-6));
        }
        assert(near(thumb.get("existing")->a[1].n,h.geometry.finger[0].existing[1],1e-6) && near(thumb.get("lowest")->a[0].n,h.geometry.finger[0].lowest[0],1e-6));
        for(unsigned i=0;i<16;++i) assert(near(g.get("bind")->a[1].a[i].n,h.bind[h.chains[0][0]].f[i],1e-6));
    }
    // ---- bad input is refused, not guessed
    assert(throws("") && throws("{") && throws("[1,2") && throws("{\"palm\":{}}") && throws("{\"palm\":{\"p\":[1,2],\"basis\":[]}}") && throws("not json") && throws("[1]"));
    // ---- the lab's promise: the answer is a function of the record alone. A bottle seated in a real hand, solved, written, read back, solved again
    for(const bool right:{true,false}) {
        const auto h=real_hand(right);
        const d2vr::NativeGrabBounds box{{-.035,-.035,0},{.035,.035,.25}};
        const auto cylinder=r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},box);
        const auto seat=r::seat_held(cylinder,h.palm,r::palm_normal(h.palm,h.geometry),{0,0,.125});
        std::array<r::HeldShape,2> shapes{seat.shape,r::slab_shape(h.palm.position+h.palm.basis[1]*-.08,h.palm.basis[1]*.97+h.palm.basis[2]*.24)};
        const auto n=static_cast<unsigned>(shapes.size());
        r::FingerIkState state{};
        const auto contact=r::finger_contact(h.geometry,h.palm,shapes.data(),n,0,state,r::held_curl);
        const auto turns=r::finger_turns(h.geometry,contact,1.f,0.f,true);
        r::FingerSnapshot s{};
        s.side=right?1:0;s.palm=h.palm;s.grip=h.palm;s.squeeze=1;s.index=0;s.reach=r::held_curl;s.holding=true;s.contact=contact;s.turns=turns;
        for(const auto& x:shapes) s.shapes.push_back({x.frame.position,x.frame.basis,x.half,x.radius,"shape"});
        const auto t=r::parse_snapshot(r::snapshot_json(s));
        std::vector<r::HeldShape> again;
        for(const auto& x:t.shapes) again.push_back({{x.position,x.basis},x.half,x.radius});
        r::FingerIkState fresh{};
        const auto replay=r::finger_contact(h.geometry,t.palm,again.data(),static_cast<unsigned>(again.size()),0,fresh,t.reach);
        const auto replay_turns=r::finger_turns(h.geometry,replay,t.squeeze,t.index,t.holding);
        double worst=0;
        for(unsigned f=0;f<5;++f) for(unsigned j=0;j<3;++j) {
            worst=std::max(worst,static_cast<double>(std::abs(replay.pose[f][j]-contact.pose[f][j])));
            worst=std::max(worst,static_cast<double>(std::abs(replay_turns[f][j]-turns[f][j])));
        }
        std::cout<<(right?"right":"left")<<" hand replay from text: largest joint difference "<<worst*57.29578<<" deg\n";
        assert(worst<1e-4);
        for(unsigned f=0;f<5;++f) assert(replay.touching[f]==contact.touching[f]);
        // and it did touch: this is the held bottle, not an empty hand
        unsigned touching=0;for(unsigned f=0;f<5;++f) touching+=contact.touching[f]?1u:0u;
        assert(touching>=3);
    }
    std::cout<<"finger snapshot check passed\n";
    return 0;
}
