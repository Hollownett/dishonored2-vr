#pragma once
#include <atomic>
#include "game_rig.hpp"
#include "physical_grab.hpp"
#include "living_npc_hold.hpp"
#include "prop_hull.hpp"
#include <cstring>
#include <limits>
namespace d2vr {
struct NativeGrabBounds {
    Vec3 minimum{},maximum{};
    Vec3 closest(Vec3 p) const {
        return {std::clamp(p.x,minimum.x,maximum.x),std::clamp(p.y,minimum.y,maximum.y),std::clamp(p.z,minimum.z,maximum.z)};
    }
};
struct NativeRigidBody {
    std::uintptr_t entity{},component{},world{},record{},model{};
    std::uint32_t entity_id{},handle{},flags{};
    rig::Identity identity{};
    WorldTrackedPose pose{};
    std::optional<NativeGrabBounds> bounds{};
    int bone=-1; // ragdoll bone of a guard's body (native_ragdoll_body), -1 for a standalone prop
    const PropShape* hull{}; // its real shape (prop_hull.hpp: its render mesh found by its Havok hull, or the hull alone); nullptr: composite, unreadable or a hull short of the render box (the bounds stand)
    Vec3 contact(Vec3 hand) const {return bounds?bounds->closest(hand):pose.position;}
    double contact_distance(Vec3 hand) const {return length(contact(hand)-hand);}
};
// A physical candidate as actual bounds, with one identity per entity. A corpse's
// nearest bone can change as the hand moves. Legacy priority metadata does not rank pickups.
inline std::uint64_t palm_body_id(const NativeRigidBody& b) noexcept {return b.entity_id?b.entity_id:static_cast<std::uint64_t>(b.entity);}
inline PalmTarget palm_body_target(const NativeRigidBody& b,const GrabTuning& tuning) {
    PalmTarget t;t.id=palm_body_id(b);t.kind=b.bone>=0?PalmKind::bone:PalmKind::prop;t.priority=tuning.priority(t.kind,b.bone);
    if(b.bounds) {t.minimum=b.bounds->minimum;t.maximum=b.bounds->maximum;} else t.minimum=t.maximum=b.pose.position;
    return t;
}
// Why native_rigid_body refused a body, or (bounds_*) why it fell back from its model bounds to its origin point (the body is still returned).
enum class BodyReject : unsigned char {ok,not_prop,pickable,no_component,not_dynamic,no_model,bounds_size,bounds_centre};
inline const char* body_reject_name(BodyReject r) noexcept {
    switch(r) {case BodyReject::ok: return "ok";case BodyReject::not_prop: return "not_prop";case BodyReject::pickable: return "pickable";
        case BodyReject::no_component: return "no_component";case BodyReject::not_dynamic: return "not_dynamic";case BodyReject::no_model: return "no_model";
        case BodyReject::bounds_size: return "bounds_size";case BodyReject::bounds_centre: return "bounds_centre";}
    return "unknown";
}
// The model AABB is used for the grab distance when it is sane: finite, ordered, a diagonal of 1 mm to 3 m, the rendered origin within 0.25 m
// of the body and the body within 0.5 m of the box. G3 thin props (plan Task 7): when the ONLY failing limit is the rendered-origin offset and
// the body's pose lies inside the box (closest(pose)==pose within 1 mm, e.g. an off-centre paper stack), the box is accepted anyway.
inline bool accept_bounds(const NativeGrabBounds& bounds,Vec3 rendered_origin,Vec3 pose,BodyReject* why=nullptr) noexcept {
    const auto fail=[&](BodyReject r) {if(why) *why=r;return false;};
    if(!finite(bounds.minimum) || !finite(bounds.maximum) || !finite(rendered_origin) || !finite(pose) ||
       bounds.minimum.x>bounds.maximum.x || bounds.minimum.y>bounds.maximum.y || bounds.minimum.z>bounds.maximum.z) return fail(BodyReject::bounds_size);
    const double diagonal=length(bounds.maximum-bounds.minimum);
    if(!(diagonal>.001) || !(diagonal<=3)) return fail(BodyReject::bounds_size);
    const double gap=length(bounds.closest(pose)-pose);
    if(!(gap<=.5)) return fail(BodyReject::bounds_centre);
    // thin props: the offset alone is forgiven when the pose is inside the box AND the rendered centre is inside its own box (within 1 cm): an
    // off-centre pivot. A rendered centre outside the box is a stale render after the body moved (native_rigid_body_check), never accepted.
    const double centre_gap=length(bounds.closest(rendered_origin)-rendered_origin);
    if(!(length(rendered_origin-pose)<=.25) && (!(gap<=.001) || !(centre_gap<=.01))) return fail(BodyReject::bounds_centre);
    if(why) *why=BodyReject::ok;
    return true;
}
// v788 (user, headset v796: "the free hand must always pick things up, whatever the other holds"): with dual wield the hands hold bottles and
// glasses physically too, never the game's HoldObject (which locks the sword's EquipChange and takes no second item). The 2026-10-13 failure
// ("fought the native carry") was the same squeeze also firing the game's Use; the grip-alone decision now gives such a squeeze to the grab only.
// Off (user, 2026-11-01: the HoldObject pickup works; only its effect on the hands changes): knob build/grab-bottles, with dual wield; a thrown one still gets the game's thrown state (thrown_prop, 4c94e0).
inline std::atomic<bool> grab_carryables{false};
// v788: the entity is a native carryable (owns arkCpntPickable_Simple 0x1c5a3b8: a bottle, a glass), the game's HoldObject item.
template<class Read> bool native_carryable(Read& read,std::uintptr_t base,std::uintptr_t entity) {
    if(!entity || !rig::pointer(entity)) return false;
    const auto entries=rig::list(read,entity+0x268,256);if(!entries) return false;
    for(unsigned i=0;i<entries->packed_count;++i) if(const auto at=rig::value<std::uintptr_t>(read,entries->data+i*8);at && rig::typed(read,base,*at,0x1c5a3b8)) return true;
    return false;
}
// carryable: also native carryables (bottles, glasses), for a hand that pushes them without grabbing (native_hand_contact.hpp).
// why (G3, plan Task 7): the reason a body was refused (or ok), for the grab_focus status block's thin-prop line. Never changes the result.
template<class Read> std::optional<NativeRigidBody> native_rigid_body(Read& read,std::uintptr_t base,std::uintptr_t entity,bool carryable=false,BodyReject* why=nullptr) {
    using namespace rig;
    BodyReject scratch{};BodyReject& reason=why?*why:scratch;reason=BodyReject::not_prop;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    // Exact pinned RTTI: base idEntity can own a standalone dynamic prop too.
    if(!game || !typed(read,base,*game,0x1c940a8) ||
       !(typed(read,base,entity,0x1c886a8) || typed(read,base,entity,0x1c87d58) || typed(read,base,entity,0x1c5de18)) || value<std::uintptr_t>(read,*game+0x3130b0)==entity) return {}; // 0x1c5de18 arkMovable
    const auto id=value<std::uint32_t>(read,entity+0xa8);
    if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
    reason=BodyReject::no_model;
    const auto model=value<std::uintptr_t>(read,entity+0xf0);
    if(!model || !typed(read,base,*model,0x1b0dd88)) return {}; // Rendered static-mesh prop, not an invisible physics helper.
    reason=BodyReject::no_component;
    const auto entries=list(read,entity+0x268,256);
    if(!entries) return {};
    std::uintptr_t component{};
    for(unsigned i=0;i<entries->packed_count;++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);
        // Native carryables (bottles, glasses: arkCpntPickable_Simple 0x1c5a3b8) belong to the game's own Use (HoldObject), which
        // the mod puts in the tracked hand (native_held_object.hpp). Grabbed physically they fought the native carry, floated or
        // were lost (sim 2026-10-13: picked, moved 75 frames, ownership lost).
        if(!carryable && !grab_carryables.load(std::memory_order_relaxed) && at && typed(read,base,*at,0x1c5a3b8)) {reason=BodyReject::pickable;return {};}
        if(at && (typed(read,base,*at,0x1c745c8) || typed(read,base,*at,0x1b64b68))) {
            if(component) return {};component=*at;
        }
    }
    const auto identity=started(read,component);const auto handle=value<std::uint32_t>(read,component+0xf8);
    const auto table=value<std::uintptr_t>(read,component);
    if(!identity || !handle || !table || value<std::uintptr_t>(read,component+0x10)!=entity ||
       value<std::uintptr_t>(read,component+0xe8)!=base+(*table==base+0x1c745c8?0x1c74798:0x1b64d38)) return {};
    const auto world=value<std::uintptr_t>(read,*game+0x60410);
    if(!world || !pointer(*world)) return {};
    const auto pool=value<std::uintptr_t>(read,*world+0x28);const auto count=value<std::uint32_t>(read,*world+0x30);
    const auto index=*handle&0xffffff;
    if(!pool || !pointer(*pool) || !count || *count>0x100000 || index==0xffffff || index>=*count) return {};
    const auto record=*pool+index*0xb0;
    const auto flags=value<std::uint32_t>(read,record+0x44),motion=value<std::uint32_t>(read,record+0x40);
    const auto transform=value<Transform>(read,record);
    if(flags && (*flags&7)!=2) {reason=BodyReject::not_dynamic;return {};}   // reported, never forced (a settled body that is not dynamic)
    // Only a standalone dynamic primary body: no static/keyframed/compound,
    // native carry parent, or existing temporary activation lease.
    // Bit 8 is the native active-body flag, set by c85dc0/c88dd0 when waking
    // a previously sleeping prop. It is independent of motion type bits 1/2/4.
    reason=BodyReject::not_dynamic;
    if(!flags || (*flags&7)!=2 || !motion || !*motion || *motion>=0x100000 || !transform || !valid(*transform) ||
       value<std::uint32_t>(read,record+0x70)!=handle || value<std::uint32_t>(read,record+0x74)!=handle ||
       value<std::uintptr_t>(read,component+0xc8)!=0 || value<unsigned char>(read,component+0xd2)!=0) return {};
    if(*table==base+0x1b64b68) {
        const auto bits=value<std::uint32_t>(read,component+0x438);
        if(!bits || (*bits&8)) return {};
    }
    const auto& t=transform->f;
    NativeRigidBody result{entity,component,*world,record,*model,*id,*handle,*flags,*identity,
        {{t[12],t[13],t[14]},{{{t[0],t[1],t[2]},{t[4],t[5],t[6]},{t[8],t[9],t[10]}}}}};
    // Static-model 268de0/268b80 transforms its actual mesh bounds to this
    // world AABB; 1bd8b0 reads the same cache for the native model centre.
    const auto box=value<std::array<float,8>>(read,*model+0x1840);
    const auto origin=value<std::array<float,3>>(read,*model+0x1860);
    BodyReject bounds_result=BodyReject::ok;
    if(box && origin) {
        const NativeGrabBounds bounds{{(*box)[0],(*box)[1],(*box)[2]},{(*box)[4],(*box)[5],(*box)[6]}};
        const Vec3 rendered{(*origin)[0],(*origin)[1],(*origin)[2]};
        if(accept_bounds(bounds,rendered,result.pose.position,&bounds_result)) result.bounds=bounds;
    }
    if(value<Identity>(read,component+0x18)!=identity || value<std::uint32_t>(read,component+0xf8)!=handle ||
       value<std::uint32_t>(read,entity+0xa8)!=id || value<std::uintptr_t>(read,entity+0xf0)!=model || value<std::uint32_t>(read,record+0x70)!=handle ||
       value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) {reason=BodyReject::not_prop;return {};}
    result.hull=prop_hull(read,base,record);
    // v1080: a Havok hull alone (no render-mesh entry) is the grip only when it covers the render box (within 2.5 cm a side): a spoon's hull is its bowl
    // alone, and a grip on it would leave the handle in the air.
    if(result.hull && !result.hull->mesh && result.bounds) {
        const auto& w=result.hull->whole;Vec3 lo{1e300,1e300,1e300},hi{-1e300,-1e300,-1e300};
        for(const auto& v:w.vertices) {const Vec3 b=w.centre+w.axes[0]*v.x+w.axes[1]*v.y+w.axes[2]*v.z;
            const Vec3 p=result.pose.position+result.pose.basis[0]*b.x+result.pose.basis[1]*b.y+result.pose.basis[2]*b.z;
            lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
        const auto& B=*result.bounds;const double r=w.radius;
        if(std::max({lo.x-r-B.minimum.x,lo.y-r-B.minimum.y,lo.z-r-B.minimum.z,B.maximum.x-hi.x-r,B.maximum.y-hi.y-r,B.maximum.z-hi.z-r})>.025) result.hull=nullptr;
    }
    reason=bounds_result;
    return result;
}
// Guard bodies (sim v399, read live): every arkNPC (0x1c5e108) owns 17 consecutive hknp ragdoll bodies. Their handles sit at
// arkCpntPhysicsSystemEvolved (0x1c54008) +0x100 -> +0x120. Alive they are keyframed to the animation (flags&7 == 6); dead or
// unconscious (entity+0xd1 bits 0x08/0x20) they are dynamic (flags&7 == 2), the motion type the grab motor already drives.
// Bone order: 0 pelvis 1 spine 2 chest 3 neck 4 head | 5-7 left upper arm, forearm, hand | 8-10 right arm | 11-13 left thigh,
// calf, foot | 14-16 right leg (mapped from a standing guard's bodies).
inline constexpr std::array<double,17> ragdoll_bone_radius{.14,.13,.16,.07,.11,.06,.05,.06,.06,.05,.06,.08,.06,.06,.08,.06,.06};
// The hands drag a body by a forearm, wrist, calf or ankle; the chest, collar and head are the game's own shoulder carry
// (user, headset 2026-10-13: "torso = carry, limb = drag").
inline constexpr bool ragdoll_grab_bone(int bone) {
    return bone==6 || bone==7 || bone==9 || bone==10 || bone==12 || bone==13 || bone==15 || bone==16;
}
// Dead or unconscious guards can also be held by the collar (chest 2, neck 3): a full fist on the collar is a physical hold, while a grip alone
// stays the game's shoulder carry. Kept apart from ragdoll_grab_bone so the limb-versus-torso wake classification does not change.
inline constexpr bool ragdoll_hold_bone(int bone) {return ragdoll_grab_bone(bone) || bone==2 || bone==3;}
template<class Read> std::optional<NativeRigidBody> native_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,int bone) {
    using namespace rig;
    if(bone<0 || bone>=17) return {};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) || !typed(read,base,entity,0x1c5e108)) return {};
    const auto id=value<std::uint32_t>(read,entity+0xa8);
    if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
    // A living guard's bodies follow his animation (keyframed: refused below) unless he is ragdolling (yanked, v425): then
    // they are dynamic like a corpse's and the hand can hold one until he gets up.
    const auto model=value<std::uintptr_t>(read,entity+0xf0);
    const auto entries=list(read,entity+0x268,256);
    if(!model || !*model || !entries) return {};
    std::uintptr_t component{};
    for(unsigned i=0;i<entries->packed_count && !component;++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);
        if(at && typed(read,base,*at,0x1c54008)) component=*at;
    }
    if(!component) return {};
    const auto identity=started(read,component);const auto handles=value<std::uintptr_t>(read,component+0x100);
    if(!identity || !handles || !pointer(*handles)) return {};
    // An idList of the 17 body handles: data +0x28, count +0x30. The data is often the inline buffer at +0x120, but a corpse
    // woken again (and a yanked guard) can hold a heap array (sim v429): reading +0x120 inline gave garbage.
    const auto data=value<std::uintptr_t>(read,*handles+0x28);
    if(!data || !pointer(*data) || value<std::uint32_t>(read,*handles+0x30)!=17u) return {};
    const auto handle=value<std::uint32_t>(read,*data+bone*4);
    const auto world=value<std::uintptr_t>(read,*game+0x60410);
    if(!handle || !*handle || !world || !pointer(*world)) return {};
    const auto pool=value<std::uintptr_t>(read,*world+0x28);const auto count=value<std::uint32_t>(read,*world+0x30);
    const auto index=*handle&0xffffff;
    if(!pool || !pointer(*pool) || !count || *count>0x100000 || index==0xffffff || index>=*count) return {};
    const auto record=*pool+index*0xb0;
    const auto flags=value<std::uint32_t>(read,record+0x44),motion=value<std::uint32_t>(read,record+0x40);
    const auto transform=value<Transform>(read,record);
    if(!flags || (*flags&7)!=2 || !motion || !*motion || *motion>=0x100000 || !transform || !valid(*transform) ||
       value<std::uint32_t>(read,record+0x70)!=handle || value<std::uint32_t>(read,record+0x74)!=handle) return {};
    const auto& t=transform->f;
    // The physics system's flag word changes while its ragdoll is pushed (sim v401 lost the held wrist one frame after binding):
    // a ragdoll target is identified by the component's generation only.
    const Identity stable{identity->generation,0};
    NativeRigidBody result{entity,component,*world,record,*model,*id,*handle,*flags,stable,
        {{t[12],t[13],t[14]},{{{t[0],t[1],t[2]},{t[4],t[5],t[6]},{t[8],t[9],t[10]}}}}};
    const auto r=ragdoll_bone_radius[static_cast<std::size_t>(bone)];
    result.bounds=NativeGrabBounds{result.pose.position-Vec3{r,r,r},result.pose.position+Vec3{r,r,r}};result.bone=bone;
    const auto again=started(read,component);
    if(!again || again->generation!=identity->generation || value<std::uintptr_t>(read,*handles+0x28)!=data || value<std::uint32_t>(read,*data+bone*4)!=handle ||
       value<std::uint32_t>(read,record+0x70)!=handle || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
    result.hull=prop_hull(read,base,record);
    return result;
}
// G6 (plan Task 14): a LIVING NPC's bone, for the hand to hold him by the collar or an arm. His bodies are keyframed to the animation ((flags&7)==6):
// only the position and the radius are read; never a NativeRigidBody, so nothing can ever write a velocity to it. Living: !(npc+0xd1 & 0x28).
template<class Read> std::optional<LivingBone> living_bone(Read& read,std::uintptr_t base,std::uintptr_t entity,int bone) {
    using namespace rig;
    if(bone<0 || bone>=17) return {};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) || !typed(read,base,entity,0x1c5e108)) return {};
    const auto state=value<unsigned char>(read,entity+0xd1);
    if(!state || (*state&0x28)) return {};
    const auto id=value<std::uint32_t>(read,entity+0xa8);
    if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
    const auto entries=list(read,entity+0x268,256);
    if(!entries) return {};
    std::uintptr_t component{};
    for(unsigned i=0;i<entries->packed_count && !component;++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);
        if(at && typed(read,base,*at,0x1c54008)) component=*at;
    }
    if(!component) return {};
    const auto handles=value<std::uintptr_t>(read,component+0x100);
    if(!handles || !pointer(*handles)) return {};
    const auto data=value<std::uintptr_t>(read,*handles+0x28);
    if(!data || !pointer(*data) || value<std::uint32_t>(read,*handles+0x30)!=17u) return {};
    const auto handle=value<std::uint32_t>(read,*data+bone*4);
    const auto world=value<std::uintptr_t>(read,*game+0x60410);
    if(!handle || !*handle || !world || !pointer(*world)) return {};
    const auto pool=value<std::uintptr_t>(read,*world+0x28);const auto count=value<std::uint32_t>(read,*world+0x30);
    const auto index=*handle&0xffffff;
    if(!pool || !pointer(*pool) || !count || *count>0x100000 || index==0xffffff || index>=*count) return {};
    const auto record=*pool+index*0xb0;
    const auto flags=value<std::uint32_t>(read,record+0x44);const auto transform=value<Transform>(read,record);
    if(!flags || (*flags&7)!=6 || !transform || !valid(*transform) || value<std::uint32_t>(read,record+0x70)!=handle) return {};
    const Vec3 at{transform->f[12],transform->f[13],transform->f[14]};
    if(!finite(at) || value<std::uint32_t>(read,*data+bone*4)!=handle) return {};
    return LivingBone{entity,*id,bone,at,ragdoll_bone_radius[static_cast<std::size_t>(bone)]};
}
// The living bones a hand may hold (spec G6): the collar (chest 2, neck 3), the upper arms (5, 8) and the forearms (6, 9).
inline constexpr bool living_hold_bone(int bone) {return bone==2 || bone==3 || bone==5 || bone==6 || bone==8 || bone==9;}
// The grabbable limb of this body nearest the hand (with allow_collar: the chest and neck too, dead or unconscious guards only).
template<class Read> std::optional<NativeRigidBody> nearest_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,Vec3 hand,bool allow_collar=false) {
    std::optional<NativeRigidBody> best;
    for(int bone=0;bone<17;++bone) {
        if(allow_collar?!ragdoll_hold_bone(bone):!ragdoll_grab_bone(bone)) continue;
        const auto body=native_ragdoll_body(read,base,entity,bone);
        if(body && (!best || body->contact_distance(hand)<best->contact_distance(hand))) best=body;
    }
    return best;
}
// G2/G3 (plan Task 7): the bone the palm picks among `allowed` bones (the shared palm cost; 0.45 m gate from the palm point, ruling H10; the
// per-bone priority from the tuning: torso and pelvis 1.1, limbs 1.0, so a palm on a forearm still prefers the forearm). Ties: the lower bone.
// With no bone inside the gate it returns the allowed bone nearest the palm point, as the v611 pick did: discovery keeps the body cached while the
// hand approaches, and the selection (pick_by_palm, the same gate) still refuses it.
template<class Read,class Allowed> std::optional<NativeRigidBody> palm_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,
        const PalmProbe& probe,const GrabTuning& tuning,Allowed allowed) {
    std::optional<NativeRigidBody> best,nearest;double best_cost=std::numeric_limits<double>::infinity(),nearest_gap=best_cost;
    const Vec3 point=palm_point(probe);
    for(int bone=0;bone<17;++bone) {
        if(!allowed(bone)) continue;
        const auto body=native_ragdoll_body(read,base,entity,bone);
        if(!body) continue;
        const auto s=palm_cost(probe,palm_body_target(*body,tuning),tuning.hover_radius_m,grab_acquire_distance);
        if(s && s->cost<best_cost) {best_cost=s->cost;best=body;}
        const double gap=body->contact_distance(point);
        if(gap<nearest_gap) {nearest_gap=gap;nearest=body;}
    }
    return best?best:nearest;
}
// The grabbable limb the palm picks (a yank's hold: the same limb set as nearest_ragdoll_body, ranked by the palm cost).
template<class Read> std::optional<NativeRigidBody> nearest_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,const PalmProbe& probe,
        const GrabTuning& tuning,bool allow_collar=false) {
    return palm_ragdoll_body(read,base,entity,probe,tuning,[&](int bone) {return allow_collar?ragdoll_hold_bone(bone):ragdoll_grab_bone(bone);});
}
// Every ragdoll bone with a known radius is a corpse candidate (spec G3, ruling H10: the head included), so reaching for the body's middle
// catches the torso, pelvis or a thigh. The collar bones (chest 2, neck 3) keep their rule: only with allow_collar (build/collar-grab and a fist).
inline bool corpse_palm_bone(int bone,bool allow_collar) noexcept {
    return bone>=0 && bone<17 && ragdoll_bone_radius[static_cast<std::size_t>(bone)]>0 && ((bone!=2 && bone!=3) || allow_collar);
}
// Only a dead (entity+0xd1 bit 0x08) or unconscious (0x20) guard may be pushed by a hand (C2). Bit 0x80 alone (an awake guard ragdolling
// after a yank, native_ragdoll_body accepts him) is not enough: he gets up and fights.
template<class Read> bool ragdoll_pushable_state(Read& read,std::uintptr_t entity) {
    const auto state=rig::value<unsigned char>(read,entity+0xd1);
    return state && (*state&0x28)!=0;
}
// Discovery branch for a guard: a candidate only when he is dead or unconscious (an awake guard, even one ragdolling after a yank, gives none;
// living guards are the pull-and-stagger path). The collar bones are offered only when allow_collar (the build/collar-grab knob).
// The Vec3 form is the v611 pick (8 limbs + collar, nearest): build/legacy-palm-select, or no valid palm probe this frame.
template<class Read> std::optional<NativeRigidBody> corpse_candidate(Read& read,std::uintptr_t base,std::uintptr_t entity,Vec3 hand,bool allow_collar) {
    if(!ragdoll_pushable_state(read,entity)) return {};
    return nearest_ragdoll_body(read,base,entity,hand,allow_collar);
}
// G3 (plan Task 7): all bones with a known radius (corpse_palm_bone), picked by the palm cost.
template<class Read> std::optional<NativeRigidBody> corpse_candidate(Read& read,std::uintptr_t base,std::uintptr_t entity,const PalmProbe& probe,
        const GrabTuning& tuning,bool allow_collar) {
    if(!probe.valid || !ragdoll_pushable_state(read,entity)) return {};
    return palm_ragdoll_body(read,base,entity,probe,tuning,[&](int bone) {return corpse_palm_bone(bone,allow_collar);});
}
// A body the hands may push is also not carried (component +0xc8 native parent, +0xd2 temporary lease: the check of native_rigid_body for a
// prop) and belongs to this guard the way NativeWorldTrace::inspect_hit_body proves a hit body's owner: record +0x90 userdata -> +8 component ->
// +0x10 entity. A recycled handle that now serves another entity is refused. (Kept out of native_ragdoll_body so the grab path is unchanged.)
template<class Read> bool ragdoll_unclaimed(Read& read,std::uintptr_t component) {
    std::array<unsigned char,12> carry{};
    if(!read(component+0xc8,carry.data(),carry.size())) return false;
    std::uintptr_t parent;std::memcpy(&parent,carry.data(),8);
    return !parent && !carry[10];
}
template<class Read> bool ragdoll_owned(Read& read,const NativeRigidBody& body) {
    using namespace rig;
    const auto userdata=value<std::uintptr_t>(read,body.record+0x90);
    if(!userdata || !pointer(*userdata)) return false;
    const auto component=value<std::uintptr_t>(read,*userdata+8);
    if(!component || !pointer(*component)) return false;
    const auto entity=value<std::uintptr_t>(read,*component+0x10);
    return entity && *entity==body.entity;
}
template<class Read> bool ragdoll_pushable_body(Read& read,const NativeRigidBody& body) {
    return ragdoll_unclaimed(read,body.component) && ragdoll_owned(read,body);
}
// The body of this guard nearest the hand, all 17 bones, for a hand that pushes (dead or unconscious only, each body dynamic, owned and not
// carried). The torso counts here: unlike a drag, a push needs no handle. Cheap for an awake guard: the state byte, then the first body is refused.
template<class Read> std::optional<NativeRigidBody> pushable_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,Vec3 hand) {
    if(!finite(hand) || !ragdoll_pushable_state(read,entity)) return {};
    std::optional<NativeRigidBody> best;
    for(int bone=0;bone<17;++bone) {
        const auto body=native_ragdoll_body(read,base,entity,bone);
        if(!body && bone==0) return {}; // bodies follow the animation, or this is no guard: skip the remaining reads
        if(body && bone==0 && !ragdoll_unclaimed(read,body->component)) return {}; // carried or leased: all of him
        if(body && ragdoll_owned(read,*body) && (!best || body->contact_distance(hand)<best->contact_distance(hand))) best=body;
    }
    return best;
}
// Re-read a grab target of either kind, keeping its bone.
template<class Read> std::optional<NativeRigidBody> native_grab_body(Read& read,std::uintptr_t base,const NativeRigidBody& body) {
    return body.bone<0?native_rigid_body(read,base,body.entity):native_ragdoll_body(read,base,body.entity,body.bone);
}
enum class GrabSearchScope { Normal,ControlledFixture };
template<class Read> std::optional<NativeRigidBody> nearest_grab_body(Read& read,std::uintptr_t base,Vec3 point,double radius,
    std::vector<NativeRigidBody>* candidates=nullptr,GrabSearchScope scope=GrabSearchScope::Normal) {
    const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !rig::typed(read,base,*game,0x1c940a8) || !finite(point) || !std::isfinite(radius) || radius<=0 ||
       radius>(scope==GrabSearchScope::ControlledFixture?4:2)) return {};
    std::array<std::uintptr_t,0x3fff> registry{};
    if(!read(*game+0x3130b0,registry.data(),sizeof(registry))) return {};
    std::optional<NativeRigidBody> best;double distance=radius;
    for(unsigned index=1;index<registry.size();++index) {
        const auto at=registry[index];if(!rig::pointer(at)) continue;
        const auto id=rig::value<std::uint32_t>(read,at+0xa8);
        if(!id || (*id>>16)!=index) continue;
        const auto pos=rig::value<std::array<float,3>>(read,at+0x300);
        if(!pos || !finite(Vec3{(*pos)[0],(*pos)[1],(*pos)[2]}) || length(Vec3{(*pos)[0],(*pos)[1],(*pos)[2]}-point)>radius+3) continue;
        const auto body=native_rigid_body(read,base,at);
        if(body) {
            const auto d=body->contact_distance(point);
            if(candidates && d<=radius && candidates->size()<64) candidates->push_back(*body);
            if(d<distance) {best=body;distance=d;}
        }
    }
    return best;
}
} // namespace d2vr
