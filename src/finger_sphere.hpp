#pragma once
// The sphere grip (user, headset 2026-10-27, v638): a physical prop (book, folder, candlestick, spoon, fork) or a body part the hand holds is wrapped by the
// fingers on ONE invisible ball fixed in the palm for the whole hold, not on the object's own shapes. The object lags and swings behind the hand (motor-held:
// 4 rad/s, 13 mm a frame in the v638 headset run), so the per-frame contact against its real shape moved under the fingers every frame and they twitched;
// a ball in the palm's own frame is the same shape for the solver every frame (finger_contact's memo keys on the shapes in the palm frame) and the curls settle once.
// Build/legacy-finger-grab restores the contact against the object's shapes.
//
// The ball's SIZE is the grip ball (user, headset 2026-11-05): a small ball at the grip place, the width of the thing's thin side under the palm, not the ball
// round its whole box (v642). A bottle's box gave 15 cm, a plank's the 50 cm cap: the hand opened flat round a ball the prop floated inside, which is not a grip.
// The motor seats the prop's own surface on the palm (the grasp point, Task 12), so the ball on the palm skin IS the thing's cross-section under the hand:
// a bottle's 3.5 cm radius, a book's 2 cm thickness, a spoon's 1.5 cm floor. Build/legacy-sphere-grip-whole-object restores the v642 rule.
#include "finger_ik.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
namespace d2vr::rig {
// A thing's ball: 1.5 cm at least (a fist still closes on it), 0.5 m at most (what a hand can still hold out in front of it).
inline constexpr double sphere_grip_min=.015,sphere_grip_max=.5,sphere_grip_unknown=.06;
// The ball's radius from the half extents of the thing held in its own axes: the sphere round its whole box (user, after v641: "the whole object inside the
// ball, so the hand never seems to pass through it"), the tightest ball that holds every corner: a spoon 9 cm, a candlestick 15 cm, a book 14 cm, a head 15 cm.
inline double sphere_grip_radius(Vec3 half) {
    const double a=std::abs(half.x),b=std::abs(half.y),c=std::abs(half.z);
    if(!std::isfinite(a+b+c)) return sphere_grip_unknown;
    return std::clamp(std::sqrt(a*a+b*b+c*c),sphere_grip_min,sphere_grip_max);
}
// A prop: its half extents in its own axes (the world box solved for the drawn axes, held_local_extents), else the world box's own as it is.
inline double sphere_grip_radius(const WorldTrackedPose& pose,const NativeGrabBounds& bounds) {
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(const auto local=held_local_extents(pose.basis,e)) return sphere_grip_radius(*local);
    return sphere_grip_radius(e);
}
// The grip ball (user, headset 2026-11-05: "a small sphere at a grip place ... more native grip"): the thing's thin side is what the fingers wrap, so its
// half is the ball's radius, floored at sphere_grip_min (a fist still closes on it) and capped at sphere_grip_ball_max (a 4 cm radius, 8 cm ball: the widest the
// fingers still wrap; past ~37 mm they only open, research-hand-grip-placement Â§"SphereGrip"). A spoon 1.5 cm, a book 2 cm, a bottle 3.5 cm, a crate 4 cm.
inline constexpr double sphere_grip_ball_max=.03;   // v1037: a 6 cm ball at most - past it the fingers only spread ("palm stretches too much"), which pushed the thumb away from them
inline double sphere_grip_ball_radius(Vec3 half) {
    const double a=std::abs(half.x),b=std::abs(half.y),c=std::abs(half.z);
    if(!std::isfinite(a+b+c)) return sphere_grip_unknown;
    return std::clamp(std::min({a,b,c}),sphere_grip_min,sphere_grip_ball_max);
}
// v1080 real shapes: the thing's own box. From its hull (prop_hull.hpp: the Havok shape's own frame, centre and half extents, exact at any pose)
// when the body has one; else solved from the render bounds as before (held_local_extents_any, the bounds' centre, the body's axes).
struct GripBox {Vec3 centre{};std::array<Vec3,3> axes{};Vec3 half{};};
inline Vec3 body_direction(const WorldTrackedPose& pose,Vec3 v) {return pose.basis[0]*v.x+pose.basis[1]*v.y+pose.basis[2]*v.z;}
inline std::optional<Vec3> held_local_extents_any(const std::array<Vec3,3>& axes,Vec3 e);
inline std::optional<GripBox> grip_box(const WorldTrackedPose& pose,const NativeGrabBounds& bounds,const PropShape* shape=nullptr) {
    if(shape) {const auto& w=shape->whole;return GripBox{pose.position+body_direction(pose,w.centre),{unit(body_direction(pose,w.axes[0])),unit(body_direction(pose,w.axes[1])),
        unit(body_direction(pose,w.axes[2]))},w.half};}
    const auto local=held_local_extents_any(pose.basis,(bounds.maximum-bounds.minimum)*.5);if(!local) return {};
    return GripBox{(bounds.maximum+bounds.minimum)*.5,{unit(pose.basis[0]),unit(pose.basis[1]),unit(pose.basis[2])},*local};
}
// A prop's grip ball from its own local box (the world box as it is when the axes cannot be solved).
inline double sphere_grip_ball_radius(const WorldTrackedPose& pose,const NativeGrabBounds& bounds,const PropShape* shape=nullptr) {
    if(shape) return sphere_grip_ball_radius(shape->whole.half);
    const auto e=(bounds.maximum-bounds.minimum)*.5;
    if(const auto local=held_local_extents(pose.basis,e)) return sphere_grip_ball_radius(*local);
    return sphere_grip_ball_radius(e);
}
// A flat shape for a flat thing (user, headset 2026-11-05: "grip is unnatural on books"). A wide, thin prop - a book, a folder, a plank, a sheet - cannot be
// closed on: it lies on the palm. Its grip shape is the thing's FACE at the skin (finger_ik's slab_shape), so the fingers rest on it as they do on a real book
// instead of closing through its cover. Same cost as the ball: one shape, fixed in the palm frame, never re-read.
// "Wide and thin": at most 3 cm on its thinnest side, at least 4 cm on its middle side, and the thin side at most half the middle one (a spoon's 1.5 cm middle
// is a handle to close on, not a face to lie on; a bottle's 3.5 x 3.5 cross-section is a shaft).
// (v1082: the rule lives in prop_hull.hpp, prop_is_flat, so the grip map and this agree.)
inline bool grip_is_flat(Vec3 half) noexcept {return prop_is_flat(half);}
// A prop's flatness from its own local box (the world box as it is when the axes cannot be solved).
inline bool grip_is_flat(const WorldTrackedPose& pose,const NativeGrabBounds& bounds,const PropShape* shape=nullptr) noexcept {
    try {
        if(shape) return grip_is_flat(shape->whole.half);
        const auto e=(bounds.maximum-bounds.minimum)*.5;
        if(const auto local=held_local_extents(pose.basis,e)) return grip_is_flat(*local);
        return grip_is_flat(e);
    } catch(...) {return false;}
}
// The flat grip itself: a wide thin prop gets NO contact shape at all - the hand is not closing on anything, it lies on the thing's face - and the fingers are
// asked for a relaxed curl (a fraction of the hold's) instead of the closed one. Nothing to collide with every frame, so it is also the cheapest grip: the
// solver has one shape fewer than a ball, and nothing that can twitch (user, headset 2026-11-05: "grip is unnatural on books").
inline constexpr float grip_flat_reach=.35f;
// The surface grip's contact shape (the thing's own, fixed in the palm frame): an ORIENTED box in its own axes - a book, crate, plank or spoon is a box, and
// fitting it as a fat capsule (classify_held's fit_cylinder takes a 22x16x4 cm book within 10%) made the fingers fan round a ball with the thumb far off -
// unless its cross-section is genuinely round (two smaller half extents within 25%, the long one at least 1.5x longer): then the capsule along the long
// axis. A corpse bone is classify_held's limb capsule. Local extents unsolvable (axes 45 degrees to the box): classify_held.
// v1063: held_local_extents, or where it has no answer (two axes at about 45 degrees to the world box: only their sum is known) the least-squares box
// with a small ridge (the ambiguous sides split evenly - a crate stays a cube, not a slab), accepted when it reproduces the world box to 3 mm.
inline std::optional<Vec3> held_local_extents_any(const std::array<Vec3,3>& axes,Vec3 e) {
    if(const auto exact=held_local_extents(axes,e)) return exact;
    double A[3][3];
    for(unsigned c=0;c<3;++c) {const Vec3 a=unit(axes[c]);A[0][c]=std::abs(a.x);A[1][c]=std::abs(a.y);A[2][c]=std::abs(a.z);}
    const double ev[3]{e.x,e.y,e.z},lambda=1e-3;double M[3][3]{},b[3]{};
    for(unsigned i=0;i<3;++i) {for(unsigned j=0;j<3;++j) {for(unsigned k=0;k<3;++k) M[i][j]+=A[k][i]*A[k][j];}M[i][i]+=lambda;for(unsigned k=0;k<3;++k) b[i]+=A[k][i]*ev[k];}
    const double det=M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1])-M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])+M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
    if(!(std::abs(det)>1e-12)) return {};
    double h[3];
    for(unsigned c=0;c<3;++c) {double m[3][3];for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) m[i][j]=j==c?b[i]:M[i][j];
        h[c]=(m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))/det;}
    if(!(h[0]>1e-3 && h[1]>1e-3 && h[2]>1e-3) || !std::isfinite(h[0]+h[1]+h[2])) return {};
    for(unsigned k=0;k<3;++k) if(std::abs(A[k][0]*h[0]+A[k][1]*h[1]+A[k][2]*h[2]-ev[k])>.003) return {};
    return Vec3{h[0],h[1],h[2]};
}
// v1063: a corpse bone is a capsule along its OWN long axis (the longest side of its body's local box: forearm, calf, thigh), centred on that box, its
// radius the visible limb (surface_bone_share of the padded box's cross-section, at most the bone table's radius); a squat box (a head, a hand) is a ball.
// The v641 limb capsule (centred on the joint, laid across the palm, the padded radius) made the v1062 seat push a dragged limb 71 mm off the palm (sim).
inline constexpr double surface_bone_share=.6;
inline std::optional<HeldShape> surface_bone_shape(const WorldTrackedPose& body,const NativeGrabBounds& bounds,int bone) {
    if(bone<0 || bone>=17) return {};
    const auto local=held_local_extents_any(body.basis,(bounds.maximum-bounds.minimum)*.5);if(!local) return {};
    const double v[3]{local->x,local->y,local->z};unsigned a=0;for(unsigned i=1;i<3;++i) if(v[i]>v[a]) a=i;
    const double cross_half=.5*(v[(a+1)%3]+v[(a+2)%3]);
    const double r=std::clamp(cross_half*surface_bone_share,.02,ragdoll_bone_radius[static_cast<std::size_t>(bone)]);
    HeldShape shape{};shape.frame.position=(bounds.maximum+bounds.minimum)*.5;
    shape.frame.basis={unit(body.basis[a]),unit(body.basis[(a+1)%3]),unit(body.basis[(a+2)%3])};
    shape.half={v[a]>=1.3*cross_half?std::max(0.,v[a]-r):0.,0,0};shape.radius=r;
    return shape;
}
// v1080: a convex piece of the real shape as a contact shape: the fingers meet its exact surface; `half` is its own box (the thumb's face rules).
inline HeldShape hull_grip_shape(const WorldTrackedPose& body,const ConvexHull& h) {
    HeldShape s{};s.frame.position=body.position+body_direction(body,h.centre);
    for(unsigned k=0;k<3;++k) s.frame.basis[k]=unit(body_direction(body,h.axes[k]));s.half=h.half;s.hull=&h;return s;
}
// All the pieces of a prop's real shape (a long thing's slabs: its handle, its shaft, its bowl), at most `room`; the count.
inline unsigned prop_grip_shapes(const WorldTrackedPose& body,const PropShape& shape,HeldShape* out,unsigned room) {
    unsigned n=0;for(const auto& p:shape.pieces) if(n<room) out[n++]=hull_grip_shape(body,p);return n;
}
inline HeldShape surface_grip_shape(const WorldTrackedPose& body,const std::optional<NativeGrabBounds>& bounds,int bone,const WorldTrackedPose& palm,const PropShape* real=nullptr) {
    // v1080: the real shape. A prop: its first piece (prop_grip_shapes gives them all). A corpse bone's Havok capsule: its exact axis and length, the
    // visible limb's radius as before (surface_bone_share of the padded capsule).
    if(real && bone<0) return hull_grip_shape(body,real->pieces.front());
    if(real && bone>=0 && bone<17 && !real->whole.solid) {const auto* hull=&real->whole;HeldShape s{};s.frame.position=body.position+body_direction(body,hull->centre+hull->axes[0]*((hull->a.x+hull->b.x)*.5));
        for(unsigned k=0;k<3;++k) s.frame.basis[k]=unit(body_direction(body,hull->axes[k]));
        s.radius=std::clamp(hull->radius*surface_bone_share,.02,ragdoll_bone_radius[static_cast<std::size_t>(bone)]);s.half={std::abs(hull->b.x-hull->a.x)*.5,0,0};return s;}
    if(bone>=0 && bounds && finite(bounds->minimum) && finite(bounds->maximum))
        if(const auto limb=surface_bone_shape(body,*bounds,bone)) return *limb;
    if(bone<0 && bounds && finite(bounds->minimum) && finite(bounds->maximum)) {
        const auto local=held_local_extents_any(body.basis,(bounds->maximum-bounds->minimum)*.5);
        if(local) {
            const auto h=held_clamped_extents(*local);const double v[3]{h.x,h.y,h.z};
            unsigned a=0;for(unsigned i=1;i<3;++i) if(v[i]>v[a]) a=i;
            const double p=v[(a+1)%3],q=v[(a+2)%3],wide=std::max(p,q);
            HeldShape shape{};shape.frame.position=(bounds->maximum+bounds->minimum)*.5;
            if(std::abs(p-q)<=.25*wide && v[a]>=1.5*wide) { // round: a capsule along the long axis (half = the straight part)
                const double r=(p+q)*.5;
                shape.frame.basis={unit(body.basis[a]),unit(body.basis[(a+1)%3]),unit(body.basis[(a+2)%3])};shape.half={std::max(0.,v[a]-r),0,0};shape.radius=r;
            } else {shape.frame.basis={unit(body.basis[0]),unit(body.basis[1]),unit(body.basis[2])};shape.half=h;shape.radius=0;}
            return shape;
        }
    }
    return classify_held(body,bounds,bone,palm);
}
// v1062 seat: how far a prop's centre stands off the palm skin when its surface lies on it - the box's support along the palm direction (its local half
// extents projected on it; the world box's own when the axes cannot be solved). The drawn hand then moves it the last millimetres (held_standoff).
inline double surface_seat_depth(const WorldTrackedPose& pose,const NativeGrabBounds& bounds,Vec3 direction,const PropShape* shape=nullptr) {
    const auto e=(bounds.maximum-bounds.minimum)*.5;const Vec3 n=unit(direction);
    if(shape) {const auto b=*grip_box(pose,bounds,shape);return shape->whole.support({dot(n,b.axes[0])*-1.,dot(n,b.axes[1])*-1.,dot(n,b.axes[2])*-1.});} // from the box centre: the side toward the palm
    if(const auto h=held_local_extents_any(pose.basis,e))
        return std::abs(h->x*dot(unit(pose.basis[0]),n))+std::abs(h->y*dot(unit(pose.basis[1]),n))+std::abs(h->z*dot(unit(pose.basis[2]),n));
    return std::abs(e.x*n.x)+std::abs(e.y*n.y)+std::abs(e.z*n.z);
}
// v1063 natural seat (user 2026-11-07: "native like VR hands grabbing"): a grab turns the thing a little and lays it in the hand the way a hand takes it, as the
// reference games' hand poses do. A long thing (its longest side at least 1.5x the others: a plank, a candlestick, a bottle, a spoon) turns its long axis
// across the palm (a bar grip) when it lies within 50 degrees of that; then the face most turned toward the palm turns flat onto it when within 35 degrees
// (a palm flat on a book, a crate side). The grasped spot is kept: the palm skin point stays where it was on the thing (its offsets along the face, kept
// seat_edge_margin inside the edges so the fingers have something to close over), and the face lies on the skin. Turns are about the skin point.
// Unsolvable local box (axes at 45 degrees to it): no answer (the caller keeps surface_seat_depth).
inline constexpr double seat_bar_snap=.873,seat_face_snap=.611,seat_edge_margin=.02,seat_bar_ratio=1.5,seat_crease_depth=.045;
// v1080 pinch (user, headset v1071: "a thin spoon is held in a full fist"): a bar whose cross-section where it is taken is thin (at most
// seat_pinch_thin half thickness and seat_pinch_wide half width: a spoon's or a fork's handle, a pen, a screwdriver's shaft - not its handle, not a
// bottle's neck) is not laid in the palm to be wrapped; it is pinched at the fingertips: its centre line where the index and middle pads meet the
// opposed thumb, seat_pinch_reach along the fingers from the palm skin and seat_pinch_lift off it. The section is the real one at the grasped spot
// when the thing has a real shape (prop_hull.hpp: the slab there), else the box's.
inline constexpr double seat_pinch_thin=prop_pinch_thin,seat_pinch_wide=prop_pinch_wide,seat_pinch_reach=.085,seat_pinch_lift=.035;
inline std::optional<WorldTrackedPose> natural_seat(const WorldTrackedPose& pose,const NativeGrabBounds& bounds,Vec3 skin,Vec3 forward,Vec3 normal,const PropShape* hull=nullptr,bool* pinched=nullptr) {
    if(pinched) *pinched=false;
    const auto box=grip_box(pose,bounds,hull);
    if(!box || !finite(skin) || !finite(forward) || !finite(normal) || length(normal)<1e-9) return {};
    const Vec3 n=unit(normal),fw0=forward-n*dot(forward,n);if(length(fw0)<1e-9) return {};
    const Vec3 fw=unit(fw0),across=cross(n,fw);
    const std::array<Vec3,3> old=box->axes;
    auto ax=old;const double h[3]{box->half.x,box->half.y,box->half.z};
    const auto spin=[](Vec3 v,Vec3 k,double a) {return v*std::cos(a)+cross(k,v)*std::sin(a)+k*(dot(k,v)*(1-std::cos(a)));};
    const auto turn=[&](Vec3 from,Vec3 to,double limit) {
        const double a=std::acos(std::clamp(dot(from,to),-1.,1.));const Vec3 k=cross(from,to);
        if(a<1e-6) return true;
        if(a>limit || length(k)<1e-9) return false;
        const Vec3 u=unit(k);for(auto& v:ax) v=spin(v,u,a);return true;
    };
    unsigned L=0;for(unsigned i=1;i<3;++i) if(h[i]>h[L]) L=i;
    const bool bar=h[L]>=seat_bar_ratio*std::max(h[(L+1)%3],h[(L+2)%3]);
    if(bar) turn(ax[L],dot(ax[L],across)>=0?across:across*-1.,seat_bar_snap);
    unsigned face=bar?(L+1)%3:0;double sign=1,best=-2;
    for(unsigned i=0;i<3;++i) for(const double sg:{1.,-1.}) {if(bar && i==L) continue;const double v=-dot(ax[i]*sg,n);if(v>best) {best=v;face=i;sign=sg;}}
    // v1067 (user: "plates or boxes sometimes right behind my hand"): a face too far from the palm to turn to it is no seat. Putting the grip on
    // it anyway slid the grasped spot onto that face unturned: a plate taken by its rim then stood half behind the hand. The support seat stays.
    if(!turn(ax[face]*sign,n*-1.,seat_face_snap)) return {};
    // Narrow along the fingers (at most seat_crease_depth either side: a bar, a book's spine, a bottle): the fingers can close round it, so it lies in the palm
    // crease a knuckle shift toward the finger roots (seat_held's), not at the palm's centre behind them (they curled in front of it and never touched it).
    const double along_fingers=h[0]*std::abs(dot(ax[0],fw))+h[1]*std::abs(dot(ax[1],fw))+h[2]*std::abs(dot(ax[2],fw));
    Vec3 grip=along_fingers<=seat_crease_depth?skin+fw*held_knuckle_shift:skin; // (a shift that keeps its front behind the knuckles wrapped worse in the lab: v1063)
    const Vec3 centre=box->centre;
    double q[3];for(unsigned j=0;j<3;++j) q[j]=dot(skin-centre,old[j]);   // the grasped spot in the thing's own frame, placed at the grip point
    for(unsigned j=0;j<3;++j) {const double room=std::max(0.,h[j]-std::min(seat_edge_margin,.5*h[j]));q[j]=std::clamp(q[j],-room,room);}
    // v1082 grip map: a bar is taken where a hand closes round it (prop_hull.hpp prop_grip: a spoon by its handle, a leg by its shin), the
    // grasped spot slid into that span, seat_edge_margin inside its ends
    if(bar && hull && L==0 && hull->grip.span) {const double m=std::min(seat_edge_margin,.5*(hull->grip.x1-hull->grip.x0));q[0]=std::clamp(q[0],hull->grip.x0+m,hull->grip.x1-m);}
    q[face]=sign*h[face];   // on the face toward the palm
    // v1082 edge pinch (lab: 0 of 5 fingers on a note laid on the palm - they curl past its edges into the air): a thin flat thing (a note, a letter, a
    // photo, a poster: flat and at most seat_pinch_thin half thick, its big face toward the palm) is pinched by its edge at the fingertips, its plane
    // along the palm, the rest of it beyond the fingers: its edge nearest the wrist (along the fingers) at the pinch point, half a margin in.
    {unsigned t=0;for(unsigned i=1;i<3;++i) if(h[i]<h[t]) t=i;
     if(!(hull && hull->grip.span) && face==t && h[t]<=seat_pinch_thin && prop_is_flat({h[0],h[1],h[2]})) {   // (a long note is a "bar" too: no handle span)
         unsigned a=t;double along=-1;for(unsigned i=0;i<3;++i) if(i!=t && std::abs(dot(ax[i],fw))>along) {along=std::abs(dot(ax[i],fw));a=i;}
         const double s=dot(ax[a],fw)>=0?1.:-1.;
         q[a]=-s*std::max(0.,h[a]-std::min(.5*seat_edge_margin,.5*h[a]));q[t]=0;
         grip=skin+fw*seat_pinch_reach+n*seat_pinch_lift;if(pinched) *pinched=true;
     }}
    if(bar) {
        const unsigned u=(L+1)%3,v=(L+2)%3;double hu=h[u],hv=h[v],cu=0,cv=0;
        if(hull && L==0) if(const auto s=hull->section(q[0])) {hu=s->hy;hv=s->hz;cu=s->y;cv=s->z;}
        if(std::min(hu,hv)<=seat_pinch_thin && std::max(hu,hv)<=seat_pinch_wide) {q[u]=cu;q[v]=cv;grip=skin+fw*seat_pinch_reach+n*seat_pinch_lift;if(pinched) *pinched=true;} // its centre line at the pinch
    }
    const Vec3 c=grip-(ax[0]*q[0]+ax[1]*q[1]+ax[2]*q[2]);
    const auto R=[&](Vec3 v) {return ax[0]*dot(v,old[0])+ax[1]*dot(v,old[1])+ax[2]*dot(v,old[2]);};
    WorldTrackedPose out{};out.position=c+R(pose.position-centre);
    for(unsigned k=0;k<3;++k) out.basis[k]=R(pose.basis[k]);
    return out;
}
// A ragdoll bone (a corpse's limb, held for a drag): the visible limb, about 0.6 of its padded collision capsule (sim v640: a dragged wrist's 5-6 cm
// capsule, or its box's x half that can be the forearm's half LENGTH, gave the 6 cm cap: a fist round a ball, not a wrist); never above the bone box's thinnest half side.
inline constexpr double sphere_grip_bone_share=.6;
inline double sphere_grip_radius_of_bone(int bone,const std::optional<NativeGrabBounds>& bounds) {
    if(bone<0 || bone>=17) return sphere_grip_unknown;
    double r=ragdoll_bone_radius[static_cast<std::size_t>(bone)]*sphere_grip_bone_share;
    if(bounds && finite(bounds->minimum) && finite(bounds->maximum)) {
        const auto h=(bounds->maximum-bounds->minimum)*.5;const double thin=std::min({h.x,h.y,h.z});if(thin>.005) r=std::min(r,thin);}
    return std::clamp(r,sphere_grip_min,sphere_grip_max);
}
// The ball in the palm: a sphere (half 0) of radius r lying on the palm skin, in the palm's own axes, so the same shape in the palm frame every frame.
inline HeldShape sphere_grip_shape(const WorldTrackedPose& palm,const HandGeometry& hand,double radius) {
    HeldShape s{};s.frame.position=palm.position+palm_normal(palm,hand)*(radius+held_palm_depth);s.frame.basis=palm.basis;s.radius=radius;
    require_shape(s);
    return s;
}
// One hold's ball, decided when the hold starts and never again: `radius` is the first reading kept for that entity (a changing box or a lagging pose does not move it).
struct SphereGripLatch {
    std::uintptr_t entity{};double radius{};
    template<class Decide> double radius_for(std::uintptr_t held,Decide decide) {if(held!=entity || !(radius>0)) {entity=held;radius=decide();}return radius;}
    void clear() {entity=0;radius=0;}
};
}
