#pragma once
#include <windows.h>
#include "native_slash_speed.hpp"
#include <array>
#include <atomic>
#include <mutex>

namespace d2vr {
class NativeManualEquip {
public:
    struct alignas(16) Params {
        std::array<unsigned char,0x30> bytes{};
        template<class Value> Value get(std::size_t offset)const noexcept {Value result{};std::memcpy(&result,bytes.data()+offset,sizeof(result));return result;}
        template<class Value> void set(std::size_t offset,Value value)noexcept {std::memcpy(bytes.data()+offset,&value,sizeof(value));}
        bool operator==(const Params& other)const noexcept{return bytes==other.bytes;}
    };
    static_assert(sizeof(Params)==0x30);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Original=void (__fastcall*)(std::uintptr_t,const Params*);
    enum class Source {none,hip,wheel};
    enum class Kind {gun,crossbow};
    struct Scope {
        NativePlayerStateLease owner;
        rig::Identity inventory_identity{};
        std::uintptr_t state{},pool{},controller{},inventory{},item{},declaration{},entity{},ammo{};
        std::uintptr_t equipment{},equipment_declaration{};rig::Identity equipment_identity{};
        std::uint32_t item_spawn{};
        Kind kind{Kind::gun};
        bool operator==(const Scope& other)const noexcept {
            return owner==other.owner && inventory_identity==other.inventory_identity && state==other.state && pool==other.pool &&
                controller==other.controller && inventory==other.inventory && item==other.item && declaration==other.declaration &&
                entity==other.entity && ammo==other.ammo && equipment==other.equipment && equipment_declaration==other.equipment_declaration &&
                equipment_identity.generation==other.equipment_identity.generation &&
                (equipment_identity.flags&~0xcu)==(other.equipment_identity.flags&~0xcu) && item_spawn==other.item_spawn && kind==other.kind;
        }
    };
    using Classify=bool (*)(const Scope&);
    struct Lease {Scope scope;Source source{Source::none};std::uint64_t generation{};};
    struct Observation {
        Params params;Scope candidate;
        bool readable{},qualified{},classification_gap{},current_slot_gap{},authorized{},would_veto{},changed{},explicit_item_gap{},holster_revoke{};
    };
    inline static constexpr std::uintptr_t processor_rva=0x8363f0,wheel_rva=0x7fb350,wheel_return_rva=0x4f57e6,wheel_request_return_rva=0x7fb43b;
    inline static constexpr std::array<unsigned char,32> prologue{
        0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x8b,0xec,0x48,0x83,0xec,0x60,0x48,0x83,0x7a,0x08,0x00,0x48,0x8b,0x41,0x10};
    inline static constexpr std::array<unsigned char,16> wheel_prologue{
        0x48,0x8b,0xc4,0x56,0x41,0x56,0x48,0x83,0xec,0x68,0x80,0xb9,0x71,0x01,0x00,0x00};
    inline static constexpr std::array<std::pair<std::uintptr_t,std::uintptr_t>,4> slots{{
        {0x1c79cf8+0x58,0x8356c0},{0x1c79cf8+0x68,0x8371f0},{0x1c79cf8+0x70,0x835cf0},{0x1c79cf8+0x90,0x836f00}}};
    struct CallPin {std::uintptr_t rva;std::array<unsigned char,5> bytes;};
    inline static constexpr std::array<CallPin,4> call_pins{{
        {0x4f57e1,{0xe8,0x6a,0x5b,0x30,0x00}},{0x7fb436,{0xe8,0x95,0x70,0x00,0x00}},
        {0x83721d,{0xe8,0xce,0xf1,0xff,0xff}},{0x8356ec,{0xe9,0xff,0x0c,0x00,0x00}}}};
    static bool verify(std::uintptr_t base,Read read)noexcept {
        Reader reader{read};std::array<unsigned char,32> actual{};std::array<unsigned char,16> wheel{};
        if(!base || !reader(base+processor_rva,actual.data(),actual.size()) || actual!=prologue ||
           !reader(base+wheel_rva,wheel.data(),wheel.size()) || wheel!=wheel_prologue)return false;
        for(const auto& slot:slots)if(rig::value<std::uintptr_t>(reader,base+slot.first)!=base+slot.second)return false;
        for(const auto& pin:call_pins){std::array<unsigned char,5> bytes{};if(!reader(base+pin.rva,bytes.data(),bytes.size()) || bytes!=pin.bytes)return false;}
        return true;
    }
    bool start_observer(std::uintptr_t base,Read read,Original original=nullptr,Classify classify=nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(running_ || in_flight_ || (base_ && (base!=base_ || read!=read_ || original!=original_)) || !verify(base,read))return false;
        base_=base;read_=read;original_=original;classify_=classify;lease_={};pending_source_=Source::none;last_={};writer_=false;running_=true;return true;
    }
    bool stop() {
        std::lock_guard<std::mutex> lock(mutex_);running_=false;writer_=false;lease_={};pending_source_=Source::none;return !in_flight_;
    }
    bool writer_enabled()const noexcept{return writer_.load();}
    bool enable_at_idle(std::uintptr_t state) {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        if(!running_ || in_flight_ || !receiver(base_,read_,state) || rig::value<unsigned char>(reader,state+0x20)!=0 ||
           rig::value<std::uintptr_t>(reader,state+0x108)!=0 || rig::value<std::uintptr_t>(reader,state+0x118)!=0 ||
           rig::value<unsigned char>(reader,state+0x150)!=0)return false;
        writer_=true;return true;
    }
    unsigned in_flight()const noexcept{return in_flight_.load();}
    Observation observation()const {std::lock_guard<std::mutex> lock(mutex_);return last_;}
    Lease lease()const {std::lock_guard<std::mutex> lock(mutex_);return lease_;}
    bool reserve_manual(const NativePlayerStateLease& owner,std::uint64_t generation,Source source) {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        if(!running_ || source==Source::none || generation<=generation_ || !(native_player_state_lease(reader,base_,owner.machine)==std::optional<NativePlayerStateLease>{owner}))return false;
        generation_=generation;pending_owner_=owner;pending_source_=source;lease_={};return true;
    }
    bool revoke(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);if(generation<=generation_)return false;
        generation_=generation;lease_={};pending_source_=Source::none;return true;
    }
    bool grant(std::uint64_t generation,const Scope& scope) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!running_ || generation!=generation_ || pending_source_==Source::none || !(pending_owner_==scope.owner))return false;
        const auto current=qualify(base_,read_,scope.state,scope.item,scope.ammo);
        if(!current || !(*current==scope) || (classify_ && !classified(classify_,scope)))return false;
        lease_={scope,pending_source_,generation};pending_source_=Source::none;return true;
    }
    static bool actual_wheel_origin(std::uintptr_t base,std::uintptr_t return_address)noexcept {return return_address==base+wheel_return_rva;}
    static std::optional<Scope> qualify(std::uintptr_t base,Read read,std::uintptr_t state,std::uintptr_t item,std::uintptr_t ammo=0)noexcept {
        Reader reader{read};const auto machine=rig::value<std::uintptr_t>(reader,state+0x10);
        const auto owner=machine?native_player_state_lease(reader,base,*machine):std::nullopt;if(!owner)return {};
        const auto pool=rig::value<std::uintptr_t>(reader,*machine+0x90),controller=rig::value<std::uintptr_t>(reader,*machine+0x2f8),inventory=rig::value<std::uintptr_t>(reader,*machine+0x328);
        if(!pool || !controller || !inventory || !rig::pointer(*pool) || !rig::typed(reader,base,state,0x1c79cf8) ||
           rig::value<int>(reader,state+8)!=64 || rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=state ||
           !rig::typed(reader,base,*controller,0x1c72918) || rig::value<std::uintptr_t>(reader,*controller+0x10)!=owner->player ||
           rig::value<std::uintptr_t>(reader,*controller+0x58)!=machine || !rig::typed(reader,base,*inventory,0x1b9a878) || rig::value<std::uintptr_t>(reader,*inventory+0x10)!=owner->player)return {};
        const auto identity=rig::started(reader,*inventory);const auto entries=rig::list(reader,*inventory+0x38,4096);
        if(!identity || !entries || !member(*entries,item) || (ammo && !member(*entries,ammo)))return {};
        const auto declaration=rig::value<std::uintptr_t>(reader,item);const auto spawn=rig::value<std::uint32_t>(reader,item+8);
        if(!declaration || !rig::typed(reader,base,*declaration,0x1b9ea48) || !spawn || !*spawn || (*spawn>>16)>=0x3fff)return {};
        const auto entity=rig::value<std::uintptr_t>(reader,owner->game+0x3130b0+(*spawn>>16)*8ull);
        if(!entity || !rig::typed(reader,base,*entity,0x1ce1e98) || rig::value<std::uint32_t>(reader,*entity+0xa8)!=spawn)return {};
        const auto equipment=rig::value<std::uintptr_t>(reader,item+0x10),equipment_decl=rig::value<std::uintptr_t>(reader,item+0x18);
        if(!equipment || !equipment_decl)return {};
        const bool gun=rig::typed(reader,base,*equipment,0x1b53f58) && rig::typed(reader,base,*equipment_decl,0x1b53e18);
        const bool crossbow=rig::typed(reader,base,*equipment,0x1b9cbf8) && rig::typed(reader,base,*equipment_decl,0x1b9cab8);
        const auto equipment_identity=retained_equipment_identity(reader,*equipment);const auto components=rig::list(reader,*entity+0x268,4096);
        if((!gun && !crossbow) || !equipment_identity || !components || rig::value<std::uintptr_t>(reader,*equipment+8)!=equipment_decl ||
           rig::value<std::uintptr_t>(reader,*equipment+0x10)!=entity || rig::value<int>(reader,*equipment_decl+0x68)!=1)return {};
        unsigned membership{};
        for(unsigned index=0;index<components->packed_count;++index){const auto entry=rig::value<std::uintptr_t>(reader,components->data+index*8ull);if(!entry)return {};if(entry==equipment)++membership;}
        if(membership!=1)return {};
        const auto final_entries=rig::list(reader,*inventory+0x38,4096);
        if(!(native_player_state_lease(reader,base,*machine)==owner) || rig::started(reader,*inventory)!=identity || !final_entries ||
           final_entries->data!=entries->data || final_entries->packed_count!=entries->packed_count || final_entries->packed_capacity!=entries->packed_capacity ||
           rig::value<std::uintptr_t>(reader,*machine+0x90)!=pool || rig::value<std::uintptr_t>(reader,*machine+0x2f8)!=controller ||
           rig::value<std::uintptr_t>(reader,*machine+0x328)!=inventory || rig::value<std::uintptr_t>(reader,item)!=declaration || rig::value<std::uint32_t>(reader,item+8)!=spawn ||
           !rig::typed(reader,base,state,0x1c79cf8) || rig::value<int>(reader,state+8)!=64 || rig::value<std::uintptr_t>(reader,state+0x10)!=machine ||
           rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=state || !rig::typed(reader,base,*controller,0x1c72918) ||
           rig::value<std::uintptr_t>(reader,*controller+0x10)!=owner->player || rig::value<std::uintptr_t>(reader,*controller+0x58)!=machine ||
           !rig::typed(reader,base,*inventory,0x1b9a878) || rig::value<std::uintptr_t>(reader,*inventory+0x10)!=owner->player ||
           rig::value<std::uintptr_t>(reader,owner->game+0x3130b0+(*spawn>>16)*8ull)!=entity || !rig::typed(reader,base,*entity,0x1ce1e98) || rig::value<std::uint32_t>(reader,*entity+0xa8)!=spawn)return {};
        if(rig::value<std::uintptr_t>(reader,item+0x10)!=equipment || rig::value<std::uintptr_t>(reader,item+0x18)!=equipment_decl ||
           rig::value<rig::Identity>(reader,*equipment+0x18)!=equipment_identity || rig::value<std::uintptr_t>(reader,*equipment+8)!=equipment_decl ||
           rig::value<std::uintptr_t>(reader,*equipment+0x10)!=entity || rig::value<int>(reader,*equipment_decl+0x68)!=1 ||
           !rig::typed(reader,base,*equipment,gun?0x1b53f58:0x1b9cbf8) || !rig::typed(reader,base,*equipment_decl,gun?0x1b53e18:0x1b9cab8))return {};
        const auto final_components=rig::list(reader,*entity+0x268,4096);
        if(!final_components || final_components->data!=components->data || final_components->packed_count!=components->packed_count || final_components->packed_capacity!=components->packed_capacity)return {};
        membership=0;
        for(unsigned index=0;index<components->packed_count;++index){const auto entry=rig::value<std::uintptr_t>(reader,components->data+index*8ull);if(!entry)return {};if(entry==equipment)++membership;}
        if(membership!=1)return {};
        return Scope{*owner,*identity,state,*pool,*controller,*inventory,item,*declaration,*entity,ammo,*equipment,*equipment_decl,*equipment_identity,*spawn,gun?Kind::gun:Kind::crossbow};
    }
    bool prepare(std::uintptr_t state,Params& params,bool object_occupied) {
        ++in_flight_;
        __try {prepare_cpp(state,params,object_occupied);return true;}
        __finally {--in_flight_;}
    }
    void invoke(std::uintptr_t state,const Params* params,bool object_occupied) {
        ++in_flight_;
        __try {invoke_cpp(state,params,object_occupied);}
        __finally {--in_flight_;}
    }
private:
    template<class ReaderType> static std::optional<rig::Identity> retained_equipment_identity(ReaderType& reader,std::uintptr_t equipment)noexcept {
        const auto identity=rig::value<rig::Identity>(reader,equipment+0x18);
        if(!identity || identity->generation==-1 || (identity->flags&3u)!=2u || ((identity->flags>>2)&3u)==3u)return {};
        return identity;
    }
    struct Reader {
        Read read{};
        static bool cpp(Read read,std::uintptr_t address,void* output,std::size_t size)noexcept {try{return read && read(address,output,size);}catch(...){return false;}}
        bool operator()(std::uintptr_t address,void* output,std::size_t size)const noexcept {
            __try{return cpp(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        }
    };
    static bool classified_cpp(Classify classify,const Scope& scope)noexcept {try{return classify && classify(scope);}catch(...){return false;}}
    static bool classified(Classify classify,const Scope& scope)noexcept {
        __try{return classified_cpp(classify,scope);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool member(const rig::List& entries,std::uintptr_t item)noexcept {
        return item>=entries.data && item-entries.data<static_cast<std::uintptr_t>(entries.packed_count)*0x38 && (item-entries.data)%0x38==0;
    }
    static bool receiver(std::uintptr_t base,Read read,std::uintptr_t state)noexcept {
        Reader reader{read};const auto machine=rig::value<std::uintptr_t>(reader,state+0x10);
        const auto owner=machine?native_player_state_lease(reader,base,*machine):std::nullopt;
        const auto pool=machine?rig::value<std::uintptr_t>(reader,*machine+0x90):std::nullopt;
        return owner && pool && rig::typed(reader,base,state,0x1c79cf8) && rig::value<int>(reader,state+8)==64 &&
            rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)==state &&
            native_player_state_lease(reader,base,*machine)==owner && rig::value<std::uintptr_t>(reader,state+0x10)==machine;
    }
    static std::optional<Scope> candidate(std::uintptr_t base,Read read,std::uintptr_t state,const Params& params)noexcept {
        Reader reader{read};auto item=params.get<std::uintptr_t>(8),ammo=params.get<std::uintptr_t>(16);
        const auto code=params.get<std::uint32_t>(24);
        if(!item){
            const auto machine=rig::value<std::uintptr_t>(reader,state+0x10),controller=machine?rig::value<std::uintptr_t>(reader,*machine+0x2f8):std::nullopt;
            if(!controller)return {};
            const auto offset=(code&3)?0x1a0u:0x190u;
            const auto inventory=machine?rig::value<std::uintptr_t>(reader,*machine+0x328):std::nullopt;
            const auto remembered=(code&0x0c)?rig::value<std::uintptr_t>(reader,*controller+offset):
                inventory?rig::value<std::uintptr_t>(reader,*inventory+0x70):std::nullopt;
            const auto secondary=(code&0x0c)?rig::value<std::uintptr_t>(reader,*controller+offset+8):std::optional<std::uintptr_t>{0};
            if(!remembered || !secondary)return {};item=*remembered;ammo=*secondary;
        }
        const auto result=qualify(base,read,state,item,ammo);if(!result)return {};
        if(!params.get<std::uintptr_t>(8)){
            const auto offset=(code&3)?0x1a0u:0x190u;
            if(code&0x0c){if(rig::value<std::uintptr_t>(reader,result->controller+offset)!=item || rig::value<std::uintptr_t>(reader,result->controller+offset+8)!=ammo)return {};}
            else if(rig::value<std::uintptr_t>(reader,result->inventory+0x70)!=item)return {};
        }
        return result;
    }
    void invoke_cpp(std::uintptr_t state,const Params* params,bool object_occupied) {
        Original original{};Read read{};
        {std::lock_guard<std::mutex> lock(mutex_);original=original_;read=read_;}
        if(!original)return;
        Reader reader{read};Params copied{};const bool readable=reader(reinterpret_cast<std::uintptr_t>(params),&copied,sizeof(copied));
        if(readable)prepare_cpp(state,copied,object_occupied);
        else {std::lock_guard<std::mutex> lock(mutex_);last_={};}
        original(state,readable?&copied:params);
    }
    void prepare_cpp(std::uintptr_t state,Params& copied,bool object_occupied) {
        std::uintptr_t base{};Read read{};Classify classify{};bool running{};
        {std::lock_guard<std::mutex> lock(mutex_);base=base_;read=read_;classify=classify_;running=running_;}
        Reader reader{read};Observation observation{};
        observation.readable=true;observation.params=copied;
        const auto current=running && observation.readable?candidate(base,read,state,copied):std::nullopt;
        observation.qualified=current.has_value();if(current)observation.candidate=*current;
        observation.classification_gap=current && classify && !classified(classify,*current);
        const auto code=copied.get<std::uint32_t>(24);const bool explicit_item=copied.get<std::uintptr_t>(8)!=0;
        const auto current_slot=current?rig::value<std::uintptr_t>(reader,current->inventory+0x70):std::nullopt;
        observation.current_slot_gap=current && (!current_slot || (*current_slot && *current_slot!=current->item));
        const bool holster=running && observation.readable && !explicit_item && !(code&0x0c) && !(code&1) && receiver(base,read,state);
        {std::lock_guard<std::mutex> lock(mutex_);
            const bool temporary_unequip=!explicit_item && !(code&0x0c) && (code&1);
            if(current && !temporary_unequip && lease_.generation && !(*current==lease_.scope)){lease_={};pending_source_=Source::none;}
            if(holster || object_occupied){lease_={};pending_source_=Source::none;}
            observation.holster_revoke=holster;
            observation.authorized=running_ && current && !object_occupied && lease_.generation && *current==lease_.scope;
            observation.explicit_item_gap=explicit_item && !observation.authorized;
            observation.would_veto=current && !observation.classification_gap && !observation.current_slot_gap && !explicit_item && (code&0x0c) && !observation.authorized;
            observation.changed=running_ && writer_ && observation.would_veto;
            if(observation.changed)copied.set<std::uint32_t>(24,code&~0x0cu);
            last_=observation;
        }
    }
    mutable std::mutex mutex_;
    std::uintptr_t base_{};Read read_{};Original original_{};Classify classify_{};bool running_{};
    Lease lease_{};NativePlayerStateLease pending_owner_{};Source pending_source_{Source::none};
    Observation last_{};std::uint64_t generation_{};
    std::atomic<bool> writer_{};std::atomic<unsigned> in_flight_{};
};
}
