#pragma once
#include "game_rig.hpp"
#include "physical_grab.hpp"
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
    Vec3 contact(Vec3 hand) const {return bounds?bounds->closest(hand):pose.position;}
    double contact_distance(Vec3 hand) const {return length(contact(hand)-hand);}
};
// carryable: also native carryables (bottles, glasses), for a hand that pushes them without grabbing (native_hand_contact.hpp).
template<class Read> std::optional<NativeRigidBody> native_rigid_body(Read& read,std::uintptr_t base,std::uintptr_t entity,bool carryable=false) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    // Exact pinned RTTI: base idEntity can own a standalone dynamic prop too.
    if(!game || !typed(read,base,*game,0x1c940a8) ||
       !(typed(read,base,entity,0x1c886a8) || typed(read,base,entity,0x1c87d58) || typed(read,base,entity,0x1c5de18)) || value<std::uintptr_t>(read,*game+0x3130b0)==entity) return {}; // 0x1c5de18 arkMovable
    const auto id=value<std::uint32_t>(read,entity+0xa8);
    if(!id || *id==0x3fff0010 || (*id>>16)>=0x3fff || value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
    const auto model=value<std::uintptr_t>(read,entity+0xf0);
    if(!model || !typed(read,base,*model,0x1b0dd88)) return {}; // Rendered static-mesh prop, not an invisible physics helper.
    const auto entries=list(read,entity+0x268,256);
    if(!entries) return {};
    std::uintptr_t component{};
    for(unsigned i=0;i<entries->packed_count;++i) {
        const auto at=value<std::uintptr_t>(read,entries->data+i*8);
        // Native carryables (bottles, glasses: arkCpntPickable_Simple 0x1c5a3b8) belong to the game's own Use (HoldObject), which
        // the mod puts in the tracked hand (native_held_object.hpp). Grabbed physically they fought the native carry, floated or
        // were lost (sim 2026-10-13: picked, moved 75 frames, ownership lost).
        if(!carryable && at && typed(read,base,*at,0x1c5a3b8)) return {};
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
    // Only a standalone dynamic primary body: no static/keyframed/compound,
    // native carry parent, or existing temporary activation lease.
    // Bit 8 is the native active-body flag, set by c85dc0/c88dd0 when waking
    // a previously sleeping prop. It is independent of motion type bits 1/2/4.
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
    if(box && origin) {
        const NativeGrabBounds bounds{{(*box)[0],(*box)[1],(*box)[2]},{(*box)[4],(*box)[5],(*box)[6]}};
        const Vec3 rendered{(*origin)[0],(*origin)[1],(*origin)[2]};
        if(finite(bounds.minimum) && finite(bounds.maximum) && finite(rendered) &&
           bounds.minimum.x<=bounds.maximum.x && bounds.minimum.y<=bounds.maximum.y && bounds.minimum.z<=bounds.maximum.z &&
           length(bounds.maximum-bounds.minimum)>.001 && length(bounds.maximum-bounds.minimum)<=3 &&
           length(rendered-result.pose.position)<=.25 && length(bounds.closest(result.pose.position)-result.pose.position)<=.5)
            result.bounds=bounds;
    }
    if(value<Identity>(read,component+0x18)!=identity || value<std::uint32_t>(read,component+0xf8)!=handle ||
       value<std::uint32_t>(read,entity+0xa8)!=id || value<std::uintptr_t>(read,entity+0xf0)!=model || value<std::uint32_t>(read,record+0x70)!=handle ||
       value<std::uintptr_t>(read,*game+0x3130b0+(*id>>16)*8)!=entity) return {};
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
    return result;
}
// The grabbable limb of this body nearest the hand.
template<class Read> std::optional<NativeRigidBody> nearest_ragdoll_body(Read& read,std::uintptr_t base,std::uintptr_t entity,Vec3 hand) {
    std::optional<NativeRigidBody> best;
    for(int bone=0;bone<17;++bone) {
        if(!ragdoll_grab_bone(bone)) continue;
        const auto body=native_ragdoll_body(read,base,entity,bone);
        if(body && (!best || body->contact_distance(hand)<best->contact_distance(hand))) best=body;
        if(!body && bone==3) break; // not a guard body the hands can drag: skip the remaining reads
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
