#pragma once
#include "native_assassination_receipt_hooks.hpp"
#include "native_slash_speed.hpp"
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace d2vr {
class NativeAssassinationReceipt {
    friend struct NativeAssassinationReceiptFixture;
public:
    using Read=NativeAssassinationReceiptHooks::Read;
    struct Packet {
        std::uint32_t serial{},state{},victim{},mode{},outcome{};
        std::uint32_t requester{};std::uintptr_t context{};
        bool valid{};
        bool operator==(const Packet& other)const noexcept {
            return serial==other.serial && state==other.state && victim==other.victim && mode==other.mode &&
                outcome==other.outcome && requester==other.requester && context==other.context && valid==other.valid;
        }
    };
    struct Snapshot {
        NativePlayerStateLease owner{};
        std::uintptr_t pool{},state{},victim_entity{},sensor{},handler{};
        rig::Identity sensor_identity{};
        std::uint32_t victim{},mode{},kind{};
        std::uint8_t active{},ready{};
        bool owner_valid{},state_valid{},victim_valid{},offer_valid{},packet_present{},packet_search_valid{};
    };
    struct Row {
        std::uint64_t activation{},id{},parent{},epoch{},begin_tick{},end_tick{};
        std::uintptr_t caller{},subject{};
        std::uint32_t thread{};
        unsigned hook{},state_id{},depth{};
        Packet packet{};
        Snapshot before{},after{};
        bool completed{},current{},same_owner{},accepted_copy{},active_victim_match{},native_result{};
    };
    static_assert(std::is_trivially_copyable_v<Row>);
    static NativeAssassinationReceipt& process_owner(){static auto* owner=new NativeAssassinationReceipt;return *owner;}
    bool start(std::uintptr_t base,Read read,const std::filesystem::path& folder) noexcept {
        if(!stop())return false;
        try {
            base_=base;read_=read;if(activation_==UINT64_MAX)return false;++activation_;next_ga_tick_=0;sequence_=0;dropped_=overflow_=written_=0;bytes_=0;failed_=false;
            {std::lock_guard<std::mutex> lock(queue_mutex_);head_=count_=0;}
            output_.clear();output_.open(folder/L"native-assassination-receipts.jsonl",std::ios::trunc);
            if(!output_)return false;
            auto& hooks=NativeAssassinationReceiptHooks::process_owner();
            if(!hooks.install(base,read,{this,&observe})){failed_=true;stop();return false;}
            active_=true;hooks.enabled(true);return hooks.enabled();
        }catch(...){failed_=true;stop();return false;}
    }
    bool stop() noexcept {
        active_=false;
        if(!NativeAssassinationReceiptHooks::process_owner().disable()) {failed_=true;return false;}
        flush();if(output_.is_open())output_.close();return true;
    }
    bool enabled()const noexcept{return active_ && NativeAssassinationReceiptHooks::process_owner().enabled() && !NativeAssassinationReceiptHooks::process_owner().observer_failed();}
    bool failed()const noexcept{return failed_ || NativeAssassinationReceiptHooks::process_owner().observer_failed();}
    std::uint64_t dropped()const noexcept{return dropped_;}
    void flush() noexcept {
        try {
            for(unsigned index=0;index<256;++index) {
                Row row{};
                {std::lock_guard<std::mutex> lock(queue_mutex_);if(!count_)break;row=rows_[head_];head_=(head_+1)%rows_.size();--count_;}
                if(!output_ || bytes_>=64ull*1024*1024){++dropped_;failed_=true;active_=false;continue;}
                const auto began=output_.tellp();
                output_<<"{\"activation\":"<<row.activation<<",\"id\":"<<row.id<<",\"parent\":"<<row.parent<<",\"epoch\":"<<row.epoch
                    <<",\"begin_tick\":"<<row.begin_tick<<",\"end_tick\":"<<row.end_tick<<",\"thread\":"<<row.thread<<",\"depth\":"<<row.depth
                    <<",\"hook\":"<<row.hook<<",\"caller_rva\":"<<row.caller<<",\"subject\":"<<row.subject<<",\"state_id\":"<<row.state_id
                    <<",\"serial\":"<<row.packet.serial<<",\"packet_state\":"<<row.packet.state<<",\"packet_victim\":"<<row.packet.victim
                    <<",\"packet_mode\":"<<row.packet.mode<<",\"packet_outcome\":"<<row.packet.outcome<<",\"packet_valid\":"<<row.packet.valid
                    <<",\"requester\":"<<row.packet.requester<<",\"context\":"<<row.packet.context<<",\"completed\":"<<row.completed
                    <<",\"current\":"<<row.current<<",\"same_owner\":"<<row.same_owner<<",\"accepted_copy\":"<<row.accepted_copy
                    <<",\"active_victim_match\":"<<row.active_victim_match<<",\"native_result\":"<<row.native_result<<",\"before\":";
                write_snapshot(output_,row.before);output_<<",\"after\":";write_snapshot(output_,row.after);
                output_<<",\"dropped_total\":"<<dropped_.load()<<",\"overflow_total\":"<<overflow_.load()<<"}\n";
                const auto ended=output_.tellp();
                if(!output_ || began<0 || ended<began){failed_=true;active_=false;}else{bytes_+=static_cast<std::uint64_t>(ended-began);++written_;}
            }
            if(output_.is_open())output_.flush();
        }catch(...){failed_=true;active_=false;}
    }
private:
    struct Pending {Row row{};bool sampled{};};
    static thread_local std::array<Pending,64> pending_;
    inline static thread_local unsigned depth_{};
    std::uintptr_t base_{};Read read_{};
    std::atomic<bool> active_{},failed_{};
    std::atomic<std::uint64_t> next_ga_tick_{},sequence_{},dropped_{},overflow_{},written_{};
    std::uint64_t activation_{},bytes_{};
    std::mutex queue_mutex_;std::array<Row,256> rows_{};std::size_t head_{},count_{};
    std::ofstream output_;
    template<class Value> std::optional<Value> value(std::uintptr_t address) {
        auto reader=[&](std::uintptr_t at,void* out,std::size_t size){return read_ && read_(at,out,size);};
        return rig::value<Value>(reader,address);
    }
    Packet packet(std::uintptr_t address,unsigned state) {
        Packet out{};if(state!=13 && state!=19)return out;
        std::array<unsigned char,56> bytes{};
        if(!read_ || !read_(address,bytes.data(),state==13?56:48))return out;
        std::memcpy(&out.serial,bytes.data(),4);std::memcpy(&out.state,bytes.data()+4,4);
        if(state==13) {
            std::memcpy(&out.victim,bytes.data()+8,4);std::memcpy(&out.outcome,bytes.data()+0x30,4);
            std::memcpy(&out.requester,bytes.data()+0x20,4);std::memcpy(&out.context,bytes.data()+0x28,8);
        }else {std::memcpy(&out.mode,bytes.data()+8,4);std::memcpy(&out.victim,bytes.data()+12,4);}
        out.valid=out.state==state;return out;
    }
    bool victim(Snapshot& out) {
        if(!out.victim || out.victim==0x3fff0010 || (out.victim>>16)>=0x3fff)return false;
        const auto slot=out.owner.game+0x3130b0+(out.victim>>16)*8ull;
        const auto entity=value<std::uintptr_t>(slot);if(!entity || !rig::pointer(*entity))return false;
        if(value<std::uint32_t>(*entity+0xa8)!=out.victim || value<std::uintptr_t>(slot)!=entity)return false;
        out.victim_entity=*entity;return true;
    }
    bool present(const Snapshot& out,const Packet& expected,bool& found) {
        found=false;if(!expected.valid)return false;
        const auto offset=expected.state==13?0x30u:0xb0u;
        const auto stride=expected.state==13?0x38u:0x30u;
        auto reader=[&](std::uintptr_t at,void* buffer,std::size_t size){return read_ && read_(at,buffer,size);};
        const auto list=rig::list(reader,out.state+offset,4096);if(!list)return false;
        unsigned previous{};
        for(unsigned pass=0;pass<2;++pass) {
            unsigned matches{};
            for(unsigned index=0;index<list->packed_count;++index) {
                const auto entry=list->data+index*stride;
                const auto serial=value<std::uint32_t>(entry);if(!serial)return false;
                if(*serial==expected.serial) {if(!(packet(entry,expected.state)==expected))return false;++matches;}
            }
            if(matches>1 || (pass && matches!=previous))return false;previous=matches;
        }
        const auto again=rig::list(reader,out.state+offset,4096);
        if(!again || again->data!=list->data || again->packed_count!=list->packed_count || again->packed_capacity!=list->packed_capacity)return false;
        found=previous==1;return true;
    }
    Snapshot capture(unsigned hook,std::uintptr_t subject,unsigned state,const Packet& expected) {
        Snapshot out{};
        auto reader=[&](std::uintptr_t at,void* buffer,std::size_t size){return read_ && read_(at,buffer,size);};
        const auto machine=value<std::uintptr_t>(subject+(hook==0?0x20u:0x10u));
        const auto owner=machine?native_player_state_lease(reader,base_,*machine):std::nullopt;
        if(!owner)return out;out.owner=*owner;
        if(hook==4) {
            const auto pool=value<std::uintptr_t>(*machine+0x90),sensor=value<std::uintptr_t>(subject+0x88);
            if(!pool || !sensor || !rig::typed(reader,base_,subject,0x1c85d28) || value<std::uint32_t>(subject+8)!=39u ||
               value<std::uintptr_t>(*pool+0x10+39*8)!=subject)return out;
            out=capture(0,*sensor,0,{});
            if(!out.owner_valid || !(out.owner==*owner) || value<std::uintptr_t>(subject+0x88)!=sensor ||
               value<std::uintptr_t>(*machine+0x90)!=pool || value<std::uintptr_t>(*pool+0x10+39*8)!=subject ||
               value<std::uintptr_t>(subject+0x10)!=machine)return Snapshot{};
            out.pool=*pool;out.state=subject;return out;
        }
        if(hook==0) {
            const auto identity=rig::started(reader,subject);
            if(!identity || !rig::typed(reader,base_,subject,0x1b3f998) || value<std::uintptr_t>(subject+0x10)!=owner->player)return out;
            const auto components=rig::list(reader,owner->player+0x268,4096);if(!components)return out;
            for(unsigned pass=0;pass<2;++pass) {
                unsigned membership{};
                for(unsigned index=0;index<components->packed_count;++index) {
                    const auto component=value<std::uintptr_t>(components->data+index*8ull);if(!component)return out;
                    if(*component==subject)++membership;
                }
                const auto again=rig::list(reader,owner->player+0x268,4096);
                if(membership!=1 || !again || again->data!=components->data || again->packed_count!=components->packed_count || again->packed_capacity!=components->packed_capacity)return out;
            }
            out.sensor=subject;out.sensor_identity=*identity;
            const auto handler=value<std::uintptr_t>(subject+0x1d0);const auto kind=value<std::uint32_t>(subject+0x1d8);
            const auto ready=value<std::uint8_t>(subject+0x1dc),enabled=value<std::uint8_t>(subject+0x38);if(!handler || !kind || !ready || !enabled)return out;
            out.handler=*handler;out.kind=*kind;out.ready=*ready;
            constexpr std::array<unsigned,5> map{1,1,1,3,2};
            if(*kind<map.size()) {
                const auto slot=subject+0x40+map[*kind]*0x38ull;
                if(rig::typed(reader,base_,slot,0x1b3f978) && value<std::uintptr_t>(slot+8)==subject &&
                   value<std::uintptr_t>(subject+0x1a8+*kind*8ull)==handler) {
                    const auto target=value<std::uint32_t>(slot+0x20);
                    if(target){out.victim=*target;out.victim_valid=victim(out);out.offer_valid=*enabled!=0 && *ready==1 && *handler && out.victim_valid;}
                }
            }
            if(rig::started(reader,subject)!=identity || value<std::uintptr_t>(subject+0x1d0)!=handler ||
               value<std::uint32_t>(subject+0x1d8)!=kind || value<std::uint8_t>(subject+0x1dc)!=ready || value<std::uint8_t>(subject+0x38)!=enabled ||
               value<std::uintptr_t>(subject+0x20)!=machine)return Snapshot{};
        }else {
            if(state!=13 && state!=19)return out;
            const auto pool=value<std::uintptr_t>(*machine+0x90);if(!pool || !rig::pointer(*pool))return out;
            const auto selected=value<std::uintptr_t>(*pool+0x10+state*8ull);if(!selected || !*selected || *selected!=subject)return out;
            const auto table=state==13?0x1c83468u:0x1c79478u;
            if(!rig::typed(reader,base_,*selected,table) || value<std::uint32_t>(*selected+8)!=state || value<std::uintptr_t>(*selected+0x10)!=machine)return out;
            out.pool=*pool;out.state=*selected;
            const auto active=value<std::uint8_t>(*selected+0x20);
            const auto spawn=value<std::uint32_t>(*selected+(state==13?0xc4u:0x124u));
            if(!active || *active>1 || !spawn)return out;
            out.active=*active;out.victim=*spawn;out.victim_valid=victim(out);
            if(state==19){const auto mode=value<std::uint32_t>(*selected+0x120);if(!mode)return out;out.mode=*mode;}
            out.packet_search_valid=present(out,expected,out.packet_present);
            if(value<std::uintptr_t>(*machine+0x90)!=pool || value<std::uintptr_t>(*pool+0x10+state*8ull)!=selected ||
               value<std::uint8_t>(*selected+0x20)!=active || value<std::uint32_t>(*selected+(state==13?0xc4u:0x124u))!=spawn ||
               value<std::uintptr_t>(*selected+0x10)!=machine || !rig::typed(reader,base_,*selected,table) ||
               (state==19 && value<std::uint32_t>(*selected+0x120)!=out.mode) || (out.victim_valid && !victim(out)))return Snapshot{};
            out.state_valid=true;
        }
        if(!(native_player_state_lease(reader,base_,*machine)==owner))return Snapshot{};
        out.owner_valid=true;return out;
    }
    void enqueue(const Row& row) {
        std::unique_lock<std::mutex> lock(queue_mutex_,std::try_to_lock);
        if(!lock.owns_lock() || count_==rows_.size()){++dropped_;return;}
        rows_[(head_+count_)%rows_.size()]=row;++count_;
    }
    void begin(unsigned hook,std::uintptr_t caller,std::uintptr_t subject,int state,const void* params) {
        ++depth_;if(depth_>pending_.size()){++overflow_;return;}
        auto& pending=pending_[depth_-1];pending={};if(!active_)return;
        const auto tick=GetTickCount64();
        if(hook==0) {
            const auto game=value<std::uintptr_t>(base_+0x39a5290);
            const auto player=game?value<std::uintptr_t>(*game+0x3130b0):std::nullopt;
            if(!player || value<std::uintptr_t>(subject+0x10)!=player)return;
            auto previous=next_ga_tick_.load();if(tick<previous || !next_ga_tick_.compare_exchange_strong(previous,tick+100))return;
        }else if(hook!=4 && state!=13 && state!=19)return;
        pending.sampled=true;auto& row=pending.row;
        row.activation=activation_;row.id=sequence_.fetch_add(1)+1;row.epoch=activation_;
        for(unsigned ancestor=depth_-1;ancestor>0;--ancestor)if(pending_[ancestor-1].sampled){row.parent=pending_[ancestor-1].row.id;break;}
        row.begin_tick=tick;row.thread=GetCurrentThreadId();row.hook=hook;row.depth=depth_;row.caller=caller>=base_?caller-base_:0;
        row.subject=subject;row.state_id=hook==4?39u:static_cast<unsigned>(state);row.packet=packet(reinterpret_cast<std::uintptr_t>(params),row.state_id);
        row.before=capture(hook,subject,row.state_id,row.packet);
    }
    void end_body(unsigned hook,std::uintptr_t subject,bool completed,bool current,bool result) {
        if(depth_>pending_.size())return;
        auto& pending=pending_[depth_-1];if(!pending.sampled)return;
        auto& row=pending.row;row.end_tick=GetTickCount64();row.completed=completed;row.current=current && active_;row.native_result=result;
        row.after=capture(hook,subject,row.state_id,row.packet);
        row.same_owner=row.before.owner_valid && row.after.owner_valid && row.before.owner==row.after.owner;
        row.accepted_copy=(hook==1 || hook==5) && result && completed && row.current && row.same_owner && row.packet.valid &&
            row.before.state_valid && row.after.state_valid && row.before.pool==row.after.pool && row.before.state==row.after.state &&
            row.before.packet_search_valid && !row.before.packet_present && row.after.packet_search_valid && row.after.packet_present;
        row.active_victim_match=(hook==2 || hook==3) && completed && row.current && row.same_owner && row.packet.valid && row.after.state_valid &&
            row.after.active==1 && row.after.victim_valid && row.after.victim==row.packet.victim &&
            row.before.pool==row.after.pool && row.before.state==row.after.state && (row.state_id!=19 || row.after.mode==row.packet.mode);
        enqueue(row);
    }
    void end(unsigned hook,std::uintptr_t subject,bool completed,bool current,bool result) {
        if(!depth_)return;
        __try {end_body(hook,subject,completed,current,result);}__finally {if(depth_<=pending_.size())pending_[depth_-1]={};--depth_;}
    }
    static void observe(void* context,unsigned phase,unsigned hook,std::uintptr_t caller,std::uintptr_t subject,int state,const void* params,bool completed,bool current,bool result) {
        auto& owner=*static_cast<NativeAssassinationReceipt*>(context);
        if(phase==0)owner.begin(hook,caller,subject,state,params);else owner.end(hook,subject,completed,current,result);
    }
    static void write_snapshot(std::ostream& out,const Snapshot& snapshot) {
        out<<"{\"owner_valid\":"<<snapshot.owner_valid<<",\"game\":"<<snapshot.owner.game<<",\"player\":"<<snapshot.owner.player
            <<",\"player_spawn\":"<<snapshot.owner.spawn<<",\"machine\":"<<snapshot.owner.machine<<",\"generation\":"<<snapshot.owner.identity.generation
            <<",\"identity_flags\":"<<snapshot.owner.identity.flags<<",\"pool\":"<<snapshot.pool<<",\"state\":"<<snapshot.state
            <<",\"state_valid\":"<<snapshot.state_valid<<",\"active\":"<<unsigned(snapshot.active)<<",\"victim\":"<<snapshot.victim
            <<",\"victim_entity\":"<<snapshot.victim_entity<<",\"victim_valid\":"<<snapshot.victim_valid<<",\"mode\":"<<snapshot.mode
            <<",\"sensor\":"<<snapshot.sensor<<",\"sensor_generation\":"<<snapshot.sensor_identity.generation<<",\"handler\":"<<snapshot.handler
            <<",\"kind\":"<<snapshot.kind<<",\"ready\":"<<unsigned(snapshot.ready)<<",\"offer_valid\":"<<snapshot.offer_valid
            <<",\"packet_search_valid\":"<<snapshot.packet_search_valid<<",\"packet_present\":"<<snapshot.packet_present<<'}';
    }
};
inline thread_local std::array<NativeAssassinationReceipt::Pending,64> NativeAssassinationReceipt::pending_{};
}
