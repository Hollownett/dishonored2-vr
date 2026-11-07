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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
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
// A native model frame (model+0x1860: origin, three axis rows, three scales; 15 floats) as the column-vector model matrix:
// column c is axis c * scale c, the last column the origin.
inline Mat model_of(const float* raw) noexcept {
    Mat m=identity();
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+c)]=double(raw[3+c*3+r])*double(raw[12+c]);
    for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+3)]=raw[r];
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
    static constexpr double settle_s=.7,still_mps=.25,still_rps=1.5,still_tau=.12,off_m=.15,off_rad=.6,relearn_s=.5;
    // One render: H = the drawn hand (joint 59), F = the thing's engine frame (scaled), t = seconds. Returns true with B when it is placed.
    bool frame(unsigned hand,std::uintptr_t id,const Mat& H,const Mat& F,double t,Mat& out) {
        if(hand>1 || !id) return false;
        S& s=s_[hand];Mat hi{};
        if(!finite(H) || !finite(F) || !inverse(H,hi)) return false;
        const Mat meas=mul(hi,F);
        if(s.id!=id) {s=S{};s.id=id;s.g=meas;s.start=s.last_t=t;s.last_h=H;return false;}
        const double dt=std::clamp(t-s.last_t,0.,.1);
        const double speed=dt>0?distance(H,s.last_h)/dt:0,turn=dt>0?angle(H,s.last_h)/dt:0;
        s.last_t=t;s.last_h=H;
        const bool still=speed<still_mps && turn<still_rps;
        if(distance(meas,s.g)>off_m || angle(meas,s.g)>off_rad) {   // the thing is not where the hold says (a wall stops it, or it was moved)
            if(!still) {s.off_since=0;return false;}                  // a moving hand's physics lag is no new spot
            if(!s.off_since) s.off_since=t;
            if(t-s.off_since<relearn_s) return false;
            s.g=meas;s.start=t;s.off_since=0;return false;            // off while still for 0.5 s: a new hold spot, learned again
        }
        s.off_since=0;
        if(t-s.start<settle_s) blend(s.g,meas,std::min(1.,dt/.05));
        else if(still) blend(s.g,meas,std::min(1.,dt/still_tau));   // a moving hand teaches nothing (its lag is not the hold)
        if(t-s.start<settle_s) return false;                          // the seat is still settling (nudge at 0.2 s, ease, fit)
        out=mul(H,s.g);return finite(out);
    }
    void clear(unsigned hand) {if(hand<2) s_[hand]=S{};}
private:
    struct S {std::uintptr_t id{};Mat g{};double start{},last_t{},off_since{};Mat last_h{};};
    std::array<S,2> s_{};
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
struct Result {int side{-1},eye{-1},hint{-1};Stage stage{rejected};};

struct Side {
    bool valid{},have_prev{},bound{};
    double reach{};   // v1067: >0 = this side's own match radius (a held thing: its draws share F, and a twin prop nearby must not match)
    std::uint32_t prev_frame{};
    Mat model{},delta{},prev{},relative{},bound_frame{},prev_bound{};
    // v1106: the previous DRAWN hand per eye (the same B in a paired render, kept per eye so a separately rendered eye can never take
    // the other eye's history). prev_eye[eye] already includes G: prev rows are VP_prev(eye) * prev_eye[eye] * inv(F) * M.
    std::array<Mat,2> prev_eye{};
    std::array<bool,2> prev_eye_ok{};
};
struct Slot {std::atomic<std::uint32_t> ready{};std::array<Side,2> side{};};

class Place {
public:
    static constexpr unsigned ring=4;
    inline static std::atomic<double> axes_tol{0.004},reach{0.8},identity_tol{0.0015},origin_tie{0.01};
    inline static std::atomic<float> near_w{3.f};

    // Render thread, at the eye-0 begin of the render that draws `frame`. raw[s] = the engine's F (15 floats); bound[s] = B = H_image*G
    // or null while no valid G exists (the engine's rows stay). The old publish(frame,raw,delta) form is kept for the labs and treats
    // delta*F as the bound frame.
    static void publish_bound(std::uint32_t frame,const std::array<const float*,2>& raw,const std::array<const Mat*,2>& bound,const std::array<double,2>& side_reach={0.,0.}) noexcept {
        Slot& slot=ring_[frame%ring];
        slot.ready.store(0,std::memory_order_release);
        for(unsigned s=0;s<2;++s) {
            Side& out=slot.side[s];out=Side{};
            Mat model{},inverse_model{};
            if(!raw[s]) {last_[s]={};continue;}
            model=model_of(raw[s]);
            if(!finite(model) || !inverse(model,inverse_model)) {last_[s]={};continue;}
            out.valid=true;out.model=model;out.reach=side_reach[s];
            if(!bound[s] || !finite(*bound[s])) {last_[s]={};continue;}   // no G yet: no placement, no previous chain
            out.bound=true;out.bound_frame=*bound[s];
            out.delta=mul(*bound[s],inverse_model);
            Last& l=last_[s];
            if(l.valid && l.frame==frame) {out.prev=l.prev;out.relative=l.relative;out.prev_frame=l.prev_frame;out.have_prev=l.have_prev;}
            else if(l.valid && frame-l.frame<ring) {out.prev=mul(l.placed,inverse_model);out.relative=mul(l.model,inverse_model);out.prev_frame=l.frame;out.have_prev=true;}
            else {out.prev=out.delta;out.relative=identity();}
            out.prev_eye[0]=out.prev_eye[1]=out.prev;
            out.prev_eye_ok[0]=out.prev_eye_ok[1]=out.have_prev;
            l={true,out.have_prev,frame,out.prev_frame,model,*bound[s],out.prev,out.relative};
            published_side_[s].fetch_add(1,std::memory_order_relaxed);
        }
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
    static void clear() noexcept {for(auto& s:ring_) s.ready.store(0,std::memory_order_release);last_={};latest_frame_.store(0,std::memory_order_relaxed);}
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
    static Result place(std::uint32_t frame,Get&& get_views,const float* cur,const float* prev,float* cur_out,float* prev_out,int tag_eye=-1,int pass_eye=-1) noexcept {
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
        double side_distance[2]={-1,-1};bool side_oriented=false;
        for(unsigned s=0;s<2;++s) {
            const Side& w=slot->side[s];
            if(!w.valid) continue;
            for(unsigned e=0;e<2;++e) {
                if(!views.ok[e] || (pass_eye_ok && static_cast<int>(e)!=pass_eye)) continue;
                if(!same_orientation(cur_model[e],w.model,tol)) continue;
                side_oriented=true;
                const double d=origin_distance(cur_model[e],w.model);
                if(d<=reach_of(w) && (side_distance[s]<0 || d<side_distance[s])) side_distance[s]=d;
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
        if(!(origin_distance(model,w.model)<=reach_of(w))) {result.stage=far_away;return result;}
        const Mat m=full(model);
        const Mat vp=views.vp[eye];
        const bool previous=w.have_prev && w.prev_eye_ok[eye] && views.prev_frame[candidate]==w.prev_frame && views.prev_ok[candidate][eye];
        const Mat now=mul(mul(vp,w.delta),m);
        const Mat before=previous?mul(mul(views.prev_vp[candidate][eye],w.prev_eye[eye]),m):mul(mul(vp,w.prev_eye[eye]),m);
        for(std::size_t i=0;i<16;++i) {cur_out[i]=static_cast<float>(now[i]);prev_out[i]=static_cast<float>(before[i]);}
        if(!still(*slot,frame)) {result.stage=rejected;return result;}
        result.side=candidate;result.eye=eye;
        result.stage=pass_eye_ok?placed_pass:tagged?placed_tagged:last_frame?placed_last_frame:placed_same_frame;
        return result;
    }

private:
    struct Last {bool valid{},have_prev{};std::uint32_t frame{},prev_frame{};Mat model{},placed{},prev{},relative{};};
    inline static std::array<Slot,ring> ring_{};
    inline static std::array<Last,2> last_{};
    inline static std::atomic<std::uint64_t> published_{};
    inline static std::array<std::atomic<std::uint64_t>,2> published_side_{};
    inline static std::atomic<std::uint32_t> latest_frame_{};
};
} // namespace d2vr::weapon_draw
