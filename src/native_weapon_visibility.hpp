#pragma once
#include "game_rig.hpp"
#include "native_slash_speed.hpp"
#include <array>
#include <cstring>

namespace d2vr {
class NativeWeaponVisibility {
public:
    static constexpr unsigned hierarchy_limit=32;
    struct Entity {
        std::uintptr_t address{},vtable{},model{},rf{},rf_owner{},parent{},animator{},declaration{};
        std::uint32_t spawn{};
        rig::Identity rf_identity{};
        std::int16_t joint{-1};
        std::uint8_t hidden{};
        bool valid{},weapon{},rf_valid{};
        std::array<float,3> origin{};
        std::array<float,9> axis{};
        std::array<float,15> model_frame{};
        bool origin_valid{},axis_valid{},model_frame_valid{};
        std::array<char,96> name{};
    };
    struct Item {std::uintptr_t address{};bool member{},valid{};Entity entity{};};
    struct Snapshot {
        std::uint64_t tick{},holster_enqueued{},holster_dropped{},holster_serviced{},holster_request_returned{},holster_failed{};
        std::uintptr_t game{},player{},body{},inventory{},machine{},pool{},equip{},controller{};
        rig::Identity body_identity{},inventory_identity{},machine_identity{};
        std::array<Item,3> items{};
        std::array<Entity,2> hip{};
        std::array<Entity,hierarchy_limit> hierarchy{};
        std::uintptr_t equip_108{},equip_118{};
        std::uint32_t player_spawn{};
        unsigned hierarchy_count{};
        std::uint8_t equip_active{};
        bool owner_valid{},items_complete{},hierarchy_complete{};
    };
    template<std::size_t Count,class Read> static bool observation(Read& read,std::uintptr_t address,std::array<float,Count>& output) {
        std::array<float,Count> copy{};
        if(!read(address,copy.data(),sizeof(copy)))return false;
        for(const float value:copy)if(!std::isfinite(value) || std::abs(value)>1e7f)return false;
        output=copy;return true;
    }
    template<std::size_t Count,class Read> static void confirm_observation(Read& read,std::uintptr_t address,std::array<float,Count>& output,bool& valid) {
        if(!valid)return;
        std::array<float,Count> copy{};
        valid=observation(read,address,copy) && copy==output;
        if(!valid)output={};
    }
    template<class Read> static Entity entity(std::uintptr_t base,Read& read,std::uintptr_t game,std::uint32_t spawn) {
        Entity result{};result.spawn=spawn;
        if(!spawn || spawn==0x3fff0010 || (spawn>>16)>=0x3fff)return result;
        const auto address=rig::value<std::uintptr_t>(read,game+0x3130b0+(spawn>>16)*8ull);
        if(!address || !rig::pointer(*address) || rig::value<std::uint32_t>(read,*address+0xa8)!=spawn)return result;
        result.address=*address;
        const auto vtable=rig::value<std::uintptr_t>(read,*address),model=rig::value<std::uintptr_t>(read,*address+0xf0),
            rf=rig::value<std::uintptr_t>(read,*address+0x358),decl=rig::value<std::uintptr_t>(read,*address+0x2e8);
        const auto hidden=rig::value<std::uint8_t>(read,*address+0xd0);
        if(!vtable || !model || !rf || !decl || !hidden || !rig::pointer(*vtable) ||
           (*model && !rig::typed(read,base,*model,0x1b0d628)))return result;
        result.vtable=*vtable;result.weapon=*vtable==base+0x1ce1e98;result.model=*model;result.rf=*rf;
        result.declaration=*decl;result.hidden=*hidden;
        result.origin_valid=observation(read,*address+0x300,result.origin);
        result.axis_valid=observation(read,*address+0x30c,result.axis);
        if(*model)result.model_frame_valid=observation(read,*model+0x1860,result.model_frame);
        if(*decl) {
            const auto text=rig::value<std::uintptr_t>(read,*decl+0x48);
            if(text && *text>=0x10000) {
                if(!read(*text,result.name.data(),result.name.size()-1))result.name={};
            }
        }
        if(*rf && rig::typed(read,base,*rf,0x1c573e8,0x1c574c0)) {
            const auto identity=rig::started(read,*rf);
            const auto owner=rig::value<std::uintptr_t>(read,*rf+0x10),
                parent=rig::value<std::uintptr_t>(read,*rf+0x3c8),animator=rig::value<std::uintptr_t>(read,*rf+0x400);
            const auto joint=rig::value<std::int16_t>(read,*rf+0x3d0);
            const auto components=rig::list(read,*address+0x268,4096);
            bool member=false;
            if(components)for(unsigned index=0;index<components->packed_count;++index)
                if(rig::value<std::uintptr_t>(read,components->data+index*8ull)==rf)member=true;
            if(identity && owner && parent && animator && joint && *owner==*address && member) {
                result.rf_identity=*identity;result.rf_owner=*owner;result.parent=*parent;result.animator=*animator;
                result.joint=*joint;result.rf_valid=true;
            }
        }
        confirm_observation(read,*address+0x300,result.origin,result.origin_valid);
        confirm_observation(read,*address+0x30c,result.axis,result.axis_valid);
        if(*model)confirm_observation(read,*model+0x1860,result.model_frame,result.model_frame_valid);
        if(rig::value<std::uintptr_t>(read,game+0x3130b0+(spawn>>16)*8ull)!=address ||
           rig::value<std::uint32_t>(read,*address+0xa8)!=spawn || rig::value<std::uintptr_t>(read,*address)!=vtable ||
           rig::value<std::uintptr_t>(read,*address+0xf0)!=model || rig::value<std::uintptr_t>(read,*address+0x358)!=rf ||
           rig::value<std::uint8_t>(read,*address+0xd0)!=hidden || (*model && !rig::typed(read,base,*model,0x1b0d628)))return Entity{};
        if(result.rf_valid && (rig::started(read,*rf)!=std::optional<rig::Identity>{result.rf_identity} ||
           rig::value<std::uintptr_t>(read,*rf+0x10)!=result.rf_owner || rig::value<std::uintptr_t>(read,*rf+0x3c8)!=result.parent ||
           rig::value<std::uintptr_t>(read,*rf+0x400)!=result.animator || rig::value<std::int16_t>(read,*rf+0x3d0)!=result.joint))return Entity{};
        result.valid=true;return result;
    }
    template<class Read> static Snapshot capture(std::uintptr_t base,Read& read,
            const std::array<std::uint32_t,2>& hip_ids,std::uint64_t now) {
        Snapshot result{};result.tick=now;
        const auto arms=rig::player_arms(read,base);if(!arms)return result;
        result.player=(*arms)[0].player;result.body=(*arms)[0].body;result.body_identity=(*arms)[0].body_identity;
        const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290),inventory=rig::value<std::uintptr_t>(read,result.body+0x60);
        if(!game || !inventory || !rig::typed(read,base,*inventory,0x1b9a878) ||
           rig::value<std::uintptr_t>(read,*inventory+0x10)!=result.player)return result;
        const auto inventory_id=rig::started(read,*inventory);
        const auto components=rig::list(read,result.player+0x268,4096);
        if(!inventory_id || !components)return result;
        result.game=*game;result.inventory=*inventory;result.inventory_identity=*inventory_id;
        for(unsigned index=0;index<components->packed_count;++index) {
            const auto component=rig::value<std::uintptr_t>(read,components->data+index*8ull);
            if(!component)return result;
            if(rig::typed(read,base,*component,0x1c73d58)) {if(result.machine)return result;result.machine=*component;}
        }
        const auto lease=native_player_state_lease(read,base,result.machine);if(!lease)return result;
        result.machine_identity=lease->identity;result.player_spawn=lease->spawn;
        const auto pool=rig::value<std::uintptr_t>(read,result.machine+0x90),controller=rig::value<std::uintptr_t>(read,result.machine+0x2f8);
        if(!pool || !controller || !rig::pointer(*pool) || !rig::typed(read,base,*controller,0x1c72918) ||
           rig::value<std::uintptr_t>(read,*controller+0x10)!=result.player ||
           rig::value<std::uintptr_t>(read,*controller+0x58)!=result.machine ||
           rig::value<std::uintptr_t>(read,result.machine+0x300)!=result.body)return result;
        result.pool=*pool;result.controller=*controller;
        const auto equip=rig::value<std::uintptr_t>(read,*pool+0x10+64*8);
        if(!equip || !rig::typed(read,base,*equip,0x1c79cf8) || rig::value<std::uintptr_t>(read,*equip+0x10)!=result.machine ||
           rig::value<int>(read,*equip+8)!=64)return result;
        result.equip=*equip;
        const auto active=rig::value<std::uint8_t>(read,*equip+0x20);
        const auto current=rig::value<std::uintptr_t>(read,*equip+0x108),
            pending=rig::value<std::uintptr_t>(read,*equip+0x118);
        if(!active || !current || !pending)return result;
        result.equip_active=*active;result.equip_108=*current;result.equip_118=*pending;
        const std::array<std::uintptr_t,3> fields{*inventory+0x70,*controller+0x190,*controller+0x1a0};
        const auto entries=rig::list(read,*inventory+0x38,4096);if(!entries)return result;
        result.items_complete=true;
        for(unsigned index=0;index<fields.size();++index) {
            const auto item=rig::value<std::uintptr_t>(read,fields[index]);
            if(!item) {result.items_complete=false;continue;}
            auto& output=result.items[index];output.address=*item;
            if(!*item) {output.valid=true;continue;}
            for(unsigned entry=0;entry<entries->packed_count;++entry)
                if(*item==entries->data+entry*0x38ull)output.member=true;
            if(!output.member) {result.items_complete=false;continue;}
            const auto spawn=rig::value<std::uint32_t>(read,*item+8);
            if(spawn)output.entity=entity(base,read,*game,*spawn);
            output.valid=output.entity.valid && rig::value<std::uintptr_t>(read,fields[index])==item;
            if(!output.valid)result.items_complete=false;
        }
        for(unsigned index=0;index<hip_ids.size();++index)if(hip_ids[index])result.hip[index]=entity(base,read,*game,hip_ids[index]);
        const auto arm_id=rig::value<std::uint32_t>(read,result.body+0x370);
        if(arm_id) {
            result.hierarchy[0]=entity(base,read,*game,*arm_id);result.hierarchy_count=result.hierarchy[0].valid?1u:0u;
            result.hierarchy_complete=result.hierarchy_count!=0;
            for(unsigned cursor=0;cursor<result.hierarchy_count && result.hierarchy_complete;++cursor) {
                const auto parent=result.hierarchy[cursor].address;
                const auto header=rig::value<rig::List>(read,parent+0xb0);
                if(!header) {result.hierarchy_complete=false;break;}
                const auto count=header->packed_count&0xffffff;
                if(count>hierarchy_limit-result.hierarchy_count || (count && !rig::pointer(header->data))) {result.hierarchy_complete=false;break;}
                for(unsigned child_index=0;child_index<count;++child_index) {
                    const auto child=rig::value<std::uintptr_t>(read,header->data+child_index*8ull);
                    const auto spawn=child?rig::value<std::uint32_t>(read,*child+0xa8):std::nullopt;
                    if(!child || !spawn) {result.hierarchy_complete=false;break;}
                    const auto child_record=entity(base,read,*game,*spawn);
                    bool duplicate=false;
                    for(unsigned previous=0;previous<result.hierarchy_count;++previous)if(result.hierarchy[previous].address==*child)duplicate=true;
                    if(!child_record.valid || child_record.address!=*child || duplicate) {result.hierarchy_complete=false;break;}
                    result.hierarchy[result.hierarchy_count++]=child_record;
                }
                const auto after=rig::value<rig::List>(read,parent+0xb0);
                if(!after || after->data!=header->data || after->packed_count!=header->packed_count)result.hierarchy_complete=false;
            }
        }
        const auto after_entries=rig::list(read,*inventory+0x38,4096);
        for(unsigned index=0;index<fields.size();++index)
            if(rig::value<std::uintptr_t>(read,fields[index])!=result.items[index].address)result.items_complete=false;
        const auto after_lease=native_player_state_lease(read,base,result.machine);
        result.owner_valid=after_lease && *after_lease==*lease && rig::player_arms(read,base)==arms &&
            rig::started(read,*inventory)==inventory_id && rig::value<std::uintptr_t>(read,result.body+0x60)==inventory &&
            rig::value<std::uintptr_t>(read,*inventory+0x10)==result.player && after_entries &&
            after_entries->data==entries->data && after_entries->packed_count==entries->packed_count &&
            rig::value<std::uintptr_t>(read,result.machine+0x90)==pool && rig::value<std::uintptr_t>(read,result.machine+0x2f8)==controller &&
            rig::value<std::uintptr_t>(read,*pool+0x10+64*8)==equip;
        return result;
    }
};
}
