#pragma once
// Pure policy: the visible hand stops at world surfaces (physical hands phase 1: G1, G3, C3, C4, C8, C11, C14).
// Nothing here touches the engine. update() posts sphere sweeps (palm, fingertip, or chest re-acquire) and consumes
// their results one frame later through ClampInput. Hits are kept as half-space planes for plane_seconds, so a contact
// survives a frame without a result and a corner holds both walls; a plane no sweep meets directly expires.
#include "weapon_collision.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

// Contract checks (note_drawn after every update, one update per frame). Off by default: the DLL is built without
// NDEBUG, so a plain assert would abort the game. Breaks are always counted (missed_notes, repeated_updates).
#ifndef D2VR_HAND_CLAMP_CONTRACT
#  if defined(D2VR_DEBUG_CONTRACTS) && !defined(NDEBUG)
#    include <cassert>
#    define D2VR_HAND_CLAMP_CONTRACT(ok) assert(ok)
#  else
#    define D2VR_HAND_CLAMP_CONTRACT(ok) ((void)0)
#  endif
#endif

namespace d2vr {

struct ClampProbe {Vec3 local{};double radius{};};      // sphere in the grip frame (x forward, y left, z up)
struct ClampHit {Vec3 point{},normal{};std::uintptr_t entity{};std::uint16_t material{0xffff};bool initial_overlap{};bool normal_valid{true};};
// exclude: the body (its entity) the cast must not report (the native cast's source-body exclusion, request +0x68/+0x14).
struct ClampSweep {Vec3 from{},to{};double radius{};bool valid{};std::optional<std::uintptr_t> exclude;};
struct HandClampConfig {
    double palm_radius=.05,skin=.005,plane_seconds=.1,release_speed=1.5,snap_gap=.35,snap_seconds=.25,
           rearm_seconds=.1,gap_seconds=.25,haptic_cooldown=.08,speed_for_full=3.;
    unsigned overlap_frames=3,max_planes=4;};
struct ClampInput {
    double now{};                       // monotonic seconds; per-side dt is derived from it
    std::uint64_t epoch{};              // pose_epoch ^ (reference_epoch<<32) ^ player; a change resets the side
    WorldTrackedPose root{};            // level player frame (C11, deviation 9)
    WorldTrackedPose target{};          // calibrated, reach-clamped controller grip
    Vec3 chest{};                       // free-space re-acquire origin
    std::optional<ClampHit> palm_hit,tip_hit,chest_hit; // results of the sweeps this side posted last frame
    ClampProbe tip{};bool tip_enabled{true},post{true},yield{};
    // Loose props the push logic owns (light, in reach, pinned or not; NativeHandContact::fill_clamp_input): an initial
    // overlap with one never counts toward the overlap snap, and a re-acquire sweep stopped by one does not keep the side
    // released (a toppled bottle on the hand is pushed, not a reason to let go of the walls).
    static constexpr std::size_t loose_capacity=8;
    std::array<std::uintptr_t,loose_capacity> loose{};std::size_t loose_count{};
    // The met prop (I1): this side also sweeps its leading probe with that body excluded, so a wall behind the prop is held
    // as a plane (the closest-hit sweeps only ever report the prop in front of it). exclude_hit: last frame's result.
    std::optional<std::uintptr_t> exclude;std::optional<ClampHit> exclude_hit;
    bool is_loose(std::uintptr_t entity) const {
        for(std::size_t i=0;i<std::min(loose_count,loose_capacity);++i) if(loose[i]==entity) return true;
        return false;
    }};
enum class ClampState {free,contact,released};
struct ClampOutput {
    bool valid{};WorldTrackedPose drawn{};Vec3 shift{};ClampState state{};bool snapped{};
    std::optional<ClampHit> contact;double approach_speed{};
    std::optional<CollisionEvent> event;std::optional<CollisionHaptic> haptic;
    ClampSweep palm_sweep,tip_sweep;    // palm_sweep is chest->target while released
    ClampSweep exclude_sweep;           // the leading probe, the met prop excluded (ClampInput::exclude)
    bool held{};                        // drawn at last frame's hand while this frame's sweep validates a jump
    bool contact_fresh{};};             // `contact`'s plane was made or re-hit by the last answered raw cast or since (I3)

namespace clamp_detail {
inline Vec3 rotate(const std::array<Vec3,3>& basis,Vec3 local) {return basis[0]*local.x+basis[1]*local.y+basis[2]*local.z;}
inline Vec3 local_vector(const WorldTrackedPose& frame,Vec3 v) {return {dot(v,frame.basis[0]),dot(v,frame.basis[1]),dot(v,frame.basis[2])};}
inline Vec3 local_point(const WorldTrackedPose& frame,Vec3 p) {return local_vector(frame,p-frame.position);}
inline Vec3 world_point(const WorldTrackedPose& frame,Vec3 local) {return frame.position+rotate(frame.basis,local);}
inline bool finite_pose(const WorldTrackedPose& p) {
    return finite(p.position) && finite(p.basis[0]) && finite(p.basis[1]) && finite(p.basis[2]);
}
inline Vec3 clamp_length(Vec3 v,double limit) {const double l=length(v);return l>limit && l>0?v*(limit/l):v;}
inline bool usable(const ClampProbe& p) {return finite(p.local) && std::isfinite(p.radius) && p.radius>0;}
} // namespace clamp_detail

// The level player frame: forward flattened onto the ground plane, z up. {1,0,0} when forward is vertical or not finite.
inline WorldTrackedPose level_frame(Vec3 position,Vec3 forward) {
    Vec3 f{forward.x,forward.y,0};
    const double l=std::hypot(f.x,f.y);
    f=std::isfinite(l) && l>1e-6?f*(1/l):Vec3{1,0,0};
    return {position,{f,{-f.y,f.x,0},{0,0,1}}};
}

// The open-hand probe that leads the motion: maximises dot(R*local, unit(motion)) + radius (the first wins a tie).
// Below 0.05 m/s, or with no usable probe, the fallback.
template<std::size_t N> ClampProbe leading_probe(const std::array<ClampProbe,N>& open_hand,const WorldTrackedPose& grip,Vec3 motion,ClampProbe fallback) {
    const double speed=length(motion);
    if(!std::isfinite(speed) || speed<.05 || !clamp_detail::finite_pose(grip)) return fallback;
    const Vec3 direction=motion*(1/speed);
    const ClampProbe* best=nullptr;double score=-std::numeric_limits<double>::infinity();
    for(const auto& probe:open_hand) {
        if(!clamp_detail::usable(probe)) continue;
        const double s=dot(clamp_detail::rotate(grip.basis,probe.local),direction)+probe.radius;
        if(std::isfinite(s) && (!best || s>score)) {best=&probe;score=s;}
    }
    return best?*best:fallback;
}

// C14: the clamp's cast cost per frame. A rolling mean over window_seconds; once a full window since the last change is
// over budget_us, the fingertip sweep goes first, then the hands alternate frames. Both stay until reset().
class CastBudget {
public:
    static constexpr double budget_us=500,window_seconds=2;
    void note(double frame_us,double now) {
        if(!std::isfinite(frame_us) || frame_us<0 || !std::isfinite(now)) return;
        if(!started_ || (!samples_.empty() && now<samples_.back().now)) {started_=true;start_=now;samples_.clear();}
        samples_.push_back({now,frame_us});
        while(samples_.front().now<=now-window_seconds) samples_.pop_front();
        double sum=0,peak=0;
        for(const auto& s:samples_) {sum+=s.us;peak=std::max(peak,s.us);}
        mean_=sum/static_cast<double>(samples_.size());max_=peak;
        if(now-start_>=window_seconds-1e-9 && mean_>budget_us && (tip_ || !alternate_)) {
            if(tip_) tip_=false; else alternate_=true;
            start_=now;samples_.clear(); // the next decision measures the new mode for a full window
        }
    }
    bool tip_enabled() const {return tip_;}
    bool alternate() const {return alternate_;}
    double mean_us() const {return mean_;}
    double max_us() const {return max_;}
    void reset() {*this=CastBudget{};}
private:
    struct Sample {double now,us;};
    std::deque<Sample> samples_;
    double start_{},mean_{},max_{};
    bool started_{},tip_{true},alternate_{};
};

class HandClamp {
public:
    static constexpr double shadow_seconds=.2;     // a plane kept only by shadowing gets this long past its last direct hit
    static constexpr double max_lead=.15;          // the predictive extension never reaches farther per cast cadence
                                                   // (a jump is not extrapolated)
    static constexpr unsigned max_hold_frames=2;   // frames the hand may wait for a sweep to validate an unvalidated target
private:
    enum class Posted : unsigned char {none,palm,chest};
    struct Plane {
        Vec3 point{},normal{},local{};double radius{},age{},direct_age{};
        std::uintptr_t entity{};std::uint16_t material{0xffff};bool tip{},refuted{};
        bool last_answer{};   // hit directly by this side's last answered result (it lands a frame late, or two in C14)
        bool fresh{true};     // made or re-hit since the last answered raw (unslid) cast (I3; kept through frames without a result)
    };
    static constexpr std::size_t plane_capacity=8;
    struct Side {
        bool has{};std::uint64_t epoch{};double last_now{};ClampState state{ClampState::free};
        Vec3 prev_local{},shift_local{};std::array<Vec3,3> prev_basis_local{};  // root (level player) frame, C11
        Vec3 drawn_world{},target_world{};std::array<Vec3,3> basis_world{};     // last frame in the world
        std::array<Plane,plane_capacity> planes{};std::size_t plane_count{};
        double over_time{},clear_time{},since_haptic{1e9};unsigned overlap_count{},hold_frames{};
        Posted posted{Posted::none};bool last_slid{};ClampSweep posted_palm{},posted_tip{};ClampProbe posted_tip_probe{};
        ClampSweep posted_exclude{};ClampProbe posted_exclude_probe{};bool posted_exclude_tip{};
        std::optional<std::uintptr_t> loose_touch;   // a loose prop the last answered cast hit or started in (excluded from the next exclusion sweep)
        Vec3 seg_from{},seg_to{};bool has_segment{},seg_cut{};   // the path the last answered slot-0 sweep found free
        Vec3 clear_from_local{};bool has_clear_from{},last_overlap{};   // start of the last palm sweep that began free
        bool posted_slid{},slid_cut{};Vec3 slid_from{},slid_to{};unsigned slid_frames{1000};   // the last answered slid sweep
        unsigned posted_slid_window{},slid_window{};   // frames that slid sweep's lead covers after it is answered
        bool drawn_validated{};                        // last frame drew a validated (or held) hand
        unsigned cadence{1};                           // frames between casts: 1, or 2 in C14 alternate mode
        bool pending_note{};ClampOutput last_out{};
    };
    HandClampConfig cfg_;
    std::array<Side,2> sides_{};
    std::array<std::uint64_t,2> snapbacks_{},contact_frames_{},missed_notes_{},repeated_updates_{},blocked_frames_{};

    std::size_t capacity() const {return std::clamp<std::size_t>(cfg_.max_planes,1,plane_capacity);}
    static void store(Side& s,const ClampInput& in,Vec3 drawn) {
        s.has=true;s.epoch=in.epoch;s.last_now=in.now;
        s.prev_local=clamp_detail::local_point(in.root,drawn);
        for(std::size_t i=0;i<3;++i) s.prev_basis_local[i]=clamp_detail::local_vector(in.root,in.target.basis[i]);
        s.drawn_world=drawn;s.target_world=in.target.position;s.basis_world=in.target.basis;
    }
    void post_chest(Side& s,const ClampInput& in,ClampOutput& out) const {
        if(!in.post || !finite(in.chest)) return;
        out.palm_sweep={in.chest,in.target.position,cfg_.palm_radius,true};
        s.posted=Posted::chest;s.posted_palm=out.palm_sweep;
    }
    void released_frame(Side& s,const ClampInput& in,ClampOutput& out) const {
        out.state=ClampState::released;out.drawn=in.target;out.shift={};
        post_chest(s,in,out);
        store(s,in,in.target.position);
    }
    static void release(Side& s) {
        s.state=ClampState::released;s.plane_count=0;s.shift_local={};
        s.over_time=0;s.clear_time=0;s.overlap_count=0;s.hold_frames=0;s.has_segment=false;s.slid_frames=1000;s.drawn_validated=false;
        s.has_clear_from=false;s.last_overlap=false;
    }
    void snap(unsigned side,Side& s,ClampOutput& out) {release(s);++snapbacks_[side];out.snapped=true;}
    // Fraction along the sweep where its sphere first touches the plane (0 when it starts touching).
    static std::optional<double> crossing(const ClampSweep& sweep,Vec3 point,Vec3 normal) {
        const double s0=dot(sweep.from-point,normal)-sweep.radius,s1=dot(sweep.to-point,normal)-sweep.radius;
        if(s0<=0) return 0.;
        if(s1>=0) return std::nullopt;
        return s0/(s0-s1);
    }
    static bool plane_hit(const ClampHit& hit) {
        const double nl=length(hit.normal);
        return !hit.initial_overlap && hit.normal_valid && finite(hit.point) && std::isfinite(nl) && nl>.5;
    }
    // Where along the sweep the hit stopped it.
    static double hit_fraction(const ClampSweep& sweep,const ClampHit& hit) {
        if(hit.initial_overlap) return 0;
        if(plane_hit(hit)) if(const auto t=crossing(sweep,hit.point,hit.normal*(1/length(hit.normal)))) return *t;
        const Vec3 d=sweep.to-sweep.from;const double l2=dot(d,d);
        const double t=l2>1e-12 && finite(hit.point)?dot(hit.point-sweep.from,d)/l2:0;
        return std::isfinite(t)?std::clamp(t,0.,1.):0;
    }
    static double segment_distance(Vec3 p,Vec3 a,Vec3 b) {
        const Vec3 d=b-a;const double l2=dot(d,d);
        const double t=l2>1e-12?std::clamp(dot(p-a,d)/l2,0.,1.):0;
        return length(p-(a+d*t));
    }
    // Within palm_radius of a swept segment. Past the end of a segment that a hit cut short lies unknown space (the
    // second wall of a corner reports at the same fraction as the first): there only beyond_cut is allowed.
    static constexpr double beyond_cut=.01;
    bool on_segment(Vec3 p,Vec3 a,Vec3 b,bool cut) const {
        const Vec3 d=b-a;const double l2=dot(d,d);
        const double t=l2>1e-12?dot(p-a,d)/l2:1;   // a sweep stopped at its start (an overlap) is all end
        const double distance=segment_distance(p,a,b);
        return distance<=(cut && t>=1?beyond_cut:cfg_.palm_radius)+1e-9;
    }
    // A closest-hit cast reports one surface. A held plane it reached at or beyond the hit is shadowed, not disproved:
    // it is kept fresh, but only within shadow_seconds of its last direct hit. One it passed through without reporting is
    // refuted and ages out.
    static void judge(Side& s,const ClampSweep& sweep,const std::optional<ClampHit>& hit,bool tip) {
        if(!sweep.valid) return;
        const std::optional<double> at=hit?std::optional<double>{hit_fraction(sweep,*hit)}:std::nullopt;
        const double span=length(sweep.to-sweep.from);
        for(std::size_t i=0;i<s.plane_count;++i) {
            auto& p=s.planes[i];
            if(p.tip!=tip) continue;
            const auto t=crossing(sweep,p.point,p.normal);
            if(!t) continue;   // the sweep never reached its region: no news
            if(at && (*t>=*at || (*at-*t)*span<=.002)) {if(!p.refuted && p.direct_age<=shadow_seconds) p.age=0;}
            else p.refuted=true;
        }
    }
    // Only a real surface hit makes a plane: an initial overlap or an invalid normal does not say where free space is.
    void add_plane(Side& s,const ClampHit& hit,double radius,Vec3 local,bool tip,bool answer=true) const {
        if(!plane_hit(hit)) return;
        const Plane plane{hit.point,hit.normal*(1/length(hit.normal)),local,radius,0,0,hit.entity,hit.material,tip,false,answer};
        std::size_t slot=s.plane_count;
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& p=s.planes[i];
            if(p.tip==tip && p.radius==radius && length(p.local-local)<1e-9 && dot(p.normal,plane.normal)>.99 &&
               std::abs(dot(plane.point-p.point,p.normal))<.01) {slot=i;break;}  // the same surface: a direct hit
        }
        if(slot==s.plane_count) {
            if(s.plane_count<capacity()) ++s.plane_count;
            else slot=static_cast<std::size_t>(std::max_element(s.planes.begin(),s.planes.begin()+s.plane_count,
                [](const Plane& a,const Plane& b){return a.age<b.age;})-s.planes.begin());  // full: replace the oldest
        }
        s.planes[slot]=plane;
    }
    // Pushes grip point p (oriented by basis) out of the planes by the probe's radius plus skin; adds each push to
    // `pushed` when given. direct_only: just the planes hit directly by this side's last answered result.
    // except: the planes of this entity are skipped.
    void project(const Side& s,const std::array<Vec3,3>& basis,Vec3& p,double* pushed=nullptr,bool direct_only=false,
                 std::optional<std::uintptr_t> except=std::nullopt) const {
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& plane=s.planes[i];
            if(direct_only && (plane.tip || !plane.last_answer)) continue;
            if(except && plane.entity==*except) continue;
            const Vec3 offset=clamp_detail::rotate(basis,plane.local);
            const double d=dot(p+offset-plane.point,plane.normal)-(plane.radius+cfg_.skin);
            if(d<0) {p=p-plane.normal*d;if(pushed) pushed[i]-=d;}
        }
    }
    void project3(const Side& s,const std::array<Vec3,3>& basis,Vec3& p,double* pushed=nullptr,bool direct_only=false,
                  std::optional<std::uintptr_t> except=std::nullopt) const {
        for(int pass=0;pass<3;++pass) project(s,basis,p,pushed,direct_only,except);
    }
    ClampOutput finish(Side& s,const ClampOutput& out) {s.pending_note=true;s.last_out=out;return out;}

public:
    explicit HandClamp(HandClampConfig config={}) : cfg_(config) {}
    void reset(unsigned side) {if(side<2) sides_[side]=Side{};}   // keeps the counters
    std::uint64_t snapbacks(unsigned side) const {return side<2?snapbacks_[side]:0;}
    std::uint64_t contact_frames(unsigned side) const {return side<2?contact_frames_[side]:0;}
    // Contract breaks: an update without note_drawn after the previous one, and a second update at the same `now`.
    std::uint64_t missed_notes(unsigned side) const {return side<2?missed_notes_[side]:0;}
    std::uint64_t repeated_updates(unsigned side) const {return side<2?repeated_updates_[side]:0;}
    // Frames held released because the re-acquire could not run: the chest was not finite or began inside geometry.
    std::uint64_t reacquire_blocked_frames(unsigned side) const {return side<2?blocked_frames_[side]:0;}

    // Contract: call once after every update that keeps the side (not after a yield or an invalid target), with the
    // position finally drawn (meet shift, blade push). The next frame's sweep starts there.
    void note_drawn(unsigned side,const WorldTrackedPose& root,Vec3 drawn) {
        if(side>1 || !sides_[side].has) return;
        auto& s=sides_[side];
        D2VR_HAND_CLAMP_CONTRACT(s.pending_note);
        s.pending_note=false;
        s.prev_local=clamp_detail::local_point(root,drawn);   // non-finite: the next update snaps
        s.drawn_world=drawn;
    }

    // The meet (NativeHandContact::meet): the drawn hand is the target moved out of the prop it pushes (`met`, whose planes
    // are skipped: the prop moves with the hand) instead of held off it. It is pushed out of every other plane this side
    // holds now (last update's planes, no new cast), so the meet never moves the hand through a wall the clamp holds.
    // Nothing held (a released or reset side): the meet position as it is.
    Vec3 keep_out(unsigned side,const std::array<Vec3,3>& basis,Vec3 meet_position,std::optional<std::uintptr_t> met) const {
        if(side>1 || !finite(meet_position)) return meet_position;
        const auto& s=sides_[side];
        if(!s.has || s.state==ClampState::released) return meet_position;
        project3(s,basis,meet_position,nullptr,false,met);
        return meet_position;
    }

    // Contract: one update per side per frame, `now` strictly increasing. In C14 alternate mode, pass post=false on the
    // frames this side does not cast; a frame that posts after one that did not predicts two frames ahead.
    ClampOutput update(unsigned side,const ClampInput& in) {
        ClampOutput out{};
        if(side>1) return out;
        auto& s=sides_[side];
        if(s.has && s.pending_note) {++missed_notes_[side];D2VR_HAND_CLAMP_CONTRACT(false);}
        if(s.has && !in.yield && in.now==s.last_now && in.epoch==s.epoch) {
            ++repeated_updates_[side];D2VR_HAND_CLAMP_CONTRACT(false);
            s.pending_note=true;return s.last_out;   // the same frame again: never fold its results twice
        }
        out.drawn=in.target;
        const bool target_ok=clamp_detail::finite_pose(in.target);
        if(in.yield) {reset(side);out.valid=target_ok;return out;}
        if(!target_ok || !clamp_detail::finite_pose(in.root) || !std::isfinite(in.now)) {reset(side);return out;}
        out.valid=true;
        const double elapsed=in.now-s.last_now;
        if(!s.has || in.epoch!=s.epoch || !(elapsed>=0) || elapsed>cfg_.gap_seconds) {
            // Re-acquire: nothing from before is trusted. The chest sweep validates the path to the controller.
            s=Side{};
            if(finite(in.chest)) {out.state=ClampState::free;post_chest(s,in,out);}
            else {release(s);out.state=ClampState::released;++blocked_frames_[side];}
            store(s,in,in.target.position);
            return finish(s,out);
        }
        const double dt=std::min(elapsed,.1);
        s.since_haptic+=dt;

        // What this side posted last frame decides how its results read; nothing posted, nothing to read.
        const Posted posted=s.posted;const ClampSweep answered=s.posted_palm,posted_tip=s.posted_tip;
        const ClampProbe tip_probe=s.posted_tip_probe;const bool answered_slid=s.posted_slid;
        const ClampSweep posted_exclude=s.posted_exclude;const ClampProbe exclude_probe=s.posted_exclude_probe;const bool exclude_tip=s.posted_exclude_tip;
        s.posted=Posted::none;s.posted_palm={};s.posted_tip={};s.posted_slid=false;s.posted_exclude={};
        if(s.slid_frames<1000) ++s.slid_frames;
        const std::optional<ClampHit>& slot0=in.palm_hit?in.palm_hit:in.chest_hit;
        const bool slot0_loose=slot0 && in.is_loose(slot0->entity);   // a loose prop the push logic owns
        if(posted!=Posted::none) {   // an answered cast: the planes it makes or re-hits are the fresh ones (I3)
            // A slid sweep runs along the planes it slides on and cannot re-hit them: only a raw sweep's answer renews freshness.
            if(!answered_slid) for(std::size_t i=0;i<s.plane_count;++i) s.planes[i].fresh=false;
            s.loose_touch.reset();
            if(slot0_loose) s.loose_touch=slot0->entity;
            else if(posted_tip.valid && in.tip_hit && in.is_loose(in.tip_hit->entity)) s.loose_touch=in.tip_hit->entity;
        }

        if(!finite(s.prev_local) || !finite(s.shift_local) || !finite(s.drawn_world) || !finite(s.target_world) ||
           !finite(s.prev_basis_local[0]) || !finite(s.prev_basis_local[1]) || !finite(s.prev_basis_local[2]))
            snap(side,s,out);

        // Fold in last frame's results.
        if(s.state!=ClampState::released) {
            // A long frame (a 100 ms hitch) ages a plane by at most half its life, so one hitch cannot expire a wall the
            // hand is pressed into before a sweep can confirm it again.
            const double age_step=std::min(dt,cfg_.plane_seconds*.5);
            for(std::size_t i=0;i<s.plane_count;++i) {s.planes[i].age+=age_step;s.planes[i].direct_age+=age_step;}
            // The re-acquire sweep: a chest inside geometry (crouched under a table) cannot validate the path.
            if(posted==Posted::chest && slot0 && slot0->initial_overlap && !slot0_loose) release(s);
            else if(posted!=Posted::none && answered.valid) {
                const double t=slot0?hit_fraction(answered,*slot0):1.;
                s.seg_from=answered.from;s.seg_to=answered.from+(answered.to-answered.from)*t;s.has_segment=true;
                s.seg_cut=slot0.has_value();
                if(answered_slid) {
                    s.slid_from=s.seg_from;s.slid_to=s.seg_to;s.slid_cut=s.seg_cut;s.slid_frames=0;s.slid_window=s.posted_slid_window;
                }
                if(posted==Posted::palm) {
                    s.last_overlap=slot0 && slot0->initial_overlap;
                    if(!s.last_overlap) {s.clear_from_local=clamp_detail::local_point(in.root,answered.from);s.has_clear_from=true;}
                }
                for(std::size_t i=0;i<s.plane_count;++i) if(!s.planes[i].tip) s.planes[i].last_answer=false;
                if(posted==Posted::palm) judge(s,answered,slot0,false);
                if(slot0) add_plane(s,*slot0,cfg_.palm_radius,{},false);
                if(posted==Posted::palm) {
                    // A hit that makes no plane (invalid normal) neither counts nor clears: the recovery sweep must not
                    // reset the deep-start count while the hand stays inside a surface it cannot describe.
                    // An overlap with a loose prop never counts (I2): the push logic owns a prop on the hand.
                    if(slot0 && slot0->initial_overlap) {if(!slot0_loose) ++s.overlap_count;}
                    else if(!slot0 || plane_hit(*slot0)) s.overlap_count=0;
                    if(cfg_.overlap_frames && s.overlap_count>=cfg_.overlap_frames) snap(side,s,out);
                }
            }
            if(s.state!=ClampState::released && posted_tip.valid) {
                judge(s,posted_tip,in.tip_hit,true);
                if(in.tip_hit) add_plane(s,*in.tip_hit,tip_probe.radius,tip_probe.local,true);
            }
            // The exclusion sweep (I1) only adds what lies behind the met prop: it judges nothing (it passes through the
            // prop's planes by design) and does not steer the next sweep's slide.
            if(s.state!=ClampState::released && posted_exclude.valid && in.exclude_hit && !in.exclude_hit->initial_overlap &&
               !(posted_exclude.exclude && in.exclude_hit->entity==*posted_exclude.exclude)) {
                if(exclude_tip) add_plane(s,*in.exclude_hit,exclude_probe.radius,exclude_probe.local,true);
                else add_plane(s,*in.exclude_hit,cfg_.palm_radius,{},false,false);
            }
            std::size_t kept=0;
            for(std::size_t i=0;i<s.plane_count;++i) if(s.planes[i].age<=cfg_.plane_seconds) s.planes[kept++]=s.planes[i];
            s.plane_count=kept;
        }

        bool rearmed=false;
        if(s.state==ClampState::released) {
            const bool chest_inside=posted==Posted::chest && slot0 && slot0->initial_overlap && !slot0_loose;
            // A re-acquire stopped only by a loose prop is clear (I2): the side re-arms within rearm_seconds.
            if(posted==Posted::chest) s.clear_time=slot0 && !slot0_loose?0:s.clear_time+dt;
            if(chest_inside || !finite(in.chest)) ++blocked_frames_[side];
            if(s.clear_time<cfg_.rearm_seconds-1e-9) {released_frame(s,in,out);return finish(s,out);}
            // Re-arm: the chest-to-controller path stayed clear, so the controller is in free space. prev = target.
            s.state=ClampState::free;s.clear_time=0;s.plane_count=0;s.shift_local={};s.overlap_count=0;s.over_time=0;
            s.hold_frames=0;s.has_segment=false;s.slid_frames=1000;s.has_clear_from=false;s.last_overlap=false;
            s.prev_local=clamp_detail::local_point(in.root,in.target.position);
            for(std::size_t i=0;i<3;++i) s.prev_basis_local[i]=clamp_detail::local_vector(in.root,in.target.basis[i]);
            s.drawn_world=in.target.position;s.basis_world=in.target.basis;
            rearmed=true;
        }

        // The constraint.
        const Vec3 target=in.target.position;
        std::array<double,plane_capacity> pushed{};
        Vec3 p=target;
        project3(s,in.target.basis,p,pushed.data());
        const Vec3 wanted=p-target;
        Vec3 shift=clamp_detail::rotate(in.root.basis,s.shift_local);
        // Engage instantly (never render inside a surface); ease out at release_speed when the push shrinks.
        shift=length(wanted)>=length(shift)?wanted:shift+clamp_detail::clamp_length(wanted-shift,cfg_.release_speed*dt);
        p=target+shift;
        project(s,in.target.basis,p,pushed.data());   // the eased hand never enters a plane
        shift=p-target;

        // Draw only what the answered sweep found free. A target off that path (a jump, a hitch, a snap turn) may be
        // behind a surface no sweep has met yet: keep last frame's hand, out of the known planes, for up to
        // max_hold_frames while this frame's sweep validates the way. These frames do not count toward snap-back.
        // The last answered sweep, or the last slid one while its lead still covers this frame (a raw sweep pressed into
        // a surface is cut short at its hit), or, after a sweep that met nothing, a step of at most palm_radius from a
        // hand drawn validated last frame (a plane that is being refuted can hold the hand off the raw sweep's path).
        // Once the holds run out the hand follows unvalidated until a frame validates again.
        const bool validated=rearmed || !s.has_segment || on_segment(p,s.seg_from,s.seg_to,s.seg_cut) ||
            (s.slid_frames<=s.slid_window && on_segment(p,s.slid_from,s.slid_to,s.slid_cut)) ||
            (!s.seg_cut && s.drawn_validated && length(p-s.drawn_world)<=cfg_.palm_radius+1e-9);
        bool hold=false;Vec3 held=s.drawn_world;
        // A hold waits for max_hold_frames casts: in C14 alternate mode a cast is answered every other frame.
        if(!validated && s.hold_frames<max_hold_frames*s.cadence) {project3(s,in.target.basis,held);hold=true;}
        s.drawn_validated=hold || validated;
        if(hold) {
            ++s.hold_frames;out.held=true;out.drawn.position=held;out.shift=held-target;
        } else {
            if(validated) s.hold_frames=0;
            s.over_time=length(shift)>cfg_.snap_gap?s.over_time+dt:0;
            if(s.over_time>cfg_.snap_seconds) {snap(side,s,out);released_frame(s,in,out);return finish(s,out);}
            out.drawn.position=p;out.shift=shift;
            s.shift_local=clamp_detail::local_vector(in.root,shift);
        }

        // World velocity, the player's own motion included (C8): the result lands after the root has moved too.
        const Vec3 velocity=dt>0?(target-s.target_world)*(1/dt):Vec3{};
        std::size_t strongest=plane_capacity;
        for(std::size_t i=0;i<s.plane_count;++i) if(pushed[i]>1e-9 && (strongest==plane_capacity || pushed[i]>pushed[strongest])) strongest=i;
        if(strongest<plane_capacity) {
            const auto& plane=s.planes[strongest];
            out.contact=ClampHit{plane.point,plane.normal,plane.entity,plane.material,false,true};
            out.contact_fresh=plane.fresh;
            out.approach_speed=std::max(0.,-dot(velocity,plane.normal));
        }
        const bool was_contact=s.state==ClampState::contact;
        s.state=out.contact || length(shift)>1e-4?ClampState::contact:ClampState::free;
        out.state=s.state;
        if(s.state==ClampState::contact) {
            ++contact_frames_[side];
            if(!was_contact && out.contact) {
                const double speed=out.approach_speed;
                out.event=CollisionEvent{out.contact->point,out.contact->normal,speed,out.contact->entity,false,side,out.contact->material};
                if(s.since_haptic>=cfg_.haptic_cooldown) {
                    out.haptic=CollisionHaptic{static_cast<float>(std::clamp(.2+.8*speed/cfg_.speed_for_full,.2,1.)),
                        static_cast<float>(std::clamp(.03+.02*speed,.03,.12))};
                    s.since_haptic=0;
                }
            }
        }

        // Sweep from the previous drawn hand (carried by the root, C11; pushed out of the planes known now, since a late
        // result can leave it inside one) to one frame of world motion ahead of the target (C8), two after a frame
        // without a cast. A hold sweeps from the held hand to the target slid along the known planes.
        // After this side's last answered result hit a plane directly, the next sweep slides along the planes it hit
        // (in C14 alternate mode that result landed on the frame between posts) instead (two leads ahead, since every
        // other sweep slides): the closest-hit cast reports one surface, so this is how a second surface along the slide
        // (a corner, a crate on a table) is met directly, and how a shadowed plane that is gone gets refuted.
        // A raw sweep without a direct hit is followed by another raw sweep, so raw sweeps find the frames with results.
        if(in.post) {
            const double lead=posted==Posted::none?2.:1.;
            // The cap scales with the cadence: in C14 alternate mode a sweep must cover twice the frames.
            const auto ahead_for=[&](double frames) {return clamp_detail::clamp_length(velocity*(dt*frames),max_lead*lead);};
            const Vec3 ahead=ahead_for(lead);
            // Recovery: the last palm sweep began inside something no plane describes (an overlap makes no plane), so a
            // sweep from the hand would again. Sweep from the start of the last palm sweep that began free instead.
            const bool recover=s.last_overlap && s.has_clear_from;
            std::array<Vec3,3> from_basis=s.basis_world;Vec3 from=held;
            if(!hold || recover) {
                for(std::size_t i=0;i<3;++i) from_basis[i]=clamp_detail::rotate(in.root.basis,s.prev_basis_local[i]);
                from=clamp_detail::world_point(in.root,recover?s.clear_from_local:s.prev_local);
                project3(s,from_basis,from);
            }
            Vec3 end=target+ahead;bool slid=false;
            if(hold || recover) {end=target+ahead_for(2*lead);project3(s,in.target.basis,end);slid=true;}   // along all known planes
            else if(!s.last_slid) {
                for(std::size_t i=0;i<s.plane_count;++i) if(!s.planes[i].tip && s.planes[i].last_answer) slid=true;
                if(slid) {end=target+ahead_for(2*lead);project3(s,in.target.basis,end,nullptr,true);}
            }
            s.last_slid=slid;
            const ClampSweep palm{from,end,cfg_.palm_radius,true};
            if(finite(palm.from) && finite(palm.to)) {
                out.palm_sweep=palm;s.posted=Posted::palm;s.posted_palm=palm;s.posted_slid=slid;
                s.posted_slid_window=static_cast<unsigned>(2*lead);s.cadence=static_cast<unsigned>(lead);
            }
            if(in.tip_enabled && clamp_detail::usable(in.tip)) {
                const ClampSweep tip{from+clamp_detail::rotate(from_basis,in.tip.local),
                                     end+clamp_detail::rotate(in.target.basis,in.tip.local),in.tip.radius,true};
                if(finite(tip.from) && finite(tip.to)) {out.tip_sweep=tip;s.posted_tip=tip;s.posted_tip_probe=in.tip;}
            }
            // I1: the leading probe (the palm without one) from the hand to the unslid target, the met prop excluded. A loose prop
            // the last answered cast met (it hides the wall behind it from the closest-hit sweeps) is excluded when no prop is met.
            const std::optional<std::uintptr_t> excluded=in.exclude?in.exclude:s.loose_touch;
            if(excluded && out.palm_sweep.valid) {
                const bool tip_probe_used=in.tip_enabled && clamp_detail::usable(in.tip);
                const Vec3 local=tip_probe_used?in.tip.local:Vec3{};
                ClampSweep ex{from+clamp_detail::rotate(from_basis,local),target+ahead+clamp_detail::rotate(in.target.basis,local),
                              tip_probe_used?in.tip.radius:cfg_.palm_radius,true,excluded};
                if(finite(ex.from) && finite(ex.to)) {
                    out.exclude_sweep=ex;s.posted_exclude=ex;s.posted_exclude_tip=tip_probe_used;
                    s.posted_exclude_probe=tip_probe_used?in.tip:ClampProbe{{},cfg_.palm_radius};
                }
            }
        }
        store(s,in,out.drawn.position);
        return finish(s,out);
    }
};

// How deep the tracked grip is past the surface the clamp holds it at: the hold beyond the palm shell (palm_radius + skin).
// NativeHandContact::meet latches on it as on the contact's depth: a tracked hand pushed on through a pinned or toppled
// prop can overlap it only shallowly (under a fallen bottle) while the clamp holds the drawn hand 20 cm back.
inline double clamp_depth(const ClampOutput& out,const HandClampConfig& cfg={}) {
    if(!out.valid || out.state!=ClampState::contact || !out.contact) return 0;
    const double depth=length(out.shift)-(cfg.palm_radius+cfg.skin);
    return std::isfinite(depth)?std::max(0.,depth):0.;
}

// Where the hand is drawn, the clamp and the meet composed (C8, C9). meeting: the meet moves this hand
// (NativeHandContact::Meeting::prop, or a non-zero shift); prop: the pushable prop the clamp holds the hand at (the cast
// entity when Meeting::prop, else none); shift: Meeting::shift. Not meeting (latched, pinned, nothing met): the clamp's
// hand as it is. Meeting: the hand the meet starts from is the target while the clamp's contact is the met prop (or there
// is none), else the clamp's hand (it holds the hand at something else: a wall); it is moved by the shift, then pushed out
// of every plane the clamp holds but the prop's, so a meet never moves the hand through a surface the clamp holds.
inline Vec3 meet_drawn(const HandClamp& clamp,unsigned side,const ClampOutput& out,const WorldTrackedPose& target,bool meeting,
                       std::optional<std::uintptr_t> prop,Vec3 shift) {
    if(!meeting || !out.valid) return out.drawn.position;
    const bool held_elsewhere=out.contact && !(prop && out.contact->entity==*prop);
    // The way out leaves the prop: it never carries the hand further into the prop than the clamp's plane on it says (a
    // stale or flipped way out, the tracked hand past the prop's middle, would push the hand on through it and beyond).
    if(!held_elsewhere && out.contact) {const double into=dot(shift,out.contact->normal);if(std::isfinite(into) && into<0) shift=shift-out.contact->normal*into;}
    return clamp.keep_out(side,target.basis,(held_elsewhere?out.drawn.position:target.position)+shift,prop);
}

} // namespace d2vr
