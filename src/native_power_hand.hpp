#pragma once
// Dual wield, power hand (docs: artifacts/claude/dual-wield/fleet/C-power-hand.md).
// Pure maths, no game access. Two helpers:
//  * cross_joint: the RIGHT arm's tracked joint, re-expressed in the LEFT arm model, for the attachment readers (NativeAttachmentPose).
//    Things bound to a joint of the left arm (the slot-1 item entity on weapon_attach_tag = joint 59, the power entity, grenade/mine
//    models, FX that follow those entities) then ride the right hand.
//  * mirror_pose: a left-arm model-space pose mirrored into the right-arm skeleton (the game's authored power hand on the right hand).
// Both arm skeletons have the same 69 joints and parents; every right bind joint is the left one with its model-space rotation
// multiplied by D=diag(1,-1,-1) and x negated (checked on all 68 joints against artifacts/fullbody-assets/pack/skeleton_*_arm.json,
// largest position error 8.6 mm on joint 66).
#include "rig_transform.hpp"
#include <array>
#include <cmath>

namespace d2vr::power_hand {
// left_hand_attachment_jnt and right_hand_attachment_jnt are joint 59 of their skeletons (skeleton_*_arm.json).
inline constexpr unsigned attachment_joint=59;
// A local rotation applied on the right of a joint: out columns = in columns * C. Row-major.
using Rot3=std::array<float,9>;
inline constexpr Rot3 identity_rot{1,0,0, 0,1,0, 0,0,1};
// The right arm's own weapon_attach_tag is rot (0,1,0,0) = 180 degrees about local Y, the left one is identity (md6def player_*arm tags):
// the same weapon frame, mirrored. A left-slot item bound with the left tag therefore needs this on the right hand's joint 59.
inline constexpr Rot3 y180_rot{-1,0,0, 0,1,0, 0,0,-1};
// v776 (headset v775: "the Heart in the right hand is upside down"): the rotation a left-arm bind tag puts between joint 59 and the item it holds (md6def
// player_leftarm tags; the item's component names its tag in m_equippedBindTag, game data). weapon_attach_tag (every gun, crossbow, grenade, mine, razor
// and power; the sword's right-arm tag is y180_rot) is the identity. heart_attach_tag (cpntHeart, the left arm only) is (0,-.7071,-.7071,0), a half
// turn about (0,1,1)/sqrt2: item X = joint -X (the lens faces the player), item Y = joint Z, item Z = joint Y (the vessels point out of the palm).
inline constexpr Rot3 weapon_tag_rot=identity_rot;
inline constexpr Rot3 heart_tag_rot{-1,0,0, 0,0,1, 0,1,0};
// The correction that puts an item bound with `tag` at the other hand as the exact mirror image of how the first hand holds it. The right joint 59 is the
// left one's mirror_joint (a half turn about X and the x negation, i.e. S o J o -I with S = diag(-1,1,1)), so an item at the right hand is the left item
// reflected across the body plane AND across its own sagittal plane, the item-local XZ plane (normal Y): the flat of the sword and the guns, the plane
// through the Heart's lens axis and its vessels (its mesh is most symmetric across it: chamfer 8 mm, against 14-17 mm across X and Z; the pistols, crossbows, sword, sticky grenade and stun mine likewise: 3-8 mm against 8-47). In joint
// coordinates that reflection is a half turn about m = tag*Y: C = 2 m m^T - I. The weapon tag gives y180_rot, as before. The Heart's gives z180: it keeps
// the lens toward the player and the vessels up, where y180 turned it a half turn about its lens axis.
inline Rot3 mirror_correction(const Rot3& tag) {
    const float m[3]{tag[1],tag[4],tag[7]};
    Rot3 c{};
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) c[i*3+j]=2*m[i]*m[j]-(i==j?1.f:0.f);
    return c;
}
inline bool valid_rot(const Rot3& c) {
    for(float v:c) if(!std::isfinite(v)) return false;
    for(unsigned i=0;i<3;++i) for(unsigned j=i;j<3;++j) {
        float d{};for(unsigned k=0;k<3;++k) d+=c[i*3+k]*c[j*3+k];
        if(std::abs(d-(i==j?1.f:0.f))>.01f) return false;
    }
    return c[0]*(c[4]*c[8]-c[5]*c[7])-c[1]*(c[3]*c[8]-c[5]*c[6])+c[2]*(c[3]*c[7]-c[4]*c[6])>.99f;
}
// `source` (a joint in the source arm model's space) as seen from the `to` arm model: the world pose is the same, the coordinates are
// the destination model's. `lanes` supplies the non-spatial lanes (f[3], f[7], f[11], f[15]) of the destination slot. A null or
// identity correction leaves the orientation as it is.
// One composition, both readings: `uncorrected` is the source joint re-expressed in the destination arm (the hand the physical
// controller actually holds), `corrected` additionally applies the item's mirror correction. A caller that reports the hand to the
// weapon binder must use the uncorrected value; recomputing it in a second call (v1014) could fail or race and then leak the y180
// correction into the read, which the render-time gate saw as a 180 degree mismatch.
inline void cross_joint_both(const rig::Transform& source,const rig::ModelFrame& from,const rig::ModelFrame& to,
        const rig::Transform& lanes,const Rot3* correction,rig::Transform& corrected,rig::Transform& uncorrected) {
    std::array<Vec3,3> axes{};
    for(unsigned i=0;i<3;++i) axes[i]=to.model_direction(from.world_direction(rig::column(source,i)));
    const auto p=to.model_point(from.world_point(rig::position(source)));
    const auto write=[&](const std::array<Vec3,3>& a) {
        rig::Transform out=lanes;
        for(unsigned i=0;i<3;++i) {
            out.f[i*4]=static_cast<float>(a[i].x);out.f[i*4+1]=static_cast<float>(a[i].y);out.f[i*4+2]=static_cast<float>(a[i].z);
        }
        out.f[12]=static_cast<float>(p.x);out.f[13]=static_cast<float>(p.y);out.f[14]=static_cast<float>(p.z);
        return out;
    };
    uncorrected=write(axes);
    if(correction) {
        const auto original=axes;
        for(unsigned i=0;i<3;++i)
            axes[i]=original[0]*(*correction)[0*3+i]+original[1]*(*correction)[1*3+i]+original[2]*(*correction)[2*3+i];
    }
    corrected=write(axes);
}
inline rig::Transform cross_joint(const rig::Transform& source,const rig::ModelFrame& from,const rig::ModelFrame& to,
        const rig::Transform& lanes,const Rot3* correction=nullptr) {
    rig::Transform corrected{},uncorrected{};
    cross_joint_both(source,from,to,lanes,correction,corrected,uncorrected);
    return corrected;
}
// One joint between the left and the right arm skeleton. Its own inverse (applying it twice returns the input).
inline rig::Transform mirror_joint(const rig::Transform& t) {
    rig::Transform out=t;
    for(unsigned i=0;i<3;++i) {out.f[i*4+1]=-t.f[i*4+1];out.f[i*4+2]=-t.f[i*4+2];}
    out.f[12]=-t.f[12];
    return out;
}
// A 69-joint model-space pose of one arm as the other arm's skeleton would hold it. Joint 0 (origin) is the identity in both.
inline std::array<rig::Transform,69> mirror_pose(const std::array<rig::Transform,69>& pose) {
    std::array<rig::Transform,69> out=pose;
    for(unsigned joint=1;joint<out.size();++joint) out[joint]=mirror_joint(pose[joint]);
    return out;
}
} // namespace d2vr::power_hand
