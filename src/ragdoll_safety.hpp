#pragma once
// F3: ragdoll write-safety envelope. Pure logic, no engine access, no allocation per call.
// Every corpse/guard ragdoll event post and velocity write goes through these guards.
#include "arm_ik.hpp"
#include "native_motion.hpp"
#include "release_reason.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
namespace d2vr {

enum class PostDecision {allow,already_dynamic,retry_wait,ceiling,disabled,refused,locked};

// UNITS: every `now` in this header is monotonic SECONDS. Callers on GetTickCount64() milliseconds convert with
// seconds_from_ms. THREADING: EventPostBudget and EntityLockout are game-thread-only; only the counters and the
// disabled flag may be read from another thread (relaxed atomics).
inline double seconds_from_ms(std::uint64_t ms) noexcept {return static_cast<double>(ms)/1000.0;}

// Blocks writes to an entity for `seconds` after a runaway. Game-thread-only.
class EntityLockout {
public:
    static constexpr double seconds=1;
    // A lock never evicts a lock that is still active: with all 16 slots held by active locks, a new entity is NOT locked and the refusal is counted
    // (refused()), so a runaway body can never become grabbable again because other entities churned the table. Expired slots are reusable.
    bool lock(std::uint32_t entity_id,double now) noexcept {
        for(auto& e:entries_) if(e.used&&e.id==entity_id) {e.until=now+seconds;return true;}
        for(auto& e:entries_) if(!e.used || e.until<=now) {e.used=true;e.id=entity_id;e.until=now+seconds;return true;}
        ++refused_;return false;
    }
    std::uint64_t refused() const noexcept {return refused_;}
    bool locked(std::uint32_t entity_id,double now) const noexcept {
        for(const auto& e:entries_) if(e.used&&e.id==entity_id) return now<e.until;
        return false;
    }
private:
    struct Entry {bool used{};std::uint32_t id{};double until{};};
    std::array<Entry,16> entries_{};std::uint64_t refused_{};
};

// START_RAGDOLL (0xb9) budget. Game-thread-only (see above).
// Rules in order: disabled; locked (lockout overload only); body already dynamic (no post); refused (both posts
// used); retry_wait (second request inside retry_after); global ceiling (2 allowed posts in the last second);
// then allow (first post, or the one retry at/after retry_after).
// A request the ceiling defers does not consume the entity's post or retry.
// Table: 16 entities. Only a stale entry is evicted (one post and idle > 5 s, or refused and idle > 60 s); a refused
// entity is never reset by churn. When no slot can be reclaimed a new entity gets `ceiling`.
class EventPostBudget {
public:
    static constexpr double ceiling_per_second=2,retry_after=1;
    PostDecision request(std::uint32_t entity_id,bool primary_dynamic,double now,const EntityLockout& lockout) noexcept {
        if(!disabled_.load(std::memory_order_relaxed) && lockout.locked(entity_id,now)) {bump(suppressed_);return PostDecision::locked;}
        return request(entity_id,primary_dynamic,now);
    }
    PostDecision request(std::uint32_t entity_id,bool primary_dynamic,double now) noexcept {
        if(disabled_.load(std::memory_order_relaxed)) {bump(suppressed_);return PostDecision::disabled;}
        if(primary_dynamic) {bump(suppressed_);return PostDecision::already_dynamic;}
        Entry* e=find(entity_id);
        if(e) {
            const double seen=e->last_seen;
            e->last_seen=now;
            if(e->posts>=2) {bump(refused_);return PostDecision::refused;}
            if(now-e->first_post<retry_after) {bump(suppressed_);return PostDecision::retry_wait;}
            if(ceiling_hit(now)) {e->last_seen=seen;bump(suppressed_);return PostDecision::ceiling;}
            e->posts=2;
        } else {
            if(ceiling_hit(now)) {bump(suppressed_);return PostDecision::ceiling;}
            e=claim(entity_id,now);
            if(!e) {bump(suppressed_);return PostDecision::ceiling;}
            e->first_post=now;e->posts=1;
        }
        record(now);
        bump(posted_);
        return PostDecision::allow;
    }
    void forget(std::uint32_t entity_id) noexcept {if(Entry* e=find(entity_id)) e->used=false;}
    void disable(bool off) noexcept {disabled_.store(off,std::memory_order_relaxed);}
    bool disabled() const noexcept {return disabled_.load(std::memory_order_relaxed);}
    std::uint64_t posted() const noexcept {return posted_.load(std::memory_order_relaxed);}
    std::uint64_t suppressed() const noexcept {return suppressed_.load(std::memory_order_relaxed);}
    std::uint64_t refused() const noexcept {return refused_.load(std::memory_order_relaxed);}
private:
    static constexpr std::size_t slots=16;
    static constexpr std::size_t ceiling_slots=static_cast<std::size_t>(ceiling_per_second);
    static constexpr double stale_after=5,refused_stale_after=60;
    struct Entry {bool used{};std::uint32_t id{};double first_post{},last_seen{};int posts{};};
    static void bump(std::atomic<std::uint64_t>& c) noexcept {c.store(c.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);}
    Entry* find(std::uint32_t id) noexcept {
        for(auto& e:entries_) if(e.used&&e.id==id) return &e;
        return nullptr;
    }
    Entry* claim(std::uint32_t id,double now) noexcept {
        Entry* pick=nullptr;
        for(auto& e:entries_) {
            if(!e.used) {pick=&e;break;}
            const bool stale=now-e.last_seen>(e.posts>=2?refused_stale_after:stale_after);
            if(stale && (!pick || e.last_seen<pick->last_seen)) pick=&e;
        }
        if(!pick) return nullptr;
        pick->used=true;pick->id=id;pick->last_seen=now;
        return pick;
    }
    bool ceiling_hit(double now) const noexcept {
        for(double t:recent_) if(now-t>=1.0) return false;   // a slot older than 1 s is free
        return true;
    }
    void record(double now) noexcept {
        std::size_t oldest=0;
        for(std::size_t i=1;i<ceiling_slots;++i) if(recent_[i]<recent_[oldest]) oldest=i;
        recent_[oldest]=now;
    }
    static std::array<double,ceiling_slots> no_posts_yet() noexcept {
        std::array<double,ceiling_slots> a{};
        a.fill(-std::numeric_limits<double>::infinity());
        return a;
    }
    std::array<Entry,slots> entries_{};
    std::array<double,ceiling_slots> recent_ = no_posts_yet();
    std::atomic<std::uint64_t> posted_{0},suppressed_{0},refused_{0};
    std::atomic<bool> disabled_{false};
};

struct RunawayVerdict {bool runaway{};double max_speed{};bool non_finite{};};
// Input per body: nullopt = body not resolved (ignored); a value = resolved. native_motion() returns nullopt for a
// RESOLVED body whose velocity is non-finite or above 100 m/s, so always build entries with runaway_sample().
inline std::optional<Vec3> runaway_sample(bool body_resolved,const std::optional<NativeMotion>& motion) noexcept {
    if(!body_resolved) return std::nullopt;
    if(motion) return motion->velocity;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    return Vec3{nan,nan,nan};   // resolved but its motion read failed validation: counts as non-finite
}
class RunawayDetector {
public:
    static constexpr double limit=12;
    static RunawayVerdict check(const std::array<std::optional<Vec3>,17>& velocities) noexcept {
        RunawayVerdict r;
        for(const auto& v:velocities) {
            if(!v) continue;
            if(!finite(*v)) {r.non_finite=true;continue;}
            const double s=length(*v);
            if(s>r.max_speed) r.max_speed=s;
        }
        r.runaway=r.non_finite||r.max_speed>limit;
        return r;
    }
};

// Session-wide latch: any SEH fault on a ragdoll write trips it; writes stay off until restart.
// Generic on purpose (the hand-body manager reuses it).
class FaultLatch {
public:
    static void trip() noexcept {flag().store(true,std::memory_order_relaxed);}
    static bool tripped() noexcept {return flag().load(std::memory_order_relaxed);}
#ifdef D2VR_TESTING
    static void reset_for_tests() noexcept {flag().store(false,std::memory_order_relaxed);}
#endif
private:
    static std::atomic<bool>& flag() noexcept {static std::atomic<bool> f{false};return f;}
};

struct KillSwitches {
    bool no_posts{},no_corpse_grab{};
    static KillSwitches read(const std::filesystem::path& build_dir) {
        return read(build_dir,[](const std::filesystem::path& p,std::error_code& ec) {return std::filesystem::exists(p,ec);});
    }
    // `exists(path, error_code&)` is injectable so the error path is testable. Fail closed: if the filesystem
    // cannot answer, the switch counts as present.
    template<class Exists> static KillSwitches read(const std::filesystem::path& build_dir,Exists exists) {
        KillSwitches k;
        k.no_posts=present(build_dir/"no-ragdoll-posts",exists);
        k.no_corpse_grab=present(build_dir/"no-corpse-grab",exists);
        return k;
    }
private:
    template<class Exists> static bool present(const std::filesystem::path& p,Exists& exists) {
        std::error_code ec;
        const bool there=exists(p,ec);
        return ec?true:there;
    }
};
}
