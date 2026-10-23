#pragma once
// B10: hand-body lifecycle state machine (pure, no engine code, no clock: the caller passes now_ms).
// Decides, per side, when the caller must create or destroy the hand's physics body and when writes to it are allowed.
// Bonelab's hand body is a long-lived physical object that is rebuilt with the level; ours must never outlive its world.
// Rules: create only when enabled, gameplay, focused, tracked, physics idle, world known, the state is absent and no fault is latched.
// A world or world_generation change, loss of gameplay, disable, shutdown or fault destroys at once; loss of focus or tracking destroys
// after kFocusGraceMs (a menu blink does not churn). destroy_queued means "physics is busy: a direct destroy is not allowed, use the
// 0x89fb0 command queue" (docs/native-hand-body-re.md Q8). After update() asks for a destroy the state is `destroying` and update() returns
// none until the caller reports destroyed(side); create can never be returned while a destroy is pending. A fault latches the side off.
#include <cstdint>

namespace d2vr {
enum class BodyState {absent,ready,destroying,faulted};
enum class BodyAction {none,create,destroy,destroy_queued};
struct LifecycleInput {std::uintptr_t world{};std::uint32_t world_generation{};bool gameplay{};bool focused{};bool tracked{};bool physics_busy{};bool shutdown{};bool fault{};bool enabled{true};std::uint64_t now_ms{};};

class HandBodyLifecycle {
public:
    static constexpr std::uint64_t kFocusGraceMs=1000;
    BodyAction update(unsigned side,const LifecycleInput& in) {
        if(side>=2) return BodyAction::none;
        Side& s=sides_[side];
        if(in.fault) s.latched=true;
       s.last_gen=in.world_generation;
        if(s.state==BodyState::faulted||s.state==BodyState::destroying) return BodyAction::none;
        if(s.state==BodyState::absent) {
            if(s.latched) {s.state=BodyState::faulted;return BodyAction::none;}
            if(in.enabled&&in.gameplay&&in.focused&&in.tracked&&!in.physics_busy&&!in.shutdown&&in.world!=0) return BodyAction::create;
            return BodyAction::none;
        }
        // ready
        bool now_bad=in.shutdown||s.latched||!in.enabled||!in.gameplay||in.world!=s.world||in.world_generation!=s.gen;
        if(!now_bad) {
            if(in.focused&&in.tracked) s.lost=false;
            else {
                if(!s.lost) {s.lost=true;s.lost_since=in.now_ms;}
                if(in.now_ms>=s.lost_since&&in.now_ms-s.lost_since>=kFocusGraceMs) now_bad=true;
            }
        }
        if(!now_bad) return BodyAction::none;
        s.state=BodyState::destroying;
        return in.physics_busy?BodyAction::destroy_queued:BodyAction::destroy;
    }
    // The world generation seen by the latest update() is the body's generation.
    void created(unsigned side,std::uintptr_t world,std::uint32_t handle) {
        if(side>=2) return;
        Side& s=sides_[side];
        s.state=BodyState::ready;s.world=world;s.handle=handle;s.gen=s.last_gen;s.lost=false;
    }
    void destroyed(unsigned side) {
        if(side>=2) return;
        Side& s=sides_[side];
        s.world=0;s.handle=0;s.lost=false;
        s.state=s.latched?BodyState::faulted:BodyState::absent;
    }
    bool can_write(unsigned side,std::uintptr_t world,std::uint32_t handle) const {
        if(side>=2) return false;
        const Side& s=sides_[side];
        return s.state==BodyState::ready&&world==s.world&&handle==s.handle;
    }
    BodyState state(unsigned side) const {return side<2?sides_[side].state:BodyState::faulted;}
private:
    struct Side {BodyState state{BodyState::absent};std::uintptr_t world{};std::uint32_t handle{},gen{},last_gen{};bool latched{},lost{};std::uint64_t lost_since{};};
    Side sides_[2];
};
}
