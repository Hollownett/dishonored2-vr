#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include "native_trampoline_unwind.hpp"
#include <intrin.h>
#include "native_slash_speed.hpp"
#include <atomic>
#include <mutex>

namespace d2vr {
class NativeOfferedChoke {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=unsigned (*)();
    using Update=void (__fastcall*)(std::uintptr_t);
    using Hint=void (__fastcall*)(std::uintptr_t,int,unsigned char,unsigned char);
    struct Receipt {
        NativePlayerStateLease owner{};
        std::uintptr_t hud{},container{},sensor{},victim{};
        rig::Identity container_identity{},sensor_identity{};
        std::uint32_t spawn{};
        std::uint64_t epoch{},tick{};
    };
    enum class Rejection : unsigned {disabled,missing_receipt,expired,record,owner,epoch,no_hint,count};
    enum class QualifyPhase : unsigned {before_hint,after_hint,update,current,count};
    enum class QualifyStage : unsigned {hud,machine,sensors,sensor,victim,final_read,count};
    struct Diagnostics {
        std::uint64_t updates{},hints{},exact_candidates{},published{};
        std::array<std::uint64_t,static_cast<unsigned>(Rejection::count)> rejected{};
        std::array<std::array<std::uint64_t,static_cast<unsigned>(QualifyStage::count)>,static_cast<unsigned>(QualifyPhase::count)> qualify_rejected{};
    };
    Diagnostics diagnostics() const noexcept {
        Diagnostics result{updates_.load(),hints_.load(),exact_candidates_.load(),published_.load(),{}};
        for(unsigned index=0;index<result.rejected.size();++index)result.rejected[index]=rejected_[index].load();
        for(unsigned phase=0;phase<result.qualify_rejected.size();++phase)
            for(unsigned stage=0;stage<result.qualify_rejected[phase].size();++stage)
                result.qualify_rejected[phase][stage]=qualify_rejected_[phase][stage].load();
        return result;
    }
    inline static constexpr std::array<std::uintptr_t,2> rvas{0x391280,0x391550};
    inline static constexpr std::array<unsigned,2> sizes{366,252};
    inline static constexpr std::array<std::uint64_t,2> hashes{0xde5a0e9ec71e378full,0x6fffc255cf8e88b5ull};
    inline static constexpr std::array<std::array<unsigned char,32>,2> prologues{{
        {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xba,2,0,0,0,0x48,0x83,0xc1,0x40,0xe8,0x75,0x96,4,0,0x48,0x8b,0xf8,0x48,0x85},
        {0x48,0x8b,0xc4,0x41,0x56,0x48,0x83,0xec,0x70,0x48,0xc7,0x40,0xa8,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,8,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78}}};
    inline static constexpr std::array<std::array<unsigned char,8>,2> unwinds{{{1,5,2,0,5,0x34,2,0},{1,5,1,0,5,0xe0,0,0}}};
    static NativeOfferedChoke& process_owner() {static auto* owner=new NativeOfferedChoke;return *owner;}
    static bool verify(std::uintptr_t base,Read read) noexcept {
        Reader reader{read};std::array<unsigned char,366> bytes{};
        if(!base || !read)return false;
        for(unsigned index=0;index<2;++index) {
            if(!reader(base+rvas[index],bytes.data(),sizes[index]))return false;
            std::uint64_t hash=14695981039346656037ull;
            for(unsigned offset=0;offset<sizes[index];++offset)hash=(hash^bytes[offset])*1099511628211ull;
            if(hash!=hashes[index])return false;
        }
        return true;
    }
    bool install(std::uintptr_t base,Read read,Logic logic) noexcept {
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);
        if(!guard.owns_lock() || !logic || !ready_for_install() || (base_ && (base_!=base || read_!=read || logic_!=logic)) || !verify(base,read))return false;
        base_=base;read_=read;logic_=logic;stopping_=true;stop_complete_=false;clear();
        const auto callbacks=hooks();
        for(unsigned index=0;index<2;++index)if(!created_[index]) {
            void* original{};
            if(MH_CreateHook(reinterpret_cast<void*>(base+rvas[index]),callbacks[index],&original)!=MH_OK)return rollback();
            originals_[index]=original;created_[index]=true;if(!original)return rollback();
        }
        for(unsigned index=0;index<2;++index)if(!prepare(index))return rollback();
        if(!verify(base,read))return rollback();
        for(unsigned index=0;index<2;++index) {
            if(MH_EnableHook(reinterpret_cast<void*>(base+rvas[index]))!=MH_OK)return rollback();installed_[index]=true;
        }
        stopping_=false;return true;
    }
    bool enabled() const noexcept {return !stopping_ && !exhausted_ && installed_[0] && installed_[1];}
    bool stopped() const noexcept {
        if(!stop_complete_ || installed_[0] || installed_[1] || in_flight_)return false;
        for(unsigned index=0;index<2;++index)if(protection_pending_[index] || cache_pending_[index])return false;
        return true;
    }
    bool ready_for_install() const noexcept {
        if(exhausted_ || !stopped())return false;
        for(unsigned index=0;index<2;++index)if(created_[index] && !originals_[index])return false;
        return true;
    }
    unsigned in_flight() const noexcept {return in_flight_;}
    void* original(unsigned index) const noexcept {return index<2?originals_[index].load():nullptr;}
    void clear() noexcept {std::lock_guard<std::mutex> guard(state_);advance_locked();receipt_.reset();}
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        stopping_=true;clear();if(depth_)return false;
        std::unique_lock<std::mutex> guard(lifecycle_,std::try_to_lock);if(!guard.owns_lock())return false;
        return stop_locked(timeout_ms);
    }
    std::optional<Receipt> offered(std::uint64_t now,std::uint64_t max_age_ms=100) const noexcept {
        std::optional<Receipt> copy;
        {std::lock_guard<std::mutex> guard(state_);copy=receipt_;}
        if(!copy) {reject(Rejection::missing_receipt);return {};}
        if(!current(*copy,now,max_age_ms))return {};return copy;
    }
    bool current(const Receipt& receipt,std::uint64_t now,std::uint64_t max_age_ms=100) const noexcept {
        if(!enabled() || !gameplay())return reject(Rejection::disabled);
        if(!receipt.epoch)return reject(Rejection::epoch);
        if(now<receipt.tick || now-receipt.tick>max_age_ms)return reject(Rejection::expired);
        if(!record_current(receipt.hud,RecordPhase::displayed))return reject(Rejection::record);
        const auto fresh=qualify(receipt.hud,QualifyPhase::current);
        if(!fresh || !same(receipt,*fresh))return reject(Rejection::owner);
        if(!record_current(receipt.hud,RecordPhase::displayed))return reject(Rejection::record);
        std::lock_guard<std::mutex> guard(state_);
        if(!enabled() || receipt.epoch!=epoch_ || !receipt_ || receipt_->epoch!=receipt.epoch)return reject(Rejection::epoch);
        return true;
    }
    static std::uint32_t active_victim(Read read,std::uintptr_t base,std::uintptr_t machine) noexcept {
        Reader reader{read};const auto lease=native_player_state_lease(reader,base,machine);if(!lease)return 0;
        const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        const auto state=pool?rig::value<std::uintptr_t>(reader,*pool+0x158):std::nullopt;
        if(!state || !rig::typed(reader,base,*state,0x1c77c18) || rig::value<std::uint32_t>(reader,*state+8)!=0x29u ||
           rig::value<std::uintptr_t>(reader,*state+0x10)!=machine || rig::value<unsigned char>(reader,*state+0x20)!=1)return 0;
        const auto spawn=rig::value<std::uint32_t>(reader,*state+0xb8);
        if(!spawn || !target(reader,*lease,*spawn))return 0;
        if(!(native_player_state_lease(reader,base,machine)==lease) || rig::value<std::uintptr_t>(reader,machine+0x90)!=pool ||
           rig::value<std::uintptr_t>(reader,*pool+0x158)!=state || rig::value<unsigned char>(reader,*state+0x20)!=1 ||
           rig::value<std::uint32_t>(reader,*state+0xb8)!=spawn)return 0;
        return *spawn;
    }
    void invoke_update(std::uintptr_t hud) {
        Frame frame{};frame.owner=this;frame.previous=frame_;frame.hud=hud;
        ++in_flight_;++depth_;frame_= &frame;
        __try {update_body(frame);}__finally {frame_=frame.previous;--depth_;--in_flight_;}
    }
    void invoke_hint(std::uintptr_t caller,std::uintptr_t hud,int action,unsigned char normal,unsigned char alternate) {
        ++in_flight_;++depth_;
        __try {hint_body(caller,hud,action,normal,alternate);}__finally {--depth_;--in_flight_;}
    }
private:
    friend struct NativeOfferedChokeFixture;
    NativeOfferedChoke()=default;
    struct Reader {
        Read read{};
        static bool cpp(Read callback,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return callback && callback(address,output,size);}catch(...) {return false;}}
        bool operator()(std::uintptr_t address,void* output,std::size_t size) const noexcept {
            __try {return cpp(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
    };
    struct Frame {NativeOfferedChoke* owner{};Frame* previous{};std::uintptr_t hud{};std::uint64_t epoch{};std::optional<Receipt> hint{};};
    struct Metadata {std::array<RUNTIME_FUNCTION,2> functions{};std::array<unsigned char,8> unwind{};std::array<unsigned char,4> relay{1,0,0,0};};
    static bool local_read(std::uintptr_t address,void* output,std::size_t size) noexcept {
        __try {std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static unsigned cpp_logic(Logic logic) noexcept {try {return logic?logic():0;}catch(...) {return 0;}}
    bool gameplay() const noexcept {__try {return cpp_logic(logic_)==2;}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}}
    static std::uintptr_t target(Reader& reader,const NativePlayerStateLease& owner,std::uint32_t spawn) noexcept {
        if(!spawn || spawn==0xffffffffu || (spawn>>16)>=0x3fff)return 0;
        const auto slot=owner.game+0x3130b0+(spawn>>16)*8ull;
        const auto entity=rig::value<std::uintptr_t>(reader,slot);
        if(!entity || !rig::typed(reader,owner.base,*entity,0x1c5e108) || rig::value<std::uint32_t>(reader,*entity+0xa8)!=spawn ||
           rig::value<std::uintptr_t>(reader,slot)!=entity)return 0;
        return *entity;
    }
    std::optional<Receipt> qualify(std::uintptr_t hud,QualifyPhase phase) const noexcept {
        const auto failed=[&](QualifyStage stage)->std::optional<Receipt> {++qualify_rejected_[static_cast<unsigned>(phase)][static_cast<unsigned>(stage)];return {};};
        Reader reader{read_};if(!enabled() || !gameplay() || !rig::typed(reader,base_,hud,0x1b3a3c8))return failed(QualifyStage::hud);
        const auto machine=rig::value<std::uintptr_t>(reader,hud+0x48);
        const auto owner=machine?native_player_state_lease(reader,base_,*machine):std::nullopt;
        if(!owner || rig::value<std::int32_t>(reader,hud+0x40)!=owner->identity.generation)return failed(QualifyStage::machine);
        const auto container=rig::value<std::uintptr_t>(reader,hud+0x68);
        const auto identity=container?rig::started(reader,*container):std::nullopt;
        if(!identity || rig::value<std::int32_t>(reader,hud+0x60)!=identity->generation ||
           rig::value<std::uintptr_t>(reader,owner->machine+0x338)!=container || rig::value<std::uintptr_t>(reader,*container+0x10)!=owner->player)return failed(QualifyStage::sensors);
        const auto sensor=rig::value<std::uintptr_t>(reader,*container+0xd8);
        const auto sensor_identity=sensor?rig::started(reader,*sensor):std::nullopt;
        if(!sensor_identity || !rig::typed(reader,base_,*sensor,0x1b3e158) || rig::value<std::uintptr_t>(reader,*sensor+0x10)!=owner->player ||
           rig::value<std::uintptr_t>(reader,*sensor+0x20)!=*machine || rig::value<std::uintptr_t>(reader,*sensor+0x30)!=*container ||
           rig::value<unsigned char>(reader,*sensor+0x38).value_or(0)==0)return failed(QualifyStage::sensor);
        const auto spawn=rig::value<std::uint32_t>(reader,*sensor+0x50);
        const auto victim=spawn?target(reader,*owner,*spawn):0;if(!victim)return failed(QualifyStage::victim);
        if(!(native_player_state_lease(reader,base_,*machine)==owner) || rig::value<std::uintptr_t>(reader,hud+0x48)!=machine ||
           rig::value<std::int32_t>(reader,hud+0x40)!=owner->identity.generation || rig::value<std::uintptr_t>(reader,hud+0x68)!=container ||
           rig::value<std::int32_t>(reader,hud+0x60)!=identity->generation || rig::started(reader,*container)!=identity ||
           rig::value<std::uintptr_t>(reader,owner->machine+0x338)!=container || rig::value<std::uintptr_t>(reader,*container+0xd8)!=sensor ||
           rig::started(reader,*sensor)!=sensor_identity || rig::value<std::uint32_t>(reader,*sensor+0x50)!=spawn)return failed(QualifyStage::final_read);
        return Receipt{*owner,hud,*container,*sensor,victim,*identity,*sensor_identity,*spawn,0,0};
    }
    static bool same(const Receipt& first,const Receipt& second) noexcept {
        return first.owner==second.owner && first.hud==second.hud && first.container==second.container && first.sensor==second.sensor &&
            first.victim==second.victim && first.container_identity==second.container_identity && first.sensor_identity==second.sensor_identity && first.spawn==second.spawn;
    }
    enum class RecordPhase {authored,displayed};
    struct RecordFields {int action{};unsigned char normal{},alternate{},flags{};};
    static bool same_record(const RecordFields& first,const RecordFields& second) noexcept {
        return first.action==second.action && first.normal==second.normal && first.alternate==second.alternate && first.flags==second.flags;
    }
    bool reject(Rejection reason) const noexcept {++rejected_[static_cast<unsigned>(reason)];return false;}
    bool record_current(std::uintptr_t hud,RecordPhase phase) const noexcept {
        Reader reader{read_};if(rig::value<int>(reader,base_+0x397d3a8).value_or(0)==0)return false;
        const auto records=rig::list(reader,hud+0x80,128);if(!records)return false;
        const auto fields=[&](std::uintptr_t address)->std::optional<RecordFields> {
            const auto action=rig::value<int>(reader,address);
            const auto normal=rig::value<unsigned char>(reader,address+0x38),alternate=rig::value<unsigned char>(reader,address+0x39),
                flags=rig::value<unsigned char>(reader,address+0x3a);
            if(!action || !normal || !alternate || !flags)return {};
            return RecordFields{*action,*normal,*alternate,*flags};
        };
        unsigned matches{};
        std::array<RecordFields,128> observed{};
        for(unsigned index=0;index<records->packed_count;++index) {
            const auto record=records->data+index*0x40ull;
            const auto first=fields(record);if(!first)return false;
            observed[index]=*first;
            if(first->action==9 && first->normal==1 && first->alternate==0) {
                ++matches;
                const bool accepted=phase==RecordPhase::authored?(first->flags&2)!=0:
                    (first->flags&4)!=0 && (first->flags&1)==0;
                if(!accepted || (first->flags&8))return false;
            }
        }
        for(unsigned index=0;index<records->packed_count;++index) {
            const auto second=fields(records->data+index*0x40ull);
            if(!second || !same_record(observed[index],*second))return false;
        }
        const auto after=rig::list(reader,hud+0x80,128);
        return matches==1 && after && after->data==records->data && after->packed_count==records->packed_count &&
            after->packed_capacity==records->packed_capacity && rig::value<int>(reader,base_+0x397d3a8).value_or(0)!=0;
    }
    void update_body(Frame& frame) {
        ++updates_;
        const auto original=reinterpret_cast<Update>(this->original(0));if(!original)return;
        {std::lock_guard<std::mutex> guard(state_);frame.epoch=advance_locked();receipt_.reset();}
        original(frame.hud);
        if(!enabled()) {reject(Rejection::disabled);return;}
        if(!frame.hint) {reject(Rejection::no_hint);return;}
        if(!record_current(frame.hud,RecordPhase::authored)) {reject(Rejection::record);return;}
        const auto after=qualify(frame.hud,QualifyPhase::update);if(!after || !same(*after,*frame.hint)) {reject(Rejection::owner);return;}
        std::lock_guard<std::mutex> guard(state_);
        if(enabled() && frame.epoch && frame.epoch==epoch_) {frame.hint->epoch=frame.epoch;frame.hint->tick=GetTickCount64();receipt_=frame.hint;++published_;}
        else reject(Rejection::epoch);
    }
    void hint_body(std::uintptr_t caller,std::uintptr_t hud,int action,unsigned char normal,unsigned char alternate) {
        ++hints_;
        const auto original=reinterpret_cast<Hint>(this->original(1));if(!original)return;
        Frame* frame=frame_;const bool candidate=enabled() && frame && frame->owner==this && frame->hud==hud && caller==base_+0x3913e3 && action==9 && normal==1 && alternate==0;
        if(candidate)++exact_candidates_;
        const auto before=candidate?qualify(hud,QualifyPhase::before_hint):std::nullopt;
        original(hud,action,normal,alternate);
        if(!candidate)return;
        if(!before) {reject(Rejection::owner);return;}
        if(!enabled() || frame_!=frame) {reject(Rejection::epoch);return;}
        if(!record_current(hud,RecordPhase::authored)) {reject(Rejection::record);return;}
        const auto after=qualify(hud,QualifyPhase::after_hint);if(after && same(*before,*after))frame->hint=after;
        else reject(Rejection::owner);
    }
    static void __fastcall update_hook(std::uintptr_t hud) {process_owner().invoke_update(hud);}
    static void __fastcall hint_hook(std::uintptr_t hud,int action,unsigned char normal,unsigned char alternate) {
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());process_owner().invoke_hint(caller,hud,action,normal,alternate);
    }
    static std::array<void*,2> hooks() noexcept {return {reinterpret_cast<void*>(&update_hook),reinterpret_cast<void*>(&hint_hook)};}
    static std::array<unsigned char,14> tail(std::uintptr_t target,bool owned) noexcept {
        std::array<unsigned char,14> code{};
        if(owned) {code[0]=0x49;code[1]=0xbb;std::memcpy(code.data()+2,&target,8);code[10]=0x41;code[11]=0xff;code[12]=0xe3;code[13]=0x90;}
        else {code[0]=0xff;code[1]=0x25;std::memcpy(code.data()+6,&target,8);}return code;
    }
    bool protect(unsigned index) noexcept {
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));DWORD ignored{};
        if(protection_pending_[index]) {if(!VirtualProtect(reinterpret_cast<void*>(trampoline+5),14,protection_[index],&ignored))return false;protection_pending_[index]=false;}
        if(cache_pending_[index]) {if(!FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(trampoline),19))return false;cache_pending_[index]=false;}return true;
    }
    bool prepare(unsigned index) noexcept {
        if(!protect(index))return false;
        const auto trampoline=reinterpret_cast<std::uintptr_t>(original(index));std::array<unsigned char,33> code{};
        const auto replacement=tail(base_+rvas[index]+5,true),expected=tail(base_+rvas[index]+5,patched_[index]),relay=tail(reinterpret_cast<std::uintptr_t>(hooks()[index]),false);
        if(!trampoline || !local_read(trampoline,code.data(),33) || std::memcmp(code.data(),prologues[index].data(),5) ||
           std::memcmp(code.data()+5,expected.data(),14) || std::memcmp(code.data()+19,relay.data(),14))return false;
        auto* metadata=metadata_[index];
        if(!metadata) {
            metadata=static_cast<Metadata*>(alloc_unwind_above(trampoline));if(!metadata)return false;metadata_[index]=metadata;
            const auto rva=reinterpret_cast<std::uintptr_t>(metadata)-trampoline;
            metadata->functions={RUNTIME_FUNCTION{0,19,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},RUNTIME_FUNCTION{19,33,static_cast<DWORD>(rva+offsetof(Metadata,relay))}};
            metadata->unwind=unwinds[index];metadata->relay={1,0,0,0};
        }
        if(!patched_[index]) {
            DWORD previous{};if(!VirtualProtect(reinterpret_cast<void*>(trampoline+5),14,PAGE_EXECUTE_READWRITE,&previous))return false;
            protection_[index]=previous;std::memcpy(reinterpret_cast<void*>(trampoline+5),replacement.data(),14);patched_[index]=true;protection_pending_[index]=true;cache_pending_[index]=true;
            if(!protect(index))return false;
        }
        const auto base=trampoline,block=reinterpret_cast<std::uintptr_t>(metadata);if(block<=base || block-base>UINT32_MAX-4096)return false;
        const auto rva=block-base;
        const std::array<RUNTIME_FUNCTION,2> expected_functions{{{0,19,static_cast<DWORD>(rva+offsetof(Metadata,unwind))},{19,33,static_cast<DWORD>(rva+offsetof(Metadata,relay))}}};
        if(std::memcmp(metadata->functions.data(),expected_functions.data(),sizeof(expected_functions)) || metadata->unwind!=unwinds[index] || metadata->relay!=std::array<unsigned char,4>{1,0,0,0})return false;
        if(!registered_[index]) {
            for(unsigned offset=0;offset<33;++offset) {DWORD64 lookup{};if(RtlLookupFunctionEntry(trampoline+offset,&lookup,nullptr))return false;}
            if(!RtlAddFunctionTable(metadata->functions.data(),2,base))return false;registered_[index]=true;
        }
        DWORD64 lookup{};
        return local_read(trampoline+5,code.data(),14) && !std::memcmp(code.data(),replacement.data(),14) &&
            RtlLookupFunctionEntry(trampoline+5,&lookup,nullptr)==&metadata->functions[0] && lookup==base &&
            RtlLookupFunctionEntry(trampoline+19,&lookup,nullptr)==&metadata->functions[1] && lookup==base;
    }
    bool stop_locked(std::uint64_t timeout_ms) noexcept {
        stop_complete_=false;const auto began=GetTickCount64();
        for(const unsigned index:{1u,0u}) {
            if(created_[index] && !originals_[index])return false;
            if(created_[index]) {const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+rvas[index]));if(status!=MH_OK && status!=MH_ERROR_DISABLED)return false;installed_[index]=false;}
        }
        while(in_flight_) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        for(unsigned index=0;index<2;++index)if(created_[index] && !protect(index))return false;
        for(unsigned index=0;index<2;++index)if(created_[index]) {std::array<unsigned char,5> bytes{};Reader reader{read_};if(!reader(base_+rvas[index],bytes.data(),5) || std::memcmp(bytes.data(),prologues[index].data(),5))return false;}
        stop_complete_=true;return true;
    }
    bool rollback() noexcept {stopping_=true;clear();stop_locked(5000);return false;}
    std::uint64_t advance_locked() noexcept {
        if(exhausted_ || epoch_==UINT64_MAX) {exhausted_=true;return 0;}return ++epoch_;
    }
    std::uintptr_t base_{};Read read_{};Logic logic_{};
    mutable std::mutex state_;std::mutex lifecycle_;std::optional<Receipt> receipt_;std::uint64_t epoch_{};
    std::array<std::atomic<void*>,2> originals_{};std::array<std::atomic<bool>,2> created_{},installed_{};
    std::array<Metadata*,2> metadata_{};std::array<bool,2> registered_{},patched_{};std::array<std::atomic<bool>,2> protection_pending_{},cache_pending_{};std::array<DWORD,2> protection_{};
    std::atomic<bool> stopping_{true},stop_complete_{true},exhausted_{};std::atomic<unsigned> in_flight_{};
    std::atomic<std::uint64_t> updates_{},hints_{},exact_candidates_{},published_{};
    mutable std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(Rejection::count)> rejected_{};
    mutable std::array<std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(QualifyStage::count)>,static_cast<unsigned>(QualifyPhase::count)> qualify_rejected_{};
    inline static thread_local unsigned depth_{};inline static thread_local Frame* frame_{};
};
}
