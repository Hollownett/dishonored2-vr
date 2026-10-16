#include "../src/native_door_event_admission.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_ERROR_NOT_CREATED;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_ERROR_DISABLED;}

namespace d2vr {
struct NativeDoorAnimationFixture {
    NativeDoorAnimation animation;
    NativeDoorAnimationFixture() {animation.stopping_=false;for(auto& installed:animation.installed_)installed=true;}
    static bool scoped(NativeDoorAnimation& animation,std::uint64_t ownership) {
        return NativeDoorAnimation::request_ && NativeDoorAnimation::request_->owner==&animation && NativeDoorAnimation::request_->operation.ownership==ownership;
    }
    bool published(std::uint64_t ownership) const {for(const auto& receipt:animation.receipts_)if(receipt.published && receipt.operation.ownership==ownership)return true;return false;}
};
struct NativeDoorEventAdmissionFixture {
    NativeDoorAnimationFixture animation;
    NativeDoorEventAdmission owner;
    std::vector<unsigned char> game=std::vector<unsigned char>(0x314000);
    std::array<unsigned char,0x400> door{},component{},part{},player{},anim{};
    std::array<unsigned char,24> envelope{};
    std::array<unsigned char,12> event{};
    NativeDoorEventAdmission::Request request{};
    unsigned flushes{},posts{},listeners{},uses{},plays{},guards{},resets{},retargets{};
    std::uint32_t raw_start{};
    bool scoped{},nested{},script_nested{},unknown_lookup{},fault_use{},fault_play{},cpp_use{},qualify_false{},qualify_seh{},qualify_cpp{},phase_invalidate{},wrong_emitter{},revoke_in_post{},revoke_before_use{},skip_delivery{},repost_before_exit{};
    unsigned reads{};
    bool repost_in_author{};
    bool refuse_close{},bad_close_params{},revoke_other_in_use{};
    unsigned cancel_before_use{UINT32_MAX};
    inline static NativeDoorEventAdmissionFixture* current{};
    static std::uintptr_t address(const void* value) {return reinterpret_cast<std::uintptr_t>(value);}
    template<class Value> static void put(std::uintptr_t destination,const Value& value) {std::memcpy(reinterpret_cast<void*>(destination),&value,sizeof(value));}
    static bool read(std::uintptr_t source,void* output,std::size_t size) {
        ++current->reads;
        if(source==current->owner.base_+0x39a5290 && size==8) {const auto pointer=address(current->game.data());std::memcpy(output,&pointer,8);return true;}
        std::memcpy(output,reinterpret_cast<void*>(source),size);return true;
    }
    static bool qualify(const NativeDoorEventAdmission::Request&,NativeDoorEventAdmission::Phase phase) {
        auto& fixture=*current;
        if(fixture.repost_in_author && phase==NativeDoorEventAdmission::Phase::Author) {fixture.repost_in_author=false;fixture.owner.revoke();assert(fixture.owner.submit(fixture.request));return false;}
        if(fixture.qualify_seh)RaiseException(0xe0421234,0,0,nullptr);
        if(fixture.qualify_cpp)throw std::runtime_error("qualify");
        return !fixture.qualify_false;
    }
    NativeDoorEventAdmissionFixture() {
        current=this;
        owner.base_=address(VirtualAlloc(nullptr,0x4000000,MEM_RESERVE,PAGE_NOACCESS));assert(owner.base_);
        assert(VirtualAlloc(reinterpret_cast<void*>((owner.base_+0x1c56988)&~std::uintptr_t{0xfff}),0x1000,MEM_COMMIT,PAGE_READWRITE));
        assert(VirtualAlloc(reinterpret_cast<void*>((owner.base_+NativeDoorEventAdmission::opened_state_getter_rva)&~std::uintptr_t{0xfff}),0x1000,MEM_COMMIT,PAGE_READWRITE));
        const auto page=(owner.base_+0x8112c0)&~std::uintptr_t{0xfff};assert(VirtualAlloc(reinterpret_cast<void*>(page),0x1000,MEM_COMMIT,PAGE_READWRITE));
        const auto code=NativeDoorEventAdmission::tail(address(reinterpret_cast<void*>(&post)),false);std::memcpy(reinterpret_cast<void*>(owner.base_+0x8112c0),code.data(),code.size());
        DWORD previous{};assert(VirtualProtect(reinterpret_cast<void*>(page),0x1000,PAGE_EXECUTE_READ,&previous));assert(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(page),0x1000));
        owner.read_=&read;owner.qualify_=&qualify;owner.animation_=&animation.animation;owner.stopping_=false;
        for(auto& installed:owner.installed_)installed=true;
        owner.originals_[0]=reinterpret_cast<void*>(&dispatch);owner.originals_[1]=reinterpret_cast<void*>(&flush);owner.originals_[2]=reinterpret_cast<void*>(&reset);
        owner.originals_[3]=reinterpret_cast<void*>(&use);owner.originals_[4]=reinterpret_cast<void*>(&retarget);owner.originals_[5]=reinterpret_cast<void*>(&lookup);
        owner.originals_[6]=reinterpret_cast<void*>(&activate);owner.originals_[7]=reinterpret_cast<void*>(&play);owner.originals_[8]=reinterpret_cast<void*>(&binary);
        owner.originals_[9]=reinterpret_cast<void*>(&script);owner.originals_[10]=reinterpret_cast<void*>(&serialize);
        for(unsigned index=11;index<16;++index)owner.originals_[index]=reinterpret_cast<void*>(&unary);
        request.operation={1,2,3,4,address(player.data()),address(part.data()),address(anim.data()),address(anim.data())+0x338,0x1000,.2f,0,1,true,false};
        request.game=address(game.data());request.manager=request.game+0x1e8968;request.component=address(component.data());request.door=address(door.data());
        request.component_generation=5;request.door_spawn=0x10001;request.player_spawn=0x20002;request.part_spawn=0x30003;
        put(request.door,owner.base_+0x1cb7c68);put(request.door+0xa8,request.door_spawn);put(request.door+0x370,request.component);put(request.door+0x378,0x3fff0010u);
        put(request.game+0x3130b0+8,request.door);put(request.game+0x3130b0+16,request.operation.player);put(request.game+0x3130b0+24,request.operation.part);
        put(request.component,owner.base_+0x1c569c8);put(request.component+0x10,request.door);put(request.component+0x20,static_cast<unsigned char>(1));
        put(request.component+0xdc,request.part_spawn);put(request.component+0xf8,request.operation.animation);
        put(request.component+0x68,request.component+0x40);put(request.component+0x88,request.component+0x40);
        put(request.operation.player+0xa8,request.player_spawn);put(request.operation.part+0xa8,request.part_spawn);
        put(request.operation.animation,owner.base_+0x1b7f408);put(request.operation.animation+0x10,request.operation.part);
    }
    ~NativeDoorEventAdmissionFixture() {assert(!owner.in_flight());VirtualFree(reinterpret_cast<void*>(owner.base_),0,MEM_RELEASE);}
    static std::uintptr_t __fastcall post(std::uintptr_t queue,std::uint16_t message,int size,std::uint32_t target) {
        auto& fixture=*current;assert(queue==fixture.request.manager+0xc8e8 && message==0x96 && size==4 && target==fixture.request.door_spawn);
        ++fixture.posts;put(address(fixture.event.data()),message);put(address(fixture.event.data())+2,static_cast<std::uint16_t>(size));put(address(fixture.event.data())+4,target);
        if(fixture.revoke_in_post)fixture.owner.revoke(fixture.request.operation.ownership);
        return address(fixture.event.data())+8;
    }
    static void __fastcall dispatch(std::uintptr_t manager,std::uintptr_t queue) {
        auto& fixture=*current;fixture.owner.invoke_flush(manager,fixture.owner.base_+0x81001d);
        if(fixture.repost_before_exit) {fixture.owner.revoke(fixture.request.operation.ownership);assert(fixture.owner.submit(fixture.request));return;}
        if(fixture.skip_delivery)return;
        if(fixture.posts && queue==fixture.request.manager+0xc8e8) {
            ++fixture.listeners;put(address(fixture.envelope.data()),0x96u);put(address(fixture.envelope.data())+8,address(fixture.event.data())+8);put(address(fixture.envelope.data())+16,fixture.request.door_spawn);
            if(fixture.revoke_before_use)fixture.owner.revoke(fixture.request.operation.ownership);
            if(fixture.cancel_before_use==8)fixture.owner.invoke_binary_guard(8,fixture.request.component,0x123);
            if(fixture.cancel_before_use==9)fixture.owner.invoke_script(fixture.request.component,true,false);
            if(fixture.cancel_before_use==10)fixture.owner.invoke_serialize(fixture.request.component,0x123,0x12345678);
            if(fixture.cancel_before_use>=11 && fixture.cancel_before_use<16)fixture.owner.invoke_unary_guard(fixture.cancel_before_use,fixture.request.component);
            fixture.owner.invoke_use(fixture.request.component,address(fixture.envelope.data()),fixture.owner.base_+0x74bde1);
            fixture.owner.invoke_reset(queue);
        }
        fixture.owner.invoke_flush(manager,fixture.owner.base_+0x8101d5);
    }
    static void __fastcall flush(std::uintptr_t) {++current->flushes;}
    static void __fastcall reset(std::uintptr_t) {++current->resets;}
    static void __fastcall use(std::uintptr_t component,std::uintptr_t) {
        auto& fixture=*current;++fixture.uses;
        if(fixture.fault_use)RaiseException(0xe0421234,0,0,nullptr);
        if(fixture.cpp_use)throw std::runtime_error("use");
        fixture.owner.invoke_lookup(component+0x40,0,fixture.owner.base_+0x74b761);
        if(fixture.script_nested)fixture.owner.invoke_script(component,false,false);
        if(fixture.unknown_lookup)fixture.owner.invoke_lookup(component+0x40,0,fixture.owner.base_+0x74dc4c);
        if(fixture.nested) {fixture.nested=false;fixture.owner.invoke_dispatch(fixture.request.manager,fixture.request.manager+0xc918,fixture.owner.base_+0x8af58f);}
        unsigned state{};std::memcpy(&state,reinterpret_cast<void*>(component+0x48),4);
        if(fixture.revoke_other_in_use)fixture.owner.revoke(999);
        if(state==2) {
            if(fixture.refuse_close)return;
            unsigned char sense{};std::memcpy(&sense,reinterpret_cast<void*>(component+0xc8),1);
            put(component+0x90,std::array<unsigned char,4>{static_cast<unsigned char>(fixture.bad_close_params?1:0),0,sense,fixture.request.operation.closing_clip});
        } else put(component+0x90,std::array<unsigned char,4>{1,0,1,0});
        put(component+0x4c,1u);put(component+0x114,fixture.request.player_spawn);
    }
    static std::uintptr_t __fastcall lookup(std::uintptr_t machine,int state) {return machine+(state==0?0x20:0x40);}
    static void __fastcall activate(std::uintptr_t,std::uintptr_t) {
        auto& fixture=*current;
        if(fixture.phase_invalidate)fixture.owner.invoke_script(fixture.request.component,false,false);
        const auto caller=fixture.owner.base_+(fixture.wrong_emitter?0x74c981:0x74c7eb);
        assert(fixture.owner.invoke_play(fixture.request.operation.animation,0,0,.375f,1,.25f,0x7fc01234u,true,caller));
    }
    static bool __fastcall play(std::uintptr_t animation,int channel,unsigned char clip,float weight,int mode,float rate,std::uint32_t raw_start,bool extra) {
        auto& fixture=*current;++fixture.plays;assert(animation==fixture.request.operation.animation && channel==0 && clip==0 && weight==.375f && mode==1 && rate==.25f && extra);
        fixture.raw_start=raw_start;fixture.scoped=NativeDoorAnimationFixture::scoped(fixture.animation.animation,fixture.request.operation.ownership);
        if(fixture.fault_play)RaiseException(0xe0421234,0,0,nullptr);
        return true;
    }
    static void __fastcall retarget(std::uintptr_t,int,bool) {++current->retargets;}
    static void __fastcall binary(std::uintptr_t,std::uintptr_t) {++current->guards;}
    static void __fastcall unary(std::uintptr_t) {++current->guards;}
    static void __fastcall script(std::uintptr_t,bool,bool) {++current->guards;}
    static void __fastcall serialize(std::uintptr_t,std::uintptr_t,std::uint32_t version) {assert(version==0x12345678);++current->guards;}
    void run_open() {assert(owner.submit(request));owner.invoke_dispatch(request.manager,request.manager+0xc8e8,owner.base_+0x8af58f);}
    void prepare_opened(bool sense) {
        const auto opened_component=request.component;
        put(opened_component+0x48,2u);put(opened_component+0x4c,2u);put(opened_component+0xb8,owner.base_+0x1c56988);put(opened_component+0xc0,opened_component+0x40);
        put(opened_component+0xc8,static_cast<unsigned char>(sense));put(opened_component+0xd0,opened_component);put(opened_component+0xd8,2u);
        put(owner.base_+0x1c56988+0x28,owner.base_+NativeDoorEventAdmission::opened_state_getter_rva);
        put(owner.base_+NativeDoorEventAdmission::opened_state_getter_rva,NativeDoorEventAdmission::opened_state_getter_code);
        put(opened_component+0x1e2,static_cast<unsigned char>(1));put(opened_component+0x1e3,static_cast<unsigned char>(0));request.operation.closing_clip=sense?1:0;
    }
    void run_use_close() {assert(owner.submit(request,NativeDoorEventAdmission::Action::UseClose));owner.invoke_dispatch(request.manager,request.manager+0xc8e8,owner.base_+0x8af58f);}
    void run_activation() {
        owner.invoke_lookup(request.component+0x40,0,owner.base_+0x6df760);owner.invoke_lookup(request.component+0x40,1,owner.base_+0x6df76e);
        put(request.component+0x48,1u);owner.invoke_activate(request.component+0x80,request.component+0x60);
    }
    void clean() {assert(!owner.in_flight() && !NativeDoorEventAdmission::callback_depth_ && !NativeDoorEventAdmission::dispatch_ && !NativeDoorEventAdmission::producer_);}
    static bool fault_open(NativeDoorEventAdmissionFixture& fixture) {__try {fixture.run_open();return false;}__except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0421234;}}
    static bool fault_activation(NativeDoorEventAdmissionFixture& fixture) {__try {fixture.run_activation();return false;}__except(EXCEPTION_EXECUTE_HANDLER) {return GetExceptionCode()==0xe0421234;}}
    static void tests() {
        for(const bool sense:{false,true}) for(const unsigned observed_field:{0u,1u,2u}) {
            NativeDoorEventAdmissionFixture fixture;fixture.prepare_opened(sense);assert(fixture.owner.submit(fixture.request,NativeDoorEventAdmission::Action::UseClose));
            put(fixture.request.component+0xd8,observed_field);
            const auto submitted=fixture.owner.status(fixture.request);assert(submitted.stage==NativeDoorEventAdmission::Stage::Submitted);
            assert(fixture.owner.submit(fixture.request,NativeDoorEventAdmission::Action::UseClose) && !fixture.owner.submit(fixture.request) && !fixture.owner.submit(fixture.request,NativeDoorEventAdmission::Action::Close));
            fixture.run_use_close();assert(fixture.posts==1 && fixture.listeners==1 && fixture.uses==1 && fixture.owner.use_close_accepted()==1);
            assert(fixture.owner.status(fixture.request).stage==NativeDoorEventAdmission::Stage::Unknown && !fixture.owner.admitted() && !fixture.owner.emitted() && !fixture.animation.published(fixture.request.operation.ownership));
            fixture.run_activation();assert(fixture.plays==1 && !fixture.scoped && !fixture.owner.emitted());fixture.clean();
        }
        for(unsigned variant=0;variant<5;++variant) {
            NativeDoorEventAdmissionFixture fixture;fixture.prepare_opened(true);
            fixture.refuse_close=variant==0;fixture.bad_close_params=variant==1;fixture.revoke_before_use=variant==2;fixture.script_nested=variant==3;fixture.revoke_other_in_use=variant==4;
            fixture.run_use_close();assert(fixture.uses==1 && !fixture.owner.use_close_accepted() && !fixture.animation.published(fixture.request.operation.ownership));
            assert(fixture.owner.status(fixture.request).stage==NativeDoorEventAdmission::Stage::Unknown);fixture.clean();
        }
        for(unsigned variant=0;variant<9;++variant) {
            NativeDoorEventAdmissionFixture fixture;fixture.prepare_opened(true);
            if(variant==0)put(fixture.request.component+0x48,1u);if(variant==1)put(fixture.request.component+0x4c,1u);
            if(variant==2)fixture.request.operation.closing_clip=0;if(variant==3)put(fixture.request.component+0xb8,fixture.owner.base_+0x1c56948);
            if(variant==4)put(fixture.request.component+0xc0,fixture.request.component+0x48);
            if(variant==5)put(fixture.request.component+0xd0,fixture.request.component+8);
            if(variant==6)put(fixture.request.component+0xc8,static_cast<unsigned char>(2));
            if(variant==7)put(fixture.owner.base_+0x1c56988+0x28,fixture.owner.base_+NativeDoorEventAdmission::opened_state_getter_rva+1);
            if(variant==8)put(fixture.owner.base_+NativeDoorEventAdmission::opened_state_getter_rva,std::array<unsigned char,6>{0xb8,1,0,0,0,0xc3});
            fixture.run_use_close();assert(!fixture.posts && !fixture.uses && !fixture.owner.use_close_accepted());fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();assert(fixture.posts==1 && fixture.listeners==1 && fixture.uses==1 && fixture.resets==1 && fixture.flushes==2);
            assert(fixture.owner.admitted()==1 && fixture.owner.snapshot(fixture.request.component).pending);fixture.run_activation();
            assert(fixture.scoped && fixture.plays==1 && fixture.raw_start==0x7fc01234u && fixture.owner.emitted()==1);fixture.clean();
            fixture.owner.invoke_serialize(fixture.request.component,0x123,0x12345678);assert(!fixture.owner.snapshot(fixture.request.component).live && fixture.guards==1);fixture.clean();
        }
        for(unsigned variant=0;variant<6;++variant) {
            NativeDoorEventAdmissionFixture fixture;
            if(variant==0)fixture.script_nested=true;if(variant==1)fixture.unknown_lookup=true;if(variant==2)fixture.nested=true;
            fixture.run_open();
            if(variant<3)assert(!fixture.owner.snapshot(fixture.request.component).live);
            else {if(variant==3)fixture.phase_invalidate=true;if(variant==4)fixture.wrong_emitter=true;if(variant==5)fixture.request.component_generation++;
                if(variant==5)fixture.owner.revoke();fixture.run_activation();assert(!fixture.scoped && fixture.owner.emitted()==0);}
            fixture.clean();
        }
        for(unsigned variant=0;variant<3;++variant) {
            NativeDoorEventAdmissionFixture fixture;fixture.qualify_false=variant==0;fixture.qualify_cpp=variant==1;fixture.qualify_seh=variant==2;
            fixture.run_open();assert(!fixture.posts && !fixture.owner.admitted());fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.fault_use=true;assert(fault_open(fixture));fixture.clean();assert(!fixture.owner.snapshot(fixture.request.component).live);
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.cpp_use=true;bool caught=false;try {fixture.run_open();}catch(const std::runtime_error&) {caught=true;}
            assert(caught);fixture.clean();assert(!fixture.owner.snapshot(fixture.request.component).live);
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();fixture.fault_play=true;assert(fault_activation(fixture));fixture.clean();assert(!fixture.owner.snapshot(fixture.request.component).live);
            assert(!NativeDoorAnimationFixture::scoped(fixture.animation.animation,fixture.request.operation.ownership));
        }
        {
            NativeDoorEventAdmissionFixture fixture;assert(fixture.owner.submit(fixture.request));fixture.owner.invoke_dispatch(fixture.request.manager,fixture.request.manager+0xc8e8,fixture.owner.base_+0x7ec622);
            assert(!fixture.posts);fixture.clean();fixture.owner.stopping_=true;assert(!fixture.owner.submit(fixture.request));
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();fixture.run_activation();assert(fixture.animation.published(fixture.request.operation.ownership));
            const auto receipt=fixture.owner.snapshot(fixture.request.component);NativeDoorEventAdmission::Producer frame{};frame.owner=&fixture.owner;frame.component=fixture.request.component;
            frame.epoch=receipt.epoch;frame.request=fixture.request;frame.kind=NativeDoorEventAdmission::Action::Update;
            fixture.owner.cancel_component(fixture.request.component);fixture.owner.update_request(fixture.request,frame);
            assert(!fixture.animation.published(fixture.request.operation.ownership));fixture.clean();
        }
        for(unsigned index=8;index<16;++index) {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();assert(fixture.owner.snapshot(fixture.request.component).pending);
            if(index==8)fixture.owner.invoke_binary_guard(index,fixture.request.component,0x123);
            else if(index==9)fixture.owner.invoke_script(fixture.request.component,true,false);
            else if(index==10)fixture.owner.invoke_serialize(fixture.request.component,0x123,0x12345678);
            else fixture.owner.invoke_unary_guard(index,fixture.request.component);
            assert(!fixture.owner.snapshot(fixture.request.component).live && fixture.guards==1);fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();fixture.run_activation();const auto receipt=fixture.owner.snapshot(fixture.request.component);
            NativeDoorEventAdmission::Producer frame{};frame.owner=&fixture.owner;frame.component=fixture.request.component;frame.request=fixture.request;frame.epoch=receipt.epoch;
            frame.kind=NativeDoorEventAdmission::Action::Close;NativeDoorEventAdmission::producer_=&frame;
            fixture.owner.invoke_retarget(fixture.request.component,3,true);assert(fixture.owner.snapshot(fixture.request.component).live);
            fixture.owner.invoke_retarget(fixture.request.component,3,true);assert(!fixture.owner.snapshot(fixture.request.component).live && fixture.retargets==2);
            NativeDoorEventAdmission::producer_=nullptr;fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.owner.sequence_=UINT64_MAX;assert(!fixture.owner.submit(fixture.request));
            assert(!fixture.uses && !fixture.owner.admitted() && !fixture.owner.snapshot(fixture.request.component).live);fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();fixture.qualify_false=true;fixture.run_activation();
            assert(!fixture.scoped && !fixture.owner.emitted() && !fixture.owner.snapshot(fixture.request.component).live);fixture.clean();
        }
        for(unsigned variant=0;variant<10;++variant) {
            NativeDoorEventAdmissionFixture fixture;
            fixture.revoke_in_post=variant==0;fixture.revoke_before_use=variant==1;if(variant>=2)fixture.cancel_before_use=variant+6;
            fixture.run_open();assert(fixture.posts==1 && fixture.listeners==1 && fixture.uses==1 && !fixture.owner.admitted() && !fixture.owner.snapshot(fixture.request.component).live);
            fixture.run_activation();assert(fixture.plays==1 && !fixture.scoped && !fixture.owner.emitted());fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;const auto token=fixture.owner.revocation_epoch_.load();fixture.owner.revoke(fixture.request.operation.ownership);
            NativeDoorEventAdmission::Producer frame{};frame.owner=&fixture.owner;frame.component=fixture.request.component;
            assert(!fixture.owner.begin(frame,fixture.request,token,1));fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;fixture.run_open();fixture.run_activation();assert(fixture.animation.published(fixture.request.operation.ownership));
            fixture.owner.revocation_epoch_=UINT64_MAX;fixture.owner.revoke(999);
            assert(!fixture.owner.revocation_epoch_ && !fixture.owner.enabled() && !fixture.owner.submit(fixture.request) && !fixture.animation.published(fixture.request.operation.ownership));
            fixture.owner.revoke();assert(!fixture.owner.revocation_epoch_);fixture.clean();
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;
            assert(fixture.owner.status(fixture.request).stage==Stage::Unknown);assert(fixture.owner.submit(fixture.request));
            const auto submitted=fixture.owner.status(fixture.request);assert(submitted.stage==Stage::Submitted && submitted.epoch);
            assert(fixture.owner.submit(fixture.request) && fixture.owner.status(fixture.request).epoch==submitted.epoch);
            fixture.run_open();const auto admitted=fixture.owner.status(fixture.request);assert(admitted.stage==Stage::Admitted && admitted.epoch==submitted.epoch && fixture.posts==1);
            fixture.run_activation();const auto emitted=fixture.owner.status(fixture.request);assert(emitted.stage==Stage::Emitted && emitted.epoch==submitted.epoch);
            assert(!fixture.animation.animation.status(fixture.request.operation).input_owned);
            const auto before_status_reads=fixture.reads;assert(fixture.owner.status(fixture.request).stage==Stage::Emitted && fixture.reads==before_status_reads);
            auto wrong=fixture.request;wrong.component_generation++;assert(fixture.owner.status(wrong).stage==Stage::Unknown);
            wrong=fixture.request;wrong.operation.ownership++;assert(fixture.owner.status(wrong).stage==Stage::Unknown);
            fixture.owner.revoke();assert(fixture.owner.status(fixture.request).stage==Stage::Unknown);
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;
            for(unsigned index=0;index<8;++index) {assert(fixture.owner.submit(fixture.request));fixture.owner.revoke(999);assert(fixture.owner.status(fixture.request).stage==Stage::Unknown);}
            assert(fixture.owner.submit(fixture.request));const auto first=fixture.owner.status(fixture.request);fixture.owner.revoke();assert(fixture.owner.submit(fixture.request));
            const auto recycled=fixture.owner.status(fixture.request);assert(recycled.stage==Stage::Submitted && recycled.epoch!=first.epoch);
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;fixture.skip_delivery=true;
            for(unsigned index=0;index<4;++index) {fixture.run_open();assert(fixture.owner.status(fixture.request).stage==Stage::Unknown);}
            fixture.skip_delivery=false;fixture.run_open();assert(fixture.owner.status(fixture.request).stage==Stage::Admitted);
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;fixture.repost_before_exit=true;
            fixture.run_open();const auto fresh=fixture.owner.status(fixture.request);assert(fresh.stage==Stage::Submitted);
            fixture.repost_before_exit=false;fixture.run_open();assert(fixture.owner.status(fixture.request).stage==Stage::Admitted && fixture.owner.status(fixture.request).epoch==fresh.epoch);
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;assert(fixture.owner.submit(fixture.request));fixture.qualify_false=true;
            fixture.owner.invoke_dispatch(fixture.request.manager,fixture.request.manager+0xc8e8,fixture.owner.base_+0x8af58f);
            assert(fixture.owner.status(fixture.request).stage==Stage::Unknown && !fixture.posts);
        }
        {
            NativeDoorEventAdmissionFixture fixture;using Stage=NativeDoorEventAdmission::Stage;assert(fixture.owner.submit(fixture.request));
            const auto old=fixture.owner.status(fixture.request);fixture.repost_in_author=true;
            fixture.owner.invoke_dispatch(fixture.request.manager,fixture.request.manager+0xc8e8,fixture.owner.base_+0x8af58f);
            const auto fresh=fixture.owner.status(fixture.request);assert(fresh.stage==Stage::Submitted && fresh.epoch!=old.epoch && !fixture.posts);
            fixture.owner.cancel_receipt(fixture.request.component,old.epoch);assert(fixture.owner.status(fixture.request).epoch==fresh.epoch);
            fixture.run_open();assert(fixture.owner.status(fixture.request).stage==Stage::Admitted && fixture.owner.status(fixture.request).epoch==fresh.epoch);fixture.clean();
        }
    }
};
}
int main() {d2vr::NativeDoorEventAdmissionFixture::tests();std::cout<<"native_door_event_admission_check: PASS (synthetic callback semantics, not native runtime)\n";}
