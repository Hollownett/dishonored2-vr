#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_arm_binding_snapshot.hpp"
#include "arm_tracking.hpp"
#include <mutex>

namespace d2vr {
struct NativeWeaponSourceContent {
    rig::Arm arm{};std::uintptr_t game{},source{};
    std::uint32_t frame{};
    std::array<rig::List,3> lists{};
    std::array<std::int16_t,69> remap{};
    std::array<rig::Transform,69> joints{};
};
struct NativeWeaponModelBase {
    std::uintptr_t model{},game{};std::uint32_t frame{};
    std::array<float,15> values{};bool valid{};
};
inline bool same_weapon_model_base(const NativeWeaponModelBase& a,const NativeWeaponModelBase& b) {
    return a.valid && b.valid && a.model==b.model && a.game==b.game && a.frame==b.frame &&
        !std::memcmp(a.values.data(),b.values.data(),sizeof(a.values));
}
template<class Read> std::optional<std::pair<std::uintptr_t,std::uint32_t>> weapon_source_owner(
    Read& read,std::uintptr_t base,const rig::Arm& arm) {
    using namespace rig;
    if(arm.side>1) return {};
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    const auto descriptor=value<std::uintptr_t>(read,arm.model+0x1fe8);
    if(!game || !typed(read,base,*game,0x1c940a8) || value<std::uintptr_t>(read,*game+0x3130b0)!=arm.player ||
       !typed(read,base,arm.player,0x1c5e428) || !typed(read,base,arm.body,0x1c5a8d8,0x1c5a9b0) ||
       value<std::uintptr_t>(read,arm.body+0x10)!=arm.player || started(read,arm.body)!=std::optional<Identity>(arm.body_identity) ||
       value<std::uintptr_t>(read,arm.body+(arm.side?0x688:0x378))!=arm.animation ||
       !typed(read,base,arm.animation,0x1b80408,0x1b804f0) || started(read,arm.animation)!=std::optional<Identity>(arm.animation_identity) ||
       value<std::uintptr_t>(read,arm.animation+0x38)!=arm.model || !typed(read,base,arm.model,0x1b0d628) ||
       !descriptor || !pointer(*descriptor) || value<std::uintptr_t>(read,*descriptor+0x50)!=arm.skeleton ||
       value<std::uint16_t>(read,arm.skeleton+0x78)!=69) return {};
    const auto frame=value<std::uint32_t>(read,*game+0x2e7428);
    return frame?std::optional<std::pair<std::uintptr_t,std::uint32_t>>({*game,*frame}):std::nullopt;
}
template<class Read> NativeWeaponModelBase weapon_model_base(Read& read,std::uintptr_t base,const rig::Arm& arm) {
    NativeWeaponModelBase result{};result.model=arm.model;
    const auto owner=weapon_source_owner(read,base,arm);if(!owner) return result;
    result.game=owner->first;result.frame=owner->second;
    std::array<float,15> again{};
    if(!read(arm.model+0x1860,result.values.data(),sizeof(result.values))) return result;
    try {const rig::ModelFrame validated(result.values);(void)validated;} catch(...) {return result;}
    if(!read(arm.model+0x1860,again.data(),sizeof(again)) ||
       std::memcmp(again.data(),result.values.data(),sizeof(again)) || weapon_source_owner(read,base,arm)!=owner) return result;
    result.valid=true;return result;
}
template<class Read> std::optional<NativeWeaponSourceContent> weapon_source_content(Read& read,std::uintptr_t base,const rig::Arm& arm) {
    using namespace rig;
    const auto owner=weapon_source_owner(read,base,arm);if(!owner) return {};
    const auto transforms=list(read,arm.animation+0x2d8,4096),states=list(read,arm.animation+0x2e8,4);
    if(!transforms || !states || states->packed_count!=4) return {};
    const auto mapping=list(read,states->data+0x30,69);
    if(!mapping || mapping->packed_count!=69) return {};
    NativeWeaponSourceContent result{};result.arm=arm;result.game=owner->first;result.frame=owner->second;
    const std::array<std::uintptr_t,3> addresses{arm.animation+0x2d8,arm.animation+0x2e8,states->data+0x30};
    for(unsigned i=0;i<3;++i) if(!read(addresses[i],&result.lists[i],sizeof(List))) return {};
    if(!read(mapping->data,result.remap.data(),sizeof(result.remap)) || result.remap[0]<0) return {};
    for(unsigned i=0;i<69;++i) if(result.remap[i]!=result.remap[0]+static_cast<int>(i) ||
        static_cast<unsigned>(result.remap[i])>=transforms->packed_count) return {};
    result.source=transforms->data+static_cast<std::uintptr_t>(result.remap[0])*64;
    if(!read(result.source,result.joints.data(),sizeof(result.joints))) return {};
    for(const auto& joint:result.joints) if(!valid(joint)) return {};
    std::array<Transform,69> again{};std::array<std::int16_t,69> remap{};
    if(!read(result.source,again.data(),sizeof(again)) || std::memcmp(again.data(),result.joints.data(),sizeof(again)) ||
       !read(mapping->data,remap.data(),sizeof(remap)) || remap!=result.remap) return {};
    for(unsigned i=0;i<3;++i) {List fresh{};if(!read(addresses[i],&fresh,sizeof(fresh)) ||
        std::memcmp(&fresh,&result.lists[i],sizeof(fresh))) return {};}
    if(weapon_source_owner(read,base,arm)!=owner) return {};
    return result;
}
inline bool same_weapon_source(const NativeWeaponSourceContent& a,const NativeWeaponSourceContent& b) {
    return a.arm==b.arm && a.game==b.game && a.frame==b.frame && a.source==b.source && a.remap==b.remap &&
        !std::memcmp(a.lists.data(),b.lists.data(),sizeof(a.lists)) && !std::memcmp(a.joints.data(),b.joints.data(),sizeof(a.joints));
}
struct NativeWeaponSourceMatch {
    UINT64 receipt{},epoch{};std::uint32_t native_frame{},receipt_frame{},producer_thread{};
    std::uintptr_t game{},receipt_source{},receipt_remap{};std::uint32_t receipt_transform_count{};
    std::uintptr_t producer_caller_rva{};int producer_stage{};
    bool matched{};unsigned reason{}; // 1=no receipt,2=state,3=frame,4=identity/content,5=publication/overlap.
    NativeWeaponModelBase receipt_model{},model{};
    rig::ArmTrackingStamp receipt_tracking{},tracking{};
    bool model_matched{},tracking_matched{}; // Independent diagnostics; never changes source qualification.
};
struct NativeWeaponSourceCapture {NativeWeaponSourceContent content{};NativeWeaponSourceMatch match{};};
// Passive exact-caller stage3 receipts. Phase ownership requires separate live proof.
class NativeWeaponSourceReceipts {
    using Original=void (__fastcall*)(void*,int);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);using Logic=UINT (*)();
    using Tracking=rig::ArmTrackingStamp (*)();
    struct Publication {std::shared_ptr<const rig::NativeArmBindingSnapshot> arms;UINT64 epoch{},tick{};unsigned hands{};};
    struct Receipt {NativeWeaponSourceContent content{};UINT64 serial{},epoch{};DWORD thread{};std::uintptr_t caller_rva{};int stage{};
        NativeWeaponModelBase model{};rig::ArmTrackingStamp tracking{};};
    inline static NativeWeaponSourceReceipts* instance_{};
    inline static constexpr std::array<unsigned char,32> expected{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x48,0x83,0xec,0x20,0x48,0x8b,0x41,0x38,0x85,0xd2,0x4c,0x63,0xea,0x48,0x8b,0x80,0xe8,0x1f,0,0};
    inline static constexpr std::array<std::uintptr_t,7> callers{0x540e8f,0x5411e7,0x54ebca,0x54f59d,0x5508f8,0x5509dc,0x5509f6};
    inline static constexpr std::array<int,7> caller_stages{0,3,-1,0,3,1,2};
    Original original_{};Read read_{};Logic logic_{};std::uintptr_t base_{};
    Tracking tracking_{};
    std::atomic<bool> active_{false};std::atomic<UINT> depth_{};
    std::atomic<UINT64> calls_{},receipts_count_{},matches_{},unmatched_{},overlaps_{},noncanonical_{},errors_{};
    std::array<std::atomic<UINT64>,5> stage_calls_{},owned_stage_calls_{};
    std::array<std::atomic<UINT64>,8> caller_calls_{},owned_caller_calls_{};
    std::atomic<std::uintptr_t> last_owned_caller_{};std::atomic<int> last_owned_stage_{};
    std::mutex mutex_;Publication publication_{};std::array<Receipt,2> receipts_{};
    std::array<unsigned,2> producers_{};std::array<bool,2> overlap_{};UINT64 serial_{};
    struct Depth {NativeWeaponSourceReceipts& self;explicit Depth(NativeWeaponSourceReceipts& s):self(s) {++self.depth_;}~Depth(){--self.depth_;}};
    friend struct NativeWeaponSourceReceiptsCheck;
    static void __fastcall update(void* animation,int stage) {instance_->invoke(animation,stage,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));}
    void invoke(void* animation,int stage,std::uintptr_t caller) {
        Depth depth(*this);++calls_;Publication publication{};unsigned side=2;
        const unsigned stage_index=stage>=0 && stage<4?static_cast<unsigned>(stage):4u;
        unsigned caller_index=7;for(unsigned i=0;i<callers.size();++i) if(caller==base_+callers[i]) caller_index=i;
        ++stage_calls_[stage_index];++caller_calls_[caller_index];bool qualify=false;
        std::optional<std::pair<std::uintptr_t,std::uint32_t>> owner;
        try {
            if(active_.load(std::memory_order_acquire)) {
                {std::lock_guard<std::mutex> lock(mutex_);publication=publication_;}
                const auto now=GetTickCount64();
                if(publication.arms && publication.epoch && now>=publication.tick && now-publication.tick<=250 && logic_()==2) {
                    for(unsigned i=0;i<2;++i) if(publication.arms->arms[i].animation==reinterpret_cast<std::uintptr_t>(animation)) side=i;
                    if(side<2 && (publication.hands&(1u<<side))) owner=weapon_source_owner(read_,base_,publication.arms->arms[side]);
                }
            }
            if(owner) {
                std::lock_guard<std::mutex> lock(mutex_);receipts_[side]={};
                ++owned_stage_calls_[stage_index];++owned_caller_calls_[caller_index];last_owned_caller_=caller-base_;last_owned_stage_=stage;
                const bool known=caller_index<7 && stage_index<4 &&
                    (caller_stages[caller_index]==-1 || caller_stages[caller_index]==stage);
                if(!known) ++noncanonical_;
                qualify=known && stage==3;
                if(++producers_[side]>1) {overlap_[side]=true;++overlaps_;}
            }
        } catch(...) {++errors_;owner.reset();}
        struct Finish {
            NativeWeaponSourceReceipts& self;unsigned side;bool armed;
            ~Finish() {if(armed) {std::lock_guard<std::mutex> lock(self.mutex_);if(--self.producers_[side]==0) self.overlap_[side]=false;}}
        } finish{*this,side,owner.has_value()};
        original_(animation,stage); // Exactly once, preserving both native arguments/exceptions.
        if(!owner || !qualify) return;
        try {
            const auto content=weapon_source_content(read_,base_,publication.arms->arms[side]);
            const auto model=weapon_model_base(read_,base_,publication.arms->arms[side]);
            const auto tracking=tracking_?tracking_():rig::ArmTrackingStamp{};
            std::lock_guard<std::mutex> lock(mutex_);
            const auto now=GetTickCount64();
            if(!content || content->game!=owner->first || content->frame!=owner->second || producers_[side]!=1 || overlap_[side] ||
                !active_.load() || publication_.arms!=publication.arms || publication_.epoch!=publication.epoch ||
                !(publication_.hands&(1u<<side)) || now<publication_.tick || now-publication_.tick>250 || logic_()!=2) return;
            receipts_[side]={*content,++serial_,publication.epoch,GetCurrentThreadId(),caller-base_,stage,model,tracking};++receipts_count_;
        } catch(...) {++errors_;}
    }
public:
    void tracking_reader(Tracking tracking) {tracking_=tracking;} // Configure only before installing/starting callbacks.
    rig::ArmTrackingStamp tracking_stamp() const {return tracking_?tracking_():rig::ArmTrackingStamp{};}
    void publish(std::shared_ptr<const rig::NativeArmBindingSnapshot> arms,UINT64 epoch,UINT64 tick,unsigned hands,bool eligible) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!eligible || !arms || !rig::native_arm_pair_owned(arms->arms) || !epoch) {publication_={};receipts_={};return;}
        if(publication_.arms!=arms || publication_.epoch!=epoch) receipts_={};
        publication_={std::move(arms),epoch,tick,hands&3u};
    }
    void clear() noexcept {try {std::lock_guard<std::mutex> lock(mutex_);publication_={};receipts_={};} catch(...) {active_=false;}}
    NativeWeaponSourceMatch match_current(const rig::Arm& arm) noexcept {
        std::uintptr_t source{};
        if(arm.side<2) {std::lock_guard<std::mutex> lock(mutex_);source=receipts_[arm.side].content.source;}
        return match(arm,source);
    }
    std::optional<NativeWeaponSourceCapture> capture_current(const rig::Arm& arm) noexcept {
        try {
            auto match=match_current(arm);if(!match.matched || arm.side>1) return {};
            std::lock_guard<std::mutex> lock(mutex_);
            if(!active_.load() || producers_[arm.side] || receipts_[arm.side].serial!=match.receipt) return {};
            return NativeWeaponSourceCapture{receipts_[arm.side].content,match};
        } catch(...) {++errors_;return {};}
    }
    NativeWeaponSourceMatch match(const rig::Arm& arm,std::uintptr_t source,unsigned joint=69) noexcept {
        NativeWeaponSourceMatch result{};result.reason=1;
        try {
            if(arm.side>1 || !active_.load()) {++unmatched_;return result;}
            result.model=weapon_model_base(read_,base_,arm);
            result.native_frame=result.model.frame;
            result.tracking=tracking_?tracking_():rig::ArmTrackingStamp{};
            Receipt receipt{};Publication publication{};
            {std::lock_guard<std::mutex> lock(mutex_);receipt=receipts_[arm.side];publication=publication_;}
            result.receipt=receipt.serial;result.epoch=receipt.epoch;result.receipt_frame=receipt.content.frame;result.producer_thread=receipt.thread;
            result.game=receipt.content.game;result.receipt_source=receipt.content.source;
            result.receipt_remap=receipt.content.lists[2].data;result.receipt_transform_count=receipt.content.lists[0].packed_count&0xffffff;
            result.producer_caller_rva=receipt.caller_rva;result.producer_stage=receipt.stage;
            result.receipt_model=receipt.model;result.receipt_tracking=receipt.tracking;
            if(!receipt.serial) {++unmatched_;return result;}
            const auto content=weapon_source_content(read_,base_,arm);
            if(!content) result.reason=2;
            else {
                result.native_frame=content->frame;
                if(content->frame!=receipt.content.frame) result.reason=3;
                else if(!same_weapon_source(*content,receipt.content) ||
                    source!=content->source+(joint<69?static_cast<std::uintptr_t>(joint)*64:0)) result.reason=4;
                else {
                    std::lock_guard<std::mutex> lock(mutex_);const auto now=GetTickCount64();
                    if(!active_.load() || producers_[arm.side] || receipts_[arm.side].serial!=receipt.serial ||
                        publication_.arms!=publication.arms || publication_.epoch!=receipt.epoch || !publication_.arms ||
                        !(publication_.hands&(1u<<arm.side)) || now<publication_.tick || now-publication_.tick>250 || logic_()!=2) result.reason=5;
                    else if(weapon_source_owner(read_,base_,arm)!=std::optional<std::pair<std::uintptr_t,std::uint32_t>>({content->game,content->frame})) result.reason=2;
                    else {result.matched=true;result.reason=0;}
                }
            }
            result.model_matched=result.matched && same_weapon_model_base(receipt.model,result.model);
            const auto tracking_now=GetTickCount64();
            result.tracking_matched=result.matched && receipt.tracking.revision && receipt.tracking.epoch &&
                (receipt.tracking.hands&(1u<<arm.side)) && tracking_now>=receipt.tracking.tick && tracking_now-receipt.tracking.tick<=250 &&
                receipt.tracking.revision==result.tracking.revision;
        } catch(...) {++errors_;result.reason=2;}
        if(result.matched) ++matches_;else ++unmatched_;return result;
    }
    void install(std::uintptr_t base,Read read,Logic logic) {
        if(instance_ && instance_!=this) throw std::runtime_error("Another source receipt observer owns this DLL");
        if(!read || !logic || (original_ && base_!=base)) throw std::invalid_argument("Invalid source receipt owner/readers");
        base_=base;read_=read;logic_=logic;
        std::array<unsigned char,32> bytes{};
        if(!read(base+0x540a90,bytes.data(),bytes.size()) || bytes!=expected) throw std::runtime_error("Source receipt prefix differs");
        for(const auto caller:callers) {
            std::array<unsigned char,5> call{};std::int32_t displacement{};
            if(!read(base+caller-5,call.data(),call.size()) || call[0]!=0xe8) throw std::runtime_error("Source receipt caller differs");
            std::memcpy(&displacement,call.data()+1,4);
            if(base+caller+displacement!=base+0x540a90) throw std::runtime_error("Source receipt target differs");
        }
        const auto getter=rig::value<std::uintptr_t>(read,base+0x1c940a8+0xc0),domain=rig::value<std::uintptr_t>(read,base+0x1c940a8+0xb8);
        if(getter!=base+0x8a89a0 || domain!=base+0x8a8a20) throw std::runtime_error("Source receipt game getters differ");
        const std::array<unsigned char,12> counter{0x48,0x81,0xc1,0x28,0x74,0x2e,0,0xe9,0x14,0x78,0x7c,0};
        const std::array<unsigned char,8> address{0x48,0x8d,0x81,0x28,0x74,0x2e,0,0xc3};
        std::array<unsigned char,12> check{};std::array<unsigned char,8> check_address{};
        if(!read(base+0x8a89a0,check.data(),check.size()) || check!=counter ||
            !read(base+0x8a8a20,check_address.data(),check_address.size()) || check_address!=address)
            throw std::runtime_error("Source receipt getter instructions differ");
        std::array<unsigned char,3> value_code{};
        if(!read(base+0x10701c0,value_code.data(),value_code.size()) || value_code!=std::array<unsigned char,3>{0x8b,0x01,0xc3})
            throw std::runtime_error("Source receipt counter read differs");
        clear();calls_=receipts_count_=matches_=unmatched_=overlaps_=noncanonical_=errors_=0;instance_=this;
        for(auto& count:stage_calls_) count=0;for(auto& count:owned_stage_calls_) count=0;
        for(auto& count:caller_calls_) count=0;for(auto& count:owned_caller_calls_) count=0;
        last_owned_caller_=0;last_owned_stage_=0;
        if(!original_ && MH_CreateHook(reinterpret_cast<void*>(base+0x540a90),reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original_))!=MH_OK)
            throw std::runtime_error("Cannot create source receipt hook");
        if(MH_EnableHook(reinterpret_cast<void*>(base+0x540a90))!=MH_OK) throw std::runtime_error("Cannot enable source receipt hook");
        active_.store(true,std::memory_order_release);
    }
    bool disable() noexcept {
        active_=false;clear();if(!original_) return true;
        const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+0x540a90));
        bool okay=status==MH_OK || status==MH_ERROR_DISABLED;
        const auto deadline=GetTickCount64()+1000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        std::array<unsigned char,32> bytes{};
        return okay && !depth_ && read_ && read_(base_+0x540a90,bytes.data(),bytes.size()) && bytes==expected;
    }
    UINT64 calls() const{return calls_.load();}UINT64 receipts() const{return receipts_count_.load();}
    UINT64 matches() const{return matches_.load();}UINT64 unmatched() const{return unmatched_.load();}
    UINT64 overlaps() const{return overlaps_.load();}UINT64 noncanonical() const{return noncanonical_.load();}UINT64 errors() const{return errors_.load();}
    UINT64 stage_calls(unsigned stage,bool owned=false) const {return stage<5?(owned?owned_stage_calls_[stage].load():stage_calls_[stage].load()):0;}
    UINT64 caller_calls(unsigned caller,bool owned=false) const{return caller<8?(owned?owned_caller_calls_[caller].load():caller_calls_[caller].load()):0;}
    std::uintptr_t last_owned_caller() const{return last_owned_caller_.load();}int last_owned_stage() const{return last_owned_stage_.load();}
};
} // namespace d2vr
