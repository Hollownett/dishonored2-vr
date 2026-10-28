#pragma once
#include "native_world_ui.hpp"
#include <mutex>
#include <chrono>
#include <string>
#include <thread>

namespace d2vr {
// Only the pinned native ShowNote/ShowMap calls identify this shared movie's
// subtype. Never infer a SimpleNoteScreen from logic16 or ui/notes.iggy alone.
class NativeNoteKind {
public:
    struct Receipt {
        NativeMenuState owner{};
        std::uintptr_t iggy{};
        std::uint64_t epoch{},generation{};
    };
private:
    using RootRecord=std::array<unsigned char,0x28>;
    struct CallIdentity {
        std::uintptr_t iggy{},name{},table{};
        RootRecord root{};
    };
    struct Pending {
        Receipt receipt{};
        std::uintptr_t caller{},method{},receiver{},block{};
        std::thread::id thread{};
        CallIdentity identity{};
    };
    std::mutex mutex_;
    std::optional<Pending> pending_;
    std::optional<Receipt> note_;
    std::uint64_t generation_{};
    RootRecord note_root_{};
    std::uint64_t unbound_since_{};
    const char* rejection_{"none"};
    static std::uint64_t now_ms() {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    std::uintptr_t base_{};
    bool verified_{};
    bool configured_{};
    std::uint64_t before_calls_{},after_calls_{},accepted_shows_{},call_owner_rejections_{},current_owner_losses_{},clears_{};
    std::uintptr_t signature_failure_rva_{};
    static bool same_owner(const NativeMenuState& a,const NativeMenuState& b) noexcept {
        return a.root==b.root && a.manager==b.manager && a.wrapper==b.wrapper && a.file==b.file && a.player==b.player;
    }
    template<class Read> std::optional<CallIdentity> call_identity(Read& read,
            std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block) {
        const auto iggy=rig::value<std::uintptr_t>(read,method),name=rig::value<std::uintptr_t>(read,method+8);
        const auto count=rig::value<std::uint32_t>(read,block+0x14),capacity=rig::value<std::uint32_t>(read,block+0x10);
        const unsigned argc=caller==0x790755?0u:5u;
        rejection_="call_identity";
        if(!iggy || !rig::pointer(*iggy) || !name || !*name ||
           rig::value<std::uintptr_t>(read,receiver+8)!=iggy) return {};
        rejection_="call_arguments";
        if(!count || *count!=argc || !capacity || *capacity<argc || *capacity>16) return {};
        // 28ee0/29300 copy all five qwords from *[file+1780]. Keep the
        // synchronous call identity without requiring the movie to be drawable yet.
        const auto table=rig::value<std::uintptr_t>(read,receiver+0x18);
        const auto index=rig::value<std::uint32_t>(read,receiver+0x20);
        rejection_="call_root";
        CallIdentity identity{*iggy,*name,table.value_or(0),{}};
        if(!table || !rig::pointer(*table) || !index || (*index&0xffffffu)!=1 ||
           !read(*table,identity.root.data(),identity.root.size())) return {};
        rejection_="none";return identity;
    }
    template<class Read> static bool root_matches(Read& read,const NativeWorldUiOwner& owner,const RootRecord& root_record) {
        const auto root=rig::value<std::uintptr_t>(read,owner.menu.file+0x1780);
        RootRecord expected{};
        return root && rig::pointer(*root) && read(*root,expected.data(),expected.size()) && expected==root_record;
    }

public:
    // Caller must clear on VR/focus/view lifetime changes, including every
    // exit from logic16. epoch is that lifetime, not a render frame counter.
    void clear() {std::lock_guard<std::mutex> lock(mutex_);++clears_;++generation_;pending_.reset();note_.reset();}
    std::string status_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::string("{\"configured\":")+(configured_?"true":"false")+",\"verified\":"+(verified_?"true":"false")+
            ",\"before_calls\":"+std::to_string(before_calls_)+",\"after_calls\":"+std::to_string(after_calls_)+
            ",\"accepted_shows\":"+std::to_string(accepted_shows_)+",\"call_owner_rejections\":"+std::to_string(call_owner_rejections_)+
            ",\"current_owner_losses\":"+std::to_string(current_owner_losses_)+",\"clears\":"+std::to_string(clears_)+
            ",\"signature_failure_rva\":"+std::to_string(signature_failure_rva_)+",\"pending\":"+(pending_?"true":"false")+
            ",\"receipt\":"+(note_?"true":"false")+",\"generation\":"+std::to_string(generation_)+",\"bound\":"+(note_ && note_->owner.file?"true":"false")+",\"rejection\":\""+rejection_+"\"}";
    }
    template<class Read> bool configure(Read& read,std::uintptr_t base) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;pending_.reset();note_.reset();verified_=false;base_=base;configured_=true;
        before_calls_=after_calls_=accepted_shows_=call_owner_rejections_=current_owner_losses_=clears_=0;signature_failure_rva_=0;rejection_="none";
        struct Region {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
        // Steam17576867 SHA256 b671f824...6180. Full ShowMap/ShowNote
        // bodies include the literal method binding and root construction.
        constexpr Region regions[]{
            {0x790380,594,0xc43e4cabde100658ull},{0x7905e0,471,0xb2ffbdff554301c2ull},
            {0x28ee0,149,0x02592036c555a03cull},{0x28a50,60,0x9e1efe69f38ef372ull},
            {0x2a000,23,0x26bb067670f66dd5ull},{0x2bfe0,8,0x82c92ee3e70045d7ull}};
        if(!base)return false;
        for(const auto& region:regions) {
            std::array<unsigned char,594> bytes{};
            if(!read(base+region.rva,bytes.data(),region.size)) {signature_failure_rva_=region.rva;return false;}
            std::uint64_t hash=14695981039346656037ull;
            for(std::size_t i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=region.hash) {signature_failure_rva_=region.rva;return false;}
        }
        verified_=true;return true;
    }
    template<class Read> void observe(Read& read,std::uintptr_t base,bool before,std::uintptr_t caller,
            std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block,std::uint32_t result,std::uint64_t epoch,
            std::uint64_t tick=now_ms()) {
        if(caller!=0x790755 && caller!=0x790574)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(before)++before_calls_;else ++after_calls_;
        if(before) {++generation_;pending_.reset();note_.reset();}
        if(!verified_ || base!=base_ || !epoch)return;
        const auto identity=call_identity(read,caller,method,receiver,block);
        if(!identity) {++call_owner_rejections_;pending_.reset();note_.reset();return;}
        const auto owner=native_world_ui_owner(read,base,identity->iggy);
        if(owner && (owner->kind!=WorldUiKind::notes || !root_matches(read,*owner,identity->root))) {
            rejection_=owner->kind!=WorldUiKind::notes?"owner_kind":"owner_root";++call_owner_rejections_;pending_.reset();note_.reset();return;
        }
        if(before) {
            pending_=Pending{{owner?owner->menu:NativeMenuState{},identity->iggy,epoch,generation_},
                caller,method,receiver,block,std::this_thread::get_id(),*identity};
            return;
        }
        if(result==1 && pending_ && pending_->thread==std::this_thread::get_id() &&
           pending_->caller==caller && pending_->method==method && pending_->receiver==receiver && pending_->block==block &&
           pending_->receipt.epoch==epoch && pending_->receipt.generation==generation_ && pending_->identity.iggy==identity->iggy &&
           pending_->identity.name==identity->name && pending_->identity.table==identity->table && pending_->identity.root==identity->root &&
           (!pending_->receipt.owner.file || (owner && same_owner(pending_->receipt.owner,owner->menu)))) {
            note_=pending_->receipt;note_root_=identity->root;unbound_since_=tick;
            if(owner)note_->owner=owner->menu;
            ++accepted_shows_;rejection_="none";
        } else {
            note_.reset();rejection_="call_pair";
            if(result!=1)rejection_="native_result";
            else if(pending_) {
                if(pending_->identity.iggy!=identity->iggy || pending_->identity.name!=identity->name || pending_->identity.table!=identity->table)
                    rejection_="call_identity_changed";
                else if(pending_->identity.root!=identity->root)rejection_="call_root_changed";
                else if(pending_->receipt.owner.file && (!owner || !same_owner(pending_->receipt.owner,owner->menu)))rejection_="call_owner_changed";
            }
        }
        pending_.reset();
    }
    // Capture provenance only: an observed event is not permission to attach a
    // hand or submit a panel. Those consumers must obtain current() ownership.
    std::optional<std::uint64_t> observed_generation(std::uint64_t epoch,std::uint64_t tick=now_ms()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!verified_ || !note_ || !epoch || note_->epoch!=epoch ||
           (!note_->owner.file && (tick<unbound_since_ || tick-unbound_since_>2000))) return {};
        return note_->generation;
    }
    // ShowNote can precede drawable registration. Retain only its copied native
    // identity for at most two seconds; expose no receipt until ownership joins.
    // Once bound, any ownership/root loss invalidates the show permanently.
    template<class Read> std::optional<Receipt> current(Read& read,std::uintptr_t base,std::uint64_t epoch,
            std::uint64_t tick=now_ms()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!verified_ || base!=base_ || !note_ || !epoch || note_->epoch!=epoch)return {};
        const bool bound=note_->owner.file!=0;
        if(!bound && (tick<unbound_since_ || tick-unbound_since_>2000)) {
            rejection_="owner_timeout";note_.reset();return {};
        }
        const auto owner=native_world_ui_owner(read,base,note_->iggy);
        if(!owner) {
            rejection_="owner_unavailable";
            if(bound) {++current_owner_losses_;note_.reset();}
            return {};
        }
        const bool same_root=root_matches(read,*owner,note_root_);
        if(owner->kind!=WorldUiKind::notes || !same_root || (bound && !same_owner(note_->owner,owner->menu))) {
            rejection_=owner->kind!=WorldUiKind::notes?"owner_kind":!same_root?"owner_root":"owner_changed";
            ++current_owner_losses_;note_.reset();return {};
        }
        note_->owner=owner->menu;rejection_="none";return note_;
    }
    template<class Read> std::optional<Receipt> current(Read& read,std::uintptr_t base,
            std::uintptr_t file,std::uintptr_t iggy,std::uint64_t epoch) {
        const auto receipt=current(read,base,epoch);
        return receipt && receipt->owner.file==file && receipt->iggy==iggy?receipt:std::nullopt;
    }
};
}
