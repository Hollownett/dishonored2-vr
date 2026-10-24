#pragma once
#include "finger_pose.hpp"
#include "native_rigid_body.hpp"
#include "radius_profile.hpp"
#include <cstring>
#include <optional>
#include <string_view>

// Finger contact IK for every object a hand touches: the held one, loose props, bodies and world surfaces, as one set of shapes.
// A finger is a three-joint chain that flexes about the hinge of each joint (finger_angles: the limits of a human hand and the way the
// joints couple) as ONE curl parameter closes it. Against shapes it wraps (finger_contact): the knuckle closes until the finger touches,
// then the middle joint (the distal one following it) until the middle phalanx does, then the distal joint on its own until the
// fingertip does, so a finger lies on what it holds with every joint flexed, instead of hooking by its tip or stopping straight; the
// thumb starts open, past its bind pose (finger_open_degrees: out and away from the palm), and closes across the palm as one, the joints
// beyond the phalanx that touched closing on. finger_turns poses each joint along its own curl path up to
// the angle the contact left it. The hinge axes come from the rig (hand_geometry) and are the ones BodySolver bends the bones about.
// Geometry lives in hand_bind_grip's palm frame (origin = interior palm centre, metres) and is built once per hand; a shape is a world
// rounded box.
namespace d2vr::rig {
// Joint limits of a human hand (degrees): MCP 0-90, PIP 0-110, DIP 0-80; the thumb's CMC (its opposition swing toward the fingertips)
// 0-70 (the metacarpal's abduction and opposition together), MCP 0-50, IP 0-70. Row = finger (thumb, index, middle, ring, pinky), column = chain joint (fingers 02-04, thumb 01-03).
inline constexpr std::array<std::array<double,3>,5> finger_limit_degrees{{{70,50,70},{90,110,80},{90,110,80},{90,110,80},{90,110,80}}};
// How far past its bind pose a joint may open (degrees, negative = away from the palm): the thumb's CMC swings out and away from the palm
// (palmar and radial abduction), its MCP and IP extend; a finger never opens past straight. A thumb the bind pose leaves inside a seat or
// under a slab opens this far and then closes around it. Row = finger, column = chain joint.
inline constexpr std::array<std::array<double,3>,5> finger_open_degrees{{{-45,-5,-15},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}};
inline constexpr double dip_per_pip=80./110; // the distal joint follows the middle one at about two thirds (it ends at its own 80 where the PIP ends at 110)
// procedural_curl's linear table (degrees at curl 1), kept for consumers that pose a finger by a share of it (hand_contact.hpp's proxies);
// the pose BodySolver draws is finger_angles'.
inline constexpr std::array<std::array<double,3>,5> finger_full_degrees{{{15,40,55},{75,100,70},{75,100,70},{75,100,70},{75,100,70}}};
// The joint angles (rad, from a straight finger) at one curl parameter in [0,1]: the proximal joint leads (the knuckle is well ahead of
// the middle joint at half closed), the middle joint follows, the distal joint takes dip_per_pip of the middle one. The thumb's three
// joints close together. Never beyond finger_limit_degrees. These are the paths each joint follows as the controller closes the hand;
// a contact only stops a joint short of them.
inline std::array<double,3> finger_angles(unsigned f,double curl) {
    constexpr double degree=3.14159265358979323846/180;
    if(f>=5 || !std::isfinite(curl)) throw std::invalid_argument("Invalid finger curl");
    const double c=std::clamp(curl,0.,1.);const auto& limit=finger_limit_degrees[f];
    if(f==0) return {limit[0]*degree*c,limit[1]*degree*c,limit[2]*degree*c};
    const double lag=std::pow(c,1.2);
    return {limit[0]*degree*(1-std::pow(1-c,1.25)),limit[1]*degree*lag,limit[2]*degree*lag};
}
// The curl at which a joint's path reaches `angle` (rad): finger_angles inverted (the least closed joint is a finger's curl).
inline double finger_curl_at(unsigned f,unsigned joint,double angle) {
    constexpr double degree=3.14159265358979323846/180;
    if(f>=5 || joint>=3 || !std::isfinite(angle)) throw std::invalid_argument("Invalid finger curl");
    const double share=std::clamp(angle/(finger_limit_degrees[f][joint]*degree),0.,1.);
    if(f==0) return share;
    return joint==0?1-std::pow(1-share,1/1.25):std::pow(share,1/1.2);
}
// The angles (rad) a finger opens to at most: finger_open_degrees.
inline std::array<double,3> finger_open_angles(unsigned f) {
    constexpr double degree=3.14159265358979323846/180;
    if(f>=5) throw std::invalid_argument("Invalid finger");
    return {finger_open_degrees[f][0]*degree,finger_open_degrees[f][1]*degree,finger_open_degrees[f][2]*degree};
}
// The angles of a finger the controller wants closed to `want` while the shapes leave it the contact pose `pose` (the angles it reaches
// at most, finger_contact): each joint follows its own path (finger_angles) until it reaches its contact angle.
inline std::array<double,3> finger_pose_angles(unsigned f,double want,const std::array<float,3>& pose) {
    auto a=finger_angles(f,want);
    for(unsigned s=0;s<3;++s) a[s]=std::min(a[s],static_cast<double>(pose[s]));
    return a;
}
struct FingerSegments {
    Vec3 pivot{};                    // chain[0] joint, palm frame
    std::array<Vec3,3> bone{};       // rest vectors chain[0]->[1]->[2]->tip
    std::array<Vec3,3> hinge{};      // unit hinge of each joint in the rest pose (palm frame); a positive turn closes the finger
    Vec3 axis{};                     // the middle joint's hinge: the one axis of a planar chain, for consumers that pose a finger by one (hand_contact.hpp)
    std::array<double,3> existing{}; // bend already in the rest pose per joint (rad)
    std::array<double,3> lowest{};   // the least turn over the rest pose a joint takes (rad, <= 0): the thumb opens past its bind pose
};
struct HandGeometry {
    std::array<FingerSegments,5> finger{}; // thumb, index, middle, ring, pinky
    Vec3 palm_in{};                        // unit, toward where a held object sits
    std::array<double,5> thickness{.011,.008,.008,.0075,.007}; // finger half-width (m): the contact clearance knob
};
// Rounded box in world space: the |half| box grown by radius. Sphere: half 0. Capsule: half {h,0,0} along basis[0]. Round cone (a capsule
// whose ends differ: the local frustum of a profiled shaft, finger_ik.hpp seat_held): a capsule with `radius_end` >= 0, the radius at
// +basis[0] (`radius` is the one at -basis[0]).
// rim >= 0: a flat-ended cylinder (a cup, a can, a bottle's body): radius `radius` about basis[0], half.x long, its rim rounded by `rim` (no round caps beyond its ends).
struct HeldShape {WorldTrackedPose frame{};Vec3 half{};double radius{};double radius_end{-1};double rim{-1};};
struct FingerCurls {
    std::array<float,5> finger{};                // the curl the shapes leave each finger, its least closed joint's (1 = unconstrained): the scalar consumers
    std::array<std::array<float,3>,5> pose{};    // joint angles (rad) the finger can close to against the shapes: its contact pose (0: none)
    std::array<bool,5> touching{};               // stopped by a shape before its full curl
    float resting{};                             // 0..resting_curl: the light curl of a slow hand lying on a surface
};
inline constexpr unsigned max_contact_shapes=8;
// A shape in the palm's own frame: a held object moves with the palm, so it is the same shape in this frame every frame the hand carries it.
struct PalmShape {Vec3 position{};std::array<Vec3,3> basis{};Vec3 half{};double radius{};double radius_end{-1};double rim{-1};};
// What one finger was last solved against (the shapes it reached, in the palm's frame) and what it answered: the same inputs, the same answer.
// The thumb also sees the folded index and middle fingers (two phalanges each) as shapes: `thumb_fold_shapes` more than the world's.
inline constexpr unsigned thumb_fold_shapes=4,max_memo_shapes=max_contact_shapes+thumb_fold_shapes;
struct FingerMemo {bool valid{};float reach{};unsigned count{};std::array<PalmShape,max_memo_shapes> shapes{};std::array<float,3> pose{};bool touching{};};
struct FingerIkState {
    std::array<std::array<float,3>,5> pose{};bool valid{};
    float resting{},speed{};Vec3 palm{};bool palm_valid{};
    std::array<FingerMemo,5> memo{};std::uint64_t hand_key{};unsigned rotor{};
    unsigned solved{};                       // fingers the last call solved (the rest took their memo or waited for their turn)
    unsigned thumb_waited{};                 // calls the thumb has waited for a turn behind the fingers
};
inline constexpr double finger_ik_tau=.06; // s: fingers settle onto a shape in ~3 tau; opening (a shape arriving) is immediate
// A hand lying on a surface (research recommendation 10): the palm skin within resting_gap_far of one and slower than resting_speed_fast
// raises the four fingers' wanted curl toward resting_curl (a contact still caps it). Full strength inside resting_gap_near / resting_speed_slow.
inline constexpr float resting_curl=.35f;
inline constexpr double resting_gap_near=.02,resting_gap_far=.03,resting_speed_slow=.15,resting_speed_fast=.3;
// HIGGS clamps a finger's openness to 0.2 "to not overcurl the finger": a hand holding something closes to curl 0.8 at most, whatever
// the grip says and whatever the contact leaves.
inline constexpr float held_min_openness=.2f,held_curl=1.f-held_min_openness;
// The thumb's hinges, a power grip's: its CMC swings the thumb up the wrist side of what the fingers hold, its MCP and IP then flex it
// over the top, so the pad lies on the far side. Each hinge is square to its bone and to a direction in the hand's own frame (palm normal,
// fingers, ulnar side): the CMC's toward raise * normal + forward * fingers + ulnar * ulnar, the MCP's and IP's toward
// flex_forward * fingers + flex_ulnar * ulnar + flex_raise * normal as those two stand with the CMC at its limit and the MCP half closed
// (the hinges are defined there and carried back to the rest pose). Tuned on the player's rig by a search over a bottle (3.5 and 4.5 cm
// radius), a handle, a table, a fist and a relaxed hand, both hands (tests/finger_ik_check.cpp states what they must do).
inline constexpr double thumb_raise=-.43,thumb_forward=.46,thumb_ulnar=.15;
inline constexpr double thumb_flex_forward=.93,thumb_flex_ulnar=.23,thumb_flex_raise=-.09;

inline void require_frame(const WorldTrackedPose& frame,const char* what,bool right_handed) {
    if(!finite(frame.position)) throw std::invalid_argument(what);
    for(unsigned i=0;i<3;++i) {
        if(!finite(frame.basis[i])) throw std::invalid_argument(what);
        for(unsigned j=0;j<=i;++j) if(std::abs(dot(frame.basis[i],frame.basis[j])-(i==j?1.:0.))>.01) throw std::invalid_argument(what);
    }
    if(right_handed && dot(cross(frame.basis[0],frame.basis[1]),frame.basis[2])<.99) throw std::invalid_argument(what);
}
inline void require_shape(const HeldShape& shape) {
    require_frame(shape.frame,"Invalid held shape frame",false);
    if(!finite(shape.half) || shape.half.x<0 || shape.half.y<0 || shape.half.z<0 || length(shape.half)>3 ||
       !std::isfinite(shape.radius) || shape.radius<0 || shape.radius>3 || length(shape.half)+shape.radius<=0 ||
       (shape.radius_end>=0 && (!std::isfinite(shape.radius_end) || shape.radius_end>3 || shape.half.y!=0 || shape.half.z!=0)) ||
       (shape.rim>=0 && (!std::isfinite(shape.rim) || shape.rim>shape.radius || shape.half.y!=0 || shape.half.z!=0 || shape.radius_end>=0 || !(shape.half.x>0))))
        throw std::invalid_argument("Invalid held shape");
}
inline void require_hand(const HandGeometry& hand) {
    if(!finite(hand.palm_in) || std::abs(length(hand.palm_in)-1)>.01) throw std::invalid_argument("Invalid finger geometry palm");
    for(unsigned f=0;f<5;++f) {
        const auto& g=hand.finger[f];
        if(!finite(g.pivot) || length(g.pivot)>.3 || !std::isfinite(hand.thickness[f]) || hand.thickness[f]<0 || hand.thickness[f]>.03)
            throw std::invalid_argument("Invalid finger geometry");
        for(unsigned s=0;s<3;++s) if(!finite(g.bone[s]) || length(g.bone[s])<1e-4 || length(g.bone[s])>.15 || !finite(g.hinge[s]) ||
            std::abs(length(g.hinge[s])-1)>.01 || !std::isfinite(g.existing[s]) || std::abs(g.existing[s])>3.2 || !std::isfinite(g.lowest[s]) || g.lowest[s]>0 || g.lowest[s]<-3.2) throw std::invalid_argument("Invalid finger segment");
    }
}
// Signed distance to a round cone: the hull of a sphere of radius r1 at -h and one of r2 at +h on the x axis (q in that frame). Exact (Quilez's
// sdRoundCone); when one sphere holds the other, the bigger one.
inline double round_cone_distance(Vec3 q,double h,double r1,double r2) {
    const double l=2*h,rr=r1-r2,l2=l*l,a2=l2-rr*rr;
    if(!(l>1e-9) || !(a2>1e-12)) return std::min(length(q-Vec3{-h,0,0})-r1,length(q-Vec3{h,0,0})-r2);
    const double il2=1/l2,y=(q.x+h)*l,z=y-l2;
    const Vec3 w{(q.x+h)*l2-l*y,q.y*l2,q.z*l2};
    const double x2=dot(w,w),y2=y*y*l2,z2=z*z*l2,k=(rr<0?-1.:1.)*rr*rr*x2;
    if((z<0?-1.:1.)*a2*z2>k) return std::sqrt(x2+z2)*il2-r2;
    if((y<0?-1.:1.)*a2*y2<k) return std::sqrt(x2+y2)*il2-r1;
    return (std::sqrt(x2*a2*il2)+y*rr)*il2-r1;
}
// Signed distance to a flat-ended cylinder (radius R about the x axis, half L long, its rim rounded by `rim`: no more than L or R), in the shape's frame.
inline double cylinder_distance(double axial,double radial,double half,double radius,double rim) {
    const double rho=std::min({rim,half,radius});
    const double qx=std::abs(axial)-(half-rho),qy=radial-(radius-rho);
    return std::hypot(std::max(qx,0.),std::max(qy,0.))+std::min(std::max(qx,qy),0.)-rho;
}
// Signed distance from a world point to the shape's surface (negative inside).
inline double shape_distance(const HeldShape& shape,Vec3 point) {
    const auto d=point-shape.frame.position;
    if(shape.rim>=0 && shape.half.x>0)
        return cylinder_distance(dot(d,shape.frame.basis[0]),std::hypot(dot(d,shape.frame.basis[1]),dot(d,shape.frame.basis[2])),shape.half.x,shape.radius,shape.rim);
    if(shape.radius_end>=0 && shape.radius_end!=shape.radius && shape.half.x>0)
        return round_cone_distance({dot(d,shape.frame.basis[0]),dot(d,shape.frame.basis[1]),dot(d,shape.frame.basis[2])},shape.half.x,shape.radius,shape.radius_end);
    const Vec3 q{std::abs(dot(d,shape.frame.basis[0]))-shape.half.x,std::abs(dot(d,shape.frame.basis[1]))-shape.half.y,
                 std::abs(dot(d,shape.frame.basis[2]))-shape.half.z};
    return length({std::max(q.x,0.),std::max(q.y,0.),std::max(q.z,0.)})+std::min(std::max({q.x,q.y,q.z}),0.)-shape.radius;
}

// Palm-frame chains from a hand palette (bind or current) and its grip frame. Each joint hinges about the axis perpendicular to its own
// bone and to the palm normal (the way a finger joint flexes), so a positive turn closes it toward the palm; the thumb's joints hinge
// perpendicular to their bone and the direction to the thumb target (the fingertips, lifted toward the palm: its opposition). The axes
// are expressed in the palm frame; hinge_axes rotates them by the grip's columns into the palette's space for BodySolver.
inline HandGeometry hand_geometry(const std::array<Transform,69>& joints,const ArmHierarchy& parents,const FingerChains& chains,
    HandJoints palm,unsigned wrist,const Transform& grip) {
    if(wrist>=69 || !valid_hierarchy(parents) || !valid(grip) || !valid(joints[wrist])) throw std::invalid_argument("Invalid finger geometry source");
    for(const auto& chain:chains) for(auto joint:chain) if(joint>=69 || !valid(joints[joint])) throw std::invalid_argument("Invalid finger geometry chain");
    const auto local_direction=[&](Vec3 v) {return Vec3{dot(v,column(grip,0)),dot(v,column(grip,1)),dot(v,column(grip,2))};};
    const auto at=[&](unsigned joint) {return local_direction(position(joints[joint])-position(grip));};
    const auto knuckle=[&](unsigned finger) {return at(chains[finger][0]);};
    const auto across=knuckle(1)-knuckle(4),along=(knuckle(1)+knuckle(4))*.5-at(wrist);
    Vec3 palm_in=palm.underwrist<69 && palm.upperwrist<69?at(palm.underwrist)-at(palm.upperwrist):cross(along,across);
    if(length(palm_in)<1e-6) throw std::invalid_argument("Degenerate finger geometry palm");
    palm_in=unit(palm_in);
    std::array<bool,69> chain_joint{};
    for(const auto& chain:chains) for(auto joint:chain) chain_joint[joint]=true;
    const auto next_joint=[&](unsigned joint) {
        unsigned best=69;double distance=-1;
        for(unsigned child=0;child<69;++child) if(parents[child]==static_cast<int>(joint)) {
            if(chain_joint[child]) return child;
            const auto d=length(position(joints[child])-position(joints[joint]));
            if(d>distance) {distance=d;best=child;}
        }
        return best;
    };
    HandGeometry hand{};hand.palm_in=palm_in;
    // The hand's own directions in the palm frame: the fingers' (wrist to knuckles, square to the palm normal) and the ulnar side's.
    const auto fingers_dir=unit(along-palm_in*dot(along,palm_in));
    const auto ulnar=unit(across*-1.-palm_in*dot(across*-1.,palm_in)-fingers_dir*dot(across*-1.,fingers_dir));
    constexpr double degree=3.14159265358979323846/180;
    const auto segments=[&](unsigned f) {
        const auto& chain=chains[f];
        FingerSegments g{};g.pivot=at(chain[0]);
        std::array<Vec3,3> d{};
        for(unsigned s=0;s<3;++s) {
            const auto joint=chain[s];const auto parent=parents[joint];const auto child=next_joint(joint);
            if(parent<0) throw std::invalid_argument("Finger joint has no parent");
            Vec3 outgoing=child<69?at(child)-at(joint):Vec3{};
            // No tip joint, or a sideways helper where the tip would be: extend the distal phalanx.
            if(s && (child>=69 || length(outgoing)<.4*length(g.bone[s-1]) || dot(unit(outgoing),unit(g.bone[s-1]))<.5)) outgoing=g.bone[s-1]*.8;
            if(length(outgoing)<1e-6) throw std::invalid_argument("Degenerate finger segment");
            g.bone[s]=outgoing;d[s]=unit(outgoing);
        }
        // Each hinge is square to its bone and to the way the joint closes it: a finger's toward the palm normal; the thumb's CMC toward its
        // raise/forward/ulnar direction, its MCP and IP toward the fingers as they stand with the CMC at its limit and the MCP half closed.
        const auto closing=[&](Vec3 bone,Vec3 toward) {
            const auto t=toward-bone*dot(toward,bone);
            if(length(t)<1e-6 || length(cross(bone,t))<1e-9) throw std::invalid_argument("Degenerate finger hinge");
            return unit(cross(bone,t));
        };
        if(f) for(unsigned s=0;s<3;++s) g.hinge[s]=closing(d[s],palm_in);
        else {
            g.hinge[0]=closing(d[0],palm_in*thumb_raise+fingers_dir*thumb_forward+ulnar*thumb_ulnar);
            const auto swing=axis_angle(g.hinge[0],finger_limit_degrees[0][0]*degree),unswing=axis_angle(g.hinge[0],-finger_limit_degrees[0][0]*degree);
            const auto flex_to=fingers_dir*thumb_flex_forward+ulnar*thumb_flex_ulnar+palm_in*thumb_flex_raise;
            const auto first=closing(rotated(swing,d[1]),flex_to);
            g.hinge[1]=rotated(unswing,first);
            const auto flex=axis_angle(first,finger_limit_degrees[0][1]*.5*degree),unflex=axis_angle(first,-finger_limit_degrees[0][1]*.5*degree);
            const auto second=closing(rotated(flex,rotated(swing,d[2])),flex_to);
            g.hinge[2]=rotated(unswing,rotated(unflex,second));
        }
        for(unsigned s=0;s<3;++s) {
            const auto parent=parents[chain[s]];
            auto in=at(chain[s])-at(static_cast<unsigned>(parent)),out=g.bone[s];
            in=in-g.hinge[s]*dot(in,g.hinge[s]);out=out-g.hinge[s]*dot(out,g.hinge[s]);
            // The thumb's wrist-to-CMC line is not a bone (procedural_curl): its turn is additive.
            g.existing[s]=(f || s) && length(in)>1e-6 && length(out)>1e-6?std::atan2(dot(cross(in,out),g.hinge[s]),dot(in,out)):0;
        }
        g.axis=g.hinge[1];
        if(f==0) for(unsigned s=0;s<3;++s) g.lowest[s]=std::min(0.,finger_open_degrees[0][s]*3.14159265358979323846/180-g.existing[s]);
        return g;
    };
    for(unsigned f=0;f<5;++f) hand.finger[f]=segments(f);
    require_hand(hand);
    return hand;
}
// The hinge axes in the space the palette was in when the geometry was built (the grip's columns turn palm-frame vectors back):
// BodySolver bends the finger bones about exactly these (Inputs::finger_axis), so the pose that is drawn is the pose that was tested.
inline std::array<std::array<Vec3,3>,5> hinge_axes(const HandGeometry& hand,const Transform& grip) {
    require_hand(hand);
    if(!valid(grip)) throw std::invalid_argument("Invalid finger grip frame");
    std::array<std::array<Vec3,3>,5> out{};
    for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {
        const auto& a=hand.finger[f].hinge[s];
        out[f][s]=unit(column(grip,0)*a.x+column(grip,1)*a.y+column(grip,2)*a.z);
    }
    return out;
}

// The finger's skin where it can touch, once its joints turn by `turns` (rad over the rest pose, each about its own hinge, the later
// hinges riding with the earlier joints): HIGGS's three recorded arcs per finger (finger_curves.cpp), here the points of the chain that
// sweep them as it closes: the pad side (inner, the way the bone moves), the back (outer) and the tip, `samples` along each bone. The
// least clearance of the three is the least-curled hit, so the first arc to touch stops the finger. A sample whose thickness-sphere is
// clear cannot have an arc point inside (the arcs lie on that sphere), so only near a shape does a sample cost more than one distance.
// arcs == 0: no arcs, the capsule itself (every sample's thickness sphere): the thumb, which closes across the palm out of its hinge plane.
// Negative = inside; `stop` returns at the first value below it (the solver only needs the sign); only bones from..to are tested (all
// bones move), from sample `first` on (the thumb's middle stage skips the part of a bone near its own joint, which a seat may bury).
inline constexpr unsigned arc_tip=1,arc_outer=2,arc_inner=4,arc_all=7;
template<class World,class Distance>
double finger_clearance(const FingerSegments& g,double thickness,const std::array<double,3>& turns,World&& world,Distance&& distance,
    unsigned arcs=arc_all,unsigned samples=3,double stop=-1e300,unsigned from=0,unsigned to=2,unsigned first=1) {
    auto bone=g.bone;auto axis=g.hinge;Vec3 pivot=g.pivot;double worst=1e9;
    for(unsigned s=0;s<3;++s) {
        const auto q=axis_angle(axis[s],turns[s]);
        for(unsigned k=s;k<3;++k) bone[k]=rotated(q,bone[k]);
        for(unsigned k=s+1;k<3;++k) axis[k]=rotated(q,axis[k]);
        const auto along=unit(bone[s]);const auto pad=cross(axis[s],along);
        const auto side=length(pad)>1e-6?pad*(thickness/length(pad)):Vec3{};
        for(unsigned i=first;i<=samples && s>=from && s<=to;++i) {
            const auto p=pivot+bone[s]*(static_cast<double>(i)/samples);
            const double d=distance(world(p));
            if(d>=thickness || !arcs) {worst=std::min(worst,d-thickness);if(worst<stop) return worst;continue;} // (no arcs: the whole thickness sphere)
            if(arcs&arc_inner) { // the pad, and the skin a bone's tilt turns toward its tip or its knuckle (a tilted pad meets a plane there first)
                static constexpr double lean[5][2]{{1,0},{.8660254,.5},{.8660254,-.5},{.5,.8660254},{.5,-.8660254}}; // cos, sin of 0, +-30, +-60 degrees
                for(const auto& l:lean) worst=std::min(worst,distance(world(p+side*l[0]+along*(thickness*l[1]))));
                if(worst<stop) return worst;
            }
            if(arcs&arc_outer) {worst=std::min(worst,distance(world(p-side)));if(worst<stop) return worst;}
            if((arcs&arc_tip) && s==2 && i==samples) {worst=std::min(worst,distance(world(p+along*thickness)));if(worst<stop) return worst;}
        }
        pivot=pivot+bone[s];
    }
    return worst;
}
// How far each phalanx's skin floats off the shapes, once the chain turns by `turns`: the skin clearance (the distance of the bone's centre line, less the
// finger's thickness) at four points along each phalanx, per phalanx the mean of what is outside (m; a point inside counts nothing) and the deepest
// penetration. A finger that hooks a wide curved surface by one point floats off it along the rest of its length; one that lies on it floats little.
struct FingerGaps {std::array<double,3> mean{};double sum{},penetration{};};
template<class World,class Distance>
FingerGaps finger_gaps(const FingerSegments& g,double thickness,const std::array<double,3>& turns,World&& world,Distance&& distance) {
    auto bone=g.bone;auto axis=g.hinge;Vec3 pivot=g.pivot;FingerGaps out{};
    constexpr unsigned samples=4;constexpr double cap=.03;
    for(unsigned s=0;s<3;++s) {
        const auto q=axis_angle(axis[s],turns[s]);
        for(unsigned k=s;k<3;++k) bone[k]=rotated(q,bone[k]);
        for(unsigned k=s+1;k<3;++k) axis[k]=rotated(q,axis[k]);
        double total=0;
        for(unsigned i=1;i<=samples;++i) {
            const double gap=distance(world(pivot+bone[s]*(static_cast<double>(i)/samples)))-thickness;
            total+=std::min(std::max(gap,0.),cap);out.penetration=std::max(out.penetration,-gap);
        }
        out.mean[s]=total/samples;out.sum+=out.mean[s];
        pivot=pivot+bone[s];
    }
    return out;
}
// Where the chain's joints lie (palm frame: knuckle, second joint, third joint, tip) once it turns by `turns`.
inline std::array<Vec3,4> finger_points(const FingerSegments& g,const std::array<double,3>& turns) {
    auto bone=g.bone;auto axis=g.hinge;std::array<Vec3,4> out{};out[0]=g.pivot;
    for(unsigned s=0;s<3;++s) {
        const auto q=axis_angle(axis[s],turns[s]);
        for(unsigned k=s;k<3;++k) bone[k]=rotated(q,bone[k]);
        for(unsigned k=s+1;k<3;++k) axis[k]=rotated(q,axis[k]);
        out[s+1]=out[s]+bone[s];
    }
    return out;
}
// The turns (rad over the rest pose, never below `lowest`: a finger is not bent back past its rest pose, a thumb opens by its limits) for joint angles.
inline std::array<double,3> finger_turns_of(const FingerSegments& g,const std::array<double,3>& angles) {
    return {std::max(g.lowest[0],angles[0]-g.existing[0]),std::max(g.lowest[1],angles[1]-g.existing[1]),std::max(g.lowest[2],angles[2]-g.existing[2])};
}
inline std::array<double,3> finger_turn_at(const FingerSegments& g,unsigned f,double curl) {
    return finger_turns_of(g,finger_angles(f,curl));
}

// The palm normal (toward where a held object sits) in world space.
inline Vec3 palm_normal(const WorldTrackedPose& palm,const HandGeometry& hand) {
    return palm.basis[0]*hand.palm_in.x+palm.basis[1]*hand.palm_in.y+palm.basis[2]*hand.palm_in.z;
}
// ponytail: +1 cm palm depth; hand_bind_grip's origin is an interior skeletal point, not the palm skin.
inline constexpr double held_palm_depth=.01;

// The angles a finger can close to at `reach` (the most the controller can ask): its path's, except that the thumb's MCP and IP always have
// their full range: its pad meets the far side of what it wraps, and a contact stops it.
inline std::array<double,3> finger_reach_angles(unsigned f,double reach) {
    auto a=finger_angles(f,reach);
    if(f==0) {const auto full=finger_angles(0,1.);a[1]=full[1];a[2]=full[2];}
    return a;
}
// What a hand with nothing in the way looks like: every finger free, each joint able to reach its path's end.
inline FingerCurls free_curls(float reach=1.f) {
    if(!std::isfinite(reach) || reach<0 || reach>1) throw std::invalid_argument("Invalid finger reach");
    FingerCurls c{};c.finger.fill(1);
    for(unsigned f=0;f<5;++f) {const auto a=finger_reach_angles(f,reach);for(unsigned s=0;s<3;++s) c.pose[f][s]=static_cast<float>(a[s]);}
    return c;
}
// Close every finger onto whatever it can reach: the held object, loose props, bodies and world surfaces alike, as one set of
// rounded boxes (none: free_curls), smoothed in `state`. Each finger wraps in three stages, each testing the phalanx it closes (its
// skin: finger_clearance, the three arcs, least curled wins) against the shapes: the knuckle closes until the proximal phalanx
// touches; then the middle joint, the distal one following it at its own share, until the middle phalanx (or the tip it carries)
// touches; then the distal joint on its own until the fingertip touches. A finger lies on what it holds with every joint flexed,
// whatever the size of it. Contact means ENTRY into a shape from its free side: lying on a surface (a few millimetres into it) is
// contact, the finger may close until it goes deeper than it rests; a finger that already sits deep inside a shape (a seat that
// overlaps the open hand: the thumb of a hand around a bottle) is not blocked by it until it has left it, and from then on the shape
// blocks it as any other. No joint goes beyond the end of its path at `reach` (the most the controller can ask: held_curl for a hold).
// The thumb starts from its open pose (finger_open_degrees), clear of whatever the bind pose buries it in, and closes as one across the palm,
// then the same stages (its thickness sphere, not the arcs: it closes out of its hinge plane). The
// palm skin's gap to the set and the palm's speed give the resting curl of a hand lying on a surface. Contact poses fall at once and rise with finger_ik_tau, so a finger never crosses a
// surface the frame it arrives. Deterministic; dt in seconds.
inline constexpr double contact_tolerance=.004;
// The same shape twice, for the memo: two millimetres of position, size and rounding, a hundredth of basis (a held shaft's end moves ~2 mm).
inline constexpr double memo_length_tolerance=.002,memo_basis_tolerance=.01;
inline constexpr unsigned finger_solve_all=5;
inline double palm_shape_distance(const PalmShape& shape,Vec3 point) {
    const auto d=point-shape.position;
    if(shape.rim>=0 && shape.half.x>0)
        return cylinder_distance(dot(d,shape.basis[0]),std::hypot(dot(d,shape.basis[1]),dot(d,shape.basis[2])),shape.half.x,shape.radius,shape.rim);
    if(shape.radius_end>=0 && shape.radius_end!=shape.radius && shape.half.x>0)
        return round_cone_distance({dot(d,shape.basis[0]),dot(d,shape.basis[1]),dot(d,shape.basis[2])},shape.half.x,shape.radius,shape.radius_end);
    const Vec3 q{std::abs(dot(d,shape.basis[0]))-shape.half.x,std::abs(dot(d,shape.basis[1]))-shape.half.y,std::abs(dot(d,shape.basis[2]))-shape.half.z};
    return length({std::max(q.x,0.),std::max(q.y,0.),std::max(q.z,0.)})+std::min(std::max({q.x,q.y,q.z}),0.)-shape.radius;
}
inline bool near_palm_shape(const PalmShape& a,const PalmShape& b) {
    const auto within=[](Vec3 u,Vec3 v,double tolerance) {return std::abs(u.x-v.x)<=tolerance && std::abs(u.y-v.y)<=tolerance && std::abs(u.z-v.z)<=tolerance;};
    return within(a.position,b.position,memo_length_tolerance) && within(a.half,b.half,memo_length_tolerance) && std::abs(a.radius-b.radius)<=memo_length_tolerance && std::abs(a.radius_end-b.radius_end)<=memo_length_tolerance && std::abs(a.rim-b.rim)<=memo_length_tolerance &&
        within(a.basis[0],b.basis[0],memo_basis_tolerance) && within(a.basis[1],b.basis[1],memo_basis_tolerance) && within(a.basis[2],b.basis[2],memo_basis_tolerance);
}
// A fingerprint of everything a finger solve reads from the hand (a rebuilt rig invalidates the memos).
inline std::uint64_t geometry_key(const HandGeometry& hand) {
    std::uint64_t h=1469598103934665603ull;
    const auto mix=[&](double v) {std::uint64_t bits{};std::memcpy(&bits,&v,sizeof bits);h^=bits+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);};
    const auto mix3=[&](Vec3 v) {mix(v.x);mix(v.y);mix(v.z);};
    mix3(hand.palm_in);
    for(unsigned f=0;f<5;++f) {
        const auto& g=hand.finger[f];mix3(g.pivot);mix(hand.thickness[f]);
        for(unsigned s=0;s<3;++s) {mix3(g.bone[s]);mix3(g.hinge[s]);mix(g.existing[s]);mix(g.lowest[s]);}
    }
    return h;
} // m: a finger resting this far into a surface is touching it, not inside it
inline constexpr float relaxed_share=.3f;
// The curl each finger asks for before any contact limits it (finger_turns): a hold wants every finger shut to held_curl, the thumb all the
// way (its pad meets the far side; a contact stops it); a free hand, the controller's squeeze and trigger. The folded fingers the thumb lies on
// are posed by the same want.
inline std::array<float,5> finger_wants(bool held,float squeeze,float index) {
    return held?std::array<float,5>{1.f,held_curl,held_curl,held_curl,held_curl}:std::array<float,5>{std::max(squeeze,.5f*index),index,squeeze,squeeze,squeeze};
}
inline constexpr float thumb_fold_curl=.4f; // a finger asking for less is not folded: the thumb has nothing to lie on
inline FingerCurls finger_contact(const HandGeometry& hand,const WorldTrackedPose& palm,const HeldShape* shapes,unsigned count,double dt,FingerIkState& state,float reach=1.f,
    unsigned budget=finger_solve_all,const std::array<float,5>* wanted=nullptr) {
    require_hand(hand);require_frame(palm,"Invalid finger palm frame",true);
    if(!std::isfinite(dt) || dt<0 || dt>1) throw std::invalid_argument("Invalid finger contact step");
    if(count>max_contact_shapes || (count && !shapes)) throw std::invalid_argument("Invalid finger contact set");
    for(unsigned i=0;i<count;++i) require_shape(shapes[i]);
    auto pose_target=free_curls(reach);
    std::array<bool,5> touching{};
    float rest_target=0;
    state.solved=0;
    std::array<float,5> wants;wants.fill(reach);if(wanted) wants=*wanted;
    const bool fold=wanted && (wants[1]>=thumb_fold_curl || wants[2]>=thumb_fold_curl); // (no wants given: the fingers are nothing to the thumb)
    if(count || fold) {
        // The solve runs in the palm's own frame: the shapes are carried into it once (a held object, which moves with the palm, is the same
        // shape every frame), and the points the fingers test need no transform.
        const auto world=[](Vec3 p) {return p;};
        std::array<PalmShape,max_memo_shapes> local{};
        for(unsigned i=0;i<count;++i) {
            const auto& s=shapes[i];const auto d=s.frame.position-palm.position;
            const auto into=[&](Vec3 v) {return Vec3{dot(v,palm.basis[0]),dot(v,palm.basis[1]),dot(v,palm.basis[2])};};
            local[i]={into(d),{into(s.frame.basis[0]),into(s.frame.basis[1]),into(s.frame.basis[2])},s.half,s.radius,s.radius_end,s.rim};
        }
        // The shapes this finger can reach (its whole length from the knuckle plus its thickness); the rest cost nothing.
        std::array<const PalmShape*,max_memo_shapes> within_reach{};std::array<unsigned,max_memo_shapes> reach_index{};unsigned reachable=0;
        const auto distance=[&](Vec3 p) {double d=1e9;for(unsigned i=0;i<reachable;++i) d=std::min(d,palm_shape_distance(*within_reach[i],p));return d;};
        const auto reach_of=[&](const FingerSegments& g,unsigned f) {
            reachable=0;double length_sum=hand.thickness[f];for(const auto& b:g.bone) length_sum+=length(b);
            for(unsigned i=0;i<count;++i) if(length(local[i].position-g.pivot)<=length(local[i].half)+std::max(local[i].radius,local[i].radius_end)+length_sum) {reach_index[reachable]=i;within_reach[reachable++]=&local[i];}
            return reachable>0;
        };
        // Largest u in [0,1] before the first ENTRY into a shape, for a clearance `at(u,stop)`: lying on a surface (up to contact_tolerance
        // into it) is contact, the search may go on until it gets deeper than it rests; sitting deeper inside a shape, nothing blocks until
        // the skin has left it. March, then bisect (no monotonicity assumed, 22 evaluations at most). A step is at least 1/16 (a fingertip
        // travel of ~6 mm, so a pill 1 cm across is not stepped over) and as far as the clearance proves empty: no skin point moves
        // faster than `rate` per unit of u, so a clearance c leaves c/rate free.
        struct March {double lo;bool touched;bool buried;};
        const auto march=[&](double rate,auto&& at) {
            const double rest=at(0.,-1e300);
            const bool inside=rest<-contact_tolerance;
            const double floor=inside?0.:std::min(0.,rest)-1e-3;
            const auto clear=[&](double u) {return at(u,inside?-1e300:floor)-floor;};
            double v=inside?-1:rest-floor,lo=0,hi=-1,best=-1e300,best_u=0;bool armed=!inside; // (rest > floor: `clear(0)` without a second evaluation)
            while(lo<1 && hi<0) {
                const double u=std::min(1.,lo+(armed?std::max(1./16,.9*v/std::max(rate,1e-6)):1./16));
                v=clear(u);
                if(!armed) {if(v>best) {best=v;best_u=u;}if(v>=0) armed=true;lo=u;}
                else if(v<0) hi=u;else lo=u;
            }
            if(!armed) return March{best_u,false,true}; // never out of it: the seat buries the joint; it stays where it is least buried
            if(hi>=0) for(unsigned i=0;i<4;++i) {const double m=(lo+hi)*.5;if(clear(m)<0) hi=m;else lo=m;}
            return March{lo,hi>=0,false};
        };
        // One finger on one curve (finger_angles' paths end at `top`): the three stages above, each closing from where the one before left
        // the finger, each joint no further than the end of its own path.
        const auto solve=[&](const FingerSegments& g,unsigned f,std::array<float,3>& pose) {
            const double thickness=hand.thickness[f];
            const auto top=finger_reach_angles(f,reach);
            const double follow_ratio=finger_limit_degrees[f][2]/finger_limit_degrees[f][1]; // the distal joint's share of the middle one's angle
            std::array<double,3> lever{};
            for(unsigned s=0;s<3;++s) {lever[s]=thickness;for(unsigned k=s;k<3;++k) lever[s]+=length(g.bone[k]);}
            const auto skin=[&](const std::array<double,3>& angles,double stop,unsigned from,unsigned to,unsigned first=1) {
                return finger_clearance(g,thickness,finger_turns_of(g,angles),world,distance,f?arc_all:0u,4,stop,from,to,first);};
            std::array<double,3> a{};bool stopped=false;
            // A stage whose phalanges lie buried in a shape for the whole of it (a seat around the palm buries the thumb) is run again on
            // the far end of the phalanx alone: the tip may still meet the shape from outside.
            const auto run=[&](double rate,auto&& at,unsigned first) {
                auto m=march(rate,[&](double u,double stop) {return at(u,stop,first);});
                if(m.buried) m=march(rate,[&](double u,double stop) {return at(u,stop,3u);});
                return m;
            };
            // The stages close from the open pose (a finger: straight; the thumb: opened past its bind pose, finger_open_degrees), so a joint
            // the bind pose leaves inside a shape starts clear of it and enters it from the free side. The first stage swings the knuckle
            // (the thumb's CMC) alone for a finger, and the whole thumb together (`couple`: it closes across the palm as one, curling as it
            // goes); the second closes the middle joint on from where that left it, the distal joint following.
            const auto open_=finger_open_angles(f);
            const double couple=f==0?1.:0.;
            {
                const double lo=open_[0],room=top[0]-lo;
                if(room>0) {
                    const double room1=top[1]-open_[1],room2=top[2]-open_[2];
                    const auto m=run(1.3*(room*lever[0]+couple*(room1*lever[1]+room2*lever[2])),[&](double u,double stop,unsigned first) {
                        return skin({lo+u*room,open_[1]+couple*u*room1,open_[2]+couple*u*room2},stop,0,2,first);},1);
                    a={lo+m.lo*room,open_[1]+couple*m.lo*room1,open_[2]+couple*m.lo*room2};stopped=stopped || m.touched;
                } else a[0]=top[0];
            }
            {
                const double lo=a[1],room=top[1]-lo,start2=a[2];
                if(room>0) {
                    const auto m=run(1.3*room*(lever[1]+follow_ratio*lever[2]),[&](double u,double stop,unsigned first) {
                        return skin({a[0],lo+u*room,std::min(top[2],start2+follow_ratio*u*room)},stop,1,2,first);},f==0?2u:1u);
                    a[1]=lo+m.lo*room;stopped=stopped || m.touched;
                    a[2]=std::min(top[2],start2+follow_ratio*m.lo*room);
                }
            }
            if(top[2]>a[2]) {
                const double room=top[2]-a[2],start=a[2];
                const auto m=run(1.3*room*lever[2],[&](double u,double stop,unsigned first) {return skin({a[0],a[1],start+u*room},stop,2,2,first);},1);
                a[2]=start+m.lo*room;stopped=stopped || m.touched;
            }
            for(unsigned s=0;s<3;++s) pose[s]=static_cast<float>(a[s]);
            return stopped;
        };
        // Each finger on its own, against the shapes it reaches. A finger whose shapes are what it was last solved against (in the palm's frame,
        // to two millimetres) takes that answer; the others are solved, at most `budget` a call, in turn (the rest keep their pose until their
        // turn: the fingers take turns, so what a frame costs is bounded whatever arrives). The four fingers first; the thumb then lies on
        // the index and middle fingers as they stand (folded: they ask for a curl; their phalanges are shapes to it, which is what puts the
        // thumb of a fist over the fingers instead of inside them, and its pad against the index of a hand round a handle).
        std::array<std::array<unsigned,max_memo_shapes>,5> picked{};std::array<unsigned,5> picked_count{};std::array<bool,5> stale{},drifted{};
        const std::uint64_t key=geometry_key(hand);
        if(key!=state.hand_key) {for(auto& m:state.memo) m.valid=false;state.hand_key=key;}
        const auto classify=[&](unsigned f) {
            auto& memo=state.memo[f];
            if(reachable==0) {memo.valid=false;return;}
            picked_count[f]=reachable;for(unsigned k=0;k<reachable;++k) picked[f][k]=reach_index[k];
            bool same=memo.valid && memo.reach==reach && memo.count==reachable;
            for(unsigned k=0;same && k<reachable;++k) same=near_palm_shape(memo.shapes[k],local[reach_index[k]]);
            if(same) {pose_target.pose[f]=memo.pose;touching[f]=memo.touching;}
            else {stale[f]=true;drifted[f]=memo.valid && memo.reach==reach && memo.count==reachable;} // drifted: the same shapes, moved more than the memo allows
        };
        const auto run_finger=[&](unsigned f) {
            reachable=picked_count[f];for(unsigned k=0;k<reachable;++k) {reach_index[k]=picked[f][k];within_reach[k]=&local[picked[f][k]];}
            touching[f]=solve(hand.finger[f],f,pose_target.pose[f]);
            auto& memo=state.memo[f];memo.valid=true;memo.reach=reach;memo.count=reachable;memo.pose=pose_target.pose[f];memo.touching=touching[f];
            for(unsigned k=0;k<reachable;++k) memo.shapes[k]=local[picked[f][k]];
            stale[f]=false;++state.solved;
        };
        for(unsigned f=1;f<5;++f) {reach_of(hand.finger[f],f);classify(f);}
        // New situations first-come (up to `budget`); a finger whose shapes merely drifted refreshes one at a time (half the budget).
        const unsigned refresh_limit=budget>=finger_solve_all?budget:std::max(1u,budget/2);unsigned refreshed=0;
        const bool thumb_first=state.thumb_waited>=3; // a thumb kept waiting goes before the fingers
        const unsigned first_turn=state.rotor%4;
        for(unsigned n=0;n<4 && state.solved+(thumb_first?1u:0u)<budget;++n) {
            const unsigned f=1+(first_turn+n)%4;
            if(!stale[f]) continue;
            if(drifted[f]) {if(refreshed>=refresh_limit) continue;++refreshed;}
            run_finger(f);state.rotor=f%4;
        }
        if(state.valid) for(unsigned f=1;f<5;++f) if(stale[f]) pose_target.pose[f]=state.pose[f]; // not its turn yet: the finger stays as it is
        {   // the thumb: the world's shapes it reaches, then the folded fingers' phalanges
            reach_of(hand.finger[0],0);
            if(fold) for(unsigned f=1;f<=2 && reachable+2<=max_memo_shapes;++f) {
                if(wants[f]<thumb_fold_curl) continue;
                const auto angles=finger_pose_angles(f,std::max(wants[f],relaxed_share),pose_target.pose[f]);
                const auto points=finger_points(hand.finger[f],finger_turns_of(hand.finger[f],angles));
                for(unsigned k=0;k<2;++k) {
                    const Vec3 along=points[k+1]-points[k];const double len=length(along);
                    if(len<1e-6) continue;
                    const Vec3 u=along*(1/len),t=unit(cross(u,std::abs(u.x)<.9?Vec3{1,0,0}:Vec3{0,1,0}));
                    const unsigned slot=max_contact_shapes+(f-1)*2+k;
                    local[slot]={(points[k]+points[k+1])*.5,{u,t,cross(u,t)},{len*.5,0,0},hand.thickness[f]};
                    reach_index[reachable]=slot;within_reach[reachable++]=&local[slot];
                }
            }
            classify(0);
            if(stale[0] && (state.solved<budget || thumb_first) && (!drifted[0] || refreshed<refresh_limit)) {run_finger(0);if(drifted[0]) ++refreshed;}
            if(stale[0]) ++state.thumb_waited;else state.thumb_waited=0;
            if(state.valid && stale[0]) pose_target.pose[0]=state.pose[0];
        }
        // Resting: the palm skin's gap to the nearest shape, slow hand.
        const auto skin_point=palm.position+palm_normal(palm,hand)*held_palm_depth;
        double gap=1e9;for(unsigned i=0;i<count;++i) gap=std::min(gap,shape_distance(shapes[i],skin_point));
        rest_target=static_cast<float>(std::clamp((resting_gap_far-gap)/(resting_gap_far-resting_gap_near),0.,1.))*resting_curl;
    }
    const double k=dt>0?1-std::exp(-dt/finger_ik_tau):0;
    // The hand's speed (smoothed over a tau), from the palm frames the calls see.
    if(state.palm_valid && dt>1e-4) {const double raw=length(palm.position-state.palm)/dt;state.speed=static_cast<float>(state.speed+(raw-state.speed)*k);}
    state.palm=palm.position;state.palm_valid=true;
    const float slow=static_cast<float>(std::clamp((resting_speed_fast-state.speed)/(resting_speed_fast-resting_speed_slow),0.,1.));
    rest_target*=slow;
    const auto follow=[&](float& c,float t) {c=state.valid?std::min(t,static_cast<float>(c+(t-c)*k)):t;};
    const auto ease=[&](float& c,float t) {c=state.valid?static_cast<float>(c+(t-c)*k):t;};
    FingerCurls out{};out.touching=touching;
    // The scalar a consumer that knows no joints takes (the legacy arm, the hand's push shape): the least closed joint's curl.
    const auto curl_of=[&](unsigned f,const std::array<float,3>& pose) {
        double curl=1;
        for(unsigned s=0;s<3;++s) if(pose[s]<finger_reach_angles(f,reach)[s]-1e-6) curl=std::min(curl,finger_curl_at(f,s,pose[s]));
        return static_cast<float>(std::clamp(curl,0.,1.));
    };
    for(unsigned f=0;f<5;++f) {
        for(unsigned s=0;s<3;++s) {follow(state.pose[f][s],pose_target.pose[f][s]);out.pose[f][s]=state.pose[f][s];}
        out.finger[f]=curl_of(f,state.pose[f]);
    }
    ease(state.resting,rest_target);out.resting=state.resting;
    state.valid=true;
    return out;
}
inline FingerCurls finger_contact(const HandGeometry& hand,const WorldTrackedPose& palm,const HeldShape* shape,double dt,FingerIkState& state,float reach=1.f) {
    return finger_contact(hand,palm,shape,shape?1u:0u,dt,state,reach);
}
// Squeeze/trigger fingers as arm_tracking.hpp builds them, clamped by the contact. The four fingers of a hand lying on a surface want at
// least the resting curl (a contact still caps them).
inline std::array<float,5> grip_curls(const FingerCurls& contact,const std::array<float,5>& wanted) { // the wants given per finger (the touch curl's)
    for(float w:wanted) if(!std::isfinite(w) || w<0 || w>1) throw std::invalid_argument("Invalid controller finger curl");
    std::array<float,5> out{};
    for(unsigned f=0;f<5;++f) out[f]=std::min(f?std::max(wanted[f],contact.resting):wanted[f],contact.finger[f]);
    return out;
}
inline std::array<float,5> grip_curls(const FingerCurls& contact,float squeeze,float index) {
    if(!std::isfinite(squeeze) || squeeze<0 || squeeze>1 || !std::isfinite(index) || index<0 || index>1) throw std::invalid_argument("Invalid controller finger curl");
    return grip_curls(contact,std::array<float,5>{std::max(squeeze,.5f*index),index,squeeze,squeeze,squeeze});
}
// The hinge turn (rad, over the rest pose) of every chain joint, for a pose that bends joints by angle (BodySolver). Each finger takes
// one wanted curl and every joint follows its own path (finger_angles: their limits and coupling) up to the angle the contact left it
// (the finger wraps: finger_contact), less the bend the rest pose has. A held hand wants every finger shut to held_curl (the grip alone
// holds it; the index is not a trigger finger then; HIGGS's minimum openness 0.2 keeps a finger that touches nothing from overcurling);
// a free finger wants at least a relaxed curl, or the resting curl of a hand on a surface, which a surface still caps.
inline std::array<std::array<float,3>,5> finger_turns(const HandGeometry& hand,const FingerCurls& contact,const std::array<float,5>& wanted) {
    require_hand(hand);
    for(float w:wanted) if(!std::isfinite(w) || w<0 || w>1) throw std::invalid_argument("Invalid controller finger curl");
    std::array<std::array<float,3>,5> turn{};
    for(unsigned f=0;f<5;++f) {
        const float floor_share=f?std::max(relaxed_share,contact.resting):relaxed_share;
        const float want=std::max(wanted[f],floor_share);
        const auto angles=finger_pose_angles(f,want,contact.pose[f]);
        const auto t=finger_turns_of(hand.finger[f],angles);
        for(unsigned s=0;s<3;++s) turn[f][s]=static_cast<float>(t[s]);
    }
    return turn;
}
inline std::array<std::array<float,3>,5> finger_turns(const HandGeometry& hand,const FingerCurls& contact,float squeeze,float index,bool held) {
    if(!std::isfinite(squeeze) || squeeze<0 || squeeze>1 || !std::isfinite(index) || index<0 || index>1) {require_hand(hand);throw std::invalid_argument("Invalid controller finger curl");}
    return finger_turns(hand,contact,finger_wants(held,squeeze,index));
}

// The cylinder that best explains a world AABB (half extents e) for a body with these axes: e_j = h|a_j| + r sqrt(1-a_j^2),
// least squares in shaft half length h and radius r per axis, exact for a cylinder at any tilt; the axis with the least residual wins.
struct BoundsFit {unsigned axis{};double half{},radius{},residual{1e9};};
inline BoundsFit fit_cylinder(const std::array<Vec3,3>& axes,Vec3 e) {
    const std::array<double,3> ev{e.x,e.y,e.z};const double mean=(e.x+e.y+e.z)/3;
    BoundsFit best{};
    for(unsigned i=0;i<3;++i) {
        const auto a=axes[i];const std::array<double,3> u{std::abs(a.x),std::abs(a.y),std::abs(a.z)};
        std::array<double,3> v{};
        for(unsigned j=0;j<3;++j) v[j]=std::sqrt(std::max(0.,1-u[j]*u[j]));
        double suu{},suv{},svv{},sue{},sve{};
        for(unsigned j=0;j<3;++j) {suu+=u[j]*u[j];suv+=u[j]*v[j];svv+=v[j]*v[j];sue+=u[j]*ev[j];sve+=v[j]*ev[j];}
        const double det=suu*svv-suv*suv;
        if(det<.05) continue; // axis within_reach the cube diagonal: h and r are not separable
        double h=(svv*sue-suv*sve)/det,r=(suu*sve-suv*sue)/det;
        if(r<0) {r=0;h=sue/suu;}
        if(h<0) {h=0;r=svv>0?sve/svv:0;}
        double residual=0;
        for(unsigned j=0;j<3;++j) residual+=(ev[j]-h*u[j]-r*v[j])*(ev[j]-h*u[j]-r*v[j]);
        residual=std::sqrt(residual)/mean;
        if(residual<best.residual) best={i,h,r,residual};
    }
    return best;
}

// What a hand holds, from what the mod reads. A ragdoll limb (bone>=0) is a
// capsule of the bone's radius across the palm. A prop's world AABB becomes the
// cylinder along the body axis that explains it (bottle, pipe, severed arm, at any
// tilt: half extents e_j = h|a_j| + r sqrt(1-a_j^2), least squares in h and r,
// exact for a cylinder) as a capsule of that shaft, or, when none fits within
// 10%, the AABB itself. No bounds: a 10 cm sphere at the body (the old held_size fallback).
inline HeldShape classify_held(const WorldTrackedPose& body,const std::optional<NativeGrabBounds>& bounds,int bone,const WorldTrackedPose& palm) {
    require_frame(body,"Invalid held body frame",false);require_frame(palm,"Invalid finger palm frame",true);
    HeldShape shape{};shape.frame=body;
    if(bone>=0) {
        if(bone>=17) throw std::invalid_argument("Invalid ragdoll bone");
        double r=ragdoll_bone_radius[static_cast<std::size_t>(bone)];
        if(bounds && finite(bounds->minimum) && finite(bounds->maximum)) {
            const double half=(bounds->maximum.x-bounds->minimum.x)*.5;
            if(half>.01 && half<.3) r=half;
        }
        // ponytail: the limb lies across the palm (bar grip); use the ragdoll bone's direction once it is read.
        shape.frame.basis=palm.basis;shape.half={.15,0,0};shape.radius=r;
        return shape;
    }
    if(!bounds) {shape.radius=.1;return shape;}
    if(!finite(bounds->minimum) || !finite(bounds->maximum)) throw std::invalid_argument("Invalid held bounds");
    const auto e=(bounds->maximum-bounds->minimum)*.5;
    if(e.x<=0 || e.y<=0 || e.z<=0 || length(e)>3) throw std::invalid_argument("Invalid held bounds");
    shape.frame.position=(bounds->maximum+bounds->minimum)*.5;
    const auto fit=fit_cylinder(body.basis,e);
    if(fit.residual<=.1 && fit.radius>0 && fit.half>=0) {
        shape.frame.basis={body.basis[fit.axis],body.basis[(fit.axis+1)%3],body.basis[(fit.axis+2)%3]};
        shape.half={fit.half,0,0};shape.radius=fit.radius;
        return shape;
    }
    shape.frame.basis={{{1,0,0},{0,1,0},{0,0,1}}};shape.half=e;shape.radius=0; // the drawn AABB: never inside it
    return shape;
}

// A held prop as an axis, a shaft half length and a radius: the capsule (a ball at half 0) the world AABB reveals against the drawn axes.
struct HeldCylinder {unsigned axis{2};double half{},radius{.03};RadiusProfile profile{};}; // profile: the measured cross-section (radius_profile.hpp), if any
inline constexpr double held_radius_floor=.004;
// The engine's world AABB of a prop (model+0x1840) is its mesh's LOCAL box turned by the drawn axes and boxed again (static model 268de0
// "transforms its actual mesh bounds"): e_j = sum_i half_i |axes[i][j]|. Turned by a tilted prop it is a loose box of the real shape (sim v522:
// a bottle 13.9 x 13.9 x 41.3 cm, tilted, reported 40 x 24 x 41 cm, and a cylinder fitted to those extents came out 8.7 cm in radius). The
// local half extents come back exactly, every frame and any pose, by solving that 3x3 system (the same box in all the frames of a hold, the
// physics shape's own AABB agrees to the millimetre); a pose where two axes lie at 45 degrees to the box leaves it unsolvable (no answer).
inline std::optional<Vec3> held_local_extents(const std::array<Vec3,3>& axes,Vec3 e) {
    const auto m=[&](unsigned row,unsigned column) { // row j: world axis, column i: the model axis i's component on it
        const Vec3 a=unit(axes[column]);return std::abs(row==0?a.x:row==1?a.y:a.z);};
    const double a00=m(0,0),a01=m(0,1),a02=m(0,2),a10=m(1,0),a11=m(1,1),a12=m(1,2),a20=m(2,0),a21=m(2,1),a22=m(2,2);
    const double det=a00*(a11*a22-a12*a21)-a01*(a10*a22-a12*a20)+a02*(a10*a21-a11*a20);
    if(!(std::abs(det)>=.02)) return {};
    const double inv=1/det;
    const double h0=(e.x*(a11*a22-a12*a21)-a01*(e.y*a22-a12*e.z)+a02*(e.y*a21-a11*e.z))*inv;
    const double h1=(a00*(e.y*a22-a12*e.z)-e.x*(a10*a22-a12*a20)+a02*(a10*e.z-e.y*a20))*inv;
    const double h2=(a00*(a11*e.z-e.y*a21)-a01*(a10*e.z-e.y*a20)+e.x*(a10*a21-a11*a20))*inv;
    // (a half extent a little below zero is rounding; one well below, or none at all, is not a box of this shape)
    if(!(h0>1e-4 && h1>1e-4 && h2>1e-4) || !std::isfinite(h0+h1+h2)) return {};
    return Vec3{h0,h1,h2};
}
// From the prop's world AABB and its drawn axes (rows of the model's axis block): the local box (held_local_extents) read as a shaft along its
// longest side, its radius the mean of the two others; when that cannot be solved, a cylinder fitted to the world extents (fit_cylinder), and
// failing that a ball of the mean half extent of the two smaller sides (ponytail: a box-like carryable is not told from a ball).
// `latch`: the local extents of this one prop, kept by the caller while it is held (the box of a prop never changes, so a frame whose pose
// cannot be solved takes the last one that could, and a latched box is not solved again).
// Which of a prop's local axes its shaft lies along: the one the other two half extents agree about (a bottle's long axis, a tumbler's up axis: a
// body of revolution turns about it), tried in the order Z, X, Y (the models are Z-up) and refused when the prop is flatter than held_symmetry_flat of
// that radius (a plate is held by its rim, not as a ball); else the longest (a box has no axis of revolution). The longest alone took a tumbler
// 12.3 x 12.3 x 10.6 cm for a disc about its side (sim v525: axis 0, half 4 mm, radius 5.7 cm).
inline constexpr double held_symmetry_tolerance=.1,held_symmetry_flat=.6,held_cup_ratio=.6;
inline unsigned held_axis_of(const double (&h)[3]) {
    for(const unsigned a:{2u,0u,1u}) {
        const double p=h[(a+1)%3],q=h[(a+2)%3],wide=std::max(p,q);
        if(std::abs(p-q)<=held_symmetry_tolerance*wide && h[a]>=held_symmetry_flat*wide) return a;
    }
    unsigned a=0;for(unsigned i=1;i<3;++i) if(h[i]>h[a]) a=i;
    return a;
}
inline HeldCylinder held_cylinder(const std::array<Vec3,3>& axes,const NativeGrabBounds& bounds,std::optional<Vec3>* latch=nullptr) {
    for(const auto& a:axes) if(!finite(a)) throw std::invalid_argument("Invalid held axes");
    if(!finite(bounds.minimum) || !finite(bounds.maximum)) throw std::invalid_argument("Invalid held bounds");
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(e.x<=0 || e.y<=0 || e.z<=0 || length(e)>3) throw std::invalid_argument("Invalid held bounds");
    HeldCylinder c{};
    std::optional<Vec3> local=latch?*latch:std::nullopt;
    if(!local) {local=held_local_extents(axes,e);if(local && latch) *latch=local;}
    if(local) {
        const double h[3]{local->x,local->y,local->z};
        const unsigned a=held_axis_of(h);
        c.axis=a;c.radius=(h[(a+1)%3]+h[(a+2)%3])*.5;
        // A shaft; a cup (a tumbler, a jar, a tin: round about its axis and at least held_cup_ratio of its radius tall) is a capsule as long as it is tall, so its wall
        // is straight along the axis where the four fingers lie side by side (a ball's wall curves away from the outer fingers and the thumb: they floated 1 cm off
        // a tumbler's); anything squatter or without an axis of revolution is a ball of its smaller sides.
        const bool round_about=std::abs(h[(a+1)%3]-h[(a+2)%3])<=held_symmetry_tolerance*std::max(h[(a+1)%3],h[(a+2)%3]);
        c.half=(h[a]>1.5*c.radius || (round_about && h[a]>=held_cup_ratio*c.radius))?h[a]:std::max(0.,h[a]-c.radius);
        c.radius=std::max(c.radius,held_radius_floor);c.half=std::min(c.half,.35);
        return c;
    }
    const auto fit=fit_cylinder(axes,e);
    if(fit.residual<=.1 && fit.radius>0 && fit.half>=0) {c.axis=fit.axis;c.half=fit.half;c.radius=fit.radius;}
    else {
        const double lo=std::min({e.x,e.y,e.z}),hi=std::max({e.x,e.y,e.z}),mid=e.x+e.y+e.z-lo-hi;
        c.radius=(lo+mid)*.5;c.half=std::max(0.,hi-c.radius);
    }
    c.radius=std::max(c.radius,held_radius_floor);c.half=std::min(c.half,.35);
    return c;
}
// Where the fist holds it, derived from the shape itself: the long axis along the grip's hole axis (basis[0], across the palm), the
// palm skin resting on its surface (the skin is one radius from the axis, measured across it: the palm normal's component across the
// shaft, whatever the angle between the two), and held `along` of its length up from the base (HIGGS keeps where the hand touched,
// Bonelab's cylinder slides within a limit; held_touch measures it, a hold with no touch point takes the lower third, 0.3). The shaft
// lies over the knuckles, not the palm centre: the axis is turned from the palm normal toward the fingers about the palm skin point so
// that it sits held_knuckle_shift (at most 0.8 of the radius) toward the finger bases, the skin still on the surface, the open fingers
// just outside it. Base = the -axis end
// (ponytail: Z-up models; the native hold has no pickup touch point). The other two object axes keep following the hand (X down, Y left),
// turned about the axis by `roll` (held_touch: the roll the object had when taken; Marrow's cylinder spins freely, HIGGS keeps the
// rotation). `lift` stands the shaft off the palm skin by that much (held_lift). `centre` is the mesh centre in object axes: origin is
// the object origin that puts it there. `shape` is what the fingers close on, in the same pose.
inline constexpr double held_grip_fraction=.3,held_along_min=.15,held_along_max=.45,held_knuckle_shift=.035;
// A profiled shaft (the cross-section measured along it) can be held anywhere along its length, and the fingers close on the profile
// within held_profile_span each side of the grip point (a hand's width), as at most max_held_shapes round cones, not one cylinder for it all.
inline constexpr double held_profile_along_min=.08,held_profile_along_max=.92,held_profile_span=.045;
inline constexpr unsigned max_held_shapes=4;
// The ways a seat can lay a shaft in the hand (seat_held `mode`): 0 along the hole axis, its + end toward the index side; 1 along it the other way round;
// 2 along the palm normal (+ end away from the palm: the end-on cradle or top grip, the palm on the shaft's end); 3 along it reversed. 2 and 3 are for a
// squat round prop only (held_squat_ratio). Each has the axis `u` and the direction `ref` its first side axis takes at no roll.
inline constexpr double held_squat_ratio=1.2,held_cradle_margin=.1745; // a squat prop is at most 1.2 radii half long; the cradle must turn it 10 degrees less than the wall grip
inline constexpr double held_rim=.008;                                  // a held cylinder's rim rounding (m)
struct SeatAxis {Vec3 u,ref;};
inline Vec3 seat_across(const WorldTrackedPose& palm,Vec3 in) {
    const auto& h=palm.basis;auto across=in-h[0]*dot(in,h[0]);
    if(length(across)<.3) across=h[1]; // a palm normal along the shaft is no palm normal for a grip
    return unit(across);
}
inline SeatAxis seat_axis(const WorldTrackedPose& palm,Vec3 in,unsigned mode) {
    const auto& h=palm.basis;
    switch(mode) {
        case 1: return {h[0]*-1.,h[2]*-1.};
        case 2: return {seat_across(palm,in),h[0]};
        case 3: return {seat_across(palm,in)*-1.,h[0]};
        default: return {h[0],h[2]*-1.};
    }
}
struct HeldSeat {std::array<Vec3,3> axes{};Vec3 origin{};HeldShape shape{};std::array<HeldShape,max_held_shapes> shapes{};unsigned count{1};}; // shape == shapes[0]
inline bool held_mesh_centre_valid(Vec3 centre,bool severed_limb) {
    return finite(centre) && (severed_limb?length(centre)<=2.:length(centre)<.5);
}
inline bool held_native_mesh_centre_valid(Vec3 centre,std::string_view native_class) {
    return held_mesh_centre_valid(centre,native_class==".?AVarkSeveredLimb@@");
}
// The profile of `c` when it is one for this very shaft (its span and axis are the cylinder's), else none.
inline bool held_profiled(const HeldCylinder& c) {return c.profile.valid() && std::abs(c.profile.half-c.half)<1e-6 && c.profile.axis==c.axis;}
inline HeldSeat seat_held(const HeldCylinder& c,const WorldTrackedPose& palm,Vec3 in,Vec3 centre,double along=held_grip_fraction,double roll=0,double lift=0,double knuckle=-1,double slide=0,unsigned mode=0) {
    require_frame(palm,"Invalid finger palm frame",true);
    if(!finite(in) || !finite(centre) || c.axis>2 || !(c.radius>0) || c.half<0 || std::abs(length(in)-1)>.01 || !(along>=0 && along<=1) || !std::isfinite(roll) || !(lift>=0 && lift<=.1) || mode>3)
        throw std::invalid_argument("Invalid held seat");
    const auto& h=palm.basis;HeldSeat s{};
    const auto direction=seat_axis(palm,in,mode);const auto u=direction.u;
    const auto side=rotated(axis_angle(u,roll),direction.ref);
    s.axes[c.axis]=u;s.axes[(c.axis+1)%3]=side;s.axes[(c.axis+2)%3]=cross(u,side);
    // the frame the shape lies in: the palm's own for the grip along the hole axis, else the axis and two squares to it (a cylinder is the same about its axis)
    const std::array<Vec3,3> frame_axes=mode==0?palm.basis:std::array<Vec3,3>{u,direction.ref,cross(u,direction.ref)};
    const auto skin=palm.position+in*held_palm_depth;
    const auto n=seat_across(palm,in);
    auto toward=h[2]*-1.;toward=toward-n*dot(toward,n)-h[0]*dot(toward,h[0]); // the fingers' direction, across the shaft and square to n
    const double grip=(along-.5)*2*c.half;                  // the grip point's place along the shaft, from its middle toward the +axis end
    const bool profiled=held_profiled(c);
    const double radius=profiled?std::max(held_radius_floor,profile_radius(c.profile,grip)):c.radius; // the shaft's radius where the palm lies
    const double shift=length(toward)>1e-6?std::min(knuckle>=0?knuckle:held_knuckle_shift,.8*radius):0.;
    const double sine=shift/radius,cosine=std::sqrt(1-sine*sine);
    const auto inward=shift>0?n*cosine+unit(toward)*sine:n; // from the palm skin to the axis, square to the shaft
    const auto hold=skin+inward*(radius+lift);              // the axis point over the palm
    // The end-on seats (2, 3): the palm lies on the near end face, the shaft's middle one half length (a ball: one radius) out along the palm normal; the knuckle
    // candidate becomes how far toward the fingers it sits from there.
    const double extent=c.half>0?c.half:c.radius;
    const auto cradle=skin+n*(extent+lift)+(length(toward)>1e-6?unit(toward)*(knuckle>=0?knuckle-held_knuckle_shift:0.):Vec3{});
    const auto middle=mode>=2?cradle+h[0]*slide:hold-u*grip+h[0]*slide; // slide: along the hole axis (signed: the axis points from the little finger toward the index)                       // the shaft's middle lies ahead of it when held below the middle
    s.origin=middle-(s.axes[0]*centre.x+s.axes[1]*centre.y+s.axes[2]*centre.z);
    s.shapes[0]={{middle,frame_axes},{c.half,0,0},c.radius,-1.,c.half>0?held_rim:-1.}; // (the profile's pieces below replace it near the grip)
    if(profiled) {
        // The profile within a hand's width of the grip point: its end points and the stations between, a round cone from one to the next.
        const double lo=std::max(-c.half,grip-held_profile_span),hi=std::min(c.half,grip+held_profile_span);
        if(hi-lo>1e-3) {
            std::array<double,profile_stations+2> at{};unsigned pieces=0;
            at[pieces++]=lo;
            for(unsigned i=0;i<profile_stations;++i) {const double si=profile_station(i,c.profile.half);if(si>lo+1e-3 && si<hi-1e-3) at[pieces++]=si;}
            at[pieces++]=hi;
            while(pieces-1>max_held_shapes) { // too many pieces: drop the interior point farthest from the grip point
                unsigned worst=1;for(unsigned i=2;i+1<pieces;++i) if(std::abs(at[i]-grip)>std::abs(at[worst]-grip)) worst=i;
                for(unsigned i=worst;i+1<pieces;++i) at[i]=at[i+1];
                --pieces;
            }
            s.count=pieces-1;
            for(unsigned i=0;i+1<pieces;++i) {
                const double a=at[i],b=at[i+1];
                const double ra=std::max(held_radius_floor,profile_radius(c.profile,a)),rb=std::max(held_radius_floor,profile_radius(c.profile,b));
                // A straight piece that reaches the prop's own flat end (a bottle's base, its mouth) is a flat-ended cylinder, not a capsule whose round cap would stand
                // out past that end; its inner end meets the next piece's capsule, which fills the rim's rounding there.
                const bool flat_end=(a<=-c.half+1e-9 || b>=c.half-1e-9) && std::abs(ra-rb)<=5e-4;
                s.shapes[i]={{middle+u*((a+b)*.5),frame_axes},{(b-a)*.5,0,0},flat_end?std::max(ra,rb):ra,flat_end?-1.:rb,flat_end?held_rim:-1.};
            }
        }
    }
    s.shape=s.shapes[0];
    return s;
}
// Where the palm touched the object when the hand took it (HIGGS keeps the point, the rotation stays what it was; Marrow clamps the
// slide to a limit): the palm skin's place along the shaft as a fraction of its length from the base, clamped to
// [held_along_min, held_along_max], and the roll about the shaft the object has against the seat's default spin. `axes` are the object's
// drawn axes in world (rows of the model's axis block), `centre` the world middle of its bounds; the palm must be within held_touch_reach of the
// surface or there was no touch, and the default grip (0.3, no roll) holds. A ball or a stub has no length to slide along.
inline constexpr double held_touch_reach=.25;
// gap: the palm skin's distance from the surface (m). mode: the seat (seat_held) that turns the prop least from the way it lay before the pickup (HIGGS keeps
// the grab's rotation); rotation: how far (rad) that seat, at `roll`, turns it from its pre-pickup axes (-1: not measured).
struct HeldTouch {double along{held_grip_fraction},roll{};bool touched{};double gap{-1};unsigned mode{};double rotation{-1};};
// `aim_reach` (m, 0: none): a hand beyond held_touch_reach has not touched the prop, but its place still says where on it the hand is pointing
// (a hand reaching for the neck is 30 cm from it when the press lands): the grip goes where the prop's axis is nearest the palm, `touched` stays
// false and the roll stays the default. Only for a prop measured before the pickup moved it (the caller knows).
inline constexpr double held_aim_reach=1.5;
inline HeldTouch held_touch(const HeldCylinder& c,const std::array<Vec3,3>& axes,Vec3 centre,const WorldTrackedPose& palm,Vec3 in,double aim_reach=0) {
    require_frame(palm,"Invalid finger palm frame",true);
    for(const auto& a:axes) if(!finite(a) || std::abs(length(a)-1)>.01) throw std::invalid_argument("Invalid held touch axes");
    if(!finite(centre) || !finite(in) || c.axis>2 || !(c.radius>0) || c.half<0 || std::abs(length(in)-1)>.01) throw std::invalid_argument("Invalid held touch");
    HeldTouch t{};const auto up=axes[c.axis];
    const auto skin=palm.position+in*held_palm_depth;
    const double s=dot(skin-centre,up); // from the middle toward the +axis end
    const bool profiled=held_profiled(c);
    const double at=std::clamp(s,-c.half,c.half);
    t.gap=length(skin-(centre+up*at))-(profiled?std::max(held_radius_floor,profile_radius(c.profile,at)):c.radius);
    const double along_min=profiled?held_profile_along_min:held_along_min,along_max=profiled?held_profile_along_max:held_along_max;
    if(t.gap>held_touch_reach) {
        if(aim_reach>0 && t.gap<=aim_reach && c.half>=.01) t.along=std::clamp(.5+s/(2*c.half),along_min,along_max);
        return t;
    }
    t.touched=true;
    if(c.half>=.01) t.along=std::clamp(.5+s/(2*c.half),along_min,along_max);
    // The seat that turns the prop least from the way it lay: the shaft along the hole axis or the other way round (never more than a quarter turn),
    // or for a squat round prop (a cup standing on a table, taken by a hand over it) the end-on seat along the palm normal when that turns it
    // held_cradle_margin less than the wall grip does. Then the roll: the turn that lays the + axis end along the seat's axis, then how far the
    // object's first side axis is from the seat's default.
    const double squat=c.half<=held_squat_ratio*c.radius;
    const auto angle_to=[&](unsigned m) {return std::acos(std::clamp(dot(up,seat_axis(palm,in,m).u),-1.,1.));};
    double best=angle_to(0);
    if(angle_to(1)<best) {best=angle_to(1);t.mode=1;}
    if(squat) for(unsigned m=2;m<4;++m) {const double a=angle_to(m);if(a<best-held_cradle_margin) {best=a;t.mode=m;}}
    const auto direction=seat_axis(palm,in,t.mode);const double d=dot(up,direction.u);
    if(d>-.95) {
        const auto k=cross(up,direction.u);const auto v=axes[(c.axis+1)%3];
        const auto first=v+cross(k,v)+cross(k,cross(k,v))*(1/(1+d));
        t.roll=std::atan2(dot(cross(direction.ref,first),direction.u),dot(direction.ref,first));
    }
    {   // the turn from the pre-pickup axes to the seat's, as one rotation angle
        const auto side=rotated(axis_angle(direction.u,t.roll),direction.ref);
        std::array<Vec3,3> seat_rows{};seat_rows[c.axis]=direction.u;seat_rows[(c.axis+1)%3]=side;seat_rows[(c.axis+2)%3]=cross(direction.u,side);
        const double trace=dot(axes[0],seat_rows[0])+dot(axes[1],seat_rows[1])+dot(axes[2],seat_rows[2]);
        t.rotation=std::acos(std::clamp((trace-1)/2,-1.,1.));
    }
    return t;
}
// The controller's recent grips, newest last, with when each was taken and how hard the hand squeezed (the caller notes one a frame). A native
// pickup flies the prop off and the drawn palm lies where the arm reaches (the avatar can fall short of the controller by 40 cm: sim v525 measured
// every touch 30 cm from the bottle, at the clamp, 0.92 of the shaft, whatever was pressed), so the touch is measured where the controller was
// when the press began: the newest sample, within `window` ms, from before the squeeze rose past `threshold` (none: the caller takes the grip now).
template<unsigned N=128> struct GripTrace {
    struct Sample {std::uint64_t tick{};WorldTrackedPose grip{};float squeeze{};};
    std::array<Sample,N> samples{};unsigned count{};
    void note(std::uint64_t tick,const WorldTrackedPose& grip,float squeeze) {samples[count%N]={tick,grip,squeeze};++count;}
    std::optional<Sample> press(std::uint64_t now,std::uint64_t window,float threshold=.3f) const {
        for(unsigned i=0;i<std::min(count,N);++i) {
            const auto& s=samples[(count-1-i)%N];
            if(now>=s.tick && now-s.tick>window) break; // older than the window: so is everything before it
            if(s.squeeze<threshold) return s;
        }
        return {};
    }
};
// How a prop the probes saw before the pickup relates to the one held now (both as sorted local half extents): `same` when every extent is within 3 mm (the
// same mesh under another entity: sim v527, the aim's target was an entity a heap block away from the glass that was taken), `similar` when each is .5 to 2
// times the held one's, the prop's centre lies within .5 m of where the hand pressed and it was seen within the last .5 s (a neighbour the hand reached for),
// else `none` (a table, a wall, a prop of another size or one that was seen long ago).
enum class ProbeMatch {none,similar,same};
inline ProbeMatch probe_match(const std::array<double,3>& held,const std::array<double,3>& seen,double press_distance,double age_ms) {
    bool same=true,alike=true;
    for(unsigned i=0;i<3;++i) {
        if(!(held[i]>0) || !(seen[i]>0)) return ProbeMatch::none;
        same=same && std::abs(held[i]-seen[i])<.003;
        alike=alike && seen[i]>=.5*held[i] && seen[i]<=2*held[i];
    }
    if(same) return ProbeMatch::same;
    return alike && press_distance<=.5 && age_ms<=500?ProbeMatch::similar:ProbeMatch::none;
}
// How far the shaft must stand off the palm skin for the thumb's first bone (the thenar: it bulges out of the palm toward what is held, the
// chain's first bone at rest) to lie outside it, so the object rests on the thenar and the fingers, not through the thumb's root. At most 4 cm.
inline constexpr double held_lift_max=.04;
inline double held_lift(const HandGeometry& hand,const WorldTrackedPose& palm,const HeldCylinder& c,Vec3 in,double along=held_grip_fraction,double roll=0) {
    require_hand(hand);require_frame(palm,"Invalid finger palm frame",true);
    const auto world=[&](Vec3 p) {return palm.position+palm.basis[0]*p.x+palm.basis[1]*p.y+palm.basis[2]*p.z;};
    double lift=0;
    for(unsigned i=0;i<4;++i) {
        const auto shape=seat_held(c,palm,in,{},along,roll,lift).shape;
        const double clearance=finger_clearance(hand.finger[0],hand.thickness[0],{0,0,0},world,[&](Vec3 w) {return shape_distance(shape,w);},arc_all,4,-1e300,0,0);
        if(clearance>=0 || lift>=held_lift_max) break;
        lift=std::min(held_lift_max,lift-clearance+.001);
    }
    return lift;
}
// Where a shaft lies in the hand decides how the fingers meet it. held_knuckle_shift (its axis turned toward the finger bases about the palm skin point) is
// one constant; a shaft 4.5 cm in radius with it sits against the knuckles, the knuckle joint is blocked at once and the finger folds over it from the
// middle joint (sim lab: index 6 / 68 / 50 degrees, floating 7 and 14 mm off it); a ball 6 cm in radius is centred on the palm and leaves the thumb hanging
// 7 to 14 mm off it. A hand takes a wide thing deeper or shallower, and a round one toward the thumb, as it fits. GripFitter seats the shape at each
// candidate, closes the five fingers on it (finger_contact, held_curl) and keeps the seat whose fingers float least off it (finger_gaps summed, a penetration
// past 3 mm counted four times) when that is clearly less than the constant's: first the knuckle shift (the shaft's place along the fingers), then, for a
// round prop (a shaft too short to be held along its length), a slide along the hole axis toward the thumb. One seat a step() (about 0.1 ms: a hold
// takes up to eight frames over it, and goes on with the constant's seat meanwhile); once per hold, the shape stays where it is in the palm. A shaft so thin
// that every candidate shift lies past 0.8 radius has one seat and is not searched.
inline constexpr std::array<double,6> held_knuckle_candidates{.020,.025,.030,.035,.040,.045};
inline constexpr std::array<double,2> held_slide_candidates{.008,.016};
inline constexpr double held_slide_round=.02; // a shaft shorter than this half length (m) is round enough to slide
struct GripFit {double shift{held_knuckle_shift},slide{};double plain{},fitted{};unsigned tried{};}; // slide: signed, along the hole axis (palm basis[0])
class GripFitter {
public:
    GripFitter(const HandGeometry& hand,const WorldTrackedPose& palm,const HeldCylinder& c,Vec3 in,Vec3 centre,double along=held_grip_fraction,double roll=0,double lift=0,unsigned mode=0):
        hand_(hand),palm_(palm),c_(c),in_(in),centre_(centre),along_(along),roll_(roll),lift_(lift),mode_(mode) {
        require_hand(hand);require_frame(palm,"Invalid finger palm frame",true);
        toward_thumb_=hand.finger[0].pivot.x>=hand.finger[4].pivot.x?1.:-1.;
        const double radius=held_profiled(c)?std::max(held_radius_floor,profile_radius(c.profile,(along-.5)*2*c.half)):c.radius; // seat_held's own cap: 0.8 of the radius under the palm
        cap_=.8*radius;plain_=std::min(held_knuckle_shift,cap_);
    }
    bool done() const {return done_;}
    const GripFit& result() const {return fit_;}
    // One seat's worth of work; true once the fit is finished.
    bool step() {
        if(done_) return true;
        if(stage_==0) {
            fit_.plain=floating(plain_,0);fit_.fitted=fit_.plain;fit_.tried=1;seen_[seen_count_++]=plain_;stage_=1;
            return false;
        }
        if(stage_==1) {
            while(index_<held_knuckle_candidates.size()) {
                const double shift=std::min(held_knuckle_candidates[index_++],cap_);
                bool duplicate=false;for(unsigned i=0;i<seen_count_;++i) duplicate=duplicate || std::abs(seen_[i]-shift)<1e-9;
                if(duplicate) continue;
                seen_[seen_count_++]=shift;++fit_.tried;
                const double value=floating(shift,0);
                if(value<fit_.fitted && value<.9*fit_.plain) {fit_.fitted=value;fit_.shift=shift;}
                return false;
            }
            stage_=2;index_=0;base_=fit_.fitted;
        }
        if(stage_==2 && c_.half<held_slide_round && index_<held_slide_candidates.size()) {
            const double slide=held_slide_candidates[index_++]*toward_thumb_;
            ++fit_.tried;
            const double value=floating(std::min(fit_.shift,cap_),slide);
            if(value<fit_.fitted && value<.9*base_) {fit_.fitted=value;fit_.slide=slide;}
            return false;
        }
        done_=true;return true;
    }
private:
    double floating(double shift,double slide) const {
        const auto world=[&](Vec3 p) {return palm_.position+palm_.basis[0]*p.x+palm_.basis[1]*p.y+palm_.basis[2]*p.z;};
        const auto seat=seat_held(c_,palm_,in_,centre_,along_,roll_,lift_,shift,slide,mode_);
        FingerIkState state{};const auto wants=finger_wants(true,1.f,0.f);
        const auto contact=finger_contact(hand_,palm_,seat.shapes.data(),seat.count,0,state,held_curl,finger_solve_all,&wants);
        const auto distance=[&](Vec3 w) {double d=1e9;for(unsigned k=0;k<seat.count;++k) d=std::min(d,shape_distance(seat.shapes[k],w));return d;};
        double total=0;
        for(unsigned f=0;f<5;++f) {
            const auto gaps=finger_gaps(hand_.finger[f],hand_.thickness[f],finger_turns_of(hand_.finger[f],{contact.pose[f][0],contact.pose[f][1],contact.pose[f][2]}),world,distance);
            total+=gaps.sum+4*std::max(0.,gaps.penetration-.003);
        }
        return total;
    }
    HandGeometry hand_;WorldTrackedPose palm_;HeldCylinder c_;Vec3 in_,centre_;double along_,roll_,lift_,toward_thumb_{1},cap_{},plain_{},base_{};unsigned mode_{};
    GripFit fit_{};unsigned stage_{},index_{},seen_count_{};std::array<double,8> seen_{};bool done_{};
};
inline GripFit fit_grip(const HandGeometry& hand,const WorldTrackedPose& palm,const HeldCylinder& c,Vec3 in,Vec3 centre,double along=held_grip_fraction,double roll=0,double lift=0,unsigned mode=0) {
    GripFitter fitter(hand,palm,c,in,centre,along,roll,lift,mode);
    while(!fitter.step()) {}
    return fitter.result();
}
// What the fingers close on for a seated prop (the same shape, without the object origin).// What the fingers close on for a seated prop (the same shape, without the object origin).
inline HeldShape seat_shape(const HeldCylinder& c,const WorldTrackedPose& palm,Vec3 in,double along=held_grip_fraction,double roll=0,double lift=0) {return seat_held(c,palm,in,{},along,roll,lift).shape;}

// A flat patch of world surface (a table, a wall, a floor; any body too big to be a prop) from a sphere-cast hit: a slab whose face
// lies on `point`, facing `normal`, 2*half wide, 4 cm thick behind it. The solver treats it like any other shape.
inline HeldShape slab_shape(Vec3 point,Vec3 normal,double half=.12) {
    if(!finite(point) || !finite(normal) || std::abs(length(normal)-1)>.05) throw std::invalid_argument("Invalid surface hit");
    const auto n=unit(normal);const auto t1=unit(cross(n,std::abs(n.x)<.9?Vec3{1,0,0}:Vec3{0,1,0}));
    constexpr double thick=.02;
    return {{point-n*thick,{t1,cross(n,t1),n}},{half,half,thick},0};
}
// The probes that find what each finger could touch before it does: the segment the fully closing fingertip sweeps (its knuckle to the
// tip at curl 0.5, where a long finger reaches farthest toward what the palm holds; the thumb's at curl 1) and one out of the palm along
// its normal, both in world space.
struct FingerSweep {Vec3 from{},to{};};
inline FingerSweep finger_sweep(const HandGeometry& hand,const WorldTrackedPose& palm,unsigned f) {
    require_hand(hand);require_frame(palm,"Invalid finger palm frame",true);
    if(f>=5) throw std::invalid_argument("Invalid finger");
    const auto world=[&](Vec3 p) {return palm.position+palm.basis[0]*p.x+palm.basis[1]*p.y+palm.basis[2]*p.z;};
    const auto& g=hand.finger[f];
    return {world(g.pivot),world(finger_points(g,finger_turn_at(g,f,f?.5:1.))[3])};
}
inline FingerSweep palm_sweep(const HandGeometry& hand,const WorldTrackedPose& palm,double reach=.14) {
    require_hand(hand);require_frame(palm,"Invalid finger palm frame",true);
    const auto in=palm_normal(palm,hand);
    return {palm.position-in*.01,palm.position+in*reach};
}
} // namespace d2vr::rig
