#pragma once
#include "finger_ik.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

// Bonelab's finger curl from the controller's touch sensors (visual only: no intent reads it). The index rests at touch_rest while the trigger
// is touched and adds the pull, slewed at index_slew; middle/ring/pinky follow the grip as grip*1.3-0.1; the thumb closes on any thumb
// sensor, slewed at thumb_slew; a held hand's target is held_floor at least (G4: before the filter); every finger then steps toward that by clamp01(smooth_rate*dt).
// A runtime without touch paths (or sensors silent for touch_silent_seconds while the trigger or grip move) gets today's mapping
// (rig::finger_wants) untouched. Pure: no engine, no OpenXR. Curl order: thumb, index, middle, ring, pinky.
namespace d2vr::rig {
struct FingerTouchParams {
    float touch_rest=.3f,index_slew=10.f,thumb_slew=25.f,smooth_rate=20.f,held_floor=.6f;
    float pose_smooth=static_cast<float>(finger_pose_smooth_time); // s: the drawn pose's spring (finger_ik.hpp FingerPoseFilter), 0 = off
    bool floor_after_smoothing=false; // build/legacy-finger-ease (not a file key): the pre-G4 order, the floor clamped after the filter (an open hand jumps to it)
    // "key=value" lines (blank lines and # comments allowed); any bad line -> nullopt; values clamped to sane ranges.
    static std::optional<FingerTouchParams> parse(const std::string& text) {
        FingerTouchParams p;std::istringstream in(text);std::string line;
        const auto trim=[](std::string s) {const char* w=" \t\r\n";const auto a=s.find_first_not_of(w);if(a==std::string::npos) return std::string();return s.substr(a,s.find_last_not_of(w)-a+1);};
        while(std::getline(in,line)) {
            line=trim(line);if(line.empty() || line[0]=='#') continue;
            const auto eq=line.find('=');if(eq==std::string::npos) return std::nullopt;
            const auto key=trim(line.substr(0,eq)),text_value=trim(line.substr(eq+1));
            if(text_value.empty()) return std::nullopt;
            std::istringstream num(text_value);num.imbue(std::locale::classic()); // the C locale's decimal comma must not make ".4" malformed
            float v=0;num>>v;if(num.fail() || num.peek()!=std::char_traits<char>::eof() || !std::isfinite(v)) return std::nullopt;
            if(key=="touch_rest") p.touch_rest=std::clamp(v,0.f,1.f);
            else if(key=="index_slew") p.index_slew=std::clamp(v,1.f,100.f);
            else if(key=="thumb_slew") p.thumb_slew=std::clamp(v,1.f,100.f);
            else if(key=="smooth_rate") p.smooth_rate=std::clamp(v,1.f,100.f);
            else if(key=="held_floor") p.held_floor=std::clamp(v,0.f,1.f);
            else if(key=="pose_smooth") p.pose_smooth=std::clamp(v,0.f,.3f);
            else return std::nullopt;
        }
        return p;
    }
};
struct FingerTouchInput {float trigger{},grip{};bool trigger_touch{},thumb_touch{};bool touch_available{};};
enum class CurlMapping {touch,legacy};
struct FingerTouchOutput {std::array<float,5> curl{};CurlMapping mapping{CurlMapping::legacy};bool floor_active{};bool touch_silent{};};
inline constexpr double touch_silent_seconds=10.,max_silent_step=.25; // one frame adds at most .25 s to the silent timer: a stall is not silence
inline constexpr float touch_in_use=.1f,touch_moved=.01f; // a trigger or grip above/changing by this counts as the hand using the controller
class FingerTouchCurl {
public:
    FingerTouchOutput update(unsigned side,const FingerTouchInput& in,double dt,bool held,const FingerTouchParams& p) {
        const float trigger=unit(in.trigger),grip=unit(in.grip);
        if(side>1) { // no such hand: today's mapping, no state, never a throw
            FingerTouchOutput legacy;legacy.curl=finger_wants(held,grip,trigger);return legacy;
        }
        auto& s=side_[side];
        if(!in.touch_available) {s=State{};s.last.curl=finger_wants(held,grip,trigger);return s.last;} // legacy: finger_wants verbatim, no state
        if(!std::isfinite(dt) || dt<=0) return s.last;
        const float step=static_cast<float>(dt);
        // Bonelab targets, slewed: the index's touch rest and the thumb's open/shut.
        s.sim=toward(s.sim,in.trigger_touch?p.touch_rest:0.f,p.index_slew*step);
        s.thumb=toward(s.thumb,in.thumb_touch?1.f:0.f,p.thumb_slew*step);
        const float grip_curl=std::clamp(grip*1.3f-.1f,0.f,1.f);
        std::array<float,5> target{s.thumb,std::min(1.f,trigger+s.sim),grip_curl,grip_curl,grip_curl};
        // G4: a held hand's floor goes into the target, so it rises through the 50 ms filter (Bonelab clamp01(20*dt)) instead of in one frame.
        if(held && !p.floor_after_smoothing) for(auto& c:target) c=std::max(c,p.held_floor);
        const float k=std::clamp(p.smooth_rate*step,0.f,1.f);
        for(unsigned f=0;f<5;++f) s.smooth[f]=std::clamp(s.smooth[f]+(target[f]-s.smooth[f])*k,0.f,1.f);
        // Silent sensors: the timer runs only while the trigger or grip is in use or moving, and any touch resets it.
        const bool active=trigger>touch_in_use || grip>touch_in_use || std::abs(trigger-s.last_trigger)>touch_moved || std::abs(grip-s.last_grip)>touch_moved;
        s.last_trigger=trigger;s.last_grip=grip;
        if(in.trigger_touch || in.thumb_touch) s.silent_time=0;
        else if(active) s.silent_time+=std::min(dt,max_silent_step); // a stall adds at most max_silent_step
        FingerTouchOutput out;
        out.touch_silent=s.silent_time>=touch_silent_seconds;
        if(out.touch_silent) {out.mapping=CurlMapping::legacy;out.curl=finger_wants(held,grip,trigger);}
        else {
            out.mapping=CurlMapping::touch;out.curl=s.smooth;
            if(held) {out.floor_active=true;if(p.floor_after_smoothing) for(auto& c:out.curl) c=std::max(c,p.held_floor);}
        }
        s.last=out;return out;
    }
    void reset(unsigned side) {if(side<2) side_[side]=State{};}
    // Start a side's smoothed curls at these (the mapping it just left), so the first touch frame doesn't snap the hand open. No-op for no such side.
    void seed(unsigned side,const std::array<float,5>& curls) {if(side<2) for(unsigned f=0;f<5;++f) side_[side].smooth[f]=unit(curls[f]);}
private:
    struct State {float sim{},thumb{},last_trigger{},last_grip{};std::array<float,5> smooth{};double silent_time{};FingerTouchOutput last{};};
    static float unit(float v) {return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;}
    static float toward(float v,float target,float max_move) {return v+std::clamp(target-v,-max_move,max_move);}
    std::array<State,2> side_{};
};
}
