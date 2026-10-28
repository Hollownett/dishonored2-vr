#pragma once
#include "native_world_ui.hpp"
#include <mutex>
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
    struct Pending {
        Receipt receipt{};
        std::uintptr_t caller{},method{},receiver{},block{};
        std::thread::id thread{};
    };
    std::mutex mutex_;
    std::optional<Pending> pending_;
    std::optional<Receipt> note_;
    std::uint64_t generation_{};
    std::uintptr_t base_{};
    bool verified_{};
    static bool same_owner(const NativeMenuState& a,const NativeMenuState& b) noexcept {
        return a.root==b.root && a.manager==b.manager && a.wrapper==b.wrapper && a.file==b.file && a.player==b.player;
    }
    template<class Read> static std::optional<NativeWorldUiOwner> call_owner(Read& read,std::uintptr_t base,
            std::uintptr_t caller,std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block) {
        const auto iggy=rig::value<std::uintptr_t>(read,method),name=rig::value<std::uintptr_t>(read,method+8);
        const auto count=rig::value<std::uint32_t>(read,block+0x14),capacity=rig::value<std::uint32_t>(read,block+0x10);
        const unsigned argc=caller==0x790755?0u:5u;
        if(!iggy || !rig::pointer(*iggy) || !name || !*name || !count || *count!=argc || !capacity || *capacity<argc || *capacity>16 ||
           rig::value<std::uintptr_t>(read,receiver+8)!=iggy) return {};
        const auto owner=native_world_ui_owner(read,base,*iggy);
        if(!owner || owner->kind!=WorldUiKind::notes) return {};
        // 28ee0 seeds one root record from *[file+1780]; 2a000 selects
        // table[(low24(index)-1)*28]. Both callsites pass this root unchanged.
        const auto table=rig::value<std::uintptr_t>(read,receiver+0x18),root=rig::value<std::uintptr_t>(read,owner->menu.file+0x1780);
        const auto index=rig::value<std::uint32_t>(read,receiver+0x20);
        std::array<unsigned char,0x28> actual{},expected{};
        if(!table || !rig::pointer(*table) || !root || !rig::pointer(*root) || !index || (*index&0xffffffu)!=1 ||
           !read(*table,actual.data(),actual.size()) || !read(*root,expected.data(),expected.size()) || actual!=expected) return {};
        return owner;
    }
public:
    // Caller must clear on VR/focus/view lifetime changes, including every
    // exit from logic16. epoch is that lifetime, not a render frame counter.
    void clear() {std::lock_guard<std::mutex> lock(mutex_);++generation_;pending_.reset();note_.reset();}
    template<class Read> bool configure(Read& read,std::uintptr_t base) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;pending_.reset();note_.reset();verified_=false;base_=base;
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
            if(!read(base+region.rva,bytes.data(),region.size))return false;
            std::uint64_t hash=14695981039346656037ull;
            for(std::size_t i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=region.hash)return false;
        }
        verified_=true;return true;
    }
    template<class Read> void observe(Read& read,std::uintptr_t base,bool before,std::uintptr_t caller,
            std::uintptr_t method,std::uintptr_t receiver,std::uintptr_t block,std::uint32_t result,std::uint64_t epoch) {
        if(caller!=0x790755 && caller!=0x790574)return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(before) {++generation_;pending_.reset();note_.reset();}
        if(!verified_ || base!=base_ || !epoch)return;
        const auto owner=call_owner(read,base,caller,method,receiver,block);
        if(!owner) {pending_.reset();note_.reset();return;}
        const auto iggy=rig::value<std::uintptr_t>(read,method).value_or(0);
        if(before) {
            if(caller==0x790755)pending_=Pending{{owner->menu,iggy,epoch,generation_},caller,method,receiver,block,std::this_thread::get_id()};
            return;
        }
        if(caller==0x790755 && result==1 && pending_ && pending_->thread==std::this_thread::get_id() &&
           pending_->caller==caller && pending_->method==method && pending_->receiver==receiver && pending_->block==block &&
           pending_->receipt.epoch==epoch && pending_->receipt.generation==generation_ && pending_->receipt.iggy==iggy &&
           same_owner(pending_->receipt.owner,owner->menu))note_=pending_->receipt;
        else note_.reset();
        pending_.reset();
    }
    // Either overload revalidates the stored owner before exposing its show
    // generation. Consumers must carry that generation in retained page poses.
    template<class Read> std::optional<Receipt> current(Read& read,std::uintptr_t base,std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!verified_ || base!=base_ || !note_ || !epoch || note_->epoch!=epoch)return {};
        const auto owner=native_world_ui_owner(read,base,note_->iggy);
        if(!owner || owner->kind!=WorldUiKind::notes || !same_owner(note_->owner,owner->menu)) {note_.reset();return {};}
        return note_;
    }
    template<class Read> std::optional<Receipt> current(Read& read,std::uintptr_t base,
            std::uintptr_t file,std::uintptr_t iggy,std::uint64_t epoch) {
        const auto receipt=current(read,base,epoch);
        return receipt && receipt->owner.file==file && receipt->iggy==iggy?receipt:std::nullopt;
    }
};
}
