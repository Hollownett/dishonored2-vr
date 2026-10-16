#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace d2vr {
class NativeDoorAnimation {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Input=void (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
    using Evaluate=std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t);
    using Retire=void (__fastcall*)(std::uintptr_t,std::uintptr_t);
    using Parent=void (__fastcall*)(std::uintptr_t,unsigned char);
    using Insert=unsigned (__fastcall*)(std::uintptr_t,std::uintptr_t,unsigned);
    using Update=unsigned char (__fastcall*)(std::uintptr_t,std::uint32_t);
    enum class RequestKind {Open,ClosedEndpoint};
    struct Operation {
        std::uint64_t ownership{},world{},player_generation{},part_generation{};
        std::uintptr_t player{},part{},animation{},container{},binding{};
        float desired_seconds{};
        unsigned char opening_clip{},closing_clip{};
        bool close_authorized{},linked{};
    };
    struct Status {
        std::uint64_t input_generation{},desired_revision{},evaluated_revision{};
        float desired_seconds{},applied_seconds{},duration_seconds{};
        bool published{},input_owned{},evaluated{},zero_ack{},closing{};
    };
    inline static constexpr std::array<std::uintptr_t,6> rvas{0x6e1fe0,0x6eb070,0x6eafc0,0x6e4af0,0x6e3fa0,0x6e4850};
    inline static constexpr std::array<unsigned,6> prefixes{7,5,5,7,5,5};
    inline static constexpr std::array<std::array<unsigned char,32>,6> prologues{{
        {0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xf2,0x41,0x0f,0x10,0x48,0x10,0x41,0x0f,0x10,0,0x4c,0x63,0x42,4,0x4c,3,0,0x48,0x8d,0x44,0x24,0x30,0xf2,0x0f,0x11},
        {0x48,0x89,0x5c,0x24,8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0xa0,0,0,0,0x4c,0x8b,0x4a,0x18,0xf3},
        {0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x81,0xa0,0,0,0,0x48,0x8b,0xfa,0x48,0x8b,0x52,0x18,0x48,0x8b,8},
        {0x48,0x8b,0xc4,0x48,0x89,0x48,8,0x55,0x41,0x54,0x41,0x57,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xa0,0,0,0,0x48,0x89,0x70,0xd8,0x48,0x89,0x78,0xd0,0x4c},
        {0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x8b,0x49,0x0c,0x41,0x8b,0xf8,0x8b,0x43,8,0x44,0x8b,0xc1,0x48,0x8b},
        {0x48,0x89,0x5c,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0xd9,0x48,0x8b,9,0x0f,0x29,0x74,0x24,0x40,0x48,0x8d,0x54,0x24,0x60,0x0f,0x29}}};
    inline static constexpr std::array<std::array<unsigned char,8>,6> unwinds{{
        {1,4,1,0,4,0x42,0,0},{1,5,2,0,5,0x34,1,0},{1,5,2,0,5,0x34,1,0},
        {1,0,0,0,0,0,0,0},{1,5,2,0,5,0x34,1,0},{1,5,2,0,5,0x34,4,0}}};
    inline static constexpr std::array<unsigned,6> profile_sizes{118,160,176,1760,448,469};
    inline static constexpr std::array<std::uint64_t,6> profile_hashes{0x311e504591585131ull,0x939f4de46b651986ull,0xf420ee6ca94e580cull,0xf11c59744626d909ull,0x6c8c6360081cb7c7ull,0x017ed628b7d5ba7aull};
    static NativeDoorAnimation& process_owner() {static auto* owner=new NativeDoorAnimation;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;
        std::array<unsigned char,32> bytes{};
        for(unsigned index=0;index<6;++index)if(!safe_read(read,base+rvas[index],bytes.data(),bytes.size()) || bytes!=prologues[index])return false;
        for(unsigned index=0;index<6;++index) {
            std::array<unsigned char,1760> body{};if(!safe_read(read,base+rvas[index],body.data(),profile_sizes[index]))return false;
            std::uint64_t hash=14695981039346656037ull;for(unsigned offset=0;offset<profile_sizes[index];++offset)hash=(hash^body[offset])*1099511628211ull;
            if(hash!=profile_hashes[index])return false;
        }
        return true;
    }
    bool install(std::uintptr_t base,Read read) noexcept {
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);
        if(!guard.owns_lock() || !ready_for_install() || (base_ && (base_!=base || read_!=read)) || !verify(base,read))return false;
        base_=base;read_=read;stopping_=true;stop_complete_=false;unwind_ready_=false;
        const auto callbacks=hooks();
        for(unsigned index=0;index<6;++index)if(!created_[index]) {
            void* trampoline{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),callbacks[index],&trampoline)!=MH_OK)return rollback();
            originals_[index]=trampoline;created_[index]=true;if(!trampoline)return rollback();
        }
        for(unsigned index=0;index<6;++index)if(!prepare_trampoline(index))return rollback();
        if(!verify(base,read))return rollback();unwind_ready_=true;
        for(const unsigned index:{2u,0u,1u,3u,4u,5u}) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();installed_[index]=true;
        }
        stopping_=false;return true;
    }
    bool enabled() const noexcept {return installed() && !stopping_;}
    bool installed(unsigned index) const noexcept {return index<6 && installed_[index];}
    bool installed() const noexcept {for(unsigned index=0;index<6;++index)if(!installed(index))return false;return true;}
    bool created(unsigned index) const noexcept {return index<6 && created_[index];}
    void* original(unsigned index) const noexcept {return index<6?originals_[index].load():nullptr;}
    bool unwind_registered() const noexcept {return unwind_ready_;}
    unsigned in_flight() const noexcept {return in_flight_;}
    std::uint64_t substitutions() const noexcept {return substitutions_;}
    std::uint64_t inputs() const noexcept {return sequence_;}
    bool stopped() const noexcept {if(!stop_complete_ || in_flight())return false;for(unsigned index=0;index<6;++index)if(installed(index))return false;return true;}
    bool ready_for_install() const noexcept {if(!stopped())return false;for(unsigned index=0;index<6;++index)if(created(index) && !original(index))return false;return true;}
    bool publish(const Operation& operation) noexcept {
        if(!enabled() || !valid(operation))return false;std::lock_guard<std::mutex> guard(state_);
        if(!enabled())return false;
        if(revision_==UINT64_MAX) {for(auto& receipt:receipts_)receipt={};return false;}
        for(auto& receipt:receipts_)if(receipt.published && (receipt.operation.world!=operation.world || receipt.operation.player!=operation.player || receipt.operation.player_generation!=operation.player_generation))receipt={};
        for(auto& receipt:receipts_)if(receipt.published && same(receipt.operation,operation)) {if(receipt.operation.desired_seconds!=operation.desired_seconds || receipt.operation.close_authorized!=operation.close_authorized) {receipt.revision=++revision_;clear_evaluation(receipt);}receipt.operation=operation;return true;}
        for(auto& receipt:receipts_)if(receipt.published && (receipt.operation.container==operation.container || receipt.operation.part==operation.part)) {receipt={};receipt.published=true;receipt.operation=operation;receipt.revision=++revision_;return true;}
        for(auto& receipt:receipts_)if(!receipt.published) {receipt={};receipt.published=true;receipt.operation=operation;receipt.revision=++revision_;return true;}
        return false;
    }
    Status status(const Operation& operation) noexcept {
        std::lock_guard<std::mutex> guard(state_);if(!enabled())return {};
        for(const auto& receipt:receipts_)if(receipt.published && same(receipt.operation,operation) && receipt.operation.desired_seconds==operation.desired_seconds && receipt.operation.close_authorized==operation.close_authorized) {
            Status result{};result.published=true;result.desired_seconds=receipt.operation.desired_seconds;result.desired_revision=receipt.revision;
            result.input_generation=receipt.generation;result.input_owned=receipt.owned && receipt.input_completed;result.closing=result.input_owned && receipt.closing;
            result.evaluated=result.input_owned && receipt.evaluated_revision==receipt.revision && receipt.evaluated_revision;
            if(result.evaluated) {result.evaluated_revision=receipt.evaluated_revision;result.applied_seconds=receipt.applied_seconds;result.duration_seconds=receipt.duration_seconds;result.zero_ack=receipt.zero_ack;}
            return result;
        }
        return {};
    }
    void revoke(std::uint64_t ownership=0) noexcept {
        std::lock_guard<std::mutex> guard(state_);for(auto& receipt:receipts_)if(!ownership || receipt.operation.ownership==ownership)receipt={};
    }
    bool close_ready(const Operation& operation) noexcept {
        std::lock_guard<std::mutex> guard(state_);for(const auto& receipt:receipts_)if(receipt.published && receipt.owned && receipt.input_completed && same(receipt.operation,operation) && receipt.operation.desired_seconds==operation.desired_seconds)return enabled() && receipt.evaluated_revision==receipt.revision && receipt.zero_ack && receipt.operation.close_authorized && !receipt.closing;return false;
    }
    template<class Invoke> decltype(auto) request_call(const Operation& operation,RequestKind kind,Invoke&& invoke) {
        Request frame{request_,this,operation,kind};auto* previous=request_;request_=&frame;
        __try {return invoke();}__finally {request_=previous;}
    }
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        stopping_=true;revoke();if(callback_depth_)return false;
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);return guard.owns_lock() && stop_locked(timeout_ms);
    }
    void invoke_input(std::uintptr_t container,std::uintptr_t binding,std::uintptr_t input) {
        ++in_flight_;++callback_depth_;
        __try {input_body(container,binding,input);}__finally {--callback_depth_;--in_flight_;}
    }
    std::uintptr_t invoke_evaluate(std::uintptr_t schema,std::uintptr_t holder) {
        Evaluation frame{};frame.previous=evaluation_;frame.owner=this;frame.schema=schema;frame.holder=holder;
        evaluation_=&frame;++in_flight_;++callback_depth_;
        __try {return evaluate_body(frame);}__finally {evaluation_=frame.previous;--callback_depth_;--in_flight_;}
    }
    void invoke_retire(std::uintptr_t schema,std::uintptr_t holder) {
        ++in_flight_;++callback_depth_;
        __try {retire_body(schema,holder);}__finally {--callback_depth_;--in_flight_;}
    }
    void invoke_parent(std::uintptr_t context,unsigned char changed,std::uintptr_t caller) {
        ParentFrame frame{};frame.previous=parent_;frame.owner=this;frame.context=context;frame.slot=2;
        ++in_flight_;++callback_depth_;parent_=&frame;
        __try {parent_body(frame,changed,caller);}__finally {parent_=frame.previous;--callback_depth_;--in_flight_;}
    }
    unsigned invoke_insert(std::uintptr_t list,std::uintptr_t entry,unsigned index,std::uintptr_t caller) {
        bool locked=false;++in_flight_;++callback_depth_;
        __try {return insert_body(list,entry,index,caller,locked);}__finally {if(locked)state_.unlock();--callback_depth_;--in_flight_;}
    }
    unsigned char invoke_update(std::uintptr_t entry,std::uint32_t raw_dt,std::uintptr_t caller) {
        Prepared frame{};frame.entry=entry;++in_flight_;++callback_depth_;
        __try {prepare_update(frame,caller,raw_dt);return update_body(frame,raw_dt);}__finally {__try {finish_update(frame);}__finally {--callback_depth_;--in_flight_;}}
    }
private:
    friend struct NativeDoorAnimationFixture;
    NativeDoorAnimation()=default;
    struct Request {Request* previous;NativeDoorAnimation* owner;Operation operation;RequestKind kind;};
    struct Receipt {
        Operation operation{};std::uintptr_t holder{},arena{},schema{},destination{};std::uint32_t context_offset{};
        std::uint64_t generation{},opening_instance{},revision{},evaluated_revision{};std::array<unsigned char,24> input{};
        float applied_seconds{},duration_seconds{};
        std::array<std::uint64_t,4> instances{};unsigned count{};bool published{},owned{},closing{},zero_ack{},ledger{},input_completed{};
    };
    struct Evaluation {Evaluation* previous{};NativeDoorAnimation* owner{};std::uintptr_t schema{},holder{},arena{};std::uint32_t size{};};
    struct ParentFrame {ParentFrame* previous{};NativeDoorAnimation* owner{};std::uintptr_t context{};std::uint64_t generation{};unsigned slot{2};};
    struct Prepared {std::uintptr_t entry{};std::uint64_t generation{},revision{};float rate{},desired{},duration{};unsigned index{},slot{2};bool locked{},indexed{},written{},completed{};unsigned char result{};};
    struct Metadata {std::array<RUNTIME_FUNCTION,2> functions{};std::array<unsigned char,8> unwind{};std::array<unsigned char,4> relay{1,0,0,0};};
    static bool valid(const Operation& value) noexcept {
        return value.ownership && value.world && value.player_generation && value.part_generation && value.player && value.part && value.animation &&
            value.container==value.animation+0x338 && value.binding && value.opening_clip!=0xff && value.closing_clip!=0xff && !value.linked &&
            std::isfinite(value.desired_seconds) && value.desired_seconds>=0.f;
    }
    static bool same(const Operation& first,const Operation& second) noexcept {
        return first.ownership==second.ownership && first.world==second.world && first.player_generation==second.player_generation &&
            first.part_generation==second.part_generation && first.player==second.player && first.part==second.part && first.animation==second.animation &&
            first.container==second.container && first.binding==second.binding && first.opening_clip==second.opening_clip && first.closing_clip==second.closing_clip && !first.linked && !second.linked;
    }
    static void clear_evaluation(Receipt& receipt) noexcept {receipt.zero_ack=false;receipt.evaluated_revision=0;receipt.applied_seconds=0;receipt.duration_seconds=0;}
    static bool cpp_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return read && read(address,output,size);}catch(...) {return false;}}
    static bool safe_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {__try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    template<class Value> bool get(std::uintptr_t address,Value& value) const noexcept {return safe_read(read_,address,&value,sizeof(value));}
    static bool local_read(std::uintptr_t address,void* output,std::size_t size) noexcept {__try {std::memcpy(output,reinterpret_cast<void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    static void write_float(std::uintptr_t address,float value) {std::memcpy(reinterpret_cast<void*>(address),&value,4);}
    bool unique_context(std::uintptr_t schema,std::uintptr_t binding,std::uint32_t size,std::uint32_t& offset) const noexcept {
        std::uintptr_t buckets{},key{};unsigned bucket_count{},node_count{},found{};
        if(binding<8 || !get(binding-8,key) || !get(schema+0x58,buckets) || !get(schema+0x60,bucket_count) || !get(schema+0x64,node_count) ||
           !buckets || !bucket_count || bucket_count>4096 || node_count>4096)return false;
        unsigned visited{};
        for(unsigned bucket=0;bucket<bucket_count;++bucket) {
            std::uintptr_t record{};if(!get(buckets+bucket*8ull,record))return false;
            while(record) {
                if(++visited>node_count)return false;
                std::uintptr_t node{},vtable{},input_key{},next{};unsigned char mode{};std::int32_t current_offset{};
                if(!get(record+8,node) || !get(record+0x10,current_offset) || !get(record+0x18,next) || !node || !get(node,vtable))return false;
                if(vtable==base_+0x1c4bce8) {
                    if(!get(node+0x590,mode) || !get(node+0x5b0,input_key))return false;
                    if(mode==1 && input_key==key) {if(++found!=1 || current_offset<0 || static_cast<std::uint32_t>(current_offset)>size || size-static_cast<std::uint32_t>(current_offset)<0xb78)return false;offset=static_cast<std::uint32_t>(current_offset);}
                }
                record=next;
            }
        }
        return visited==node_count && found==1;
    }
    void input_body(std::uintptr_t container,std::uintptr_t binding,std::uintptr_t input) {
        const auto original_input=reinterpret_cast<Input>(original(0));if(!original_input)return;
        std::uint64_t generation{};
        {
            std::lock_guard<std::mutex> guard(state_);if(sequence_.load()!=UINT64_MAX)generation=++sequence_;
            for(auto& receipt:receipts_)if(receipt.published && receipt.operation.container==container) {
                const bool was_owned=receipt.owned && receipt.input_completed,was_zero=receipt.zero_ack && receipt.evaluated_revision==receipt.revision;
                receipt.owned=false;receipt.input_completed=false;clear_evaluation(receipt);
                auto* request=request_;
                if(!generation || !enabled() || !request || request->owner!=this || !same(receipt.operation,request->operation) || binding!=receipt.operation.binding)continue;
                std::uintptr_t holder{},arena{},schema{},destination{};std::int32_t input_offset{};std::uint32_t size{},context_offset{};
                std::array<unsigned char,24> bytes{};
                if(!get(container,holder) || !get(container+8,schema) || !holder || !schema || !get(holder,arena) || !arena || !get(schema+0xc0,size) || size>16*1024*1024 ||
                   !get(binding+4,input_offset) || input_offset<0 || static_cast<std::uint32_t>(input_offset)>size || size-static_cast<std::uint32_t>(input_offset)<24 ||
                   !safe_read(read_,input,bytes.data(),bytes.size()) || !unique_context(schema,binding,size,context_offset))continue;
                destination=arena+static_cast<std::uint32_t>(input_offset);
                int mode{};float weight{},rate{},start{};std::memcpy(&weight,bytes.data()+4,4);std::memcpy(&mode,bytes.data()+8,4);std::memcpy(&rate,bytes.data()+12,4);std::memcpy(&start,bytes.data()+16,4);
                if(weight!=1.f || rate!=1.f || start!=0.f)continue;
                const bool closing=request->kind==RequestKind::ClosedEndpoint;
                if(closing) {
                    if(!was_owned || !was_zero || receipt.closing || !receipt.opening_instance || !receipt.operation.close_authorized || receipt.operation.desired_seconds!=0.f || mode!=4 || bytes[0]!=receipt.operation.closing_clip)continue;
                } else {
                    if(mode!=1 || bytes[0]!=receipt.operation.opening_clip)continue;
                    receipt.instances={};receipt.count=0;receipt.opening_instance=0;receipt.ledger=false;receipt.closing=false;
                }
                if(closing && (receipt.holder!=holder || receipt.arena!=arena || receipt.schema!=schema || receipt.destination!=destination || receipt.context_offset!=context_offset))continue;
                receipt.holder=holder;receipt.arena=arena;receipt.schema=schema;receipt.destination=destination;receipt.context_offset=context_offset;
                receipt.input=bytes;receipt.generation=generation;receipt.owned=true;receipt.closing=closing;
            }
        }
        original_input(container,binding,input);
        {std::lock_guard<std::mutex> guard(state_);for(auto& receipt:receipts_)if(receipt.owned && receipt.generation==generation)receipt.input_completed=true;}
    }
    std::uintptr_t evaluate_body(Evaluation& frame) {
        if(!get(frame.holder,frame.arena) || !get(frame.schema+0xc0,frame.size) || frame.size>16*1024*1024)frame.arena=0;
        const auto evaluator=reinterpret_cast<Evaluate>(original(1));return evaluator?evaluator(frame.schema,frame.holder):0;
    }
    void retire_body(std::uintptr_t schema,std::uintptr_t holder) {
        {std::lock_guard<std::mutex> guard(state_);for(auto& receipt:receipts_)if(receipt.holder==holder)receipt={};}
        const auto retire=reinterpret_cast<Retire>(original(2));if(retire)retire(schema,holder);
    }
    bool canonical(std::uintptr_t context,unsigned& count) const noexcept {
        std::uintptr_t data{};std::uint32_t packed{},capacity{};
        if(!get(context+0x9b0,data) || data!=context+0x9c0 || !get(context+0x9b8,packed) || packed>>24!=5 || !get(context+0x9bc,capacity) || capacity!=0x440)return false;
        count=packed&0xffffff;return count<=4;
    }
    void parent_body(ParentFrame& frame,unsigned char changed,std::uintptr_t caller) {
        {
            std::lock_guard<std::mutex> guard(state_);auto* evaluation=evaluation_;
            if(enabled() && evaluation && evaluation->owner==this && evaluation->arena && caller==base_+0x6e3f2c && !frame.previous) {
                for(unsigned slot=0;slot<2;++slot) {
                    auto& receipt=receipts_[slot];unsigned count{};std::uintptr_t time{},events{},source{};std::array<unsigned char,24> copied{};
                    if(!receipt.published || !receipt.owned || !receipt.input_completed || receipt.holder!=evaluation->holder || receipt.schema!=evaluation->schema || receipt.arena!=evaluation->arena ||
                       frame.context!=evaluation->arena+receipt.context_offset || receipt.context_offset>evaluation->size || evaluation->size-receipt.context_offset<0xb78 ||
                       !get(frame.context+0x10,time) || time!=receipt.operation.container+0x38 || !get(frame.context+0x18,events) || events!=receipt.operation.container+0x20 ||
                       !get(frame.context+0x9a0,source) || source!=receipt.destination || !safe_read(read_,frame.context+0x988,copied.data(),copied.size()) ||
                       copied[0]!=receipt.input[0] || std::memcmp(copied.data()+4,receipt.input.data()+4,0x11) || !canonical(frame.context,count))continue;
                    if(!receipt.ledger) {receipt.count=count;for(unsigned index=0;index<count;++index)receipt.instances[index]=++instance_;receipt.ledger=true;}
                    if(receipt.count!=count) {receipt.owned=false;continue;}
                    frame.slot=slot;frame.generation=receipt.generation;break;
                }
            }
        }
        const auto parent=reinterpret_cast<Parent>(original(3));if(parent)parent(frame.context,changed);
        if(frame.slot<2) {std::lock_guard<std::mutex> guard(state_);auto& receipt=receipts_[frame.slot];unsigned count{};
            if(receipt.generation==frame.generation && (!canonical(frame.context,count) || receipt.count!=count))receipt.owned=false;}
    }
    unsigned insert_body(std::uintptr_t list,std::uintptr_t entry,unsigned index,std::uintptr_t caller,bool& locked) {
        const auto insert=reinterpret_cast<Insert>(original(4));if(!insert)return 0;
        auto* parent=parent_;if(!parent || parent->owner!=this || parent->slot>=2 || caller!=base_+0x6e504b || list!=parent->context+0x9b0 || index!=0)return insert(list,entry,index);
        state_.lock();locked=true;auto& receipt=receipts_[parent->slot];unsigned count{};
        if(!receipt.owned || receipt.generation!=parent->generation || !canonical(parent->context,count) || count>3 || receipt.count<count) {receipt.owned=false;state_.unlock();locked=false;return insert(list,entry,index);}
        for(unsigned removed=count;removed<receipt.count;++removed)if(receipt.instances[removed]==receipt.opening_instance)receipt.owned=false;
        receipt.count=count;state_.unlock();locked=false;const auto result=insert(list,entry,index);
        state_.lock();locked=true;
        if(!receipt.owned || receipt.generation!=parent->generation)return result;
        unsigned after{};if(result!=0 || !canonical(parent->context,after) || after!=count+1 || receipt.count!=count) {receipt.owned=false;return result;}
        for(unsigned current=count;current>0;--current)receipt.instances[current]=receipt.instances[current-1];
        receipt.instances[0]=++instance_;receipt.count=count+1;
        if(!receipt.closing && !receipt.opening_instance)receipt.opening_instance=receipt.instances[0];return result;
    }
    void prepare_update(Prepared& frame,std::uintptr_t caller,std::uint32_t raw_dt) {
        auto* parent=parent_;if(!parent || parent->owner!=this || parent->slot>=2 || caller!=base_+0x6e4d10)return;
        state_.lock();frame.locked=true;frame.slot=parent->slot;frame.generation=parent->generation;
        __try {
            auto& receipt=receipts_[frame.slot];unsigned count{};const auto data=parent->context+0x9c0;
            if(!receipt.owned || receipt.generation!=parent->generation || !canonical(parent->context,count) || count!=receipt.count || frame.entry<data || (frame.entry-data)%0x28)return;
            const auto index=(frame.entry-data)/0x28;if(index>=count)return;frame.index=static_cast<unsigned>(index);frame.indexed=true;
            int mode{};float time{},duration{},delta{};std::uintptr_t clip{},descriptor{};std::memcpy(&delta,&raw_dt,4);
            if(!enabled() || receipt.instances[index]!=receipt.opening_instance || !receipt.opening_instance || !get(frame.entry+0x1c,mode) || mode!=1 ||
               !std::isfinite(delta) || delta<0.f ||
               !get(frame.entry+0x10,frame.rate) || frame.rate!=1.f || !get(frame.entry+0x14,time) || !std::isfinite(time) || !get(frame.entry,clip) || !clip ||
               !get(clip+0x30,descriptor) || !descriptor || !get(descriptor+0x38,duration) || !std::isfinite(duration) || duration<=0.f ||
               receipt.operation.desired_seconds>=duration)return;
            frame.desired=receipt.operation.desired_seconds;frame.revision=receipt.revision;frame.duration=duration;write_float(frame.entry+0x14,frame.desired);write_float(frame.entry+0x10,0.f);frame.written=true;++substitutions_;
        }__finally {state_.unlock();frame.locked=false;}
    }
    unsigned char update_body(Prepared& frame,std::uint32_t raw_dt) {
        const auto update=reinterpret_cast<Update>(original(5));frame.result=update?update(frame.entry,raw_dt):0;frame.completed=true;return frame.result;
    }
    void finish_update(Prepared& frame) {
        if(frame.written)write_float(frame.entry+0x10,frame.rate);
        float applied{};const bool observed=frame.written && frame.completed && local_read(frame.entry+0x14,&applied,sizeof(applied)) && std::isfinite(applied) && applied==frame.desired;
        if(frame.indexed && frame.completed) {state_.lock();frame.locked=true;}
        __try {
            if(frame.locked && frame.indexed && frame.completed) {auto& receipt=receipts_[frame.slot];if(!receipt.owned || receipt.generation!=frame.generation)return;
                if(frame.written && observed && receipt.input_completed && receipt.revision==frame.revision && receipt.operation.desired_seconds==frame.desired) {
                    receipt.evaluated_revision=frame.revision;receipt.applied_seconds=applied;receipt.duration_seconds=frame.duration;receipt.zero_ack=applied==0.f;
                }
                if(!frame.result && frame.index<receipt.count) {for(unsigned index=frame.index;index+1<receipt.count;++index)receipt.instances[index]=receipt.instances[index+1];--receipt.count;}
            }
        }__finally {if(frame.locked)state_.unlock();}
    }
    static void __fastcall input_hook(std::uintptr_t container,std::uintptr_t binding,std::uintptr_t input) {process_owner().invoke_input(container,binding,input);}
    static std::uintptr_t __fastcall evaluate_hook(std::uintptr_t schema,std::uintptr_t holder) {return process_owner().invoke_evaluate(schema,holder);}
    static void __fastcall retire_hook(std::uintptr_t schema,std::uintptr_t holder) {process_owner().invoke_retire(schema,holder);}
    static void __fastcall parent_hook(std::uintptr_t context,unsigned char changed) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());process_owner().invoke_parent(context,changed,caller);}
    static unsigned __fastcall insert_hook(std::uintptr_t list,std::uintptr_t entry,unsigned index) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return process_owner().invoke_insert(list,entry,index,caller);}
    static unsigned char __fastcall update_hook(std::uintptr_t entry,std::uint32_t raw_dt) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return process_owner().invoke_update(entry,raw_dt,caller);}
    static std::array<void*,6> hooks() noexcept {return {reinterpret_cast<void*>(&input_hook),reinterpret_cast<void*>(&evaluate_hook),reinterpret_cast<void*>(&retire_hook),reinterpret_cast<void*>(&parent_hook),reinterpret_cast<void*>(&insert_hook),reinterpret_cast<void*>(&update_hook)};}
    static std::array<unsigned char,14> tail(std::uintptr_t target,bool owned) noexcept {
        std::array<unsigned char,14> code{};if(owned) {code[0]=0x49;code[1]=0xbb;std::memcpy(code.data()+2,&target,8);code[10]=0x41;code[11]=0xff;code[12]=0xe3;code[13]=0x90;}
        else {code[0]=0xff;code[1]=0x25;std::memcpy(code.data()+6,&target,8);}return code;
    }
    bool restore_protection(unsigned index) noexcept {
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));DWORD ignored{};
        if(protection_pending_[index]) {if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefixes[index]),14,protection_[index],&ignored))return false;protection_pending_[index]=false;}
        if(cache_pending_[index]) {if(!FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(trampoline),prefixes[index]+14))return false;cache_pending_[index]=false;}return true;
    }
    bool prepare_trampoline(unsigned index) noexcept {
        if(!restore_protection(index))return false;
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));const auto prefix=prefixes[index];std::array<unsigned char,35> code{};
        const auto replacement=tail(base_+rvas[index]+prefix,true),expected=tail(base_+rvas[index]+prefix,patched_[index]),relay=tail(reinterpret_cast<std::uintptr_t>(hooks()[index]),false);
        if(!trampoline || !local_read(trampoline,code.data(),prefix+28) || std::memcmp(code.data(),prologues[index].data(),prefix) ||
           std::memcmp(code.data()+prefix,expected.data(),14) || std::memcmp(code.data()+prefix+14,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            const auto aligned=trampoline&~std::uintptr_t{0xffff};
            for(unsigned slot=1;slot<=4096 && aligned>slot*0x10000ull;++slot) {metadata=static_cast<Metadata*>(VirtualAlloc(reinterpret_cast<void*>(aligned-slot*0x10000ull),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(metadata)break;}
            if(!metadata)return false;metadata_[index]=metadata;const auto base=reinterpret_cast<std::uintptr_t>(metadata);
            if(base>=trampoline || trampoline-base>UINT32_MAX-35)return false;const auto begin=static_cast<DWORD>(trampoline-base);
            metadata->functions={RUNTIME_FUNCTION{begin,begin+prefix+14,static_cast<DWORD>(offsetof(Metadata,unwind))},RUNTIME_FUNCTION{begin+prefix+14,begin+prefix+28,static_cast<DWORD>(offsetof(Metadata,relay))}};metadata->unwind=unwinds[index];metadata->relay={1,0,0,0};
        }
        if(!patched_[index]) {
            DWORD previous{};if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefix),14,PAGE_EXECUTE_READWRITE,&previous))return false;
            protection_[index]=previous;std::memcpy(reinterpret_cast<void*>(trampoline+prefix),replacement.data(),14);patched_[index]=true;protection_pending_[index]=true;cache_pending_[index]=true;
            if(!restore_protection(index))return false;
        }
        const auto base=reinterpret_cast<std::uintptr_t>(metadata);const auto begin=static_cast<DWORD>(trampoline-base);
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{{begin,begin+prefix+14,static_cast<DWORD>(offsetof(Metadata,unwind))},{begin+prefix+14,begin+prefix+28,static_cast<DWORD>(offsetof(Metadata,relay))}}};
        if(std::memcmp(metadata->functions.data(),expected_functions.data(),sizeof(expected_functions)) || metadata->unwind!=unwinds[index] || metadata->relay!=std::array<unsigned char,4>{1,0,0,0})return false;
        if(!registered_[index]) {
            for(unsigned offset=0;offset<prefix+28;++offset) {DWORD64 lookup{};if(RtlLookupFunctionEntry(trampoline+offset,&lookup,nullptr))return false;}
            if(!RtlAddFunctionTable(metadata->functions.data(),2,base))return false;registered_[index]=true;
        }
        DWORD64 lookup{};return local_read(trampoline+prefix,code.data(),14) && std::memcmp(code.data(),replacement.data(),14)==0 &&
            RtlLookupFunctionEntry(trampoline+prefix,&lookup,nullptr)==&metadata->functions[0] && lookup==base &&
            RtlLookupFunctionEntry(trampoline+prefix+14,&lookup,nullptr)==&metadata->functions[1] && lookup==base;
    }
    bool stop_locked(std::uint64_t timeout_ms) noexcept {
        stopping_=true;stop_complete_=false;revoke();const auto began=GetTickCount64();
        for(const unsigned index:{5u,4u,3u,1u,0u,2u}) {
            if(created(index) && !original(index))return false;
            if(created(index)) {const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[index]=false;}
            while(in_flight()) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        }
        for(unsigned index=0;index<6;++index)if(created(index) && !restore_protection(index))return false;
        if(base_ && !verify(base_,read_))return false;stop_complete_=true;return true;
    }
    bool rollback() noexcept {stop_locked(0);return false;}
    std::uintptr_t base_{};Read read_{};std::mutex lifecycle_,state_;
    std::array<Receipt,2> receipts_{};std::array<std::atomic<void*>,6> originals_{};
    std::array<std::atomic<bool>,6> created_{},installed_{};std::array<Metadata*,6> metadata_{};
    std::array<bool,6> registered_{},patched_{},protection_pending_{},cache_pending_{};std::array<DWORD,6> protection_{};
    std::atomic<bool> stopping_{true},stop_complete_{true},unwind_ready_{};
    std::atomic<unsigned> in_flight_{};std::atomic<std::uint64_t> sequence_{},substitutions_{};std::uint64_t instance_{},revision_{};
    inline static thread_local Request* request_{};
    inline static thread_local Evaluation* evaluation_{};
    inline static thread_local ParentFrame* parent_{};
    inline static thread_local unsigned callback_depth_{};
};
}
