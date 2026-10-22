#pragma once
#include "alternate_taa_state.hpp"
#include <atomic>
#include <cstring>
#include <mutex>

namespace d2vr::alternate_taa {
using Read=bool (*)(std::uintptr_t,void*,std::size_t);
using Write=bool (*)(std::uintptr_t,const void*,std::size_t);
// Exact instruction regions independently verified in Steam17576867, SHA256 b671f824...6180. See ABI evidence report.
inline bool verified_abi(Read read,std::uintptr_t base) {
    struct Region {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
    static constexpr Region regions[]{
    // Begin past our Setup detour's patched prologue; the old-position loads and descriptor copy remain pinned.
    {0x374e61,111,0x620885b5b4c9ad51ull},
    {0x375447,662,0x368ca4de159f0d57ull},
    {0x375a98,17,0xefed16af6f4dfcb7ull},
    {0x375ace,22,0x4f4d44b2da077af1ull},
    {0x375b87,149,0xc082ab5a71f586b6ull},
    {0x3760a6,428,0xbf3ec9f7f048d570ull},
    {0x3742b0,120,0x5548a7f8d7a0cdcfull},
    };
    for(const auto& region:regions) {
        std::array<unsigned char,1024> bytes{};
        if(region.size>bytes.size() || !read(base+region.rva,bytes.data(),region.size)) return false;
        std::uint64_t hash=14695981039346656037ull;
        for(std::size_t i=0;i<region.size;++i) hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash) return false;
    }
    return true;
}
inline bool read_snapshot(Read read,std::uintptr_t view,Snapshot& value) {
    for(std::size_t i=0;i<3;++i) if(!read(view+current_matrix_offsets[i],value.matrices[i].data(),64)) return false;
    return read(view+position_offset,value.position.data(),12) && read(view+jitter_offset,value.jitter.data(),8) && finite(value);
}
inline bool same_snapshot(const Snapshot& a,const Snapshot& b) noexcept {
    return a.matrices==b.matrices && a.position==b.position && a.jitter==b.jitter;
}
// Setup history is render-thread-owned. Only immutable Begin-admitted receipts cross to post-process workers.
class Runtime {
    History history_;ReceiptBook book_;std::mutex book_mutex_;
    Read read_{};Write write_{};
    // Only invalidate() and receipt lookup touch these from non-Setup threads.
    std::atomic<std::uint64_t> invalidation_{0};
    std::uint64_t owner_generation_{};
    std::optional<Prepared> pending_;Snapshot saved_{};
    std::uint32_t saved_stamp_{};unsigned char saved_reset_{};bool seeded_{};
    bool transaction_valid() const noexcept {return owner_generation_==invalidation_.load();}
    void consume_invalidation() {
        const auto generation=invalidation_.load();
        if(owner_generation_!=generation) {
            history_.reset();pending_.reset();seeded_=false;owner_generation_=generation;
        }
    }
    bool write_current(std::uintptr_t view,const Snapshot& value,bool matrices) {
        if(matrices) {
            for(std::size_t i=0;i<3;++i) if(!write_(view+current_matrix_offsets[i],value.matrices[i].data(),64)) return false;
            if(!write_(view+position_offset,value.position.data(),12)) return false;
        }
        return write_(view+jitter_offset,value.jitter.data(),8);
    }
public:
    std::atomic<bool> failed{false};
    std::atomic<std::uintptr_t> single_view{0};
    std::atomic<std::uint64_t> completed{0},admitted{0},rejected{0},resets{0};
    // Configure, clear, retire, plan, seed, finish and abort are Setup-owner-only.
    void configure(Read read,Write write) {
        read_=read;write_=write;failed=false;completed=admitted=rejected=resets=0;
        clear();single_view=0;
    }
    // Recovery/Present handoff: immediately revoke receipts, never mutate the live Setup transaction.
    void invalidate() {
        std::lock_guard<std::mutex> lock(book_mutex_);
        ++invalidation_;book_.reset();++resets;
    }
    void clear() {
        invalidate();history_.reset();pending_.reset();seeded_=false;
        owner_generation_=invalidation_.load();
    }
    // The owner calls this only after intentionally selecting the paired path.
    void retire() {cancel_plan();clear();single_view=0;}
    void fail(std::uintptr_t view) noexcept {single_view=view;failed=true;}
    std::optional<std::uint32_t> plan(const Key& key) {
        if(seeded_) {abort();failed=true;return std::nullopt;}
        consume_invalidation();pending_.reset();
        if(failed || !read_ || !write_ || !read_snapshot(read_,key.view,saved_) ||
            !read_(key.view+stamp_offset,&saved_stamp_,4) || !read_(key.view+reset_offset,&saved_reset_,1)) return std::nullopt;
        const auto generation=history_.generation();
        pending_=history_.prepare(key,saved_reset_!=0);
        if(!pending_) return std::nullopt;
        if(history_.generation()!=generation) {std::lock_guard<std::mutex> lock(book_mutex_);book_.reset();++resets;}
        return pending_->phase;
    }
    void cancel_plan() {if(seeded_) {abort();failed=true;} else pending_.reset();}
    bool planned() const noexcept {return pending_.has_value();}
    bool needs_reset() const noexcept {return pending_ && pending_->reset && !pending_->duplicate;}
    bool seed(std::uintptr_t view) {
        if(!pending_ || pending_->key.view!=view || failed) return false;
        single_view=view;
        if(!transaction_valid()) {abort();failed=true;return false;}
        const auto& p=*pending_;
        if(p.has_previous && !p.duplicate && saved_stamp_==p.key.native_counter) {fail(view);return false;}
        seeded_=true;single_view=view;
        if(p.has_previous && !write_current(view,p.previous,saved_stamp_!=p.key.native_counter)) {abort();failed=true;return false;}
        if(p.reset) {const unsigned char one=1;if(!write_(view+reset_offset,&one,1)) {abort();failed=true;return false;}}
        if(!transaction_valid()) {abort();failed=true;return false;}
        return true;
    }
    void finish(std::uintptr_t view) {
        if(!seeded_ || !pending_ || pending_->key.view!=view) {cancel_plan();return;}
        if(!transaction_valid()) {abort();failed=true;return;}
        const auto p=*pending_;Snapshot current{};std::uint32_t stamp{};
        if(!read_snapshot(read_,view,current) || !read_(view+stamp_offset,&stamp,4) || stamp!=p.key.native_counter) {abort();failed=true;return;}
        // Native reset copies current matrices/position, but does not reset jitter. Keep its own initialization coherent.
        if(p.reset && !write_(view+previous_jitter_offset,current.jitter.data(),8)) {abort();failed=true;return;}
        const auto& expected=p.has_previous?p.previous:current;
        for(std::size_t i=0;i<3;++i) {
            std::array<float,16> previous{};
            if(!read_(view+previous_matrix_offsets[i],previous.data(),64) || previous!=expected.matrices[i]) {abort();failed=true;return;}
        }
        std::array<float,3> previous_position{};std::array<float,2> previous_jitter{};
        if(!read_(view+previous_position_offset,previous_position.data(),12) || previous_position!=expected.position ||
            !read_(view+previous_jitter_offset,previous_jitter.data(),8) || previous_jitter!=expected.jitter) {abort();failed=true;return;}
        const auto receipt=history_.complete(p,current);
        bool published=false;
        {std::lock_guard<std::mutex> lock(book_mutex_);published=transaction_valid() && receipt && book_.publish(*receipt);}
        if(!published) {++rejected;abort();failed=true;return;}
        ++completed;seeded_=false;pending_.reset();
    }
    void abort() {
        if(seeded_ && pending_) {
            std::uint32_t stamp{};const auto view=pending_->key.view;
            if(read_(view+stamp_offset,&stamp,4) && stamp==saved_stamp_) {
                if(!write_current(view,saved_,true) || !write_(view+reset_offset,&saved_reset_,1)) failed=true;
            }
        }
        clear(); // Preserve classification throughout rollback, including already queued worker passes.
    }
    bool begin(std::uintptr_t view,std::uint64_t display,std::uint64_t pose,std::uint64_t reference) {
        const auto generation=invalidation_.load();std::uint32_t stamp{};
        if(failed || !read_ || !read_(view+stamp_offset,&stamp,4)) return false;
        std::lock_guard<std::mutex> lock(book_mutex_);
        if(generation==invalidation_.load() && book_.admit(view,stamp,display,pose,reference)) {++admitted;return true;}
        ++rejected;return false;
    }
    std::optional<Receipt> resolve(std::uintptr_t view) {
        const auto generation=invalidation_.load();std::uint32_t before{},after{};Snapshot current{};
        if(failed || !read_ || !read_(view+stamp_offset,&before,4) || !read_snapshot(read_,view,current) ||
            !read_(view+stamp_offset,&after,4) || before!=after) return std::nullopt;
        std::lock_guard<std::mutex> lock(book_mutex_);
        if(generation!=invalidation_.load()) {++rejected;return std::nullopt;}
        const auto receipt=book_.resolve(view,before);
        if(receipt && same_snapshot(receipt->current,current)) return receipt;
        ++rejected;return std::nullopt;
    }
};
} // namespace d2vr::alternate_taa
