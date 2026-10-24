#pragma once
// Bonelab feel B15 (research Phase 3): which pose the VISIBLE hand is drawn at. Three candidates per side: the controller (palm pose, before
// the clamp), the clamp output (phase 1: the controller kept out of walls) and the hand body (the physics sphere pulled toward the controller).
// Intent never comes through here: aim, gestures and grabs read the raw controller poses (C9); this only picks the drawn pose.
//
// The body lags the controller (about 0.11 m at 2 m/s, 0.65 m at 6 m/s), so "the body wins whenever it is sane" would make the visible hand
// trail in free air. The body is therefore drawn only while it is in contact (the drive's shortfall signal or the clamp's own contact) or
// when it already sits within 0.02 m of the clamp pose; otherwise the clamp output is drawn, i.e. exactly the phase-1 pose. Every source
// change blends over 0.15 s. The body is distrusted (the clamp wins) when it is farther than 0.35 m from the controller for 0.25 s, when it
// lies behind a surface the clamp holds, or when its pose stopped updating for more than two displayed frames.
// Pure: no engine access, one render-thread caller. Counters are atomics for the status thread.
#include "hand_clamp.hpp"
#include "drawn_anchor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace d2vr {
enum class VisualSource {controller,clamp,body};
struct VisualInput {
    double now{};                         // seconds, monotonic
    bool body_ready{};
    std::optional<WorldTrackedPose> body; // the body's published pose (a sphere: only the position is used; the orientation is the clamp's)
    std::uint64_t body_seq{};             // the pose's publication serial: unchanged for more than two displayed frames means stale
    WorldTrackedPose clamp{};             // the phase-1 drawn pose
    WorldTrackedPose controller{};        // the palm pose before the clamp (what a yielding hand draws)
    bool clamp_contact{};
    bool body_contact{};                  // the drive's contact signal (achieved < 75% of commanded for two ticks) OR clamp_contact
    bool yield{};
    bool body_behind_surface{};           // the clamp's tunnelling guard: the body is behind a plane the clamp holds
};
struct VisualOutput {WorldTrackedPose drawn{};VisualSource source{VisualSource::clamp};bool snapped{};double gap_m{};bool changed{};bool blending{};};

// True when the body centre is behind any held plane by more than `margin` (the planes' normals point toward free space).
inline bool behind_held_planes(Vec3 body,const ClampHit* planes,std::size_t count,double margin=.02) {
    if(!finite(body)) return false;
    for(std::size_t i=0;i<count;++i) if(finite(planes[i].point) && finite(planes[i].normal) && dot(body-planes[i].point,planes[i].normal)<-margin) return true;
    return false;
}

class HandVisualSource {
public:
    static constexpr double blend_seconds=.15,snap_gap=.35,snap_seconds=.25,agree_gap=.02,regain_gap=.30;
    static constexpr unsigned stale_frames=2;   // the body pose may be unchanged for this many displayed frames
    VisualOutput update(unsigned side,const VisualInput& in) {
        VisualOutput out;out.drawn=in.clamp;
        if(side>=2) return out;
        State& s=state_[side];
        if(!std::isfinite(in.now) || !clamp_detail::finite_pose(in.clamp)) {s=State{};return out;}
        double dt=s.has_time?in.now-s.time:0;
        if(!(dt>=0) || dt>.5) dt=0;   // a clock that went backwards or a long hitch starts the timers over rather than counting as dwell
        s.time=in.now;s.has_time=true;
        const bool present=in.body_ready && in.body && finite(in.body->position);
        bool latched=false;
        if(present) {
            if(!s.has_seq || in.body_seq!=s.seq) {s.seq=in.body_seq;s.has_seq=true;s.unchanged=0;} else ++s.unchanged;
            const double lead_gap=length(in.body->position-in.controller.position);
            if(std::isfinite(lead_gap) && lead_gap>snap_gap) s.far_s+=dt;else s.far_s=0;
            if(!s.distrusted && s.far_s>=snap_seconds-1e-9) {s.distrusted=true;latched=true;}
            else if(s.distrusted && std::isfinite(lead_gap) && lead_gap<=regain_gap) {s.distrusted=false;s.far_s=0;}
        } else {s.has_seq=false;s.unchanged=0;s.far_s=0;s.distrusted=false;}
        const bool stale=present && s.unchanged>stale_frames;
        const bool sane=present && !stale && !s.distrusted && !in.body_behind_surface && clamp_detail::finite_pose(in.clamp);
        const double gap=present?length(in.body->position-in.clamp.position):0;
        out.gap_m=std::isfinite(gap)?gap:0;
        const bool contact=in.body_contact || in.clamp_contact;
        VisualSource want=VisualSource::clamp;
        if(present && in.yield) want=VisualSource::controller;
        else if(sane && (contact || out.gap_m<=agree_gap)) want=VisualSource::body;
        out.snapped=latched || (present && in.body_behind_surface);
        WorldTrackedPose target=in.clamp;
        if(want==VisualSource::controller) target=clamp_detail::finite_pose(in.controller)?in.controller:in.clamp;
        else if(want==VisualSource::body) target.position=in.body->position;   // the orientation stays the clamp's (a sphere has none)
        if(!s.has_source) {s.has_source=true;s.source=want;s.elapsed=blend_seconds;}
        else if(want!=s.source) {s.from=s.last;s.source=want;s.elapsed=0;out.changed=true;}
        else s.elapsed+=dt;
        const double w=s.elapsed>=blend_seconds-1e-9?1.:s.elapsed/blend_seconds;
        if(w>=1) out.drawn=target;   // a finished blend is the target exactly (bit-exact phase 1 when the clamp is the source)
        else {out.drawn=target;out.drawn.position=s.from.position+(target.position-s.from.position)*w;out.blending=true;}
        out.source=want;s.last=out.drawn;
        return out;
    }
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    struct State {bool has_time{},has_source{},has_seq{},distrusted{};double time{},far_s{},elapsed{};VisualSource source{VisualSource::clamp};
                  WorldTrackedPose from{},last{};std::uint64_t seq{};unsigned unchanged{};};
    std::array<State,2> state_{};
};

// X-4, the drawn-grip precedence in one place: the hand body (when the rules above selected it) > the plan A DrawnAnchor grasp anchor of a
// motor hold > the clamp output. A yielding hand is the controller (the anchor does not apply). `visual.drawn` is the clamp output (or its
// blend) unless the body or the controller was chosen.
struct DrawnGrip {WorldTrackedPose pose{};const char* owner{"clamp"};bool anchored{};};
inline DrawnGrip resolve_drawn_grip(const VisualOutput& visual,const DrawnAnchor::Result& anchor) {
    DrawnGrip g;g.pose=visual.drawn;
    if(visual.source==VisualSource::body) {g.owner="body";return g;}
    if(visual.source==VisualSource::controller) {g.owner="controller";return g;}
    if(anchor.weight>0) {g.pose.position=anchor.position;g.owner="anchor";g.anchored=true;}
    return g;
}

// Source counters for the status (render thread writes, status thread reads).
class HandVisualStats {
public:
    void note(unsigned side,const VisualOutput& v) {
        if(side>=2) return;
        auto& c=c_[side];
        (v.source==VisualSource::body?c.body:v.source==VisualSource::controller?c.controller:c.clamp).fetch_add(1,std::memory_order_relaxed);
        if(v.changed) c.blends.fetch_add(1,std::memory_order_relaxed);
        if(v.snapped) c.snaps.fetch_add(1,std::memory_order_relaxed);
        c.gap_m.store(v.gap_m,std::memory_order_relaxed);
    }
    std::string json() const {
        std::ostringstream o;o<<"{\"sides\":[";
        for(unsigned i=0;i<2;++i) {const auto& c=c_[i];
            o<<(i?",":"")<<"{\"body_frames\":"<<c.body.load()<<",\"clamp_frames\":"<<c.clamp.load()<<",\"controller_frames\":"<<c.controller.load()
             <<",\"blends\":"<<c.blends.load()<<",\"snaps\":"<<c.snaps.load()<<",\"gap_m\":"<<c.gap_m.load()<<'}';}
        o<<"]}";return o.str();
    }
private:
    struct C {std::atomic<std::uint64_t> body{},clamp{},controller{},blends{},snaps{};std::atomic<double> gap_m{};};
    std::array<C,2> c_{};
};
}
