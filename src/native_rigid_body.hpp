#pragma once
#include <atomic>
#include "game_rig.hpp"
#include "physical_grab.hpp"
#include "living_npc_hold.hpp"
#include "prop_hull.hpp"
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
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
    int limb=-1; // v1086: the body's index in a severed limb's own bodies (native_severed_limb_body), held like a prop (bone -1)
    bool group_root{}; // v1089: it heads an attachment group (a severed part's root): its velocity moves the group's one motion (NativeGrab::write)
    const PropShape* hull{}; // its real shape (prop_hull.hpp: its render mesh found by its Havok hull, or the hull alone); nullptr: composite, unreadable or a hull short of the render box (the bounds stand)
    Vec3 contact(Vec3 hand) const {return bounds?bounds->closest(hand):pose.position;}
    double contact_distance(Vec3 hand) const {return length(contact(hand)-hand);}
};
// v1087: the world box of a body's real shape (its hull's vertices at the body pose, grown by the convex radius).
inline NativeGrabBounds hull_world_bounds(const WorldTrackedPose& pose,const PropShape& shape) {
    const auto& w=shape.whole;Vec3 lo{1e300,1e300,1e300},hi{-1e300,-1e300,-1e300};
    for(const auto& v:w.vertices) {const Vec3 b=w.centre+w.axes[0]*v.x+w.axes[1]*v.y+w.axes[2]*v.z;
        const Vec3 p=pose.position+pose.basis[0]*b.x+pose.basis[1]*b.y+pose.basis[2]*b.z;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    const Vec3 r{w.radius,w.radius,w.radius};return {lo-r,hi+r};
}
// v1087: a world point's signed distance to a body's real surface (negative inside).
inline double hull_surface_distance(const WorldTrackedPose& pose,const PropShape& shape,Vec3 point) {
    const Vec3 d=point-pose.position;return shape.whole.distance(shape.whole.to_own({dot(d,pose.basis[0]),dot(d,pose.basis[1]),dot(d,pose.basis[2])}));
}
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
        const auto h=hull_world_bounds(result.pose,*result.hull);const auto& B=*result.bounds;
        if(std::max({h.minimum.x-B.minimum.x,h.minimum.y-B.minimum.y,h.minimum.z-B.minimum.z,B.maximum.x-h.maximum.x,B.maximum.y-h.maximum.y,B.maximum.z-h.maximum.z})>.025) result.hull=nullptr;
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
// v1086 (user, v1085: "a severed leg or arm does not stick like bottles and glasses ... it floats like normal game use"): a severed limb
// (arkSeveredLimb, a skinned model 0x1b0d628) was never a physical candidate - native_rigid_body wants a static-mesh prop of three exact
// classes and native_ragdoll_body a guard's 17 bodies - so every grip on one went to the game's Use, whose HoldObject drives a jointed
// body only softly (headset v1085: 17.6 cm off its target). It is held like a prop instead, by its body nearest the hand, with the sticking
// law. Its class is told by its RTTI name (the vtable is kept once seen); its bodies are read from either physics component the game uses:
// the ragdoll system (0x1c54008: +0x100 -> idList data +0x28, count +0x30) or a single-body component (0x1c745c8 / 0x1b64b68: handle +0xf8).
// Every body record is checked exactly as the guard and prop readers check theirs; anything that does not validate offers nothing, and the
// grip stays the game's Use as before. Counters: severed_limb_reads (ok, refused).
// v1087 (v1086 headset: still Use, the leg "at a distance with a sphere"): each body now carries its real Havok hull (prop_hull), its box is the
// hull's and the body nearest the palm is the one whose SURFACE is nearest; it is seated in the hand like any prop (rig::prop_seat). Static RTTI
// (Dishonored2.exe): .?AVarkSeveredLimb@@ vtable 0x1cbeeb8, 0x1c54008 arkCpntPhysicsSystemEvolved, 0x1c745c8 arkCpntRigidBody, 0x1b64b68
// arkCpntRigidBodyThrowable, 0x1b0d628 arkSkinnedRenderModel. Live (v1086 run): a limb's Use component is arkCpntPickable_Simple 0x1c5a3b8, and
// one limb body bound by the grab (flags 0x800ca, the record checks passed).
inline std::atomic<std::uintptr_t> severed_limb_vtable{0};
inline std::atomic<unsigned long long> severed_limb_reads_ok{0},severed_limb_reads_refused{0};
// v1087: the refusals by the check that refused (the v1086 run could not tell a limb on the floor refused from one out of reach): 0 not registered,
// 1 no model / world / pool, 2 no body component, 3 component not started, 4 no body record passed (dynamic, motion, transform, handles), 5 bodies
// without a readable shape, 6 changed during the read. last_flags: the flags word of the last body record refused (its motion type in bits 0-2).
inline std::array<std::atomic<unsigned long long>,7> severed_limb_why{};
inline std::atomic<std::uint32_t> severed_limb_last_flags{0};
// v1089 (user, v1088 headset: "I can not pick up a severed head with just grip ... the hand and fingers closer, as the other parts"): every read of the head
// refused (v1088 status why[4] 743: no record passed; severed-limb-hold.csv: 28 presses, no pick). From the game's own Havok reflection (Dishonored2.exe
// hkReflect): hknpBody +0x40 motionId, +0x44 flags, +0x70 id, +0x74 nextAttachedBodyId; hknpMotion (0x80 each, world +0x120) +0x00 centerOfMass (world),
// +0x28 firstAttachedBodyId; hknpPhysicsSystemData::bodyCinfoWithAttachment (+0xa0 attachedBody): a system's body may be ATTACHED to another and share its
// motion. A severed part is a clone of the whole cut ragdoll (0x731a20 -> 0x72ffa0: a new system of all its bodies from the cut NPC's data, the arkCpntPhysics-
// SystemEvolved m_severRequester +0x2e0 / m_severBody +0x2f0), and v1088 took only a STANDALONE body (+0x74 == its own id). In that run no part's root was ever
// held: legs gave the calf or foot (12, 13, 15) and arms the forearm (6, 9), taken 13-32 cm from a palm on the thigh or upper arm, and the head, a part of one
// body, gave nothing. Read (static ABI + that run, not a live read): a part's root heads an attachment group, the clone's other bodies attached to it.
// Now every physics component is read; a body that heads its group (its motion's first attached body is itself) is the part's root and may be held (its
// velocity moves the whole group); a body attached to another is never offered. Standalone bodies come first (v1088's legs and arms unchanged); the root is
// held when its part has none (a head; a hand or foot cut at the wrist or ankle). Each refused body is counted by the check it failed (severed_limb_body_why:
// 0 component not started, 1 slot, 2 not dynamic, 3 motion, 4 transform, 5 id, 6 attached to another body, 7 no shape) and logged, once per body and check
// every 2 s (severed_limb_refusals -> severed-limb-hold.csv "refuse"), so the next run proves this or names the check that is left.
inline std::array<std::atomic<unsigned long long>,8> severed_limb_body_why{};
inline std::atomic<unsigned long long> severed_limb_roots{0};
struct LimbRefusal {std::uint32_t entity_id{},handle{},flags{},motion{},id{},next{};std::int64_t first{-1};int index{},check{};std::uintptr_t component_rva{};};
inline std::mutex severed_limb_refusal_mutex;
inline std::vector<LimbRefusal> severed_limb_refusals;   // drained into severed-limb-hold.csv by NativeGrab
inline std::map<std::uint64_t,std::chrono::steady_clock::time_point> severed_limb_refusal_seen;
inline void note_limb_refusal(const LimbRefusal& r) {
    severed_limb_body_why[static_cast<std::size_t>(std::clamp(r.check,0,7))].fetch_add(1,std::memory_order_relaxed);
    const std::uint64_t key=(std::uint64_t{r.entity_id}<<32)|(static_cast<std::uint64_t>(r.index&0xff)<<8)|static_cast<std::uint64_t>(r.check&0xff);
    const auto now=std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(severed_limb_refusal_mutex);
    if(severed_limb_refusal_seen.size()>1024) severed_limb_refusal_seen.clear();
    const auto it=severed_limb_refusal_seen.find(key);
    if(it!=severed_limb_refusal_seen.end() && now-it->second<std::chrono::seconds(2)) return;
    severed_limb_refusal_seen[key]=now;
    if(severed_limb_refusals.size()<256) severed_limb_refusals.push_back(r);
}
// v1089: the velocity the native setter must be given at a motion's centre c for the body origin p to move at v (rigid motion: v_p = v_c + w x (p - c)).
inline Vec3 motion_centre_velocity(Vec3 v_at_point,Vec3 w,Vec3 centre,Vec3 point) {return v_at_point+cross(w,centre-point);}
template<class Read> bool severed_limb_entity(Read& read,std::uintptr_t base,std::uintptr_t entity) {
    using namespace rig;
    const auto table=value<std::uintptr_t>(read,entity);
    if(!table || !*table || *table==base+0x1c5e108) return false;   // arkNPC: never a limb
    if(*table==severed_limb_vtable.load(std::memory_order_relaxed)) return true;
    if(severed_limb_vtable.load(std::memory_order_relaxed)) return false;   // known: another class
    const auto model=value<std::uintptr_t>(read,entity+0xf0);
    if(!model || !typed(read,base,*model,0x1b0d628)) return false;   // only skinned models are worth an RTTI read
    const auto locator=value<std::uintptr_t>(read,*table-8);
    const auto rva=locator && *locator?value<std::uint32_t>(read,*locator+12):std::nullopt;
    char name[24]{};
    if(!rva || !*rva || !read(base+*rva+16,name,sizeof(name)-1) || std::strcmp(name,".?AVarkSeveredLimb@@")!=0) return false;
    severed_limb_vtable.store(*table,std::memory_order_relaxed);return true;
}
// index >= 0: that body (a held one, re-read every pass); index < 0: the body nearest `toward`.
template<class Read> std::optional<NativeRigidBody> native_severed_limb_body(Read& read,std::uintptr_t base,std::uintptr_t entity,int index,Vec3 toward={}) {
    using namespace rig;
    const auto refuse=[&](unsigned why) -> std::optional<NativeRigidBody> {
        severed_limb_reads_refused.fetch_add(1,std::memory_order_relaxed);severed_limb_why[why].fetch_add(1,std::memory_order_relaxed);return {};};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    if(!game || !typed(read,base,*game,0x1c940a8) || !severed_limb_entity(read,base,entity)) return {};
    const auto id=value<std::uint32_t>(read,entity+0xa8);
    if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return refuse(0);
    const auto model=value<std::uintptr_t>(read,entity+0xf0);
    const auto entries=list(read,entity+0x268,256);
    const auto world=value<std::uintptr_t>(read,*game+0x60410);
    if(!model || !*model || !entries || !world || !pointer(*world)) return refuse(1);
    const auto pool=value<std::uintptr_t>(read,*world+0x28);const auto count=value<std::uint32_t>(read,*world+0x30);
    if(!pool || !pointer(*pool) || !count || *count>0x100000) return refuse(1);
    const auto motion_array=value<std::uintptr_t>(read,*world+0x120);   // v1089: hknpMotion[], 0x80 each (+0x28 firstAttachedBodyId)
    // The body handles of EVERY physics component (v1089: v1088 stopped at the first component that gave any): the ragdoll system's list
    // (+0x100 -> data +0x28, count +0x30) and a single-body component's handle (+0xf8). A body's index is its place in this order.
    struct Slot {std::uint32_t handle;std::uintptr_t component;};std::array<Slot,40> handles{};unsigned n=0;
    for(unsigned i=0;i<entries->packed_count && n<handles.size();++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);if(!at || !*at) continue;
        if(typed(read,base,*at,0x1c54008)) {
            const auto list_at=value<std::uintptr_t>(read,*at+0x100);if(!list_at || !pointer(*list_at)) continue;
            const auto data=value<std::uintptr_t>(read,*list_at+0x28);const auto c=value<std::uint32_t>(read,*list_at+0x30);
            if(!data || !pointer(*data) || !c || *c>32) continue;
            for(unsigned k=0;k<*c && n<handles.size();++k) if(const auto h=value<std::uint32_t>(read,*data+k*4);h && *h) handles[n++]={*h,*at};
        } else if(typed(read,base,*at,0x1c745c8) || typed(read,base,*at,0x1b64b68)) {
            if(const auto h=value<std::uint32_t>(read,*at+0xf8);h && *h) handles[n++]={*h,*at};
        }
    }
    if(!n) return refuse(2);
    std::optional<NativeRigidBody> best,root;double best_d=std::numeric_limits<double>::infinity(),root_d=best_d;bool any_record=false,any_started=false;
    for(unsigned k=0;k<n;++k) {
        if(index>=0 && static_cast<unsigned>(index)!=k) continue;
        const auto handle=handles[k].handle;const auto component=handles[k].component;const auto slot=handle&0xffffff;
        LimbRefusal no{*id,handle};no.index=static_cast<int>(k);
        if(const auto table=value<std::uintptr_t>(read,component);table && *table>base) no.component_rva=*table-base;
        const auto identity=started(read,component);
        if(!identity) {no.check=0;note_limb_refusal(no);continue;}
        any_started=true;
        if(slot==0xffffff || slot>=*count) {no.check=1;note_limb_refusal(no);continue;}
        const auto record=*pool+slot*0xb0;
        const auto flags=value<std::uint32_t>(read,record+0x44),motion=value<std::uint32_t>(read,record+0x40);
        const auto transform=value<Transform>(read,record);
        const auto own=value<std::uint32_t>(read,record+0x70),next=value<std::uint32_t>(read,record+0x74);
        no.flags=flags.value_or(0);no.motion=motion.value_or(0);no.id=own.value_or(0);no.next=next.value_or(0);
        bool group=false;
        if(!flags || (*flags&7)!=2) no.check=2;
        else if(!motion || !*motion || *motion>=0x100000) no.check=3;
        else if(!transform || !valid(*transform)) no.check=4;
        else if(own!=handle) no.check=5;
        else if(next!=handle) {   // one of an attachment group: only the body that heads it (its motion's first attached body)
            const auto first=motion_array && pointer(*motion_array)?value<std::uint32_t>(read,*motion_array+static_cast<std::uintptr_t>(*motion)*0x80+0x28):std::nullopt;
            if(first) no.first=*first;
            if(next && *next && first && *first==handle) group=true;else no.check=6;
        }
        if(no.check) {if(flags) severed_limb_last_flags.store(*flags,std::memory_order_relaxed);note_limb_refusal(no);continue;}
        any_record=true;
        const auto& t=transform->f;
        NativeRigidBody body{entity,component,*world,record,*model,*id,handle,*flags,Identity{identity->generation,0},
            {{t[12],t[13],t[14]},{{{t[0],t[1],t[2]},{t[4],t[5],t[6]},{t[8],t[9],t[10]}}}}};
        // v1087: the body's REAL shape (its Havok hull, as a guard's bone has it). A ragdoll part's origin is its joint and the part reaches 10-25 cm
        // past it (live hulls, v1086 run: a calf 46 cm, a forearm 31 cm), so v1086's 7 cm box round the origin sat at the knee or the elbow: the
        // hand on the shin was 12-25 cm from it (v1086 log: phys_mm 121-243). Its box, the nearest body and the seat are the hull's; no readable
        // convex shape: no candidate (counted), the grip stays the game's Use.
        body.hull=prop_hull(read,base,record);
        if(!body.hull) {no.check=7;note_limb_refusal(no);continue;}
        body.bounds=hull_world_bounds(body.pose,*body.hull);body.limb=static_cast<int>(k);body.group_root=group;
        const double d=index>=0?0.:hull_surface_distance(body.pose,*body.hull,toward);
        auto& pick=group?root:best;auto& pick_d=group?root_d:best_d;
        if(!pick || d<pick_d) {pick=body;pick_d=d;}
    }
    if(!best && root) {best=root;severed_limb_roots.fetch_add(1,std::memory_order_relaxed);}   // v1089: a part with no standalone body is held by its root
    if(!best) return refuse(!any_started?3:any_record?5:4);
    if(value<std::uint32_t>(read,entity+0xa8)!=id || value<std::uintptr_t>(read,entity+0xf0)!=model) return refuse(6);
    severed_limb_reads_ok.fetch_add(1,std::memory_order_relaxed);
    return best;
}
// v1291 (user, 2026-11-13: "I cannot pick up a dog body"): the hand reads only the 17-body human ragdoll (native_ragdoll_body below). An NPC whose
// ragdoll has another body count (a hound) offers the hand nothing, so its body must keep the game's own Carry. True when the hand can read this body.
template<class Read> bool hand_ragdoll(Read& read,std::uintptr_t base,std::uintptr_t entity) {
    using namespace rig;
    const auto entries=list(read,entity+0x268,256);
    if(!entries) return false;
    for(unsigned i=0;i<entries->packed_count;++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);
        if(!at || !typed(read,base,*at,0x1c54008)) continue;
        const auto handles=value<std::uintptr_t>(read,*at+0x100);
        return handles && pointer(*handles) && value<std::uint32_t>(read,*handles+0x30)==17u;
    }
    return false;
}
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
    if(body.limb>=0) return native_severed_limb_body(read,base,body.entity,body.limb);   // v1086: the same body of a severed limb
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
