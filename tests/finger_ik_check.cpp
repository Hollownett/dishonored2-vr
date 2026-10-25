#include "../src/finger_ik.hpp"
#include "../src/full_body_driver.hpp"
#include "../src/hand_bind.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
namespace r=d2vr::rig;
using d2vr::Vec3;
namespace {
constexpr double deg=3.14159265358979323846/180;
// Test fixtures, not engine data: a sword-hilt-sized handle and the capsule a fist closes on, one radius out of the palm plane.
constexpr double sword_handle_radius=.017;
r::HeldShape handle_shape(const d2vr::WorldTrackedPose& palm,const r::HandGeometry& hand,double radius) {
    return {{palm.position+r::palm_normal(palm,hand)*(radius+r::held_palm_depth),palm.basis},{.1,0,0},radius};
}
struct Hand {r::ArmHierarchy parents{};std::array<r::Transform,69> bind{};r::FingerChains chains{};r::Transform grip{};r::HandGeometry geometry{};d2vr::WorldTrackedPose palm{};};
// The synthetic hand of tests/finger_curl_check.cpp: wrist at the origin, fingers along +Y, palm normal -mirror*Z.
Hand synthetic_hand(float mirror) {
    Hand h{};h.parents.fill(0);h.parents[0]=-1;
    for(auto& t:h.bind) t.f[0]=t.f[5]=t.f[10]=1;
    for(unsigned finger=0;finger<5;++finger) for(unsigned segment=0;segment<3;++segment) {
        const auto joint=5+finger*4+segment;h.chains[finger][segment]=joint;
        h.parents[joint]=segment?static_cast<std::int16_t>(joint-1):0;
        h.bind[joint].f[12]=mirror*(.05f-.02f*finger);h.bind[joint].f[13]=.1f+.025f*segment;
        if(finger && !segment) {h.parents[joint]=static_cast<std::int16_t>(joint-1);h.bind[joint-1]=h.bind[joint];h.bind[joint-1].f[13]=.075f;}
    }
    for(unsigned finger=0;finger<5;++finger) {
        const auto root=h.chains[finger][0],helper=30+finger;
        h.parents[helper]=static_cast<std::int16_t>(root);h.bind[helper]=h.bind[root];h.bind[helper].f[14]=.005f;
    }
    std::array<r::Transform,4> knuckles{};
    for(unsigned i=0;i<4;++i) knuckles[i]=h.bind[h.chains[i+1][0]];
    h.grip=r::hand_bind_grip(h.bind[0],knuckles);
    h.geometry=r::hand_geometry(h.bind,h.parents,h.chains,{},0,h.grip);
    h.palm={r::position(h.grip),{r::column(h.grip,0),r::column(h.grip,1),r::column(h.grip,2)}};
    return h;
}
// The player's real rig (the same arm data the full-body driver bends): bind palette, the live chains (thumb 01-03, others 02-04), the
// underwrist/upperwrist palm helpers, as dxgi_probe's refresh_finger_geometry builds the geometry.
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
Vec3 world_of(const Hand& h,Vec3 p) {return h.palm.position+h.palm.basis[0]*p.x+h.palm.basis[1]*p.y+h.palm.basis[2]*p.z;}
// Capsule across the palm (grip forward), under the knuckles, `gap` out of the palm plane.
r::HeldShape across_palm(const Hand& h,float mirror,double radius,double gap,double y=.08) {
    return {{{0,y,-mirror*(radius+gap)},h.palm.basis},{.1,0,0},radius};
}
// The finger's turns at a curl.
std::array<double,3> turns_of(const Hand& h,unsigned f,double curl) {return r::finger_turn_at(h.geometry.finger[f],f,curl);}
// Least skin clearance (the three arcs, 12 samples a bone: denser than the solver's 3, so a miss between its samples shows) of the
// fingers posed as finger_turns poses them when they want to close fully (the contact pose).
double posed_clearance(const Hand& h,const r::FingerCurls& curls,const r::HeldShape& shape,int only=-1,unsigned arcs=r::arc_all) {
    double least=1e9;
    for(unsigned f=0;f<5;++f) {
        if(only>=0 && f!=static_cast<unsigned>(only)) continue;
        const auto a=r::finger_pose_angles(f,1.,curls.pose[f]);
        least=std::min(least,r::finger_clearance(h.geometry.finger[f],h.geometry.thickness[f],r::finger_turns_of(h.geometry.finger[f],a),
            [&](Vec3 p) {return world_of(h,p);},[&](Vec3 w) {return r::shape_distance(shape,w);},arcs,12));
    }
    return least;
}
// The thumb as a capsule (every sample's thickness sphere, 12 a bone: it closes out of its hinge plane, so no arcs) once it is posed as
// finger_turns poses it wanting to close fully.
double thumb_clearance(const Hand& h,const r::FingerCurls& curls,const r::HeldShape& shape) {
    const auto a=r::finger_pose_angles(0,1.,curls.pose[0]);
    return r::finger_clearance(h.geometry.finger[0],h.geometry.thickness[0],r::finger_turns_of(h.geometry.finger[0],a),
        [&](Vec3 p) {return world_of(h,p);},[&](Vec3 w) {return r::shape_distance(shape,w);},0u,12);
}
// The four long fingers only (the thumb starts inside many a seat and may take its alternate curve: it has its own checks).
double long_clearance(const Hand& h,const r::FingerCurls& curls,const r::HeldShape& shape) {
    double least=1e9;for(int f=1;f<5;++f) least=std::min(least,posed_clearance(h,curls,shape,f));return least;
}
// The angles (rad, from straight) a finger_turns row bends its joints to: the turn plus the bend the rest pose has.
std::array<double,3> final_angles(const Hand& h,unsigned f,const std::array<float,3>& turn) {
    return {turn[0]+h.geometry.finger[f].existing[0],turn[1]+h.geometry.finger[f].existing[1],turn[2]+h.geometry.finger[f].existing[2]};
}
// Total flexion of a long finger as a share of its limits (MCP+PIP+DIP over 280 degrees).
double flexion_share(const std::array<double,3>& angles) {
    return (angles[0]+angles[1]+angles[2])/((90+110+80)*deg);
}
template<class F> bool rejects(F&& f) {try {f();} catch(const std::invalid_argument&) {return true;} return false;}
} // namespace
int main() {
    // Joint limits and coupling (pure functions, no hand): the angles a curl parameter asks of a human finger.
    {
        const auto fist=r::finger_angles(2,1.);
        assert(std::abs(fist[0]-90*deg)<1e-9 && std::abs(fist[1]-110*deg)<1e-9 && std::abs(fist[2]-80*deg)<1e-9 && std::abs(r::dip_per_pip-80./110)<1e-12);
        for(unsigned f=0;f<5;++f) {
            std::array<double,3> last{};
            for(unsigned i=0;i<=200;++i) {
                const double c=i/200.;
                const auto a=r::finger_angles(f,c);
                for(unsigned s=0;s<3;++s) assert(a[s]>=-1e-12 && a[s]<=r::finger_limit_degrees[f][s]*deg+1e-9); // never past a human joint
                for(unsigned s=0;s<3;++s) assert(a[s]>=last[s]-1e-12); // a larger curl never opens a joint
                last=a;
                if(f) {
                    // The distal joint follows the middle one at about 2/3 (80 where the middle ends at 110); the proximal joint leads; the
                    // middle joint never trails the distal one.
                    assert(std::abs(a[2]-r::dip_per_pip*a[1])<1e-9 && r::dip_per_pip>.65 && r::dip_per_pip<.75);
                    assert(a[0]/(90*deg)>=a[1]/(110*deg)-1e-12 && a[1]>=a[2]-1e-12);
                }
            }
        }
        // Half closed: the knuckle is further along than the middle joint, nothing is straight, nothing is at its limit.
        const auto half=r::finger_angles(1,.5);
        assert(half[0]/(90*deg)>half[1]/(110*deg)+.1 && half[0]>20*deg && half[2]>20*deg && half[0]<80*deg);
        // The thumb: CMC (the swing across the palm), MCP, IP, closing together.
        const auto t_half=r::finger_angles(0,.5),t_full=r::finger_angles(0,1.);
        assert(std::abs(t_full[0]-70*deg)<1e-9 && std::abs(t_full[1]-50*deg)<1e-9 && std::abs(t_full[2]-70*deg)<1e-9);
        assert(std::abs(t_half[0]-35*deg)<1e-9);
        assert(rejects([&] {(void)r::finger_angles(5,.5);}) && rejects([&] {(void)r::finger_angles(1,std::nan(""));}));
        // The curl a joint's angle belongs to inverts the path.
        for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) for(double c:{0.,.2,.5,.9,1.}) assert(std::abs(r::finger_curl_at(f,s,r::finger_angles(f,c)[s])-c)<1e-9);
        assert(r::finger_angles(1,2.)==r::finger_angles(1,1.) && r::finger_angles(1,-1.)==r::finger_angles(1,0.));
    }
    std::array<r::FingerCurls,2> by_mirror{};
    for(float mirror:{-1.f,1.f}) {
        const auto h=synthetic_hand(mirror);
        // Geometry: palm normal recovered in grip space, straight rest chains, each joint hinges perpendicular to its own bone and closes
        // toward the palm; the thumb swings toward the fingertips lifted over the palm, not under the fingers.
        const auto& g=h.geometry;
        const auto palm_in_world=h.palm.basis[0]*g.palm_in.x+h.palm.basis[1]*g.palm_in.y+h.palm.basis[2]*g.palm_in.z;
        assert(d2vr::length(palm_in_world-Vec3{0,0,-mirror})<1e-5);
        for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {
            assert(std::abs(g.finger[f].existing[s])<1e-6);
            assert(std::abs(d2vr::length(g.finger[f].bone[s])-(s==2?.02:.025))<1e-6); // no tip joint: distal = 80% of the middle
            assert(std::abs(d2vr::dot(g.finger[f].hinge[s],g.finger[f].bone[s]))<1e-6);
        }
        for(unsigned f=1;f<5;++f) for(unsigned s=0;s<3;++s) {
            const auto turned=d2vr::rotated(r::axis_angle(g.finger[f].hinge[s],1.),g.finger[f].bone[s]);
            assert(d2vr::dot(turned-g.finger[f].bone[s],g.palm_in)>0); // a positive turn closes toward the palm
        }
        {
            const auto tip=[&](double curl) {return r::finger_points(g.finger[0],turns_of(h,0,curl))[3];};
            assert(d2vr::length(tip(1.)-tip(0.))>.01 && d2vr::length(tip(.5)-tip(0.))>.005); // the thumb moves as it closes (its design is judged on the real rig below)
        }
        // The hinge axes BodySolver bends about, in the palette's space: the grip's columns turn the palm frame back.
        {
            const auto axes=r::hinge_axes(g,h.grip);
            for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {
                const auto a=g.finger[f].hinge[s];
                const Vec3 expect=d2vr::unit(r::column(h.grip,0)*a.x+r::column(h.grip,1)*a.y+r::column(h.grip,2)*a.z);
                assert(d2vr::length(axes[f][s]-expect)<1e-9 && std::abs(d2vr::length(axes[f][s])-1)<1e-9);
            }
        }
        // No shape: unconstrained, nothing touched.
        r::FingerIkState state{};
        const auto open=r::finger_contact(g,h.palm,nullptr,0,state);
        for(unsigned f=0;f<5;++f) assert(open.finger[f]==1 && !open.touching[f]);
        assert(open.resting==0.f);
        // Sword handle under the knuckles: every finger wraps it, never through it.
        const auto handle=across_palm(h,mirror,sword_handle_radius,.01);
        r::FingerIkState fresh{};
        const auto on_handle=r::finger_contact(g,h.palm,&handle,0,fresh);
        for(unsigned f=1;f<5;++f) {assert(on_handle.touching[f]);assert(on_handle.finger[f]>.05f && on_handle.finger[f]<1.f);}
        assert(long_clearance(h,on_handle,handle)>-5e-3);
        // A ball under the proximal phalanges: the fingers wrap it. Its top also intrudes into the resting middle and ring bones (all
        // digits of this rig lie in one plane): a finger is never driven deeper than its rest pose already is.
        const r::HeldShape ball{{{0,.13,-mirror*.04},h.palm.basis},{},.035};
        r::FingerIkState sb{};const auto on_ball=r::finger_contact(g,h.palm,&ball,0,sb);
        const r::FingerCurls rest{};
        for(int f=1;f<5;++f) {
            if(f==2 || f==3) assert(on_ball.touching[f] && on_ball.finger[f]<1.f); // (the index and little finger pass beside a ball this small)
            assert(posed_clearance(h,on_ball,ball,f)>std::min(0.,posed_clearance(h,rest,ball,f))-5e-3);
        }
        assert(posed_clearance(h,rest,ball,2)<0 && posed_clearance(h,rest,ball,1)>=0);
        // The contact caps the finger's curl where its first phalanx touches; the joints beyond that phalanx keep closing on, so the finger
        // wraps with every joint flexed (no claw with the tip bent and the knuckle straight, no hook by the tip alone), the distal joint
        // never behind its coupled share, every angle inside the human limits.
        {
            const std::array<r::HeldShape,3> shapes{handle,across_palm(h,mirror,.035,.01),ball};
            for(const auto& sh:shapes) {
                r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&sh,0,st);
                const auto turn=r::finger_turns(g,c,1.f,1.f,true);
                for(unsigned f=1;f<5;++f) {
                    const auto a=final_angles(h,f,turn[f]);
                    assert(a[2]>=r::dip_per_pip*a[1]-1e-6);
                    if(c.finger[f]>.1f) assert(a[0]>0 && a[1]>0 && a[2]>0);
                    for(unsigned s=0;s<3;++s) assert(a[s]<=r::finger_limit_degrees[f][s]*deg+1e-6);
                }
            }
        }
        // Fatter objects open the hand further; a crate face on the palm leaves it nearly flat; far away frees it.
        const auto bottle=across_palm(h,mirror,.035,.01);
        r::FingerIkState s2{};const auto on_bottle=r::finger_contact(g,h.palm,&bottle,0,s2);
        for(unsigned f=1;f<5;++f) assert(on_bottle.finger[f]<on_handle.finger[f]);
        assert(long_clearance(h,on_bottle,bottle)>-5e-3);
        const r::HeldShape crate{{{0,.1,-mirror*.215},{{{1,0,0},{0,1,0},{0,0,1}}}},{.2,.2,.2},0};
        r::FingerIkState s3{};const auto on_crate=r::finger_contact(g,h.palm,&crate,0,s3);
        for(unsigned f=1;f<5;++f) {assert(on_crate.touching[f]);assert(on_crate.finger[f]<.2f);}
        assert(long_clearance(h,on_crate,crate)>-5e-3);
        const r::HeldShape far{{{2,0,0},h.palm.basis},{.1,0,0},.02};
        r::FingerIkState s4{};const auto nothing=r::finger_contact(g,h.palm,&far,0,s4);
        for(unsigned f=0;f<5;++f) assert(nothing.finger[f]==1 && !nothing.touching[f]);
        // Smoothing: a shape arriving clamps at once; its leaving reopens over ~3 tau.
        r::FingerIkState s5{};(void)r::finger_contact(g,h.palm,nullptr,0,s5);
        const auto arrived=r::finger_contact(g,h.palm,&handle,.011,s5);
        for(unsigned f=0;f<5;++f) assert(arrived.finger[f]==on_handle.finger[f]);
        const auto leaving=r::finger_contact(g,h.palm,nullptr,.01,s5);
        for(unsigned f=1;f<5;++f) {assert(leaving.finger[f]>on_handle.finger[f]);assert(leaving.finger[f]<1.f);}
        for(unsigned i=0;i<60;++i) (void)r::finger_contact(g,h.palm,nullptr,.011,s5);
        const auto gone=r::finger_contact(g,h.palm,nullptr,.011,s5);
        for(unsigned f=0;f<5;++f) assert(gone.finger[f]>.999f);
        // Deterministic: a fresh state reproduces the first solve bit for bit.
        r::FingerIkState s6{};const auto again=r::finger_contact(g,h.palm,&handle,0,s6);
        assert(again.finger==on_handle.finger);
        by_mirror[mirror<0?0:1]=on_handle;
        // Weapon handle helper sits one radius plus palm depth out of the palm, along the controller.
        const auto hilt=handle_shape(h.palm,g,sword_handle_radius);
        assert(std::abs(d2vr::dot(hilt.frame.position-h.palm.position,palm_in_world)-(sword_handle_radius+.01))<1e-6);
        assert(hilt.half.x>0 && hilt.radius==sword_handle_radius);
        // A held bottle seated from its own shape: the cylinder's axis along the hole axis, the palm skin on its surface, held low on the body.
        {
            const d2vr::NativeGrabBounds box{{-.035,-.035,0},{.035,.035,.24}};
            const std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
            const auto cyl=r::held_cylinder(axes,box);
            assert(cyl.axis==2 && std::abs(cyl.half-.12)<1e-6 && std::abs(cyl.radius-.035)<1e-6);
            const auto in=r::palm_normal(h.palm,g);
            const auto seat=r::seat_held(cyl,h.palm,in,{0,0,.12});
            // Long axis along the fist's hole axis; the spin keeps following the hand (X down, Y left, as the old placement).
            assert(d2vr::length(seat.axes[2]-h.palm.basis[0])<1e-12 && d2vr::length(seat.axes[0]+h.palm.basis[2])<1e-12 && d2vr::length(seat.axes[1]-h.palm.basis[1])<1e-12);
            // The palm skin touches the surface (the axis used to go through the palm point: half the bottle inside the hand).
            const auto skin=h.palm.position+in*r::held_palm_depth;
            assert(std::abs(r::shape_distance(seat.shape,skin))<1e-9);
            assert(r::shape_distance(seat.shape,h.palm.position)>0); // the palm centre is outside it
            // Held 0.3 of its length up from the base: the shaft's middle lies 0.2 of the length (4.8 cm) ahead of the palm, the base behind it.
            assert(std::abs(d2vr::dot(seat.shape.frame.position-h.palm.position,h.palm.basis[0])-.048)<1e-12);
            assert(d2vr::length(seat.origin+seat.axes[2]*.12-seat.shape.frame.position)<1e-12); // the mesh centre is the shape's middle
            const auto centred=r::seat_held(cyl,h.palm,in,{0,0,.12},.5);
            assert(std::abs(d2vr::dot(centred.shape.frame.position-h.palm.position,h.palm.basis[0]))<1e-12);
            // The fingers wrap the seated bottle: touching, never through it.
            r::FingerIkState st{};const auto wrap=r::finger_contact(g,h.palm,&seat.shape,0,st);
            for(unsigned f=1;f<5;++f) {assert(wrap.touching[f] && wrap.finger[f]<1.f && wrap.finger[f]>0.f);}
            assert(long_clearance(h,wrap,seat.shape)>-5e-3);
            // Any size seats the same way, with no per-type offset: a thin pencil and a thick tin both rest on the palm skin.
            for(double radius:{.006,.02,.05}) {
                const auto sh=r::seat_held({2,.1,radius},h.palm,in,{}).shape;
                assert(std::abs(r::shape_distance(sh,skin))<1e-9);
            }
            // A palm normal that is not square to the hole axis (the rig's is 99 degrees from it) seats the same: the skin on the surface, the
            // axis one radius from it measured across the shaft (the old seat added half the shaft times the tilt, about 2 cm), the shaft
            // lying over the knuckles: held_knuckle_shift toward the finger bases, the skin still on it.
            {
                const auto tilted_in=d2vr::unit(in+h.palm.basis[0]*.17);
                const auto over=r::seat_held(cyl,h.palm,tilted_in,{0,0,.12});
                const auto skin2=h.palm.position+tilted_in*r::held_palm_depth;
                assert(std::abs(r::shape_distance(over.shape,skin2))<1e-9);
                const auto across=skin2-over.shape.frame.position;const auto square=across-h.palm.basis[0]*d2vr::dot(across,h.palm.basis[0]);
                assert(std::abs(d2vr::length(square)-cyl.radius)<1e-9);
                // The shaft lies toward the fingers of the palm skin point by held_knuckle_shift (down the grip's -up axis).
                assert(std::abs(d2vr::dot(over.shape.frame.position-skin2,h.palm.basis[2]*-1.)-std::min(r::held_knuckle_shift,.8*cyl.radius))<1e-9);
            }
            // A tilted bottle keeps radius and shaft: the fit goes through the object's drawn axes, any pose.
            const double s30=.5,c30=std::sqrt(.75);
            const std::array<Vec3,3> tilt{{{c30,0,-s30},{0,1,0},{s30,0,c30}}};
            const double ex=.12*s30+.035*c30,ez=.12*c30+.035*s30;
            const auto tilted=r::held_cylinder(tilt,{{-ex,-.035,-ez},{ex,.035,ez}});
            assert(tilted.axis==2 && std::abs(tilted.half-.12)<1e-6 && std::abs(tilted.radius-.035)<1e-6);
            // The sim's bottle (v522, bottle_02: its mesh and physics box are 13.9 x 13.9 x 41.3 cm), tilted in the hand: the world AABB the engine reports
            // is 39.7 x 24 x 40.7 cm; the cylinder fitted to those extents came out 8.7 cm in radius. The local box comes back exactly instead.
            {
                const std::array<Vec3,3> rows{{{-.733,-.221,-.643},{-.289,.957,0},{.615,.186,-.766}}};
                std::array<Vec3,3> unit_rows;for(unsigned i=0;i<3;++i) unit_rows[i]=d2vr::unit(rows[i]);
                const Vec3 local{.0693,.0693,.2065};Vec3 e{};
                for(unsigned i=0;i<3;++i) {const double hi=i==0?local.x:i==1?local.y:local.z;e=e+Vec3{std::abs(unit_rows[i].x),std::abs(unit_rows[i].y),std::abs(unit_rows[i].z)}*hi;}
                const auto got=r::held_local_extents(unit_rows,e);
                assert(got && d2vr::length(*got-local)<1e-9);
                const auto bottle_cyl=r::held_cylinder(unit_rows,{e*-1.,e});
                assert(bottle_cyl.axis==2 && std::abs(bottle_cyl.radius-.0693)<1e-9 && std::abs(bottle_cyl.half-.2065)<1e-9);
                // any pose of any box: the same local extents (the box of a box, solved)
                unsigned solved=0;
                for(unsigned k=0;k<200;++k) {
                    const double a=.1+k*.031,b=.3+k*.047,c2=.7+k*.019;
                    const auto q=r::axis_angle(d2vr::unit(Vec3{std::sin(a),std::cos(b),std::sin(c2)+1.1}),k*.173);
                    std::array<Vec3,3> turned{d2vr::rotated(q,{1,0,0}),d2vr::rotated(q,{0,1,0}),d2vr::rotated(q,{0,0,1})};
                    const Vec3 halves{.04+.001*(k%7),.05+.002*(k%5),.21-.002*(k%11)};Vec3 aabb{};
                    for(unsigned i=0;i<3;++i) {const double hi=i==0?halves.x:i==1?halves.y:halves.z;aabb=aabb+Vec3{std::abs(turned[i].x),std::abs(turned[i].y),std::abs(turned[i].z)}*hi;}
                    const auto back=r::held_local_extents(turned,aabb);
                    if(!back) continue;
                    ++solved;
                    assert(d2vr::length(*back-halves)<1e-7);
                    const auto fitted=r::held_cylinder(turned,{aabb*-1.,aabb}); // a shaft along the long side, never fatter than the box
                    assert(fitted.axis==2 && fitted.radius<=.0551 && fitted.half>.18);
                }
                assert(solved>=150); // (the unsolvable poses are the 45 degree ones: a minority)
                // a prop turned 45 degrees to its box has no unique local extents: no answer, and held_cylinder still gives a fit
                const double s45=std::sqrt(.5);
                const std::array<Vec3,3> diagonal{{{s45,s45,0},{-s45,s45,0},{0,0,1}}};
                assert(!r::held_local_extents(diagonal,{.1,.1,.2}));
                assert(r::held_cylinder(diagonal,{{-.1,-.1,-.2},{.1,.1,.2}}).radius>0);
                // the latch: solved on a good pose, kept for the poses that cannot be (the box of a prop never changes), and solved once
                std::optional<Vec3> latch;
                const auto first=r::held_cylinder(unit_rows,{e*-1.,e},&latch);
                assert(latch && d2vr::length(*latch-local)<1e-9);
                const auto kept=r::held_cylinder(diagonal,{{-.3,-.3,-.3},{.3,.3,.3}},&latch); // (bounds that would fit nothing like it: the latch answers)
                assert(kept.axis==first.axis && std::abs(kept.radius-first.radius)<1e-12 && std::abs(kept.half-first.half)<1e-12);
                std::optional<Vec3> none;
                (void)r::held_cylinder(diagonal,{{-.1,-.1,-.2},{.1,.1,.2}},&none);
                assert(!none); // nothing to latch from a pose that cannot be solved
            }
            // A box-like prop falls back to a ball of its smaller sides.
            const auto ballish=r::held_cylinder(axes,{{-.05,-.05,-.05},{.05,.05,.05}});
            assert(std::abs(ballish.radius-.05)<1e-9);
            // A body of revolution turns about its axis of symmetry, not its longest side: a tumbler 12.3 x 12.3 x 10.6 cm (sim v525: a disc of 4 mm
            // about its side) is a ball of its own radius about its up axis, in any tilt of its box and any order of its axes; a bottle, a box, a plate
            // and a cube keep what they had.
            {
                const Vec3 tum_half{.0615,.0615,.053};
                const std::array<Vec3,3> tum_rows{{{1,0,0},{0,1,0},{0,0,1}}};
                const auto tum_up=r::held_cylinder(tum_rows,{tum_half*-1.,tum_half});
                assert(tum_up.axis==2 && std::abs(tum_up.half-.053)<1e-9 && std::abs(tum_up.radius-.0615)<1e-9); // a cup: a capsule as long as it is tall, its wall straight
                const double c35=std::cos(35*deg),s35=std::sin(35*deg); // tilted 35 degrees about X: the engine's world box is e_j = sum_i half_i |axes[i][j]|
                const std::array<Vec3,3> tum_tilted{{{1,0,0},{0,c35,s35},{0,-s35,c35}}};
                const Vec3 tum_aabb{tum_half.x,tum_half.y*c35+tum_half.z*s35,tum_half.y*s35+tum_half.z*c35};
                const auto tum_turned=r::held_cylinder(tum_tilted,{tum_aabb*-1.,tum_aabb});
                assert(tum_turned.axis==2 && std::abs(tum_turned.half-.053)<1e-6 && std::abs(tum_turned.radius-.0615)<1e-6);
                const Vec3 tum_side{.053,.0615,.0615}; // the same glass with its up axis first in the model
                const auto tum_x=r::held_cylinder(tum_rows,{tum_side*-1.,tum_side});
                assert(tum_x.axis==0 && std::abs(tum_x.half-.053)<1e-9 && std::abs(tum_x.radius-.0615)<1e-9);
                const auto tum_bottle=r::held_cylinder(tum_rows,{{-.0695,-.0695,-.2065},{.0695,.0695,.2065}});
                assert(tum_bottle.axis==2 && std::abs(tum_bottle.half-.2065)<1e-9 && std::abs(tum_bottle.radius-.0695)<1e-9);
                const auto tum_lying=r::held_cylinder(tum_rows,{{-.2065,-.0695,-.0695},{.2065,.0695,.0695}}); // a bottle whose long axis is the model's X
                assert(tum_lying.axis==0 && std::abs(tum_lying.half-.2065)<1e-9);
                const auto tum_plate=r::held_cylinder(tum_rows,{{-.15,-.15,-.01},{.15,.15,.01}}); // flatter than .6 of its radius: not a ball of 15 cm
                assert(tum_plate.axis!=2 && tum_plate.radius<.1);
                const auto tum_book=r::held_cylinder(tum_rows,{{-.1,-.15,-.02},{.1,.15,.02}});
                assert(tum_book.axis==1);
                const auto tum_puck=r::held_cylinder(tum_rows,{{-.0615,-.0615,-.03},{.0615,.0615,.03}}); // under .6 of its radius tall: no axis of revolution, a stub
                assert(tum_puck.half<.02 && tum_puck.radius<.05);
                const auto tum_cube=r::held_cylinder(tum_rows,{{-.05,-.05,-.05},{.05,.05,.05}});
                assert(tum_cube.axis==2 && std::abs(tum_cube.radius-.05)<1e-9);
                const double tin[3]{.06,.075,.04},slender[3]{.04,.044,.2},squat[3]{.2,.2,.14},flat[3]{.2,.2,.1};
                assert(r::held_axis_of(tin)==1 && r::held_axis_of(slender)==2 && r::held_axis_of(squat)==2 && r::held_axis_of(flat)==0);
                // seated, the glass's up axis lies along the fist's hole axis (the palm's first axis) whatever the model's axis order
                const auto tum_seat=r::seat_held(tum_x,h.palm,in,{0,0,0});
                assert(d2vr::dot(tum_seat.axes[tum_x.axis],h.palm.basis[0])>.999);
            }
            assert(rejects([&] {(void)r::held_cylinder(axes,{{0,0,0},{0,0,0}});}));
            auto bad_seat=cyl;bad_seat.radius=0;
            assert(rejects([&] {(void)r::seat_held(bad_seat,h.palm,in,{});}));
            assert(rejects([&] {(void)r::seat_held(cyl,h.palm,in,{},1.5);}));
        }
        // One contact system for everything (Bonelab / HIGGS style): shapes of any kind, one set, one solver.
        {
            const auto world=[&](Vec3 p) {return h.palm.position+h.palm.basis[0]*p.x+h.palm.basis[1]*p.y+h.palm.basis[2]*p.z;};
            // A table under the open hand: a slab from a cast hit, 3 cm on the palm side, its normal out of the table.
            const Vec3 up=r::palm_normal(h.palm,g);
            const auto top=r::slab_shape(h.palm.position+up*.03,up*-1.);
            assert(std::abs(r::shape_distance(top,h.palm.position+up*.03))<1e-9 && r::shape_distance(top,h.palm.position)>.029 && r::shape_distance(top,h.palm.position+up*.06)<0);
            r::FingerIkState sa{};const auto flat=r::finger_contact(g,h.palm,&top,0,sa);
            for(unsigned f=1;f<5;++f) {assert(flat.touching[f] && flat.finger[f]<.35f);}
            assert(long_clearance(h,flat,top)>-5e-3);
            // The same table far away leaves the hand free; a table the hand lies on blocks all four fingers harder.
            const auto far_table=r::slab_shape(h.palm.position+up*.4,up*-1.);
            r::FingerIkState sb2{};const auto air=r::finger_contact(g,h.palm,&far_table,0,sb2);
            for(unsigned f=0;f<5;++f) assert(air.finger[f]==1.f && !air.touching[f]);
            const auto pressed=r::slab_shape(h.palm.position+up*.008,up*-1.);
            r::FingerIkState sc{};const auto on_table=r::finger_contact(g,h.palm,&pressed,0,sc);
            for(unsigned f=1;f<5;++f) assert(on_table.finger[f]<flat.finger[f]+1e-6f);
            // Several shapes at once: a bottle in the fingers and a table behind them; every finger obeys whichever it meets first.
            const auto bottle3=across_palm(h,mirror,.035,.01);
            const std::array<r::HeldShape,2> both{bottle3,top};
            r::FingerIkState sd{};const auto mixed=r::finger_contact(g,h.palm,both.data(),2,0,sd);
            r::FingerIkState se{};const auto only_bottle=r::finger_contact(g,h.palm,&bottle3,0,se);
            for(unsigned f=1;f<5;++f) assert(mixed.finger[f]<=only_bottle.finger[f]+1e-6f);
            const std::array<r::HeldShape,2> reversed{top,bottle3};
            r::FingerIkState sf2{};const auto swapped=r::finger_contact(g,h.palm,reversed.data(),2,0,sf2);
            assert(swapped.finger==mixed.finger); // order never matters
            assert(long_clearance(h,mixed,bottle3)>-5e-3 && long_clearance(h,mixed,top)>-5e-3);
            // A capsule (a bar, a limb) and a box (a crate edge) go through the same call.
            const r::HeldShape bar{{{0,.08,-mirror*.045},h.palm.basis},{.12,0,0},.03};
            const r::HeldShape crate2{{{0,.1,-mirror*.215},{{{1,0,0},{0,1,0},{0,0,1}}}},{.2,.2,.2},0};
            for(const auto* sh:{&bar,&crate2}) {
                r::FingerIkState sg{};const auto c2=r::finger_contact(g,h.palm,sh,0,sg);
                for(unsigned f=1;f<5;++f) assert(c2.touching[f] && c2.finger[f]<1.f);
                assert(long_clearance(h,c2,*sh)>-5e-3);
            }
            // Empty air: nothing in the set: unconstrained, no work.
            r::FingerIkState sh2{};const auto empty=r::finger_contact(g,h.palm,nullptr,0u,0,sh2);
            for(unsigned f=0;f<5;++f) assert(empty.finger[f]==1.f && !empty.touching[f]);
            assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,h.palm,nullptr,2u,0,s);}));
            std::array<r::HeldShape,9> too_many{};too_many.fill(bottle3);
            assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,h.palm,too_many.data(),9u,0,s);}));
            assert(rejects([&] {(void)r::slab_shape({0,0,0},{0,0,0});}));
            // The probes that find those surfaces: each finger's closing sweep ends where its tip reaches farthest, the palm one goes out of the palm.
            for(unsigned f=0;f<5;++f) {
                const auto sw=r::finger_sweep(g,h.palm,f);
                assert(d2vr::length(sw.from-world(g.finger[f].pivot))<1e-12 && d2vr::length(sw.to-sw.from)>.02);
                // The tip lies on the palm side of the open finger's plane (the finger bent toward the palm).
                assert(f==0 || d2vr::dot(sw.to-sw.from,up)>0);
            }
            const auto ps=r::palm_sweep(g,h.palm);
            assert(d2vr::dot(ps.to-ps.from,up)>.14 && d2vr::length(ps.from-h.palm.position)<.011);
            assert(rejects([&] {(void)r::finger_sweep(g,h.palm,5);}));
            // A probe hit on a table gives a slab the fingers then stop on, exactly the flat case above.
            const auto from_probe=r::slab_shape(ps.from+(ps.to-ps.from)*(.04/.15),up*-1.);
            r::FingerIkState si{};const auto stop=r::finger_contact(g,h.palm,&from_probe,0,si);
            for(unsigned f=1;f<5;++f) assert(stop.touching[f]);
            // Cost of a busy set (bottle, table, bar, crate and two more: every finger tests what it can reach) that keeps changing under a
            // hand jittering by a millimetre and a half (no memo can hold), two fingers a call: two hands, every frame.
            const std::array<r::HeldShape,6> six{bottle3,top,bar,crate2,pressed,far_table};
            r::FingerIkState busy{};double busy_sum=0,busy_max=0;
            for(unsigned i=0;i<1000;++i) {
                auto moving=h.palm;moving.position=moving.position+h.palm.basis[1]*(.0025*(i%7));
                const auto begin=std::chrono::steady_clock::now();
                (void)r::finger_contact(g,moving,six.data(),6,.011,busy,1.f,2);
                const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
                busy_sum+=us;busy_max=std::max(busy_max,us);
            }
            std::cout<<"finger_contact, 6 shapes, 2 fingers a call: "<<busy_sum/1000<<" us per hand, worst "<<busy_max<<std::endl;
            assert(busy_sum/1000<150);
        }
        // finger_turns: what BodySolver bends the fingers by. One curl per finger, the joints following it through the coupling.
        {
            const auto angles_of=[&](unsigned f,const std::array<float,3>& turn) {return final_angles(h,f,turn);};
            // Plain fist, nothing held: grip + trigger close every finger to curl 1 (the thumb joins, on its standard curve).
            r::FingerIkState sf{};const auto free_hand=r::finger_contact(g,h.palm,nullptr,0,sf);
            const auto fist_turns=r::finger_turns(g,free_hand,1.f,1.f,false);
            for(unsigned f=0;f<5;++f) {
                const auto a=r::finger_angles(f,1.);
                for(unsigned s=0;s<3;++s) assert(std::abs(angles_of(f,fist_turns[f])[s]-a[s])<1e-5);
            }
            // Grip alone leaves the index (the trigger finger) at the relaxed curl; nothing is flat.
            const auto grip_only=r::finger_turns(g,free_hand,1.f,0.f,false);
            const auto relaxed_angles=r::finger_angles(1,r::relaxed_share);
            for(unsigned s=0;s<3;++s) assert(std::abs(angles_of(1,grip_only[1])[s]-relaxed_angles[s])<1e-5);
            assert(flexion_share(angles_of(2,grip_only[2]))>flexion_share(angles_of(1,grip_only[1])));
            assert(r::finger_turns(g,free_hand,0.f,0.f,false)[3][0]>0.f);
            // The per-finger overloads (the touch curl's wants) give the scalar forms' answers for the wants those forms derive.
            for(const bool held:{false,true}) for(const float sq:{0.f,.4f,1.f}) for(const float ix:{0.f,.7f}) {
                const auto a=r::finger_turns(g,free_hand,sq,ix,held),b=r::finger_turns(g,free_hand,r::finger_wants(held,sq,ix));
                for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) assert(a[f][s]==b[f][s]);
            }
            assert(r::grip_curls(free_hand,.6f,.3f)==r::grip_curls(free_hand,std::array<float,5>{std::max(.6f,.15f),.3f,.6f,.6f,.6f}));
            {bool threw=false;try {(void)r::grip_curls(free_hand,std::array<float,5>{0,0,2.f,0,0});} catch(const std::invalid_argument&) {threw=true;}assert(threw);}
            // Holding wants every finger shut (to HIGGS's minimum openness 0.2: curl 0.8) whatever the trigger does: the index follows the
            // middle finger and stops at the surface.
            const auto bottle2=across_palm(h,mirror,.035,.01);
            r::FingerIkState sh{};const auto touch=r::finger_contact(g,h.palm,&bottle2,0,sh);
            const auto held_turns=r::finger_turns(g,touch,0.f,0.f,true);
            const auto idle_turns=r::finger_turns(g,touch,0.f,0.f,false);
            for(unsigned f=1;f<5;++f) for(unsigned s=0;s<3;++s) assert(held_turns[f][s]>=idle_turns[f][s]);
            // Held, the index follows the grip like the rest (free it would stay at the relaxed curl); a surface still caps it. A finger
            // that touches nothing closes to the wanted curl, never past 0.8: openness stays at 0.2 or more.
            const auto held_free=r::finger_turns(g,free_hand,0.f,0.f,true);
            for(unsigned f=0;f<5;++f) {
                const auto a=r::finger_angles(f,f?r::held_curl:1.f); // (the thumb all the way: its pad meets the far side, a contact stops it)
                for(unsigned s=0;s<3;++s) assert(std::abs(angles_of(f,held_free[f])[s]-a[s])<1e-5);
            }
            assert(r::held_curl==.8f && std::abs(r::held_min_openness-.2f)<1e-7);
            assert(flexion_share(angles_of(1,held_free[1]))>flexion_share(angles_of(1,r::finger_turns(g,free_hand,0.f,0.f,false)[1]))+.3);
            // A fist with no object (free hand, grip and trigger down) stays a full fist: only the hold is capped.
            for(unsigned f=1;f<5;++f) assert(flexion_share(angles_of(f,held_free[f]))<flexion_share(angles_of(f,fist_turns[f])));
            for(unsigned f=1;f<5;++f) {
                const auto a=r::finger_pose_angles(f,r::held_curl,touch.pose[f]);
                for(unsigned s=0;s<3;++s) assert(std::abs(angles_of(f,held_turns[f])[s]-a[s])<1e-5);
            }
            // The turns are the joint angles of one curl: applied to the bind they keep the surface clear.
            r::FingerCurls applied{};for(unsigned f=0;f<5;++f) applied.pose[f]=touch.pose[f];
            assert(long_clearance(h,applied,bottle2)>-5e-3);
            auto weak=g;weak.finger[1].existing[1]=.5; // a rest bend is not added twice
            assert(r::finger_turns(weak,free_hand,1.f,1.f,false)[1][1]<fist_turns[1][1]-.4f);
            assert(rejects([&] {(void)r::finger_turns(g,touch,2.f,0.f,true);}));
        }
        // HIGGS-style contact refinements (user, headset 2026-10-14: fingers must react to every object like Boneworks/Bonelab).
        {
            const Vec3 up=r::palm_normal(h.palm,g);
            const auto world=[&](Vec3 p) {return world_of(h,p);};
            // The first angle at which the given arcs touch the shape in one stage of finger f's wrap (stage 0: the knuckle alone, stage 1: the
            // middle joint with the distal one following, stage 2: the distal joint alone), from `prev` (the joints the earlier stages left),
            // scanned a thousand steps, independent of the solver's march and bisect. The limit when nothing does.
            const auto stage_hit=[&](unsigned f,const r::HeldShape& sh,unsigned stage,std::array<double,3> prev,unsigned arcs) {
                const double limit=r::finger_limit_degrees[f][stage]*deg,follow=r::finger_limit_degrees[f][2]/r::finger_limit_degrees[f][1];
                const double start=stage==2?std::min(r::finger_limit_degrees[f][2]*deg,follow*prev[1]):0.;
                const auto skin=[&](const std::array<double,3>& a,unsigned mask) {
                    return r::finger_clearance(g.finger[f],g.thickness[f],r::finger_turns_of(g.finger[f],a),world,[&](Vec3 w) {return r::shape_distance(sh,w);},mask,4,-1e300,stage==2?2u:stage,2);};
                auto at=[&](double u,double& angle) {
                    auto a=prev;
                    if(stage==0) {a={u*limit,0,0};angle=a[0];}
                    else if(stage==1) {a[1]=u*limit;a[2]=std::min(r::finger_limit_degrees[f][2]*deg,follow*a[1]);angle=a[1];}
                    else {a[2]=start+u*(limit-start);angle=a[2];}
                    return a;
                };
                double angle=0;
                const double floor=std::min(0.,skin(at(0.,angle),r::arc_all))-1e-3; // (the solver lets a joint go a millimetre deeper than it rests)
                for(unsigned i=0;i<=1000;++i) {
                    const auto a=at(i/1000.,angle);
                    if(skin(a,arcs)<floor) return angle;
                }
                return limit;
            };
            // The arcs: in every stage whichever arc (pad, back, tip) touches first stops the joint (the least-curled hit), at the same place a
            // scan finds: a bar across the palm, a table six centimetres under it, a small pill the tip grazes.
            const auto bar=across_palm(h,mirror,sword_handle_radius,.01);
            const auto table6=r::slab_shape(h.palm.position+up*.06,up*-1.);
            const r::HeldShape pill{{{mirror*.03,.18,-mirror*.02},h.palm.basis},{},.01}; // a 2 cm pill the tip grazes: not stepped over
            unsigned winners{};
            for(const auto* sh:{&bar,&table6,&pill}) {
                r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,sh,0,st);
                for(unsigned f=1;f<5;++f) {
                    std::array<double,3> prev{};
                    for(unsigned stage=0;stage<3;++stage) {
                        const double limit=r::finger_limit_degrees[f][stage]*deg;
                        const double tip=stage_hit(f,*sh,stage,prev,r::arc_tip),outer=stage_hit(f,*sh,stage,prev,r::arc_outer),inner=stage_hit(f,*sh,stage,prev,r::arc_inner);
                        const double least=std::min({tip,outer,inner});
                        assert(std::abs(stage_hit(f,*sh,stage,prev,r::arc_all)-least)<1e-9);
                        assert(std::abs(c.pose[f][stage]-least)<.04);
                        if(least<limit-1e-6 && stage!=2) winners|=(tip==least?r::arc_tip:0u)|(outer==least?r::arc_outer:0u)|(inner==least?r::arc_inner:0u);
                        if(least<limit-1e-6 && stage==2) winners|=(tip==least?r::arc_tip:0u)|(outer==least?r::arc_outer:0u)|(inner==least?r::arc_inner:0u);
                        prev[stage]=c.pose[f][stage];
                        if(stage==1) prev[2]=std::min(r::finger_limit_degrees[f][2]*deg,r::finger_limit_degrees[f][2]/r::finger_limit_degrees[f][1]*prev[1]);
                    }
                }
            }
            assert(winners&r::arc_inner); // the pad arc (with the skin a tilted pad leans onto) stopped joints; the tip and the back never touch earlier here
            // The tip arc alone, in every stage, never comes before the pad's lean toward the tip by more than a few degrees: it is the end of that arc.
            for(unsigned f=1;f<5;++f) {
                assert(stage_hit(f,table6,0,{},r::arc_tip)<stage_hit(f,table6,0,{},r::arc_outer));
                assert(stage_hit(f,table6,0,{},r::arc_tip)>stage_hit(f,table6,0,{},r::arc_inner)-.01);
            }
            // The arcs are in the finger's own plane: a pin beside a finger (within the old swept capsule's thickness, never in the way of
            // the closing finger) does not stop it. The finger's thickness sphere would have.
            {
                const auto joint2=world(r::finger_points(g.finger[1],turns_of(h,1,.5))[2]);
                const auto hinge=h.palm.basis[0]*g.finger[1].hinge[1].x+h.palm.basis[1]*g.finger[1].hinge[1].y+h.palm.basis[2]*g.finger[1].hinge[1].z;
                const r::HeldShape pin{{joint2+hinge*.006,h.palm.basis},{},.004};
                assert(r::shape_distance(pin,joint2)-g.thickness[1]<0); // the swept capsule stopped here
                r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&pin,0,st);
                assert(!c.touching[1] && c.finger[1]==1.f);
            }
            // Contact means ENTRY from the free side, not touching at rest. (1) Resting against a shape (a ball touching the back of the
            // finger, a millimetre in) does not freeze the finger: it closes away from it. (2) A finger that already sits deep inside a
            // shape (a seat that overlaps the open hand) is not blocked by it: it closes, and is stopped only by a shape it enters.
            {
                const r::HeldShape back{{{mirror*.03,.125,mirror*.011},h.palm.basis},{},.004};
                assert(posed_clearance(h,rest,back,1)<0 && posed_clearance(h,rest,back,1)>-r::contact_tolerance);
                r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&back,0,st);
                assert(c.finger[1]==1.f && !c.touching[1]);
                const r::HeldShape deep{{{mirror*.03,.125,0},h.palm.basis},{},.03};
                assert(posed_clearance(h,rest,deep,1)<-.015);
                r::FingerIkState sd2{};const auto c2=r::finger_contact(g,h.palm,&deep,0,sd2);
                assert(c2.finger[1]>.3f);
                // ... and one it enters on the way is still a wall: the same deep ball with a bar further round the curl.
                const std::array<r::HeldShape,2> two{deep,across_palm(h,mirror,.02,.02,.12)};
                r::FingerIkState sd3{};const auto c3=r::finger_contact(g,h.palm,two.data(),2,0,sd3);
                assert(c3.finger[1]<c2.finger[1] || c3.finger[1]==1.f);
                assert(c3.finger[1]>0.f);
            }
            // Thumb: it closes across the palm from an OPEN pose, past its bind pose (finger_open_degrees: the CMC swings out and away from the
            // palm, the MCP and IP extend), as one; a joint the bind pose leaves inside a shape starts clear of it and enters it from the free
            // side, and the joints beyond the phalanx that touched close on (the same stages as a finger's).
            {
                const auto open_a=r::finger_open_angles(0),top=r::finger_angles(0,1.);
                // Nothing in reach: the free path, whatever the open limits.
                r::FingerIkState sf{};const auto free_thumb=r::finger_contact(g,h.palm,nullptr,0,sf);
                for(unsigned s=0;s<3;++s) assert(std::abs(free_thumb.pose[0][s]-top[s])<1e-6);
                // The turns never go below the open limits, and only the thumb goes negative at all.
                for(unsigned s=0;s<3;++s) {
                    assert(g.finger[0].lowest[s]<0 && std::abs(g.finger[0].lowest[s]+g.finger[0].existing[s]-open_a[s])<1e-9);
                    for(unsigned f=1;f<5;++f) assert(g.finger[f].lowest[s]==0);
                }
                // A ball on the thumb's path half way from open to closed: the thumb moves off its open pose and stops short of the ball.
                std::array<double,3> half_way{};for(unsigned s=0;s<3;++s) half_way[s]=open_a[s]+.5*(top[s]-open_a[s]);
                const r::HeldShape on_path{{world(r::finger_points(g.finger[0],r::finger_turns_of(g.finger[0],half_way))[3]),h.palm.basis},{},.004};
                r::FingerIkState st{};const auto a=r::finger_contact(g,h.palm,&on_path,0,st);
                assert(a.touching[0] && a.pose[0][0]>open_a[0]+.05 && a.pose[0][0]<half_way[0]+.05);
                // A ball under the thumb's bind pose (a seat or a slab buries a thumb that hangs out of the palm toward it): the thumb starts
                // from its open pose, clear of the ball, closes until it touches it from the free side, and stops with its CMC still opened out.
                const auto bind_points=r::finger_points(g.finger[0],{0,0,0});
                const r::HeldShape under{{world((bind_points[1]+bind_points[2])*.5),h.palm.basis},{},.012};
                r::FingerCurls bind_pose{};
                assert(thumb_clearance(h,bind_pose,under)<-.01); // (buried at its bind pose)
                r::FingerIkState sp{};const auto lifted=r::finger_contact(g,h.palm,&under,0,sp);
                assert(lifted.touching[0] && lifted.pose[0][0]<0 && thumb_clearance(h,lifted,under)>-3e-3);
            }
            // Holding: openness 0.2 at least. Nothing reaches the fingers of a hand holding air, so they close to curl 0.8; a fist is a full fist.
            {
                r::FingerIkState st{};const auto air=r::finger_contact(g,h.palm,nullptr,0,st);
                const auto shut=r::finger_turns(g,air,0.f,0.f,true),full=r::finger_turns(g,air,1.f,1.f,false);
                for(unsigned f=0;f<5;++f) {
                    const auto a=r::finger_angles(f,f?r::held_curl:1.f);
                    for(unsigned s=0;s<3;++s) {assert(std::abs(final_angles(h,f,shut[f])[s]-a[s])<1e-5);assert(shut[f][s]<=full[f][s]+1e-6f);}
                }
                const auto legacy=r::grip_curls(air,r::held_curl,r::held_curl);
                for(float c:legacy) assert(c==r::held_curl);
            }
            // The grip point is where the palm touched. A bottle taken 20% up from its base: held there; taken near the top or the base, held at
            // the clamp; taken from afar or a ball, the default third. Its roll about the shaft comes along.
            {
                const d2vr::NativeGrabBounds box{{-.035,-.035,0},{.035,.035,.24}};
                const std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
                const auto cyl=r::held_cylinder(axes,box);const Vec3 centre{0,0,.12};
                const auto in=r::palm_normal(h.palm,g);
                const auto palm_at=[&](double fraction) { // a palm whose skin lies on the bottle at that fraction of its length, a radius out
                    auto p=h.palm;const Vec3 skin=centre+Vec3{.035,0,(fraction-.5)*.24};
                    p.position=skin-in*r::held_palm_depth;return p;};
                for(double fraction:{.2,.3,.4}) {
                    const auto touch=r::held_touch(cyl,axes,centre,palm_at(fraction),in);
                    assert(touch.touched && std::abs(touch.along-fraction)<1e-9);
                    const auto seat=r::seat_held(cyl,palm_at(fraction),in,{0,0,.12},touch.along);
                    assert(std::abs(d2vr::dot(seat.shape.frame.position-palm_at(fraction).position,h.palm.basis[0])-(.5-touch.along)*.24)<1e-9);
                }
                // (The default hold sits at .3: 4.8 cm ahead of the palm. A bottle taken at .2 sits 7.2 cm ahead.)
                const auto taken=r::held_touch(cyl,axes,centre,palm_at(.2),in);
                const auto default_seat=r::seat_held(cyl,palm_at(.2),in,{0,0,.12});
                const auto touch_seat=r::seat_held(cyl,palm_at(.2),in,{0,0,.12},taken.along);
                assert(d2vr::dot(touch_seat.shape.frame.position-default_seat.shape.frame.position,h.palm.basis[0])>.02);
                assert(std::abs(r::held_touch(cyl,axes,centre,palm_at(.05),in).along-r::held_along_min)<1e-9);
                assert(std::abs(r::held_touch(cyl,axes,centre,palm_at(.95),in).along-r::held_along_max)<1e-9);
                auto distant=palm_at(.2);distant.position=distant.position+Vec3{2,0,0};
                const auto none=r::held_touch(cyl,axes,centre,distant,in);
                assert(!none.touched && none.along==r::held_grip_fraction && none.roll==0.);
                // Out of reach but pointing at it (a hand reaching for the top of a bottle, 35 cm off when the press lands): with an aim reach the grip
                // goes where the axis is nearest the palm, `touched` stays false and the roll the default; without one, or beyond it, the default third.
                const Vec3 square{1,0,0}; // square to the shaft
                auto far_up=palm_at(.4);far_up.position=far_up.position-square*.35;
                const auto aimed=r::held_touch(cyl,axes,centre,far_up,in,r::held_aim_reach);
                assert(!aimed.touched && aimed.roll==0. && std::abs(aimed.along-.4)<1e-6 && r::held_touch(cyl,axes,centre,far_up,in).along==r::held_grip_fraction);
                auto very_far=far_up;very_far.position=very_far.position-square*3.;
                assert(r::held_touch(cyl,axes,centre,very_far,in,r::held_aim_reach).along==r::held_grip_fraction);
                const auto ball_touch=r::held_touch({2,0,.05},axes,{0,0,0},palm_at(.2),in);
                assert(ball_touch.along==r::held_grip_fraction);
                // Roll: the object tilted away from the fist keeps the spin it had about its shaft.
                const double roll0=.7;
                const auto spin_axes=[&](double tilt,double twist) {
                    // seat axes at the hand with roll0, then tilted about the hand's left axis and twisted about the hand's forward axis
                    const auto base=r::seat_held(cyl,h.palm,in,{},.3,roll0).axes;
                    const auto twist_q=r::axis_angle(h.palm.basis[0],twist),tilt_q=r::axis_angle(h.palm.basis[1],tilt);
                    std::array<Vec3,3> out{};for(unsigned i=0;i<3;++i) out[i]=d2vr::rotated(tilt_q,d2vr::rotated(twist_q,base[i]));return out;
                };
                const auto tilted=r::held_touch(cyl,spin_axes(.9,0),centre,palm_at(.3),in);
                assert(std::abs(tilted.roll-roll0)<1e-6);
                const auto twisted=r::held_touch(cyl,spin_axes(.9,.5),centre,palm_at(.3),in);
                assert(std::abs(twisted.roll-(roll0+.5))<1e-6);
                const auto seat=r::seat_held(cyl,h.palm,in,{},.3,roll0);
                assert(d2vr::length(seat.axes[2]-h.palm.basis[0])<1e-9);
                const auto spun=d2vr::rotated(r::axis_angle(h.palm.basis[0],roll0),h.palm.basis[2]*-1.);
                assert(d2vr::length(seat.axes[0]-spun)<1e-9 && std::abs(d2vr::dot(seat.axes[0],seat.axes[1]))<1e-9);
                assert(d2vr::length(r::seat_held(cyl,h.palm,in,{}).axes[0]+h.palm.basis[2])<1e-9); // no roll: X down, as before
                assert(rejects([&] {(void)r::seat_held(cyl,h.palm,in,{},.3,std::nan(""));}));
                assert(rejects([&] {(void)r::held_touch(cyl,{{{2,0,0},{0,1,0},{0,0,1}}},centre,h.palm,in);}));
            }
            // A hand lying on a surface: the palm skin within 3 cm of it and slow curls the four fingers lightly (their contact still caps them).
            {
                const auto table_at=[&](double gap) {return r::slab_shape(h.palm.position+up*(r::held_palm_depth+gap),up*-1.);};
                for(const auto& [gap,expect]:{std::pair<double,float>{.0,.35f},{.02,.35f},{.025,.175f},{.03,0.f},{.06,0.f}}) {
                    const auto t=table_at(gap);r::FingerIkState st{};
                    const auto c=r::finger_contact(g,h.palm,&t,0,st);
                    assert(std::abs(c.resting-expect)<1e-5f);
                }
                const auto t=table_at(.02);
                r::FingerIkState st{};const auto still=r::finger_contact(g,h.palm,&t,0,st);
                const auto lying=r::grip_curls(still,0.f,0.f);
                assert(lying[0]==0.f);
                for(unsigned f=1;f<5;++f) assert(lying[f]==std::min(r::resting_curl,still.finger[f]) && lying[f]>0.f);
                const auto flat_turns=r::finger_turns(g,still,0.f,0.f,false);
                const auto no_shape=r::free_curls();
                const auto relaxed_turns=r::finger_turns(g,no_shape,0.f,0.f,false);
                auto lightly=no_shape;lightly.resting=r::resting_curl;
                const auto lit=r::finger_turns(g,lightly,0.f,0.f,false);
                for(unsigned s=0;s<3;++s) {
                    assert(std::abs(lit[0][s]-relaxed_turns[0][s])<1e-6f); // the thumb keeps the relaxed curl
                    for(unsigned f=1;f<5;++f) assert(std::abs(lit[f][s]-r::finger_angles(f,r::resting_curl)[s])<1e-5f && lit[f][s]>relaxed_turns[f][s]);
                }
                for(unsigned f=1;f<5;++f) assert(flexion_share(final_angles(h,f,flat_turns[f]))<=flexion_share(final_angles(h,f,lit[f]))+1e-6); // the table's contact caps them
                // Moving: the speed (smoothed) takes it away over a few tau; slow drift keeps it; stopping brings it back.
                r::FingerIkState sm{};Vec3 shift{};auto moving=h.palm;
                const auto run=[&](unsigned frames,double speed) {
                    r::FingerCurls last{};
                    for(unsigned i=0;i<frames;++i) {
                        shift=shift+Vec3{speed*.011,0,0};moving.position=h.palm.position+shift;
                        const auto slab_here=r::slab_shape(moving.position+up*(r::held_palm_depth+.015),up*-1.);
                        last=r::finger_contact(g,moving,&slab_here,.011,sm);
                    }
                    return last;
                };
                assert(run(5,0.).resting>.34f);
                assert(run(40,.1).resting>.34f);
                assert(run(40,1.).resting<.02f);
                assert(run(60,0.).resting>.34f);
            }
        }
        // Squeeze clamp.
        const auto gripped=r::grip_curls(on_handle,1.f,.5f);
        for(unsigned f=0;f<5;++f) assert(gripped[f]==std::min(f==1?.5f:1.f,on_handle.finger[f]));
        // A relaxed hand on the handle (palm skin on its surface, still): the four fingers rest at the resting curl, the thumb keeps its own.
        assert(on_handle.resting==r::resting_curl);
        const auto relaxed=r::grip_curls(on_handle,.1f,0.f);
        assert(relaxed[0]==.1f);
        for(unsigned f=1;f<5;++f) assert(relaxed[f]==std::min(r::resting_curl,on_handle.finger[f]));
        // Validation.
        auto bad_palm=h.palm;bad_palm.position.x=std::nan("");
        assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,bad_palm,nullptr,0,s);}));
        auto skewed=h.palm;skewed.basis[1]=skewed.basis[0];
        assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,skewed,nullptr,0,s);}));
        assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,h.palm,nullptr,-.01,s);}));
        auto bad_shape=handle;bad_shape.radius=-1;
        assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(g,h.palm,&bad_shape,0,s);}));
        auto bad_geometry=g;bad_geometry.finger[2].hinge[1]={0,0,0};
        assert(rejects([&] {r::FingerIkState s{};(void)r::finger_contact(bad_geometry,h.palm,nullptr,0,s);}));
        assert(rejects([&] {(void)r::grip_curls(on_handle,1.5f,0.f);}));
        auto bad_chains=h.chains;bad_chains[1][0]=100;
        assert(rejects([&] {(void)r::hand_geometry(h.bind,h.parents,bad_chains,{},0,h.grip);}));
        assert(rejects([&] {(void)r::classify_held(h.palm,std::nullopt,17,h.palm);}));
        // Cost, a hand holding a bottle (the shape rides with the palm, which moves a millimetre a frame): two hands every frame on the render thread.
        {
            const auto in=r::palm_normal(h.palm,g);
            const auto shaft=r::seat_held(r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},d2vr::NativeGrabBounds{{-.035,-.035,0},{.035,.035,.25}}),h.palm,in,{0,0,.125}).shape;
            const auto slab_in_world=r::slab_shape(h.palm.position+in*.05,in*-1.);
            for(const bool with_slab:{false,true}) {
                r::FingerIkState timing{};double sum=0,worst=0;unsigned frames=0;
                for(unsigned i=0;i<1500;++i) {
                    const Vec3 delta=h.palm.basis[0]*(.001*(i%80)); // sliding along the wall: it stays in reach and changes under the hand every frame
                    auto moving=h.palm;moving.position=moving.position+delta;
                    std::array<r::HeldShape,2> set{shaft,slab_in_world};set[0].frame.position=set[0].frame.position+delta;
                    const auto begin=std::chrono::steady_clock::now();
                    (void)r::finger_contact(g,moving,set.data(),with_slab?2u:1u,.011,timing,r::held_curl,2);
                    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
                    if(i>=10) {sum+=us;worst=std::max(worst,us);++frames;}
                }
                std::cout<<"finger_contact, held bottle"<<(with_slab?" + a wall the hand drifts along":"")<<": "<<sum/frames<<" us per hand, worst "<<worst<<std::endl;
                assert(sum/frames<(with_slab?60:15));
            }
        }
    }
    // Left and right synthetic hands are mirror images: identical contact.
    for(unsigned f=0;f<5;++f) assert(std::abs(by_mirror[0].finger[f]-by_mirror[1].finger[f])<1e-5f);
    // Shape classifier.
    {
        const auto h=synthetic_hand(1);
        const d2vr::WorldTrackedPose upright{{1,2,.12},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const d2vr::NativeGrabBounds bottle_box{{1-.035,2-.035,0},{1+.035,2+.035,.24}};
        const auto bottle=r::classify_held(upright,bottle_box,-1,h.palm);
        assert(std::abs(bottle.radius-.035)<1e-6 && std::abs(bottle.half.x-.12)<1e-6 && bottle.half.y==0 && bottle.half.z==0);
        assert(d2vr::length(bottle.frame.basis[0]-Vec3{0,0,1})<1e-9 && d2vr::length(bottle.frame.position-Vec3{1,2,.12})<1e-9);
        // The same cylinder tilted 30 degrees about Y (its axis is basis[2]): the AABB still yields radius, height and axis.
        const double s30=.5,c30=std::sqrt(.75);
        const d2vr::WorldTrackedPose tilted{{0,0,0},{{{c30,0,-s30},{0,1,0},{s30,0,c30}}}};
        const double ex=.12*s30+.035*c30,ez=.12*c30+.035*s30;
        const d2vr::NativeGrabBounds tilted_box{{-ex,-.035,-ez},{ex,.035,ez}};
        const auto pipe=r::classify_held(tilted,tilted_box,-1,h.palm);
        assert(std::abs(pipe.radius-.035)<1e-6 && std::abs(pipe.half.x-.12)<1e-6 && pipe.half.y==0);
        assert(d2vr::length(pipe.frame.basis[0]-Vec3{s30,0,c30})<1e-9);
        // A book: no capsule explains it; the AABB stays.
        const d2vr::NativeGrabBounds book_box{{-.1,-.075,-.015},{.1,.075,.015}};
        const auto book=r::classify_held(upright,book_box,-1,h.palm);
        assert(book.radius==0 && d2vr::length(book.half-Vec3{.1,.075,.015})<1e-9 && d2vr::length(book.frame.basis[0]-Vec3{1,0,0})<1e-9);
        // A cube and a ball share an AABB: the inscribed puck is chosen (ponytail: shape type from the body record later).
        const d2vr::NativeGrabBounds cube_box{{-.05,-.05,-.05},{.05,.05,.05}};
        const auto ball=r::classify_held(upright,cube_box,-1,h.palm);
        assert(std::abs(ball.radius-.05)<1e-6 && std::abs(ball.half.x-.05)<1e-6 && ball.half.y==0 && ball.half.z==0);
        // A guard's wrist: the bone radius, across the palm.
        const auto limb=r::classify_held(upright,d2vr::NativeGrabBounds{{1-.06,2-.06,.06},{1+.06,2+.06,.18}},7,h.palm);
        assert(std::abs(limb.radius-.06)<1e-6 && limb.half.x==.15 && d2vr::length(limb.frame.basis[0]-h.palm.basis[0])<1e-9);
        const auto limb_no_bounds=r::classify_held(upright,std::nullopt,10,h.palm);
        assert(std::abs(limb_no_bounds.radius-d2vr::ragdoll_bone_radius[10])<1e-9);
        // No bounds at all: the old 10 cm fallback.
        const auto unknown=r::classify_held(upright,std::nullopt,-1,h.palm);
        assert(unknown.radius==.1 && d2vr::length(unknown.half)==0);
        assert(rejects([&] {(void)r::classify_held(upright,d2vr::NativeGrabBounds{{0,0,0},{0,0,0}},-1,h.palm);}));
        // Distance field sanity: inside negative, on the surface zero, outside positive.
        assert(r::shape_distance(bottle,{1,2,.12})<0);
        assert(std::abs(r::shape_distance(bottle,{1.035,2,.12}))<1e-9);
        assert(std::abs(r::shape_distance(book,{.1,0,.1})-.085)<1e-9);
    }
    // The player's real rig, both hands (the arm data the full-body driver bends, as dxgi_probe builds its geometry).
    for(bool right:{false,true}) {
        const auto h=real_hand(right);const auto& g=h.geometry;
        const char* digit_names[5]={"thumb","index","middle","ring","pinky"};
        const bool report=false;
        // Hinge signs and axes: every long finger closes toward the palm normal at each joint; the rest bends are small.
        for(unsigned f=1;f<5;++f) {
            const auto open_tip=r::finger_points(g.finger[f],turns_of(h,f,0.))[3],closed_tip=r::finger_points(g.finger[f],turns_of(h,f,.5))[3]; // (half closed: the tip is furthest toward the palm side)
            assert(d2vr::dot(closed_tip-open_tip,g.palm_in)>.05);
            for(unsigned s=0;s<3;++s) {
                const auto turned=d2vr::rotated(r::axis_angle(g.finger[f].hinge[s],.5),g.finger[f].bone[s]);
                assert(d2vr::dot(turned-g.finger[f].bone[s],g.palm_in)>0 && std::abs(g.finger[f].existing[s])<15*deg);
            }
        }
        for(unsigned s=0;s<3;++s) assert(std::abs(g.finger[0].existing[s])<15*deg);
        // The thumb of a fist lies ON the folded fingers: the index and middle phalanges are shapes to it (every finger asks for a full curl, no
        // object): it stops against them, with its pad near the index or middle finger's middle phalanx, not inside the hand.
        {
            const std::array<float,5> fist{1.f,1.f,1.f,1.f,1.f};
            r::FingerIkState sf{};const auto c=r::finger_contact(g,h.palm,nullptr,0,0,sf,1.f,r::finger_solve_all,&fist);
            const auto points=[&](unsigned f) {return r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],r::finger_pose_angles(f,1.,c.pose[f])));};
            const auto thumb=points(0);double least=1e9,near_pad=1e9;
            const Vec3 pad=(thumb[2]+thumb[3])*.5;
            const auto segment_distance=[&](Vec3 a,Vec3 b,Vec3 q) {const auto ab=b-a;const double t=std::clamp(d2vr::dot(q-a,ab)/std::max(1e-12,d2vr::dot(ab,ab)),0.,1.);return d2vr::length(q-(a+ab*t));};
            for(unsigned f=1;f<5;++f) {
                const auto fp=points(f);
                if(f<=2) near_pad=std::min(near_pad,d2vr::length(pad-(fp[1]+fp[2])*.5));
                for(unsigned k=0;k<3;++k) for(unsigned ts=0;ts<3;++ts) for(unsigned ti=0;ti<=4;++ti)
                    least=std::min(least,segment_distance(fp[k],fp[k+1],thumb[ts]+(thumb[ts+1]-thumb[ts])*(ti/4.))-g.thickness[f]-g.thickness[0]);
            }
            if(report) std::cerr<<(right?"right":"left")<<" fist: thumb touching "<<c.touching[0]<<", skin clearance to the fingers "<<least*1000<<" mm, pad to the middle phalanx "<<near_pad*100<<" cm"<<std::endl;
            assert(c.touching[0] && least>-.006 && near_pad<.05);
            const auto across=d2vr::unit(g.finger[4].pivot-g.finger[1].pivot); // index to little finger: the free thumb closes across the palm
            const auto tip_free=[&](double curl) {return r::finger_points(g.finger[0],turns_of(h,0,curl))[3];};
            assert(d2vr::dot(tip_free(1.)-tip_free(0.),across)>.08 && d2vr::dot(tip_free(.5)-tip_free(0.),across)>.03);
            // with nothing folded (the fingers ask for little) the thumb takes the whole of its path
            const std::array<float,5> open{.3f,.3f,.3f,.3f,.3f};
            r::FingerIkState so{};const auto o=r::finger_contact(g,h.palm,nullptr,0,0,so,1.f,r::finger_solve_all,&open);
            assert(!o.touching[0] && o.pose[0][0]>=r::finger_angles(0,1.)[0]-1e-6);
        }
        // A cylinder seated against the palm (the palm skin on its surface, over the knuckles): the open hand is just outside it
        // and the hand closes around it: every long finger wraps with all three joints flexed, the thumb opposes on the far side.
        for(const auto& [radius,min_share]:{std::pair<double,double>{.017,.6},{.02,.6},{.03,.5},{.035,.45},{.045,0.}}) {
            const d2vr::NativeGrabBounds box{{-radius,-radius,0},{radius,radius,.24}};
            const auto cyl=r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},box);
            const auto in=r::palm_normal(h.palm,g);
            const auto shape=r::seat_held(cyl,h.palm,in,{0,0,.12}).shape;
            r::FingerCurls rest{};
            // The seat leaves the open fingers outside it (or touching) ...
            for(unsigned f=1;f<5;++f) assert(posed_clearance(h,rest,shape,static_cast<int>(f))>-r::contact_tolerance);
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&shape,0,st);
            const auto turn=r::finger_turns(g,c,0.f,0.f,true);
            for(unsigned f=1;radius<.04 && f<5;++f) { // (a 9 cm bottle is the thumb's check only: the fingers just touch it)
                const auto a=final_angles(h,f,turn[f]);
                if(report) std::cerr<<(right?"right ":"left ")<<digit_names[f]<<" r="<<radius<<": curl "<<c.finger[f]<<" angles "<<a[0]/deg<<" "<<a[1]/deg<<" "<<a[2]/deg<<" flexion share "<<flexion_share(a)<<std::endl;
                assert(c.touching[f]);
                for(unsigned s=0;s<3;++s) assert(a[s]>12*deg); // all three joints flexed: no hook, no straight finger
                assert(flexion_share(a)>min_share);
                assert(a[2]>=a[1]*r::dip_per_pip-1e-6);   // the distal joint is at least its coupled share
            }
            // ... and the thumb, which the bind pose leaves hanging inside a seat around the palm (30 mm and more inside it at r 3.5 cm, which
            // is where v520 left it, root and proximal phalanx), opens past its bind pose, starts clear of the shape, and closes onto it
            // from the free side: no part of it more than 3 mm inside.
            const auto bind_inside=thumb_clearance(h,rest,shape);
            const auto open_pose=r::finger_open_angles(0);
            const auto open_clearance=r::finger_clearance(g.finger[0],g.thickness[0],r::finger_turns_of(g.finger[0],open_pose),
                [&](Vec3 p) {return world_of(h,p);},[&](Vec3 w) {return r::shape_distance(shape,w);},0u,12);
            const auto thumb_now=thumb_clearance(h,c,shape);
            if(report) std::cerr<<(right?"right":"left")<<" thumb r="<<radius<<": bind pose "<<bind_inside*1000<<" mm, open pose "<<open_clearance*1000<<" mm, now "<<thumb_now*1000<<" mm, pose "<<c.pose[0][0]/deg<<" "<<c.pose[0][1]/deg<<" "<<c.pose[0][2]/deg<<std::endl;
            if(radius>=.03) assert(bind_inside<-.01);
            assert(open_clearance>-r::contact_tolerance && thumb_now>-3e-3 && c.touching[0] && c.pose[0][0]<0);
            // The power grip as the game calls it (a hold: reach held_curl, the hold's wants): the thumb comes up the wrist side and over the top,
            // its pad on the far side of the shaft from the palm's contact: at least ~120 degrees around the axis for the tip (the pad a little
            // less), within 3 mm of the surface.
            if(radius==.017 || radius==.035 || radius==.045) {
                const auto wants=r::finger_wants(true,1.f,0.f);
                r::FingerIkState sg{};const auto g2=r::finger_contact(g,h.palm,&shape,1,0,sg,r::held_curl,r::finger_solve_all,&wants);
                const auto angles=r::finger_pose_angles(0,wants[0],g2.pose[0]);
                const auto tp=r::finger_points(g.finger[0],r::finger_turns_of(g.finger[0],angles));
                const Vec3 axis=shape.frame.basis[0];const Vec3 skin=h.palm.position+in*r::held_palm_depth;
                const auto around=[&](Vec3 local) {
                    const auto perp=[&](Vec3 v) {return v-axis*d2vr::dot(v,axis);};
                    return std::acos(std::clamp(d2vr::dot(d2vr::unit(perp(skin-shape.frame.position)),d2vr::unit(perp(world_of(h,local)-shape.frame.position))),-1.,1.))/deg;};
                const double tip_angle=around(tp[3]),pad_angle=around((tp[2]+tp[3])*.5);
                const auto clearance=r::finger_clearance(g.finger[0],g.thickness[0],r::finger_turns_of(g.finger[0],angles),[&](Vec3 p) {return world_of(h,p);},
                    [&](Vec3 w) {return r::shape_distance(shape,w);},0u,12);
                if(report) std::cerr<<(right?"right":"left")<<" power grip r="<<radius<<": thumb tip "<<tip_angle<<" deg, pad "<<pad_angle<<" deg around the shaft from the palm, clearance "<<clearance*1000<<" mm"<<std::endl;
                assert(tip_angle>=115 && clearance>-3e-3 && g2.touching[0]);
                if(radius==.035) assert(pad_angle>=115);
            }
        }
        // A hand resting on a table (slab 2.5 cm under the palm): the thumb lifts clear of it instead of hanging through; the long fingers rest on it.
        {
            const auto in=r::palm_normal(h.palm,g);const auto slab=r::slab_shape(h.palm.position+in*.025,in*-1.);
            r::FingerCurls rest{};
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&slab,0,st);
            if(report) std::cerr<<(right?"right":"left")<<" table: thumb bind "<<thumb_clearance(h,rest,slab)*1000<<" mm, now "<<thumb_clearance(h,c,slab)*1000<<" mm, pose "<<c.pose[0][0]/deg<<std::endl;
            // (the thumb's closing path leads up off the table: its cap is the path's end; what lies on it is the relaxed thumb)
            const auto relaxed=r::finger_pose_angles(0,r::relaxed_share,c.pose[0]);
            const auto relaxed_clearance=r::finger_clearance(g.finger[0],g.thickness[0],r::finger_turns_of(g.finger[0],relaxed),
                [&](Vec3 p) {return world_of(h,p);},[&](Vec3 w) {return r::shape_distance(slab,w);},0u,12);
            assert(thumb_clearance(h,rest,slab)<-.01 && thumb_clearance(h,c,slab)>-3e-3 && relaxed_clearance>-3e-3);
        }
        // A profiled shaft: the sim bottle's real cross-section (its physics shape: body 6.9 cm in radius up to 26 cm, neck 2.5 cm above), measured
        // with casts across it (radius_profile.hpp). One mechanism for any prop: the fingers close on the profile where the palm lies.
        {
            r::RadiusProfile prof;prof.half=.2065;prof.axis=2;
            assert(!prof.valid());
            for(unsigned i=0;i<r::profile_stations;++i) {
                const double at=r::profile_station(i,prof.half),radius=at<.056?.0693:.025;
                assert(r::profile_record(prof,at,1,radius) && r::profile_record(prof,at+.002,-1,radius));
            }
            assert(prof.valid() && prof.measured()==r::profile_stations);
            assert(std::abs(r::profile_radius(prof,r::profile_station(6,prof.half))-.025)<1e-6 && std::abs(r::profile_radius(prof,-.19)-.0693)<1e-6);
            const double between=r::profile_radius(prof,(r::profile_station(4,prof.half)+r::profile_station(5,prof.half))*.5);
            assert(between>.04 && between<.055); // linear across the shoulder
            // hits off a station's plane, an implausible radius and a hit from no side are not taken
            r::RadiusProfile fresh;fresh.half=.2065;
            assert(!r::profile_record(fresh,r::profile_station(3,fresh.half)+.03,1,.05) && !r::profile_record(fresh,0.,1,.0001) && !r::profile_record(fresh,0.,0,.05) && !r::profile_record(fresh,0.,1,2.));
            for(unsigned i=0;i<3;++i) assert(r::profile_record(fresh,r::profile_station(i,fresh.half),1,.03));
            assert(!fresh.valid()); // three stations of eight are not a profile
            // the round cone's distance against the swept spheres it is the hull of
            {
                const double hh=.03,r1=.04,r2=.012;unsigned checked=0;
                for(unsigned k=0;k<400;++k) {
                    const Vec3 q{-.12+.24*std::fmod(k*.6180339,1.),-.1+.2*std::fmod(k*.7548777,1.),-.1+.2*std::fmod(k*.5698403,1.)};
                    double brute=1e9;for(unsigned i=0;i<=4000;++i) {const double t=i/4000.;brute=std::min(brute,d2vr::length(q-Vec3{-hh+2*hh*t,0,0})-(r1+(r2-r1)*t));}
                    if(brute<.002) continue;
                    ++checked;assert(std::abs(r::round_cone_distance(q,hh,r1,r2)-brute)<3e-4);
                }
                assert(checked>250);
                assert(r::round_cone_distance({0,0,0},hh,r1,r2)<0 && r::round_cone_distance({-hh-r1,0,0},hh,r1,r2)<1e-9 && r::round_cone_distance({hh+r2,0,0},hh,r1,r2)<1e-9);
                assert(std::abs(r::round_cone_distance({.3,0,0},.02,.01,.01)-(.3-.02-.01))<1e-9); // equal ends: the capsule
            }
            const d2vr::NativeGrabBounds box{{-.0693,-.0693,0},{.0693,.0693,.413}};
            auto cyl=r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},box);
            assert(cyl.axis==2 && std::abs(cyl.half-.2065)<1e-9 && std::abs(cyl.radius-.0693)<1e-9);
            assert(!r::held_profiled(cyl));
            cyl.profile=prof;assert(r::held_profiled(cyl));
            const auto in=r::palm_normal(h.palm,g);
            const auto posed_over=[&](const r::HeldSeat& seat,const r::FingerCurls& c,int f) { // least clearance of a finger's skin against every shape of the seat
                double least=1e9;for(unsigned k=0;k<seat.count;++k) least=std::min(least,posed_clearance(h,c,seat.shapes[k],f));return least;};
            { // a grip at the base: the piece that reaches the base is flat-ended (no round cap past it), the interior ones stay capsules
                const auto at_base=r::seat_held(cyl,h.palm,in,{0,0,.2065},r::held_profile_along_min);
                assert(at_base.count>=2 && at_base.shapes[0].rim>=0 && at_base.shapes[0].radius_end<0 && at_base.shapes[at_base.count-1].rim<0);
                r::require_shape(at_base.shapes[0]);
                const auto bottom=at_base.shapes[0].frame.position-at_base.shapes[0].frame.basis[0]*(at_base.shapes[0].half.x+.01);   // 1 cm past the base, on the axis
                assert(std::abs(r::shape_distance(at_base.shapes[0],bottom)-.01)<1e-6);   // (a capsule's cap would put that point well inside)
            }
            for(const double along:{.3,.85}) {
                const auto seat=r::seat_held(cyl,h.palm,in,{0,0,.2065},along);
                const double radius=along<.5?.0693:.025;
                const auto skin=h.palm.position+in*r::held_palm_depth;
                double nearest=1e9;for(unsigned k=0;k<seat.count;++k) {r::require_shape(seat.shapes[k]);nearest=std::min(nearest,std::abs(r::shape_distance(seat.shapes[k],skin)));}
                assert(seat.count>=1 && seat.count<=r::max_held_shapes && nearest<1e-6); // the palm skin lies on the surface of the local profile
                // the shapes are the profile within a hand's width of the grip point, and nothing fatter than the body
                for(unsigned k=0;k<seat.count;++k) assert(seat.shapes[k].radius<=.0693+1e-6 && seat.shapes[k].radius_end<=.0693+1e-6 && seat.shapes[k].half.x<=r::held_profile_span+1e-9);
                const double grip=(along-.5)*2*cyl.half;
                (void)radius;(void)grip;
                r::FingerIkState st{};const auto wants=r::finger_wants(true,1.f,0.f);
                const auto c=r::finger_contact(g,h.palm,seat.shapes.data(),seat.count,0,st,r::held_curl,r::finger_solve_all,&wants);
                unsigned touching=0;double least=1e9;
                for(unsigned f=1;f<5;++f) {touching+=c.touching[f]?1u:0u;least=std::min(least,posed_over(seat,c,static_cast<int>(f)));}
                if(report) std::cerr<<(right?"right":"left")<<" profiled bottle held at "<<along<<": "<<seat.count<<" shapes, radius "<<seat.shapes[0].radius*100<<" cm, "<<touching<<" fingers touching, least clearance "<<least*1000<<" mm"<<std::endl;
                assert(least>-5e-3);
                if(along>.5) { // the neck: a thin shaft, every finger wraps it with every joint flexed
                    assert(touching==4);
                    const auto turn=r::finger_turns(g,c,1.f,0.f,true);
                    for(unsigned f=1;f<5;++f) {const auto a=final_angles(h,f,turn[f]);for(unsigned s=0;s<3;++s) assert(a[s]>12*deg);}
                } else assert(touching>=3); // the body: the hand lies along the curve, fingers resting on it, none through it
                // where the palm touched is kept up to the ends of a profiled shaft (the cylinder alone clamps it to 0.45)
                const auto world_axes=seat.axes;const auto middle=seat.origin+seat.axes[2]*.2065;
                const auto touch=r::held_touch(cyl,world_axes,middle,h.palm,in);
                auto plain=cyl;plain.profile={};const auto flat=r::held_touch(plain,world_axes,middle,h.palm,in);
                assert(touch.touched && std::abs(touch.along-along)<.02);
                { // the same grip taken from 30 cm off (a hand reaching for the neck): the aim finds it, the plain touch does not
                    const auto sq=world_axes[0]; // square to the shaft
                    auto far=h.palm;far.position=far.position-sq*.3;
                    const auto aimed=r::held_touch(cyl,world_axes,middle,far,in,r::held_aim_reach);
                    assert(!aimed.touched && std::abs(aimed.along-along)<.02 && r::held_touch(cyl,world_axes,middle,far,in).along==r::held_grip_fraction);
                }
                if(along>.5) assert(flat.along<=r::held_along_max+1e-12);
            }
            // The touch follows the press: read at the controller grip where the squeeze began, the grip point is where the hand was (live v525: read at
            // the drawn palm, 38 cm up the arm's shortfall, every grip came out 0.92 with a gap of 30 cm, whatever was pressed).
            {
                const Vec3 pr_centre{0,0,.2065};const std::array<Vec3,3> pr_world{{{1,0,0},{0,1,0},{0,0,1}}};
                const auto pr_palm=[&](double fraction,double lift) { // a palm whose skin lies on the shaft at that fraction, `lift` further up the shaft
                    auto p=h.palm;const double s=(fraction-.5)*2*cyl.half;
                    const Vec3 skin=pr_centre+Vec3{std::max(r::held_radius_floor,r::profile_radius(cyl.profile,s)),0,s+lift};
                    p.position=skin-in*r::held_palm_depth;return p;};
                const std::uint64_t pr_t0=10000;unsigned pr_checked=0;
                for(const double fraction:{.22,.78,.70,.59}) {
                    r::GripTrace<8> pr_trace;
                    for(unsigned i=0;i<6;++i) pr_trace.note(pr_t0+i*10,pr_palm(fraction,0),0.f);                      // the hand arrives and waits
                    pr_trace.note(pr_t0+60,pr_palm(fraction,0),.1f);pr_trace.note(pr_t0+70,pr_palm(fraction,0),.6f);  // then squeezes
                    const auto pr_press=pr_trace.press(pr_t0+100,1500);assert(pr_press && pr_press->tick==pr_t0+60);
                    const auto pr_raw=r::held_touch(cyl,pr_world,pr_centre,pr_press->grip,in,r::held_aim_reach);
                    assert(pr_raw.touched && std::abs(pr_raw.along-fraction)<.02);
                    const auto pr_drawn=r::held_touch(cyl,pr_world,pr_centre,pr_palm(fraction,.38),in,r::held_aim_reach); // the arm's shortfall: 38 cm up the shaft
                    assert(std::abs(pr_drawn.along-r::held_profile_along_max)<1e-9);                                    // the live symptom: pinned at .92
                    ++pr_checked;
                }
                assert(pr_checked==4);
                // The trace itself: the newest sample from before the squeeze, nothing when the hand has been closed (or silent) for longer than the window.
                r::GripTrace<4> pr_ring;
                assert(!pr_ring.press(1000,1500));
                pr_ring.note(100,pr_palm(.2,0),0.f);pr_ring.note(200,pr_palm(.3,0),0.f);pr_ring.note(300,pr_palm(.4,0),.2f);pr_ring.note(400,pr_palm(.5,0),.5f);
                assert(pr_ring.press(500,1500) && pr_ring.press(500,1500)->tick==300);
                pr_ring.note(500,pr_palm(.6,0),1.f);pr_ring.note(600,pr_palm(.7,0),1.f);                              // the ring wraps: 300..600 left
                assert(pr_ring.press(700,1500)->tick==300);
                pr_ring.note(700,pr_palm(.8,0),1.f);assert(!pr_ring.press(800,1500));                                  // 300 dropped out: all squeezed
                // The prop the probes saw before the pickup against the one held: the same mesh under another entity (the tumbler), a neighbour of another size
                // near the press, a table, a prop out of reach, a prop seen long ago, an unsolved box.
                {
                    const std::array<double,3> pm_glass{.053,.0614,.0614},pm_same{.0529,.0614,.0615},pm_other{.07,.09,.1},pm_table{.02,.4,.6};
                    assert(r::probe_match(pm_glass,pm_same,.3,16)==r::ProbeMatch::same);
                    assert(r::probe_match(pm_glass,pm_same,3.,1400)==r::ProbeMatch::same);          // the same mesh needs neither nearness nor freshness
                    assert(r::probe_match(pm_glass,pm_other,.3,16)==r::ProbeMatch::similar);
                    assert(r::probe_match(pm_glass,pm_other,.8,16)==r::ProbeMatch::none && r::probe_match(pm_glass,pm_other,.3,900)==r::ProbeMatch::none);
                    assert(r::probe_match(pm_glass,pm_table,.1,16)==r::ProbeMatch::none);
                    assert(r::probe_match(pm_glass,{0,0,0},.1,16)==r::ProbeMatch::none && r::probe_match({0,.1,.1},pm_glass,.1,16)==r::ProbeMatch::none);
                }
                r::GripTrace<4> pr_old;pr_old.note(100,pr_palm(.2,0),0.f);
                assert(pr_old.press(1500,1500) && !pr_old.press(1700,1500));                                          // 1.4 s old is in, 1.6 s old is out
            }
        }
            // A hand fits the grip. finger_gaps: how far each phalanx's skin floats off the shapes; fit_grip: where a wide shaft is seated so the fingers float
            // least (sim v532: a 4.5 cm shaft sat against the knuckles, the index 6 / 68 / 50 degrees and 7 and 14 mm off it; a tumbler left the thumb hanging).
            {
                const auto gf_in=r::palm_normal(h.palm,g);
                const auto gf_world=[&](Vec3 p) {return h.palm.position+h.palm.basis[0]*p.x+h.palm.basis[1]*p.y+h.palm.basis[2]*p.z;};
                const auto gf_far=[](Vec3) {return .1;};const auto gf_inside=[](Vec3) {return -.01;};
                const auto gf_open=r::finger_gaps(g.finger[1],g.thickness[1],{0,0,0},gf_world,gf_far);
                assert(std::abs(gf_open.sum-.09)<1e-9 && std::abs(gf_open.mean[2]-.03)<1e-9 && gf_open.penetration<=0); // far off: each phalanx floats at the 3 cm cap
                const auto gf_buried=r::finger_gaps(g.finger[1],g.thickness[1],{0,0,0},gf_world,gf_inside);
                assert(gf_buried.sum==0 && std::abs(gf_buried.penetration-(.01+g.thickness[1]))<1e-9);
                const std::array<Vec3,3> gf_axes{{{1,0,0},{0,1,0},{0,0,1}}};
                const auto gf_time=[&](const r::HeldCylinder& shaft,Vec3 centre,const char* what) {
                    const auto begin=std::chrono::steady_clock::now();
                    const auto fit=r::fit_grip(g,h.palm,shaft,gf_in,centre);
                    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
                    std::cout<<"fit_grip, "<<what<<": "<<us<<" us ("<<fit.tried<<" seats; shift "<<fit.shift*1000<<" mm, slide "<<fit.slide*1000<<" mm; floating "<<fit.plain*1000<<" -> "<<fit.fitted*1000<<" mm)"<<std::endl;
                    assert(us<30000);return fit;
                };
                const auto gf_wide=r::held_cylinder(gf_axes,{{-.045,-.045,0},{.045,.045,.25}});
                const auto wide_fit=gf_time(gf_wide,{0,0,.125},"shaft r=4.5 cm");
                assert(wide_fit.fitted<.5*wide_fit.plain && wide_fit.shift!=r::held_knuckle_shift && wide_fit.slide==0.);   // a long shaft is not slid (the touch decides where along it)
                const auto wide_again=r::fit_grip(g,h.palm,gf_wide,gf_in,{0,0,.125});
                assert(wide_again.shift==wide_fit.shift && wide_again.fitted==wide_fit.fitted);                              // deterministic
                const auto gf_thin=r::held_cylinder(gf_axes,{{-.017,-.017,0},{.017,.017,.2}});
                const auto thin_fit=gf_time(gf_thin,{0,0,.1},"handle r=1.7 cm");
                assert(thin_fit.tried==1 && thin_fit.shift==r::held_knuckle_shift && thin_fit.slide==0.);                    // every candidate lies past 0.8 radius: one seat, kept
                const r::HeldCylinder gf_tumbler{2,0.,.0614}; // a ball 12.3 cm across (the tumbler was one before it was told to be a cup)
                const auto ball_fit=gf_time(gf_tumbler,{0,0,0},"ball 12.3 cm");
                assert(ball_fit.fitted<.7*ball_fit.plain && std::abs(ball_fit.slide)>0);                                               // taken deeper and toward the thumb
                const auto gf_cup=r::held_cylinder(gf_axes,{{-.0614,-.0614,-.053},{.0614,.0614,.053}});                                   // the tumbler: a cup, a capsule as long as it is tall
                assert(std::abs(gf_cup.half-.053)<1e-9);
                const auto cup_fit=gf_time(gf_cup,{0,0,0},"tumbler 12.3 x 10.6 cm (a cup)");
                assert(cup_fit.fitted<=cup_fit.plain && cup_fit.slide==0.);                                                          // a cup is held by its wall: not slid
                {   // the fit runs a seat a step, none over 3 ms, and ends on the same answer
                    r::GripFitter stepper(g,h.palm,gf_tumbler,gf_in,{0,0,0});unsigned steps=0;double worst_us=0;
                    for(;;) {
                        const auto begin=std::chrono::steady_clock::now();const bool finished=stepper.step();++steps;
                        worst_us=std::max(worst_us,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count());
                        if(finished) break;
                    }
                    std::cout<<"fit_grip steps: "<<steps<<", the slowest "<<worst_us<<" us"<<std::endl;
                    assert(steps==ball_fit.tried+1 && stepper.done() && stepper.result().shift==ball_fit.shift && stepper.result().slide==ball_fit.slide && worst_us<3000);
                }
                // seat_held honours both: the slide moves the shaft along the hole axis, the shift turns it toward the finger bases
                const auto gf_plain=r::seat_held(gf_tumbler,h.palm,gf_in,{0,0,0}),gf_slid=r::seat_held(gf_tumbler,h.palm,gf_in,{0,0,0},r::held_grip_fraction,0,0,-1,.01);
                assert(std::abs(d2vr::dot(gf_slid.shape.frame.position-gf_plain.shape.frame.position,h.palm.basis[0])-.01)<1e-9);
                const auto gf_shifted=r::seat_held(gf_wide,h.palm,gf_in,{0,0,.125},r::held_grip_fraction,0,0,.02),gf_unshifted=r::seat_held(gf_wide,h.palm,gf_in,{0,0,.125},r::held_grip_fraction,0,0,.035);
                assert(d2vr::length(gf_shifted.shape.frame.position-gf_unshifted.shape.frame.position)>.005);
            }
        // The flat-ended cylinder (a cup, a can, a bottle's body: no round caps) and the seat that turns a prop least from the way it lay.
        {
            const auto st_in=r::palm_normal(h.palm,g);const auto st_n=r::seat_across(h.palm,st_in);
            // distance: flat ends, a rounded rim, the same in the palm's frame
            const r::HeldShape st_cyl{{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},{.05,0,0},.06,-1.,.008};
            r::require_shape(st_cyl);
            assert(std::abs(r::shape_distance(st_cyl,{.07,0,0})-.02)<1e-9 && std::abs(r::shape_distance(st_cyl,{0,.08,0})-.02)<1e-9 && std::abs(r::shape_distance(st_cyl,{0,0,-.08})-.02)<1e-9);
            assert(r::shape_distance(st_cyl,{0,0,0})<-.04 && std::abs(r::shape_distance(st_cyl,{.05,.0,0})-0.)<1e-9);
            assert(std::abs(r::shape_distance(st_cyl,{.06,.07,0})-(std::hypot(.06-.042,.07-.052)-.008))<1e-9);     // past the rim: the corner's own roundness
            assert(r::shape_distance({{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},{.05,0,0},.06},{.05+.03,0,0})<-.02);   // (a capsule's round cap reaches past the same point: that is the phantom the cylinder drops)
            const r::PalmShape st_palm{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}},{.05,0,0},.06,-1.,.008};
            for(const Vec3 p:{Vec3{.07,0,0},Vec3{.06,.07,0},Vec3{0,.03,.04},Vec3{-.02,.1,.1}}) assert(std::abs(r::palm_shape_distance(st_palm,p)-r::shape_distance(st_cyl,p))<1e-12);
            assert(rejects([&] {r::require_shape({{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},{.05,.01,0},.06,-1.,.008});}) && rejects([&] {r::require_shape({{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},{.05,0,0},.06,-1.,.07});}));
            // a seated cylinder prop is flat-ended; the palm lies on its wall, or, end-on, on its end face
            const r::HeldCylinder st_cup{2,.053,.0614};
            const auto st_wall=r::seat_held(st_cup,h.palm,st_in,{0,0,0});
            assert(st_wall.shape.rim>=0 && st_wall.count==1 && std::abs(r::shape_distance(st_wall.shape,h.palm.position+st_in*r::held_palm_depth))<1e-6);
            for(const unsigned mode:{2u,3u}) {
                const auto end_on=r::seat_held(st_cup,h.palm,st_in,{0,0,0},r::held_grip_fraction,0,0,-1,0,mode);
                assert(d2vr::length(end_on.axes[2]-st_n*(mode==2?1.:-1.))<1e-9 && d2vr::length(end_on.shape.frame.basis[0]-end_on.axes[2])<1e-9);
                assert(std::abs(r::shape_distance(end_on.shape,h.palm.position+st_in*r::held_palm_depth))<1e-6); // the palm lies on the end face
            }
            const auto reversed=r::seat_held(st_cup,h.palm,st_in,{0,0,0},r::held_grip_fraction,0,0,-1,0,1);
            assert(d2vr::length(reversed.axes[2]+h.palm.basis[0])<1e-9 && std::abs(r::shape_distance(reversed.shape,h.palm.position+st_in*r::held_palm_depth))<1e-6);
            assert(rejects([&] {(void)r::seat_held(st_cup,h.palm,st_in,{0,0,0},r::held_grip_fraction,0,0,-1,0,4);}));
            // the seat that turns the prop least: a cup standing up, taken by a hand in each orientation (its axes built from the hand's own)
            const auto st_rows=[&](Vec3 up) {const Vec3 a=d2vr::unit(d2vr::cross(up,std::abs(up.x)<.9?Vec3{1,0,0}:Vec3{0,1,0}));return std::array<Vec3,3>{a,d2vr::cross(up,a),up};};
            const auto st_touch=[&](const r::HeldCylinder& shaft,Vec3 up) {
                auto palm=h.palm;palm.position=st_n*shaft.radius-st_in*r::held_palm_depth;      // the skin lies on the prop's side at its middle (the prop's middle at the origin)
                return r::held_touch(shaft,st_rows(up),{0,0,0},palm,st_in);
            };
            const auto h0=h.palm.basis[0];const double deg_=3.14159265358979323846/180;
            const auto tilted=[&](double degrees) {return d2vr::unit(h0*std::cos(degrees*deg_)+st_n*std::sin(degrees*deg_));};
            struct Expect {Vec3 up;unsigned mode;double rotation_degrees;};
            for(const Expect e:{Expect{h0,0,0},Expect{h0*-1.,1,0},Expect{st_n,2,0},Expect{st_n*-1.,3,0},Expect{tilted(40),0,40},Expect{tilted(140),1,40},Expect{tilted(70),2,20},Expect{tilted(250),3,20}}) {
                const auto t=st_touch(st_cup,e.up);
                assert(t.touched && t.mode==e.mode && std::abs(t.rotation/deg_-e.rotation_degrees)<1.0);   // (the prop's roll about its axis is kept: the whole turn is the axis's)
            }
            const r::HeldCylinder st_bottle{2,.2,.035};                   // a long shaft is never taken end-on: it turns to the hand's axis, at most a quarter turn
            assert(st_touch(st_bottle,st_n).mode<2 && std::abs(st_touch(st_bottle,st_n).rotation/deg_-90)<1.0);
            // fitted on the end-on seat as on the wall's: the same machinery
            const auto st_fit=r::fit_grip(g,h.palm,st_cup,st_in,{0,0,0},r::held_grip_fraction,0,0,2);
            assert(st_fit.fitted<=st_fit.plain && st_fit.tried>=2);
        }
        // The memo and the turn-taking (a held object is the same shape in the palm's frame every frame; a frame's cost is bounded).
        {
            const d2vr::NativeGrabBounds box{{-.035,-.035,0},{.035,.035,.25}};
            const auto cyl=r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},box);
            const auto in=r::palm_normal(h.palm,g);
            const auto bottle=r::seat_held(cyl,h.palm,in,{0,0,.125}).shape;
            const auto moved=[&](const d2vr::WorldTrackedPose& pose,const r::HeldShape& shape,const d2vr::WorldTrackedPose& to) { // the shape that keeps its place in the pose's frame
                const auto local=[&](Vec3 v) {return Vec3{d2vr::dot(v,pose.basis[0]),d2vr::dot(v,pose.basis[1]),d2vr::dot(v,pose.basis[2])};};
                const auto world=[&](Vec3 l) {return to.basis[0]*l.x+to.basis[1]*l.y+to.basis[2]*l.z;};
                r::HeldShape out=shape;out.frame.position=to.position+world(local(shape.frame.position-pose.position));
                for(unsigned i=0;i<3;++i) out.frame.basis[i]=world(local(shape.frame.basis[i]));
                return out;
            };
            const auto widest=[&](const r::FingerCurls& a,const r::FingerCurls& b) {
                double w=0;for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) w=std::max(w,static_cast<double>(std::abs(a.pose[f][s]-b.pose[f][s])));return w;};
            r::FingerIkState st{};
            const auto first=r::finger_contact(g,h.palm,&bottle,0,st,r::held_curl);
            assert(st.solved>=3); // every finger that reaches it (the call's default budget is the whole hand)
            // The hand turns and travels with the bottle in it: nothing is solved, the answer is the same.
            const auto turn=r::axis_angle(d2vr::unit(Vec3{.3,.8,.5}),.7);
            d2vr::WorldTrackedPose to{h.palm.position+Vec3{2,-3,1.5},{d2vr::rotated(turn,h.palm.basis[0]),d2vr::rotated(turn,h.palm.basis[1]),d2vr::rotated(turn,h.palm.basis[2])}};
            const auto carried=moved(h.palm,bottle,to);
            const auto again=r::finger_contact(g,to,&carried,0,st,r::held_curl);
            assert(st.solved==0 && widest(first,again)<1e-6);
            for(unsigned f=0;f<5;++f) assert(first.touching[f]==again.touching[f]);
            // A shape a millimetre and a half off (inside the memo's tolerance) is the same shape: nothing is solved, and the old pose still lies on it.
            auto nudged=bottle;nudged.frame.position=nudged.frame.position+h.palm.basis[1]*.0015;
            const auto close=r::finger_contact(g,h.palm,&nudged,0,st,r::held_curl);
            r::FingerIkState fresh_state{};const auto fresh=r::finger_contact(g,h.palm,&nudged,0,fresh_state,r::held_curl);
            assert(st.solved==0 && widest(close,first)<1e-6 && long_clearance(h,close,nudged)>-7e-3 && long_clearance(h,close,nudged)<long_clearance(h,fresh,nudged)+4e-3); // the old pose still lies on the nudged shape (its skin inside the 4 mm contact slack and the memo's 2)
            // Four millimetres off is not: it is solved again, and agrees with a fresh solve.
            auto off=bottle;off.frame.position=off.frame.position+h.palm.basis[1]*.004;
            const auto solved_again=r::finger_contact(g,h.palm,&off,1.,st,r::held_curl); // (dt 1: the smoothing has run its course)
            r::FingerIkState off_state{};const auto off_fresh=r::finger_contact(g,h.palm,&off,0,off_state,r::held_curl);
            assert(st.solved>=3 && widest(solved_again,off_fresh)<1e-6);
            // Another reach, another hand: solved again.
            r::finger_contact(g,h.palm,&off,0,st,1.f);assert(st.solved>=3);
            auto other=g;other.finger[2].pivot=other.finger[2].pivot+Vec3{.001,0,0};
            r::finger_contact(other,h.palm,&off,0,st,1.f);assert(st.solved>=3);
            // Turns: at most `budget` fingers a call, and after enough calls the whole hand is what the unlimited call gives. The fingers that wait keep their pose.
            const std::array<r::HeldShape,2> set{bottle,r::slab_shape(h.palm.position+in*.05,in*-1.)};
            r::FingerIkState all{};const auto everything=r::finger_contact(g,h.palm,set.data(),2,0,all,r::held_curl);
            r::FingerIkState turns{};unsigned calls=0,total=0;r::FingerCurls last{};
            for(;calls<8 && (calls==0 || total<all.solved);++calls) {
                last=r::finger_contact(g,h.palm,set.data(),2,0,turns,r::held_curl,2);
                assert(turns.solved<=2);total+=turns.solved;
            }
            assert(total==all.solved && calls==(all.solved+1)/2 && widest(last,everything)<1e-6);
            // Drifted shapes refresh one at a time under a budget of two (a new situation may take both).
            auto drift=set;for(auto& s:drift) s.frame.position=s.frame.position+h.palm.basis[1]*.004;
            r::finger_contact(g,h.palm,drift.data(),2,0,turns,r::held_curl,2);
            assert(turns.solved==1);
        }
    }
    {
        const std::array<Vec3,2> recorded_centres{{{-.009255406586971882,-.000003122030371,1.663318157386436},
            {-.03810724317147708,.4168504459464545,1.1527134886821786}}};
        const d2vr::WorldTrackedPose palm{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const r::HeldCylinder cylinder{2,.08,.03};
        for(const auto& centre:recorded_centres) {
            assert(!r::held_mesh_centre_valid(centre,false) && r::held_mesh_centre_valid(centre,true));
            assert(r::held_native_mesh_centre_valid(centre,".?AVarkSeveredLimb@@"));
            assert(!r::held_native_mesh_centre_valid(centre,".?AVarkItem@@"));
            assert(!r::held_native_mesh_centre_valid(centre,"arkSeveredLimb"));
            const auto seat=r::seat_held(cylinder,palm,{0,1,0},centre);
            const auto mesh_centre=seat.origin+seat.axes[0]*centre.x+seat.axes[1]*centre.y+seat.axes[2]*centre.z;
            assert(d2vr::length(mesh_centre-seat.shape.frame.position)<1e-9);
            assert(d2vr::length(mesh_centre-palm.position)<.1);
        }
        assert(r::held_mesh_centre_valid({0,0,.499},false));
        assert(!r::held_mesh_centre_valid({0,0,.5},false));
        assert(r::held_mesh_centre_valid({0,0,2},true));
        assert(!r::held_mesh_centre_valid({0,0,2.001},true));
        for(const bool severed_limb:{false,true}) {
            assert(!r::held_mesh_centre_valid({std::nan(""),0,0},severed_limb));
            assert(!r::held_mesh_centre_valid({0,std::numeric_limits<double>::infinity(),0},severed_limb));
        }
    }
    // A sheet (paper, a note, a stamp) has an AABB of no thickness: it still has a shape, a thin slab the fingers stop on.
    {
        const std::array<Vec3,3> id{{{1,0,0},{0,1,0},{0,0,1}}};
        const d2vr::NativeGrabBounds sheet{{-.1,-.15,0},{.1,.15,0}};
        assert(d2vr::length(r::held_clamped_extents({.1,.15,0})-Vec3{.1,.15,.0025})<1e-12 && r::held_clamped_extents({.1,-.1,std::nan("")}).y==-.1 && std::isnan(r::held_clamped_extents({.1,-.1,std::nan("")}).z));
        const auto e=(sheet.maximum-sheet.minimum)*.5;
        assert(!rejects([&] {(void)r::held_cylinder(id,sheet);}));
        const auto local=r::held_local_extents(id,r::held_clamped_extents(e));
        assert(local && r::held_is_thin(*local) && !r::held_is_thin({.05,.05,.05}) && !r::held_is_thin({.1,.15,.02}));
        const auto slab=r::held_slab(id,{},*local);
        assert(slab.radius==0 && d2vr::length(slab.half-Vec3{.1,.15,.0025})<1e-12);
        for(float mirror:{-1.f,1.f}) {
            const auto h=synthetic_hand(mirror);
            auto seated=r::held_slab(id,{0,.1,-mirror*(.0025+.01)},*local);
            for(const float reach:{r::held_curl,1.f}) {
                r::FingerIkState st{};const std::array<float,5> wants{1.f,reach,reach,reach,reach};
                const auto c=r::finger_contact(h.geometry,h.palm,&seated,1,0.,st,reach,r::finger_solve_all,&wants);
                for(unsigned f=1;f<5;++f) assert(c.touching[f] && c.finger[f]<1.f);
                assert(long_clearance(h,c,seated)>=-r::contact_tolerance);
            }
        }
        // A physical grab of the same sheet: a box with its clamped thickness (no throw).
        const d2vr::WorldTrackedPose upright{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const auto h=synthetic_hand(1);
        r::HeldShape grabbed{};
        assert(!rejects([&] {grabbed=r::classify_held(upright,sheet,-1,h.palm);}));
        assert(grabbed.radius==0 && grabbed.half.z==.0025);
        // Turned 30 degrees about X: the engine's box of the sheet is still thin once solved back to local extents.
        const double c30=std::cos(30*deg),s30=std::sin(30*deg);
        const std::array<Vec3,3> turned{{{1,0,0},{0,c30,s30},{0,-s30,c30}}};
        const Vec3 aabb{.1,.15*c30+.0025*s30,.15*s30+.0025*c30};
        const auto tl=r::held_local_extents(turned,aabb);
        assert(tl && r::held_is_thin(*tl) && std::abs(tl->z-.0025)<1e-6);
        // Oversized bounds still reject.
        assert(rejects([&] {(void)r::held_cylinder(id,{{-4,-4,0},{4,4,0}});}));
        assert(rejects([&] {(void)r::classify_held(upright,d2vr::NativeGrabBounds{{-4,-4,0},{4,4,0}},-1,h.palm);}));
    }
    // G4 eased_opening_bounded_lag: a shape (the fitter's, after the hover shape) that opens the contact pose by 20 degrees or more: with ease_opening
    // the pose follows with finger_ik_tau, never lagging the target by more than max_open_lag_rad; without it, the opening is immediate (as above).
    for(float mirror:{-1.f,1.f}) {
        const auto h=synthetic_hand(mirror);const auto& g=h.geometry;
        const auto bottle=across_palm(h,mirror,.035,.01);const std::array<float,5> wants{1,1,1,1,1};
        r::FingerIkState free_state{};const auto shut=r::finger_contact(g,h.palm,nullptr,0,0,free_state,1.f,r::finger_solve_all,&wants);
        r::FingerIkState target_state{};const auto target=r::finger_contact(g,h.palm,&bottle,1,0,target_state,1.f,r::finger_solve_all,&wants);
        double opened=0;for(unsigned f=1;f<5;++f) for(unsigned s=0;s<3;++s) opened=std::max(opened,static_cast<double>(shut.pose[f][s]-target.pose[f][s]));
        assert(opened>=20*deg);
        constexpr double lag=.1,dt=1/72.;
        r::FingerIkState st=free_state;bool eased=false;
        for(unsigned i=0;i<72;++i) {
            const auto c=r::finger_contact(g,h.palm,&bottle,1,dt,st,1.f,r::finger_solve_all,&wants,true,lag);
            for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {
                assert(c.pose[f][s]>=target.pose[f][s]-1e-6f && c.pose[f][s]<=target.pose[f][s]+lag+1e-6);
                eased=eased || c.pose[f][s]>target.pose[f][s]+1e-4f;
            }
            if(i==71) for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) assert(std::abs(c.pose[f][s]-target.pose[f][s])<1e-3f);
        }
        assert(eased);
        r::FingerIkState immediate=free_state;const auto now=r::finger_contact(g,h.palm,&bottle,1,dt,immediate,1.f,r::finger_solve_all,&wants);
        assert(now.pose==target.pose);
    }
    // G4 memo_stable_under_reach_blend: the reach blends 1 -> held_curl over the grip ease; a step from 0.81 to 0.80 keys the same memo (0.05 steps).
    {
        const auto h=synthetic_hand(1.f);const auto& g=h.geometry;
        const auto bottle=across_palm(h,1.f,.035,.01);const std::array<float,5> wants{1,.81f,.81f,.81f,.81f};
        r::FingerIkState st{};(void)r::finger_contact(g,h.palm,&bottle,1,1/72.,st,.81f,r::finger_solve_all,&wants);
        assert(st.solved==5);
        (void)r::finger_contact(g,h.palm,&bottle,1,1/72.,st,.80f,r::finger_solve_all,&wants);
        assert(st.solved==0);
        (void)r::finger_contact(g,h.palm,&bottle,1,1/72.,st,.7f,r::finger_solve_all,&wants);
        assert(st.solved==5); // a real change of reach still solves again
        // The blend's split of finger_turns: wanted angles (floors included), then the contact clamp: the same turns as finger_turns.
        r::FingerIkState s2{};const auto c=r::finger_contact(g,h.palm,&bottle,1,0,s2,1.f,r::finger_solve_all,&wants);
        for(const auto& w:{wants,std::array<float,5>{0,0,0,0,0},std::array<float,5>{1,1,1,1,1}}) {
            const auto a=r::finger_turns(g,c,w),b=r::finger_turns_of_angles(g,c,r::finger_want_angles(c,w));
            for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) assert(std::abs(a[f][s]-b[f][s])<1e-5f);
        }
        // the lag bound: every joint may lag at once, so a tip errs by at most the tolerance (the sum of each joint's lever to the tip)
        double lever=0;for(unsigned f=1;f<5;++f) {double sum=0;for(unsigned s=0;s<3;++s) {double l=g.thickness[f];for(unsigned k=s;k<3;++k) l+=d2vr::length(g.finger[f].bone[k]);sum+=l;}lever=std::max(lever,sum);}
        assert(std::abs(r::finger_open_lag(g,.005)-.005/lever)<1e-12 && rejects([&] {(void)r::finger_open_lag(g,-1);}));
        {   const double lag=r::finger_open_lag(g,.005);
            for(unsigned f=1;f<5;++f) {
                const std::array<double,3> base{.3,.4,.3},lagged{.3+lag,.4+lag,.3+lag};
                const auto a=r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],base))[3],b=r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],lagged))[3];
                assert(d2vr::length(a-b)<=.005+1e-6);
            }
        }
        assert(rejects([&] {r::FingerIkState s3{};(void)r::finger_contact(g,h.palm,&bottle,1,0,s3,1.f,r::finger_solve_all,&wants,true,-1);}));
    }
    // G5 (plan Task 11): the solver stops an extended finger. A flat hand pressed on a table stays flat: an extended finger already inside a shape
    // first tries more closed along its path, then opens its middle and tip joints up to finger_hyperextension_degrees (15), and stops there.
    assert(r::finger_hyperextension_degrees==15.);
    for(float mirror:{-1.f,1.f}) {
        const auto h=synthetic_hand(mirror);const auto& g=h.geometry;const Vec3 n=d2vr::unit(r::palm_normal(h.palm,g));
        const auto drawn=[&](const r::FingerCurls& c,const std::array<float,5>& wants) {return r::finger_turns(g,c,wants);};
        const auto points=[&](unsigned f,const std::array<float,3>& turn) {
            std::array<Vec3,4> out{};const auto p=r::finger_points(g.finger[f],{turn[0],turn[1],turn[2]});for(unsigned k=0;k<4;++k) out[k]=world_of(h,p[k]);return out;};
        const auto tip_skin=[&](const r::HeldShape& s,unsigned f,const std::array<float,3>& turn) {return r::shape_distance(s,points(f,turn)[3])-g.thickness[f];};
        const auto straight_tip=world_of(h,r::finger_points(g.finger[2],{0,0,0})[3]),knuckle=world_of(h,g.finger[2].pivot);
        // A table tilted so the knuckles' skin is 2 mm clear (the palm a centimetre above it) and the straight fingertips' skin `in` metres into it.
        const auto tilted=[&](double in) {
            const Vec3 a=knuckle+n*(g.thickness[2]+.002),b=straight_tip+n*(g.thickness[2]-in);
            const Vec3 across=d2vr::unit(d2vr::cross(n,b-a));Vec3 m=d2vr::unit(d2vr::cross(across,b-a));if(d2vr::dot(m,n)>0) m=m*-1.;
            return r::slab_shape(a,m,.2);
        };
        const std::array<float,5> open{0,0,0,0,0};
        {   // flat_hand_on_plane_stays_flat: tips 4 mm in: every middle and tip joint in [-15, +5] degrees, every fingertip >= -1 mm, hyperextended
            const auto table=tilted(.004);
            for(unsigned f=1;f<5;++f) assert(tip_skin(table,f,{0,0,0})<-.0025); // the straight hand is in (the thinner ring and little fingers a little less)
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&table,1,0,st,1.f,r::finger_solve_all,&open,false,0,true);
            const auto t=drawn(c,open);
            for(unsigned f=1;f<5;++f) {
                assert(c.hyperextended[f] && !c.blocked[f] && c.touching[f] && c.stuck_frames[f]==0);
                for(unsigned s=1;s<3;++s) {const double angle=final_angles(h,f,t[f])[s];assert(angle>=-15*deg-1e-6 && angle<=5*deg);}
                assert(tip_skin(table,f,t[f])>=-.001);
            }
            // without the stop (the default, a held hand's wrap): today's behaviour, no flags
            r::FingerIkState old{};const auto o=r::finger_contact(g,h.palm,&table,1,0,old,1.f,r::finger_solve_all,&open);
            for(unsigned f=0;f<5;++f) assert(!o.hyperextended[f] && !o.blocked[f]);
        }
        {   // pressed_deeper_stops_at_limit: tips 3 cm in: the middle and tip joints at -15 degrees, blocked, stuck_frames counting up
            const auto table=tilted(.03);
            r::FingerIkState st{};
            for(unsigned frame=1;frame<=3;++frame) {
                const auto c=r::finger_contact(g,h.palm,&table,1,1/72.,st,1.f,r::finger_solve_all,&open,false,0,true);const auto t=drawn(c,open);
                for(unsigned f=1;f<5;++f) {
                    assert(c.blocked[f] && c.hyperextended[f] && c.stuck_frames[f]==frame);
                    for(unsigned s=1;s<3;++s) assert(std::abs(final_angles(h,f,t[f])[s]+15*deg)<1e-4);
                }
            }
            // lifted clear: no longer blocked, the counter resets
            const auto far=r::slab_shape(h.palm.position+n*.3,n*-1.);
            const auto c=r::finger_contact(g,h.palm,&far,1,1/72.,st,1.f,r::finger_solve_all,&open,false,0,true);
            for(unsigned f=1;f<5;++f) assert(!c.blocked[f] && !c.hyperextended[f] && c.stuck_frames[f]==0);
        }
        {   // finger_sliding_over_edge_stays_outside: a table 3 mm into the straight fingers' skin, its edge moving from past the tips to the knuckles
            // over 30 frames: no fingertip below -2 mm on any frame.
            const Vec3 along=d2vr::unit(straight_tip-knuckle),across=d2vr::unit(d2vr::cross(along,n));
            const double top=d2vr::dot(knuckle-h.palm.position,n)+g.thickness[2]-.003; // the table's face, along n from the palm
            r::FingerIkState st{};
            for(unsigned i=0;i<=30;++i) {
                const double edge=d2vr::dot(straight_tip-h.palm.position,along)+.02-i*(d2vr::length(straight_tip-knuckle)+.02)/30.;
                r::HeldShape box{};box.frame.position=h.palm.position+along*(edge+.15)+n*(top+.02);box.frame.basis={across,along,n};box.half={.12,.15,.02};
                const auto c=r::finger_contact(g,h.palm,&box,1,1/72.,st,1.f,r::finger_solve_all,&open,false,0,true);const auto t=drawn(c,open);
                for(unsigned f=1;f<5;++f) assert(tip_skin(box,f,t[f])>=-.002);
            }
        }
        {   // thin_slab_stops_fingers: a 4 mm sheet (held_slab) across the fingers' path: closing fingers stop on it, none passes through it
            const std::array<Vec3,3> axes{d2vr::unit(d2vr::cross(d2vr::unit(straight_tip-knuckle),n)),d2vr::unit(straight_tip-knuckle),n};
            const auto sheet=r::held_slab(axes,knuckle+n*.03,{.12,.1,.002});
            const std::array<float,5> fist{1,1,1,1,1};
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&sheet,1,0,st,1.f,r::finger_solve_all,&fist,false,0,true);const auto t=drawn(c,fist);
            for(unsigned f=1;f<5;++f) {
                assert(c.touching[f]);
                const auto p=points(f,t[f]);
                for(const auto& q:p) assert(d2vr::dot(q-(knuckle+n*.03),n)<-.001); // no joint and no tip into the sheet past a millimetre (it stops on its near face)
            }
            // pressed into the same sheet (lying 3 mm into the straight fingers' skin): the tips stay out of it
            const auto low=r::held_slab(axes,straight_tip+n*(g.thickness[2]-.003+.002),{.12,.03,.002});
            r::FingerIkState s2{};const auto d=r::finger_contact(g,h.palm,&low,1,0,s2,1.f,r::finger_solve_all,&open,false,0,true);const auto u=drawn(d,open);
            for(unsigned f=1;f<5;++f) assert(tip_skin(low,f,u[f])>=-.001);
        }
        {   // bend_preferred_when_clear: a wall ahead of the fingertips (their skin 3 mm into it at the relaxed curl) with the palm clear: the finger
            // closes on its path until it is out, never hyperextends
            const std::array<float,3> relaxed_turn{static_cast<float>(r::finger_angles(2,r::relaxed_share)[0]),static_cast<float>(r::finger_angles(2,r::relaxed_share)[1]),static_cast<float>(r::finger_angles(2,r::relaxed_share)[2])};
            const auto relaxed_tip=points(2,relaxed_turn)[3];const Vec3 along=d2vr::unit(straight_tip-knuckle);
            const auto wall=r::slab_shape(relaxed_tip+along*(g.thickness[2]-.003),along*-1.,.2);
            for(unsigned f=1;f<5;++f) assert(tip_skin(wall,f,relaxed_turn)<-.0015);
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&wall,1,0,st,1.f,r::finger_solve_all,&open,false,0,true);const auto t=drawn(c,open);
            for(unsigned f=1;f<5;++f) {
                assert(!c.hyperextended[f] && !c.blocked[f] && c.touching[f] && c.min_curl[f]>r::relaxed_share);
                for(unsigned s=0;s<3;++s) assert(final_angles(h,f,t[f])[s]>=-1e-6);
                assert(tip_skin(wall,f,t[f])>=-.001);
            }
            // the scalar consumers see it too: the least curl the finger takes
            const auto scalar=r::grip_curls(c,open);
            for(unsigned f=1;f<5;++f) assert(scalar[f]>=std::min(c.finger[f],c.min_curl[f])-1e-6f);
        }
    }
    // G5 thumb_closes_off_table: the player's rig over a table 2 cm under the palm point: the thumb (on the palm side, no hyperextension) closes across
    // the palm until it is out of the table; without the stop it lies in it.
    for(bool right:{false,true}) {
        const auto h=real_hand(right);const auto& g=h.geometry;const Vec3 n=d2vr::unit(r::palm_normal(h.palm,g));
        const auto table=r::slab_shape(h.palm.position+n*.02,n*-1.);const std::array<float,5> open{0,0,0,0,0};
        const auto thumb_tip=[&](const r::FingerCurls& c) {const auto t=r::finger_turns(g,c,open);
            return r::shape_distance(table,world_of(h,r::finger_points(g.finger[0],{t[0][0],t[0][1],t[0][2]})[3]))-g.thickness[0];};
        r::FingerIkState a{},b{};
        const auto stopped=r::finger_contact(g,h.palm,&table,1,0,a,1.f,r::finger_solve_all,&open,false,0,true);
        const auto old=r::finger_contact(g,h.palm,&table,1,0,b,1.f,r::finger_solve_all,&open);
        assert(thumb_tip(old)<-.005 && thumb_tip(stopped)>=-.001 && stopped.min_curl[0]>0 && stopped.touching[0] && !stopped.hyperextended[0]);
        assert(r::grip_curls(stopped,open)[0]>=std::min(stopped.min_curl[0],stopped.finger[0])-1e-6f);
    }
    // G5 (plan Task 12) current_pose_sweep_reaches_past_tip: the fingertip probe follows the finger's CURRENT pose: a flat hand's sweep runs from its
    // knuckle along the finger and ends 2 cm past the tip; a curled finger's along its curl; the free hand's palm cast reaches 8 cm.
    for(float mirror:{-1.f,1.f}) {
        const auto h=synthetic_hand(mirror);const auto& g=h.geometry;
        for(unsigned f=0;f<5;++f) {
            const std::array<float,3> flat{0,0,0};
            const auto sw=r::finger_sweep(g,h.palm,f,flat);
            const auto p=r::finger_points(g.finger[f],{0,0,0});
            const Vec3 knuckle=world_of(h,p[0]),tip=world_of(h,p[3]),dir=d2vr::unit(tip-world_of(h,p[2]));
            assert(d2vr::length(sw.from-knuckle)<1e-9 && d2vr::length(sw.to-(tip+dir*.02))<1e-9);
            assert(d2vr::dot(d2vr::unit(sw.to-sw.from),d2vr::unit(tip-knuckle))>.999); // along the finger, not toward the palm
            const auto old=r::finger_sweep(g,h.palm,f);assert(f==0 || d2vr::length(old.to-sw.to)>.01); // not the fixed curl-0.5 probe
            const std::array<float,3> curled{static_cast<float>(r::finger_angles(f,.6)[0]),static_cast<float>(r::finger_angles(f,.6)[1]),static_cast<float>(r::finger_angles(f,.6)[2])};
            const auto cs=r::finger_sweep(g,h.palm,f,curled,.03);const auto cp=r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],{curled[0],curled[1],curled[2]}));
            assert(d2vr::length(cs.to-(world_of(h,cp[3])+d2vr::unit(world_of(h,cp[3])-world_of(h,cp[2]))*.03))<1e-9);
            assert(rejects([&] {(void)r::finger_sweep(g,h.palm,f,flat,-.01);}) && rejects([&] {(void)r::finger_sweep(g,h.palm,5,flat);}));
        }
        const auto ps=r::palm_sweep(g,h.palm,r::free_palm_reach);
        assert(r::free_palm_reach==.08 && std::abs(d2vr::dot(ps.to-ps.from,d2vr::unit(r::palm_normal(h.palm,g)))-.09)<1e-9);
        assert(r::slab_shape({0,0,0},{0,0,1}).half.x==.12); // the slab stays 24 cm (ruling H10)
    }
    // G8: an empty fist rests on the palm (palm_stop_slab): the tips stop on the skin plane, an open hand is untouched, the knuckles lie outside it.
    for(bool right:{false,true}) {
        const auto h=real_hand(right);const auto& g=h.geometry;const auto n=d2vr::unit(r::palm_normal(h.palm,g));const Vec3 skin=h.palm.position+n*r::held_palm_depth;
        const auto slab=r::palm_stop_slab(g,h.palm);
        for(const auto& wants:{r::finger_wants(false,1.f,1.f),std::array<float,5>{1,1,1,1,1}}) {
            r::FingerIkState st{};const auto c=r::finger_contact(g,h.palm,&slab,1,0,st,1.f,r::finger_solve_all,&wants);
            for(unsigned f=0;f<5;++f) {
                const auto a=r::finger_pose_angles(f,wants[f],c.pose[f]);const auto tip=world_of(h,r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],a))[3]);
                const double above=d2vr::dot(tip-skin,n);
                assert(above>=0);
            }
            r::FingerIkState bare{};const auto free_fist=r::finger_contact(g,h.palm,nullptr,0,0,bare,1.f,r::finger_solve_all,&wants);double lowest=1e9; // control: with no slab a tip sinks below the skin
            for(unsigned f=1;f<5;++f) {const auto a=r::finger_pose_angles(f,wants[f],free_fist.pose[f]);lowest=std::min(lowest,d2vr::dot(world_of(h,r::finger_points(g.finger[f],r::finger_turns_of(g.finger[f],a))[3])-skin,n));}
            assert(lowest<0);
        }
        const std::array<float,5> open{};r::FingerIkState a{},b{};
        const auto with=r::finger_contact(g,h.palm,&slab,1,0,a,1.f,r::finger_solve_all,&open),without=r::finger_contact(g,h.palm,nullptr,0,0,b,1.f,r::finger_solve_all,&open);
        for(unsigned f=0;f<5;++f) {const auto x=r::finger_pose_angles(f,0.f,with.pose[f]),y=r::finger_pose_angles(f,0.f,without.pose[f]);for(unsigned s=0;s<3;++s) assert(std::abs(x[s]-y[s])<1e-6);} // an open hand asks no curl: the slab leaves its angles alone
        for(unsigned f=1;f<5;++f) assert(r::shape_distance(slab,world_of(h,g.finger[f].pivot))>0);
        assert(with.resting==0 && without.resting==0); // the stop under the palm is no surface: an empty hand takes no resting curl from it
    }
    {   // fast_hand_solve_cap: a hand at >= 0.3 m/s solves at most 3 fingers a frame; a slow hand still solves all 5
        assert(r::finger_free_budget(.3f,5)==3 && r::finger_free_budget(1.2f,5)==3 && r::finger_free_budget(.29f,5)==5 && r::finger_free_budget(0.f,5)==5);
        assert(r::finger_free_budget(1.f,1)==1); // never raises a smaller budget
    }
    std::cout<<"finger_ik_check passed"<<std::endl;
    return 0;
}
