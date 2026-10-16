#define NOMINMAX
#include "../src/native_door_animation.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_ERROR_NOT_CREATED;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_ERROR_DISABLED;}

namespace d2vr {
struct NativeDoorAnimationFixture {
    NativeDoorAnimation owner;
    std::array<unsigned char,0x2000> arena{};
    std::array<unsigned char,0x400> animation{};
    std::array<unsigned char,0xd0> schema{};
    std::array<unsigned char,0x5c0> node{};
    std::array<unsigned char,0x20> record{};
    std::array<unsigned char,0x20> input_binding{};
    std::array<unsigned char,0x40> holder{};
    std::array<unsigned char,0x90> clips{};
    std::array<unsigned char,0x80> descriptors{};
    std::uintptr_t bucket{};
    NativeDoorAnimation::Operation operation{};
    unsigned calls{},updates{},retirements{};
    std::uint32_t last_dt{};
    unsigned corrupt_offset{UINT32_MAX};
    bool pending{},fault_update{},cpp_fault{},fault_insert{},nested{},reenter_update{},reenter_insert{},republish_aba{};
    unsigned reads{};
    inline static NativeDoorAnimationFixture* current{};
    static std::uintptr_t address(const void* value) {return reinterpret_cast<std::uintptr_t>(value);}
    template<class Value> static void put(std::uintptr_t at,const Value& value) {std::memcpy(reinterpret_cast<void*>(at),&value,sizeof(value));}
    template<class Value> static Value get(std::uintptr_t at) {Value value{};std::memcpy(&value,reinterpret_cast<void*>(at),sizeof(value));return value;}
    static bool read(std::uintptr_t at,void* value,std::size_t size) {++current->reads;std::memcpy(value,reinterpret_cast<void*>(at),size);return true;}
    std::uintptr_t context() const {return address(arena.data())+0x200;}
    std::uintptr_t front() const {return context()+0x9c0;}
    NativeDoorAnimationFixture() {
        current=this;owner.base_=0x140000000;owner.read_=&read;owner.stopping_=false;
        for(auto& installed:owner.installed_)installed=true;
        owner.originals_[0]=reinterpret_cast<void*>(&input);
        owner.originals_[1]=reinterpret_cast<void*>(&evaluate);
        owner.originals_[2]=reinterpret_cast<void*>(&retire);
        owner.originals_[3]=reinterpret_cast<void*>(&parent);
        owner.originals_[4]=reinterpret_cast<void*>(&insert);
        owner.originals_[5]=reinterpret_cast<void*>(&update);
        operation={1,2,3,4,0x1111,0x2222,address(animation.data()),address(animation.data())+0x338,address(input_binding.data())+8,.4f,0,1,false,false};
        put(operation.container,address(holder.data()));put(operation.container+8,address(schema.data()));put(address(holder.data()),address(arena.data()));
        put(address(schema.data())+0xc0,std::uint32_t{static_cast<std::uint32_t>(arena.size())});
        put(address(input_binding.data()),std::uintptr_t{0x1234});put(operation.binding+4,std::int32_t{0});
        bucket=address(record.data());put(address(schema.data())+0x58,address(&bucket));put(address(schema.data())+0x60,1u);put(address(schema.data())+0x64,1u);
        put(address(record.data())+8,address(node.data()));put(address(record.data())+0x10,std::int32_t{0x200});
        put(address(node.data()),owner.base_+0x1c4bce8);put(address(node.data())+0x590,static_cast<unsigned char>(1));put(address(node.data())+0x5b0,std::uintptr_t{0x1234});
        put(context()+0x10,operation.container+0x38);put(context()+0x18,operation.container+0x20);put(context()+0x9a0,address(arena.data()));
        put(context()+0x9b0,front());put(context()+0x9b8,0x05000000u);put(context()+0x9bc,0x440u);
        arena[1]=0xa1;arena[2]=0xb2;arena[3]=0xc3;
        for(unsigned index=0;index<2;++index) {put(address(clips.data())+index*0x48+0x30,address(descriptors.data())+index*0x40);put(address(descriptors.data())+index*0x40+0x38,1.f);}
        assert(owner.publish(operation));
    }
    static void __fastcall input(std::uintptr_t container,std::uintptr_t binding,std::uintptr_t value) {
        ++current->calls;const auto destination=get<std::uintptr_t>(get<std::uintptr_t>(container))+get<std::int32_t>(binding+4);
        const auto version=get<unsigned char>(destination+0x16);put(destination,get<unsigned char>(value));std::memcpy(reinterpret_cast<void*>(destination+4),reinterpret_cast<void*>(value+4),0x11);
        put(destination+0x15,static_cast<unsigned char>(1));put(destination+0x16,static_cast<unsigned char>(version+1));current->pending=true;
    }
    static std::uintptr_t __fastcall evaluate(std::uintptr_t,std::uintptr_t) {
        auto& fixture=*current;
        if(fixture.pending)std::memcpy(reinterpret_cast<void*>(fixture.context()+0x988),fixture.arena.data(),24);
        if(fixture.corrupt_offset!=UINT32_MAX) {auto* byte=reinterpret_cast<unsigned char*>(fixture.context()+0x988+fixture.corrupt_offset);*byte^=1;}
        fixture.owner.invoke_parent(fixture.context(),fixture.pending?1:0,fixture.owner.base_+0x6e3f2c);fixture.pending=false;return fixture.context()+0x5f0;
    }
    static void __fastcall retire(std::uintptr_t,std::uintptr_t) {++current->retirements;}
    static unsigned __fastcall insert(std::uintptr_t list,std::uintptr_t entry,unsigned index) {
        if(current->fault_insert)RaiseException(0xe1370547,EXCEPTION_NONCONTINUABLE,0,nullptr);
        if(current->reenter_insert) {current->reenter_insert=false;current->owner.revoke();assert(current->owner.publish(current->operation));}
        assert(index==0);const auto count=get<unsigned>(list+8)&0xffffff;const auto data=get<std::uintptr_t>(list);
        assert(count<=3);std::memmove(reinterpret_cast<void*>(data+0x28),reinterpret_cast<void*>(data),count*0x28);std::memcpy(reinterpret_cast<void*>(data),reinterpret_cast<void*>(entry),0x28);
        put(list+8,0x05000000u|count+1);return 0;
    }
    static unsigned char __fastcall update(std::uintptr_t entry,std::uint32_t raw_dt) {
        auto& fixture=*current;++fixture.updates;fixture.last_dt=raw_dt;
        if(fixture.reenter_update) {fixture.reenter_update=false;fixture.owner.revoke();assert(fixture.owner.publish(fixture.operation));fixture.play(NativeDoorAnimation::RequestKind::Open,false);}
        if(fixture.republish_aba) {fixture.republish_aba=false;for(const float desired:{.2f,0.f,.2f,0.f}) {fixture.operation.desired_seconds=desired;assert(fixture.owner.publish(fixture.operation));}}
        if(fixture.cpp_fault)throw std::runtime_error("owned update");
        if(fixture.fault_update)RaiseException(0xe1370547,EXCEPTION_NONCONTINUABLE,0,nullptr);
        if(get<int>(entry+0x1c)==1) {float dt{};std::memcpy(&dt,&raw_dt,4);put(entry+0x14,get<float>(entry+0x14)+dt*get<float>(entry+0x10));}
        return 1;
    }
    static void __fastcall parent(std::uintptr_t context,unsigned char changed) {
        auto& fixture=*current;
        if(fixture.nested) {fixture.nested=false;const auto pending=fixture.pending;fixture.pending=false;fixture.owner.invoke_evaluate(address(fixture.schema.data()),address(fixture.holder.data()));fixture.pending=pending;}
        if(changed) {
            std::array<unsigned char,0x28> entry{};const auto clip=fixture.arena[0];
            put(address(entry.data()),address(fixture.clips.data())+clip*0x48);put(address(entry.data())+0x10,1.f);
            put(address(entry.data())+0x1c,get<int>(context+0x988+8));
            fixture.owner.invoke_insert(context+0x9b0,address(entry.data()),0,fixture.owner.base_+0x6e504b);
        }
        const auto count=get<unsigned>(context+0x9b8)&0xffffff;
        for(unsigned index=0;index<count;++index)fixture.owner.invoke_update(context+0x9c0+index*0x28,0x3d23d70a,fixture.owner.base_+0x6e4d10);
    }
    void play(NativeDoorAnimation::RequestKind kind,bool owned=true) {
        std::array<unsigned char,24> input_bytes{};input_bytes[0]=kind==NativeDoorAnimation::RequestKind::Open?0:1;
        input_bytes[1]=0x17;input_bytes[2]=0x28;input_bytes[3]=0x39;
        put(address(input_bytes.data())+4,1.f);put(address(input_bytes.data())+8,kind==NativeDoorAnimation::RequestKind::Open?1:4);put(address(input_bytes.data())+12,1.f);
        const auto invoke=[&] {owner.invoke_input(operation.container,operation.binding,address(input_bytes.data()));};
        if(owned)owner.request_call(operation,kind,invoke);else invoke();
    }
    void run() {assert(owner.invoke_evaluate(address(schema.data()),address(holder.data()))==context()+0x5f0);assert(!NativeDoorAnimation::evaluation_ && !NativeDoorAnimation::parent_ && !NativeDoorAnimation::callback_depth_ && !owner.in_flight());}
    static bool seh(NativeDoorAnimationFixture& fixture) {
        __try {fixture.run();}__except(GetExceptionCode()==0xe1370547?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}return false;
    }
    static void unwind_check() {
        auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(trampoline);
        NativeDoorAnimation owner;owner.base_=0x140000000;
        for(unsigned index=0;index<6;++index) {
            auto* code=trampoline+index*64;const auto prefix=NativeDoorAnimation::prefixes[index];owner.originals_[index]=code;
            std::memcpy(code,NativeDoorAnimation::prologues[index].data(),prefix);
            const auto tail=NativeDoorAnimation::tail(owner.base_+NativeDoorAnimation::rvas[index]+prefix,false);
            const auto relay=NativeDoorAnimation::tail(reinterpret_cast<std::uintptr_t>(NativeDoorAnimation::hooks()[index]),false);
            std::memcpy(code+prefix,tail.data(),14);std::memcpy(code+prefix+14,relay.data(),14);assert(owner.prepare_trampoline(index));
            for(unsigned offset=0;offset<prefix+28;++offset) {
                std::array<std::uint64_t,64> stack{};const auto entry_rsp=address(stack.data())+128;put(entry_rsp,std::uint64_t{0x123456789abcdef0});
                CONTEXT context{};context.Rip=address(code)+offset;context.Rsp=entry_rsp;context.Rbx=0x1111222233334444;
                const bool body=offset>=prefix && offset<prefix+14;
                if(index==0 && offset>=4 && offset<prefix+14)context.Rsp-=0x28;
                const bool saved=index!=0 && index!=3 && offset>=5 && offset<prefix+14;
                if(saved) {put(entry_rsp+(index==5?0x20:8),context.Rbx);context.Rbx=0;}
                DWORD64 base{};const auto* function=RtlLookupFunctionEntry(context.Rip,&base,nullptr);assert(function);
                void* handler{};DWORD64 establisher{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,const_cast<PRUNTIME_FUNCTION>(function),&context,&handler,&establisher,nullptr);
                if(offset==0 || offset==prefix || offset==prefix+10 || offset==prefix+14 || offset==prefix+27 || body) {
                    assert(context.Rip==0x123456789abcdef0 && context.Rsp==entry_rsp+8 && context.Rbx==0x1111222233334444);
                }
            }
        }
    }
    static void check() {
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();assert(fixture.arena[1]==0xa1 && fixture.owner.receipts_[0].input[1]==0x17);assert(get<float>(fixture.front()+0x14)==.4f && get<float>(fixture.front()+0x10)==1.f && fixture.last_dt==0x3d23d70a);
            for(const auto desired:{.2f,.1f,0.f}) {fixture.operation.desired_seconds=desired;assert(fixture.owner.publish(fixture.operation));fixture.run();assert(get<float>(fixture.front()+0x14)==desired && get<float>(fixture.front()+0x10)==1.f);}
            fixture.operation.close_authorized=true;assert(fixture.owner.publish(fixture.operation) && !fixture.owner.close_ready(fixture.operation));fixture.run();assert(fixture.owner.close_ready(fixture.operation));const auto instance=fixture.owner.receipts_[0].opening_instance;
            fixture.play(NativeDoorAnimation::RequestKind::ClosedEndpoint);fixture.run();assert(fixture.owner.receipts_[0].instances[1]==instance && get<float>(fixture.front()+0x28+0x14)==0.f);
            fixture.owner.revoke();fixture.run();assert(get<float>(fixture.front()+0x28+0x14)>.03f);
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();const auto version=fixture.arena[0x16];const auto sequence=fixture.owner.inputs();
            for(unsigned index=0;index<256;++index)fixture.play(NativeDoorAnimation::RequestKind::Open,false);
            assert(fixture.arena[0x16]==version && fixture.owner.inputs()==sequence+256 && !fixture.owner.receipts_[0].owned);
        }
        {
            NativeDoorAnimationFixture fixture;fixture.operation.desired_seconds=0;fixture.operation.close_authorized=true;assert(fixture.owner.publish(fixture.operation));fixture.play(NativeDoorAnimation::RequestKind::Open);
            fixture.play(NativeDoorAnimation::RequestKind::ClosedEndpoint);assert(!fixture.owner.receipts_[0].owned);
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();fixture.fault_update=true;assert(seh(fixture));fixture.fault_update=false;
            assert(get<float>(fixture.front()+0x10)==1.f && fixture.owner.in_flight()==0 && !NativeDoorAnimation::callback_depth_ && !NativeDoorAnimation::parent_ && !NativeDoorAnimation::evaluation_);
            assert(fixture.owner.publish(fixture.operation));fixture.cpp_fault=true;bool caught=false;try {fixture.run();}catch(const std::runtime_error&) {caught=true;}assert(caught && get<float>(fixture.front()+0x10)==1.f && !fixture.owner.in_flight());
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();fixture.owner.invoke_retire(address(fixture.schema.data()),address(fixture.holder.data()));assert(fixture.retirements==1 && !fixture.owner.receipts_[0].published);fixture.run();
            assert(get<float>(fixture.front()+0x14)>.4f);
        }
        {
            NativeDoorAnimationFixture fixture;put(fixture.context()+0x18,std::uintptr_t{17});fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();assert(get<float>(fixture.front()+0x14)>.03f && fixture.owner.substitutions()==0);
            fixture.operation.linked=true;assert(!fixture.owner.publish(fixture.operation));
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.fault_insert=true;assert(seh(fixture));fixture.fault_insert=false;
            assert(!fixture.owner.in_flight() && !NativeDoorAnimation::parent_ && !NativeDoorAnimation::evaluation_ && !NativeDoorAnimation::callback_depth_);assert(fixture.owner.publish(fixture.operation));
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();fixture.nested=true;fixture.run();assert(get<float>(fixture.front()+0x10)==1.f && !fixture.owner.in_flight());
        }
        {
            NativeDoorAnimationFixture fixture;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();const auto before=fixture.updates;fixture.reenter_update=true;fixture.run();
            assert(fixture.updates==before+1 && !fixture.owner.receipts_[0].owned && get<float>(fixture.front()+0x10)==1.f && fixture.owner.publish(fixture.operation));
        }
        {
            NativeDoorAnimationFixture fixture;fixture.reenter_insert=true;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();assert(!fixture.owner.receipts_[0].owned && fixture.updates==1 && fixture.owner.publish(fixture.operation));
        }
        for(const auto offset:{0u,4u,8u,12u,16u,20u}) {
            NativeDoorAnimationFixture fixture;fixture.corrupt_offset=offset;fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();assert(fixture.owner.substitutions()==0 && fixture.updates==1);
        }
        {
            NativeDoorAnimationFixture fixture;const auto published=fixture.owner.status(fixture.operation);assert(published.published && !published.input_owned && !published.evaluated && published.desired_revision);
            fixture.play(NativeDoorAnimation::RequestKind::Open);const auto input_status=fixture.owner.status(fixture.operation);assert(input_status.input_owned && input_status.input_generation && !input_status.evaluated);
            put(address(fixture.descriptors.data())+0x38,1.75f);fixture.run();const auto applied=fixture.owner.status(fixture.operation);
            assert(applied.evaluated && applied.evaluated_revision==applied.desired_revision && applied.applied_seconds==.4f && applied.duration_seconds==1.75f && !applied.zero_ack);
            const auto before_status_reads=fixture.reads;assert(fixture.owner.status(fixture.operation).evaluated && fixture.reads==before_status_reads);
            fixture.operation.desired_seconds=0.f;fixture.operation.close_authorized=true;assert(fixture.owner.publish(fixture.operation));
            const auto pending_zero=fixture.owner.status(fixture.operation);assert(pending_zero.input_owned && pending_zero.input_generation==applied.input_generation && pending_zero.desired_revision!=applied.desired_revision && !pending_zero.evaluated && !pending_zero.zero_ack && !fixture.owner.close_ready(fixture.operation));
            fixture.run();const auto zero=fixture.owner.status(fixture.operation);assert(zero.evaluated && zero.applied_seconds==0.f && zero.zero_ack && fixture.owner.close_ready(fixture.operation));
            fixture.operation.desired_seconds=.1f;assert(fixture.owner.publish(fixture.operation));const auto nonzero=fixture.owner.status(fixture.operation);
            assert(nonzero.input_owned && !nonzero.evaluated && !nonzero.zero_ack && !fixture.owner.close_ready(fixture.operation));fixture.run();assert(fixture.owner.status(fixture.operation).applied_seconds==.1f);
            fixture.operation.desired_seconds=0.f;assert(fixture.owner.publish(fixture.operation));fixture.republish_aba=true;fixture.run();
            const auto aba=fixture.owner.status(fixture.operation);assert(aba.input_owned && !aba.evaluated && !aba.zero_ack && !fixture.owner.close_ready(fixture.operation));
            fixture.run();assert(fixture.owner.status(fixture.operation).zero_ack);const auto before_close=fixture.owner.status(fixture.operation);fixture.play(NativeDoorAnimation::RequestKind::ClosedEndpoint);
            const auto closing=fixture.owner.status(fixture.operation);assert(closing.input_owned && closing.closing && closing.input_generation!=before_close.input_generation && !closing.evaluated && !closing.zero_ack);
            fixture.run();assert(fixture.owner.status(fixture.operation).closing && fixture.owner.status(fixture.operation).evaluated);
            auto wrong=fixture.operation;wrong.ownership++;assert(!fixture.owner.status(wrong).published);wrong=fixture.operation;wrong.part_generation++;assert(!fixture.owner.status(wrong).published);
            fixture.owner.revoke();assert(!fixture.owner.status(fixture.operation).published);assert(fixture.owner.publish(fixture.operation));const auto recycled=fixture.owner.status(fixture.operation);
            assert(recycled.published && recycled.desired_revision!=closing.desired_revision && !recycled.input_owned && !recycled.evaluated);
        }
        {
            NativeDoorAnimationFixture fixture;fixture.operation.desired_seconds=0.f;assert(fixture.owner.publish(fixture.operation));
            fixture.play(NativeDoorAnimation::RequestKind::Open);fixture.run();const auto initial=fixture.owner.status(fixture.operation);
            assert(initial.evaluated && initial.zero_ack && !initial.closing && !fixture.operation.close_authorized);
            const auto unauthorized_revision=initial.desired_revision;
            fixture.operation.close_authorized=true;assert(fixture.owner.publish(fixture.operation));
            const auto authorized_pending=fixture.owner.status(fixture.operation);
            assert(authorized_pending.published && authorized_pending.input_owned && authorized_pending.input_generation==initial.input_generation &&
                authorized_pending.desired_revision==unauthorized_revision+1 && !authorized_pending.evaluated && !authorized_pending.zero_ack &&
                !fixture.owner.close_ready(fixture.operation) && !fixture.owner.status(NativeDoorAnimationFixture::current->operation).zero_ack);
            auto unauthorized=fixture.operation;unauthorized.close_authorized=false;assert(!fixture.owner.status(unauthorized).published);
            fixture.run();const auto authorized=fixture.owner.status(fixture.operation);
            assert(authorized.evaluated && authorized.zero_ack && authorized.input_generation==initial.input_generation && fixture.owner.close_ready(fixture.operation));
            const auto authorized_revision=authorized.desired_revision;
            fixture.operation.close_authorized=false;assert(fixture.owner.publish(fixture.operation));
            const auto revoked_pending=fixture.owner.status(fixture.operation);
            assert(revoked_pending.published && revoked_pending.input_owned && revoked_pending.input_generation==initial.input_generation &&
                revoked_pending.desired_revision==authorized_revision+1 && !revoked_pending.evaluated && !revoked_pending.zero_ack &&
                !fixture.owner.close_ready(fixture.operation));
            auto stale_authorized=fixture.operation;stale_authorized.close_authorized=true;assert(!fixture.owner.status(stale_authorized).published);
            fixture.run();const auto revoked=fixture.owner.status(fixture.operation);
            assert(revoked.evaluated && revoked.zero_ack && revoked.input_generation==initial.input_generation && !fixture.owner.close_ready(fixture.operation));
        }
        unwind_check();
    }
};
}
int main() {d2vr::NativeDoorAnimationFixture::check();std::cout<<"PASS native door animation semantic receipts, reversal, close lineage, raw-dt, revocation and SEH/C++ cleanup; not real native hook proof\n";}
