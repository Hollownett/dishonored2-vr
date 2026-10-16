#pragma once
#include "native_melee.hpp"
#include "native_slash_speed.hpp"
#include "native_action_camera.hpp"
#include <atomic>
#include <mutex>

namespace d2vr {
struct SwordContactHand {bool held{};std::uint64_t epoch{},tick{};};
class SwordContactPublication {
    mutable std::mutex mutex_;
    SwordContactHand hand_;
public:
    void publish(bool held,std::uint64_t epoch,std::uint64_t tick) noexcept {
        std::lock_guard<std::mutex> guard(mutex_);hand_={held,epoch,tick};
    }
    std::optional<SwordContactHand> current(std::uint64_t now) const noexcept {
        std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);if(!guard.owns_lock())return {};
        if(!hand_.held || !hand_.epoch || !hand_.tick || now<hand_.tick || now-hand_.tick>100)return {};
        return hand_;
    }
};
struct NativeContactScene {
    std::uintptr_t pool{};
    std::array<std::uintptr_t,native_action_camera_states.size()> states{};
};
template<class Read> std::optional<NativeContactScene> native_contact_scene(Read& read,const NativePlayerStateLease& owner) {
    using namespace rig;
    if(!(native_player_state_lease(read,owner.base,owner.machine)==std::optional<NativePlayerStateLease>{owner}))return {};
    const auto pool=value<std::uintptr_t>(read,owner.machine+0x90);if(!pool || !pointer(*pool))return {};
    NativeContactScene scene{};scene.pool=*pool;
    for(unsigned pass=0;pass<2;++pass) {
        for(std::size_t index=0;index<native_action_camera_states.size();++index) {
            const auto entry=native_action_camera_states[index];if(entry.combat)continue;
            const auto state=value<std::uintptr_t>(read,*pool+0x10+entry.index*8);if(!state)return {};
            if(pass && *state!=scene.states[index])return {};
            scene.states[index]=*state;if(!*state)continue;
            if(!typed(read,owner.base,*state,entry.vtable) || value<std::uint32_t>(read,*state+8)!=entry.index ||
               value<std::uintptr_t>(read,*state+0x10)!=owner.machine || value<std::uint8_t>(read,*state+0x20)!=0)return {};
        }
        if(value<std::uintptr_t>(read,owner.machine+0x90)!=pool ||
           !(native_player_state_lease(read,owner.base,owner.machine)==std::optional<NativePlayerStateLease>{owner}))return {};
    }
    return scene;
}
struct NativeContactEquipment {
    NativeMelee::Sword sword;
    std::uintptr_t inventory{},item{},item_decl{},weapon_decl{};
    rig::Identity inventory_identity{},weapon_identity{};
    bool operator==(const NativeContactEquipment& other) const noexcept {
        return sword.player==other.sword.player && sword.entity==other.sword.entity && sword.weapon==other.sword.weapon && sword.decl==other.sword.decl &&
            sword.player_id==other.sword.player_id && sword.sword_id==other.sword.sword_id && inventory==other.inventory && item==other.item &&
            item_decl==other.item_decl && weapon_decl==other.weapon_decl && inventory_identity==other.inventory_identity && weapon_identity==other.weapon_identity;
    }
};
template<class Read> std::optional<NativeContactEquipment> native_contact_equipment(Read& read,const NativePlayerStateLease& owner) {
    using namespace rig;
    const auto sword=NativeMelee::sword(owner.base,read);if(!sword || sword->player!=owner.player || sword->player_id!=owner.spawn)return {};
    NativeContactEquipment equipment{};equipment.sword=*sword;
    const auto components=list(read,owner.player+0x268,4096);if(!components)return {};
    for(std::uint32_t index=0;index<components->packed_count;++index) {
        const auto component=value<std::uintptr_t>(read,components->data+index*8ull);if(!component)return {};
        if(typed(read,owner.base,*component,NativeMelee::inventory_vtable_rva)) {
            if(equipment.inventory)return {};equipment.inventory=*component;
        }
    }
    const auto inventory_identity=started(read,equipment.inventory),weapon_identity=started(read,sword->weapon);
    const auto item=value<std::uintptr_t>(read,equipment.inventory+0x68);
    const auto items=list(read,equipment.inventory+0x38,4096);
    const auto item_decl=item?value<std::uintptr_t>(read,*item):std::nullopt;
    const auto weapon_decl=value<std::uintptr_t>(read,sword->weapon+8);
    if(!inventory_identity || !weapon_identity || value<std::uintptr_t>(read,equipment.inventory+0x10)!=owner.player ||
       !item || !pointer(*item) || !items || *item<items->data || (*item-items->data)%0x38 || (*item-items->data)/0x38>=items->packed_count ||
       !item_decl || !typed(read,owner.base,*item_decl,0x1b9ea48) ||
       !weapon_decl || !typed(read,owner.base,*weapon_decl,0x1b9d3c8) || value<std::uint32_t>(read,*item+8)!=sword->sword_id ||
       value<std::uintptr_t>(read,*item+0x10)!=sword->weapon || value<std::uintptr_t>(read,*item+0x18)!=weapon_decl ||
       value<std::uintptr_t>(read,*weapon_decl+0x120)!=sword->decl ||
       value<std::uint32_t>(read,sword->entity+0xac)!=owner.spawn || value<std::uintptr_t>(read,owner.game+0x3130b0+(sword->sword_id>>16)*8ull)!=sword->entity)return {};
    equipment.inventory_identity=*inventory_identity;equipment.weapon_identity=*weapon_identity;
    equipment.item=*item;equipment.item_decl=*item_decl;equipment.weapon_decl=*weapon_decl;return equipment;
}
struct OwnedSwordContact {
    MeleeHitRequest hit;
    NativePlayerStateLease owner;
    NativeContactEquipment equipment;
    std::uint32_t victim_spawn{};
    std::uintptr_t pool{};
    std::uint64_t pose_epoch{};
    bool stealth_strike{}; // deliberate, from behind, on the kill sensor's target (sword_contact_policy.hpp)
    bool native_offer{}; // the native kill sensor targeted this victim when the blade touched him (render thread)
};
template<class Read> bool native_contact_victim(Read& read,const NativePlayerStateLease& owner,std::uintptr_t victim,std::uint32_t spawn) {
    if(!spawn || spawn==NativeMelee::invalid_id || (spawn>>16)>=0x3fff || victim==owner.player)return false;
    const auto slot=owner.game+0x3130b0+(spawn>>16)*8ull;
    return rig::value<std::uint32_t>(read,victim+0xa8)==spawn && rig::value<std::uintptr_t>(read,slot)==victim &&
        (rig::typed(read,owner.base,victim,NativeMelee::npc_vtable_rva) || rig::typed(read,owner.base,victim,NativeMelee::swarm_agent_vtable_rva));
}
template<class Read> std::optional<OwnedSwordContact> capture_sword_contact(Read& read,std::uintptr_t base,std::uintptr_t machine,
        const MeleeHitRequest& hit,std::uint64_t pose_epoch,std::uint64_t now) {
    if(!pose_epoch || !hit.tick || now<hit.tick || now-hit.tick>100)return {};
    const auto owner=native_player_state_lease(read,base,machine);if(!owner)return {};
    const auto scene=native_contact_scene(read,*owner);if(!scene)return {};
    const auto equipment=native_contact_equipment(read,*owner);if(!equipment)return {};
    const auto spawn=rig::value<std::uint32_t>(read,hit.victim+0xa8);
    if(!spawn || !native_contact_victim(read,*owner,hit.victim,*spawn))return {};
    const auto final_equipment=native_contact_equipment(read,*owner);const auto final_scene=native_contact_scene(read,*owner);
    if(!final_equipment || !(*final_equipment==*equipment) || !final_scene || final_scene->pool!=scene->pool ||
       !native_contact_victim(read,*owner,hit.victim,*spawn))return {};
    return OwnedSwordContact{hit,*owner,*equipment,*spawn,scene->pool,pose_epoch};
}
template<class Read> bool sword_contact_current(Read& read,const OwnedSwordContact& contact,std::uint64_t pose_epoch,std::uint64_t now) {
    if(contact.pose_epoch!=pose_epoch)return false;
    const auto current=capture_sword_contact(read,contact.owner.base,contact.owner.machine,contact.hit,pose_epoch,now);
    return current && current->owner==contact.owner && current->equipment==contact.equipment && current->victim_spawn==contact.victim_spawn && current->pool==contact.pool;
}
// Player FSM state `index` (vtable `vtable`) of the leased machine is running (pool +0x10 + index*8, +0x20 active byte).
template<class Read> bool native_player_state_active(Read& read,const NativePlayerStateLease& owner,unsigned index,std::uintptr_t vtable) {
    using namespace rig;
    const auto pool=value<std::uintptr_t>(read,owner.machine+0x90);if(!pool || !pointer(*pool))return false;
    const auto state=value<std::uintptr_t>(read,*pool+0x10+index*8ull);
    return state && typed(read,owner.base,*state,vtable) && value<std::uint32_t>(read,*state+8)==index &&
        value<std::uintptr_t>(read,*state+0x10)==owner.machine && value<std::uint8_t>(read,*state+0x20)==1;
}
inline constexpr unsigned native_state_assassination=13,native_state_choke=41;
inline constexpr std::uintptr_t native_state_assassination_vtable=0x1c83468,native_state_choke_vtable=0x1c77c18;
// A held contact (older than the 100 ms capture window) still belongs to the same player, sword and victim.
template<class Read> bool sword_contact_owned(Read& read,const OwnedSwordContact& contact) {
    const auto owner=native_player_state_lease(read,contact.owner.base,contact.owner.machine);
    if(!owner || !(*owner==contact.owner))return false;
    const auto equipment=native_contact_equipment(read,*owner);
    return equipment && *equipment==contact.equipment && native_contact_victim(read,*owner,contact.hit.victim,contact.victim_spawn);
}
}
