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
// What the hit body is, read from the body itself when the cast answered: the world (no entity or body), an entity that does not move
// (static or keyframed: a door, an animated actor) or a dynamic body (motion type 2, a prop of any mass). Ranked world > entity_static >
// dynamic by the clamp. A body that could not be read is entity_static.
enum class BodyClass : unsigned char {world,entity_static,dynamic};
struct ClampHit {Vec3 point{},normal{};std::uintptr_t entity{};std::uint16_t material{0xffff};bool initial_overlap{};bool normal_valid{true};BodyClass body{BodyClass::world};};
// exclude: the body (its entity) the cast must not report (the native cast's source-body exclusion, request +0x68/+0x14).
struct ClampSweep {Vec3 from{},to{};double radius{};bool valid{};std::optional<std::uintptr_t> exclude;};
struct HandClampConfig {
    double palm_radius=.05,skin=.005,plane_seconds=.1,release_speed=1.5,snap_gap=.35,snap_seconds=.25,
           rearm_seconds=.1,gap_seconds=.25,haptic_cooldown=.08,speed_for_full=3.,
           look_ahead=.10;   // how far past the target the wall and exclusion probes sweep along the horizontal motion
    unsigned overlap_frames=3,max_planes=4;};
struct ClampInput {
    double now{};                       // monotonic seconds; per-side dt is derived from it
    std::uint64_t epoch{};              // pose_epoch ^ (reference_epoch<<32) ^ player; a change resets the side
    WorldTrackedPose root{};            // level player frame (C11, deviation 9)
    WorldTrackedPose target{};          // calibrated, reach-clamped controller grip
    Vec3 chest{};                       // free-space re-acquire origin
    std::optional<ClampHit> palm_hit,tip_hit,chest_hit,wall_tip_hit; // results of the sweeps this side posted last frame
    ClampProbe tip{};bool tip_enabled{true},post{true},yield{};
    // The probe that leads the motion's horizontal part (wall_leading_probe), swept as well while it is not `tip`: a wall meets
    // first the probe that reaches farthest horizontally, however steeply the hand rises or falls. Radius 0: none. tip_enabled
    // (C14) drops it with the tip.
    ClampProbe wall_tip{};
    // Loose props the push logic owns (light, in reach, pinned or not; NativeHandContact::fill_clamp_input): an initial
    // overlap with one never counts toward the overlap snap, and a re-acquire sweep stopped by one does not keep the side
    // released (a toppled bottle on the hand is pushed, not a reason to let go of the walls).
    static constexpr std::size_t loose_capacity=8;
    std::array<std::uintptr_t,loose_capacity> loose{};std::size_t loose_count{};
    // The met prop (I1): this side also sweeps its leading probe with that body excluded, so a wall behind the prop is held
    // as a plane (the closest-hit sweeps only ever report the prop in front of it). exclude_hit: last frame's result.
    std::optional<std::uintptr_t> exclude;std::optional<ClampHit> exclude_hit;
    // The open hand's spheres in the grip frame (the set leading_probe chooses from). Every held plane keeps all of them out (and the
    // probe that found it): a plane is a surface, not the probe that found it (sliding along a wall the side probes lead, and the forward
    // fingers went 6 cm into the wall once only the palm was held). None: a plane holds only the probe that hit it.
    static constexpr std::size_t hand_capacity=32;
    std::array<ClampProbe,hand_capacity> hand{};std::size_t hand_count{};
    // The way out the meet will draw the hand along (world, from the contact service; zero: none). With the hand at rest it says which way
    // the wall tip's sweep must look.
    Vec3 kick{};
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
    ClampSweep wall_tip_sweep;          // ClampInput::wall_tip while it is another probe than the tip; while a prop is excluded (I1), the
                                        // wall tip (or the tip) with that prop excluded (ClampSweep::exclude): the caller honours exclude
    ClampSweep exclude_sweep;           // the probe leading into the excluded prop's pressed face, that prop excluded (ClampInput::exclude)
    bool held{};                        // drawn at last frame's hand while this frame's sweep validates a jump
    bool contact_fresh{};               // `contact`'s plane was made or re-hit within the last two answered raw casts (I3)
    bool reacquired{};};                // this update re-acquired the side (first update, a gap, an epoch change, a reset): drawn == target, unswept

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
inline bool same_probe(const ClampProbe& a,const ClampProbe& b) {
    return a.local.x==b.local.x && a.local.y==b.local.y && a.local.z==b.local.z && a.radius==b.radius;
}
// A probe in the lead is kept while no other leads it by more than this (it then reaches at most this much less far): between
// two near-equal probes the choice would flicker (a 45 degree push switched probes 39 times in one run).
constexpr double lead_margin=.005;
constexpr double lead_speed=.05;   // below it the motion says nothing about the lead: `current` stays
// The probe of open_hand that reaches farthest along unit `direction` (dot(R*local, direction) + radius; the first wins a tie),
// or `current` while it is one of them and within lead_margin of that. No usable probe: `current`.
template<std::size_t N> ClampProbe lead_along(const std::array<ClampProbe,N>& open_hand,const WorldTrackedPose& grip,Vec3 direction,ClampProbe current) {
    const ClampProbe* best=nullptr;double score=-std::numeric_limits<double>::infinity(),kept=score;
    for(const auto& probe:open_hand) {
        if(!usable(probe)) continue;
        const double s=dot(rotate(grip.basis,probe.local),direction)+probe.radius;
        if(!std::isfinite(s)) continue;
        if(!best || s>score) {best=&probe;score=s;}
        if(same_probe(probe,current)) kept=s;
    }
    if(!best) return current;
    return kept>=score-lead_margin?current:*best;
}
} // namespace clamp_detail

// The level player frame: forward flattened onto the ground plane, z up. {1,0,0} when forward is vertical or not finite.
inline WorldTrackedPose level_frame(Vec3 position,Vec3 forward) {
    Vec3 f{forward.x,forward.y,0};
    const double l=std::hypot(f.x,f.y);
    f=std::isfinite(l) && l>1e-6?f*(1/l):Vec3{1,0,0};
    return {position,{f,{-f.y,f.x,0},{0,0,1}}};
}

// The open-hand probe that leads the motion (ClampInput::tip): the one that reaches farthest along the motion's own direction,
// dot(R*local, motion/|motion|) + radius (the first wins a tie). `current` (last frame's choice, or a default) is kept while it
// is one of open_hand and within lead_margin of the leader; below 0.05 m/s, or for a non-finite motion or grip, it is kept as is.
template<std::size_t N> ClampProbe leading_probe(const std::array<ClampProbe,N>& open_hand,const WorldTrackedPose& grip,Vec3 motion,ClampProbe current) {
    const double speed=length(motion);
    if(!std::isfinite(speed) || speed<clamp_detail::lead_speed || !clamp_detail::finite_pose(grip)) return current;
    return clamp_detail::lead_along(open_hand,grip,motion*(1/speed),current);
}
// The open-hand probe that leads the motion's horizontal part (ClampInput::wall_tip; world z up): walls are vertical, so the
// probe that meets one first reaches farthest horizontally, however steeply the hand rises or falls (rising at 45 degrees or
// more, the forward thumb went 7-8 cm into the wall while an upper probe led the motion; descending at 0.3 of the speed, a
// hanging finger led it and the thumb went 2.5 cm in). Kept like leading_probe; also while the horizontal part is under 1 cm/s.
template<std::size_t N> ClampProbe wall_leading_probe(const std::array<ClampProbe,N>& open_hand,const WorldTrackedPose& grip,Vec3 motion,ClampProbe current) {
    const double speed=length(motion),sideways=std::hypot(motion.x,motion.y);
    if(!std::isfinite(speed) || speed<clamp_detail::lead_speed || !(sideways>=.01) || !clamp_detail::finite_pose(grip)) return current;
    return clamp_detail::lead_along(open_hand,grip,Vec3{motion.x/sideways,motion.y/sideways,0},current);
}

// C14: the clamp's cast cost per frame. A rolling mean over window_seconds. Every decision needs a covered window: at least
// min_samples since the window started, and at least window_seconds of continuous samples. A gap of more than max_gap between
// samples restarts the window, as reset_window() does (an epoch change, a world-UI pause): one slow first scope after a load decides
// nothing. Over budget_us on a covered window, the fingertip sweeps go first, then the hands alternate frames. They come back in the
// reverse order only when the cost predicted for the better mode stays under restore_us for the recovery wait: the mean now times
// restore_factor, the ratio measured over the first covered window after that step down (better-mode mean / this mode's; default 2,
// kept in [1.2, 2.5]). Each step down that follows a recovery doubles the wait (5, 10, 20 ... 120 s), so a scene that does not fit
// the budget in the better mode cannot flip it back every few seconds (a tip that comes and goes clips walls every few seconds).
class CastBudget {
public:
    static constexpr double budget_us=500,window_seconds=2,restore_us=400,default_restore_factor=2,min_restore_factor=1.2,
                            max_restore_factor=2.5,recover_seconds=5,max_recover_seconds=120,max_gap=.25;
    static constexpr std::size_t min_samples=60;
    void note(double frame_us,double now) {
        if(!std::isfinite(frame_us) || frame_us<0 || !std::isfinite(now)) return;
        if(!started_ || now<last_ || now-last_>max_gap) restart(now);
        last_=now;
        samples_.push_back({now,frame_us});++counted_;
        while(samples_.front().now<=now-window_seconds) samples_.pop_front();
        double sum=0,peak=0;
        for(const auto& s:samples_) {sum+=s.us;peak=std::max(peak,s.us);}
        mean_=sum/static_cast<double>(samples_.size());max_=peak;
        const unsigned level=alternate_?2u:tip_?0u:1u;
        const bool covered=now-start_>=window_seconds-1e-9 && counted_>=min_samples;
        if(!covered) {quiet_=false;return;}
        // The ratio this step down bought, measured once over its first covered window.
        if(level && !measured_[level] && mean_>0) {
            measured_[level]=true;factor_[level]=std::clamp(before_[level]/mean_,min_restore_factor,max_restore_factor);
        }
        const double predicted=level?mean_*factor_[level]:mean_;
        if(level && predicted<restore_us) {if(!quiet_) {quiet_=true;quiet_start_=now;}} else quiet_=false;
        if(mean_>budget_us && level<2) {
            const unsigned next=level+1;
            if(recovered_) recover_wait_=std::min(recover_wait_*2,max_recover_seconds);
            recovered_=false;before_[next]=mean_;measured_[next]=false;factor_[next]=default_restore_factor;
            if(tip_) tip_=false; else alternate_=true;
            restart(now);   // the next decision measures the new mode for a covered window
        } else if(level && quiet_ && now-quiet_start_>=recover_wait_-1e-9) {
            if(alternate_) alternate_=false; else tip_=true;
            recovered_=true;
            restart(now);
        }
    }
    // An epoch change or a world-UI pause: the next decision waits for a new covered window (the modes and the backoff stay).
    void reset_window() {started_=false;samples_.clear();quiet_=false;counted_=0;}
    bool tip_enabled() const {return tip_;}
    bool alternate() const {return alternate_;}
    double mean_us() const {return mean_;}
    double max_us() const {return max_;}
    double recover_wait() const {return recover_wait_;}
    double restore_factor() const {const unsigned level=alternate_?2u:tip_?0u:1u;return level?factor_[level]:1.;}
    void reset() {*this=CastBudget{};}
private:
    void restart(double now) {started_=true;start_=now;samples_.clear();quiet_=false;counted_=0;}
    struct Sample {double now,us;};
    std::deque<Sample> samples_;
    double start_{},last_{},mean_{},max_{},quiet_start_{},recover_wait_{recover_seconds};
    std::size_t counted_{};
    std::array<double,3> before_{},factor_{1,default_restore_factor,default_restore_factor};std::array<bool,3> measured_{};
    bool started_{},tip_{true},alternate_{},quiet_{},recovered_{};
};

class HandClamp {
public:
    static constexpr double shadow_seconds=.2;     // a plane kept only by shadowing gets this long past its last direct hit
    static constexpr double contact_margin=.01;    // a plane the drawn hand is this close to does not age while no sweep reaches it
    static constexpr double max_lead=.15;          // the predictive extension never reaches farther per cast cadence
                                                   // (a jump is not extrapolated)
    static constexpr unsigned max_hold_frames=2;   // frames the hand may wait for a sweep to validate an unvalidated target
    static constexpr unsigned fresh_misses=2;      // a plane stops being fresh after this many answered raw casts in a row miss it:
                                                   // one stale cast is survived (a 4-5 m/s swat re-hits a fleeing prop every other cast)
private:
    enum class Posted : unsigned char {none,palm,chest};
    struct Plane {
        Vec3 point{},normal{},local{};double radius{},age{},direct_age{};
        std::uintptr_t entity{};std::uint16_t material{0xffff};bool tip{},refuted{};
        bool last_answer{};   // hit directly by this side's last answered result (it lands a frame late, or two in C14)
        unsigned misses{};    // answered raw (unslid) casts since it was made or last re-hit (I3; frames without a result do not count)
        bool touched{},news{};   // this frame: the drawn hand was within contact_margin of it; a sweep reached its region (judge)
        bool loose{};            // a loose prop's face (ClampInput::loose when it was hit): it moves out of the hand's way, a wall does not
        unsigned char rank{2};   // 2 the world, 1 an entity that does not move, 0 a dynamic body or a loose prop (BodyClass of the hit): higher outranks lower
    };
    static constexpr std::size_t plane_capacity=8;
    struct Side {
        bool has{};std::uint64_t epoch{};double last_now{};ClampState state{ClampState::free};
        Vec3 prev_local{},shift_local{};std::array<Vec3,3> prev_basis_local{};  // root (level player) frame, C11
        std::array<ClampProbe,ClampInput::hand_capacity> hand{};std::size_t hand_count{};   // ClampInput::hand, this frame's
        Vec3 own_shift_local{};   // the clamp's own eased push: shift_local without what callers drew on top (note_drawn); the snap-back gap
        Vec3 chest_world{};   // the re-acquire origin of the last update (the end of a retreat)
        Vec3 drawn_world{},target_world{};std::array<Vec3,3> basis_world{};     // last frame in the world
        std::array<Plane,plane_capacity> planes{};std::size_t plane_count{};
        double over_time{},clear_time{},since_haptic{1e9};unsigned overlap_count{},hold_frames{};
        Posted posted{Posted::none};bool last_slid{};ClampSweep posted_palm{},posted_tip{};ClampProbe posted_tip_probe{};
        ClampSweep posted_wall{};ClampProbe posted_wall_probe{};
        ClampSweep posted_exclude{};ClampProbe posted_exclude_probe{};bool posted_exclude_tip{};
        // A loose prop an answered raw cast hit or started in within the last plane_seconds (excluded from the exclusion sweeps): while
        // the wall tip sweeps with it excluded, nothing tells that probe still overlaps it, and a plain sweep from inside it every
        // other frame found the wall behind it a frame late (1.8 cm into the wall at 3 m/s, descending at 75 degrees).
        std::optional<std::uintptr_t> loose_touch;double loose_touch_age{};
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
    std::array<std::uint64_t,2> squeeze_infeasible_{},snapbacks_{},contact_frames_{},missed_notes_{},repeated_updates_{},blocked_frames_{};

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
        s.state=ClampState::released;s.plane_count=0;s.shift_local={};s.own_shift_local={};
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
            p.news=true;
            if(at && (*t>=*at || (*at-*t)*span<=.002)) {if(!p.refuted && p.direct_age<=shadow_seconds) p.age=0;}
            else p.refuted=true;
        }
    }
    // Only a real surface hit makes a plane: an initial overlap or an invalid normal does not say where free space is.
    // loose: the hit is on a loose prop (ClampInput::loose). A prop moves: a new hit on a face the side already holds (same
    // probe, parallel normal) is that face where it is now, and replaces the old plane wherever it was (a swatted bottle
    // otherwise leaves a trail of its old faces that hold the hand 30 cm behind it).
    void add_plane(Side& s,const ClampHit& hit,double radius,Vec3 local,bool tip,bool answer=true,bool loose=false) const {
        if(!plane_hit(hit)) return;
        Plane plane{hit.point,hit.normal*(1/length(hit.normal)),local,radius,0,0,hit.entity,hit.material,tip,false,answer};
        plane.loose=loose;plane.rank=loose?0:hit.body==BodyClass::dynamic?0:hit.body==BodyClass::entity_static?1:2;
        std::size_t slot=s.plane_count;
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& p=s.planes[i];
            if(p.tip==tip && p.radius==radius && length(p.local-local)<1e-9 && dot(p.normal,plane.normal)>.99 &&
               (std::abs(dot(plane.point-p.point,p.normal))<.01 || (loose && p.entity==hit.entity))) {slot=i;break;}  // the same surface: a direct hit
        }
        if(slot==s.plane_count) {
            if(s.plane_count<capacity()) ++s.plane_count;
            else {   // full: a plane that has outlived plane_seconds goes first (this update's purge comes after its hits are folded in, and a live hit is
                     // never dropped for a stale plane); else a plane replaces the oldest plane of the lowest rank that does not outrank it, and is dropped when every plane held
                     // outranks it (the world over an entity that does not move over a dynamic body, whatever its mass)
                std::size_t pick=s.plane_count;
                for(std::size_t i=0;i<s.plane_count;++i) {
                    const auto& q=s.planes[i];
                    // A plane the hand rests at is rescued this update, unless a sweep judged it already (news: reached and shadowed; refuted)
                    const auto stale_plane=[&](const Plane& x) {return x.age>cfg_.plane_seconds && !(x.touched && !x.news && !x.refuted);};
                    const bool stale=stale_plane(q);
                    const bool picked_stale=pick!=s.plane_count && stale_plane(s.planes[pick]);
                    if(stale) {if(pick==s.plane_count || !picked_stale || q.age>s.planes[pick].age) pick=i;continue;}
                    if(picked_stale) continue;
                    if(q.rank>plane.rank) continue;
                    if(pick==s.plane_count || q.rank<s.planes[pick].rank || (q.rank==s.planes[pick].rank && q.age>s.planes[pick].age)) pick=i;
                }
                if(pick==s.plane_count) return;
                slot=pick;
            }
        }
        s.planes[slot]=plane;
    }
    // Pushes grip point p (oriented by basis) out of the planes by the probe's radius plus skin; adds each push to
    // `pushed` when given. direct_only: just the planes hit directly by this side's last answered result.
    // except: the planes of this entity are skipped.
    // How far the hand reaches into a plane from the grip (oriented by basis): the most any of its spheres reaches along -normal
    // (dot(R*local, -normal) + radius). The probe that made the plane, and with an open hand (ClampInput::hand) every one of its
    // spheres too.
    double reach(const Side& s,const std::array<Vec3,3>& basis,const Plane& plane) const {
        const Vec3 into=plane.normal*-1.;
        double r=dot(clamp_detail::rotate(basis,plane.local),into)+plane.radius;
        if(!s.hand_count) return r;
        for(std::size_t k=0;k<std::min(s.hand_count,s.hand.size());++k) {
            const auto& probe=s.hand[k];
            if(!clamp_detail::usable(probe)) continue;
            const double q=dot(clamp_detail::rotate(basis,probe.local),into)+probe.radius;
            if(std::isfinite(q)) r=std::max(r,q);
        }
        return r;
    }
    // How far grip point p (oriented by basis) is outside the plane's skin: negative when the hand is inside.
    double clearance(const Side& s,const std::array<Vec3,3>& basis,Vec3 p,const Plane& plane) const {
        return dot(p-plane.point,plane.normal)-reach(s,basis,plane)-cfg_.skin;
    }
    // Pushes grip point p (oriented by basis) out of the planes held, each by the hand's reach into it plus skin; adds each push to `pushed`
    // when given. direct_only: just the planes hit directly by this side's last answered result. except: the planes of this entity are skipped.
    // Static geometry (every plane that is not a prop's) outranks any prop: where the planes cannot all be satisfied (the hand is wider than the
    // slot between a wall and a crate) the hand stays out of the static ones and enters the prop. Order does not matter: a prop's faces are
    // projected first and static ones last, pass after pass until nothing is deeper than converged (12 passes at most); a corner too sharp for that
    // is solved exactly (the least move that satisfies the planes, as a small QP over at most three active planes).
    // False when the world's planes alone cannot be satisfied (two walls closer than the hand): p is then left at the last pass, and the caller
    // retreats (retreat()). An entity that does not move and a dynamic body are met as far as the tiers above them allow.
    static constexpr unsigned max_passes=12;
    static constexpr double converged=5e-4;
    bool solve(const Side& s,const std::array<Vec3,3>& basis,Vec3& p,double* pushed=nullptr,bool direct_only=false,
               std::optional<std::uintptr_t> except=std::nullopt) const {
        struct Row {Vec3 n;double b;std::size_t plane;unsigned char rank;bool dynamic;};   // n . x >= b
        std::array<Row,plane_capacity> rows{};std::size_t m=0;
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& plane=s.planes[i];
            if(direct_only && (plane.tip || !plane.last_answer)) continue;
            if(except && plane.entity==*except) continue;
            rows[m++]={plane.normal,dot(plane.normal,plane.point)+reach(s,basis,plane)+cfg_.skin,i,plane.rank,plane.rank==0};
        }
        const Vec3 start=p;
        std::array<double,plane_capacity> pushed_before{};
        if(pushed) std::copy(pushed,pushed+plane_capacity,pushed_before.begin());
        const auto worst=[&](Vec3 x,bool statics_only) {
            double w=0;
            for(std::size_t k=0;k<m;++k) if(!statics_only || !rows[k].dynamic) w=std::max(w,rows[k].b-dot(rows[k].n,x));
            return w;
        };
        for(unsigned pass=0;pass<max_passes;++pass) {
            for(unsigned char level=0;level<3;++level)   // dynamic bodies first, the world last
                for(std::size_t k=0;k<m;++k) {
                    if(rows[k].rank!=level) continue;
                    const double d=dot(rows[k].n,p)-rows[k].b;
                    if(d<0) {p=p-rows[k].n*d;if(pushed) pushed[rows[k].plane]-=d;}
                }
            if(worst(p,false)<=converged) return true;
        }
        // Not converged: the least move from where p started that satisfies a set of rows (all of them, else the static ones and as many of
        // the props' as fit, the deepest first).
        const auto exact=[&](const std::array<bool,plane_capacity>& use,Vec3& out) {
            std::array<std::size_t,plane_capacity> ix{};std::size_t u=0;
            for(std::size_t k=0;k<m;++k) if(use[k]) ix[u++]=k;
            bool found=false;double best=1e300;
            const auto try_set=[&](std::size_t count,const std::array<std::size_t,3>& pick) {
                // Equalities n_a . x = b_a for the picked rows: x = start + sum l_a n_a, G l = b - n . start, G the Gram matrix.
                std::array<std::array<double,3>,3> g{};std::array<double,3> r{},l{};
                for(std::size_t a=0;a<count;++a) {
                    r[a]=rows[pick[a]].b-dot(rows[pick[a]].n,start);
                    for(std::size_t c=0;c<count;++c) g[a][c]=dot(rows[pick[a]].n,rows[pick[c]].n);
                }
                // Gauss elimination with partial pivoting.
                for(std::size_t col=0;col<count;++col) {
                    std::size_t piv=col;
                    for(std::size_t row=col+1;row<count;++row) if(std::abs(g[row][col])>std::abs(g[piv][col])) piv=row;
                    if(std::abs(g[piv][col])<1e-9) return;
                    std::swap(g[piv],g[col]);std::swap(r[piv],r[col]);
                    for(std::size_t row=col+1;row<count;++row) {
                        const double f=g[row][col]/g[col][col];
                        for(std::size_t c=col;c<count;++c) g[row][c]-=f*g[col][c];
                        r[row]-=f*r[col];
                    }
                }
                for(std::size_t a=count;a-->0;) {
                    double v=r[a];for(std::size_t c=a+1;c<count;++c) v-=g[a][c]*l[c];
                    l[a]=v/g[a][a];
                }
                Vec3 x=start;
                for(std::size_t a=0;a<count;++a) {if(l[a]<-1e-9) return;x=x+rows[pick[a]].n*l[a];}
                for(std::size_t q=0;q<u;++q) if(rows[ix[q]].b-dot(rows[ix[q]].n,x)>1e-9) return;
                const double moved=length(x-start);
                if(moved<best) {best=moved;out=x;found=true;}
            };
            {Vec3 x=start;bool ok=true;for(std::size_t q=0;q<u;++q) ok=ok && rows[ix[q]].b-dot(rows[ix[q]].n,x)<=1e-9;if(ok) {out=x;return true;}}
            for(std::size_t a=0;a<u;++a) {
                try_set(1,{a<u?ix[a]:0,0,0});
                for(std::size_t b=a+1;b<u;++b) {
                    try_set(2,{ix[a],ix[b],0});
                    for(std::size_t c=b+1;c<u;++c) try_set(3,{ix[a],ix[b],ix[c]});
                }
            }
            return found;
        };
        if(pushed) std::copy(pushed_before.begin(),pushed_before.end(),pushed);   // the passes found no place: the answer's pushes are measured from the start
        std::array<bool,plane_capacity> use{};
        for(std::size_t k=0;k<m;++k) use[k]=true;
        Vec3 solution{};
        if(!exact(use,solution)) {
            // The tiers: the world's planes are hard (all of them or no solution: false, two walls closer than the hand); the planes of entities
            // that do not move are met as far as the world allows; a dynamic body's as far as both allow. Within a tier all of its planes if they
            // fit, else one by one, the deepest first. A wall and a door's face too close for the hand: the hand is out of the wall and in the door.
            use={};
            for(std::size_t k=0;k<m;++k) use[k]=rows[k].rank==2;
            solution=start;
            if(!exact(use,solution)) return false;
            for(int tier=1;tier>=0;--tier) {
                std::array<bool,plane_capacity> with=use;
                std::array<std::size_t,plane_capacity> order{};std::size_t count=0;
                for(std::size_t k=0;k<m;++k) if(rows[k].rank==tier) {with[k]=true;order[count++]=k;}
                Vec3 trial{};
                if(exact(with,trial)) {use=with;solution=trial;continue;}
                std::sort(order.begin(),order.begin()+count,[&](std::size_t x,std::size_t y){return rows[x].b-dot(rows[x].n,start)>rows[y].b-dot(rows[y].n,start);});
                for(std::size_t q=0;q<count;++q) {
                    use[order[q]]=true;
                    if(exact(use,trial)) solution=trial;else use[order[q]]=false;
                }
            }
        }
        if(pushed) for(std::size_t k=0;k<m;++k) {const double d=dot(rows[k].n,solution-start);if(d>0) pushed[rows[k].plane]+=d;}
        p=solution;
        return true;
    }
    // Two planes of the world that no place satisfies together (the hand is wider than the slot it is pushed into): the hand
    // retreats along the segment from `from` (the controller, or the meet's position) to `to` (the chest) to the first place that satisfies
    // every plane of the world held, found by bisection (12 steps). When even the chest does not, the place on the segment where the deepest
    // violation is smallest (ternary search; the violation is a maximum of linear functions of the position): `minimax` is set.
    Vec3 retreat(const Side& s,const std::array<Vec3,3>& basis,Vec3 from,Vec3 to,std::optional<std::uintptr_t> except,bool& minimax) const {
        minimax=false;
        if(!finite(from) || !finite(to)) return from;
        const auto at=[&](double u) {return from+(to-from)*u;};
        const auto violation=[&](double u) {
            const Vec3 x=at(u);double w=-1e9;
            for(std::size_t i=0;i<s.plane_count;++i) {
                const auto& plane=s.planes[i];
                if(plane.rank<2 || (except && plane.entity==*except)) continue;
                w=std::max(w,-clearance(s,basis,x,plane));
            }
            return w;
        };
        if(violation(0)<=converged) return from;
        if(violation(1)<=converged) {
            double lo=0,hi=1;
            for(int step=0;step<12;++step) {const double mid=(lo+hi)*.5;if(violation(mid)<=converged) hi=mid;else lo=mid;}
            return at(hi);
        }
        minimax=true;
        double lo=0,hi=1;
        for(int step=0;step<24;++step) {
            const double a=lo+(hi-lo)/3,b=hi-(hi-lo)/3;
            if(violation(a)<violation(b)) hi=b;else lo=a;
        }
        return at((lo+hi)*.5);
    }
    static void take_hand(Side& s,const ClampInput& in) {
        s.hand_count=std::min(in.hand_count,in.hand.size());
        for(std::size_t k=0;k<s.hand_count;++k) s.hand[k]=in.hand[k];
    }
    ClampOutput finish(Side& s,const ClampOutput& out) {s.pending_note=true;s.last_out=out;return out;}

public:
    explicit HandClamp(HandClampConfig config={}) : cfg_(config) {}
    void reset(unsigned side) {if(side<2) sides_[side]=Side{};}   // keeps the counters
    std::uint64_t snapbacks(unsigned side) const {return side<2?snapbacks_[side]:0;}
    // The planes this side holds now (world point and normal toward free space; none while released or reset). The shot origin is
    // clipped at them while the hand is clamped (N5).
    static constexpr std::size_t max_held_planes=plane_capacity;
    std::size_t held_planes(unsigned side,std::array<ClampHit,max_held_planes>& out) const {
        if(side>1) return 0;const auto& s=sides_[side];
        if(!s.has || s.state==ClampState::released) return 0;
        std::size_t n=0;
        for(std::size_t i=0;i<s.plane_count && n<out.size();++i) out[n++]=ClampHit{s.planes[i].point,s.planes[i].normal,s.planes[i].entity,s.planes[i].material,false,true};
        return n;
    }
    // The epoch of the side's last update (none: reset, yielded or never updated).
    std::optional<std::uint64_t> side_epoch(unsigned side) const {return side<2 && sides_[side].has?std::optional<std::uint64_t>{sides_[side].epoch}:std::nullopt;}
    std::uint64_t squeeze_infeasible(unsigned side) const {return side<2?squeeze_infeasible_[side]:0;}   // frames no place satisfied the static planes
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
        // Drawn somewhere else than this side's output (the meet): its ease continues from what was drawn, so the clamp
        // taking the hand back does not jump to a hold it kept easing on its own (a 12 cm jump after a 4 m/s swat).
        if(finite(drawn) && length(drawn-s.last_out.drawn.position)>1e-9) s.shift_local=clamp_detail::local_vector(root,drawn-s.target_world);
        s.drawn_world=drawn;
    }
    const HandClampConfig& config() const {return cfg_;}

    // The meet (NativeHandContact::meet): the drawn hand is the target moved out of the prop it pushes (`met`, whose planes
    // are skipped: the prop moves with the hand) instead of held off it. It is pushed out of every other plane this side
    // holds now (last update's planes, no new cast), so the meet never moves the hand through a wall the clamp holds.
    // Nothing held (a released or reset side): the meet position as it is.
    Vec3 keep_out(unsigned side,const std::array<Vec3,3>& basis,Vec3 meet_position,std::optional<std::uintptr_t> met) const {
        if(side>1 || !finite(meet_position)) return meet_position;
        const auto& s=sides_[side];
        if(!s.has || s.state==ClampState::released) return meet_position;
        const Vec3 original=meet_position;
        if(solve(s,basis,meet_position,nullptr,false,met)) return meet_position;
        bool minimax=false;   // two static planes the hand cannot be outside of together: back toward the chest, as update() does
        return retreat(s,basis,original,s.chest_world,met,minimax);
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
            s=Side{};take_hand(s,in);s.chest_world=in.chest;out.reacquired=true;
            if(finite(in.chest)) {out.state=ClampState::free;post_chest(s,in,out);}
            else {release(s);out.state=ClampState::released;++blocked_frames_[side];}
            store(s,in,in.target.position);
            return finish(s,out);
        }
        const double dt=std::min(elapsed,.1);
        take_hand(s,in);s.chest_world=in.chest;
        s.since_haptic+=dt;

        // What this side posted last frame decides how its results read; nothing posted, nothing to read.
        const Posted posted=s.posted;const ClampSweep answered=s.posted_palm,posted_tip=s.posted_tip;
        const ClampProbe tip_probe=s.posted_tip_probe;const bool answered_slid=s.posted_slid;
        const ClampSweep posted_exclude=s.posted_exclude;const ClampProbe exclude_probe=s.posted_exclude_probe;const bool exclude_tip=s.posted_exclude_tip;
        const ClampSweep posted_wall=s.posted_wall;const ClampProbe wall_probe=s.posted_wall_probe;
        s.posted=Posted::none;s.posted_palm={};s.posted_tip={};s.posted_slid=false;s.posted_exclude={};s.posted_wall={};
        if(s.slid_frames<1000) ++s.slid_frames;
        const std::optional<ClampHit>& slot0=in.palm_hit?in.palm_hit:in.chest_hit;
        const bool slot0_loose=slot0 && in.is_loose(slot0->entity);   // a loose prop the push logic owns
        if(posted!=Posted::none) {   // an answered cast: the planes it makes or re-hits are the fresh ones (I3)
            // A slid sweep runs along the planes it slides on and cannot re-hit them: only a raw sweep's answer renews freshness.
            if(!answered_slid) for(std::size_t i=0;i<s.plane_count;++i) s.planes[i].misses=std::min(s.planes[i].misses+1,1000u);
            std::optional<std::uintptr_t> touch;
            if(slot0_loose) touch=slot0->entity;
            else if(posted_tip.valid && in.tip_hit && in.is_loose(in.tip_hit->entity)) touch=in.tip_hit->entity;
            else if(posted_wall.valid && !posted_wall.exclude && in.wall_tip_hit && in.is_loose(in.wall_tip_hit->entity)) touch=in.wall_tip_hit->entity;
            if(touch) {s.loose_touch=touch;s.loose_touch_age=0;}
            else if(s.loose_touch && (s.loose_touch_age+=dt)>cfg_.plane_seconds+1e-9) s.loose_touch.reset();
        }

        if(!finite(s.prev_local) || !finite(s.shift_local) || !finite(s.own_shift_local) || !finite(s.drawn_world) || !finite(s.target_world) ||
           !finite(s.prev_basis_local[0]) || !finite(s.prev_basis_local[1]) || !finite(s.prev_basis_local[2]))
            snap(side,s,out);

        // Fold in last frame's results.
        if(s.state!=ClampState::released) {
            // A long frame (a 100 ms hitch) ages a plane by at most half its life, so one hitch cannot expire a wall the
            // hand is pressed into before a sweep can confirm it again.
            const double age_step=std::min(dt,cfg_.plane_seconds*.5);
            for(std::size_t i=0;i<s.plane_count;++i) {
                auto& plane=s.planes[i];
                plane.age+=age_step;plane.direct_age+=age_step;plane.news=false;
                plane.touched=!plane.refuted && clearance(s,s.basis_world,s.drawn_world,plane)<=contact_margin;
            }
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
                if(slot0) add_plane(s,*slot0,cfg_.palm_radius,{},false,true,slot0_loose);
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
                if(in.tip_hit) add_plane(s,*in.tip_hit,tip_probe.radius,tip_probe.local,true,true,in.is_loose(in.tip_hit->entity));
            }
            // An exclusion sweep (I1) only adds what lies behind the excluded prop: it judges nothing (it passes through the prop's
            // planes by design) and does not steer the next sweep's slide. What it finds is held by the probe that found it and by
            // the palm too: over a met thin prop on a table, the tip's plane alone let the palm sphere 1 cm into the table (the
            // prop's own planes are skipped by the meet).
            const auto behind=[&](const ClampSweep& sweep,const std::optional<ClampHit>& hit,const ClampProbe& probe,bool tip) {
                if(s.state==ClampState::released || !sweep.valid || !hit || hit->initial_overlap || (sweep.exclude && hit->entity==*sweep.exclude)) return;
                const bool loose=in.is_loose(hit->entity);
                    if(tip) add_plane(s,*hit,probe.radius,probe.local,true,true,loose);
                add_plane(s,*hit,cfg_.palm_radius,{},false,false,loose);
            };
            if(posted_wall.exclude) behind(posted_wall,in.wall_tip_hit,wall_probe,true);
            else if(s.state!=ClampState::released && posted_wall.valid) {
                judge(s,posted_wall,in.wall_tip_hit,true);
                if(in.wall_tip_hit) add_plane(s,*in.wall_tip_hit,wall_probe.radius,wall_probe.local,true,true,in.is_loose(in.wall_tip_hit->entity));
            }
            behind(posted_exclude,in.exclude_hit,exclude_probe,exclude_tip);
            // A plane the drawn hand still touches (within contact_margin, by any of its spheres) does not age in a frame no sweep
            // reached it (a loose prop's plane still goes after fresh_misses missed casts): the swept probes need not come near it (sliding along a wall the side probes lead; stopped with only
            // the fingers at it), and the fingers went 6 cm into the wall once it expired. A sweep that reaches it still refutes it,
            // and one that a nearer surface stops still refreshes it only within shadow_seconds of its last direct hit.
            for(std::size_t i=0;i<s.plane_count;++i) {
                auto& plane=s.planes[i];
                if(plane.touched && !plane.news && !plane.refuted) plane.age=std::max(0.,plane.age-age_step);
            }
            std::size_t kept=0;
            // A loose prop moves: its plane is where the prop was when it was hit, and goes once fresh_misses answered raw casts
            // in a row miss it (a static plane lives plane_seconds). A swatted bottle's old faces otherwise held the hand 20-40 cm
            // behind the bottle.
            for(std::size_t i=0;i<s.plane_count;++i)
                if(s.planes[i].age<=cfg_.plane_seconds && !(in.is_loose(s.planes[i].entity) && s.planes[i].misses>=fresh_misses)) s.planes[kept++]=s.planes[i];
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
            s.state=ClampState::free;s.clear_time=0;s.plane_count=0;s.shift_local={};s.own_shift_local={};s.overlap_count=0;s.over_time=0;
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
        const bool feasible=solve(s,in.target.basis,p,pushed.data());
        // Two static planes that no place near the controller satisfies together (the hand is wider than the slot it is pushed into): the
        // hand retreats along the controller-to-chest segment to the first place outside every static plane held, or, when even the chest is
        // not, to where they are violated least (squeeze_infeasible counts those frames). Never drawn at a place the planes rule out when
        // another exists.
        Vec3 wanted=p-target;
        if(!feasible) {
            bool minimax=false;
            const Vec3 safe=retreat(s,in.target.basis,target,in.chest,std::nullopt,minimax);
            wanted=safe-target;
            if(minimax) ++squeeze_infeasible_[side];
        }
        // Engage instantly (never render inside a surface); ease out at release_speed when the push shrinks. The eased hand never
        // enters a plane.
        const auto ease=[&](Vec3 from,double* push) {
            Vec3 q=target+(!feasible || length(wanted)>=length(from)?wanted:from+clamp_detail::clamp_length(wanted-from,cfg_.release_speed*dt));
            if(feasible) solve(s,in.target.basis,q,push);
            return q-target;
        };
        // The drawn hand eases from what was drawn (note_drawn); the snap-back gap from the clamp's own push alone, so what a
        // caller draws on top (the meet's way out, a lift) never counts toward snap_seconds.
        const Vec3 shift=ease(clamp_detail::rotate(in.root.basis,s.shift_local),pushed.data());
        const Vec3 own=ease(clamp_detail::rotate(in.root.basis,s.own_shift_local),nullptr);
        p=target+shift;

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
        if(!validated && s.hold_frames<max_hold_frames*s.cadence) {
            Vec3 out_of_planes=held;
            if(solve(s,in.target.basis,out_of_planes)) held=out_of_planes;
            else {bool minimax=false;held=retreat(s,in.target.basis,target,in.chest,std::nullopt,minimax);}   // two walls too close
            hold=true;
        }
        s.drawn_validated=hold || validated;
        if(hold) {
            ++s.hold_frames;out.held=true;out.drawn.position=held;out.shift=held-target;
        } else {
            if(validated) s.hold_frames=0;
            s.over_time=length(own)>cfg_.snap_gap?s.over_time+dt:0;
            if(s.over_time>cfg_.snap_seconds) {snap(side,s,out);released_frame(s,in,out);return finish(s,out);}
            out.drawn.position=p;out.shift=shift;
            s.shift_local=clamp_detail::local_vector(in.root,shift);s.own_shift_local=clamp_detail::local_vector(in.root,own);
        }

        // World velocity, the player's own motion included (C8): the result lands after the root has moved too.
        const Vec3 velocity=dt>0?(target-s.target_world)*(1/dt):Vec3{};
        std::size_t strongest=plane_capacity;
        for(std::size_t i=0;i<s.plane_count;++i) if(pushed[i]>1e-9 && (strongest==plane_capacity || pushed[i]>pushed[strongest])) strongest=i;
        if(strongest<plane_capacity) {
            const auto& plane=s.planes[strongest];
            out.contact=ClampHit{plane.point,plane.normal,plane.entity,plane.material,false,true};
            out.contact_fresh=plane.misses<fresh_misses;
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
                solve(s,from_basis,from);
            }
            Vec3 end=target+ahead;bool slid=false;
            if(hold || recover) {end=target+ahead_for(2*lead);solve(s,in.target.basis,end);slid=true;}   // along all known planes
            else if(!s.last_slid) {
                for(std::size_t i=0;i<s.plane_count;++i) if(!s.planes[i].tip && s.planes[i].last_answer) slid=true;
                if(slid) {end=target+ahead_for(2*lead);solve(s,in.target.basis,end,nullptr,true);}
            }
            s.last_slid=slid;
            const ClampSweep palm{from,end,cfg_.palm_radius,true};
            if(finite(palm.from) && finite(palm.to)) {
                out.palm_sweep=palm;s.posted=Posted::palm;s.posted_palm=palm;s.posted_slid=slid;
                s.posted_slid_window=static_cast<unsigned>(2*lead);s.cadence=static_cast<unsigned>(lead);
            }
            // The probe that acts as the wall tip (its own sweep, or the tip's when it is the same probe) and the exclusion probe's sweep end
            // look_ahead beyond the target along the horizontal motion: the drawn hand is not always where the sweeps end. The meet takes it out
            // of a prop by up to meet_latch (8 cm) a frame after the sweeps were posted, and a plane that disagrees with another pushes it along
            // its own normal, so a wall a few cm past the swept tip was met by no sweep until the drawn hand was in it. At rest (under 1 cm/s)
            // the way out the meet will take (ClampInput::kick) gives the direction. The plain tip of a separate wall tip is not extended: it keeps
            // the planes the resting hand has hit.
            Vec3 look{};
            if(cfg_.look_ahead>0) {
                Vec3 d{velocity.x,velocity.y,0};double h=std::hypot(d.x,d.y);
                if(!std::isfinite(h) || h<.01) {d={in.kick.x,in.kick.y,0};h=std::hypot(d.x,d.y);if(!std::isfinite(h) || h<1e-3) h=0;}
                if(h>0) look=d*(cfg_.look_ahead/h);
            }
            const auto post_tip=[&](bool ahead_of_target,const ClampProbe& probe,ClampSweep& sweep) {
                const ClampSweep tip{from+clamp_detail::rotate(from_basis,probe.local),end+(ahead_of_target?look:Vec3{})+clamp_detail::rotate(in.target.basis,probe.local),probe.radius,true};
                if(finite(tip.from) && finite(tip.to)) sweep=tip;
            };
            const bool tip_on=in.tip_enabled && clamp_detail::usable(in.tip);
            const bool wall_on=in.tip_enabled && clamp_detail::usable(in.wall_tip) && !(tip_on && clamp_detail::same_probe(in.wall_tip,in.tip));
            const std::optional<std::uintptr_t> excluded=in.exclude?in.exclude:s.loose_touch;
            const bool excluding=excluded && out.palm_sweep.valid;
            const bool tip_is_wall_tip=!excluding && tip_on && clamp_detail::usable(in.wall_tip) && clamp_detail::same_probe(in.wall_tip,in.tip);
            if(tip_on) {
                post_tip(tip_is_wall_tip,in.tip,out.tip_sweep);
                if(out.tip_sweep.valid) {s.posted_tip=out.tip_sweep;s.posted_tip_probe=in.tip;}
            }
            // I1: a loose prop the last answered cast met (or the met prop) hides what lies behind it from the closest-hit sweeps,
            // and a probe overlapping it sweeps from inside it. Two probes then also sweep from the hand to the unslid target with
            // that prop excluded (at most 4 casts a side):
            //  - the wall tip, in its own slot instead of its plain sweep (it is the probe that meets a wall behind the prop first;
            //    with the same probe as the tip, the tip);
            //  - the probe that leads into the face of the prop the clamp pushes the hand off hardest (the palm, the tip or the wall
            //    tip; the palm wins a tie), when it is another one: what lies behind a prop lies beyond the face the hand presses
            //    (pressing a thin prop on a table with the fingers forward, the palm meets the table first). With no plane on the
            //    prop, the leader along the motion; at rest, the tip.
            const ClampProbe* wall=wall_on?&in.wall_tip:tip_on && clamp_detail::usable(in.wall_tip)?&in.tip:nullptr;
            const auto post_excluded=[&](const ClampProbe& probe,ClampSweep& sweep) {
                const ClampSweep ex{from+clamp_detail::rotate(from_basis,probe.local),target+ahead+look+clamp_detail::rotate(in.target.basis,probe.local),
                                    probe.radius,true,excluded};
                if(finite(ex.from) && finite(ex.to)) sweep=ex;
            };
            if(wall && (wall_on || excluding)) {
                if(excluding) post_excluded(*wall,out.wall_tip_sweep);else post_tip(true,*wall,out.wall_tip_sweep);
                if(out.wall_tip_sweep.valid) {s.posted_wall=out.wall_tip_sweep;s.posted_wall_probe=*wall;}
            }
            if(excluding) {
                Vec3 into{};double hardest=-1;
                for(std::size_t i=0;i<s.plane_count;++i) {
                    const auto& plane=s.planes[i];
                    const double score=pushed[i]-plane.direct_age*1e-3;   // pushed hardest now, else hit most recently
                    if(plane.entity==*excluded && score>hardest) {hardest=score;into=plane.normal*-1.;}
                }
                if(hardest<0) {const double speed=length(velocity);if(std::isfinite(speed) && speed>=clamp_detail::lead_speed) into=velocity*(1/speed);}
                ClampProbe probe{{},cfg_.palm_radius};bool by_tip=false;
                if(length(into)>.5) {
                    double best=cfg_.palm_radius;
                    for(const auto* candidate:{tip_on?&in.tip:nullptr,wall_on?&in.wall_tip:nullptr}) {
                        if(!candidate) continue;
                        const double reach=dot(clamp_detail::rotate(in.target.basis,candidate->local),into)+candidate->radius;
                        if(std::isfinite(reach) && reach>best) {best=reach;probe=*candidate;by_tip=true;}
                    }
                } else if(tip_on) {probe=in.tip;by_tip=true;}
                if(!(wall && out.wall_tip_sweep.valid && by_tip && clamp_detail::same_probe(probe,*wall))) {
                    post_excluded(probe,out.exclude_sweep);
                    if(out.exclude_sweep.valid) {s.posted_exclude=out.exclude_sweep;s.posted_exclude_tip=by_tip;s.posted_exclude_probe=probe;}
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
