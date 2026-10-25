#pragma once
// Track T item A: a grenade leaves the throwing hand with the hand's own motion. Pure state (no engine, no clock): the probe feeds a
// per-frame hand sample (world position and velocity RELATIVE to the player, game metres / m/s) and the trigger; the native Throw
// hook asks take() when the game finally launches (about 0.3 s after the release: the throw animation), and gets
// gain x (peak hand speed over the 0.10 s before the release) + the player's own velocity, capped at the game's own throw speed.
// No valid release in the last 1.5 s, or no sample in that window: ok=false and the native throw is untouched.
// Ideas (gain/window/cap, release-time capture) follow the measured facts in .superpowers/sdd/2026-10-25-small-objects-fingers/
// mines-grenades-research.md; no code is shared with that GPL project.
#include <array>
#include <cmath>
#include <cstdint>

namespace d2vr {
struct ThrowGestureParams {
    double gain=1.5,window_ms=100,cap_mps=17,stale_ms=1500,teleport_mps=12;
    float hold_on=.55f,hold_off=.35f;
};
struct ThrowGestureResult {bool ok{};std::array<double,3> velocity{},spawn{};double hand_speed{};};
class ThrowGesture {
    static constexpr unsigned history=32;
    struct Sample {std::uint64_t t;std::array<double,3> v;};
    ThrowGestureParams p_;
    std::array<Sample,history> ring_{};unsigned head_{},count_{};
    std::array<double,3> pos_{};bool have_pos_{};std::uint64_t pos_t_{};
    bool held_{},released_{};std::uint64_t release_t_{};std::array<double,3> peak_{};bool peak_ok_{};
    static double norm(const std::array<double,3>& v) noexcept {return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
    static bool finite(const std::array<double,3>& v) noexcept {return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);}
public:
    explicit ThrowGesture(const ThrowGestureParams& p={}) : p_(p) {}
    void reset() noexcept {head_=count_=0;held_=released_=false;peak_ok_=false;have_pos_=false;}
    // velocity relative to the player; samples over the teleport speed (a blink, a snap turn) restart the history.
    void note_hand(std::uint64_t now,const std::array<double,3>& pos,const std::array<double,3>& velocity) noexcept {
        if(!finite(pos) || !finite(velocity)) {count_=0;have_pos_=false;return;}
        pos_=pos;have_pos_=true;pos_t_=now;
        if(norm(velocity)>p_.teleport_mps) {count_=0;return;}
        ring_[head_]={now,velocity};head_=(head_+1)%history;if(count_<history) ++count_;
    }
    // The trigger: a release (held -> below hold_off) captures the peak over the window before it. A new squeeze discards an unused one.
    void note_trigger(std::uint64_t now,float trigger) noexcept {
        if(!held_ && trigger>=p_.hold_on) {held_=true;released_=false;}
        else if(held_ && trigger<p_.hold_off) {
            held_=false;released_=true;release_t_=now;peak_ok_=false;peak_={};double best=-1;
            for(unsigned i=0;i<count_;++i) {
                const auto& s=ring_[(head_+history-1-i)%history];
                if(now<s.t || now-s.t>p_.window_ms) continue;
                const double n=norm(s.v);if(n>best) {best=n;peak_=s.v;peak_ok_=true;}
            }
        }
    }
    bool pending(std::uint64_t now) const noexcept {return released_ && now>=release_t_ && now-release_t_<=p_.stale_ms;}
    // The game launches the grenade now. Consumes the release.
    ThrowGestureResult take(std::uint64_t now,const std::array<double,3>& player_velocity) noexcept {
        ThrowGestureResult out{};
        if(!pending(now) || !peak_ok_ || !have_pos_ || now<pos_t_ || now-pos_t_>250 || !finite(player_velocity)) return out;
        released_=false;
        out.hand_speed=norm(peak_);
        for(unsigned i=0;i<3;++i) out.velocity[i]=peak_[i]*p_.gain+player_velocity[i];
        const double n=norm(out.velocity);
        if(n>p_.cap_mps) for(double& c:out.velocity) c*=p_.cap_mps/n;
        out.spawn=pos_;out.ok=true;return out;
    }
};
} // namespace d2vr
