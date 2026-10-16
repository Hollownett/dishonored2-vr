#pragma once
#include "native_equipment_ownership.hpp"
#include "native_carry_snapshot.hpp"
#include "native_held_placement.hpp"

namespace d2vr {
class NativeLeftWeaponSocket {
public:
    struct Availability {
        std::uint64_t epoch{},tick{};
        bool valid{},focused{},physical_held{},physical_reserved{},native_object{};
        std::uint64_t placement_serial{};
    };
    struct Lease {
        NativeEquipmentOwnership equipment;
        rig::NativeCarrySnapshot carry;
        std::uintptr_t base{};
        std::uint64_t epoch{};
    };
private:
    template<class Read> struct Reader {
        Read& input;
        static bool cpp(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
            try {return read(address,output,size);}catch(...) {return false;}
        }
        static bool safe(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
            __try {return cpp(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
        bool operator()(std::uintptr_t address,void* output,std::size_t size) {return safe(input,address,output,size);}
    };
    static bool available(const Availability& value,std::uint64_t now) noexcept {
        return value.valid && value.focused && value.epoch && value.tick && now>=value.tick && now-value.tick<=100 &&
            !value.physical_held && !value.physical_reserved && !value.native_object;
    }
    static bool carry_allowed(const rig::NativeCarrySnapshot& carry,const Availability& availability,std::uint64_t now) {
        if(carry.active[2])return false;
        if(carry.active[0])return NativeHeldPlacement::process_owner().current_driven_right(
            availability.placement_serial,availability.epoch,carry,now);
        return (carry.hands()&1u)==0;
    }
public:
    template<class Read> static bool current(Read& input,const Lease& lease,const Availability& availability,std::uint64_t now) {
        try {
            if(!available(availability,now) || availability.epoch!=lease.epoch)return false;
            Reader<Read> read{input};
            const auto carry=rig::native_carry_current(read,lease.base,lease.carry);
            if(!carry || !carry_allowed(*carry,availability,now))return false;
            if(!native_equipment_current(read,lease.equipment))return false;
            const auto after=rig::native_carry_current(read,lease.base,*carry);
            return after && after==carry && carry_allowed(*after,availability,now);
        }catch(...) {return false;}
    }
    template<class Read> static std::optional<Lease> capture(std::uintptr_t base,Read& input,
            const rig::NativeArmBindingSnapshot& snapshot,const Availability& availability,std::uint64_t now) {
        try {
            if(!available(availability,now) || !rig::native_arm_pair_owned(snapshot.arms))return {};
            Reader<Read> read{input};Lease result{};result.base=base;result.epoch=availability.epoch;
            const auto& arm=snapshot.arms[0];auto& equipment=result.equipment;
            std::size_t total{};std::uintptr_t native_binding{};
            auto guarded=[&](std::uintptr_t address,void* output,std::size_t size) {
                if(!size || size>32768 || address<0x10000 || address>=0x0000800000000000-size ||
                   equipment.guards.size()>=512 || total+size>131072 || !read(address,output,size))return false;
                if(native_binding && address==native_binding+0x84 && size==4)return true;
                const auto* bytes=static_cast<const unsigned char*>(output);
                for(const auto& guard:equipment.guards)if(guard.address==address && guard.bytes.size()==size)
                    return std::equal(guard.bytes.begin(),guard.bytes.end(),bytes);
                equipment.guards.push_back({address,{bytes,bytes+size}});total+=size;return true;
            };
            const auto game=rig::value<std::uintptr_t>(guarded,base+0x39a5290);
            const auto inventory=rig::value<std::uintptr_t>(guarded,arm.body+0x60);
            const auto item=inventory?rig::value<std::uintptr_t>(guarded,*inventory+0x70):std::nullopt;
            const auto spawn=item?rig::value<std::uint32_t>(guarded,*item+8):std::nullopt;
            if(!game || !item || !spawn || !*spawn || *spawn==0x3fff0010 || (*spawn>>16)>=0x3fff)return {};
            const auto entity=rig::value<std::uintptr_t>(guarded,*game+0x3130b0+(*spawn>>16)*8ull);
            const auto binding=entity?rig::value<std::uintptr_t>(guarded,*entity+0x358):std::nullopt;
            native_binding=binding.value_or(0);
            const auto candidate=binding?native_attachment_candidate(guarded,base,snapshot,*binding,3,NativeAttachmentInventoryScope::attached_slot):std::nullopt;
            if(!candidate || candidate->arm.side!=0 || candidate->joint!=59 || candidate->entity!=*entity || candidate->slots[0]!=*item)return {};
            equipment.attachment=*candidate;equipment.game=*game;equipment.item=*item;equipment.child_id=*spawn;
            const auto registered=[&](std::uintptr_t address,std::uint32_t id) {
                return id && id!=0x3fff0010 && (id>>16)<0x3fff &&
                    rig::value<std::uintptr_t>(guarded,*game+0x3130b0+(id>>16)*8ull)==address &&
                    rig::value<std::uint32_t>(guarded,address+0xa8)==id;
            };
            const auto player_spawn=rig::value<std::uint32_t>(guarded,arm.player+0xa8);
            const auto parent_spawn=rig::value<std::uint32_t>(guarded,candidate->parent+0xa8);
            if(!player_spawn || !parent_spawn || !registered(arm.player,*player_spawn) || !registered(*entity,*spawn) ||
               !registered(candidate->parent,*parent_spawn) || rig::value<std::uint32_t>(guarded,arm.body+0x370)!=parent_spawn ||
               rig::value<std::uint32_t>(guarded,*entity+0xac)!=player_spawn || !rig::typed(guarded,base,*entity,0x1ce1e98))return {};
            equipment.player_id=*player_spawn;equipment.parent_id=*parent_spawn;
            const auto child_model=rig::value<std::uintptr_t>(guarded,*entity+0xf0);
            if(!child_model || !rig::typed(guarded,base,*child_model,0x1b0d628))return {};
            const auto members=[&](std::uintptr_t owner,std::uintptr_t component) {
                const auto list=rig::list(guarded,owner+0x268,4096);if(!list || !list->packed_count)return false;
                std::vector<std::uintptr_t> entries(list->packed_count);
                return guarded(list->data,entries.data(),entries.size()*8) && std::count(entries.begin(),entries.end(),component)==1;
            };
            if(!members(arm.player,arm.body) || !members(arm.player,*inventory) || !members(*entity,*binding))return {};
            const auto items=rig::list(guarded,*inventory+0x38,4096);
            if(!items || *item<items->data || (*item-items->data)%0x38 || (*item-items->data)/0x38>=items->packed_count)return {};
            std::array<unsigned char,0x38> raw_item{};if(!guarded(*item,raw_item.data(),raw_item.size()))return {};
            const auto item_decl=rig::value<std::uintptr_t>(guarded,*item);
            const auto component=rig::value<std::uintptr_t>(guarded,*item+0x10),declaration=rig::value<std::uintptr_t>(guarded,*item+0x18);
            if(!item_decl || !component || !declaration || !rig::typed(guarded,base,*item_decl,0x1b9ea48))return {};
            const bool gun=rig::typed(guarded,base,*component,0x1b53f58) && rig::typed(guarded,base,*declaration,0x1b53e18);
            const bool crossbow=rig::typed(guarded,base,*component,0x1b9cbf8) && rig::typed(guarded,base,*declaration,0x1b9cab8);
            const auto identity=rig::started(guarded,*component);
            if((!gun && !crossbow) || !identity || !members(*entity,*component) || rig::value<std::uintptr_t>(guarded,*component+8)!=declaration ||
               rig::value<std::uintptr_t>(guarded,*component+0x10)!=entity || rig::value<int>(guarded,*declaration+0x68)!=1)return {};
            equipment.item_declaration=*item_decl;equipment.equipment=*component;equipment.equipment_declaration=*declaration;equipment.equipment_identity=*identity;
            const auto carry=rig::native_carry_snapshot(read,base,arm.player,arm.body);
            if(!carry || !carry_allowed(*carry,availability,now))return {};
            result.carry=*carry;
            if(!current(input,result,availability,now))return {};
            return result;
        }catch(...) {return {};}
    }
};
}
