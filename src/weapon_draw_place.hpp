#pragma once
// v1106 (round 6): a held weapon is bound to the drawn hand at draw time, with a grip offset G learned once per (weapon, hand, grip).
// The engine places a held weapon once per game tick from the hand it reads there; the arms are drawn from each render's own pose sample
// (v861 image skin), so a per-frame correction of the engine's tick frame F is a race: every frame without a writer pair fell back to the
// tick-late rows, mixing placed and unplaced frames (the "skipped frames" report) and putting depth/velocity and colour in different places
// (the temporal-AA black flicker). The binding replaces the pair/read/gate with one rigid offset:
//
//     G = inverse(H_read) * F          (native engine attach; H_read is the exact hand joint the engine composed from)
//     G = inverse(H_read) * F          (crossed native item; H_read is the origin hand joint, so G carries the engine's y180 correction)
//     G = the offset held_copy::place used, mirrored for a cross-hand copy (the copy service knows it exactly)
//     B = H_image * G                  (this render's drawn hand frame H_image in the one world space, joint 59 for native/cross,
//                                      the controller grip for a teleported copy: the frame that offset was measured in)
//
// and every draw of the weapon, in every eye pass of every render, becomes
//
//     cur'  = VP(eye) * B * inverse(F) * M       (M = the draw's own model matrix, i.e. F-relative part offset)
//     prev' = VP_prev(eye) * B_prev * inverse(F) * M   (the previous render's drawn hand * the same G: true motion)
//
// A bound weapon is placed whether or not a writer note exists for that frame. G is learned on equip (identity change), re-learned on a
// weapon/hand/grip change, validated (|G.translation| < 0.3 m, orthonormal rotation), and otherwise kept. A native/crossed G needs two
// consistent writer compositions before it is accepted (a note can be newer than the frame the engine composed); a copy G is exact and is
// accepted on the first placement. A regrip without an identity change must agree for three renders before replacing G, so per-frame read
// skew never becomes a new binding. Frames without a valid G keep the engine's rows and are counted (fallback), never mixed inside a pass.
//
// Spaces (proved in docs/WEAPON-DRAW-PLACE-TEMPORAL.md, Round 6):
//   * image_world(side,59,render) and image_grip(side,render): the mod's solved world frames of the physical hand (game world, Z up,
//     columns forward/left/up). FullBodyDriver::to_world / image_world return Xf in that frame; weapon_draw::weapon_mat turns one into
//     the column-vector Mat this file uses.
//   * a native writer's read: to_world(frame_of(arm model+0x1860), hand joint in model space) = the same world hand frame; a crossed
//     writer's read is the origin hand joint re-expressed through the destination arm's published frame (uncorrected), so its world
//     equals image_world(origin physical hand,59) up to the render/tick sample difference.
//   * the engine's F at model+0x1860: origin + axis rows in the same world frame; model_of() is its column-vector matrix.
//   * a copy's feed/hand_pose: WorldTrackedPose (world position + forward/left/up), the frame held_copy::to_world composes the offset in;
//     the copy service now reports the applied offset itself, so the binding never has to infer G from a copy's propagated entity frame.
// A 180 degree gate rejection was the y180 cross correction entering the read when the second (uncorrected) reconstruction failed; the
// cross path now keeps the uncorrected joint from the same single composition. A 16 m copy rejection was a hand feed left in the previous
// world after a load/teleport (the stale feed and the copy F it placed agreed with each other, but both were 16 m from the new image hand);
// the copy service drops that feed instead of reporting it, and the binding uses the exact applied offset.
//
// Matrices are row-major, column vectors: clip = M * (x,y,z,1), the engine's cb2 and view+0x7510 storage (z row of a VP = 0,0,0,near).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace d2vr::weapon_draw {
using Mat=std::array<double,16>;

inline Mat identity() noexcept {Mat m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
inline Mat mul(const Mat& a,const Mat& b) noexcept {
    Mat o{};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {double t=0;for(int k=0;k<4;++k) t+=a[i*4+k]*b[k*4+j];o[i*4+j]=t;}
    return o;
}
inline bool inverse(const Mat& m,Mat& out) noexcept {
    double a[4][8];
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {a[i][j]=m[i*4+j];a[i][4+j]=i==j?1.0:0.0;}
    for(int c=0;c<4;++c) {
        int p=c;for(int i=c+1;i<4;++i) if(std::abs(a[i][c])>std::abs(a[p][c])) p=i;
        if(!(std::abs(a[p][c])>1e-12)) return false;
        for(int j=0;j<8;++j) std::swap(a[p][j],a[c][j]);
        const double d=a[c][c];for(int j=0;j<8;++j) a[c][j]/=d;
        for(int i=0;i<4;++i) if(i!=c) {const double k=a[i][c];for(int j=0;j<8;++j) a[i][j]-=k*a[c][j];}
    }
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[i*4+j]=a[i][4+j];
    return true;
}
inline Mat widen(const float* f) noexcept {Mat m{};for(int i=0;i<16;++i) m[i]=f[i];return m;}
inline bool finite(const Mat& m) noexcept {for(const double v:m) if(!std::isfinite(v)) return false;return true;}
inline double distance(const Mat& a,const Mat& b) noexcept {
    return std::hypot(a[3]-b[3],std::hypot(a[7]-b[7],a[11]-b[11]));
}
// Angle between two rotations (radians), given rigid matrices (columns = axes).
inline double angle(const Mat& a,const Mat& b) noexcept {
    const double trace=a[0]*b[0]+a[4]*b[4]+a[8]*b[8]+a[1]*b[1]+a[5]*b[5]+a[9]*b[9]+a[2]*b[2]+a[6]*b[6]+a[10]*b[10];
    return std::acos(std::clamp((trace-1)*.5,-1.,1.));
}
// The angle between two frames' rotations with each axis normalised (model frames carry scales).
inline double frame_angle(const Mat& a,const Mat& b) noexcept {
    double t=0;
    for(int c=0;c<3;++c) {
        double na=0,nb=0,d=0;
        for(int r=0;r<3;++r) {const double x=a[static_cast<std::size_t>(r*4+c)],y=b[static_cast<std::size_t>(r*4+c)];na+=x*x;nb+=y*y;d+=x*y;}
        if(!(na>1e-12 && nb>1e-12)) return 3.14159265358979323846;
        t+=d/std::sqrt(na*nb);
    }
    return std::acos(std::clamp((t-1)*.5,-1.,1.));
}
// A native model frame (model+0x1860: origin, three axis rows, three scales; 15 floats) as the column-vector model matrix:
// column c is axis c * scale c, the last column the origin.
inline Mat model_of(const float* raw) noexcept {
    Mat m=identity();
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+c)]=double(raw[3+c*3+r])*double(raw[12+c]);
    for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+3)]=raw[r];
    return m;
}
// A pose {position, basis[3]} (WorldTrackedPose, held_copy::Pose: world axes as columns) as a column-vector matrix.
template<class Pose> inline Mat pose_mat(const Pose& p) noexcept {
    Mat m=identity();
    for(int c=0;c<3;++c) {m[static_cast<std::size_t>(c)]=p.basis[c].x;m[static_cast<std::size_t>(4+c)]=p.basis[c].y;m[static_cast<std::size_t>(8+c)]=p.basis[c].z;}
    m[3]=p.position.x;m[7]=p.position.y;m[11]=p.position.z;
    return m;
}
// G is a rigid offset in the hand frame: a small translation and an orthonormal rotation (no scale, no shear). A bad candidate is
// rejected and never becomes a binding; while there is no valid G the engine's rows stay (counted as fallback by the publisher).
inline bool valid_grip(const Mat& g) noexcept {
    if(!finite(g)) return false;
    const double t=std::hypot(g[3],std::hypot(g[7],g[11]));
    if(!(t<.3)) return false;   // |G.translation| < 0.3 m
    for(int c=0;c<3;++c) {
        const double n=std::sqrt(g[c]*g[c]+g[c+4]*g[c+4]+g[c+8]*g[c+8]);
        if(!(std::abs(n-1.)<=.02)) return false;
    }
    for(int a=0;a<3;++a) for(int b=a+1;b<3;++b) {
        const double d=g[a]*g[b]+g[a+4]*g[b+4]+g[a+8]*g[b+8];
        if(!(std::abs(d)<=.02)) return false;
    }
    const double det=g[0]*(g[5]*g[10]-g[6]*g[9])-g[1]*(g[4]*g[10]-g[6]*g[8])+g[2]*(g[4]*g[9]-g[5]*g[8]);
    return det>.98;
}

// v1067 (user: "a head, a leg or any object in my hand jitters ... use the same synchronization as the weapons"): a held thing (a grabbed
// prop, a part held with the game's Use) is placed at its draws like a weapon, B = H * G. Its game pose comes from a tick (one interval old
// against the drawn hand), and no writer note pairs it, so G = inverse(H) * F is AVERAGED, and only while the hand is still (the lag is then ~0).
// A hold is rigid in the hand, so the average is the hold. A thing the world stops (a wall) leaves the average: it is
// drawn where the game has it until it agrees again, and a new spot that persists (a regrip, a game seat change) is learned afresh.
class HeldBind {
public:
    static constexpr double settle_s=.7,still_mps=.25,still_rps=1.5,still_tau=.12,learn_s=.15,off_m=.15,off_rad=.6,relearn_s=.5;
    // One render: H = the drawn hand (joint 59), F = the thing's engine frame (scaled), t = seconds. Returns true with B when it is placed.
    // track_mps: the hand's tracking-space speed (walking excluded; <0 = judge from H); walk_mps: the player's speed (a moving player's
    // physics lag is no blocked thing: the off tolerance grows by 0.15 s of it).
    bool frame(unsigned hand,std::uintptr_t id,const Mat& H,const Mat& F,double t,Mat& out,double track_mps=-1,double walk_mps=0) {
        if(hand>1 || !id) return false;
        S& s=s_[hand];Mat hi{};
        if(!finite(H) || !finite(F) || !inverse(H,hi)) return false;
        const Mat meas=mul(hi,F);
        if(s.id!=id) {s=S{};s.id=id;s.g=meas;s.start=s.last_t=t;s.last_h=H;note(hand,o_new);return false;}
        const double dt=std::clamp(t-s.last_t,0.,.1);
        const double speed=dt>0?distance(H,s.last_h)/dt:0,turn=dt>0?angle(H,s.last_h)/dt:0;
        s.last_t=t;s.last_h=H;
        const double walk=std::isfinite(walk_mps)?std::clamp(walk_mps,0.,10.):0.;
        const bool still=(track_mps>=0?track_mps:speed)<still_mps && turn<still_rps && walk<still_mps;   // the player walking is no still hand either: the physics trails
        // v1069 (user, v1067: "a screwdriver still appears inside my hand, the hand does not touch it"): the hold is learned ONLY from a still hand,
        // the settle included (v1067 learned the first 0.7 s from any motion, so a pickup on the move baked its lag into the hold for good), and it
        // is placed only once learn_s of still frames went into it. Until then the thing is drawn where the game has it, and G keeps up with it.
        if(t-s.start<settle_s || s.learned<learn_s) {
            if(still) {blend(s.g,meas,std::min(1.,dt/.05));if(t-s.start>=.25) s.learned+=dt;} else if(s.learned<=0) s.g=meas;   // counted after the 0.2 s seat nudge
            note(hand,o_learning);return false;
        }
        const double hand_mps=std::clamp(track_mps>=0?track_mps:speed,0.,10.);
        const double off=off_m+.15*(walk+hand_mps);   // v1071: a swinging hand's physics lag is no blocked thing either (the hand is no longer dragged onto the prop)
        if(distance(meas,s.g)>off || angle(meas,s.g)>off_rad) {   // the thing is not where the hold says (a wall stops it, or it was moved)
            if(!still) {s.off_since=0;note(hand,o_off_moving);return false;}   // a moving hand's physics lag is no new spot
            if(!s.off_since) s.off_since=t;
            if(t-s.off_since<relearn_s) {note(hand,o_off_still);return false;}
            s.g=meas;s.start=t;s.learned=0;s.off_since=0;note(hand,o_relearn);return false; // off while still for 0.5 s: a new hold spot, learned again
        }
        s.off_since=0;
        if(still) blend(s.g,meas,std::min(1.,dt/still_tau));         // a moving hand teaches nothing (its lag is not the hold)
        out=mul(H,s.g);note(hand,o_placed);return finite(out);
    }
    void clear(unsigned hand) {if(hand<2) s_[hand]=S{};}
    // from eased toward to (s 0..1): the matrix blend with the axes made orthogonal again, keeping each axis's length (the model's scales).
    static Mat eased(const Mat& from,const Mat& to,double s) noexcept {Mat g=from;blend(g,to,std::clamp(s,0.,1.));return g;}
    // Diagnostics (status held_bind): per hand, how many renders ended in each outcome.
    enum Outcome : unsigned {o_new,o_learning,o_off_moving,o_off_still,o_relearn,o_placed,outcomes};
    std::uint64_t count(unsigned hand,unsigned o) const noexcept {return hand<2 && o<outcomes?counts_[hand][o].load(std::memory_order_relaxed):0;}
private:
    struct S {std::uintptr_t id{};Mat g{};double start{},last_t{},off_since{},learned{};Mat last_h{};};
    std::array<S,2> s_{};
    std::array<std::array<std::atomic<std::uint64_t>,outcomes>,2> counts_{};
    void note(unsigned hand,unsigned o) noexcept {counts_[hand][o].fetch_add(1,std::memory_order_relaxed);}
    // Linear blend, then the 3x3 made orthogonal again with each column keeping its length (the model's scales).
    static void blend(Mat& g,const Mat& m,double a) {
        for(std::size_t i=0;i<12;++i) g[i]+=(m[i]-g[i])*a;
        double c[3][3],n[3];
        for(int k=0;k<3;++k) {for(int r=0;r<3;++r) c[k][r]=g[static_cast<std::size_t>(r*4+k)];n[k]=std::sqrt(c[k][0]*c[k][0]+c[k][1]*c[k][1]+c[k][2]*c[k][2]);}
        if(!(n[0]>1e-9 && n[1]>1e-9 && n[2]>1e-9)) return;
        for(int k=0;k<3;++k) {
            for(int j=0;j<k;++j) {double d=0;for(int r=0;r<3;++r) d+=c[k][r]*c[j][r];for(int r=0;r<3;++r) c[k][r]-=d*c[j][r];}
            double l=std::sqrt(c[k][0]*c[k][0]+c[k][1]*c[k][1]+c[k][2]*c[k][2]);if(!(l>1e-9)) return;
            for(int r=0;r<3;++r) c[k][r]/=l;
        }
        for(int k=0;k<3;++k) for(int r=0;r<3;++r) g[static_cast<std::size_t>(r*4+k)]=c[k][r]*n[k];
    }
};

// The two frames a held weapon can be composed from. Native and crossed items are bound to the engine's hand ATTACHMENT joint 59;
// a copy is teleported from the controller GRIP (the authored arm socket moves the attachment relative to the grip, and the copy's
// offset was measured in the grip frame). G is always in world axes; only the hand frame it is relative to changes.
enum HandSpace : unsigned {space_joint59,space_grip};

enum WriteKind : unsigned {write_engine,write_cross,write_mirror,write_held};
enum ReadFrame : unsigned {read_attachment,read_grip};
struct GripCandidate {
    bool attempted{},valid{};
    Mat g=d2vr::weapon_draw::identity();
    std::uintptr_t identity{};
    unsigned source{},space{space_joint59};
    std::uint64_t serial{},frame{};
};
// G = inverse(read) * engine (native/crossed): the item-to-joint transform the engine composed, including its item tag / cross
// correction because `read` is the uncorrected hand joint.
inline bool candidate_from_read(const Mat& read,const Mat& engine,GripCandidate& out,std::uintptr_t identity,
        unsigned source,unsigned space,std::uint64_t serial=0,std::uint64_t frame=0) noexcept {
    out=GripCandidate{};out.attempted=true;
    Mat inverse_read{};
    if(!finite(read) || !finite(engine) || !inverse(read,inverse_read)) return false;
    out.g=mul(inverse_read,engine);out.valid=valid_grip(out.g);
    out.identity=identity;out.source=source;out.space=space;out.serial=serial;out.frame=frame;
    return out.valid;
}
// G = the exact local offset a copy service applied (already mirrored for the other hand).
inline bool candidate_from_offset(const Mat& offset,GripCandidate& out,std::uintptr_t identity,unsigned source,
        std::uint64_t serial=0,std::uint64_t frame=0) noexcept {
    out=GripCandidate{};out.attempted=true;
    if(!finite(offset)) return false;
    out.g=offset;out.valid=valid_grip(out.g);
    out.identity=identity;out.source=source;out.space=space_grip;out.serial=serial;out.frame=frame;
    return out.valid;
}

// The per-hand binding. A new identity (or source/space: equip, weapon swap, cross-state change) drops the old G and learns afresh;
// the same identity keeps G, and only a persistent different composition (a regrip) replaces it. No per-frame read or gate participates.
class Binder {
public:
    static constexpr double same_m=.02,same_deg=5.,refresh_m=.05,refresh_deg=15.;
    struct State {
        bool bound{};
        Mat g=d2vr::weapon_draw::identity();
        std::uintptr_t identity{};
        unsigned source{},space{space_joint59};
        std::uint64_t learned_serial{},learned_frame{};
        unsigned age{};
        Mat pending=d2vr::weapon_draw::identity();
        unsigned pending_n{};
        std::uintptr_t pending_identity{};
        unsigned pending_source{},pending_space{};
        std::uint64_t learns{},refreshes{},rejects{},fallbacks{};
        unsigned sanity_n{};double sanity_gap_m{},sanity_deg{},sanity_max_m{};
        bool sanity_idle{};   // v1234: the hand held nothing since the last sample: the next sample starts a new hold's numbers
    };
    static bool same_grip(const Mat& a,const Mat& b) noexcept {
        return distance(a,b)<=same_m && angle(a,b)<=same_deg*3.14159265358979323846/180.;
    }
    // One observation per render. A candidate is nil for a frame with no writer note/offset; a bound G is then simply kept.
    static bool observe(unsigned hand,const GripCandidate& candidate) {
        if(hand>1) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        State& s=states_[hand];
        if(!candidate.identity) {if(candidate.attempted) ++s.rejects;if(!s.bound) ++s.fallbacks;return s.bound;}
        if(s.bound && (s.identity!=candidate.identity || s.source!=candidate.source || s.space!=candidate.space)) {
            reset_binding(s);   // a different weapon/hand/grip: the old G is not a base, the counters stay cumulative
        }
        if(candidate.identity && s.pending_n && (candidate.identity!=s.pending_identity || candidate.source!=s.pending_source || candidate.space!=s.pending_space)) {
            reset_binding(s);   // a weapon swap before the first G: never combine two identities' compositions
        }
        if(!s.bound) {
            if(!candidate.valid) {if(candidate.attempted) ++s.rejects;++s.fallbacks;return false;}
            if(s.pending_n && same_grip(candidate.g,s.pending)) ++s.pending_n;
            else {s.pending=candidate.g;s.pending_n=1;s.pending_identity=candidate.identity;s.pending_source=candidate.source;s.pending_space=candidate.space;}
            const bool exact=candidate.source==write_held || candidate.source==write_mirror;
            const unsigned needed=exact?1u:2u;   // a copy service's offset is exact; a writer note can be newer than F
            if(s.pending_n>=needed) {
                s.bound=true;s.g=candidate.g;s.identity=candidate.identity;s.source=candidate.source;s.space=candidate.space;
                s.learned_serial=candidate.serial;s.learned_frame=candidate.frame;s.age=0;s.pending_n=0;++s.learns;
                return true;
            }
            ++s.fallbacks;
            return false;
        }
        ++s.age;
        if(candidate.valid) {
            if(same_grip(candidate.g,s.g)) {s.pending_n=0;}
            else {
                if(s.pending_n && same_grip(candidate.g,s.pending)) ++s.pending_n; else {s.pending=candidate.g;s.pending_n=1;}
                if(s.pending_n>=3) {
                    s.g=candidate.g;s.learned_serial=candidate.serial;s.learned_frame=candidate.frame;s.age=0;s.pending_n=0;++s.refreshes;
                }
            }
        } else if(candidate.attempted) ++s.rejects;
        return true;
    }
    // v1234: a render with no composition still names what this hand draws (held = its copy entity / native model, with the source and
    // space a composition of it would carry). A nil candidate used to keep G whatever the hand now held: after a toss or swap, until a
    // note arrived, the new weapon was published at the OLD weapon's G (drawn off the hand). Now a different holder drops G like a new identity.
    static bool observe(unsigned hand,GripCandidate candidate,std::uintptr_t held,unsigned held_source,unsigned held_space) {
        if(!candidate.identity) {candidate.identity=held;candidate.source=held_source;candidate.space=held_space;}
        return observe(hand,candidate);
    }
    // v1234: this hand holds nothing this render (diagnostics only: G is kept for the same weapon's return).
    static void idle(unsigned hand) {if(hand>1) return;std::lock_guard<std::mutex> lock(mutex_);states_[hand].sanity_idle=true;}
    // B = H * G for this render's drawn hand H (joint 59 or grip, matching the G's space).
    static bool frame(unsigned hand,const Mat& hand_frame,Mat& out) {
        if(hand>1) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!states_[hand].bound || !finite(hand_frame)) return false;
        out=mul(hand_frame,states_[hand].g);return finite(out);
    }
    static bool bound(unsigned hand) {if(hand>1) return false;std::lock_guard<std::mutex> lock(mutex_);return states_[hand].bound;}
    static State state(unsigned hand) {if(hand>1) return State{};std::lock_guard<std::mutex> lock(mutex_);return states_[hand];}
    // Diagnostics only: engine frame vs bound frame. Never gates, never changes G.
    static void note_sanity(unsigned hand,const Mat& engine,const Mat& bound_frame) {
        if(hand>1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        State& s=states_[hand];
        if(s.sanity_idle) {s.sanity_idle=false;s.sanity_n=0;s.sanity_gap_m=s.sanity_deg=s.sanity_max_m=0;}   // v1234: per hold
        const double gap=distance(engine,bound_frame);
        s.sanity_gap_m=gap;s.sanity_deg=angle(engine,bound_frame)*57.29577951308232;
        if(gap>s.sanity_max_m) s.sanity_max_m=gap;++s.sanity_n;
    }
    static void clear() {std::lock_guard<std::mutex> lock(mutex_);states_={};}
    struct Summary {bool bound{};unsigned source{},space{},age{},pending{};double g_m{};unsigned long long learns{},refreshes{},rejects{},fallbacks{},sanity_n{};double sanity_mm{},sanity_deg{},sanity_max_mm{};};
    static Summary summary(unsigned hand) {
        Summary out;
        if(hand>1) return out;
        std::lock_guard<std::mutex> lock(mutex_);
        const State& s=states_[hand];
        out.bound=s.bound;out.source=s.source;out.space=s.space;out.age=s.age;out.pending=s.pending_n;
        out.g_m=std::hypot(s.g[3],std::hypot(s.g[7],s.g[11]));
        out.learns=s.learns;out.refreshes=s.refreshes;out.rejects=s.rejects;out.fallbacks=s.fallbacks;
        out.sanity_n=s.sanity_n;out.sanity_mm=s.sanity_gap_m*1000.;out.sanity_deg=s.sanity_deg;out.sanity_max_mm=s.sanity_max_m*1000.;
        return out;
    }
private:
    static void reset_binding(State& s) noexcept {
        const auto learns=s.learns,refreshes=s.refreshes,rejects=s.rejects,fallbacks=s.fallbacks;
        s=State{};
        s.learns=learns;s.refreshes=refreshes;s.rejects=rejects;s.fallbacks=fallbacks;
    }
    inline static std::array<State,2> states_{};
    inline static std::mutex mutex_;
};

// The known-writer log: the writers state the exact world hand pose they composed/moved the item from. The binder uses the newest
// applicable note only to learn/refresh G; it is not consulted for a frame that already has G.
struct Write {
    std::uint64_t serial{};
    std::uintptr_t source{};
    int engine_side{-1};
    unsigned kind{};
    unsigned read_frame{read_attachment};
    std::uint64_t frame{};
    Mat read{};
};
class WriteLog {
public:
    static constexpr unsigned ring=16;
    static std::uint64_t note(unsigned physical,std::uintptr_t source,int engine_side,unsigned kind,const float* read,
            std::uint64_t frame=0,unsigned read_frame=read_attachment) noexcept {
        if(physical>1 || !read) return 0;
        const Mat pose=model_of(read);
        if(!finite(pose)) return 0;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        for(unsigned i=0;i<8 && !lock.owns_lock();++i) if(lock.try_lock()) break;
        if(!lock.owns_lock()) {dropped_.fetch_add(1,std::memory_order_relaxed);return 0;}
        Slot& s=slots_[physical];
        Write& w=s.ring[s.next];
        w.serial=reserve();
        w.source=source;w.engine_side=engine_side;w.kind=kind;w.read_frame=read_frame;w.frame=frame;w.read=pose;
        s.next=(s.next+1)%ring;++s.total;
        return w.serial;
    }
    static std::uint64_t dropped() noexcept {return dropped_.load(std::memory_order_relaxed);}
    static std::uint64_t latest() noexcept {return serial_.load(std::memory_order_relaxed);}
    static std::uint64_t reserve() noexcept {return serial_.fetch_add(1,std::memory_order_relaxed)+1;}
    static unsigned writes(unsigned physical,Write* out) noexcept {
        if(physical>1 || !out) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        const Slot& s=slots_[physical];
        const unsigned n=s.total<ring?s.total:ring,start=(s.next+ring-n)%ring;
        for(unsigned i=0;i<n;++i) out[i]=s.ring[(start+i)%ring];
        return n;
    }
    static void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        slots_={};serial_.store(0,std::memory_order_relaxed);generation_.fetch_add(1,std::memory_order_relaxed);
    }
    static std::uint64_t generation() noexcept {return generation_.load(std::memory_order_relaxed);}
    struct NativeRoute {int arm{-1};unsigned kind{write_engine};};
    static NativeRoute native_route(unsigned physical,unsigned cross_mask) noexcept {
        if(physical>1) return {};
        const unsigned other=physical^1u;
        if(cross_mask&(1u<<other)) return {static_cast<int>(other),write_cross};
        if(!(cross_mask&(1u<<physical))) return {static_cast<int>(physical),write_engine};
        return {};
    }
    // The native notes that can have composed one physical hand's live frame, chronological: only the arm the hand carries and only
    // the writer kind that arm's cross state allows. `arm`/`kind` report the selection even when no write matches.
    static unsigned native_candidates(unsigned physical,unsigned cross_mask,const bool live[2],const Write* w,unsigned n,
            Write* out,std::uint64_t now=0,std::uint64_t max_age=3,int* arm=nullptr,unsigned* kind=nullptr) noexcept {
        if(arm) *arm=-1;
        if(kind) *kind=write_engine;
        const NativeRoute route=native_route(physical,cross_mask);
        if(route.arm<0 || !live || !live[route.arm] || !out) return 0;
        if(arm) *arm=route.arm;
        if(kind) *kind=route.kind;
        unsigned count=0;
        for(unsigned i=0;i<n;++i) {
            if(now && w[i].frame && w[i].frame+max_age<now) continue;
            if(w[i].engine_side==route.arm && w[i].kind==route.kind) out[count++]=w[i];
        }
        return count;
    }
private:
    struct Slot {unsigned total{},next{};std::array<Write,ring> ring{};};
    inline static std::mutex mutex_{};
    inline static std::array<Slot,2> slots_{};
    inline static std::atomic<std::uint64_t> serial_{};
    inline static std::atomic<std::uint64_t> dropped_{};
    inline static std::atomic<std::uint64_t> generation_{1};
};

// The two views' view-projections (index = the mod's eye 0/1) of the render, and per side the views of that side's previous published
// render. The previous drawn hand is the previous publish's bound frame B_prev (the same G), so prev rows are VP_prev * B_prev * inv(F) * M.
struct Views {
    bool ok[2]{};
    Mat vp[2]{},inv[2]{};
    std::uint32_t prev_frame[2]{};
    bool prev_ok[2][2]{};
    Mat prev_vp[2][2]{},prev_inv[2][2]{};
};
enum Stage:unsigned {rejected,no_views,no_certificate,far_away,ambiguous,unbound,placed_same_frame,placed_last_frame,placed_tagged,placed_pass,stages};
// alt (v1072): placed from the previous read F. extra (v1237): matched one of the held thing's attached children's frames.
// near_side/near_like (v1237 census, unplaced draws only): the draw's model origin is within a published side's reach (of F, the previous
// read or a child); like = its axes are also within 0.1 rad of that frame (the held thing composed at another pose, a part we did not place).
struct Result {int side{-1},eye{-1},hint{-1};Stage stage{rejected};bool alt{},extra{};int near_side{-1};bool near_like{};bool predicted{};int age{};}; // v1239: predicted match / ring age

struct Side {
    bool valid{},have_prev{},bound{},rigid{};   // rigid (v1223): a held thing (reach > 0): its draws use RigidLocal's settled L
    double reach{};   // v1067: >0 = this side's own match radius (a held thing: 0.5 m since v1072, its parts sit at offsets from F)
    std::uint32_t prev_frame{};
    Mat model{},delta{},prev{},relative{},bound_frame{},prev_bound{};
    // v1106: the previous DRAWN hand per eye (the same B in a paired render, kept per eye so a separately rendered eye can never take
    // the other eye's history). prev_eye[eye] already includes G: prev rows are VP_prev(eye) * prev_eye[eye] * inv(F) * M.
    std::array<Mat,2> prev_eye{};
    std::array<bool,2> prev_eye_ok{};
    // v1072 (user: "a carried object is out of sync with the hand and flickers when running"): the engine composes a render's per-instance
    // constants BEFORE the eye-0 begin where F is read (v901), and the game thread can write the next tick's F in between. Those draws carry
    // the PREVIOUS read F of the same thing: a moving thing then missed every match (a held thing's 2 cm reach; a turning weapon's 0.004
    // axes) and stayed at the engine's lagging pose for that render - the flicker. alt = that previous read, with its own exact delta/prev.
    bool alt{};Mat alt_model{},alt_delta{};std::array<Mat,2> alt_prev_eye{};
    // v1237 (user v1233: "the helmet was completely out of place" on a carried severed head): entities the engine binds to the held thing
    // (its +0xb0 children: a helmet, a hat) follow F rigidly but have their own model frame, so a draw of one never matched F's axes and stayed
    // at the engine's lagging pose beside the placed thing. Their frames are matched too; delta is the held thing's (B * inverse(F)).
    static constexpr unsigned max_extra=4;
    unsigned extras{};std::array<Mat,max_extra> extra{};
    // v1239 (held things only): the publisher's earlier distinct reads of the thing (ages 1..7, the previous read included), each an exact
    // match of the tick it was read at (frame index 2+max_extra+j), and the predicted-match gates from the thing's speed (0.05 s of motion).
    static constexpr unsigned max_hist=7;
    unsigned hists{};std::array<Mat,max_hist> hist{};std::array<unsigned,max_hist> hist_age{};
    double gate_m{},gate_rad{};
};
struct Slot {std::atomic<std::uint32_t> ready{};std::array<Side,2> side{};};

// v1223 (user: "I need loose props behave same as bodies (zero ghosting)"): a held thing's draw is placed at B * L, L = inverse(F) * M its
// transform in the thing's frame. For a rigid part L is a constant, but measured per draw it is inverse(F_read) * F_draw * L: a carried physics
// prop's draws are not always composed from the frame the publisher read, so the measured L - and the drawn prop - wobbled against the hand.
// v1231: consensus, not consecutiveness. A physically held prop is velocity-driven (headset: 1.8-2.6 m/s median), so a draw composed one tick
// away from the read frame is off by centimetres and that may be the common case. The true L recurs exactly whenever read and draw are in step;
// mismatched samples scatter with the body's motion. Per draw (key: vertex shader + cb2 size, then the nearest L within 12 cm) up to 6 candidate
// values with hit counts; a candidate unseen for `window` renders expires. L settles on the candidate with the most hits once it has >= 3
// (non-consecutive); the settled value is replaced only when it has stopped recurring for `window` renders and another candidate has >= 3 hits
// (a genuine change: new model, non-rigid part). Before settling, a candidate with >= 2 hits is used over the raw sample. Samples are folded at
// the next publish, so every draw of one render (both eyes, every pass) uses the same L. A new held thing (publish id) resets the side.
// Status per publisher path (set_path): samples agreeing with the settled L vs not = how often read and draw are out of step. build/no-rigid-local: off.
class RigidLocal {
public:
    inline static std::atomic<bool> on{true};
    static constexpr double agree_m=.0005,agree_rad=.0035,gate_m=.12; // gate: a draw one or two 16 ms ticks off at 3 m/s is still the same draw
    static constexpr unsigned settle_n=3,early_n=2,cands=6,window=45,clusters=8,max_age=240,paths=5; // window: ~0.5 s at 90 Hz
    enum Path:unsigned {path_unknown,path_physical,path_use,path_bind,path_severed}; // v1237: severed = a physically held arkSeveredLimb part
    static constexpr unsigned bins=11; // deviation histogram edges in mm: .05 .1 .2 .5 1 2 5 10 20 50, then above
    static void set_path(unsigned side,unsigned path) noexcept {if(side<2) path_[side].store(path<paths?path:0u,std::memory_order_relaxed);}
    // Draw time: the L to place this draw with; false = none (place with the measured one). Records the render's sample.
    // v1239: ref = the frame L is relative to (0 the thing, 2+k its child k); draw_m = the draw's model: this exact match takes the cluster
    // for this render (a predicted draw of another object is then refused).
    static bool use(unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& measured,Mat& out,unsigned ref=0,const Mat* draw_m=nullptr) noexcept {
        if(side>1 || !on.load(std::memory_order_relaxed)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& cs=side_[side];Cluster* best=nullptr;double best_d=gate_m;Cluster* free_slot=nullptr;
        for(auto& c:cs) {
            if(!c.active) {if(!free_slot) free_slot=&c;continue;}
            if(c.key!=key || c.ref!=ref) continue;
            const double d=distance(c.anchor,measured);
            if(d<best_d) {best_d=d;best=&c;}
        }
        // v1237 (user v1233 session: unsettled_draws 24,292 of 73,771 with 66 settles and 16 resets): a full side refused every new draw for the
        // rest of the hold (a cluster was freed only after 240 renders unused). The cluster unused longest (not drawn for >= 2 renders) is recycled.
        if(!best && !free_slot) {
            for(auto& c:cs) if(c.active && c.age>=2 && (!free_slot || c.age>free_slot->age)) free_slot=&c;
            if(free_slot) ++evictions_;
        }
        if(!best) {if(!free_slot) {out=canonical(side,frame,ref,measured);++unsettled_;++unsettled_why_[0];return false;} *free_slot=Cluster{};free_slot->active=true;free_slot->key=key;free_slot->ref=ref;free_slot->anchor=measured;best=free_slot;}
        best->age=0;
        if(draw_m) {
            if(best->taken_frame!=frame+1) {best->taken_frame=frame+1;best->taken_m=*draw_m;}
            else if(!best->taken_exact && !same_object(best->taken_m,*draw_m)) {remember(side,key,frame,best->taken_m);best->taken_m=*draw_m;} // a predicted taker was another object
            best->taken_exact=true;
        }
        unsigned active=0;for(const auto& c:cs) active+=c.active?1u:0u;if(active>active_max_) active_max_=active;
        if(best->pending_frame!=frame+1) {best->pending=measured;best->pending_frame=frame+1;}
        const auto used=[&](const Mat& l) { // v1237 diagnostics: a draw placed >5 mm from its own measured L (out of step, or a key shared by two parts),
            if(distance(l,measured)>.005 || angle(l,measured)>.0175) ++far_uses_;   // and draws of one cluster in one render placed with different L
            if(best->used_frame==frame+1) {const double d=distance(l,best->used)*1000.;if(d>.05) {++render_disagree_;if(d>render_disagree_mm_) render_disagree_mm_=d;}}
            else {best->used_frame=frame+1;best->used=l;}
        };
        // v1241: one bitwise L for every pass of a mesh (depth and colour passes are different keys, so different clusters whose L agree only to float
        // noise; a forward pass testing depth against a prepass placed one ulp nearer loses those pixels). The value used is canonical: the
        // earliest-settled cluster of this side and frame whose settled L agrees with it, else the first value used this render that agrees.
        if(best->settled_ok) {out=canonical(side,frame,ref,best->settled);++settled_uses_;used(out);return true;}
        if(const Cand* c=best->top();c && c->hits>=early_n) {out=canonical(side,frame,ref,c->value);++early_uses_;used(out);return true;}
        out=canonical(side,frame,ref,measured);   // v1241: unsettled, out is still the L to place with (this render's shared sample)
        ++unsettled_;++unsettled_why_[1];used(out);return false;
    }
    // v1241 counter: a placed draw of a held side (its L, the view-projection it was placed with, the 16 floats written). Two draws of one render
    // with the same VP and agreeing L (the passes of one mesh) must have written bit-identical rows; pass_spread_renders counts renders where not.
    static void note_placed(unsigned side,unsigned ref,std::uint32_t frame,const Mat& l,const Mat& vp,const float* out) noexcept {
        if(side>1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(placed_frame_!=frame+1) {placed_frame_=frame+1;placed_n_=0;placed_spread_=false;}
        for(unsigned i=0;i<placed_n_;++i) {
            const auto& p=placed_[i];
            if(p.side!=side || p.ref!=ref || p.vp!=vp || !agree(p.l,l)) continue;
            ++pass_pairs_;
            if(std::memcmp(p.out.data(),out,sizeof p.out)!=0 && !placed_spread_) {placed_spread_=true;++pass_spread_renders_;}
            return;
        }
        if(placed_n_<placed_.size()) {auto& p=placed_[placed_n_++];p.side=side;p.ref=ref;p.l=l;p.vp=vp;std::memcpy(p.out.data(),out,sizeof p.out);}
    }
    // v1239 predicted match: a draw no read matches (composed from a tick the publisher did not read) is recognised through a SETTLED L of its
    // key: raw_draw = m * inverse(L) is the thing's pose the draw implies. Accepted when it is a rigid frame like the latest one (axes lengths
    // within 2 %, orthogonal), within w.gate_m / w.gate_rad of the latest frame, not an object seen unplaced at that exact pose this or the
    // last two renders (a resting twin: a stack of plates), and the cluster not taken this render by another object (an exact match always
    // wins; between two predicted ones the first is placed, and a nearer later one takes the cluster from the next pass on). Never a sample.
    // why: 0 no settled cluster of this key, 1 not rigid, 2 far, 3 turned, 4 taken, 5 taken by a farther one (this draw takes over).
    struct Prediction {bool ok{};unsigned ref{},why{};Mat x{},l{};double mm{},deg{};};   // v1241 l: the (canonical) L of x = ref * l
    template<class RefOf>
    static Prediction predict(unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& m,double gate,double gate_rad,RefOf&& ref_of) noexcept {
        Prediction p;
        if(side>1 || !on.load(std::memory_order_relaxed)) return p;
        std::lock_guard<std::mutex> lock(mutex_);
        Cluster* best=nullptr;Mat best_ref{};double best_d=0,best_a=0;
        for(auto& c:side_[side]) {
            if(!c.active || !c.settled_ok || c.key!=key) continue;
            const Mat* r=ref_of(c.ref);Mat il{};
            if(!r || !inverse(c.settled,il)) continue;
            const Mat raw=mul(m,il);
            if(!like_frame(raw,*r)) {p.why=std::max(p.why,1u);continue;}
            const double d=distance(raw,*r);if(!(d<=gate)) {p.why=std::max(p.why,2u);continue;}
            const double a=frame_angle(raw,*r);if(!(a<=gate_rad)) {p.why=std::max(p.why,3u);continue;}
            if(!best || d<best_d) {best=&c;best_ref=*r;best_d=d;best_a=a;}
        }
        if(!best) return p;
        for(const auto& o:others_[side]) if(o.frame && o.key==key && frame+1-o.frame<=3 && same_object(o.m,m)) {p.why=4;return p;}
        if(best->taken_frame==frame+1 && !same_object(best->taken_m,m)) {
            if(best->taken_exact || best_d>=best->taken_d) {p.why=4;return p;}
            remember(side,key,frame,best->taken_m);best->taken_m=m;best->taken_d=best_d;p.why=5;return p;
        }
        if(best->taken_frame!=frame+1) {best->taken_frame=frame+1;best->taken_exact=false;best->taken_m=m;best->taken_d=best_d;}
        best->age=0;
        p.ok=true;p.ref=best->ref;p.l=canonical(side,frame,best->ref,best->settled);p.x=mul(best_ref,p.l);p.mm=best_d*1000.;p.deg=best_a*57.29577951308232;   // v1241: the canonical L
        return p;
    }
    // v1239: an unplaced draw near a held thing (another object, or the thing itself at a pose no read or prediction accepted).
    static void other(unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& m) noexcept {
        if(side>1) return;
        std::lock_guard<std::mutex> lock(mutex_);remember(side,key,frame,m);
    }
    // Publish time (render boundary): the last render's samples into each cluster's consensus; a changed held thing resets the side.
    static void fold(unsigned side,std::uintptr_t id) noexcept {
        if(side>1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(id!=id_[side]) {if(id_[side]) ++resets_;id_[side]=id;side_[side]={};others_[side]={};render_l_[side]={};return;}
        const unsigned path=path_[side].load(std::memory_order_relaxed);
        for(auto& c:side_[side]) {
            if(!c.active) continue;
            ++c.renders;
            if(!c.pending_frame) {if(++c.age>max_age) c=Cluster{};continue;}
            const Mat s=c.pending;c.pending_frame=0;++samples_;
            for(auto& k:c.cand) if(k.hits && c.renders-k.last>window) k=Cand{};   // not seen for a window: expired
            Cand* hit=nullptr;for(auto& k:c.cand) if(k.hits && agree(s,k.value)) {hit=&k;break;}
            if(hit) {++hit->hits;hit->last=c.renders;}
            else {Cand* slot=&c.cand[0];for(auto& k:c.cand) if(!k.hits || k.hits<slot->hits || (k.hits==slot->hits && k.last<slot->last)) {slot=&k;if(!k.hits) break;}
                  *slot=Cand{s,1,c.renders};hit=slot;}
            if(c.settled_ok) {
                const bool in_step=agree(s,c.settled);
                ++step_seen_[path][in_step?0:1];
                note(dev_,dev_max_um_,dev_max_mdeg_,s,c.settled);   // how far the measured (v1222) placement was from the rigid one
                if(in_step) {c.settled_last=c.renders;continue;}
                ++outliers_;
                // a genuine change: the settled value stopped recurring for a whole window and another value recurs
                if(c.renders-c.settled_last>window) if(const Cand* t=c.top();t && t->hits>=settle_n && !agree(t->value,c.settled)) {
                    note(step_,step_max_um_,step_max_mdeg_,t->value,c.settled);c.settled=t->value;c.anchor=t->value;c.settled_last=t->last;c.settled_seq=++settle_seq_;++follows_;
                }
                continue;
            }
            if(const Cand* t=c.top();t && t->hits>=settle_n) {c.settled=t->value;c.anchor=t->value;c.settled_ok=true;c.settled_last=t->last;c.settled_seq=++settle_seq_;++settles_;}
            else if(const Cand* t2=c.top()) c.anchor=t2->value;
        }
    }
    static void clear() noexcept {std::lock_guard<std::mutex> lock(mutex_);side_={};id_={};others_={};render_l_={};placed_frame_=0;placed_n_=0;}
    static std::uint64_t settles() noexcept {std::lock_guard<std::mutex> lock(mutex_);return settles_;}
    static std::pair<std::uint64_t,std::uint64_t> pass_counts() noexcept {std::lock_guard<std::mutex> lock(mutex_);return {pass_pairs_,pass_spread_renders_};}   // v1241
    static std::string json() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string o="{\"on\":"+std::string(on.load()?"true":"false")+",\"samples\":"+std::to_string(samples_)+",\"settles\":"+std::to_string(settles_)
            +",\"settled_draws\":"+std::to_string(settled_uses_)+",\"early_draws\":"+std::to_string(early_uses_)+",\"unsettled_draws\":"+std::to_string(unsettled_)
            +",\"outliers\":"+std::to_string(outliers_)+",\"follows\":"+std::to_string(follows_)+",\"resets\":"+std::to_string(resets_)
            +",\"unsettled_why\":["+std::to_string(unsettled_why_[0])+","+std::to_string(unsettled_why_[1])+"],\"evictions\":"+std::to_string(evictions_) // v1237: [no cluster, too few hits]
            +",\"active_max\":"+std::to_string(active_max_)+",\"far_uses\":"+std::to_string(far_uses_)
            +",\"render_disagree\":"+std::to_string(render_disagree_)+",\"render_disagree_mm\":"+std::to_string(render_disagree_mm_)
            +",\"pass_pairs\":"+std::to_string(pass_pairs_)+",\"pass_spread_renders\":"+std::to_string(pass_spread_renders_);   // v1241: agreeing draws of one render compared / renders where they differed
        static constexpr const char* path_names[paths]={"unknown","physical","use","bind","severed"};
        o+=",\"in_step\":{";   // per publisher path: samples [agreeing with the settled L, not] = read and draw in step / out of step
        for(unsigned p=0;p<paths;++p) o+=std::string(p?",":"")+"\""+path_names[p]+"\":["+std::to_string(step_seen_[p][0])+","+std::to_string(step_seen_[p][1])+"]";
        o+="}";
        const auto hist=[&](const char* name,const std::array<std::uint64_t,bins>& h,std::uint64_t um,std::uint64_t mdeg) {
            std::uint64_t n=0;for(auto v:h) n+=v;
            static constexpr double edge[bins]={.05,.1,.2,.5,1,2,5,10,20,50,1e9};
            double p95=0;std::uint64_t run=0;for(unsigned i=0;i<bins;++i) {run+=h[i];if(n && run*100>=n*95) {p95=edge[i];break;}}
            o+=",\""+std::string(name)+"\":{\"n\":"+std::to_string(n)+",\"p95_mm_le\":"+std::to_string(p95)+",\"max_mm\":"+std::to_string(um/1000.)
              +",\"max_deg\":"+std::to_string(mdeg/1000.)+",\"hist_mm\":[";
            for(unsigned i=0;i<bins;++i) o+=(i?",":"")+std::to_string(h[i]);
            o+="]}";
        };
        hist("measured_vs_rigid",dev_,dev_max_um_,dev_max_mdeg_);hist("rigid_steps",step_,step_max_um_,step_max_mdeg_);
        return o+'}';
    }
private:
    struct Cand {Mat value{};unsigned hits{},last{};};
    struct Cluster {
        bool active{},settled_ok{};std::uint64_t key{};Mat anchor{},settled{},pending{},used{};std::array<Cand,cands> cand{};
        unsigned renders{},settled_last{},age{};std::uint32_t pending_frame{},used_frame{};
        unsigned ref{};std::uint32_t taken_frame{};bool taken_exact{};Mat taken_m{};double taken_d{};   // v1239: L's frame; this render's taker
        std::uint64_t settled_seq{};   // v1241: settle order (the earliest-settled agreeing cluster's L is the canonical one)
        const Cand* top() const noexcept {const Cand* t=nullptr;for(const auto& k:cand) if(k.hits && (!t || k.hits>t->hits || (k.hits==t->hits && k.last>t->last))) t=&k;return t;}
    };
    static bool agree(const Mat& a,const Mat& b) noexcept {return distance(a,b)<=agree_m && angle(a,b)<=agree_rad;}
    // v1241 (mutex_ held): the L every pass of this render places with when it agrees with `l`: the earliest-settled cluster of the side and frame
    // ref whose settled L agrees, else the first L used this render (same side and ref) that agrees, else `l` itself (then the render's first).
    static Mat canonical(unsigned side,std::uint32_t frame,unsigned ref,const Mat& l) noexcept {
        const Cluster* first=nullptr;
        for(const auto& c:side_[side]) if(c.active && c.settled_ok && c.ref==ref && agree(c.settled,l) && (!first || c.settled_seq<first->settled_seq)) first=&c;
        if(first) return first->settled;
        auto& r=render_l_[side];
        if(r.frame!=frame+1) {r.frame=frame+1;r.n=0;}
        for(unsigned i=0;i<r.n;++i) if(r.ref[i]==ref && agree(r.l[i],l)) return r.l[i];
        if(r.n<r.l.size()) {r.ref[r.n]=ref;r.l[r.n++]=l;}
        return l;
    }
    struct RenderL {std::uint32_t frame{};unsigned n{};std::array<unsigned,16> ref{};std::array<Mat,16> l{};};
    inline static std::array<RenderL,2> render_l_{};
    inline static std::uint64_t settle_seq_{};
    struct Placed {unsigned side{},ref{};Mat l{},vp{};std::array<float,16> out{};};
    inline static std::array<Placed,32> placed_{};inline static unsigned placed_n_{};inline static std::uint32_t placed_frame_{};inline static bool placed_spread_{};
    inline static std::uint64_t pass_pairs_{},pass_spread_renders_{};
    // v1239: one object's model (the same draw in another eye or pass) vs another object's
    static bool same_object(const Mat& a,const Mat& b) noexcept {return distance(a,b)<=.001 && frame_angle(a,b)<=.01;}
    // a rigid frame with the reference's axis lengths (its scales), orthogonal, right-handed like it
    static bool like_frame(const Mat& a,const Mat& r) noexcept {
        if(!finite(a)) return false;
        double n[3]{};
        for(int c=0;c<3;++c) {
            double na=0,nr=0;for(int k=0;k<3;++k) {const auto i=static_cast<std::size_t>(k*4+c);na+=a[i]*a[i];nr+=r[i]*r[i];}
            na=std::sqrt(na);nr=std::sqrt(nr);if(!(nr>1e-9) || !(std::abs(na/nr-1.)<=.02)) return false;n[c]=na;
        }
        for(int x=0;x<3;++x) for(int y=x+1;y<3;++y) {
            double d=0;for(int k=0;k<3;++k) d+=a[static_cast<std::size_t>(k*4+x)]*a[static_cast<std::size_t>(k*4+y)];
            if(!(std::abs(d/(n[x]*n[y]))<=.02)) return false;
        }
        const auto det=[](const Mat& g) {return g[0]*(g[5]*g[10]-g[6]*g[9])-g[1]*(g[4]*g[10]-g[6]*g[8])+g[2]*(g[4]*g[9]-g[5]*g[8]);};
        return det(a)*det(r)>0;
    }
    struct Other {std::uint64_t key{};std::uint32_t frame{};Mat m{};};   // frame = render + 1 (0 = empty)
    static constexpr unsigned others_n=24;
    inline static std::array<std::array<Other,others_n>,2> others_{};
    static void remember(unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& m) noexcept {
        Other* slot=&others_[side][0];
        for(auto& o:others_[side]) {
            if(o.frame && o.key==key && same_object(o.m,m)) {o.frame=frame+1;return;}   // a resting object: the same entry, refreshed
            if(!o.frame || o.frame<slot->frame) slot=&o;
        }
        *slot=Other{key,frame+1,m};
    }
    static void note(std::array<std::uint64_t,bins>& h,std::uint64_t& max_um,std::uint64_t& max_mdeg,const Mat& a,const Mat& b) noexcept {
        static constexpr double edge[bins]={.05,.1,.2,.5,1,2,5,10,20,50,1e9};
        const double mm=distance(a,b)*1000.,deg=angle(a,b)*57.29577951308232;
        unsigned i=0;while(i+1<bins && !(mm<=edge[i])) ++i;++h[i];
        if(mm*1000.>static_cast<double>(max_um)) max_um=static_cast<std::uint64_t>(mm*1000.);
        if(deg*1000.>static_cast<double>(max_mdeg)) max_mdeg=static_cast<std::uint64_t>(deg*1000.);
    }
    inline static std::mutex mutex_;
    inline static std::array<std::array<Cluster,clusters>,2> side_{};
    inline static std::array<std::uintptr_t,2> id_{};
    inline static std::array<std::atomic<unsigned>,2> path_{};
    inline static std::array<std::array<std::uint64_t,2>,paths> step_seen_{};
    inline static std::uint64_t samples_{},settles_{},settled_uses_{},early_uses_{},unsettled_{},outliers_{},follows_{},resets_{};
    inline static std::array<std::uint64_t,2> unsettled_why_{};inline static std::uint64_t evictions_{},far_uses_{},render_disagree_{};
    inline static unsigned active_max_{};inline static double render_disagree_mm_{};
    inline static std::array<std::uint64_t,bins> dev_{},step_{};
    inline static std::uint64_t dev_max_um_{},dev_max_mdeg_{},step_max_um_{},step_max_mdeg_{};
};

class Place {
public:
    static constexpr unsigned ring=4;
    inline static std::atomic<double> axes_tol{0.004},reach{0.8},identity_tol{0.0015},origin_tie{0.01};
    inline static std::atomic<float> near_w{3.f};

    // Render thread, at the eye-0 begin of the render that draws `frame`. raw[s] = the engine's F (15 floats); bound[s] = B = H_image*G
    // or null while no valid G exists (the engine's rows stay). The old publish(frame,raw,delta) form is kept for the labs and treats
    // delta*F as the bound frame.
    static constexpr double alt_max_m=.6;   // a previous read farther than this from F is another thing or a teleport, never an alternative
    static void publish_bound(std::uint32_t frame,const std::array<const float*,2>& raw,const std::array<const Mat*,2>& bound,const std::array<double,2>& side_reach={0.,0.},
            const std::array<std::uintptr_t,2>& ids={0,0},double now_s=-1,const std::array<const float*,2>& body_read={nullptr,nullptr}) noexcept {
        if(now_s<0) now_s=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        Slot& slot=ring_[frame%ring];
        slot.ready.store(0,std::memory_order_release);
        for(unsigned s=0;s<2;++s) {
            roll_census(s,frame);kind_[s].store(0,std::memory_order_relaxed);
            Side& out=slot.side[s];out=Side{};
            Mat model{},inverse_model{};
            Read& rd=reads_[s];
            RigidLocal::fold(s,raw[s] && side_reach[s]>0?ids[s]:0);   // v1223: the last render's L samples; a new held thing resets
            if(!raw[s]) {last_[s]={};rd={};continue;}
            model=model_of(raw[s]);
            if(!finite(model) || !inverse(model,inverse_model)) {last_[s]={};rd={};continue;}
            if(rd.cur_ok && rd.cur_frame!=frame) {rd.prior=rd.cur;rd.prior_id=rd.cur_id;rd.prior_ok=true;}   // a republish of the same render keeps its prior
            rd.cur=model;rd.cur_id=ids[s];rd.cur_frame=frame;rd.cur_ok=true;
            if(rd.ring_id!=ids[s] || !ids[s]) {rd.ring_n=0;rd.ring_id=ids[s];}   // v1239: the ring holds one thing's distinct reads
            if(!rd.ring_n || distance(rd.ring[0],model)>1e-7 || frame_angle(rd.ring[0],model)>1e-7) {
                for(unsigned k=std::min<unsigned>(rd.ring_n,Side::max_hist);k>0;--k) {rd.ring[k]=rd.ring[k-1];rd.ring_t[k]=rd.ring_t[k-1];}
                rd.ring[0]=model;rd.ring_t[0]=now_s;rd.ring_n=std::min<unsigned>(rd.ring_n+1,Side::max_hist+1);
            }
            // v1255 (user, v1254: standing still the held prop "flips between two positions ~3-10 cm apart"): the rigid sync that makes the engine's
            // frame the hold's goal (held_render_pose.hpp v1249) runs on a physics step, not on every render. A draw composed before that step carries
            // the PHYSICS BODY's own frame, which the publisher never reads (it reads the frame after the step): no published read matched it, the draw
            // stayed at the trailing body, and the next render put it back on the hand - the flip. The body frame native_grab sampled for this hold is
            // published as one more earlier read of the same thing: a draw composed from it matches an exact historical read (base B, L relative to
            // that read), so it is placed on the hand like every other frame.
            if(body_read[s] && ids[s] && rd.ring_n) {
                const Mat body_model=model_of(body_read[s]);
                if(finite(body_model) && distance(body_model,model)<=alt_max_m &&
                   (distance(rd.ring[0],body_model)>1e-7 || frame_angle(rd.ring[0],body_model)>1e-7)) {
                    const unsigned last=std::min<unsigned>(rd.ring_n,Side::max_hist);
                    for(unsigned k=last;k>1;--k) {rd.ring[k]=rd.ring[k-1];rd.ring_t[k]=rd.ring_t[k-1];}
                    rd.ring[1]=body_model;rd.ring_t[1]=now_s;rd.ring_n=std::min<unsigned>(rd.ring_n+1,Side::max_hist+1);
                }
            }
            out.valid=true;out.model=model;out.reach=side_reach[s];out.rigid=side_reach[s]>0;
            if(!bound[s] || !finite(*bound[s])) {last_[s]={};continue;}   // no G yet: no placement, no previous chain
            out.bound=true;out.bound_frame=*bound[s];
            out.delta=mul(*bound[s],inverse_model);
            for(unsigned k=0;k<staged_[s].extras && out.extras<Side::max_extra;++k) if(finite(staged_[s].extra[k]) && distance(staged_[s].extra[k],model)<=alt_max_m) out.extra[out.extras++]=staged_[s].extra[k];
            if(out.extras) children_.fetch_add(out.extras,std::memory_order_relaxed);
            kind_[s].store(staged_[s].kind,std::memory_order_relaxed);
            Last& l=last_[s];
            if(l.valid && l.frame!=frame) {older2_[s]=older_[s];older_[s]=l;} // v1191: the last two earlier publishes (one-view frames: the other
            else if(!l.valid) older_[s]=older2_[s]={};                       // eye's render one frame back, this eye's previous render two back)
            if(l.valid && l.frame==frame) {out.prev=l.prev;out.relative=l.relative;out.prev_frame=l.prev_frame;out.have_prev=l.have_prev;}
            else if(l.valid && frame-l.frame<ring) {out.prev=mul(l.placed,inverse_model);out.relative=mul(l.model,inverse_model);out.prev_frame=l.frame;out.have_prev=true;}
            else {out.prev=out.delta;out.relative=identity();}
            out.prev_eye[0]=out.prev_eye[1]=out.prev;
            out.prev_eye_ok[0]=out.prev_eye_ok[1]=out.have_prev;
            if(Mat inverse_prior{};ids[s] && rd.prior_ok && rd.prior_id==ids[s] && distance(rd.prior,model)<=alt_max_m &&
               (distance(rd.prior,model)>1e-7 || angle(rd.prior,model)>1e-7) && inverse(rd.prior,inverse_prior)) {
                out.alt=true;out.alt_model=rd.prior;out.alt_delta=mul(*bound[s],inverse_prior);
                out.alt_prev_eye[0]=out.alt_prev_eye[1]=mul(mul(out.prev,model),inverse_prior);   // B_prev * inverse(F_prior)
            }
            if(out.rigid && ids[s]) {   // v1239: the earlier distinct reads (exact matches of their ticks) and the predicted-match gates
                for(unsigned k=1;k<rd.ring_n && out.hists<Side::max_hist;++k) {
                    const Mat& h=rd.ring[k];
                    if(distance(h,model)>alt_max_m || (out.alt && distance(h,out.alt_model)<=1e-7 && frame_angle(h,out.alt_model)<=1e-7)) continue;
                    out.hist[out.hists]=h;out.hist_age[out.hists++]=k;
                }
                double mps=0,rps=0;   // the thing's speed: the latest read against the newest earlier read at least 8 ms old
                for(unsigned k=1;k<rd.ring_n;++k) if(const double dt=now_s-rd.ring_t[k];dt>=.008) {mps=distance(model,rd.ring[k])/dt;rps=frame_angle(model,rd.ring[k])/dt;break;}
                out.gate_m=std::min(.05+mps*.05,out.reach);
                out.gate_rad=std::min((10.+rps*57.29577951308232*.05)/57.29577951308232,45./57.29577951308232);
            }
            l={true,out.have_prev,frame,out.prev_frame,model,*bound[s],out.prev,out.relative};
            published_side_[s].fetch_add(1,std::memory_order_relaxed);
        }
        staged_={};
        latest_frame_.store(frame,std::memory_order_release);
        slot.ready.store(frame+1,std::memory_order_release);
        published_.fetch_add(1,std::memory_order_relaxed);
    }
    static void publish(std::uint32_t frame,const std::array<const float*,2>& raw,const std::array<const Mat*,2>& delta) noexcept {
        std::array<Mat,2> bound{};
        std::array<const Mat*,2> bound_ptr{};
        for(unsigned s=0;s<2;++s) if(raw[s] && delta[s] && finite(*delta[s])) {
            bound[s]=mul(*delta[s],model_of(raw[s]));
            if(finite(bound[s])) bound_ptr[s]=&bound[s];
        }
        publish_bound(frame,raw,bound_ptr);
    }
    // v1237 census (user v1233: plates, teacups, parts and bodies ghost at any speed). Per held-thing kind: renders a side was published bound,
    // renders in which it was placed somewhere AND a near draw of the same axes stayed unplaced (mixed: two images of one thing), renders placed
    // without any draw carrying previous rows (no motion vector of its own: the TAA/upscaler reprojects it as background), renders with no draw
    // placed at all; and per draw placed / with previous rows / child-matched / unplaced near-like / unplaced near-other.
    enum Kind:unsigned {kind_none,kind_weapon,kind_prop,kind_use,kind_bind,kind_severed,kinds};
    // Publisher, before publish_bound: what this side holds and the frames of the entities bound to it (children, max Side::max_extra).
    static void stage(unsigned s,unsigned kind,const Mat* extra=nullptr,unsigned n=0) noexcept {
        if(s>1) return;
        staged_[s].kind=kind<kinds?kind:0u;
        for(unsigned k=0;extra && k<n && staged_[s].extras<Side::max_extra;++k) staged_[s].extra[staged_[s].extras++]=extra[k];
    }
    // v1259 per-eye census (user v1257: a held prop at another position in one eye while the Timepiece is open): a render's placed draws by eye
    // (census bits 16 eye 0, 32 eye 1): 0 both eyes, 1 eye 0 only, 2 eye 1 only, 3 neither named. Split by whether the render had a secondary
    // pass (render_secondary, set by the publisher before publish_bound); slot 3 of the status = renders placed in both eyes from ONE publish.
    static constexpr unsigned eye_slot(unsigned bits) noexcept {return (bits&16u) && (bits&32u)?0u:(bits&16u)?1u:(bits&32u)?2u:3u;}
    static void render_secondary(bool on) noexcept {secondary_next_.store(on,std::memory_order_relaxed);}
    // ObjectMotion, per draw that reached the placement (rig draws excluded): its result and whether previous rows were written for it.
    // frame: the publish the draw was placed from (paired passes; 0 = not recorded).
    static void census(const Result& r,bool with_prev,std::uint32_t frame=0) noexcept {
        const int s=r.side>=0?r.side:r.near_side;if(s<0 || s>1) return;
        const unsigned k=kind_[static_cast<unsigned>(s)].load(std::memory_order_relaxed);
        auto& d=draws_[k];
        if(r.side>=0) {
            d[0].fetch_add(1,std::memory_order_relaxed);if(with_prev) d[1].fetch_add(1,std::memory_order_relaxed);if(r.extra) d[2].fetch_add(1,std::memory_order_relaxed);
            bits_[static_cast<unsigned>(s)].fetch_or(8u|(with_prev?3u:1u),std::memory_order_relaxed);
            if(r.eye==0 || r.eye==1) {   // v1259
                bits_[static_cast<unsigned>(s)].fetch_or(16u<<r.eye,std::memory_order_relaxed);
                if(frame) {std::uint32_t none=0;eye_frame_[static_cast<unsigned>(s)][static_cast<unsigned>(r.eye)].compare_exchange_strong(none,frame+1,std::memory_order_relaxed);}
            }
        } else {
            d[r.near_like?3:4].fetch_add(1,std::memory_order_relaxed);
            bits_[static_cast<unsigned>(s)].fetch_or(8u|(r.near_like?4u:0u),std::memory_order_relaxed);   // v1254: bit 3 = a draw reached the placement at all
        }
    }
    // v1240 diagnostic (user v1233: a carried severed head drew with BLACK skin): per distinct placed draw of a held severed part (first 8 keys):
    // vertex shader, cb2 size, reflected layout, placed draws per pass, and the frame it was placed with relative to the thing (L = inverse(F) * x):
    // its axis lengths / orthogonality (a degenerate or non-rigid L) and, per render, the largest spread between the L of different keys
    // (depth pass vs colour pass placed apart: the colour pass then fails the depth test the placed depth pass wrote).
    static void severed_draw(const Result& r,std::uint64_t key,std::uint64_t layout,unsigned size,unsigned pass) noexcept {
        if(r.side<0 || r.side>1 || kind_[static_cast<unsigned>(r.side)].load(std::memory_order_relaxed)!=kind_severed) return;
        const Mat l=last_l_;
        std::lock_guard<std::mutex> lock(sev_mutex_);
        SevDraw* d=nullptr;
        for(unsigned i=0;i<sev_n_;++i) if(sev_[i].key==key) d=&sev_[i];
        if(!d && sev_n_<sev_.size()) {d=&sev_[sev_n_++];d->key=key;d->layout=layout;d->size=size;}
        if(d) {
            d->passes|=pass<64?1ull<<pass:1ull<<63;++d->placed;
            double n[3]{};
            for(int c=0;c<3;++c) {for(int k=0;k<3;++k) n[c]+=l[static_cast<std::size_t>(k*4+c)]*l[static_cast<std::size_t>(k*4+c)];n[c]=std::sqrt(n[c]);}
            for(int c=0;c<3;++c) {d->scale_min=std::min(d->scale_min,n[c]);d->scale_max=std::max(d->scale_max,n[c]);}
            for(int x=0;x<3;++x) for(int y=x+1;y<3;++y) {
                double dot=0;for(int k=0;k<3;++k) dot+=l[static_cast<std::size_t>(k*4+x)]*l[static_cast<std::size_t>(k*4+y)];
                const double e=n[x]*n[y]>1e-12?std::abs(dot/(n[x]*n[y])):1.;if(!(e<=d->ortho_max)) d->ortho_max=e;
            }
            d->l_mm={l[3]*1000.,l[7]*1000.,l[11]*1000.};
        }
        const std::uint32_t f=sev_frame_;   // per render: the spread of L between different keys
        if(last_frame_l_!=f+1) {last_frame_l_=f+1;sev_first_=l;sev_first_key_=key;}
        else if(key!=sev_first_key_ && !r.extra) {const double um=distance(l,sev_first_)*1e6;++sev_pairs_;if(um>50) ++sev_apart_;if(um>sev_spread_um_) sev_spread_um_=um;}
    }
    static std::string severed_json() {
        std::lock_guard<std::mutex> lock(sev_mutex_);
        char b[64];
        std::string o="{\"pairs\":"+std::to_string(sev_pairs_)+",\"pairs_apart_50um\":"+std::to_string(sev_apart_)+",\"spread_um_max\":"+std::to_string(sev_spread_um_)+",\"draws\":[";
        for(unsigned i=0;i<sev_n_;++i) {
            const auto& d=sev_[i];
            std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(d.key));
            o+=std::string(i?",":"")+"{\"key\":\""+b+"\",\"cb2\":"+std::to_string(d.size);
            std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(d.layout));
            o+=",\"layout\":\""+std::string(b)+"\"";
            std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(d.passes));
            o+=",\"pass_bits\":\""+std::string(b)+"\",\"placed\":"+std::to_string(d.placed)+",\"scale\":["+std::to_string(d.scale_min)+","+std::to_string(d.scale_max)
              +"],\"ortho_max\":"+std::to_string(d.ortho_max)+",\"l_mm\":["+std::to_string(d.l_mm[0])+","+std::to_string(d.l_mm[1])+","+std::to_string(d.l_mm[2])+"]}";
        }
        return o+"]}";
    }
    static std::string census_json() {
        static constexpr const char* names[kinds]={"none","weapon","prop","use","bind","severed"};
        std::string o="{";
        for(unsigned k=1;k<kinds;++k) {
            o+=std::string(k>1?",":"")+"\""+names[k]+"\":{\"renders\":"+std::to_string(renders_[k][0].load())+",\"mixed\":"+std::to_string(renders_[k][1].load())
              +",\"no_mv\":"+std::to_string(renders_[k][2].load())+",\"unplaced\":"+std::to_string(renders_[k][3].load())
              +",\"unplaced_nodraw\":"+std::to_string(renders_[k][4].load())+",\"unplaced_refused\":"+std::to_string(renders_[k][3].load()-renders_[k][4].load())
              +",\"draws\":{\"placed\":"+std::to_string(draws_[k][0].load())+",\"with_prev\":"+std::to_string(draws_[k][1].load())+",\"child\":"+std::to_string(draws_[k][2].load())
              +",\"near_like\":"+std::to_string(draws_[k][3].load())+",\"near_other\":"+std::to_string(draws_[k][4].load())+"},\"match\":"+match_json(k)+"}";
        }
        // v1239: predicted matches, how far the draw's implied pose was from the latest read (mm edges 1 2 5 10 20 50 100 200; deg .25 .5 1 2 5 10 20 45)
        o+=",\"predicted_offset\":{\"mm\":[";for(unsigned i=0;i<9;++i) o+=(i?",":"")+std::to_string(pred_mm_[i].load());
        o+="],\"deg\":[";for(unsigned i=0;i<9;++i) o+=(i?",":"")+std::to_string(pred_deg_[i].load());
        o+="]},\"children_published\":"+std::to_string(children_.load());
        // v1259: renders with a held thing placed, by eye [both, eye 0 only, eye 1 only, both from one publish], with / without a secondary pass
        return o+",\"eyes\":"+eyes_json()+'}';
    }
    static std::string eyes_json() {
        std::string o="{";
        for(unsigned l=0;l<2;++l) {o+=l?"],\"lens\":[":"\"plain\":[";for(unsigned i=0;i<4;++i) o+=(i?",":"")+std::to_string(eyes_[l][i].load());}
        return o+"]}";
    }
    static void clear() noexcept {RigidLocal::clear();for(auto& s:ring_) s.ready.store(0,std::memory_order_release);last_={};older_={};older2_={};reads_={};latest_frame_.store(0,std::memory_order_relaxed);}
    static std::uint64_t published() noexcept {return published_.load(std::memory_order_relaxed);}
    static std::uint64_t published(unsigned side) noexcept {return side<2?published_side_[side].load(std::memory_order_relaxed):0;}
    static std::uint32_t latest_frame() noexcept {return latest_frame_.load(std::memory_order_acquire);}
    static const Slot* slot_of(std::uint32_t frame) noexcept {
        const Slot& s=ring_[frame%ring];
        return s.ready.load(std::memory_order_acquire)==frame+1?&s:nullptr;
    }
    static bool still(const Slot& s,std::uint32_t frame) noexcept {return s.ready.load(std::memory_order_acquire)==frame+1;}
    using Rows=std::array<double,12>;
    static Rows model_rows(const double* inv,const float* rows) noexcept {
        Rows m{};
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {double t=0;for(int k=0;k<4;++k) t+=inv[r*4+k]*rows[k*4+c];m[static_cast<std::size_t>(r*4+c)]=t;}
        return m;
    }
    static double worst(const Rows& a,const Rows& b) noexcept {double w=0;for(std::size_t i=0;i<12;++i) {const double d=std::abs(a[i]-b[i]);if(!(d<=w)) w=d;}return w;}
    static bool same_orientation(const Rows& m,const Mat& model,double tolerance) noexcept {
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) if(!(std::abs(m[static_cast<std::size_t>(r*4+c)]-model[static_cast<std::size_t>(r*4+c)])<=tolerance)) return false;
        return true;
    }
    static double origin_distance(const Rows& m,const Mat& model) noexcept {
        return std::hypot(m[3]-model[3],std::hypot(m[7]-model[7],m[11]-model[11]));
    }
    static Rows product(const Mat& a,const Rows& m) noexcept {
        Rows o{};
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {double t=a[static_cast<std::size_t>(r*4+3)]*(c==3?1.0:0.0);for(int k=0;k<3;++k) t+=a[static_cast<std::size_t>(r*4+k)]*m[static_cast<std::size_t>(k*4+c)];o[static_cast<std::size_t>(r*4+c)]=t;}
        return o;
    }
    static double reach_of(const Side& w) noexcept {const double r=reach.load(std::memory_order_relaxed);return w.reach>0?std::min(w.reach,r):r;}
    static Mat full(const Rows& m) noexcept {Mat o=identity();for(std::size_t i=0;i<12;++i) o[i]=m[i];return o;}

    // One draw: cur/prev = its cb2 rows; pass_eye names the eye of the render pass (exact). A bound side is placed in every pass
    // (current-only colour layouts included); a side without a valid G reports `unbound` and the caller keeps the engine's rows.
    template<class Get>
    // v1218: still_out (optional) = the camera-only previous of the current binding (this eye's previous view * delta * M; no previous view:
    // the current rows), the fallback when the submitted previous cannot be proven (ObjectMotion, PrevMvpTable::placed_previous).
    static Result place(std::uint32_t frame,Get&& get_views,const float* cur,const float* prev,float* cur_out,float* prev_out,int tag_eye=-1,int pass_eye=-1,float* still_out=nullptr,std::uint64_t draw_key=0) noexcept {
        Result result;
        if(!(cur[15]>0.01f && cur[15]<near_w.load(std::memory_order_relaxed))) return result;
        const Slot* slot=slot_of(frame);
        if(!slot) return result;
        const Views* views_ptr=get_views(frame,slot->side[0].valid?slot->side[0].prev_frame:0,slot->side[1].valid?slot->side[1].prev_frame:0);
        if(!views_ptr) {result.stage=no_views;return result;}
        const Views& views=*views_ptr;
        if(!views.ok[0] || !views.ok[1]) {result.stage=no_views;return result;}
        const double tol=axes_tol.load(std::memory_order_relaxed);
        const bool pass_eye_ok=pass_eye==0 || pass_eye==1;
        Rows cur_model[2]{};
        for(unsigned e=0;e<2;++e) if(views.ok[e] && (!pass_eye_ok || static_cast<int>(e)==pass_eye)) cur_model[e]=model_rows(views.inv[e].data(),cur);
        double side_distance[2]={-1,-1};bool side_oriented=false;int hit[2]={0,0};   // hit: frame_of index (0 F, 1 previous read, 2+k child k)
        for(unsigned s=0;s<2;++s) {
            const Side& w=slot->side[s];
            if(!w.valid) continue;
            for(unsigned e=0;e<2;++e) {
                if(!views.ok[e] || (pass_eye_ok && static_cast<int>(e)!=pass_eye)) continue;
                // v1072: the previous read, v1237: the children (only in a pass with a known eye: the certificate paths compare against F)
                for(int h=0;h<frames_end(w);++h) {   // v1239: the earlier reads of a held thing too
                    if(!has_frame(w,h) || (h && !(w.bound && pass_eye_ok))) continue;
                    const Mat& f=frame_of(w,h);
                    if(!same_orientation(cur_model[e],f,tol)) continue;
                    side_oriented=true;
                    const double d=origin_distance(cur_model[e],f);
                    if(d<=reach_of(w) && (side_distance[s]<0 || d<side_distance[s])) {side_distance[s]=d;hit[s]=h;}
                }
            }
        }
        int candidate=-1;
        if(side_distance[0]>=0 && side_distance[1]>=0) {
            if(std::abs(side_distance[0]-side_distance[1])<=origin_tie.load(std::memory_order_relaxed)) {
                result.stage=ambiguous;result.hint=side_distance[1]<side_distance[0]?1:0;return result;
            }
            candidate=side_distance[1]<side_distance[0]?1:0;
        }
        else if(side_distance[0]>=0) candidate=0;
        else if(side_distance[1]>=0) candidate=1;
        bool predicted=false;Mat predicted_x{},predicted_l{};
        if(candidate<0 && pass_eye_ok && views.ok[pass_eye]) {   // v1239: no read matches: the predicted match (held things with a settled L)
            int ref=0;
            if(predict_place(*slot,full(cur_model[pass_eye]),draw_key,frame,candidate,ref,predicted_x,predicted_l)) {predicted=true;hit[candidate]=ref;}
        }
        if(candidate<0) {result.stage=side_oriented?far_away:rejected;return result;}
        result.hint=candidate;
        const Side& w=slot->side[static_cast<unsigned>(candidate)];
        if(!w.bound) {result.stage=unbound;return result;}
        int eye=-1;bool tagged=false,last_frame=false;
        if(pass_eye_ok) eye=pass_eye;
        else {
            if(!prev) {result.stage=no_certificate;return result;}
            const Rows prev_in_view[2]={model_rows(views.inv[0].data(),prev),model_rows(views.inv[1].data(),prev)};
            const double id=identity_tol.load(std::memory_order_relaxed);
            const auto certify=[&](int e,bool previous_render) {
                if(eye==-2) return;
                if(eye>=0 && eye!=e) {eye=-2;return;}
                eye=e;last_frame=previous_render;
            };
            if(same_orientation(cur_model[1],w.model,tol) && worst(cur_model[1],prev_in_view[0])<=id) certify(1,false);
            if(same_orientation(cur_model[0],w.model,tol) && worst(cur_model[0],prev_in_view[1])<=id) certify(0,false);
            const bool previous=w.have_prev && views.prev_frame[candidate]==w.prev_frame && views.prev_ok[candidate][0] && views.prev_ok[candidate][1];
            if(previous && (eye<0 || eye==-2)) {
                for(unsigned src=0;src<2;++src) if(views.prev_ok[candidate][src]) {
                    const Rows before=model_rows(views.prev_inv[candidate][src].data(),prev);
                    for(unsigned e=0;e<2;++e) if(same_orientation(cur_model[e],w.model,tol) && worst(before,product(w.relative,cur_model[e]))<=id) certify(static_cast<int>(e),true);
                }
            }
            if(eye==-2) eye=-1;
            if(eye<0) {
                if(tag_eye>=0 && tag_eye<2 && same_orientation(cur_model[tag_eye],w.model,tol)) {eye=tag_eye;last_frame=false;tagged=true;}
                else {result.stage=no_certificate;return result;}
            }
        }
        const Rows& model=cur_model[eye];
        const int h=hit[candidate];const bool use_alt=h==1,use_hist=h>=hist0;const Mat& matched=frame_of(w,h);
        if(!predicted && !(origin_distance(model,matched)<=reach_of(w))) {result.stage=far_away;return result;}
        const Mat m=full(model);
        const Mat vp=views.vp[eye];
        const bool previous=w.have_prev && w.prev_eye_ok[eye] && views.prev_frame[candidate]==w.prev_frame && views.prev_ok[candidate][eye];
        Mat hist_delta{},hist_prev{};
        if(use_hist && !rebase(w,h,static_cast<unsigned>(eye),hist_delta,hist_prev)) {result.stage=rejected;return result;}
        const Mat& delta=use_alt?w.alt_delta:use_hist?hist_delta:w.delta;
        const Mat& prev_eye=use_alt?w.alt_prev_eye[static_cast<unsigned>(eye)]:use_hist?hist_prev:w.prev_eye[static_cast<unsigned>(eye)];
        Mat l=predicted_l;const unsigned lref=predicted?static_cast<unsigned>(h):l_ref(h);
        const bool rigid=predicted || rigid_part(w,static_cast<unsigned>(candidate),matched,draw_key,frame,m,h,l);
        const Mat x=predicted?predicted_x:rigid?mul(matched,l):m;   // v1223: F_used * settled L (else M); v1239: predicted = F * L
        note_l(matched,x,frame);   // v1240 severed diagnostic (L relative to the matched frame)
        const Mat now=rigid?rigid_now(w,vp,lref,l):mul(mul(vp,delta),x);   // v1241: a held part: one product for every pass and match kind
        const Mat before=previous?mul(mul(views.prev_vp[candidate][eye],prev_eye),x):mul(mul(vp,prev_eye),x);
        for(std::size_t i=0;i<16;++i) {cur_out[i]=static_cast<float>(now[i]);prev_out[i]=static_cast<float>(before[i]);}
        if(rigid) RigidLocal::note_placed(static_cast<unsigned>(candidate),lref,frame,l,vp,cur_out);   // v1241 pass_spread_renders
        if(still_out) {
            const bool view_before=views.prev_frame[candidate]==w.prev_frame && w.prev_frame && views.prev_ok[candidate][eye];
            const Mat still=view_before?mul(mul(views.prev_vp[candidate][eye],delta),x):now;
            for(std::size_t i=0;i<16;++i) still_out[i]=static_cast<float>(still[i]);
        }
        if(!still(*slot,frame)) {result.stage=rejected;return result;}
        result.side=candidate;result.eye=eye;result.alt=use_alt;result.extra=h>1 && h<hist0;result.predicted=predicted;result.age=age_of(w,h);
        result.stage=pass_eye_ok?placed_pass:tagged?placed_tagged:last_frame?placed_last_frame:placed_same_frame;
        note_match(static_cast<unsigned>(candidate),result);
        return result;
    }
    // v1237 census: Place::place, plus for a draw it did not place whether that draw sits within a published side's reach (Result::near_side).
    template<class Get>
    static Result place(std::uint32_t frame,Get&& get_views,const float* cur,const float* prev,float* cur_out,float* prev_out,int tag_eye,int pass_eye,float* still_out,std::uint64_t draw_key,bool census_near) noexcept {
        Result r=place(frame,get_views,cur,prev,cur_out,prev_out,tag_eye,pass_eye,still_out,draw_key);
        if(r.side>=0 || !census_near || r.stage==no_views || !(cur[15]>0.01f && cur[15]<near_w.load(std::memory_order_relaxed))) return r;
        const Slot* slot=slot_of(frame);if(!slot) return r;
        const Views* v=get_views(frame,slot->side[0].valid?slot->side[0].prev_frame:0,slot->side[1].valid?slot->side[1].prev_frame:0);
        const unsigned e=pass_eye==0 || pass_eye==1?static_cast<unsigned>(pass_eye):tag_eye==1?1u:0u;
        if(v && v->ok[e]) near_of(*slot,model_rows(v->inv[e].data(),cur),r);
        return r;
    }
    // Which bound side's F / previous read / child this unplaced draw's model is within reach of (nearest), and whether its axes nearly agree.
    static void near_of(const Slot& slot,const Rows& m,Result& r) noexcept {
        double best=-1;const Mat mm=full(m);
        for(unsigned s=0;s<2;++s) {
            const Side& w=slot.side[s];if(!w.valid || !w.bound) continue;
            for(int h=0;h<2+static_cast<int>(w.extras);++h) {
                if(h==1 && !w.alt) continue;
                const Mat& f=frame_of(w,h);const double d=origin_distance(m,f);
                if(d>reach_of(w) || (best>=0 && d>=best)) continue;
                best=d;r.near_side=static_cast<int>(s);r.near_like=axes_angle(mm,f)<.1;
            }
        }
    }
    // The angle between two frames' rotations with each axis normalised (model frames carry scales).
    static double axes_angle(const Mat& a,const Mat& b) noexcept {return frame_angle(a,b);}
    static constexpr int hist0=2+static_cast<int>(Side::max_extra);   // v1239: frame index of the first earlier read
    static const Mat& frame_of(const Side& w,int h) noexcept {return h<=0?w.model:h==1?w.alt_model:h<hist0?w.extra[static_cast<std::size_t>(h-2)]:w.hist[static_cast<std::size_t>(h-hist0)];}
    static int frames_end(const Side& w) noexcept {return hist0+static_cast<int>(w.hists);}
    static bool has_frame(const Side& w,int h) noexcept {return h<=0 || (h==1 && w.alt) || (h>1 && h<hist0 && h-2<static_cast<int>(w.extras)) || (h>=hist0 && h<frames_end(w));}
    static int age_of(const Side& w,int h) noexcept {return h==1?1:h>=hist0?static_cast<int>(w.hist_age[static_cast<std::size_t>(h-hist0)]):0;}
    // v1239: delta / previous binding for a draw composed from an earlier read f_h: B * inverse(f_h), B_prev * inverse(f_h)
    static bool rebase(const Side& w,int h,unsigned eye,Mat& delta,Mat& prev_eye) noexcept {
        Mat inv_h{};if(!inverse(frame_of(w,h),inv_h)) return false;
        const Mat r=mul(w.model,inv_h);delta=mul(w.delta,r);prev_eye=mul(w.prev_eye[eye&1u],r);return true;
    }
    // v1239: the predicted match of this draw (model m) on a held side; an unplaced near draw is remembered (a resting twin is never pulled in)
    static RigidLocal::Prediction predicted(const Side& w,unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& m) noexcept {
        RigidLocal::Prediction p;
        if(!w.valid || !w.bound || !w.rigid) return p;
        p=RigidLocal::predict(side,key,frame,m,w.gate_m,w.gate_rad,[&](unsigned ref)->const Mat* {
            return ref==0?&w.model:ref>=2 && ref-2<w.extras?&w.extra[ref-2]:nullptr;});
        if(p.ok) {   // how far the thing would have popped without the prediction: raw_draw vs the latest read
            static constexpr double mm_edge[9]={1,2,5,10,20,50,100,200,1e9},deg_edge[9]={.25,.5,1,2,5,10,20,45,1e9};
            unsigned i=0;while(i<8 && !(p.mm<=mm_edge[i])) ++i;pred_mm_[i].fetch_add(1,std::memory_order_relaxed);
            i=0;while(i<8 && !(p.deg<=deg_edge[i])) ++i;pred_deg_[i].fetch_add(1,std::memory_order_relaxed);
        } else {
            if(p.why) match_[kind_[side].load(std::memory_order_relaxed)][m_refused+std::min(p.why,4u)-1].fetch_add(1,std::memory_order_relaxed);
            if(p.why!=5 && distance(m,w.model)<=reach_of(w)) RigidLocal::other(side,key,frame,m);
        }
        return p;
    }
    // v1239 status: per kind, placed draws by match (now, earlier read by age 1..7, predicted) and predicted refusals
    enum MatchSlot:unsigned {m_now,m_age1,m_predicted=8,m_refused,m_slots=m_refused+4};
    static void note_match(unsigned side,const Result& r) noexcept {
        if(side>1) return;
        const unsigned k=kind_[side].load(std::memory_order_relaxed);
        match_[k][r.predicted?m_predicted:std::min(static_cast<unsigned>(r.age),7u)].fetch_add(1,std::memory_order_relaxed);
    }
    static std::string match_json(unsigned k) {
        auto& a=match_[k];
        std::string o="{\"exact_now\":"+std::to_string(a[m_now].load())+",\"exact_history\":[";
        for(unsigned i=m_age1;i<m_predicted;++i) o+=(i>m_age1?",":"")+std::to_string(a[i].load());
        o+="],\"predicted\":"+std::to_string(a[m_predicted].load())+",\"refused\":{\"not_rigid\":"+std::to_string(a[m_refused].load())
          +",\"far\":"+std::to_string(a[m_refused+1].load())+",\"turned\":"+std::to_string(a[m_refused+2].load())+",\"taken\":"+std::to_string(a[m_refused+3].load())+"}}";
        return o;
    }
    // the matching side's predicted placement for place(): x = ref_latest * L, the thing's delta and previous binding
    static bool predict_place(const Slot& slot,const Mat& m,std::uint64_t key,std::uint32_t frame,int& side_out,int& ref_out,Mat& x,Mat& l) noexcept {
        for(unsigned s=0;s<2;++s) {   // ponytail: side 0 first; two hands holding twins within each other's gate would need a nearest-of-both pick
            const auto p=predicted(slot.side[s],s,key,frame,m);
            if(p.ok) {side_out=static_cast<int>(s);ref_out=static_cast<int>(p.ref);x=p.x;l=p.l;return true;}
        }
        return false;
    }

    // v1191 one-view frames (knob build/alt-weapon-place): place one draw of the render published last, in the one view that draws it.
    // vp = the draw's view-projection (its classified per-view write), prev_vp = this eye's previous render's (the seeded view+0x7590).
    // The part's model rows m = inverse(vp) * rows match a published side by orientation and reach (as place()); current = vp * delta * m
    // (delta = B * inverse(F): the drawn hand's binding this render); previous = prev_vp * (B_own_prev * inverse(F)) * m with B_own_prev the
    // bound frame of the publish two engine frames back (this eye's previous render); without it, prev_vp * delta * m (the hand still,
    // the camera's motion only; Result::alt false and stage placed_tagged count that). Returns the side, else -1.
    static Result place_one_view(const Mat& vp,const Mat& prev_vp,const float* cur,float* cur_out,float* prev_out,bool& own_prev,float* still_out=nullptr,std::uint64_t draw_key=0) noexcept {
        Result result;own_prev=false;
        if(!(cur[15]>0.01f && cur[15]<near_w.load(std::memory_order_relaxed))) return result;
        const std::uint32_t frame=latest_frame();
        const Slot* slot=slot_of(frame);if(!slot) return result;
        Mat inv{};if(!inverse(vp,inv)) {result.stage=no_views;return result;}
        const Rows m=model_rows(inv.data(),cur);
        const double tol=axes_tol.load(std::memory_order_relaxed);
        int candidate=-1,hit=0;double best=-1;
        for(unsigned side=0;side<2;++side) {
            const Side& w=slot->side[side];if(!w.valid || !w.bound) continue;
            for(int h=0;h<frames_end(w);++h) {   // F, the previous read, v1237: the children, v1239: the earlier reads
                if(!has_frame(w,h)) continue;
                const Mat& f=frame_of(w,h);
                if(!same_orientation(m,f,tol)) continue;
                const double d=origin_distance(m,f);
                if(d<=reach_of(w) && (best<0 || d<best)) {best=d;candidate=static_cast<int>(side);hit=h;}
            }
        }
        bool predicted=false;Mat predicted_x{},predicted_l{};
        if(candidate<0 && predict_place(*slot,full(m),draw_key,frame,candidate,hit,predicted_x,predicted_l)) predicted=true;   // v1239
        if(candidate<0) {result.stage=rejected;near_of(*slot,m,result);return result;}
        const Side& w=slot->side[static_cast<unsigned>(candidate)];
        const bool use_alt=hit==1,use_hist=hit>=hist0;const Mat& matched=frame_of(w,hit);
        Mat hist_delta{},hist_prev{};
        if(use_hist && !rebase(w,hit,0,hist_delta,hist_prev)) {result.stage=rejected;return result;}
        Mat l=predicted_l;const unsigned lref=predicted?static_cast<unsigned>(hit):l_ref(hit);
        const bool rigid=predicted || rigid_part(w,static_cast<unsigned>(candidate),matched,draw_key,frame,full(m),hit,l);
        const Mat model=predicted?predicted_x:rigid?mul(matched,l):full(m);
        const Mat& delta=use_alt?w.alt_delta:use_hist?hist_delta:w.delta; // v1223
        note_l(matched,model,frame);   // v1240 severed diagnostic (L relative to the matched frame)
        const Mat now=rigid?rigid_now(w,vp,lref,l):mul(mul(vp,delta),model);   // v1241: one product for every pass and match kind
        Mat before=mul(mul(prev_vp,delta),model);
        if(still_out) for(std::size_t i=0;i<16;++i) still_out[i]=static_cast<float>(before[i]); // v1218: camera-only, the current binding
        const Last& o=older2_[static_cast<unsigned>(candidate)];
        Mat inverse_f{};
        if(o.valid && frame-o.frame==2 && inverse(use_alt || use_hist?matched:w.model,inverse_f)) {before=mul(mul(prev_vp,mul(o.placed,inverse_f)),model);own_prev=true;}
        for(std::size_t i=0;i<16;++i) {cur_out[i]=static_cast<float>(now[i]);prev_out[i]=static_cast<float>(before[i]);}
        if(rigid) RigidLocal::note_placed(static_cast<unsigned>(candidate),lref,frame,l,vp,cur_out);   // v1241 pass_spread_renders
        if(!still(*slot,frame)) {result.stage=rejected;return result;}
        result.side=candidate;result.eye=-1;result.alt=use_alt;result.extra=hit>1 && hit<hist0;result.predicted=predicted;result.age=age_of(w,hit);result.stage=placed_pass;
        note_match(static_cast<unsigned>(candidate),result);return result;
    }
    // v1223: a held thing's part as F_used * L with L the settled rigid local transform (RigidLocal), so delta * part = B * L whatever frame
    // the engine composed this draw from; otherwise the draw's own model rows m (v1222). f = the frame the draw matched (F, previous read, child).
    // v1239: h = the matched frame index (L is kept per frame it is relative to: the thing or child k); the draw takes its cluster this render.
    // v1241: returns whether the draw is a held part placed through L (out: the canonical L to place with, RigidLocal::use; settled, early or this
    // render's shared sample); false = place the draw's own model rows (not a held thing, build/no-rigid-local, a degenerate frame).
    static bool rigid_part(const Side& w,unsigned side,const Mat& f,std::uint64_t key,std::uint32_t frame,const Mat& m,int h,Mat& l) noexcept {
        Mat inv_f{};
        if(!w.rigid || side>1 || !RigidLocal::on.load(std::memory_order_relaxed) || !inverse(f,inv_f)) return false;
        RigidLocal::use(side,key,frame,mul(inv_f,m),l,l_ref(h),&m);
        return true;
    }
    static unsigned l_ref(int h) noexcept {return h>1 && h<hist0?static_cast<unsigned>(h):0u;}   // L is kept relative to the thing (0) or its child h
    // v1241: a held part's current rows VP * (B * L), B the drawn binding of the frame L is relative to (the thing: bound_frame; its child k: B * inverse(F) * E_k).
    // Every pass, eye-matched or one-view, exact or predicted, earlier read or latest, goes through this one product: identical inputs, identical floats.
    static Mat rigid_now(const Side& w,const Mat& vp,unsigned ref,const Mat& l) noexcept {
        const Mat base=ref>=2 && ref-2<w.extras?mul(w.delta,w.extra[ref-2]):w.bound_frame;
        return mul(vp,mul(base,l));
    }
private:
    // v1240 severed diagnostic state (the placing thread's last L; the table under its own mutex)
    static void note_l(const Mat& f,const Mat& x,std::uint32_t frame) noexcept {Mat inv_f{};last_l_=inverse(f,inv_f)?mul(inv_f,x):Mat{};sev_frame_=frame;}
    inline static thread_local Mat last_l_{};inline static thread_local std::uint32_t sev_frame_{};
    struct SevDraw {std::uint64_t key{},layout{},passes{},placed{};unsigned size{};double scale_min{1e9},scale_max{},ortho_max{};std::array<double,3> l_mm{};};
    inline static std::mutex sev_mutex_;inline static std::array<SevDraw,8> sev_{};inline static unsigned sev_n_{};
    inline static std::uint32_t last_frame_l_{};inline static Mat sev_first_{};inline static std::uint64_t sev_first_key_{},sev_pairs_{},sev_apart_{};inline static double sev_spread_um_{};
    struct Staged {unsigned kind{},extras{};std::array<Mat,Side::max_extra> extra{};};
    inline static std::array<Staged,2> staged_{};
    inline static std::array<std::atomic<unsigned>,2> kind_{},bits_{};
    inline static std::array<std::uint32_t,2> census_frame_{};
    inline static std::atomic<bool> secondary_next_{};inline static std::array<bool,2> census_secondary_{};   // v1259 (publisher thread)
    inline static std::array<std::array<std::atomic<std::uint32_t>,2>,2> eye_frame_{};inline static std::array<std::array<std::atomic<std::uint64_t>,4>,2> eyes_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,5>,kinds> renders_{};   // [renders, mixed, no_mv, unplaced, unplaced without any draw]
    inline static std::array<std::array<std::atomic<std::uint64_t>,5>,kinds> draws_{};     // [placed, with_prev, child, near_like, near_other]
    inline static std::atomic<std::uint64_t> children_{};
    inline static std::array<std::array<std::atomic<std::uint64_t>,m_slots>,kinds> match_{};   // v1239
    inline static std::array<std::atomic<std::uint64_t>,9> pred_mm_{},pred_deg_{};
    // The render published last for this side ends here: its tallies into its kind (a republish of the same render adds nothing).
    static void roll_census(unsigned s,std::uint32_t frame) noexcept {
        if(census_frame_[s]==frame+1) return;
        census_frame_[s]=frame+1;
        const unsigned k=kind_[s].load(std::memory_order_relaxed),b=bits_[s].exchange(0,std::memory_order_relaxed);
        const bool lens=census_secondary_[s];census_secondary_[s]=secondary_next_.load(std::memory_order_relaxed);   // v1259: the rolled render's flag; the new one's
        const std::uint32_t f0=eye_frame_[s][0].exchange(0,std::memory_order_relaxed),f1=eye_frame_[s][1].exchange(0,std::memory_order_relaxed);
        if(k && k<kinds && (b&1u)) {const unsigned slot=eye_slot(b);if(slot<3) eyes_[lens?1:0][slot].fetch_add(1,std::memory_order_relaxed);
            if(slot==0 && f0 && f0==f1) eyes_[lens?1:0][3].fetch_add(1,std::memory_order_relaxed);}
        if(!k || k>=kinds) return;
        auto& r=renders_[k];r[0].fetch_add(1,std::memory_order_relaxed);
        if((b&1u) && (b&4u)) r[1].fetch_add(1,std::memory_order_relaxed);
        if((b&1u) && !(b&2u)) r[2].fetch_add(1,std::memory_order_relaxed);
        if(!(b&1u)) {r[3].fetch_add(1,std::memory_order_relaxed);if(!(b&8u)) r[4].fetch_add(1,std::memory_order_relaxed);}   // v1254: [unplaced, of which no draw was drawn at all]
    }
    struct Last {bool valid{},have_prev{};std::uint32_t frame{},prev_frame{};Mat model{},placed{},prev{},relative{};};
    inline static std::array<Last,2> older_{},older2_{};
    struct Read {bool cur_ok{},prior_ok{};std::uint32_t cur_frame{};std::uintptr_t cur_id{},prior_id{};Mat cur{},prior{};
        std::uintptr_t ring_id{};unsigned ring_n{};std::array<Mat,Side::max_hist+1> ring{};std::array<double,Side::max_hist+1> ring_t{};}; // v1239: distinct reads, newest first
    inline static std::array<Read,2> reads_{};
    inline static std::array<Slot,ring> ring_{};
    inline static std::array<Last,2> last_{};
    inline static std::atomic<std::uint64_t> published_{};
    inline static std::array<std::atomic<std::uint64_t>,2> published_side_{};
    inline static std::atomic<std::uint32_t> latest_frame_{};
};
// v1243 (user v1239: held props "not stable in movement", double image, still is fine; weapons fine): the decisive number for a hand-locked
// thing. The arm's palette is the image (FullBodyDriver::begin_image, the same solve B is built from) re-expressed at the GPU skin copy in the
// arm model's LIVE frame f (image_skin_commit), but the arm draw's cb2 carries the model frame M the engine composed for that draw, so the
// drawn joint 59 is E * J59 with E = M * inverse(f) (identity unless the arm model moved between the composition and the copy). Per render and
// held hand: rel = inverse(E * J59) * B (B = the placed thing's bound frame, or an unplaced weapon's engine frame), its deviation from the
// hold's first drawn value (holds older than 0.15 s: after the bind ease), by player speed (HoldSync buckets) and hand speed (< / >= 0.5 m/s);
// plus |E| at the hand by player speed. ponytail: an arm draw is any near draw within 0.15 m / 0.05 rad of f (both arm models share the
// player frame); a match of another object would show as an outlier, never as a steady bias.
class HandLock {
public:
    static constexpr unsigned kinds=6,speeds=3,hand_speeds=2,bins=10;
    static constexpr double mm_edge[bins]={.1,.5,1,2,5,10,20,50,100,1e9},deg_edge[bins]={.05,.1,.25,.5,1,2,5,10,45,1e9};
    static unsigned speed_of(double player_mps) noexcept {return !(player_mps>=.3)?0u:player_mps<2.5?1u:2u;}   // HoldSync::bucket
    // GPU skin copy: the arm slot's palette was written in this live model frame for the render `frame` (Place::latest_frame()).
    static void note_copy(unsigned slot,std::uint32_t frame,const Mat& live) noexcept {
        if(slot>1 || !finite(live)) return;
        std::lock_guard<std::mutex> l(m_);copy_[slot]={true,frame,live};
    }
    // Publisher (eye-0 begin): what this physical hand holds this render, drawn by arm `slot`. idle(): nothing held (the reference resets).
    static void note_publish(unsigned hand,unsigned slot,std::uint32_t frame,unsigned kind,std::uintptr_t id,const Mat& j59,const Mat& thing,
            double player_mps,double hand_mps,double now_s) noexcept {
        if(hand>1 || slot>1 || !kind || kind>=kinds || !finite(j59) || !finite(thing)) {idle(hand);return;}
        std::lock_guard<std::mutex> l(m_);Hand& h=hand_[hand];
        if(h.id!=id || h.kind!=kind) {h=Hand{};h.id=id;h.kind=kind;h.since=now_s;}
        h.frame=frame;h.slot=slot;h.j59=j59;h.thing=thing;h.player=speed_of(player_mps);h.fast=hand_mps>=.5;h.ready=now_s-h.since>=.15;h.sampled=false;
        want_.store(frame+1,std::memory_order_release);
    }
    static void idle(unsigned hand) noexcept {if(hand>1) return;std::lock_guard<std::mutex> l(m_);hand_[hand]=Hand{};}
    static bool wants(std::uint32_t frame) noexcept {return frame && want_.load(std::memory_order_acquire)==frame+1;}
    // A near draw of the render `frame` in an eye pass: m = inverse(VP of that eye) * its cb2 rows.
    static void on_draw(std::uint32_t frame,const Mat& m) noexcept {
        if(want_.load(std::memory_order_acquire)!=frame+1) return;
        std::lock_guard<std::mutex> l(m_);
        for(Hand& h:hand_) {
            if(!h.ready || h.sampled || h.frame!=frame) continue;
            const Copy& c=copy_[h.slot];
            if(!c.ok || c.frame!=frame || !(distance(m,c.live)<=.15 && frame_angle(m,c.live)<=.05)) continue;
            Mat inv_live{},inv_drawn{};
            if(!inverse(c.live,inv_live)) continue;
            const Mat drawn=mul(mul(m,inv_live),h.j59);
            if(!inverse(drawn,inv_drawn)) continue;
            h.sampled=true;add(arm_[h.player],distance(drawn,h.j59)*1000.,mm_edge);
            const Mat rel=mul(inv_drawn,h.thing);
            if(!h.have_ref) {h.ref=rel;h.have_ref=true;continue;}
            Cell& cell=cells_[h.kind][h.player][h.fast?1:0];
            add(cell.mm,distance(rel,h.ref)*1000.,mm_edge);add(cell.deg,frame_angle(rel,h.ref)*57.29577951308232,deg_edge);
        }
    }
    static std::string json() {
        static constexpr const char* kn[kinds]={"none","weapon","prop","use","bind","severed"};
        static constexpr const char* sn[speeds]={"still","walk","run"};
        static constexpr const char* hn[hand_speeds]={"slow","fast"};
        std::lock_guard<std::mutex> l(m_);
        std::string o="{\"arm_e_mm\":{";
        for(unsigned s=0;s<speeds;++s) o+=std::string(s?",":"")+"\""+sn[s]+"\":"+hist_json(arm_[s],mm_edge);
        o+="}";
        for(unsigned k=1;k<kinds;++k) {
            o+=",\""+std::string(kn[k])+"\":{";
            for(unsigned s=0;s<speeds;++s) for(unsigned f=0;f<hand_speeds;++f)
                o+=std::string(s||f?",":"")+"\""+sn[s]+"_"+hn[f]+"\":{\"mm\":"+hist_json(cells_[k][s][f].mm,mm_edge)+",\"deg\":"+hist_json(cells_[k][s][f].deg,deg_edge)+"}";
            o+="}";
        }
        return o+"}";
    }
    // v1247 arm slip: the camera of a render and the cb2 model frame of its arm/body draws come from the same frontend (the Setup the render
    // draws, after that tick's stages); the GPU skin copy runs later in the render and read the arm model's LIVE frame, which the next tick may
    // already have moved. The image palette is then expressed in the wrong frame and the drawn arm slips by the rig's motion. The copy expresses
    // it in the frame snapshotted at that Setup instead (dxgi_probe arm_snap_*). slip(): whether to use the snapshot - bitwise equal frames:
    // no-op (untouched); non-finite or > 0.3 m / 0.5 rad apart (teleport, level load, time shift): skipped, live kept; else applied.
    enum Slip:unsigned {slip_noop,slip_applied,slip_skipped};
    static Slip slip(const float* snap,const float* live,double player_mps) noexcept {
        if(!std::memcmp(snap,live,15*sizeof(float))) {slip_n_[slip_noop].fetch_add(1,std::memory_order_relaxed);return slip_noop;}
        const Mat a=model_of(snap),b=model_of(live);const double d=distance(a,b),r=frame_angle(a,b);
        if(!finite(a) || !finite(b) || !(d<=.3) || !(r<=.5)) {slip_n_[slip_skipped].fetch_add(1,std::memory_order_relaxed);return slip_skipped;}
        slip_n_[slip_applied].fetch_add(1,std::memory_order_relaxed);
        std::lock_guard<std::mutex> l(m_);add(slip_mm_[speed_of(player_mps)],d*1000.,mm_edge);
        return slip_applied;
    }
    static std::string slip_json() {
        static constexpr const char* sn[speeds]={"still","walk","run"};
        std::string o="{\"applied\":"+std::to_string(slip_n_[slip_applied].load())+",\"noop\":"+std::to_string(slip_n_[slip_noop].load())+",\"skipped_large\":"+std::to_string(slip_n_[slip_skipped].load())+",\"mm\":{";
        std::lock_guard<std::mutex> l(m_);
        for(unsigned s=0;s<speeds;++s) o+=std::string(s?",":"")+"\""+sn[s]+"\":"+hist_json(slip_mm_[s],mm_edge);
        return o+"}}";
    }
    static void clear() noexcept {std::lock_guard<std::mutex> l(m_);hand_={};copy_={};cells_={};arm_={};slip_mm_={};for(auto& n:slip_n_) n.store(0);want_.store(0);}
private:
    struct Hist {std::array<std::uint64_t,bins> n{};double max{};};
    struct Cell {Hist mm{},deg{};};
    struct Copy {bool ok{};std::uint32_t frame{};Mat live{};};
    struct Hand {std::uintptr_t id{};unsigned kind{},slot{},player{};bool fast{},ready{},sampled{},have_ref{};std::uint32_t frame{};double since{};Mat j59{},thing{},ref{};};
    static void add(Hist& h,double v,const double* edge) noexcept {
        if(!std::isfinite(v)) return;unsigned i=0;while(i+1<bins && !(v<=edge[i])) ++i;++h.n[i];if(v>h.max) h.max=v;
    }
    static std::string hist_json(const Hist& h,const double* edge) {
        std::uint64_t total=0;for(auto v:h.n) total+=v;
        const auto q=[&](double p) {std::uint64_t run=0;for(unsigned i=0;i<bins;++i) {run+=h.n[i];if(total && static_cast<double>(run)>=p*static_cast<double>(total)) return edge[i];}return 0.;};
        char b[96];std::snprintf(b,sizeof b,"{\"n\":%llu,\"p50_le\":%g,\"p90_le\":%g,\"max\":%.3f}",static_cast<unsigned long long>(total),q(.5),q(.9),h.max);
        return b;
    }
    inline static std::mutex m_;
    inline static std::atomic<std::uint32_t> want_{};
    inline static std::array<Copy,2> copy_{};
    inline static std::array<Hand,2> hand_{};
    inline static std::array<std::array<std::array<Cell,hand_speeds>,speeds>,kinds> cells_{};
    inline static std::array<Hist,speeds> arm_{},slip_mm_{};
    inline static std::array<std::atomic<std::uint64_t>,3> slip_n_{};
};
} // namespace d2vr::weapon_draw
