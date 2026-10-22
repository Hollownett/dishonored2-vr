#pragma once
// Pure policy: a held object's drive target stops at world surfaces (Part 5A; Bonelab's held object is a dynamic body on a
// force-limited joint, so a wall stops it and the hand lags, §1.2, §2.1, §2.2). We have no joint: the target the engine's
// keyframe bind chases is moved to where every proxy sphere of the object is clear of the world.
// Nothing here touches the engine. update() posts one sphere sweep per proxy (last published pose -> requested pose) and consumes
// the results one frame later. Hits are kept as half-space planes for plane_seconds, ranked as HandClamp ranks them
// (world > entity that does not move > dynamic body). The requested orientation is kept; only the position is pushed out.
#include "dwell_timer.hpp"
#include "hand_clamp.hpp"
#include "native_rigid_body.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace d2vr {

struct HeldProxy {Vec3 local{};double radius{};};   // sphere in the held body's frame
inline constexpr std::size_t held_proxy_capacity=4;
struct HeldProxies {std::array<HeldProxy,held_proxy_capacity> sphere{};std::size_t count{};};

// Spheres along the long axis of the model-space bounds (a cube gets one, the tank three); none when the box is unreadable
// (non-finite, empty, or over 3 m on any side) or the pose is not finite. The radius is the larger half of the two short sides, so
// the spheres cover the cross section; the pose is checked only, the spheres live in the body's own frame.
inline std::optional<HeldProxies> held_proxies(const NativeGrabBounds& model_box,const WorldTrackedPose& pose) {
    constexpr double max_extent=3;
    if(!finite(model_box.minimum) || !finite(model_box.maximum) || !clamp_detail::finite_pose(pose)) return std::nullopt;
    const std::array<double,3> lo{model_box.minimum.x,model_box.minimum.y,model_box.minimum.z},
                               hi{model_box.maximum.x,model_box.maximum.y,model_box.maximum.z};
    std::array<double,3> extent{};
    for(std::size_t a=0;a<3;++a) {
        extent[a]=hi[a]-lo[a];
        if(!(extent[a]>0) || extent[a]>max_extent) return std::nullopt;
    }
    std::size_t axis=0;
    for(std::size_t a=1;a<3;++a) if(extent[a]>extent[axis]) axis=a;
    double cross=0;
    for(std::size_t a=0;a<3;++a) if(a!=axis) cross=std::max(cross,extent[a]*.5);
    const double length_m=extent[axis];
    const double want=std::ceil(length_m/(2*cross)-1e-9);
    const std::size_t count=static_cast<std::size_t>(std::clamp(want,1.,static_cast<double>(held_proxy_capacity)));
    const double centre[3]{(lo[0]+hi[0])*.5,(lo[1]+hi[1])*.5,(lo[2]+hi[2])*.5};
    const double reach=std::max(0.,length_m*.5-cross);   // outermost centre offset along the axis
    HeldProxies out{};
    out.count=count;
    for(std::size_t i=0;i<count;++i) {
        const double along=count==1?0.:-reach+2*reach*static_cast<double>(i)/static_cast<double>(count-1);
        double c[3]{centre[0],centre[1],centre[2]};
        c[axis]+=along;
        out.sphere[i]={{c[0],c[1],c[2]},cross};
    }
    return out;
}

struct HeldClampInput {
    double now{};                  // monotonic seconds
    std::uint64_t epoch{};         // a change resets planes, the published pose and the release timer
    WorldTrackedPose requested{};  // where the engine's hold would put the object this frame
    std::optional<HeldProxies> proxies;   // none: unreadable bounds (no clamp)
    std::array<std::optional<ClampHit>,held_proxy_capacity> hits{};   // answers to last frame's sweeps, by proxy
    bool yield{};                  // a native animation owns the object: forget everything, pass the request through
};
struct HeldClampOutput {
    bool valid{};WorldTrackedPose target{};Vec3 shift{};bool clamped{};bool no_bounds{};
    std::array<ClampSweep,held_proxy_capacity> sweeps{};   // this frame's casts (valid for the first proxies.count); the caller sets exclude
    double gap_m{};};                                      // |requested - target|
struct HeldClampConfig {double skin=.005,plane_seconds=.1,release_gap=.35,release_seconds=.25;unsigned max_planes=4;};

class HeldClamp {
public:
    explicit HeldClamp(HeldClampConfig cfg={}) : cfg_(cfg) {reset();}
    HeldClampOutput update(const HeldClampInput& in) {
        HeldClampOutput out{};
        out.target=in.requested;
        if(!clamp_detail::finite_pose(in.requested) || !std::isfinite(in.now)) return out;   // invalid: nothing to publish
        out.valid=true;
        if(in.yield) {reset();return out;}
        if(has_ && in.epoch!=epoch_) reset();
        double dt=0;
        if(has_) {
            dt=in.now-last_now_;
            if(!(dt>=0) || dt>max_gap) {reset();dt=0;}
        }
        has_=true;epoch_=in.epoch;last_now_=in.now;
        if(!in.proxies || in.proxies->count==0) {
            out.no_bounds=true;
            plane_count_=0;released_=false;dwell_.reset();
            return out;
        }
        const HeldProxies& proxies=*in.proxies;
        const std::size_t n=std::min(proxies.count,held_proxy_capacity);
        for(std::size_t i=0;i<plane_count_;) {
            planes_[i].age+=dt;
            if(planes_[i].age>cfg_.plane_seconds) planes_[i]=planes_[--plane_count_]; else ++i;
        }
        // A closest-hit cast reports one surface, so a corner's second plane would never be re-hit and would lapse while the object
        // is still pressed into it. After a full sweep that hit, the next sweep is slid: its motion loses the part into the planes
        // the full sweep hit, so it meets what lies behind them. Planes are re-hit every other frame at worst.
        std::array<Vec3,held_proxy_capacity> slide{};std::size_t slide_count=0;
        for(std::size_t i=0;i<n;++i) if(in.hits[i]) {
            add_plane(*in.hits[i]);
            if(!last_slid_ && in.hits[i]->normal_valid && finite(in.hits[i]->normal) && length(in.hits[i]->normal)>.5)
                slide[slide_count++]=unit(in.hits[i]->normal);
        }

        // Push the request out along the planes: low rank first, the world last, so the stronger plane wins where they disagree.
        Vec3 p=in.requested.position;
        struct Row {Vec3 normal;double bound;unsigned char rank;};
        std::array<Row,plane_capacity> rows{};
        for(std::size_t k=0;k<plane_count_;++k) {
            const Plane& plane=planes_[k];
            double b=-std::numeric_limits<double>::infinity();
            for(std::size_t i=0;i<n;++i) {
                const Vec3 offset=clamp_detail::rotate(in.requested.basis,proxies.sphere[i].local);
                b=std::max(b,dot(plane.normal,plane.point)+proxies.sphere[i].radius+cfg_.skin-dot(plane.normal,offset));
            }
            rows[k]={plane.normal,b,plane.rank};
        }
        const auto worst=[&](Vec3 x) {
            double w=0;
            for(std::size_t k=0;k<plane_count_;++k) w=std::max(w,rows[k].bound-dot(rows[k].normal,x));
            return w;
        };
        bool moved=false;
        if(worst(p)>0) {
            for(unsigned pass=0;pass<max_passes;++pass) {
                for(unsigned char level=0;level<3;++level)
                    for(std::size_t k=0;k<plane_count_;++k) {
                        if(rows[k].rank!=level) continue;
                        const double d=dot(rows[k].normal,p)-rows[k].bound;
                        if(d<0) p=p-rows[k].normal*d;
                    }
                if(worst(p)<=converged) break;
            }
            // Planes that cannot all be met (two walls closer than the object): stay at the last published, known clear pose.
            if(worst(p)>fallback_tolerance && published_) p=published_->position;
            moved=true;
        }
        if(moved && finite(p)) {out.target.position=p;out.clamped=true;out.shift=p-in.requested.position;}
        out.gap_m=length(in.requested.position-out.target.position);

        released_=dwell_.update(out.gap_m>cfg_.release_gap,dt);

        // This frame's sweeps: from the last published pose's proxies to the requested pose's proxies (a zero-length cast on the
        // first frame, which still reports an object that starts inside the world).
        const WorldTrackedPose& from=published_?*published_:in.requested;
        WorldTrackedPose to=in.requested;
        const bool slid=!last_slid_ && slide_count>0 && published_;
        if(slid) {
            Vec3 move=in.requested.position-from.position;
            for(std::size_t k=0;k<slide_count;++k) {
                const double into=dot(move,slide[k]);
                if(into<0) move=move-slide[k]*into;
            }
            if(finite(move)) to.position=from.position+move;
        }
        last_slid_=slid;
        for(std::size_t i=0;i<n;++i) {
            out.sweeps[i]={clamp_detail::world_point(from,proxies.sphere[i].local),
                           clamp_detail::world_point(to,proxies.sphere[i].local),proxies.sphere[i].radius,true,std::nullopt};
        }
        return out;
    }
    // Held for longer than release_seconds with the request more than release_gap from the clamped target.
    bool should_release() const {return released_;}
    // What was finally published; the next sweeps start there.
    void note_published(const WorldTrackedPose& target) {
        if(clamp_detail::finite_pose(target)) published_=target;
    }
    void reset() {
        has_=false;epoch_=0;last_now_=0;plane_count_=0;published_.reset();released_=false;last_slid_=false;
        dwell_=DwellTimer{cfg_.release_seconds,0};
    }
private:
    struct Plane {Vec3 point{},normal{};double age{};unsigned char rank{2};};
    static constexpr std::size_t plane_capacity=8;
    static constexpr unsigned max_passes=12;
    static constexpr double converged=5e-4,fallback_tolerance=2e-3,max_gap=.5;
    std::size_t capacity() const {return std::clamp<std::size_t>(cfg_.max_planes,1,plane_capacity);}
    void add_plane(const ClampHit& hit) {
        // As HandClamp: a sweep that starts inside geometry (initial overlap) does not say where the surface is (its point and normal are the engine's
        // penetration data, sim 2026-10-22: a pickup next to a surface pushed the target more than 0.35 m); only a hit along the sweep makes a plane.
        if(hit.initial_overlap || !hit.normal_valid || !finite(hit.point) || !finite(hit.normal)) return;
        const double l=length(hit.normal);
        if(!(l>.5)) return;
        Plane plane{hit.point,hit.normal*(1/l),0,
                    static_cast<unsigned char>(hit.body==BodyClass::dynamic?0:hit.body==BodyClass::entity_static?1:2)};
        for(std::size_t i=0;i<plane_count_;++i)   // the same surface again: refresh it
            if(dot(planes_[i].normal,plane.normal)>.999 && std::abs(dot(plane.normal,plane.point-planes_[i].point))<.01) {planes_[i]=plane;return;}
        if(plane_count_<capacity()) {planes_[plane_count_++]=plane;return;}
        std::size_t pick=plane_count_;   // replace the oldest plane of the lowest rank that does not outrank the new one
        for(std::size_t i=0;i<plane_count_;++i) {
            if(planes_[i].rank>plane.rank) continue;
            if(pick==plane_count_ || planes_[i].rank<planes_[pick].rank || (planes_[i].rank==planes_[pick].rank && planes_[i].age>planes_[pick].age)) pick=i;
        }
        if(pick<plane_count_) planes_[pick]=plane;
    }
    HeldClampConfig cfg_;
    bool has_{};std::uint64_t epoch_{};double last_now_{};
    std::array<Plane,plane_capacity> planes_{};std::size_t plane_count_{};
    std::optional<WorldTrackedPose> published_;
    DwellTimer dwell_{};bool released_{},last_slid_{};
};

// The release probes. The engine never runs a cast shorter than min_cast_length (make_cast_request, native_world_trace.hpp: it answers null with a fresh
// serial), so a probe that starts and ends on the proxy centre would read every release as clear. Each probe therefore starts release_probe_length
// outside its proxy sphere (away from the middle of the object; straight up for a sphere at the middle: the usual penetration is into the floor) and
// sweeps to the sphere's centre: a surface between the two, or a start already inside geometry, is the overlap.
inline constexpr double release_probe_length=.08,min_cast_length=1e-3;
inline std::array<ClampSweep,held_proxy_capacity> release_probe_sweeps(const HeldProxies& proxies,const WorldTrackedPose& pose) {
    std::array<ClampSweep,held_proxy_capacity> out{};
    const std::size_t n=std::min(proxies.count,held_proxy_capacity);
    Vec3 middle{};
    for(std::size_t i=0;i<n;++i) middle=middle+proxies.sphere[i].local;
    if(n) middle=middle*(1./static_cast<double>(n));
    for(std::size_t i=0;i<n;++i) {
        const Vec3 centre=clamp_detail::world_point(pose,proxies.sphere[i].local);
        Vec3 away=clamp_detail::rotate(pose.basis,proxies.sphere[i].local-middle);
        const double l=length(away);
        away=l>.01?away*(1/l):Vec3{0,0,1};
        out[i]={centre+away*release_probe_length,centre,proxies.sphere[i].radius,true,std::nullopt};
    }
    return out;
}
// How far inside the world a release probe's answer says the proxy sphere is, or none when the probe is clear. Hit is a WorldTraceHit
// (point, centre, fraction, initial_overlap): a start inside geometry has the engine's penetration data (radius minus the distance from the sphere
// centre to the surface point); a hit along the probe means the sphere's final position lies behind a surface (how far past first contact it is). A
// hit at the very end of the probe is the resting contact of a sphere sitting on the floor, not an overlap.
template<class Hit> std::optional<double> release_probe_depth(const ClampSweep& sweep,const Hit& hit) {
    if(hit.initial_overlap) return std::max(0.,sweep.radius-length(sweep.to-hit.point));
    if(!(hit.fraction<.98)) return std::nullopt;
    return std::min(sweep.radius,length(sweep.to-hit.centre));
}

// Safe release (Part 5A): a released object keeps the DEBRIS layer until a short cast into each proxy sphere from just outside the object reports it free; while it
// overlaps the world it is driven out along the contact normal at no more than max_speed for at most max_seconds, and the layer
// then returns exactly once (restore_layer). When it never frees, the layer returns anyway and `unresolved` is set, so the failure is
// counted. The layer is never restored while checking or depenetrating. No teleport: only a velocity is requested.
enum class ReleasePhase {idle,checking,depenetrating,done};
struct SafeReleaseInput {
    double now{};bool released{};
    std::optional<ClampHit> overlap;   // the answer to the release probes (release_probe_sweeps); none: free
    bool sweep_answered{};             // overlap is this frame's answer (an unanswered cast waits)
    double depth{};};                  // how far inside, metres: the caller's dot(hit.point - pose.position, -normal) clamped >= 0 (preflight B-2)
struct SafeReleaseOutput {ReleasePhase phase{};Vec3 velocity{};bool write{};bool restore_layer{};bool unresolved{};std::uint64_t depen_frames{};};
class SafeRelease {
public:
    static constexpr double max_speed=1.5,max_seconds=.5;
    SafeReleaseOutput update(const SafeReleaseInput& in) {
        SafeReleaseOutput out{};
        out.depen_frames=depen_frames_;
        if(!in.released || !std::isfinite(in.now)) {
            if(phase_==ReleasePhase::done && !in.released) reset();
            out.phase=phase_;
            return out;
        }
        if(phase_==ReleasePhase::idle) {phase_=ReleasePhase::checking;start_=in.now;}
        if(phase_==ReleasePhase::done) {out.phase=phase_;return out;}
        bool pushing=phase_==ReleasePhase::depenetrating;
        if(in.sweep_answered) {
            if(!in.overlap) {return finish(out,false);}
            const ClampHit& hit=*in.overlap;
            const bool normal_ok=hit.normal_valid && finite(hit.normal) && length(hit.normal)>.5;
            if(normal_ok || !hit.normal_valid) {   // a non-finite normal is not an answer at all (below)
                const Vec3 n=normal_ok?unit(hit.normal):Vec3{0,0,1};
                const double depth=std::isfinite(in.depth)?std::max(0.,in.depth):0.;
                velocity_=n*std::min(max_speed,depth*20+.3);
                phase_=ReleasePhase::depenetrating;pushing=true;
            }
        }
        if(in.now-start_>=max_seconds) return finish(out,true);
        out.phase=phase_;
        if(pushing) {out.write=true;out.velocity=velocity_;out.depen_frames=++depen_frames_;}
        return out;
    }
    void reset() {phase_=ReleasePhase::idle;start_=0;velocity_={};}
private:
    SafeReleaseOutput finish(SafeReleaseOutput out,bool unresolved) {
        phase_=ReleasePhase::done;out.phase=phase_;out.restore_layer=true;out.unresolved=unresolved;out.depen_frames=depen_frames_;
        return out;
    }
    ReleasePhase phase_{};double start_{};Vec3 velocity_{};std::uint64_t depen_frames_{};
};

} // namespace d2vr
