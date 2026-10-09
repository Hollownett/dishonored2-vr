#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace d2vr {
// Passive evidence policy only. This class never reads native memory or calls an engine API.
struct HeadQueryOwner {
    std::uintptr_t player{}, manager{}, world{}, shape{};
    std::uint64_t owner_generation{}, reference_epoch{};
    bool operator==(const HeadQueryOwner& b) const noexcept {
        return player==b.player && manager==b.manager && world==b.world && shape==b.shape &&
            owner_generation==b.owner_generation && reference_epoch==b.reference_epoch;
    }
};
struct HeadQueryKey {
    HeadQueryOwner owner{};
    std::uint32_t token{};
    std::uintptr_t slot{};
};
inline std::uintptr_t head_query_slot(std::uintptr_t manager,std::uint32_t token) noexcept {
    const auto offset=std::uintptr_t{0x8280}+(token&0x1fffU)*std::uintptr_t{0x140};
    return manager && token!=0xffffffffU && manager<=std::numeric_limits<std::uintptr_t>::max()-offset ? manager+offset : 0;
}
enum class HeadQueryRetirement { completed, cancelled_before_cast, teardown_without_fence };
class HeadQueryLifetime {
    struct Record { HeadQueryKey key{}; bool used{}, started{}, active{}, completed{}, cancelled{}, retired{}; };
    std::array<Record,8> records_{};
    HeadQueryOwner current_{};
    Record* find(const HeadQueryKey& k) noexcept {
        for(auto& r:records_) if(r.used && r.key.owner==k.owner && r.key.token==k.token && r.key.slot==k.slot) return &r;
        return nullptr;
    }
public:
    // Owner changes invalidate publication, but MUST NOT forget pending shape users.
    void owner(const HeadQueryOwner& next) noexcept { current_=next; }
    bool issued(const HeadQueryKey& k,std::uint32_t observed_token,unsigned kind,unsigned variant) noexcept {
        if(!k.owner.player || !k.owner.world || !k.owner.shape || !(k.owner==current_) ||
            k.slot!=head_query_slot(k.owner.manager,k.token) || !k.slot || observed_token!=k.token || kind!=4 || variant!=1 || find(k)) return false;
        for(const auto& r:records_) if(r.used && !r.retired && r.key.slot==k.slot) return false;
        for(auto& r:records_) if(!r.used || r.retired) { r={k,true,false,false,false,false,false}; return true; }
        return false;
    }
    bool worker_begin(const HeadQueryKey& k,std::uint32_t observed_token) noexcept {
        auto* r=find(k); if(!r || r->retired || r->started || observed_token!=k.token) return false;
        r->started=r->active=true; return true;
    }
    bool worker_end(const HeadQueryKey& k,std::uint32_t observed_token,unsigned flags) noexcept {
        // Integration may use the captured entry token and normal-return completion invariant.
        // Never obtain these by rereading a slot after native worker return; it can be reused.
        auto* r=find(k); if(!r || !r->active || observed_token!=k.token || !(flags&2)) return false;
        r->active=false; r->completed=true; return true;
    }
    bool cancel_requested(const HeadQueryKey& k) noexcept {
        auto* r=find(k); if(!r || r->retired) return false; r->cancelled=true; return true;
    }
    // Capture generation around 090ac0 INSIDE 0958c0, before the tail call returns the slot to its pool.
    // A destructor return alone is insufficient: a verified caller/reason and no active cast are required.
    bool retired(const HeadQueryKey& k,std::uint32_t before,std::uint32_t after,HeadQueryRetirement why) noexcept {
        // If cleanup beats the observer's worker_end, retain its immutable proof and retry
        // after that invocation ends, without dereferencing native memory again.
        auto* r=find(k); if(!r || r->retired || r->active || before!=k.token || after!=std::uint32_t(k.token+0x2000U)) return false;
        const bool safe=why==HeadQueryRetirement::completed ? r->completed :
            why==HeadQueryRetirement::cancelled_before_cast && r->cancelled && !r->started;
        if(!safe) return false; r->retired=true; return true;
    }
    // A completion observation is not itself a copied/validated collision result.
    bool current_completed(const HeadQueryKey& k) noexcept {
        const auto* r=find(k); return r && r->completed && !r->active && !r->cancelled && !r->retired && r->key.owner==current_;
    }
    // Covers this bounded registry only, never unknown native users or refcounts.
    bool tracked_shape_retired(std::uintptr_t shape) const noexcept {
        for(const auto& r:records_) if(r.used && !r.retired && r.key.owner.shape==shape) return false;
        return true;
    }
};
} // namespace d2vr
