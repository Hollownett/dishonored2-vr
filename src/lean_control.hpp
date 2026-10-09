#pragma once
#include "controller_input.hpp"
#include "native_lean_snapshot.hpp"
namespace d2vr {
// Native gamepad lean is a latch. Only normal Y edges change it; no engine writes.
class LeanControl {
    enum class Phase { release,ready,enter,hold,neutral,pulse,wait,rearm };
    Phase phase_=Phase::release;
    std::optional<NativeLeanSnapshot> identity_;
    std::uint64_t epoch_{},last_{},since_{};
    bool previous_y_{},wheel_blocked_{};
    static bool same(const NativeLeanSnapshot& a,const NativeLeanSnapshot& b) {
        const auto& x=a.owner;const auto& y=b.owner;
        return x.game==y.game && x.player==y.player && x.player_id==y.player_id && x.machine==y.machine &&
            x.controller==y.controller && x.body==y.body && x.machine_identity==y.machine_identity &&
            x.controller_identity==y.controller_identity && x.body_identity==y.body_identity &&
            a.pool==b.pool && a.state==b.state && a.declaration==b.declaration &&
            a.input_device==b.input_device && a.action_index==b.action_index;
    }
    static bool centered(float x,float y) {return std::isfinite(x)&&std::isfinite(y)&&std::hypot(x,y)<.15f;}
public:
    ControllerInput update(const ControllerInput& raw,const std::optional<NativeLeanSnapshot>& observed,
            bool gameplay,bool focused,std::uint64_t epoch,std::uint64_t now) {
        auto out=raw;out.lean_controlled=true;if(gameplay) out.y=false;
        // An owned inactive state permits a real, freshly primed controller
        // press even after mouse Resume. The native input path selects gamepad
        // mode; an active state requires observed gamepad mode before routing
        // directions. Unknown or already-active mouse state remains blocked.
        const bool known=observed && (observed->gamepad || !observed->active);
        const bool usable=gameplay && focused && raw.active && !raw.menu && !raw.menu_modifier && !raw.journal;
        const bool changed=epoch_!=epoch || (known && (!identity_ || !same(*identity_,*observed)));
        const bool stale=last_ && (now<last_ || now-last_>250);
        epoch_=epoch;last_=now;
        if(changed || stale || !known || !usable) {
            phase_=Phase::release;since_=now;
            if(known) identity_=observed;else identity_.reset();
        }
        // Interruption never forwards the same held wheel later.
        if(raw.left_grip>.55f && (phase_!=Phase::ready || (known && observed->active))) wheel_blocked_=true;
        if(raw.left_grip<.35f) wheel_blocked_=false;
        if(wheel_blocked_) out.left_grip=0;
        const bool fresh_y=raw.y && !previous_y_;previous_y_=raw.y;
        if(!usable) {
            out.lean_suspended=gameplay && (!known || observed->active);
            if(out.lean_suspended) out.move_x=out.move_y=out.turn_x=out.turn_y=0;
            return out; // Menu and journal remain untouched. No synthetic edge.
        }
        if(!known) {
            out.lean_suspended=true;out.move_x=out.move_y=out.turn_x=out.turn_y=0;
            return out;
        }
        const bool active=observed->active;
        if(phase_==Phase::release && !raw.y && centered(raw.move_x,raw.move_y) && centered(raw.turn_x,raw.turn_y)) phase_=Phase::ready;
        if(phase_==Phase::ready && fresh_y && raw.left_grip<.35f) {
            phase_=active?Phase::neutral:Phase::enter;since_=now;
        }
        if(phase_==Phase::enter) {
            if(active) phase_=Phase::hold;
            else if(!raw.y || now-since_>=750) phase_=Phase::release;
        }
        if(phase_==Phase::hold && !active) phase_=Phase::release;
        if(phase_==Phase::hold && (!raw.y || raw.left_grip>.55f)) {phase_=Phase::neutral;since_=now;}
        if(phase_==Phase::neutral) {
            if(!active) phase_=Phase::rearm;
            else if(now-since_>=100) {phase_=Phase::pulse;since_=now;}
        }
        if(phase_==Phase::pulse && now-since_>=150) {phase_=Phase::wait;since_=now;}
        if(phase_==Phase::wait) {
            if(!active) phase_=Phase::rearm;
            else if(now-since_>=750) phase_=Phase::release; // One attempt, never retry.
        }
        if(phase_==Phase::rearm && !raw.y && centered(raw.move_x,raw.move_y) && centered(raw.turn_x,raw.turn_y)) phase_=Phase::ready;
        out.y=phase_==Phase::enter || phase_==Phase::hold || phase_==Phase::pulse;
        out.lean_modifier=phase_==Phase::hold;
        out.lean_suspended=phase_!=Phase::ready || active;
        if(out.lean_suspended && !out.lean_modifier) {
            out.move_x=out.move_y=out.turn_x=out.turn_y=0;
            out.left_trigger=out.right_trigger=out.left_grip=out.right_grip=0;
            out.left_click=out.right_click=false;
        }
        return out;
    }
};
} // namespace d2vr
