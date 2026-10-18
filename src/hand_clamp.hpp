#pragma once
// Pure policy: the visible hand stops at world surfaces (physical hands phase 1: G1, G3, C3, C4, C8, C11, C14).
// Nothing here touches the engine. update() posts sphere sweeps (palm, fingertip, or chest re-acquire) and consumes
// their results one frame later through ClampInput. Hits are kept as half-space planes for plane_seconds, so a contact
// survives a frame without a result and a corner holds both walls.
#include "weapon_collision.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>

namespace d2vr {

struct ClampProbe {Vec3 local{};double radius{};};      // sphere in the grip frame (x forward, y left, z up)
struct ClampHit {Vec3 point{},normal{};std::uintptr_t entity{};std::uint16_t material{0xffff};bool initial_overlap{};};
struct ClampSweep {Vec3 from{},to{};double radius{};bool valid{};};
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
    ClampProbe tip{};bool tip_enabled{true},post{true},yield{};};
enum class ClampState {free,contact,released};
struct ClampOutput {
    bool valid{};WorldTrackedPose drawn{};Vec3 shift{};ClampState state{};bool snapped{};
    std::optional<ClampHit> contact;double approach_speed{};
    std::optional<CollisionEvent> event;std::optional<CollisionHaptic> haptic;
    ClampSweep palm_sweep,tip_sweep;};  // palm_sweep is chest->target while released

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
    enum class Posted : unsigned char {none,palm,chest};
    struct Plane {Vec3 point{},normal{},local{};double radius{},age{};std::uintptr_t entity{};std::uint16_t material{0xffff};bool tip{};};
    static constexpr std::size_t plane_capacity=8;
    struct Side {
        bool has{};std::uint64_t epoch{};double last_now{};ClampState state{ClampState::free};
        Vec3 prev_local{},prev_target_local{},shift_local{};       // root (level player) frame
        std::array<Vec3,3> prev_basis_local{};
        std::array<Plane,plane_capacity> planes{};std::size_t plane_count{};
        double over_time{},clear_time{},since_haptic{1e9};unsigned overlap_count{};
        Posted posted{Posted::none};ClampSweep posted_palm{},posted_tip{};ClampProbe posted_tip_probe{};
    };
    HandClampConfig cfg_;
    std::array<Side,2> sides_{};
    std::array<std::uint64_t,2> snapbacks_{},contact_frames_{};

    std::size_t capacity() const {return std::clamp<std::size_t>(cfg_.max_planes,1,plane_capacity);}
    static void store(Side& s,const ClampInput& in,Vec3 drawn) {
        s.has=true;s.epoch=in.epoch;s.last_now=in.now;
        s.prev_local=clamp_detail::local_point(in.root,drawn);
        s.prev_target_local=clamp_detail::local_point(in.root,in.target.position);
        for(std::size_t i=0;i<3;++i) s.prev_basis_local[i]=clamp_detail::local_vector(in.root,in.target.basis[i]);
    }
    void released_frame(Side& s,const ClampInput& in,ClampOutput& out) const {
        out.state=ClampState::released;out.drawn=in.target;out.shift={};
        if(in.post && finite(in.chest)) {out.palm_sweep={in.chest,in.target.position,cfg_.palm_radius,true};s.posted=Posted::chest;}
        store(s,in,in.target.position);
    }
    static void release(Side& s) {
        s.state=ClampState::released;s.plane_count=0;s.shift_local={};
        s.over_time=0;s.clear_time=0;s.overlap_count=0;
    }
    void snap(unsigned side,Side& s,ClampOutput& out) {release(s);++snapbacks_[side];out.snapped=true;}
    // Fraction along the sweep where its sphere first touches the plane (0 when it starts touching).
    static std::optional<double> crossing(const ClampSweep& sweep,Vec3 point,Vec3 normal) {
        const double s0=dot(sweep.from-point,normal)-sweep.radius,s1=dot(sweep.to-point,normal)-sweep.radius;
        if(s0<=0) return 0.;
        if(s1>=0) return std::nullopt;
        return s0/(s0-s1);
    }
    // A closest-hit cast that stopped at the hit says nothing about planes beyond it: those are kept fresh.
    static void refresh_shadowed(Side& s,const ClampSweep& sweep,const ClampHit& hit,bool tip) {
        const double nl=length(hit.normal);
        if(!sweep.valid || !finite(hit.point) || !(nl>.5) || !std::isfinite(nl)) return;
        const auto at=hit.initial_overlap?std::optional<double>{0.}:crossing(sweep,hit.point,hit.normal*(1/nl));
        if(!at) return;
        const double span=length(sweep.to-sweep.from);
        for(std::size_t i=0;i<s.plane_count;++i) {
            auto& p=s.planes[i];
            if(p.tip!=tip) continue;
            const auto t=crossing(sweep,p.point,p.normal);
            if(t && (*t>=*at || (*at-*t)*span<=.002)) p.age=0;
        }
    }
    void add_plane(Side& s,const ClampHit& hit,double radius,Vec3 local,bool tip) const {
        const double nl=length(hit.normal);
        if(!finite(hit.point) || !std::isfinite(nl) || !(nl>.5)) return;
        const Plane plane{hit.point,hit.normal*(1/nl),local,radius,0,hit.entity,hit.material,tip};
        std::size_t slot=s.plane_count;
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& p=s.planes[i];
            if(p.tip==tip && p.radius==radius && length(p.local-local)<1e-9 && dot(p.normal,plane.normal)>.99 &&
               std::abs(dot(plane.point-p.point,p.normal))<.01) {slot=i;break;}  // the same surface: refresh it
        }
        if(slot==s.plane_count) {
            if(s.plane_count<capacity()) ++s.plane_count;
            else slot=static_cast<std::size_t>(std::max_element(s.planes.begin(),s.planes.begin()+s.plane_count,
                [](const Plane& a,const Plane& b){return a.age<b.age;})-s.planes.begin());  // full: replace the oldest
        }
        s.planes[slot]=plane;
    }
    // Pushes grip point p (oriented by basis) out of every plane by the probe's radius plus skin; adds each push to `pushed`.
    void project(const Side& s,const std::array<Vec3,3>& basis,Vec3& p,std::array<double,plane_capacity>& pushed) const {
        for(std::size_t i=0;i<s.plane_count;++i) {
            const auto& plane=s.planes[i];
            const Vec3 offset=clamp_detail::rotate(basis,plane.local);
            const double d=dot(p+offset-plane.point,plane.normal)-(plane.radius+cfg_.skin);
            if(d<0) {p=p-plane.normal*d;pushed[i]-=d;}
        }
    }
    ClampOutput reacquire(Side& s,const ClampInput& in,ClampOutput& out) const {
        s=Side{};
        out.state=ClampState::free;
        if(in.post && finite(in.chest)) {out.palm_sweep={in.chest,in.target.position,cfg_.palm_radius,true};s.posted=Posted::chest;}
        store(s,in,in.target.position);
        return out;
    }

public:
    explicit HandClamp(HandClampConfig config={}) : cfg_(config) {}
    void reset(unsigned side) {if(side<2) sides_[side]=Side{};}
    std::uint64_t snapbacks(unsigned side) const {return side<2?snapbacks_[side]:0;}
    std::uint64_t contact_frames(unsigned side) const {return side<2?contact_frames_[side]:0;}
    // The position finally drawn (meet shift, blade push): next frame's sweep starts there.
    void note_drawn(unsigned side,const WorldTrackedPose& root,Vec3 drawn) {
        if(side>1 || !sides_[side].has) return;
        sides_[side].prev_local=clamp_detail::local_point(root,drawn); // non-finite: the next update snaps
    }

    ClampOutput update(unsigned side,const ClampInput& in) {
        ClampOutput out{};
        if(side>1) return out;
        auto& s=sides_[side];
        out.drawn=in.target;
        const bool target_ok=clamp_detail::finite_pose(in.target);
        if(in.yield) {reset(side);out.valid=target_ok;return out;}
        if(!target_ok || !clamp_detail::finite_pose(in.root) || !std::isfinite(in.now)) {reset(side);return out;}
        out.valid=true;
        const double elapsed=in.now-s.last_now;
        if(!s.has || in.epoch!=s.epoch || !(elapsed>=0) || elapsed>cfg_.gap_seconds) return reacquire(s,in,out);
        const double dt=std::min(elapsed,.1);
        s.since_haptic+=dt;

        // What this side posted last frame decides how its results read; nothing posted, nothing to read.
        const Posted posted=s.posted;const ClampSweep posted_palm=s.posted_palm,posted_tip=s.posted_tip;
        const ClampProbe tip_probe=s.posted_tip_probe;
        s.posted=Posted::none;s.posted_palm={};s.posted_tip={};
        const std::optional<ClampHit>& slot0=in.palm_hit?in.palm_hit:in.chest_hit;

        if(!finite(s.prev_local) || !finite(s.prev_target_local) || !finite(s.shift_local) ||
           !finite(s.prev_basis_local[0]) || !finite(s.prev_basis_local[1]) || !finite(s.prev_basis_local[2]))
            snap(side,s,out);

        // Fold in last frame's hits.
        if(s.state!=ClampState::released) {
            for(std::size_t i=0;i<s.plane_count;++i) s.planes[i].age+=dt;
            if(posted==Posted::chest && slot0) {
                // The re-acquire sweep: a chest inside geometry (crouched under a table) cannot validate the path.
                if(slot0->initial_overlap) release(s);
                else add_plane(s,*slot0,cfg_.palm_radius,{},false);
            }
            if(posted==Posted::palm) {
                if(slot0) {refresh_shadowed(s,posted_palm,*slot0,false);add_plane(s,*slot0,cfg_.palm_radius,{},false);}
                s.overlap_count=slot0 && slot0->initial_overlap?s.overlap_count+1:0;
                if(cfg_.overlap_frames && s.overlap_count>=cfg_.overlap_frames) snap(side,s,out);
            }
            if(s.state!=ClampState::released && posted_tip.valid && in.tip_hit) {
                refresh_shadowed(s,posted_tip,*in.tip_hit,true);
                add_plane(s,*in.tip_hit,tip_probe.radius,tip_probe.local,true);
            }
            std::size_t kept=0;
            for(std::size_t i=0;i<s.plane_count;++i) if(s.planes[i].age<=cfg_.plane_seconds) s.planes[kept++]=s.planes[i];
            s.plane_count=kept;
        }

        if(s.state==ClampState::released) {
            if(posted==Posted::chest) s.clear_time=slot0?0:s.clear_time+dt;
            if(s.clear_time<cfg_.rearm_seconds-1e-9) {released_frame(s,in,out);return out;}
            // Re-arm: the chest-to-controller path stayed clear, so the controller is in free space.
            // prev = target; last frame's target stays for the velocity.
            s.state=ClampState::free;s.clear_time=0;s.plane_count=0;s.shift_local={};s.overlap_count=0;s.over_time=0;
            s.prev_local=clamp_detail::local_point(in.root,in.target.position);
            for(std::size_t i=0;i<3;++i) s.prev_basis_local[i]=clamp_detail::local_vector(in.root,in.target.basis[i]);
        }

        // The constraint.
        const Vec3 target=in.target.position;
        std::array<double,plane_capacity> pushed{};
        Vec3 p=target;
        for(int pass=0;pass<3;++pass) project(s,in.target.basis,p,pushed);
        const Vec3 wanted=p-target;
        Vec3 shift=clamp_detail::rotate(in.root.basis,s.shift_local);
        // Engage instantly (never render inside a surface); ease out at release_speed when the push shrinks.
        shift=length(wanted)>=length(shift)?wanted:shift+clamp_detail::clamp_length(wanted-shift,cfg_.release_speed*dt);
        p=target+shift;
        project(s,in.target.basis,p,pushed);   // the eased hand never enters a plane
        shift=p-target;
        s.over_time=length(shift)>cfg_.snap_gap?s.over_time+dt:0;
        if(s.over_time>cfg_.snap_seconds) {snap(side,s,out);released_frame(s,in,out);return out;}

        out.drawn.position=p;out.shift=shift;
        const Vec3 velocity=dt>0?(target-clamp_detail::world_point(in.root,s.prev_target_local))*(1/dt):Vec3{};
        std::size_t strongest=plane_capacity;
        for(std::size_t i=0;i<s.plane_count;++i) if(pushed[i]>1e-9 && (strongest==plane_capacity || pushed[i]>pushed[strongest])) strongest=i;
        if(strongest<plane_capacity) {
            const auto& plane=s.planes[strongest];
            out.contact=ClampHit{plane.point,plane.normal,plane.entity,plane.material,false};
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

        // Sweep from the previous drawn hand (carried by the root, C11) to one frame ahead of the target (C8). Results are
        // a frame late, so after a jump the previous hand can sit inside a surface found since: start it outside the planes
        // known now, or the sweeps keep starting inside walls the clamp already holds (and count as a deep start).
        if(in.post) {
            std::array<Vec3,3> previous{};
            for(std::size_t i=0;i<3;++i) previous[i]=clamp_detail::rotate(in.root.basis,s.prev_basis_local[i]);
            Vec3 from=clamp_detail::world_point(in.root,s.prev_local);
            std::array<double,plane_capacity> unused{};
            for(int pass=0;pass<3;++pass) project(s,previous,from,unused);
            const Vec3 ahead=velocity*dt;
            const ClampSweep palm{from,target+ahead,cfg_.palm_radius,true};
            if(finite(palm.from) && finite(palm.to)) {out.palm_sweep=palm;s.posted=Posted::palm;s.posted_palm=palm;}
            if(in.tip_enabled && clamp_detail::usable(in.tip)) {
                const ClampSweep tip{from+clamp_detail::rotate(previous,in.tip.local),
                                     target+clamp_detail::rotate(in.target.basis,in.tip.local)+ahead,in.tip.radius,true};
                if(finite(tip.from) && finite(tip.to)) {out.tip_sweep=tip;s.posted_tip=tip;s.posted_tip_probe=in.tip;}
            }
        }
        s.shift_local=clamp_detail::local_vector(in.root,shift);
        store(s,in,p);
        return out;
    }
};

} // namespace d2vr
