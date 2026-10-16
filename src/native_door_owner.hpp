#pragma once
#include "game_rig.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <type_traits>
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace d2vr {
class NativeDoorOwner {
public:
    enum class Failure {None,Read,Unsupported,Ownership,Membership,Binding,Changed,Budget};
    inline static constexpr unsigned scan_limit=4096,read_limit=32768;
    inline static constexpr std::uint32_t stable_identity_flags=0xf800000fu;
    struct Snapshot {
        std::uintptr_t base{},game{},world{},player{},door{},part{},component{},animation{},container{},schema{},binding{},node{},component_declaration{},animation_declaration{};
        std::uint32_t player_spawn{},door_spawn{},part_spawn{},arena_size{},context_offset{};
        std::int32_t input_offset{};
        rig::Identity component_identity{},animation_identity{};
        std::array<unsigned char,4> clips{};
        bool operator==(const Snapshot& other) const noexcept {
            return base==other.base && game==other.game && world==other.world && player==other.player && door==other.door && part==other.part &&
                component==other.component && animation==other.animation && container==other.container && schema==other.schema && binding==other.binding &&
                node==other.node && component_declaration==other.component_declaration && animation_declaration==other.animation_declaration &&
                player_spawn==other.player_spawn && door_spawn==other.door_spawn && part_spawn==other.part_spawn &&
                arena_size==other.arena_size && context_offset==other.context_offset && input_offset==other.input_offset &&
                same_identity(component_identity,other.component_identity) && same_identity(animation_identity,other.animation_identity) && clips==other.clips;
        }
    };
    struct State {
        std::uint32_t current{},next{},locker{},instigator{};
        std::uintptr_t linked{};
        std::array<unsigned char,4> params{};
        std::array<unsigned char,6> flags{};
        unsigned char usable{},blocked{};
        bool eligible() const noexcept {
            return usable && !blocked && !linked && locker==0x3fff0010u && !flags[0] && !flags[1] && !flags[2] && !flags[5];
        }
    };
    template<class Read> static std::optional<Snapshot> capture(std::uintptr_t base,Read& read,std::uintptr_t door,Failure* failure=nullptr) noexcept {
        if(failure)*failure=Failure::None;
        try {
            Audit<Read> audit{read};Snapshot result{};result.base=base;result.door=door;
            const auto captured=inspect(audit,result);
            if(!captured || !audit.revalidate()) {if(failure)*failure=audit.failure;return {};}
            return result;
        }catch(...) {if(failure)*failure=Failure::Read;return {};}
    }
    template<class Read> static bool current(Read& read,const Snapshot& snapshot,Failure* failure=nullptr) noexcept {
        const auto next=capture(snapshot.base,read,snapshot.door,failure);
        if(!next || !(*next==snapshot)) {if(next && failure)*failure=Failure::Changed;return false;}return true;
    }
    template<class Read> static std::optional<State> state(Read& read,const Snapshot& snapshot,Failure* failure=nullptr) noexcept {
        if(!current(read,snapshot,failure))return {};
        try {
            Audit<Read> audit{read};State result{};const auto component=snapshot.component;
            if(!audit.get(component+0x48,result.current) || !audit.get(component+0x4c,result.next) || !audit.get(snapshot.door+0x378,result.locker) ||
               !audit.get(component+0x114,result.instigator) || !audit.get(component+0xf0,result.linked) || !audit.get(component+0x90,result.params) ||
               !audit.get(component+0x1e8,result.flags) || !audit.get(component+0x20,result.usable) || !audit.get(component+0x118,result.blocked) ||
               !audit.revalidate()) {if(failure)*failure=audit.failure;return {};}
            if(!current(read,snapshot,failure))return {};
            return result;
        }catch(...) {if(failure)*failure=Failure::Read;return {};}
    }
private:
    static bool same_identity(const rig::Identity& first,const rig::Identity& second) noexcept {
        return first.generation==second.generation && ((first.flags^second.flags)&stable_identity_flags)==0;
    }
    static void mask_identity(std::array<unsigned char,16>& bytes) noexcept {
        static_assert(sizeof(rig::Identity)==8 && offsetof(rig::Identity,flags)==4);
        std::uint32_t flags{};std::memcpy(&flags,bytes.data()+offsetof(rig::Identity,flags),sizeof(flags));flags&=stable_identity_flags;
        std::memcpy(bytes.data()+offsetof(rig::Identity,flags),&flags,sizeof(flags));
    }
    template<class Read> struct Audit {
        struct Witness {std::uintptr_t address{};std::array<unsigned char,16> bytes{};std::size_t size{};bool identity{};};
        Read& read;Failure failure{Failure::None};std::vector<Witness> witnesses{};
        explicit Audit(Read& input):read(input) {}
        bool reject(Failure why) {if(failure==Failure::None)failure=why;return false;}
        static bool cpp_read(Read& input,std::uintptr_t address,void* output,std::size_t size) noexcept {
            try {return input(address,output,size);}catch(...) {return false;}
        }
        static bool safe_read(Read& input,std::uintptr_t address,void* output,std::size_t size) noexcept {
#if defined(_MSC_VER)
            __try {return cpp_read(input,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
            return cpp_read(input,address,output,size);
#endif
        }
        template<class Value> bool get(std::uintptr_t address,Value& value) {
            static_assert(sizeof(Value)<=16 && std::is_trivially_copyable_v<Value>);
            if(witnesses.size()>=read_limit)return reject(Failure::Budget);
            if(address<0x10000 || address>=0x0000800000000000-sizeof(Value) || !safe_read(read,address,&value,sizeof(value)))return reject(Failure::Read);
            Witness witness{};witness.address=address;witness.size=sizeof(value);std::memcpy(witness.bytes.data(),&value,sizeof(value));witnesses.push_back(witness);return true;
        }
        template<class Value> bool equals(std::uintptr_t address,Value expected) {
            Value actual{};return get(address,actual) && (actual==expected || reject(Failure::Ownership));
        }
        bool typed(std::uintptr_t base,std::uintptr_t address,std::uintptr_t vtable) {
            return (rig::pointer(address) || reject(Failure::Ownership)) && equals(address,base+vtable);
        }
        bool revalidate() {
            for(const auto& witness:witnesses) {
                std::array<unsigned char,16> actual{};
                if(!safe_read(read,witness.address,actual.data(),witness.size))return reject(Failure::Read);
                if(witness.identity)mask_identity(actual);
                if(actual!=witness.bytes)return reject(Failure::Changed);
            }
            return true;
        }
        bool identity(std::uintptr_t component,rig::Identity& identity) {
            if(!get(component+0x18,identity))return false;
            witnesses.back().identity=true;mask_identity(witnesses.back().bytes);
            const auto flags=identity.flags;
            const auto state=((((flags>>2)&3)|((flags&3)<<2))<<2)|((flags>>4)&3);
            return (identity.generation!=-1 && (state&40)==40) || reject(Failure::Ownership);
        }
        bool entity(std::uintptr_t game,std::uintptr_t entity,std::uint32_t& spawn) {
            if(!rig::pointer(entity) || !get(entity+0xa8,spawn))return reject(Failure::Ownership);
            return (spawn && spawn!=0x3fff0010u && (spawn>>16)<0x3fff &&
                equals(game+0x3130b0+(spawn>>16)*8ull,entity)) || reject(Failure::Ownership);
        }
        bool membership(std::uintptr_t entity,std::uintptr_t component) {
            rig::List list{};if(!get(entity+0x268,list))return false;
            const auto count=list.packed_count&0xffffffu,capacity=list.packed_capacity>>8;
            if(!count || count>capacity || count>scan_limit || !rig::pointer(list.data))return reject(Failure::Membership);
            std::vector<std::uintptr_t> seen;seen.reserve(count);unsigned found{};
            for(unsigned index=0;index<count;++index) {
                std::uintptr_t entry{};if(!get(list.data+index*8ull,entry))return false;
                if(!rig::pointer(entry) || std::find(seen.begin(),seen.end(),entry)!=seen.end())return reject(Failure::Membership);
                seen.push_back(entry);if(entry==component)++found;
            }
            return found==1 || reject(Failure::Membership);
        }
    };
    template<class Read> static bool inspect(Audit<Read>& audit,Snapshot& result) {
        if(!rig::pointer(result.base) || !audit.get(result.base+0x39a5290,result.game) || !audit.typed(result.base,result.game,0x1c940a8) ||
           !audit.get(result.game+0x60410,result.world) || !rig::pointer(result.world) || !audit.get(result.game+0x3130b0,result.player) ||
           !audit.typed(result.base,result.player,0x1c5e428) || !audit.entity(result.game,result.player,result.player_spawn) ||
           !audit.typed(result.base,result.door,0x1cb7c68) || !audit.entity(result.game,result.door,result.door_spawn) ||
           !audit.get(result.door+0x370,result.component) || !audit.typed(result.base,result.component,0x1c569c8) ||
           !audit.equals(result.component+0x10,result.door) || !audit.get(result.component+8,result.component_declaration) ||
           !audit.typed(result.base,result.component_declaration,0x1c56af8) || !audit.identity(result.component,result.component_identity) ||
           !audit.membership(result.door,result.component) || !audit.get(result.component+0xdc,result.part_spawn))return audit.reject(Failure::Ownership);
        if(!result.part_spawn || result.part_spawn==0x3fff0010u || (result.part_spawn>>16)>=0x3fff ||
           !audit.get(result.game+0x3130b0+(result.part_spawn>>16)*8ull,result.part) || !rig::pointer(result.part) ||
           !audit.equals(result.part+0xa8,result.part_spawn) || !audit.get(result.component+0xf8,result.animation) ||
           !audit.typed(result.base,result.animation,0x1b7f408) || !audit.equals(result.animation+0x10,result.part) ||
           !audit.identity(result.animation,result.animation_identity) || !audit.membership(result.part,result.animation) ||
           !audit.get(result.component+0x1e0,result.clips))return audit.reject(Failure::Ownership);
        for(const auto clip:result.clips)if(clip>=0x80)return audit.reject(Failure::Unsupported);
        result.container=result.animation+0x338;
        std::uintptr_t declaration{},declared_schema{},key{};
        if(!audit.get(result.animation+8,declaration) || !rig::pointer(declaration) || !audit.get(declaration+0x68,declared_schema) ||
           !rig::pointer(declared_schema) || !audit.get(result.container+8,result.schema) || result.schema!=declared_schema ||
           !audit.get(declaration+0x70,key) || !key || !audit.get(result.animation+0x390,result.binding) || !rig::pointer(result.binding) ||
           !audit.get(result.schema+0xc0,result.arena_size) || !result.arena_size || result.arena_size>16*1024*1024 ||
           !audit.get(result.binding+4,result.input_offset) || result.input_offset<0 || static_cast<unsigned>(result.input_offset)>result.arena_size ||
           result.arena_size-static_cast<unsigned>(result.input_offset)<24)return audit.reject(Failure::Binding);
        result.animation_declaration=declaration;
        std::uintptr_t input_buckets{};std::uint32_t mask{};
        if(!audit.get(result.schema+0x70,input_buckets) || !rig::pointer(input_buckets) || !audit.get(result.schema+0x80,mask) ||
           mask>=scan_limit)return audit.reject(Failure::Binding);
        std::uintptr_t record{};if(!audit.get(input_buckets+((key>>4)&mask)*8ull,record))return false;
        std::vector<std::uintptr_t> input_seen;unsigned bindings{};std::uintptr_t previous_key{};bool has_previous{};
        while(record) {
            if(!rig::pointer(record) || input_seen.size()>=scan_limit || std::find(input_seen.begin(),input_seen.end(),record)!=input_seen.end())return audit.reject(Failure::Binding);
            input_seen.push_back(record);std::uintptr_t actual_key{},next{};
            if(!audit.get(record,actual_key) || !audit.get(record+0x970,next))return false;
            if((has_previous && actual_key<=previous_key) || (!bindings && actual_key>key))return audit.reject(Failure::Binding);
            previous_key=actual_key;has_previous=true;
            if(actual_key==key) {if(record+8!=result.binding || ++bindings!=1)return audit.reject(Failure::Binding);}
            record=next;
        }
        if(bindings!=1)return audit.reject(Failure::Binding);
        std::uintptr_t buckets{};std::uint32_t bucket_count{},node_count{};
        if(!audit.get(result.schema+0x58,buckets) || !rig::pointer(buckets) || !audit.get(result.schema+0x60,bucket_count) ||
           !bucket_count || bucket_count>scan_limit || !audit.get(result.schema+0x64,node_count) || !node_count || node_count>scan_limit)return audit.reject(Failure::Binding);
        std::vector<std::uintptr_t> seen,nodes;unsigned found{};
        for(unsigned bucket=0;bucket<bucket_count;++bucket) {
            if(!audit.get(buckets+bucket*8ull,record))return false;
            while(record) {
                if(!rig::pointer(record) || seen.size()>=node_count || std::find(seen.begin(),seen.end(),record)!=seen.end())return audit.reject(Failure::Binding);
                seen.push_back(record);std::uintptr_t node{},next{};std::int32_t offset{};
                if(!audit.get(record+8,node) || !rig::pointer(node) || !audit.get(record+0x10,offset) || !audit.get(record+0x18,next))return false;
                if(std::find(nodes.begin(),nodes.end(),node)!=nodes.end())return audit.reject(Failure::Binding);
                nodes.push_back(node);std::uintptr_t vtable{};if(!audit.get(node,vtable))return false;
                if(vtable==result.base+0x1c4bce8) {
                    unsigned char mode{};std::uintptr_t input_key{};
                    if(!audit.equals(node+8,result.schema) || !audit.get(node+0x590,mode) || !audit.get(node+0x5b0,input_key))return false;
                    if(mode==1 && input_key==key) {
                        if(++found!=1 || offset<0 || static_cast<unsigned>(offset)>result.arena_size || result.arena_size-static_cast<unsigned>(offset)<0xb78)return audit.reject(Failure::Binding);
                        result.node=node;result.context_offset=static_cast<unsigned>(offset);
                    }
                }
                record=next;
            }
        }
        return (seen.size()==node_count && found==1) || audit.reject(Failure::Binding);
    }
};
}
