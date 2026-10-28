#pragma once
#include "finger_ik.hpp"

namespace d2vr::rig {
struct NoteTablet {
    Vec3 centre{},right{},up{},front{}; // centre of the readable face, metres
    HeldShape shape{};
};
inline constexpr double note_tablet_width=.45,note_tablet_height=.253125,note_tablet_thickness=.004;

// The left hand holds the left edge: fingers behind the sheet, thumb in front. Everything is palm-local, including roll.
inline NoteTablet note_tablet(const HandGeometry& hand,const WorldTrackedPose& palm,double width=note_tablet_width,double height=note_tablet_height) {
    require_hand(hand);require_frame(palm,"Invalid note palm",true);
    if(!std::isfinite(width) || !std::isfinite(height) || width<.1 || width>1 || height<.1 || height>1)
        throw std::invalid_argument("Invalid note dimensions");
    const auto n=unit(hand.palm_in);
    Vec3 reach{};for(unsigned f=1;f<5;++f) reach=reach+hand.finger[f].bone[0];
    const auto right=unit(reach-n*dot(reach,n)),up=unit(cross(n,right));
    if(length(right)<.99 || length(up)<.99) throw std::invalid_argument("Degenerate note grip");
    const auto w=[&](Vec3 v) {return palm.basis[0]*v.x+palm.basis[1]*v.y+palm.basis[2]*v.z;};
    double edge=0;for(unsigned f=1;f<5;++f) edge+=dot(hand.finger[f].pivot,right)*.25;
    // Keep the top corner above the thumb even at minimum HUD scale, with opposition space for the front pad and back fingers.
    const Vec3 centre=right*(width*.5+edge)+up*.025+n*(held_palm_depth+.025+note_tablet_thickness);
    NoteTablet tablet{};tablet.centre=palm.position+w(centre);tablet.right=w(right);tablet.up=w(up);tablet.front=w(n);
    tablet.shape.frame={tablet.centre-tablet.front*(note_tablet_thickness*.5),{tablet.right,tablet.up,tablet.front}};
    tablet.shape.half={width*.5,height*.5,note_tablet_thickness*.5};require_shape(tablet.shape);
    return tablet;
}

// Close the native bind fan into a relaxed pinch; only this note pose uses coupled MCP adduction.
inline HandGeometry note_tablet_hand(const HandGeometry& hand) {
    require_hand(hand);auto grip=hand;const auto n=unit(hand.palm_in);
    Vec3 along{};for(unsigned f=1;f<5;++f) along=along+unit(hand.finger[f].bone[0]);
    along=unit(along-n*dot(along,n));
    constexpr double pad[5]{.004,-.002,-.0005,.001,-.0005}; // fit native skinned pads, not just the generic bone arcs
    for(unsigned f=0;f<5;++f) {
        grip.thickness[f]+=pad[f];
        if(!f) continue;
        auto& finger=grip.finger[f];const auto b=unit(finger.bone[0]);
        const auto lateral=along-b*dot(along,b)-n*dot(along,n);
        finger.hinge[0]=unit(cross(b,n+lateral*3));
    }
    return grip;
}

inline FingerAngles note_tablet_fingers(const HandGeometry& hand,const WorldTrackedPose& palm,const NoteTablet& tablet,FingerIkState& state,double dt) {
    const auto grip=note_tablet_hand(hand);const auto wants=finger_wants(true,1.f,1.f);
    // A 4 mm slab can be crossed between contact samples. The long fingers must approach its back, never escape through its front.
    auto back_stop=tablet.shape;back_stop.half.z=.2;back_stop.fingers_only=true;
    back_stop.frame.position=tablet.centre+tablet.front*(back_stop.half.z-note_tablet_thickness);
    const HeldShape shapes[]{tablet.shape,back_stop};
    const auto contact=finger_contact(grip,palm,shapes,2,dt,state,held_curl,finger_solve_all,&wants);
    return finger_turns(grip,contact,wants);
}
}
