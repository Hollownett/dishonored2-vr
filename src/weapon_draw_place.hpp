#pragma once
// v931: a held weapon is placed at DRAW time (src/object_motion.hpp calls Place::place at each layout draw).
// The engine places a held weapon once per game tick from the hand it reads there, but the arms are drawn from each render's own
// pose sample (v861 image skin): the two disagree while walking and turning, so the weapon "lives its own life" in the hand. The
// render takes the weapon's frame before the eye-0 begin (v901 probe: a write there is never drawn), but the draw's own per-instance
// MVP (VS b2) is still ours to change (v960: a draw the cache cannot certify takes the eye it is tagged with, so no part of the weapon is drawn at the tick frame). At the eye-0 begin the mod publishes, per hand, the weapon's tick frame F, the world motion
// D = image_hand * inverse(hand_the_tick_read), and P = placed(previous render) * inverse(F). A draw whose model matrix
// M = inverse(VP) * its cb2 MVP has the weapon's orientation and lies within its reach is a part of it (parts sit at fixed
// offsets from F: v930 probe), and its rows become
//     cur'  = VP * D * M          (the weapon in the hand this render draws)
//     prev' = VP_prev * P * M     (where it was drawn last render: TAA/FSR see the hand's own motion, not the tick steps)
// VP must be the VP of the eye the draw really renders: the eye the mod tags a draw with is wrong for the weapon's draws in ~90% of
// renders (v930 probe: recovered origins off by the eye separation), so the eye is certified by the engine's own cache of the draw's
// previous MVP, which is the same instance in the other view (it renders view 0 first: view 1 carries view 0's MVP of this frame, view 0
// carries view 1's of the last render): that identity holds for exactly one eye, and a draw it cannot certify is left as the engine has it.
// Matrices are row-major, column vectors: clip = M * (x,y,z,1), the engine's cb2 and view+0x7510 storage (z row of a VP = 0,0,0,near).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
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
// A native model frame (model+0x1860: origin, three axis rows, three scales; 15 floats) as the column-vector model matrix:
// column c is axis c * scale c, the last column the origin.
inline Mat model_of(const float* raw) noexcept {
    Mat m=identity();
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+c)]=double(raw[3+c*3+r])*double(raw[12+c]);
    for(int r=0;r<3;++r) m[static_cast<std::size_t>(r*4+3)]=raw[r];
    return m;
}

struct Side {bool valid{},have_prev{};std::uint32_t prev_frame{};Mat model{},delta{},prev{},relative{};};   // F; D; P; F(previous render) * inverse(F)
struct Slot {
    std::atomic<std::uint32_t> ready{};   // frame+1 once published
    std::array<Side,2> side{};
};
// The two views' view-projections (index = the mod's eye 0/1) of the render, with inverses, and of the weapon's previous published render.
struct Views {
    bool ok[2]{},prev_ok[2]{};std::uint32_t prev_frame{};   // prev_*: the views of render `prev_frame`
    Mat vp[2]{},inv[2]{},prev_vp[2]{},prev_inv[2]{};
};
// How a draw was certified (stage counters), and what it resolved to.
enum Stage:unsigned {rejected,no_views,no_certificate,far_away,placed_same_frame,placed_last_frame,placed_tagged,stages};
struct Result {int side{-1},eye{-1};Stage stage{rejected};};

class Place {
public:
    static constexpr unsigned ring=4;
    // Orientation of the draw's model matrix against F (every rotation/scale entry), the reach around F's origin, and the identity of
    // the cached previous MVP (origin metres, other entries unitless).
    inline static std::atomic<double> axes_tol{0.004},reach{0.8},identity_tol{0.0015};
    inline static std::atomic<float> near_w{3.f};   // a weapon part's origin is at most this deep in either eye (the clip w of its MVP): the cheap rejection of the scene

    // Render thread, at the eye-0 begin of the render that draws `frame`. raw[s] = that hand's weapon tick frame (15 floats), delta[s] =
    // image_hand * inverse(tick_hand) in the world; null for a side without a weapon in an image-drawn hand.
    static void publish(std::uint32_t frame,const std::array<const float*,2>& raw,const std::array<const Mat*,2>& delta) noexcept {
        Slot& slot=ring_[frame%ring];
        slot.ready.store(0,std::memory_order_release);
        for(unsigned s=0;s<2;++s) {
            Side& out=slot.side[s];out=Side{};
            Mat model{},inverse_model{};
            if(!raw[s] || !delta[s] || !finite(*delta[s])) {last_[s]={};continue;}
            model=model_of(raw[s]);
            if(!finite(model) || !inverse(model,inverse_model)) {last_[s]={};continue;}
            out.valid=true;out.model=model;out.delta=*delta[s];
            Last& l=last_[s];
            if(l.valid && l.frame==frame) {out.prev=l.prev;out.relative=l.relative;out.prev_frame=l.prev_frame;out.have_prev=l.have_prev;}   // the same engine frame drawn again
            else if(l.valid && frame-l.frame<ring) {out.prev=mul(l.placed,inverse_model);out.relative=mul(l.model,inverse_model);out.prev_frame=l.frame;out.have_prev=true;}
            else {out.prev=*delta[s];out.relative=identity();}                      // no previous render: no object motion
            l={true,out.have_prev,frame,out.prev_frame,model,mul(*delta[s],model),out.prev,out.relative};
        }
        slot.ready.store(frame+1,std::memory_order_release);
        published_.fetch_add(1,std::memory_order_relaxed);
    }
    static void clear() noexcept {for(auto& s:ring_) s.ready.store(0,std::memory_order_release);last_={};}
    static std::uint64_t published() noexcept {return published_.load(std::memory_order_relaxed);}
    // The slot of a render, or null when none is published for it (yet, or any more).
    static const Slot* slot_of(std::uint32_t frame) noexcept {
        const Slot& s=ring_[frame%ring];
        return s.ready.load(std::memory_order_acquire)==frame+1?&s:nullptr;
    }
    static bool still(const Slot& s,std::uint32_t frame) noexcept {return s.ready.load(std::memory_order_acquire)==frame+1;}

    // Model matrix (rows 0-2) of a draw's rows under an inverse view-projection.
    using Rows=std::array<double,12>;
    static Rows model_rows(const double* inv,const float* rows) noexcept {
        Rows m{};
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {double t=0;for(int k=0;k<4;++k) t+=inv[r*4+k]*rows[k*4+c];m[static_cast<std::size_t>(r*4+c)]=t;}
        return m;
    }
    static double worst(const Rows& a,const Rows& b) noexcept {double w=0;for(std::size_t i=0;i<12;++i) {const double d=std::abs(a[i]-b[i]);if(!(d<=w)) w=d;}return w;}   // NaN stays NaN (windows.h defines max)
    static bool same_orientation(const Rows& m,const Mat& model,double tolerance) noexcept {
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) if(!(std::abs(m[static_cast<std::size_t>(r*4+c)]-model[static_cast<std::size_t>(r*4+c)])<=tolerance)) return false;
        return true;
    }
    static Rows product(const Mat& a,const Rows& m) noexcept {   // a (4x4) * [m; 0 0 0 1], rows 0-2
        Rows o{};
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {double t=a[static_cast<std::size_t>(r*4+3)]*(c==3?1.0:0.0);for(int k=0;k<3;++k) t+=a[static_cast<std::size_t>(r*4+k)]*m[static_cast<std::size_t>(k*4+c)];o[static_cast<std::size_t>(r*4+c)]=t;}
        return o;
    }
    static Mat full(const Rows& m) noexcept {Mat o=identity();for(std::size_t i=0;i<12;++i) o[i]=m[i];return o;}

    // One draw: cur/prev = its cb2 rows (current MVP, the engine's cached previous MVP). `get_views(frame,previous_frame)` returns the
    // two views' view-projections of the render and, for previous_frame != 0, of the weapon's previous published render (null: none).
    // On a result with side >= 0 the new rows are in cur_out/prev_out.
    template<class Get>
    static Result place(std::uint32_t frame,Get&& get_views,const float* cur,const float* prev,float* cur_out,float* prev_out,int tag_eye=-1) noexcept {
        Result result;
        if(!(cur[15]>0.01f && cur[15]<near_w.load(std::memory_order_relaxed))) return result;
        const Slot* slot=slot_of(frame);
        if(!slot) return result;
        const Views* views_ptr=get_views(frame,slot->side[0].valid?slot->side[0].prev_frame:slot->side[1].prev_frame);
        if(!views_ptr) {result.stage=no_views;return result;}
        const Views& views=*views_ptr;
        const double tol=axes_tol.load(std::memory_order_relaxed);
        // 1. orientation: the same in both eyes unless they are canted, so test the model matrix under each eye's inverse
        Rows cur_model[2]{};int candidate=-1;bool oriented[2]{};
        for(unsigned e=0;e<2;++e) if(views.ok[e]) {
            cur_model[e]=model_rows(views.inv[e].data(),cur);
            for(unsigned s=0;s<2;++s) if(slot->side[s].valid && same_orientation(cur_model[e],slot->side[s].model,tol)) {oriented[e]=true;candidate=static_cast<int>(s);}
        }
        if(candidate<0) return result;
        result.stage=no_views;
        if(!views.ok[0] || !views.ok[1]) return result;
        const Side& w=slot->side[static_cast<unsigned>(candidate)];
        // 2. which eye rendered it: the cached previous MVP is the same instance's MVP in another view
        const Rows prev_in_view[2]={model_rows(views.inv[0].data(),prev),model_rows(views.inv[1].data(),prev)};
        const double id=identity_tol.load(std::memory_order_relaxed);
        int eye=-1;bool ambiguous=false,last_frame=false;
        const auto certify=[&](int e,bool previous_render) {
            if(eye>=0 && eye!=e) ambiguous=true;
            eye=e;last_frame=previous_render;
        };
        // this render's other view: view 1 carries view 0's MVP (view 0 is rendered first), or the reverse if view 1 is
        if(oriented[1] && worst(cur_model[1],prev_in_view[0])<=id) certify(1,false);
        if(oriented[0] && worst(cur_model[0],prev_in_view[1])<=id) certify(0,false);
        // the previous render: the cache holds the other view's (view 1 for view 0) or this view's own MVP of it, moved by the weapon's own motion
        const bool previous=w.have_prev && views.prev_frame==w.prev_frame;
        if(previous && eye<0) {
            for(unsigned src=0;src<2;++src) if(views.prev_ok[src]) {
                const Rows before=model_rows(views.prev_inv[src].data(),prev);
                for(unsigned e=0;e<2;++e) if(oriented[e] && worst(before,product(w.relative,cur_model[e]))<=id) certify(static_cast<int>(e),true);
            }
        }
        bool tagged=false;
        if(eye<0 || ambiguous) {   // v960 (user, headset v951: the weapon flickered; 29% of the headset's weapon draws, 53% of view 0's, had no certificate and were drawn at the tick frame between placed ones):
            // the eye the draw is tagged with (v950 b1 key: wrong for 0.3% of the certified draws) when the draw has the weapon's orientation under that eye
            if(tag_eye>=0 && tag_eye<2 && oriented[tag_eye]) {eye=tag_eye;last_frame=false;tagged=true;}
            else {result.stage=no_certificate;return result;}
        }
        const Rows& model=cur_model[eye];
        if(!(std::hypot(model[3]-w.model[3],std::hypot(model[7]-w.model[7],model[11]-w.model[11]))<=reach.load(std::memory_order_relaxed))) {result.stage=far_away;return result;}
        // 3. the rows: this render's view-projection of the weapon moved to the drawn hand, the previous one from where it was drawn last
        const Mat m=full(model);
        // v961 (v960 walk sim: the tagged draws kept 45% of the engine's screen motion): without the previous render's view of this eye the weapon's previous rows are its rows now
        // (no motion), not the hand's world displacement under the current view-projection, which reads a walking player's speed as the weapon's own motion
        // v963 (user, headset v961: "the weapon goes black for a frame with TAA/FSR/DLSS"): zero motion on a tagged draw leaves TAA/DLSS sampling the
        // background at the weapon's new pixel (a dark or missing history reads as a black weapon edge). The hand-relative motion under the current view
        // (vp * previous-placed * inverse(current tick frame) * model) is not the player's walking speed: it is the weapon's own motion in the hand, and
        // it is zero only when the hand really did not move relative to the camera.
        const Mat vp=views.vp[eye];
        const Mat now=mul(mul(vp,w.delta),m),before=previous && views.prev_ok[eye]?mul(mul(views.prev_vp[eye],w.prev),m):mul(mul(vp,w.prev),m);
        for(std::size_t i=0;i<16;++i) {cur_out[i]=static_cast<float>(now[i]);prev_out[i]=static_cast<float>(before[i]);}
        if(!still(*slot,frame)) {result.stage=rejected;return result;}                 // a publisher that reused the slot meanwhile: the engine's rows stay
        result.side=candidate;result.eye=eye;result.stage=tagged?placed_tagged:last_frame?placed_last_frame:placed_same_frame;
        return result;
    }

private:
    struct Last {bool valid{},have_prev{};std::uint32_t frame{},prev_frame{};Mat model{},placed{},prev{},relative{};};
    inline static std::array<Slot,ring> ring_{};
    inline static std::array<Last,2> last_{};
    inline static std::atomic<std::uint64_t> published_{};
};
} // namespace d2vr::weapon_draw
