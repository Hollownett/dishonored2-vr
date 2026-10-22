#pragma once
// Bonelab feel Task 12 (user decision 6): the drawn hand sits on the grasp point of whatever the mod holds with its own motor (a corpse limb, a collar,
// a physical-grab prop). The controller keeps driving intent; only the drawn pose moves. The pose blend is our own wording of the shipped technique:
// the hand eases onto the grasp point over a quarter second instead of jumping there, and eases back to the controller over the same time after the
// release (the caller's anchor is gone by then, so the last live one is kept here). The offset from the controller is capped, so a body that lags far
// behind never drags the drawn hand away from where the player's hand is. Pure: no engine access, one render-thread caller.
#include "arm_ik.hpp"
#include <array>
#include <cmath>

namespace d2vr {
class DrawnAnchor {
public:
    static constexpr double blend_seconds=.25,bound_m=.35;
    struct Result {Vec3 position{};double weight{};bool bounded{};double gap_m{};};
    // raw: the pose the hand would be drawn at; anchor: the grasp point (read only while active); now: seconds, monotonic.
    // weight 0 returns raw exactly. A non-finite input, a time step above blend_seconds or a clock that went backwards starts over at weight 0.
    Result update(unsigned side,bool active,Vec3 raw,Vec3 anchor,double now) {
        Result out;out.position=raw;out.gap_m=0;
        if(side>=2) return out;
        State& s=state_[side];
        if(!finite(raw) || !std::isfinite(now) || (active && !finite(anchor))) {s=State{};return out;}
        const bool have_time=s.has_time;const double dt=now-s.time;
        s.time=now;s.has_time=true;
        if(have_time && (dt<0 || dt>blend_seconds)) {s.linear=0;s.has_anchor=false;}
        else if(have_time) s.linear+=(active?dt:-dt)/blend_seconds;
        if(s.linear<1e-9) s.linear=0; // rounding residue of a full blend-out is exactly the controller
        if(s.linear>1) s.linear=1;
        if(active) {s.anchor=anchor;s.has_anchor=true;}
        if(!s.has_anchor || s.linear<=0) {if(!active && s.linear<=0) s.has_anchor=false;return out;}
        const double w=s.linear*s.linear*(3-2*s.linear); // smoothstep
        Vec3 offset=(s.anchor-raw)*w;
        const double len=length(offset);
        if(len>bound_m) {offset=offset*(bound_m/len);out.bounded=true;}
        out.weight=w;out.position=raw+offset;out.gap_m=length(out.position-s.anchor);
        return out;
    }
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    struct State {double linear{},time{};bool has_time{},has_anchor{};Vec3 anchor{};};
    std::array<State,2> state_{};
};
}
