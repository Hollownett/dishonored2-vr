#pragma once
// F3: ragdoll write-safety envelope. Pure logic, no engine access, no allocation per call.
// Every corpse/guard ragdoll event post and velocity write goes through these guards.
#include "arm_ik.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
namespace d2vr {

enum class ReleaseReason : unsigned {none,open_fist,lag,lifecycle,runaway,fault,state_changed,refused};
enum class PostDecision {allow,already_dynamic,retry_wait,ceiling,disabled,refused};

// START_RAGDOLL (0xb9) budget. Times are monotonic seconds.
// Rules in order: disabled; body already dynamic (no post); global ceiling (2 allowed posts in the
// last second); first request for the entity -> allow; a second inside retry_after -> retry_wait;
// the second at/after retry_after -> allow once; any later request -> refused.
// A request the ceiling defers does not consume the entity's post or retry.
class EventPostBudget {
public:
    static constexpr double ceiling_per_second=2,retry_after=1;
    PostDecision request(std::uint32_t entity_id,bool primary_dynamic,double now) noexcept {
        if(disabled_) {++suppressed_;return PostDecision::disabled;}
        if(primary_dynamic) {++suppressed_;return PostDecision::already_dynamic;}
        Entry* e=find(entity_id);
        if(e && e->posts>=2) {++refused_;return PostDecision::refused;}
        if(e && now-e->first_post<retry_after) {++suppressed_;return PostDecision::retry_wait;}
        if(ceiling_hit(now)) {++suppressed_;return PostDecision::ceiling;}
        if(!e) {e=claim(entity_id);e->first_post=now;e->posts=1;}
        else e->posts=2;
        record(now);
        ++posted_;
        return PostDecision::allow;
    }
    void forget(std::uint32_t entity_id) noexcept {if(Entry* e=find(entity_id)) e->used=false;}
    void disable(bool off) noexcept {disabled_=off;}
    std::uint64_t posted() const noexcept {return posted_;}
    std::uint64_t suppressed() const noexcept {return suppressed_;}
    std::uint64_t refused() const noexcept {return refused_;}
private:
    static constexpr std::size_t slots=16;
    static constexpr std::size_t ceiling_slots=static_cast<std::size_t>(ceiling_per_second);
    struct Entry {bool used{};std::uint32_t id{};double first_post{};int posts{};std::uint64_t order{};};
    Entry* find(std::uint32_t id) noexcept {
        for(auto& e:entries_) if(e.used&&e.id==id) return &e;
        return nullptr;
    }
    Entry* claim(std::uint32_t id) noexcept {
        Entry* pick=&entries_[0];
        for(auto& e:entries_) {
            if(!e.used) {pick=&e;break;}
            if(e.order<pick->order) pick=&e;
        }
        pick->used=true;pick->id=id;pick->order=++counter_;
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
    std::uint64_t counter_{},posted_{},suppressed_{},refused_{};
    bool disabled_{};
};

struct RunawayVerdict {bool runaway{};double max_speed{};bool non_finite{};};
class RunawayDetector {
public:
    static constexpr double limit=12;
    // missing bodies (nullopt) are ignored
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

// Blocks writes to an entity for `seconds` after a runaway.
class EntityLockout {
public:
    static constexpr double seconds=1;
    void lock(std::uint32_t entity_id,double now) noexcept {
        for(auto& e:entries_) if(e.used&&e.id==entity_id) {e.until=now+seconds;return;}
        Entry* pick=&entries_[0];
        for(auto& e:entries_) {
            if(!e.used) {pick=&e;break;}
            if(e.until<pick->until) pick=&e;
        }
        pick->used=true;pick->id=entity_id;pick->until=now+seconds;
    }
    bool locked(std::uint32_t entity_id,double now) const noexcept {
        for(const auto& e:entries_) if(e.used&&e.id==entity_id) return now<e.until;
        return false;
    }
private:
    struct Entry {bool used{};std::uint32_t id{};double until{};};
    std::array<Entry,16> entries_{};
};

// Session-wide latch: any SEH fault on a ragdoll write trips it; writes stay off until restart.
// Generic on purpose (the hand-body manager reuses it).
class FaultLatch {
public:
    static void trip() noexcept {flag().store(true,std::memory_order_relaxed);}
    static bool tripped() noexcept {return flag().load(std::memory_order_relaxed);}
    static void reset_for_tests() noexcept {flag().store(false,std::memory_order_relaxed);}
private:
    static std::atomic<bool>& flag() noexcept {static std::atomic<bool> f{false};return f;}
};

struct KillSwitches {
    bool no_posts{},no_corpse_grab{};
    static KillSwitches read(const std::filesystem::path& build_dir) {
        KillSwitches k;
        std::error_code ec;
        k.no_posts=std::filesystem::exists(build_dir/"no-ragdoll-posts",ec);
        k.no_corpse_grab=std::filesystem::exists(build_dir/"no-corpse-grab",ec);
        return k;
    }
};
}
