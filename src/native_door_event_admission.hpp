#pragma once
#include "native_door_animation.hpp"

namespace d2vr {
class NativeDoorEventAdmission {
public:
    using Read=NativeDoorAnimation::Read;
    using Unary=void (__fastcall*)(std::uintptr_t);
    using Binary=void (__fastcall*)(std::uintptr_t,std::uintptr_t);
    using Retarget=void (__fastcall*)(std::uintptr_t,int,bool);
    using Lookup=std::uintptr_t (__fastcall*)(std::uintptr_t,int);
    using Script=void (__fastcall*)(std::uintptr_t,bool,bool);
    using Serialize=void (__fastcall*)(std::uintptr_t,std::uintptr_t,std::uint32_t);
    using Play=bool (__fastcall*)(std::uintptr_t,int,unsigned char,float,int,float,std::uint32_t,bool);
    using Post=std::uintptr_t (__fastcall*)(std::uintptr_t,std::uint16_t,int,std::uint32_t);
    enum class Action {Open,Update,Close,UseClose};
    enum class Phase {Author,Use,Activation,Control};
    struct Request {
        NativeDoorAnimation::Operation operation{};
        std::uintptr_t game{},manager{},component{},door{};
        std::uint64_t component_generation{};
        std::uint32_t door_spawn{},player_spawn{},part_spawn{};
    };
    using Qualify=bool (*)(const Request&,Phase);
    enum class Stage {Unknown,Submitted,Admitted,Emitted};
    struct Status {Stage stage{Stage::Unknown};std::uint64_t epoch{};};
    inline static constexpr std::uintptr_t opened_state_getter_rva=0x10bd300;
    inline static constexpr std::array<unsigned char,6> opened_state_getter_code{0xb8,0x02,0x00,0x00,0x00,0xc3};
    static bool opened_state(std::uintptr_t base,Read read,std::uintptr_t component,unsigned char& sense) noexcept {
        std::uint32_t current{},next{};std::uintptr_t table{},machine{},owner{},getter{};std::array<unsigned char,6> code{};
        return base && read && component && safe_read(read,component+0x48,&current,4) && current==2 &&
            safe_read(read,component+0x4c,&next,4) && next==2 && safe_read(read,component+0xb8,&table,8) && table==base+0x1c56988 &&
            safe_read(read,component+0xc0,&machine,8) && machine==component+0x40 &&
            safe_read(read,component+0xd0,&owner,8) && owner==component &&
            safe_read(read,table+0x28,&getter,8) && getter==base+opened_state_getter_rva &&
            safe_read(read,getter,code.data(),code.size()) && code==opened_state_getter_code &&
            safe_read(read,component+0xc8,&sense,1) && sense<=1;
    }
    inline static constexpr std::array<std::uintptr_t,6> caller_rvas{0x8af583,0x8101f0,0x74bdd6,0x6df740,0x74af00,0x8112c0};
    inline static constexpr std::array<unsigned,6> caller_sizes{12,12,11,106,131,94};
    inline static constexpr std::array<std::uint64_t,6> caller_hashes{0x894d758fbe8eceb1ull,0x8df1059b0c6eeb28ull,0x5b5f1d96e5165acaull,0x1612d86092cd4eaull,0x492dc3d4dfe23e65ull,0x5fd62a1ca579833full};
    inline static constexpr std::array<std::uintptr_t,16> rvas{0x810000,0x810430,0x811320,0x74b6e0,0x74d7a0,0x6df6d0,0x74c5c0,0x53eef0,0x74ba10,0x74dbb0,0x74e080,0x74c1f0,0x74c560,0x74cd90,0x74d910,0x74a800};
inline static constexpr std::array<unsigned,16> prefixes{11,6,6,5,5,5,5,8,5,5,6,6,6,6,5,5};
inline static constexpr std::array<unsigned,16> profile_sizes{489,278,25,804,367,101,1092,162,471,402,380,734,93,2571,669,188};
inline static constexpr std::array<std::uint64_t,16> profile_hashes{0x10c43863d84872b9ull,0xc134621a70d6547dull,0xe008b89df8694e5ull,0x21f65997fd2dfbf7ull,0x333dd8e999eeadf4ull,0x5e6f122a3df110c2ull,0xae40fe40d660cfd6ull,0xe074b24d83db4c61ull,0x7b7ebc2f64722791ull,0x9af0b80ec9778783ull,0x1d51e3c464ab3ec3ull,0xe784f56538d7900eull,0x5143bed2cc687e33ull,0xa0719f85e11e0d86ull,0x154a50193b7c7dddull,0x13f891989dc5e160ull};
inline static constexpr std::array<std::array<unsigned char,32>,16> prologues{{{0x41,0x54,0x41,0x57,0x48,0x81,0xec,0x88,0x0,0x0,0x0,0x4c,0x8b,0xe2,0x4c,0x8b,0xf9,0xc6,0x81,0x48,0xc9,0x0,0x0,0x1,0xe8,0x13,0x4,0x0,0x0,0x41,0x83,0x7c},{0x40,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x70,0x4c,0x8b,0xf9,0x48,0x8d,0x99},{0x40,0x53,0x48,0x83,0xec,0x20,0x33,0xd2,0x48,0x8b,0xd9,0xe8,0xa0,0x93,0x8e,0xff,0xc7,0x43,0x28,0x0,0x0,0x0,0x0,0x48,0x83,0xc4,0x20,0x5b,0xc3,0xcc,0xcc,0xcc},{0x48,0x89,0x54,0x24,0x10,0x56,0x48,0x83,0xec,0x50,0x48,0x8b,0x42,0x8,0x48,0x8b,0xf1,0x48,0x89,0x5c,0x24,0x48,0x44,0x8b,0x0,0x48,0x81,0xc1,0xe8,0x0,0x0,0x0},{0x48,0x89,0x5c,0x24,0x8,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x45,0xf,0xb6,0xf0,0x44,0x8b,0xfa,0x48,0x8b,0xf9},{0x48,0x89,0x5c,0x24,0x8,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x8b,0xea,0x83,0xfa,0xff,0x74,0x2e,0x8b,0x41,0x18,0x48,0x8b},{0x48,0x8b,0xc4,0x55,0x56,0x57,0x48,0x8d,0xa8,0x28,0xff,0xff,0xff,0x48,0x81,0xec,0xc0,0x1,0x0,0x0,0x48,0xc7,0x45,0xa0,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x18},{0x48,0x83,0xec,0x68,0x41,0x80,0xf8,0xff,0xf,0x8e,0x8d,0x0,0x0,0x0,0xf,0x57,0xc0,0xf,0x2f,0xd8,0xf,0x86,0x81,0x0,0x0,0x0,0x48,0x63,0xc2,0x48,0x8b,0x94},{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x8,0x48,0x8b,0xfa,0x48,0x8b,0xf1,0xf,0xb6,0x98,0xf0,0x1},{0x44,0x88,0x44,0x24,0x18,0x53,0x57,0x48,0x83,0xec,0x28,0x83,0x79,0x48,0x2,0x41,0xf,0xb6,0xc0,0xf,0xb6,0xfa,0x48,0x8b,0xd9,0xf,0x84,0x6c,0x1,0x0,0x0,0x48},{0x40,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x48,0x48,0x89,0x74,0x24,0x50,0x41,0x8b,0xf8,0x48,0x8b,0xda,0x48},{0x48,0x8b,0xc4,0x57,0x41,0x56,0x48,0x81,0xec,0x88,0x0,0x0,0x0,0x4c,0x8b,0x71,0x8,0x48,0x8b,0xf9,0x49,0x83,0x7e,0x78,0x0,0xf,0x84,0xb3,0x2,0x0,0x0,0x48},{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x8b,0x89,0xdc,0x0,0x0,0x0,0xe8,0xec,0xff,0xf8,0xff,0x48,0x85,0xc0,0x74,0x29,0x8b,0x8b,0xdc,0x0,0x0,0x0,0xe8},{0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec,0xb0,0x0,0x0,0x0,0x48,0xc7,0x45,0xe7,0xfe,0xff,0xff,0xff},{0x48,0x89,0x5c,0x24,0x18,0x4c,0x89,0x64,0x24,0x20,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x50,0x48,0x89,0x74,0x24,0x70,0x48,0x8b,0xd9,0x48,0x89},{0x48,0x89,0x4c,0x24,0x8,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x5c,0x24,0x48,0x48,0x8b,0xd9,0x48,0x8d,0x5,0xa6,0xc1}}};
inline static constexpr std::array<std::array<unsigned char,16>,16> unwinds{{{0x1,0xb,0x4,0x0,0xb,0x1,0x11,0x0,0x4,0xf0,0x2,0xc0},{0x1,0x6,0x4,0x0,0x6,0xe0,0x4,0x70,0x3,0x60,0x2,0x50},{0x1,0x6,0x2,0x0,0x6,0x32,0x2,0x30},{0x1,0x5,0x0,0x0},{0x1,0x5,0x2,0x0,0x5,0x34,0x1,0x0},{0x1,0x5,0x2,0x0,0x5,0x34,0x1,0x0},{0x1,0x5,0x2,0x0,0x5,0x60,0x4,0x50},{0x1,0x8,0x1,0x0,0x4,0xc2,0x0,0x0},{0x1,0x5,0x2,0x0,0x5,0x34,0x3,0x0},{0x1,0x5,0x0,0x0},{0x1,0x6,0x2,0x0,0x6,0x52,0x2,0x70},{0x1,0x6,0x2,0x0,0x6,0xe0,0x4,0x70},{0x1,0x6,0x2,0x0,0x6,0x72,0x2,0x30},{0x1,0x6,0x4,0x0,0x6,0xc0,0x4,0x70,0x3,0x60,0x2,0x50},{0x1,0x5,0x2,0x0,0x5,0x34,0x3,0x0},{0x1,0x5,0x0,0x0}}};
    static NativeDoorEventAdmission& process_owner() {static auto* owner=new NativeDoorEventAdmission;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        if(!base || !read)return false;
        std::array<unsigned char,2571> bytes{};
        for(unsigned index=0;index<16;++index) {
            if(!safe_read(read,base+rvas[index],bytes.data(),32) || std::memcmp(bytes.data(),prologues[index].data(),32) ||
               !safe_read(read,base+rvas[index],bytes.data(),profile_sizes[index]))return false;
            std::uint64_t hash=14695981039346656037ull;
            for(unsigned offset=0;offset<profile_sizes[index];++offset)hash=(hash^bytes[offset])*1099511628211ull;
            if(hash!=profile_hashes[index])return false;
        }
        for(unsigned index=0;index<6;++index) {
            if(!safe_read(read,base+caller_rvas[index],bytes.data(),caller_sizes[index]))return false;
            std::uint64_t hash=14695981039346656037ull;
            for(unsigned offset=0;offset<caller_sizes[index];++offset)hash=(hash^bytes[offset])*1099511628211ull;
            if(hash!=caller_hashes[index])return false;
        }
        return true;
    }
    bool install(std::uintptr_t base,Read read,Qualify qualify,NativeDoorAnimation& animation) noexcept {
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);
        if(!guard.owns_lock() || !ready_for_install() || !revocation_epoch_.load() || !qualify || !animation.enabled() ||
           (base_ && (base_!=base || read_!=read || qualify_!=qualify || animation_!=&animation)) || !verify(base,read))return false;
        base_=base;read_=read;qualify_=qualify;animation_=&animation;stopping_=true;stop_complete_=false;unwind_ready_=false;
        const auto callbacks=hooks();
        for(unsigned index=0;index<16;++index)if(!created_[index]) {
            void* trampoline{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),callbacks[index],&trampoline)!=MH_OK)return rollback();
            originals_[index]=trampoline;created_[index]=true;if(!trampoline)return rollback();
        }
        for(unsigned index=0;index<16;++index)if(!prepare_trampoline(index))return rollback();
        if(!verify(base,read))return rollback();unwind_ready_=true;
        for(const unsigned index:enable_order) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();installed_[index]=true;
        }
        stopping_=false;return true;
    }
    bool enabled() const noexcept {return installed() && !stopping_ && revocation_epoch_.load();}
    bool installed() const noexcept {for(unsigned index=0;index<16;++index)if(!installed(index))return false;return true;}
    bool installed(unsigned index) const noexcept {return index<16 && installed_[index];}
    bool created(unsigned index) const noexcept {return index<16 && created_[index];}
    void* original(unsigned index) const noexcept {return index<16?originals_[index].load():nullptr;}
    bool unwind_registered() const noexcept {return unwind_ready_;}
    unsigned in_flight() const noexcept {return in_flight_;}
    std::uint64_t authored() const noexcept {return authored_;}
    std::uint64_t admitted() const noexcept {return admitted_;}
    std::uint64_t emitted() const noexcept {return emitted_;}
    std::uint64_t use_close_accepted() const noexcept {return use_close_accepted_;}
    std::uint64_t cancelled() const noexcept {return cancelled_;}
    bool stopped() const noexcept {if(!stop_complete_ || in_flight())return false;for(unsigned index=0;index<16;++index)if(installed(index))return false;return true;}
    bool ready_for_install() const noexcept {if(!stopped())return false;for(unsigned index=0;index<16;++index)if(created(index) && !original(index))return false;return true;}
    bool submit(const Request& request,Action action=Action::Open) noexcept {
        if(!enabled() || !valid(request))return false;
        std::lock_guard<std::mutex> guard(state_);if(!enabled())return false;
        retire_stale_submissions();
        for(auto& receipt:receipts_)if(receipt.live && same(receipt.request,request)) {
            if(action==Action::Open || action==Action::UseClose)return receipt.kind==action;
            if(!receipt.active)return false;
            discard_mailbox();mailbox_={request,action,true,revocation_epoch_.load(),receipt.epoch};return true;
        }
        if((action!=Action::Open && action!=Action::UseClose) || sequence_==UINT64_MAX)return false;
        for(const auto& receipt:receipts_)if(receipt.live && receipt.request.component==request.component)return false;
        Receipt* available{};
        for(auto& receipt:receipts_)if(!receipt.live) {available=&receipt;break;}
        if(!available && mailbox_.pending && (mailbox_.action==Action::Open || mailbox_.action==Action::UseClose))for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==mailbox_.epoch && !receipt.begun && !receipt.admitted)available=&receipt;
        if(!available)return false;discard_mailbox();
        *available={};available->live=true;available->request=request;available->kind=action;available->epoch=++sequence_;available->revocation=revocation_epoch_.load();
        mailbox_={request,action,true,available->revocation,available->epoch};return true;
    }
    Status status(const Request& request) noexcept {
        std::lock_guard<std::mutex> guard(state_);if(!enabled())return {};
        retire_stale_submissions();
        for(const auto& receipt:receipts_)if(receipt.live && same(receipt.request,request)) {
            return {receipt.active?Stage::Emitted:receipt.admitted?Stage::Admitted:Stage::Submitted,receipt.epoch};
        }
        return {};
    }
    void revoke(std::uint64_t ownership=0) noexcept {
        advance_revocation();
        if(!revocation_epoch_.load())ownership=0;
        {
            std::lock_guard<std::mutex> guard(state_);
            if(!ownership || mailbox_.request.operation.ownership==ownership)mailbox_={};
            for(auto& receipt:receipts_)if(receipt.live && (!ownership || receipt.request.operation.ownership==ownership)) {receipt={};if(sequence_!=UINT64_MAX)++sequence_;++cancelled_;}
        }
        if(animation_)animation_->revoke(ownership);
    }
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        stopping_=true;revoke();if(callback_depth_)return false;
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);return guard.owns_lock() && stop_locked(timeout_ms);
    }
    void invoke_dispatch(std::uintptr_t manager,std::uintptr_t queue,std::uintptr_t caller) {
        Dispatch frame{};frame.previous=dispatch_;frame.owner=this;frame.manager=manager;frame.queue=queue;
        frame.eligible=enabled() && !dispatch_ && caller==base_+0x8af58f && queue==manager+0xc8e8;
        if(dispatch_)dispatch_->cancelled=true;
        dispatch_=&frame;++in_flight_;++callback_depth_;
        __try {const auto invoke=reinterpret_cast<Binary>(original(0));if(invoke)invoke(manager,queue);}
        __finally {frame.cancelled=true;retire_dispatch(frame);dispatch_=frame.previous;--callback_depth_;--in_flight_;}
    }
    void invoke_flush(std::uintptr_t manager,std::uintptr_t caller) {
        ++in_flight_;++callback_depth_;
        __try {const auto invoke=reinterpret_cast<Unary>(original(1));if(invoke) {invoke(manager);author(manager,caller);}}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_reset(std::uintptr_t queue) {
        ++in_flight_;++callback_depth_;
        __try {
            for(auto* frame=dispatch_;frame;frame=frame->previous)if(frame->owner==this && frame->queue==queue)frame->cancelled=true;
            const auto invoke=reinterpret_cast<Unary>(original(2));if(invoke)invoke(queue);
        }__finally {--callback_depth_;--in_flight_;}
    }
    void invoke_use(std::uintptr_t component,std::uintptr_t envelope,std::uintptr_t caller) {
        Producer frame{};frame.previous=producer_;frame.owner=this;frame.component=component;frame.kind=Action::Open;
        ++in_flight_;++callback_depth_;producer_=&frame;
        __try {use_body(frame,envelope,caller);}
        __finally {if(!frame.completed && frame.epoch)cancel_receipt(component,frame.epoch);producer_=frame.previous;--callback_depth_;--in_flight_;}
    }
    void invoke_retarget(std::uintptr_t component,int endpoint,bool sense) {
        ++in_flight_;++callback_depth_;
        __try {
            const bool owned=producer_ && producer_->owner==this && producer_->component==component && producer_->kind==Action::Close && producer_->epoch && !producer_->retarget_entered && current(*producer_);
            if(owned)producer_->retarget_entered=true;
            if(!owned)cancel_component(component);
            const auto invoke=reinterpret_cast<Retarget>(original(4));if(invoke)invoke(component,endpoint,sense);
        }__finally {--callback_depth_;--in_flight_;}
    }
    std::uintptr_t invoke_lookup(std::uintptr_t machine,int state,std::uintptr_t caller) {
        ++in_flight_;++callback_depth_;
        __try {lookup_barrier(machine,state,caller);const auto invoke=reinterpret_cast<Lookup>(original(5));return invoke?invoke(machine,state):0;}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_activate(std::uintptr_t animated,std::uintptr_t previous) {
        Producer frame{};frame.previous=producer_;frame.owner=this;frame.component=animated>=0x80?animated-0x80:0;frame.kind=Action::Open;
        ++in_flight_;++callback_depth_;producer_=&frame;
        __try {activation_body(frame,previous);}
        __finally {if(frame.epoch && !frame.completed)cancel_receipt(frame.component,frame.epoch);producer_=frame.previous;--callback_depth_;--in_flight_;}
    }
    bool invoke_play(std::uintptr_t animation,int channel,unsigned char clip,float weight,int mode,float rate,std::uint32_t raw_start,bool extra,std::uintptr_t caller) {
        ++in_flight_;++callback_depth_;
        __try {return play_body(animation,channel,clip,weight,mode,rate,raw_start,extra,caller);}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_binary_guard(unsigned index,std::uintptr_t component,std::uintptr_t argument) {
        ++in_flight_;++callback_depth_;
        __try {cancel_component(component);const auto invoke=reinterpret_cast<Binary>(original(index));if(invoke)invoke(component,argument);}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_script(std::uintptr_t component,bool sense,bool unlock) {
        ++in_flight_;++callback_depth_;
        __try {cancel_component(component);const auto invoke=reinterpret_cast<Script>(original(9));if(invoke)invoke(component,sense,unlock);}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_serialize(std::uintptr_t component,std::uintptr_t stream,std::uint32_t version) {
        ++in_flight_;++callback_depth_;
        __try {cancel_component(component);const auto invoke=reinterpret_cast<Serialize>(original(10));if(invoke)invoke(component,stream,version);}
        __finally {--callback_depth_;--in_flight_;}
    }
    void invoke_unary_guard(unsigned index,std::uintptr_t component) {
        ++in_flight_;++callback_depth_;
        __try {cancel_component(component);const auto invoke=reinterpret_cast<Unary>(original(index));if(invoke)invoke(component);}
        __finally {--callback_depth_;--in_flight_;}
    }
private:
    friend struct NativeDoorEventAdmissionFixture;
    NativeDoorEventAdmission()=default;
    struct Mailbox {Request request{};Action action{};bool pending{};std::uint64_t revocation{},epoch{};};
    struct Receipt {Request request{};std::uint64_t epoch{},revocation{};std::array<unsigned char,4> params{};Action kind{};bool live{},pending{},active{},begun{},admitted{};};
    struct Dispatch {Dispatch* previous{};NativeDoorEventAdmission* owner{};std::uintptr_t manager{},queue{},payload{};Request request{};std::uint64_t revocation{},epoch{};Action action{};unsigned char sense{};bool eligible{},cancelled{},posted{},delivered{};};
    struct Producer {Producer* previous{};NativeDoorEventAdmission* owner{};Request request{};std::uintptr_t component{};std::uint64_t epoch{};Action kind{};unsigned char sense{};bool activation{},completed{},emission{},retarget_entered{};};
    struct Metadata {std::array<RUNTIME_FUNCTION,2> functions{};std::array<unsigned char,16> unwind{};std::array<unsigned char,4> relay{1,0,0,0};};
    inline static constexpr std::array<unsigned,16> enable_order{15,14,13,12,11,10,9,8,5,4,3,6,7,2,0,1};
    inline static constexpr std::array<unsigned,16> disable_order{1,0,7,6,4,3,5,8,9,10,11,12,13,14,15,2};
    static bool valid(const Request& request) noexcept {
        const auto& operation=request.operation;
        return operation.ownership && operation.world && operation.player_generation && operation.part_generation && operation.player && operation.part &&
            operation.animation && operation.container==operation.animation+0x338 && operation.binding && operation.opening_clip!=0xff && operation.closing_clip!=0xff &&
            !operation.linked && std::isfinite(operation.desired_seconds) && operation.desired_seconds>=0.f && request.game && request.manager==request.game+0x1e8968 &&
            request.component && request.component_generation && request.door && request.door_spawn && request.player_spawn && request.part_spawn &&
            (request.door_spawn>>16)<0x3fff && (request.player_spawn>>16)<0x3fff && (request.part_spawn>>16)<0x3fff;
    }
    static bool same(const Request& first,const Request& second) noexcept {
        return first.component==second.component && first.component_generation==second.component_generation && first.game==second.game && first.manager==second.manager &&
            first.door==second.door && first.door_spawn==second.door_spawn && first.player_spawn==second.player_spawn && first.part_spawn==second.part_spawn &&
            first.operation.ownership==second.operation.ownership && first.operation.world==second.operation.world && first.operation.player==second.operation.player &&
            first.operation.player_generation==second.operation.player_generation && first.operation.part==second.operation.part && first.operation.part_generation==second.operation.part_generation &&
            first.operation.animation==second.operation.animation && first.operation.container==second.operation.container && first.operation.binding==second.operation.binding &&
            first.operation.opening_clip==second.operation.opening_clip && first.operation.closing_clip==second.operation.closing_clip;
    }
    static bool cpp_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return read && read(address,output,size);}catch(...) {return false;}}
    static bool safe_read(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {__try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    static bool local_read(std::uintptr_t address,void* output,std::size_t size) noexcept {__try {std::memcpy(output,reinterpret_cast<void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    template<class Value> bool get(std::uintptr_t address,Value& value) const noexcept {return safe_read(read_,address,&value,sizeof(value));}
    template<class Value> bool equals(std::uintptr_t address,Value expected) const noexcept {Value actual{};return get(address,actual) && actual==expected;}
    static bool cpp_qualify(Qualify qualify,const Request& request,Phase phase) noexcept {try {return qualify && qualify(request,phase);}catch(...) {return false;}}
    bool qualified(const Request& request,Phase phase) const noexcept {
        __try {return native_identity(request) && cpp_qualify(qualify_,request,phase);}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    bool native_identity(const Request& request) const noexcept {
        if(!valid(request) || !enabled() || !animation_ || !animation_->enabled())return false;
        const auto& operation=request.operation;
        std::uintptr_t linked{};unsigned char usable{},blocked{};std::array<unsigned char,6> flags{};
        return equals(base_+0x39a5290,request.game) && equals(request.door,base_+0x1cb7c68) && equals(request.door+0xa8,request.door_spawn) &&
            equals(request.door+0x370,request.component) && equals(request.game+0x3130b0+(request.door_spawn>>16)*8ull,request.door) &&
            equals(request.component,base_+0x1c569c8) && equals(request.component+0x10,request.door) && equals(request.component+0xdc,request.part_spawn) &&
            equals(request.game+0x3130b0+(request.part_spawn>>16)*8ull,operation.part) && equals(operation.part+0xa8,request.part_spawn) &&
            equals(request.game+0x3130b0+(request.player_spawn>>16)*8ull,operation.player) && equals(operation.player+0xa8,request.player_spawn) &&
            equals(request.component+0xf8,operation.animation) && equals(operation.animation,base_+0x1b7f408) && equals(operation.animation+0x10,operation.part) &&
            equals(request.door+0x378,std::uint32_t{0x3fff0010}) && get(request.component+0xf0,linked) && !linked &&
            get(request.component+0x20,usable) && usable && get(request.component+0x118,blocked) && !blocked &&
            get(request.component+0x1e8,flags) && !flags[0] && !flags[1] && !flags[2] && !flags[5];
    }
    Receipt snapshot(std::uintptr_t component) {
        std::lock_guard<std::mutex> guard(state_);for(const auto& receipt:receipts_)if(receipt.live && receipt.request.component==component)return receipt;return {};
    }
    void retire_stale_submissions() noexcept {
        const auto current_revocation=revocation_epoch_.load();
        for(auto& receipt:receipts_)if(receipt.live && !receipt.admitted && receipt.revocation!=current_revocation)receipt={};
        if(mailbox_.pending && mailbox_.revocation!=current_revocation)mailbox_={};
    }
    void discard_mailbox() noexcept {
        if(mailbox_.pending && (mailbox_.action==Action::Open || mailbox_.action==Action::UseClose))for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==mailbox_.epoch && !receipt.begun && !receipt.admitted)receipt={};
        mailbox_={};
    }
    void retire_dispatch(const Dispatch& frame) noexcept {
        if(!frame.epoch)return;
        std::lock_guard<std::mutex> guard(state_);
        for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==frame.epoch && !receipt.admitted)receipt={};
    }
    bool current(const Producer& frame) {
        std::lock_guard<std::mutex> guard(state_);for(const auto& receipt:receipts_)if(receipt.live && receipt.epoch==frame.epoch && same(receipt.request,frame.request))return enabled();return false;
    }
    void advance_revocation() noexcept {
        auto epoch=revocation_epoch_.load();
        while(epoch && !revocation_epoch_.compare_exchange_weak(epoch,epoch==UINT64_MAX?0:epoch+1)) {}
    }
    void cancel_component(std::uintptr_t component,bool invalidate_authored=true) noexcept {
        if(invalidate_authored)advance_revocation();
        std::array<std::uint64_t,2> ownership{};unsigned count{};bool exhausted{};
        {
            std::lock_guard<std::mutex> guard(state_);
            exhausted=!revocation_epoch_.load();
            for(auto& receipt:receipts_)if(receipt.live && (exhausted || receipt.request.component==component)) {ownership[count++]=receipt.request.operation.ownership;receipt={};if(sequence_!=UINT64_MAX)++sequence_;++cancelled_;}
            if(mailbox_.pending && (exhausted || mailbox_.request.component==component))mailbox_={};
        }
        if(animation_) {if(exhausted)animation_->revoke();else for(unsigned index=0;index<count;++index)animation_->revoke(ownership[index]);}
    }
    void cancel_receipt(std::uintptr_t component,std::uint64_t epoch) noexcept {
        std::uint64_t ownership{};
        {
            std::lock_guard<std::mutex> guard(state_);
            for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==epoch && receipt.request.component==component) {ownership=receipt.request.operation.ownership;receipt={};++cancelled_;}
            if(mailbox_.pending && mailbox_.epoch==epoch && mailbox_.request.component==component)mailbox_={};
        }
        if(ownership && animation_)animation_->revoke(ownership);
    }
    bool begin(Producer& frame,const Request& request,std::uint64_t revocation,std::uint64_t epoch) {
        std::lock_guard<std::mutex> guard(state_);if(!enabled() || !revocation || revocation!=revocation_epoch_.load())return false;
        for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==epoch && receipt.revocation==revocation && !receipt.begun && !receipt.admitted && same(receipt.request,request)) {
            receipt.begun=true;frame.request=receipt.request;frame.epoch=receipt.epoch;frame.kind=receipt.kind;return true;
        }
        return false;
    }
    void author(std::uintptr_t manager,std::uintptr_t caller) {
        auto* frame=dispatch_;
        if(!enabled() || !frame || frame->owner!=this || !frame->eligible || frame->cancelled || frame->posted || frame->manager!=manager || caller!=base_+0x81001d)return;
        frame->posted=true;Mailbox mailbox{};
        {std::lock_guard<std::mutex> guard(state_);if(!mailbox_.pending)return;mailbox=mailbox_;mailbox_={};}
        if(mailbox.action==Action::Open || mailbox.action==Action::UseClose) {frame->request=mailbox.request;frame->revocation=mailbox.revocation;frame->epoch=mailbox.epoch;frame->action=mailbox.action;}
        if(!mailbox.revocation || mailbox.revocation!=revocation_epoch_.load() || mailbox.request.manager!=manager || !qualified(mailbox.request,Phase::Author) || frame->cancelled) {cancel_receipt(mailbox.request.component,mailbox.epoch);return;}
        if(mailbox.action!=Action::Open && mailbox.action!=Action::UseClose) {control(mailbox.request,mailbox.action);return;}
        const auto previous=snapshot(mailbox.request.component);
        if(!previous.live || previous.epoch!=mailbox.epoch || previous.kind!=mailbox.action || previous.begun || previous.admitted || !same(previous.request,mailbox.request))return;
        if(mailbox.action==Action::UseClose) {if(!opened_close_ready(mailbox.request,frame->sense))return;}
        else if(!equals(mailbox.request.component+0x48,0u) || !equals(mailbox.request.component+0x4c,0u))return;
        const auto post=reinterpret_cast<Post>(base_+0x8112c0);
        const auto payload=post(frame->queue,0x96,4,mailbox.request.door_spawn);
        if(!payload)return;
        std::memcpy(reinterpret_cast<void*>(payload),&mailbox.request.player_spawn,4);
        if(frame->cancelled || frame->revocation!=revocation_epoch_.load() || !qualified(mailbox.request,Phase::Author))return;
        frame->payload=payload;++authored_;
    }
    bool envelope_matches(const Dispatch& frame,std::uintptr_t envelope) const noexcept {
        std::uint32_t message{},target{},player{};std::uintptr_t payload{};std::uint16_t header_message{},header_size{};std::uint32_t header_target{};
        return frame.payload && get(envelope,message) && message==0x96 && get(envelope+8,payload) && payload==frame.payload &&
            get(envelope+0x10,target) && target==frame.request.door_spawn && get(payload,player) && player==frame.request.player_spawn &&
            get(payload-8,header_message) && header_message==0x96 && get(payload-6,header_size) && header_size==4 &&
            get(payload-4,header_target) && header_target==target;
    }
    bool opened_close_ready(const Request& request,unsigned char& sense) const noexcept {
        const auto component=request.component;
        return opened_state(base_,read_,component,sense) && equals(component+(sense?0x1e2:0x1e3),request.operation.closing_clip);
    }
    void use_body(Producer& frame,std::uintptr_t envelope,std::uintptr_t caller) {
        auto* dispatch=dispatch_;
        Request request{};
        const bool authored=enabled() && dispatch && dispatch->owner==this && dispatch->eligible && !dispatch->cancelled && !dispatch->delivered && dispatch->revocation && dispatch->revocation==revocation_epoch_.load() &&
            caller==base_+0x74bde1 && dispatch->request.component==frame.component && envelope_matches(*dispatch,envelope);
        if(authored) {request=dispatch->request;dispatch->delivered=true;}
        if(!authored)cancel_component(frame.component);
        if(authored && qualified(request,Phase::Use)) {
            if(dispatch->action==Action::UseClose) {
                if(opened_close_ready(request,frame.sense) && frame.sense==dispatch->sense)begin(frame,request,dispatch->revocation,dispatch->epoch);
            } else if(equals(frame.component+0x48,0u) && equals(frame.component+0x4c,0u))begin(frame,request,dispatch->revocation,dispatch->epoch);
        }
        const auto invoke=reinterpret_cast<Binary>(original(3));if(invoke)invoke(frame.component,envelope);
        if(!frame.epoch)return;frame.completed=true;
        std::array<unsigned char,4> params{};
        if(frame.kind==Action::UseClose) {
            const std::array<unsigned char,4> expected{0,0,frame.sense,frame.request.operation.closing_clip};
            if(!dispatch || dispatch->cancelled || dispatch_!=dispatch || dispatch->revocation!=revocation_epoch_.load() || !current(frame) || !qualified(frame.request,Phase::Use) ||
               !equals(frame.component+0x48,2u) || !equals(frame.component+0x4c,1u) || !equals(frame.component+0x114,frame.request.player_spawn) ||
               !get(frame.component+0x90,params) || params!=expected) {cancel_receipt(frame.component,frame.epoch);return;}
            std::lock_guard<std::mutex> guard(state_);
            for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==frame.epoch && receipt.revocation==revocation_epoch_.load() && receipt.kind==Action::UseClose && same(receipt.request,frame.request)) {receipt={};++use_close_accepted_;}
            return;
        }
        if(!dispatch || dispatch->cancelled || dispatch_!=dispatch || !current(frame) || !qualified(frame.request,Phase::Use) || !equals(frame.component+0x48,0u) || !equals(frame.component+0x4c,1u) ||
           !equals(frame.component+0x114,frame.request.player_spawn) || !get(frame.component+0x90,params) || params[0]!=1 || params[3]!=frame.request.operation.opening_clip) {cancel_receipt(frame.component,frame.epoch);return;}
        {
            std::lock_guard<std::mutex> guard(state_);
            for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==frame.epoch) {receipt.params=params;receipt.pending=true;receipt.admitted=true;++admitted_;}
        }
    }
    void lookup_barrier(std::uintptr_t machine,int state,std::uintptr_t caller) {
        if(machine<0x40)return;const auto component=machine-0x40;const auto receipt=snapshot(component);if(!receipt.live || (!receipt.begun && !receipt.admitted))return;
        const auto relative=caller>=base_?caller-base_:0;auto* frame=producer_;bool allowed=false;
        if(frame && frame->owner==this && frame->component==component && frame->epoch==receipt.epoch && same(frame->request,receipt.request)) {
            if((frame->kind==Action::Open || frame->kind==Action::UseClose) && !frame->activation)allowed=relative==0x74b761 || relative==0x74b91e || relative==0x74b931;
            if(frame->kind==Action::Close)allowed=relative==0x74d81b || relative==0x74a428 || relative==0x74a438;
        }
        if((relative==0x6df760 || relative==0x6df76e) && receipt.pending && equals(component+0x48,0u) && equals(component+0x4c,1u))
            allowed=(relative==0x6df760 && state==0) || (relative==0x6df76e && state==1);
        if(relative==0x74af21 || relative==0x74af3e)allowed=true;
        if(!allowed)cancel_component(component);
    }
    void activation_body(Producer& frame,std::uintptr_t previous) {
        const auto receipt=snapshot(frame.component);
        if(receipt.live && receipt.pending && qualified(receipt.request,Phase::Activation) && equals(frame.component+0x48,1u) &&
           equals(frame.component+0x4c,1u) && equals(previous+8,frame.component+0x40) && previous==frame.component+0x60 &&
           equals(frame.component+0x90,receipt.params) && equals(frame.component+0x114,receipt.request.player_spawn)) {
            std::lock_guard<std::mutex> guard(state_);
            for(auto& stored:receipts_)if(stored.live && stored.epoch==receipt.epoch && stored.pending) {
                stored.pending=false;frame.request=stored.request;frame.epoch=stored.epoch;frame.activation=true;
            }
        } else if(receipt.live)cancel_component(frame.component);
        const auto invoke=reinterpret_cast<Binary>(original(6));if(invoke)invoke(frame.component+0x80,previous);
        if(!frame.epoch)return;
        frame.completed=frame.emission && current(frame);
        if(!frame.completed)cancel_receipt(frame.component,frame.epoch);
    }
    bool play_body(std::uintptr_t animation,int channel,unsigned char clip,float weight,int mode,float rate,std::uint32_t raw_start,bool extra,std::uintptr_t caller) {
        const auto invoke=reinterpret_cast<Play>(original(7));if(!invoke)return false;
        auto* frame=producer_;
        const bool opening=frame && frame->activation && frame->kind==Action::Open && caller==base_+0x74c7eb && mode==1 && clip==frame->request.operation.opening_clip;
        const bool closing=frame && frame->kind==Action::Close && caller==base_+0x74d8f5 && mode==4 && clip==frame->request.operation.closing_clip;
        if(frame && frame->owner==this && frame->epoch && !frame->emission && (opening || closing) && animation==frame->request.operation.animation &&
           channel==0 && current(*frame) && qualified(frame->request,frame->activation?Phase::Activation:Phase::Control)) {
            if(!animation_->publish(frame->request.operation)) {cancel_receipt(frame->component,frame->epoch);return invoke(animation,channel,clip,weight,mode,rate,raw_start,extra);}
            if(!current(*frame)) {animation_->revoke(frame->request.operation.ownership);return invoke(animation,channel,clip,weight,mode,rate,raw_start,extra);}
            const auto kind=opening?NativeDoorAnimation::RequestKind::Open:NativeDoorAnimation::RequestKind::ClosedEndpoint;
            const bool result=animation_->request_call(frame->request.operation,kind,[&] {return invoke(animation,channel,clip,weight,mode,rate,raw_start,extra);});
            if(result && current(*frame)) {frame->emission=true;++emitted_;std::lock_guard<std::mutex> guard(state_);for(auto& receipt:receipts_)if(receipt.live && receipt.epoch==frame->epoch)receipt.active=true;}
            else cancel_receipt(frame->component,frame->epoch);
            return result;
        }
        std::array<std::uintptr_t,2> components{};unsigned count{};
        {std::lock_guard<std::mutex> guard(state_);for(const auto& receipt:receipts_)if(receipt.live && receipt.request.operation.animation==animation)components[count++]=receipt.request.component;}
        for(unsigned index=0;index<count;++index)cancel_component(components[index]);
        return invoke(animation,channel,clip,weight,mode,rate,raw_start,extra);
    }
    void update_request(const Request& request,const Producer& frame) {
        const bool published=animation_->publish(request.operation);
        if(published && current(frame)) {std::lock_guard<std::mutex> guard(state_);for(auto& stored:receipts_)if(stored.live && stored.epoch==frame.epoch)stored.request=request;}
        else {animation_->revoke(request.operation.ownership);cancel_receipt(request.component,frame.epoch);}
    }
    void control(const Request& request,Action action) {
        const auto receipt=snapshot(request.component);
        if(!receipt.live || !receipt.active || !same(receipt.request,request))return;
        if(!qualified(request,Phase::Control)) {cancel_receipt(request.component,receipt.epoch);return;}
        Producer frame{};frame.previous=producer_;frame.owner=this;frame.component=request.component;frame.request=request;frame.epoch=receipt.epoch;frame.kind=action;
        if(action==Action::Update) {update_request(request,frame);return;}
        if(action!=Action::Close || !request.operation.close_authorized || !equals(request.component+0x48,1u) || !equals(request.component+0x4c,1u) || !animation_->close_ready(request.operation))return;
        producer_=&frame;
        __try {invoke_retarget(request.component,3,receipt.params[2]==0);frame.completed=frame.emission;}
        __finally {if(!frame.completed)cancel_receipt(request.component,frame.epoch);producer_=frame.previous;}
    }
    static void __fastcall dispatch_hook(std::uintptr_t manager,std::uintptr_t queue) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());process_owner().invoke_dispatch(manager,queue,caller);}
    static void __fastcall flush_hook(std::uintptr_t manager) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());process_owner().invoke_flush(manager,caller);}
    static void __fastcall reset_hook(std::uintptr_t queue) {process_owner().invoke_reset(queue);}
    static void __fastcall use_hook(std::uintptr_t component,std::uintptr_t envelope) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());process_owner().invoke_use(component,envelope,caller);}
    static void __fastcall retarget_hook(std::uintptr_t component,int endpoint,bool sense) {process_owner().invoke_retarget(component,endpoint,sense);}
    static std::uintptr_t __fastcall lookup_hook(std::uintptr_t machine,int state) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return process_owner().invoke_lookup(machine,state,caller);}
    static void __fastcall activate_hook(std::uintptr_t animated,std::uintptr_t previous) {process_owner().invoke_activate(animated,previous);}
    static bool __fastcall play_hook(std::uintptr_t animation,int channel,unsigned char clip,float weight,int mode,float rate,std::uint32_t raw_start,bool extra) {
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());return process_owner().invoke_play(animation,channel,clip,weight,mode,rate,raw_start,extra,caller);
    }
    static void __fastcall windblast_hook(std::uintptr_t component,std::uintptr_t envelope) {process_owner().invoke_binary_guard(8,component,envelope);}
    static void __fastcall script_hook(std::uintptr_t component,bool sense,bool unlock) {process_owner().invoke_script(component,sense,unlock);}
    static void __fastcall serialize_hook(std::uintptr_t component,std::uintptr_t stream,std::uint32_t version) {process_owner().invoke_serialize(component,stream,version);}
    static void __fastcall attach_hook(std::uintptr_t component) {process_owner().invoke_unary_guard(11,component);}
    static void __fastcall detach_hook(std::uintptr_t component) {process_owner().invoke_unary_guard(12,component);}
    static void __fastcall start_hook(std::uintptr_t component) {process_owner().invoke_unary_guard(13,component);}
    static void __fastcall stop_hook(std::uintptr_t component) {process_owner().invoke_unary_guard(14,component);}
    static void __fastcall destroy_hook(std::uintptr_t component) {process_owner().invoke_unary_guard(15,component);}
    static std::array<void*,16> hooks() noexcept {
        return {reinterpret_cast<void*>(&dispatch_hook),reinterpret_cast<void*>(&flush_hook),reinterpret_cast<void*>(&reset_hook),reinterpret_cast<void*>(&use_hook),
            reinterpret_cast<void*>(&retarget_hook),reinterpret_cast<void*>(&lookup_hook),reinterpret_cast<void*>(&activate_hook),reinterpret_cast<void*>(&play_hook),
            reinterpret_cast<void*>(&windblast_hook),reinterpret_cast<void*>(&script_hook),reinterpret_cast<void*>(&serialize_hook),reinterpret_cast<void*>(&attach_hook),
            reinterpret_cast<void*>(&detach_hook),reinterpret_cast<void*>(&start_hook),reinterpret_cast<void*>(&stop_hook),reinterpret_cast<void*>(&destroy_hook)};
    }
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
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));const auto prefix=prefixes[index];std::array<unsigned char,39> code{};
        const auto replacement=tail(base_+rvas[index]+prefix,true),expected=tail(base_+rvas[index]+prefix,patched_[index]),relay=tail(reinterpret_cast<std::uintptr_t>(hooks()[index]),false);
        if(!trampoline || !local_read(trampoline,code.data(),prefix+28) || std::memcmp(code.data(),prologues[index].data(),prefix) ||
           std::memcmp(code.data()+prefix,expected.data(),14) || std::memcmp(code.data()+prefix+14,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            metadata=static_cast<Metadata*>(alloc_unwind_above(trampoline));if(!metadata)return false;metadata_[index]=metadata;
            const auto rva=reinterpret_cast<std::uintptr_t>(metadata)-trampoline;
            metadata->functions={RUNTIME_FUNCTION{0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},RUNTIME_FUNCTION{prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}};
            metadata->unwind=unwinds[index];metadata->relay={1,0,0,0};
        }
        if(!patched_[index]) {
            DWORD previous{};if(!VirtualProtect(reinterpret_cast<void*>(trampoline+prefix),14,PAGE_EXECUTE_READWRITE,&previous))return false;
            protection_[index]=previous;std::memcpy(reinterpret_cast<void*>(trampoline+prefix),replacement.data(),14);patched_[index]=true;protection_pending_[index]=true;cache_pending_[index]=true;
            if(!restore_protection(index))return false;
        }
        const auto base=trampoline,block=reinterpret_cast<std::uintptr_t>(metadata);if(block<=base || block-base>UINT32_MAX-4096)return false;
        const auto rva=block-base;
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{{0,prefix+14,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},{prefix+14,prefix+28,static_cast<DWORD>(rva+offsetof(Metadata,relay))}}};
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
        for(const unsigned index:disable_order) {
            if(created(index) && !original(index))return false;
            if(created(index)) {const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[index]=false;}
            while(in_flight()) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        }
        for(unsigned index=0;index<16;++index)if(created(index) && !restore_protection(index))return false;
        if(base_ && !verify(base_,read_))return false;stop_complete_=true;return true;
    }
    bool rollback() noexcept {stop_locked(0);return false;}
    std::uintptr_t base_{};Read read_{};Qualify qualify_{};NativeDoorAnimation* animation_{};
    std::mutex lifecycle_,state_;Mailbox mailbox_{};std::array<Receipt,2> receipts_{};
    std::array<std::atomic<void*>,16> originals_{};std::array<std::atomic<bool>,16> created_{},installed_{};
    std::array<Metadata*,16> metadata_{};std::array<bool,16> registered_{},patched_{},protection_pending_{},cache_pending_{};std::array<DWORD,16> protection_{};
    std::atomic<bool> stopping_{true},stop_complete_{true},unwind_ready_{};std::atomic<unsigned> in_flight_{};
    std::uint64_t sequence_{};std::atomic<std::uint64_t> revocation_epoch_{1},authored_{},admitted_{},emitted_{},cancelled_{},use_close_accepted_{};
    inline static thread_local Dispatch* dispatch_{};inline static thread_local Producer* producer_{};inline static thread_local unsigned callback_depth_{};
};
}

