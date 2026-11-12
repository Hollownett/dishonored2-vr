#pragma once
// Bonelab feel B15 (research Phase 3): which pose the VISIBLE hand is drawn at. Three candidates per side: the controller (palm pose, before
// the clamp), the clamp output (phase 1: the controller kept out of walls) and the hand body (the physics sphere pulled toward the controller).
// Intent never comes through here: aim, gestures and grabs read the raw controller poses (C9); this only picks the drawn pose.
//
// The body lags the controller (about 0.11 m at 2 m/s, 0.65 m at 6 m/s), so "the body wins whenever it is sane" would make the visible hand
// trail in free air. The body is therefore drawn only while it is in contact (the drive's shortfall signal or the clamp's own contact) or
// when it already sits within 0.02 m of the clamp pose; otherwise the clamp output is drawn, i.e. exactly the phase-1 pose. Every source
// change blends over 0.15 s (a body that disappears while drawn too). Walking and sprinting use the same source policy.
// While a grasp anchor is active the body is drawn only in contact. The
// body is distrusted (the clamp wins) when it is farther than 0.35 m from the controller for 0.25 s, when it lies behind a surface the clamp
// holds, or when its pose stopped updating for more than two displayed frames.
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
enum class VisualSource {controller,clamp,body,anchor};
struct VisualInput {
    double now{};                         // seconds, monotonic
    bool body_ready{};
    std::optional<WorldTrackedPose> body; // the body's published pose (a sphere: only the position is used; the orientation is the clamp's)
    std::uint64_t body_seq{};             // the pose's publication serial: unchanged for more than two frame intervals (floor 25 ms) is stale
    WorldTrackedPose clamp{};             // the phase-1 drawn pose
    WorldTrackedPose controller{};        // the palm pose before the clamp (what a yielding hand draws)
    bool clamp_contact{};
    bool body_contact{};                  // the drive's contact signal: achieved < 75% of an uncapped command for two ticks
    bool world_hit_near{};                // a fresh world hit (a plane the clamp holds) within 0.08 m of the body: confirms body_contact
    bool yield{};
    bool body_behind_surface{};           // the clamp's tunnelling guard: the body is behind a plane the clamp holds
    double player_speed{};                // Fix C: the player's horizontal speed (m/s); 0 with build/no-moving-controller-visual
    std::optional<Vec3> anchor;           // the plan A DrawnAnchor's drawn position while its weight is above zero (already eased by its own 0.25 s)
};
// body_weight (B16): how much of the drawn hand is the body, 0..1, ramped linearly over blend_seconds with the source (1 while the body is the
// settled source, 0 in free air or with no body): held things follow the drawn hand, so they follow the body only as far as it is drawn.
struct VisualOutput {WorldTrackedPose drawn{};VisualSource source{VisualSource::clamp};bool snapped{};double gap_m{};bool changed{};bool blending{};double body_weight{};};

// True when the body centre is behind any held plane by more than `margin` (the planes' normals point toward free space).
inline bool behind_held_planes(Vec3 body,const ClampHit* planes,std::size_t count,double margin=.02) {
    if(!finite(body)) return false;
    for(std::size_t i=0;i<count;++i) if(finite(planes[i].point) && finite(planes[i].normal) && dot(body-planes[i].point,planes[i].normal)<-margin) return true;
    return false;
}
// True when any held plane (a world hit the clamp made or re-hit lately) lies within `reach` of the body centre, on either side.
inline bool world_hit_near_body(Vec3 body,const ClampHit* planes,std::size_t count,double reach=.08) {
    if(!finite(body)) return false;
    for(std::size_t i=0;i<count;++i) if(finite(planes[i].point) && finite(planes[i].normal) && std::fabs(dot(body-planes[i].point,planes[i].normal))<=reach) return true;
    return false;
}

class HandVisualSource {
public:
    static constexpr double blend_seconds=.15,snap_gap=.35,snap_seconds=.25,regain_gap=.30;
    inline static bool agreement_draws_clamp=true; // v1195: build/legacy-body-agreement draws the body itself on agreement
    static constexpr double agree_gap=.02,agree_leave=.03;          // within this of the clamp pose the body is drawn without contact
    static constexpr double contact_gap=.10,contact_leave=.12;      // contact elects the body only while it is this close to the clamp pose (hysteresis)
    static constexpr double moving_speed=1.0,moving_leave=.6;      // Fix C: moving from 1.0 m/s, until it falls under 0.6 (hysteresis); free air then draws the clamp output
    static constexpr double stale_floor_s=.025;                     // a pose unchanged for more than max(this, 2 measured pose intervals) is stale
    // X-4, the single precedence: yield = controller; else the hand body (when elected) > the grasp anchor > the clamp output.
    static VisualSource pick(bool yield,bool body_elected,bool anchor_active) {
        if(yield) return VisualSource::controller;
        if(body_elected) return VisualSource::body;
        return anchor_active?VisualSource::anchor:VisualSource::clamp;
    }
    VisualOutput update(unsigned side,const VisualInput& in) {
        VisualOutput out;out.drawn=in.clamp;
        if(side>=2) return out;
        State& s=state_[side];
        if(!std::isfinite(in.now) || !clamp_detail::finite_pose(in.clamp)) {s=State{};return out;}
        double dt=s.has_time?in.now-s.time:0;
        if(!(dt>=0) || dt>.5) dt=0;   // a clock that went backwards or a long hitch starts the timers over rather than counting as dwell
        s.time=in.now;s.has_time=true;
        const bool present=in.body_ready && in.body && finite(in.body->position);
        const bool anchor_active=in.anchor && finite(*in.anchor);
        bool latched=false,stale=false;
        if(present) {
            if(!s.has_seq || in.body_seq!=s.seq) {
                if(s.has_seq) {const double delta=std::min(in.now-s.seq_time,.1);s.interval=s.interval>0?.7*s.interval+.3*delta:delta;}
                s.seq=in.body_seq;s.has_seq=true;s.seq_time=in.now;
            }
            stale=in.now-s.seq_time>std::max(stale_floor_s,2*s.interval)+1e-4;
            const double lead_gap=length(in.body->position-in.controller.position);
            if(std::isfinite(lead_gap) && lead_gap>snap_gap) s.far_s+=dt;else s.far_s=0;
            if(!s.distrusted && s.far_s>=snap_seconds-1e-9) {s.distrusted=true;latched=true;}
            else if(s.distrusted && std::isfinite(lead_gap) && lead_gap<=regain_gap) {s.distrusted=false;s.far_s=0;}
        } else {s.has_seq=false;s.interval=0;s.far_s=0;s.distrusted=false;}
        const bool sane=present && !stale && !s.distrusted && !in.body_behind_surface;
        const double gap=present?length(in.body->position-in.clamp.position):0;
        out.gap_m=std::isfinite(gap)?gap:0;
        const bool was_body=s.has_source && s.source==VisualSource::body;
        const bool contact=(in.body_contact && in.world_hit_near) || in.clamp_contact;
        // contact never elects a body farther than 10 cm (leaves at 12 cm) from the clamp pose: a lagging body must not drag the hand back.
        // Final review P2-2 (X-4 amended): while a grasp anchor is active (a motor or corpse hold, Task 12's seated grip) the body outranks it only
        // on contact; the 0.02 m agreement rule is for bare hands (it would draw the controller and let the limb hang off the hand in free air).
        // Fix C: while the player moves, the body is drawn only in contact. Its small gap to the clamp pose in free air is the lag of a body that
        // follows the controller (about 0.11 m at 2 m/s): agreement there is a coincidence that ends the moment the hand speeds up, and each flip is a blend.
        const double speed=std::isfinite(in.player_speed)?in.player_speed:0;
        s.moving=s.moving?speed>=moving_leave:speed>=moving_speed;
        const bool agrees=!anchor_active && !s.moving && out.gap_m<=(was_body?agree_leave:agree_gap);
        const bool by_contact=contact && out.gap_m<=(was_body?contact_leave:contact_gap);
        const bool elected=sane && (by_contact || agrees);
        out.snapped=latched || (present && in.body_behind_surface);
        const VisualSource want=pick(present && in.yield,elected,anchor_active);
        WorldTrackedPose target=in.clamp;
        if(want==VisualSource::controller) target=clamp_detail::finite_pose(in.controller)?in.controller:in.clamp;
        // v1195 (sim-pose-hold truth dumps: the drawn hand moved 5-21 mm on the frame after a head step at this stage): a body elected only by
        // agreement (free air, within 2-3 cm) is drawn at the clamp pose. The physics body is a tick behind every move of the tracking -> world
        // mapping (the head moving the body), so drawing it in free air drew last frame's hand; in contact it is drawn as before (the point).
        // The source stays `body` (blends, the B16 coupling weight and the counters are unchanged); settled, the two agree within the gap.
        else if(want==VisualSource::body) target.position=by_contact || !agreement_draws_clamp?in.body->position:in.clamp.position;   // the orientation stays the clamp's (a sphere has none)
        else if(want==VisualSource::anchor) target.position=*in.anchor;
        // Final review P3-3: a body that was the source and disappears (a menu press, a fault latch, a destroy, a snap) is blended away from
        // where the hand was drawn, like any source change, and that blend runs to its end; otherwise no body in play is phase 1 exactly.
        const bool body_left=!present && s.has_source && s.source==VisualSource::body;
        const bool leaving=!present && s.has_source && want==s.source && s.elapsed<blend_seconds-1e-9;
        if(!s.has_source || (!present && !body_left && !leaving)) {   // no body in play: phase 1 exactly (the clamp, or the anchor override), nothing to blend
            s.has_source=true;s.source=want;s.elapsed=blend_seconds;s.offset={};
        } else if(want!=s.source) {   // the hand is carried along by the offset it had at the change, which decays over the blend
            // measured against where the new source was LAST frame, so the offset is the gap between the sources and never a frame of travel
            const auto idx=static_cast<std::size_t>(want);
            s.offset=s.last.position-(s.have[idx]?s.cand[idx]:target.position);s.source=want;s.elapsed=0;out.changed=true;
        } else s.elapsed+=dt;
        const double w=s.elapsed>=blend_seconds-1e-9?1.:s.elapsed/blend_seconds;
        out.drawn=target;
        if(w<1) {out.drawn.position=target.position+s.offset*(1-w);out.blending=true;}
        out.source=want;s.last=out.drawn;
        if(!present) s.body_w=0;   // no body in play: phase 1 exactly
        else {const double goal=want==VisualSource::body?1.:0.,step=dt/blend_seconds;s.body_w=s.body_w<goal?std::min(goal,s.body_w+step):std::max(goal,s.body_w-step);}
        out.body_weight=s.body_w;
        s.cand[static_cast<std::size_t>(VisualSource::clamp)]=in.clamp.position;s.have[static_cast<std::size_t>(VisualSource::clamp)]=true;
        s.cand[static_cast<std::size_t>(VisualSource::controller)]=target_of_controller(in);s.have[static_cast<std::size_t>(VisualSource::controller)]=true;
        s.have[static_cast<std::size_t>(VisualSource::body)]=present;if(present) s.cand[static_cast<std::size_t>(VisualSource::body)]=in.body->position;
        s.have[static_cast<std::size_t>(VisualSource::anchor)]=anchor_active;if(anchor_active) s.cand[static_cast<std::size_t>(VisualSource::anchor)]=*in.anchor;
        return out;
    }
    void reset(unsigned side) {if(side<2) state_[side]=State{};}
private:
    static Vec3 target_of_controller(const VisualInput& in) {return clamp_detail::finite_pose(in.controller)?in.controller.position:in.clamp.position;}
    struct State {bool has_time{},has_source{},has_seq{},distrusted{},moving{};double time{},far_s{},elapsed{},seq_time{},interval{};VisualSource source{VisualSource::clamp};
                  Vec3 offset{};WorldTrackedPose last{};std::array<Vec3,4> cand{};std::array<bool,4> have{};std::uint64_t seq{};double body_w{};};
    std::array<State,2> state_{};
};

// The knob-off (and no-body-in-play) path of the same precedence: the visual output is the clamp pose and the anchor, when it has weight, is
// applied here exactly as plan A did. When the source already chose (body, controller, anchor) the drawn pose is final.
struct DrawnGrip {WorldTrackedPose pose{};const char* owner{"clamp"};bool anchored{};};
inline DrawnGrip resolve_drawn_grip(const VisualOutput& visual,const DrawnAnchor::Result& anchor) {
    DrawnGrip g;g.pose=visual.drawn;
    if(visual.source!=VisualSource::clamp) {g.owner=visual.source==VisualSource::body?"body":visual.source==VisualSource::anchor?"anchor":"controller";g.anchored=visual.source==VisualSource::anchor;return g;}
    if(anchor.weight>0) {g.pose.position=anchor.position;g.owner="anchor";g.anchored=true;}
    return g;
}

// Source counters for the status (render thread writes, status thread reads).
class HandVisualStats {
public:
    void note(unsigned side,const VisualOutput& v) {
        if(side>=2) return;
        auto& c=c_[side];
        (v.source==VisualSource::body?c.body:v.source==VisualSource::controller?c.controller:v.source==VisualSource::anchor?c.anchor:c.clamp).fetch_add(1,std::memory_order_relaxed);
        if(v.changed) c.blends.fetch_add(1,std::memory_order_relaxed);
        if(v.snapped) c.snaps.fetch_add(1,std::memory_order_relaxed);
        c.gap_m.store(v.gap_m,std::memory_order_relaxed);
    }
    std::uint64_t blends(unsigned side) const {return side<2?c_[side].blends.load(std::memory_order_relaxed):0;}
    std::string json() const {
        std::ostringstream o;o<<"{\"sides\":[";
        for(unsigned i=0;i<2;++i) {const auto& c=c_[i];
            o<<(i?",":"")<<"{\"body_frames\":"<<c.body.load()<<",\"clamp_frames\":"<<c.clamp.load()<<",\"controller_frames\":"<<c.controller.load()<<",\"anchor_frames\":"<<c.anchor.load()
             <<",\"blends\":"<<c.blends.load()<<",\"snaps\":"<<c.snaps.load()<<",\"gap_m\":"<<c.gap_m.load()<<'}';}
        o<<"]}";return o.str();
    }
private:
    struct C {std::atomic<std::uint64_t> body{},clamp{},controller{},anchor{},blends{},snaps{};std::atomic<double> gap_m{};};
    std::array<C,2> c_{};
};
}
