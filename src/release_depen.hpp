#pragma once
// A released object's depenetration push (held_clamp.hpp SafeRelease): the render thread asks, the game thread's dispatcher writes it up to 100 ms
// later. The request names the object by entity address, entity id and physics handle; the dispatcher re-reads all three just before the write, and a
// new hold of anything cancels the request, so a recycled entity or a re-grabbed object never receives the push. Pure policy: the caller holds its lock.
#include "arm_ik.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
namespace d2vr {
struct ReleaseDepenRequest {std::uintptr_t entity{};std::uint32_t id{},handle{};Vec3 velocity{};std::uint64_t tick{};};
class ReleaseDepen {
public:
    static constexpr double max_speed=1.5;
    static constexpr std::uint64_t max_age_ms=100;
    enum class Verdict {none,ready,stale,rejected,mismatch,held};
    void request(const ReleaseDepenRequest& r) {r_=r;has_=r.entity && r.id && r.handle;}
    void cancel() {has_=false;r_={};}
    bool pending() const {return has_;}
    // Consume the request. live_id(entity) / live_handle(entity): the object's entity id and physics handle as of now (none: unreadable);
    // held(entity): a lane holds it. ready: `out` may be written as a velocity.
    template<class LiveId,class LiveHandle,class Held>
    Verdict take(std::uint64_t now,LiveId live_id,LiveHandle live_handle,Held held,ReleaseDepenRequest& out) {
        if(!has_) return Verdict::none;
        const ReleaseDepenRequest r=r_;cancel();
        if(held(r.entity)) return Verdict::held;
        if(now<r.tick || now-r.tick>max_age_ms) return Verdict::stale;
        if(!finite(r.velocity) || length(r.velocity)>max_speed+1e-6) return Verdict::rejected;
        const auto id=live_id(r.entity);const auto handle=live_handle(r.entity);
        if(!id || !handle || *id!=r.id || *handle!=r.handle) return Verdict::mismatch;
        out=r;return Verdict::ready;
    }
private:
    ReleaseDepenRequest r_{};bool has_{};
};
}
