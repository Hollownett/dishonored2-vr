#pragma once
// v792 (headset v790/v791, user: "the right eye doubles while the camera moves, never when I stand still; the frame lags, catches up,
// lags again"): in performance mode the two projection views come from different frames. A runtime that reprojects both eyes from one
// pose (SteamVR / Steam Link here: the right eye alternated by plus and minus one frame of head motion, the left eye was right) misplaces
// the eye that was not rendered now. So the kept image of that eye is warped (rotation, each view's own FOV) to the view the eye has at
// the fresh eye's Setup, and submitted with that view: both views then carry one head pose, whatever the runtime does with them.
// Translation (about a millimetre of head motion a frame) and world motion are not warped.
// v796 (headset v794 at ~22 ms a frame: "the right eye still lags and doubles when I walk, looking around is fine"): walking moves the
// game camera centimetres a frame, and near surfaces then shift by degrees. With the kept frame's scene depth (the FSR/DLSS per-eye
// copy) and both game cameras, world pixels are warped with parallax (walking, smooth turns); hands and body (attached to the player,
// in the engine's own near depth range, device depth above 0.02) keep the rotation warp, since they moved with the camera.
// v798: near content (hands, held things, a carried body, NPCs) comes from the fresh eye's image of this moment (StaleLayers).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <memory>
#include <vector>
#include "context_state.hpp"
#include "tracked_pose.hpp"

namespace d2vr {
struct WarpFov {double left{},right{},up{},down{};};  // OpenXR angles in radians: left < 0 < right, down < 0 < up
using WarpMatrix=std::array<double,9>;                // row-major 3x3

inline bool warp_fov_valid(const WarpFov& f) {
    constexpr double limit=1.553; // 89 degrees: tan stays finite
    return std::isfinite(f.left) && std::isfinite(f.right) && std::isfinite(f.up) && std::isfinite(f.down) &&
        f.left>-limit && f.right<limit && f.down>-limit && f.up<limit && f.right>f.left && f.up>f.down;
}
// A direction in a camera (OpenXR axes, -Z ahead) -> (u w, v w, w): w is the distance ahead.
inline WarpMatrix warp_image_matrix(const WarpFov& f) {
    const double l=std::tan(f.left),r=std::tan(f.right),u=std::tan(f.up),d=std::tan(f.down);
    return {1/(r-l),0,l/(r-l), 0,1/(d-u),u/(d-u), 0,0,-1};
}
// For a relative rotation m (columns: the `to` camera's axes in the `from` camera), the homography taking a pixel of the `to` view
// (u,v in 0..1, v down) to the pixel of the `from` view that shows the same direction, as homogeneous (x,y,w): u=x/w, v=y/w, and
// w <= 0 means the direction points away from the `from` camera.
inline std::optional<WarpMatrix> warp_homography(const WarpMatrix& m,const WarpFov& from_fov,const WarpFov& to_fov) {
    if(!warp_fov_valid(from_fov) || !warp_fov_valid(to_fov)) return std::nullopt;
    const double tl=std::tan(to_fov.left),tr=std::tan(to_fov.right),tu=std::tan(to_fov.up),td=std::tan(to_fov.down);
    const WarpMatrix a{tr-tl,0,tl, 0,td-tu,tu, 0,0,-1};                               // (u,v,1) -> direction in the `to` camera (-Z ahead)
    const auto mul=[](const WarpMatrix& p,const WarpMatrix& q) {
        WarpMatrix r{};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) for(int k=0;k<3;++k) r[i*3+j]+=p[i*3+k]*q[k*3+j];
        return r;
    };
    const auto h=mul(warp_image_matrix(from_fov),mul(m,a));
    for(double x:h) if(!std::isfinite(x)) return std::nullopt;
    return h;
}
// Rotation only, from the two views' tracking-space orientations.
inline std::optional<WarpMatrix> stale_eye_homography(Quaternion from,const WarpFov& from_fov,Quaternion to,const WarpFov& to_fov) {
    if(!warp_fov_valid(from_fov) || !warp_fov_valid(to_fov)) return std::nullopt;
    try {from=normalized(from);to=normalized(to);} catch(const std::invalid_argument&) {return std::nullopt;}
    const Quaternion relative=multiplied({-from.x,-from.y,-from.z,from.w},to); // the `to` camera's axes in the `from` camera
    const Vec3 cx=rotated(relative,{1,0,0}),cy=rotated(relative,{0,1,0}),cz=rotated(relative,{0,0,1});
    return warp_homography({cx.x,cy.x,cz.x, cx.y,cy.y,cz.y, cx.z,cy.z,cz.z},from_fov,to_fov);
}
inline std::optional<std::array<double,2>> warp_point(const WarpMatrix& h,double u,double v) {
    const double x=h[0]*u+h[1]*v+h[2],y=h[3]*u+h[4]*v+h[5],w=h[6]*u+h[7]*v+h[8];
    if(!(w>1e-9) || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return std::array<double,2>{x/w,y/w};
}

// v796: a game camera as the probe wrote it into the engine view (world metres; rows forward, left, up) and the depth terms of the
// engine projection it rendered with (reverse Z: distance ahead = m11/(depth+m10)). v802: and the hands drawn that frame (hand_bits:
// which ones), the same world space.
// v811: and each drawn palm's centre (world; zero: not recorded, the grip stands in).
struct WarpCamera {WorldTrackedPose pose{};double m10{},m11{};std::array<WorldTrackedPose,2> hands{};unsigned hand_bits{};std::array<Vec3,2> palms{};};
// v802: did the drawn hands hold still (in the world) from the kept frame's Setup to the target's? Then what is within reach (the hands,
// what they hold, the body) may show from the kept image. A hand drawn in only one of the frames counts as moved.
// v803: how far the drawn hands moved (metres) and turned (degrees) at most between the two Setups; infinite when a hand is drawn in
// only one of them.
inline std::array<double,2> stale_hands_motion(const WarpCamera& kept,const WarpCamera& target) {
    if(kept.hand_bits!=target.hand_bits) return {INFINITY,INFINITY};
    std::array<double,2> most{0,0};
    for(unsigned side=0;side<2;++side) if(kept.hand_bits>>side&1u) {
        const auto& a=kept.hands[side];const auto& b=target.hands[side];
        const double turn=std::acos(std::clamp(std::min(dot(a.basis[0],b.basis[0]),dot(a.basis[2],b.basis[2])),-1.,1.))*180/3.14159265358979;
        const double moved=length(a.position-b.position);
        most={std::isfinite(moved)?std::max(most[0],moved):INFINITY,std::isfinite(turn)?std::max(most[1],turn):INFINITY};
    }
    return most;
}
inline bool stale_hands_still(const WarpCamera& kept,const WarpCamera& target,double tolerance=.004) {
    const auto m=stale_hands_motion(kept,target);
    return m[0]<tolerance && m[1]<2; // 2 degrees
}
// The world part of the warp between the kept camera and the target camera. world: the rotation homography (target pixel -> kept
// pixel, anchor turns included); shift: the target camera's centre in kept image coordinates (u w, v w, w; metres). A target pixel
// whose surface lies D metres ahead of the kept camera shows kept pixel ((k*S+shift)/D).xy, S=world*(u,v,1), k=(D-shift.w)/S.w.
// back, back_shift: the same from the target side (kept pixel -> target pixel), for the reticle.
struct StaleParallax {WarpMatrix world{},back{};std::array<double,3> shift{},back_shift{};double m10{},m11{};};
// Nothing for an invalid camera or FOV, or a step of a metre or more (a blink, a cut: no parallax is right then).
inline std::optional<StaleParallax> stale_eye_parallax(const WarpCamera& from,const WarpFov& from_fov,const WarpCamera& to,const WarpFov& to_fov) {
    const auto axes=[](const WorldTrackedPose& p) -> std::optional<std::array<Vec3,3>> { // OpenXR camera axes: right, up, back
        const std::array<Vec3,3> a{p.basis[1]*-1.,p.basis[2],p.basis[0]*-1.};
        if(!finite(p.position)) return std::nullopt;
        for(int i=0;i<3;++i) for(int j=i;j<3;++j) if(!finite(a[i]) || !finite(a[j]) || std::abs(dot(a[i],a[j])-(i==j?1.:0.))>.02) return std::nullopt;
        return a;
    };
    const auto f=axes(from.pose),t=axes(to.pose);
    const Vec3 step=to.pose.position-from.pose.position;
    if(!f || !t || !(length(step)<1.) || !std::isfinite(from.m10) || !(from.m10>=0) || !std::isfinite(from.m11) || !(from.m11>0)) return std::nullopt;
    WarpMatrix m{},mt{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {m[i*3+j]=dot((*f)[i],(*t)[j]);mt[i*3+j]=dot((*t)[i],(*f)[j]);}
    const auto world=warp_homography(m,from_fov,to_fov),back=warp_homography(mt,to_fov,from_fov);
    if(!world || !back) return std::nullopt;
    const auto image=[](const WarpMatrix& b,const Vec3& v) {return std::array<double,3>{b[0]*v.x+b[2]*v.z,b[4]*v.y+b[5]*v.z,-v.z};};
    const Vec3 in_from{dot(step,(*f)[0]),dot(step,(*f)[1]),dot(step,(*f)[2])},in_to{-dot(step,(*t)[0]),-dot(step,(*t)[1]),-dot(step,(*t)[2])};
    return StaleParallax{*world,*back,image(warp_image_matrix(from_fov),in_from),image(warp_image_matrix(to_fov),in_to),from.m10,from.m11};
}
// The kept pixel that target pixel (u,v) shows when its surface lies `distance` metres ahead of the kept camera (the pass solves for
// that distance per pixel from the kept depth). Nothing behind either camera.
inline std::optional<std::array<double,2>> parallax_source(const StaleParallax& p,double u,double v,double distance) {
    const auto& h=p.world;
    const double x=h[0]*u+h[1]*v+h[2],y=h[3]*u+h[4]*v+h[5],w=h[6]*u+h[7]*v+h[8];
    if(!(w>1e-9) || !(distance>0)) return std::nullopt;
    const double k=(distance-p.shift[2])/w;
    if(!(k>0)) return std::nullopt;
    return std::array<double,2>{(k*x+p.shift[0])/distance,(k*y+p.shift[1])/distance};
}
// The target pixel of kept pixel (u,v) whose surface lies `distance` metres ahead of the kept camera.
inline std::optional<std::array<double,2>> parallax_target(const StaleParallax& p,double u,double v,double distance) {
    const auto& h=p.back;
    const double x=distance*(h[0]*u+h[1]*v+h[2])+p.back_shift[0],y=distance*(h[3]*u+h[4]*v+h[5])+p.back_shift[1],
        w=distance*(h[6]*u+h[7]*v+h[8])+p.back_shift[2];
    if(!(w>1e-9) || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return std::array<double,2>{x/w,y/w};
}
// The kept frame's scene depth: R32 device depth, its render rectangle (width x height) at the top left of the texture.
struct StaleDepth {ID3D11Texture2D* texture{};UINT width{},height{};};

// v798 (headset v797: "the hands, and everything in them, still double, and the pixels around them look wrong"). Its band bursts: in a
// turn the warped right eye moved the wall by the turn (29-45 px a Present) but a carried guard's body by 4-10 px, against 45+ px
// between the fresh images, as the player's own. What moves near the eye (a hand, its weapon, a towed body, an NPC) cannot be told
// from the kept image alone, and v797's hand capsules missed it. All of it is in the fresh eye's image of this same moment, so near
// content (nearer than near_range) is that image moved to this eye's view by the fresh depth; the rest is the kept image (v796 scene
// parallax). Where the fresh eye cannot see what this eye does (beside a near object) the kept image fills. v801: what the kept image
// held near shows only where the fresh eye cannot see its spot now; elsewhere the fresh eye's view of that spot (see WarpPS). v802: only
// while the hands move (near_from_kept false), and only within reach; otherwise the kept image throughout.
// v810 (headset v809: "the hands jerk in jumps; hands and body jerk in turns, with afterimages; near the edges the hands and what they
// hold stretch. Build the whole model instead of patching places"): what the kept image shows moved one of a few known ways since its
// frame: the world held still (world); the body, the arms and hands the head carried along (walking, jumping, falling, snap turning: they
// kept their place in the view: carried); each drawn hand by its own tracked motion, with what it holds (hand). Each is a layer from the
// kept image to the target view: the kept camera moved by that motion. The pass takes per pixel the nearest one the fresh eye confirms.
struct StaleLayers {
    StaleParallax world{};              // kept image -> target view (the scene)
    std::optional<StaleParallax> fresh; // fresh eye's image -> target view (this moment; the other eye's camera)
    WarpFov kept_fov{};
    double near_range{2};               // metres from the target view: nearer content comes from the fresh eye
    bool near_from_kept{};              // v802: the hands held still (stale_hands_still); v810: diagnostic only
    std::array<double,2> hands_motion{};// v803 diagnostic: stale_hands_motion (metres, degrees)
    std::optional<StaleParallax> carried;           // v810: what kept its place in the view
    std::array<std::optional<StaleParallax>,2> hand;// v810: what moved with each drawn hand
    std::array<double,3> start{0.5,0.5,0.5};        // v810: where each motion layer's search starts (metres from the target view:
                                                    // carried at arm's length, each hand at its own distance; from infinity it settles far)
    std::array<std::array<double,4>,2> palms{};     // v811: each drawn palm in the kept camera (right, up, forward metres), w the reach it
                                                    // owns (0: no hand drawn in the kept frame)
    std::uint64_t frame{};unsigned eye{};           // v811: the Present and eye rebuilt (the hand-fit trace)
    // v1150 movers (design 2026-11-11 section 3): the share of the fresh eye's motion span since the kept tick, (tN-tN-1)/(tN-tN-2), and
    // of the kept eye's span to carry its movers forward, (tN-tN-1)/(tN-1-tN-3); 0.5 without frame times. motion_rules false: the motion
    // textures are ignored (knob no-stale-eye-motion). kept_axes: world -> kept camera (right, up, forward), rows (stale_eye_layers).
    double fresh_part{.5},kept_part{.5};
    // v1167: the eye images' non-scene band at their right and bottom edges, fractions of the image (measured: up to 46 px of 2496 x 2688;
    // the caller may pass the exact one)
    // v1191: none by default: replays of v1170-hold and v1165-door (bands of 8-46 px) score better in the edge band with no band (hold
    // 6.6-15 vs 8-19.7, door 7-17 vs 20-31 levels) and the v1183+ scenes have none (no edge rows the eyes share); the caller may pass one
    std::array<double,2> border{0,0};
    // v1167: rig pixels by their own motion where both renders' rig motion is valid (rig_motion: kept, fresh); rig_motion_rules false
    // (knob no-stale-eye-rig-motion) keeps the hand and carried layers. Not used by the pass yet.
    bool rig_motion_rules{true};
    std::array<bool,2> rig_motion{};
    // v1171: a camera cut since the kept frame (stale_eye_cut): the whole frame is rebuilt from the fresh eye alone
    bool cut{};
    // v1173 (headset v1172: "a halo around the hands while they move"): with the rig masks, a moving hand (hand_moving, from its recorded
    // poses) is taken from the fresh eye, gated by the masks, not moved by its recorded grip (which over-moves the drawn hand); rig_from_fresh
    // false keeps the hand layers
    bool rig_from_fresh{true};
    std::array<bool,2> hand_moving{};
    // v1175 (headset v1174: "artifacts on objects close to the player" while hands and camera move): the camera moved since the kept tick
    // (camera_still_m, camera_still_deg); with a hand moving too, near scenery comes from the fresh eye as a whole (with the masks)
    bool camera_moving{};
    bool motion_rules{true};
    WarpMatrix kept_axes{};
    // v1177 AFW v2 (src/afw2.hpp): the forward splat of this eye's own kept frame instead of the gather below (off: today's warp). The
    // target's and the fresh eye's FOV (stale_eye_layers; invalid: the kept one stands in)
    bool afw2{};
    WarpFov target_fov{},fresh_fov{};
    // v1190 (engine side, rig_target.hpp): the first-person rig and held items drawn depth-only into THIS eye's target camera at the fresh
    // eye's render (exact silhouette and depth for this tick). apply()'s rig_target: R32G32_FLOAT at the depth's size (x reverse-Z depth in the
    // engine's convention, y class 255 rig / 253 held, 0 0 none); bound at t15 of the AFW v2 pass. True only when every rig/held draw of
    // that render was retargeted.
    bool rig_target{};
};
inline constexpr double near_range_default=2.;
// v811: a drawn hand owns the kept pixels within this distance of its palm: the hand, its wrist, what it holds (a glass, the Heart, a pistol).
inline constexpr double hand_own_radius=.22;
// v1171 (v1170 sim: a teleport; v1165 hold 2): the kept and target cameras show different places: the kept image is no source at all.
// A step over cut_step (over 11 m/s even at 44 ms a frame: no head, sprint or jump), or over cut_turn_step together with a view turn over
// cut_turn_deg (a head does not jump 10 cm while swinging its view 20 degrees in one tick; the two dumps: 0.3 m and 43 degrees). A snap turn
// (yaw only, the eyes move 2-3 cm) and a sprint (0.3 m, little turn) are no cut: their rules stand.
inline constexpr double cut_step=.5,cut_turn_step=.1,cut_turn_deg=20;
// v1175: the camera holds still between the kept and the target tick within these (a head at rest jitters below a millimetre)
inline constexpr double camera_still_m=.003,camera_still_deg=.25;
// v1150: a pixel whose object motion (motion texture w, render pixels over the eye's two ticks) reaches this is a mover.
// v1157: 3 px. The engine's entity vectors can carry another frame's jitter step than the static world's (measured 2026-11-11: still
// props at 0.4-1.3 px). A step's components are within +-1 px, so mixing two steps stays under 2.83 px: no jitter error can make a
// still prop a mover. A real mover under 3 px in two ticks lags at most 1.5 px. Lower it once the entity step is exact (design doc).
inline constexpr double mover_px=3.;
// v1150: an eye's motion texture (RGBA16F; xyz world displacement in metres of the surface since that eye's previous render, w object
// motion in render pixels, 0 static or unknown), its render rectangle (width x height) at the top left like StaleDepth.
struct StaleMotion {ID3D11Texture2D* texture{};UINT width{},height{};};
// v1159: an eye's first-person rig mask (R8: nonzero = the player's arms, body, held weapon or prop), its render rectangle at the top left.
struct StaleRig {ID3D11Texture2D* texture{};UINT width{},height{};};
// v1159: with the masks, how far from a drawn palm its motion moves rig pixels (the forearm, a sword); beyond, the head's (the body).
inline constexpr double hand_rig_radius=.45;
// v810: camera c as it would have to move to keep showing what moved rigidly from pose `from` to pose `to` (a drawn hand) where its
// image has it: the same rigid motion applied to the camera.
inline WarpCamera moved_with(const WarpCamera& c,const WorldTrackedPose& from,const WorldTrackedPose& to) {
    const auto turn=[&](const Vec3& v) {Vec3 out{};for(int i=0;i<3;++i) out=out+to.basis[i]*dot(from.basis[i],v);return out;};
    WarpCamera m=c;
    m.pose.position=turn(c.pose.position-from.position)+to.position;
    for(int i=0;i<3;++i) m.pose.basis[i]=turn(c.pose.basis[i]);
    return m;
}
// The scene layer and, given the fresh eye's camera (same Setup as the target), its layer; nothing when the scene cannot be solved
// (the pass warps by rotation only then). near_range 0: no fresh layer (v796). v810: and the carried and hand layers.
inline std::optional<StaleLayers> stale_eye_layers(const WarpCamera& kept,const WarpFov& kept_fov,const WarpCamera& target,const WarpFov& target_fov,
                                                   const WarpCamera* fresh=nullptr,const WarpFov& fresh_fov={},double near_range=near_range_default) {
    // v1171: a cut (see cut_step) takes the frame from the fresh eye alone; the world layer is then the target's own (unused)
    const double step=length(target.pose.position-kept.pose.position);
    const double turn=std::acos(std::clamp(dot(kept.pose.basis[0],target.pose.basis[0]),-1.,1.))*180/3.14159265358979;
    const bool cut=std::isfinite(step) && (step>cut_step || (step>cut_turn_step && turn>cut_turn_deg)) && fresh && near_range>0;
    WarpCamera own=target;own.m10=kept.m10;own.m11=kept.m11;
    const auto world=cut?stale_eye_parallax(own,kept_fov,target,target_fov):stale_eye_parallax(kept,kept_fov,target,target_fov);
    if(!world) return std::nullopt;
    StaleLayers layers{*world,std::nullopt,kept_fov,near_range};
    layers.target_fov=target_fov;layers.fresh_fov=fresh_fov;
    layers.cut=cut;
    layers.camera_moving=!(step<=camera_still_m && turn<=camera_still_deg);
    for(unsigned side=0;side<2;++side) { // v1173: each drawn hand moved (stale_hands_still per hand; drawn in only one frame counts as moved)
        WarpCamera a=kept,b=target;a.hand_bits=kept.hand_bits&(1u<<side);b.hand_bits=target.hand_bits&(1u<<side);
        layers.hand_moving[side]=(a.hand_bits|b.hand_bits)!=0 && !stale_hands_still(a,b);
    }
    if(fresh && near_range>0) layers.fresh=stale_eye_parallax(*fresh,fresh_fov,target,target_fov);
    WarpCamera with_head=target;with_head.m10=kept.m10;with_head.m11=kept.m11; // the kept depth's projection, the target's place
    layers.carried=stale_eye_parallax(with_head,kept_fov,target,target_fov);
    for(unsigned side=0;side<2;++side) if((kept.hand_bits&target.hand_bits)>>side&1u) {
        layers.hand[side]=stale_eye_parallax(moved_with(kept,kept.hands[side],target.hands[side]),kept_fov,target,target_fov);
        const double ahead=dot(target.hands[side].position-target.pose.position,target.pose.basis[0]);
        layers.start[1+side]=std::clamp(ahead,0.1,1.5);
    }
    // v811: what each hand drawn in the kept frame owns there (a hand drawn only then owns it too: its old place is uncovered, not kept)
    for(unsigned side=0;side<2;++side) if(kept.hand_bits>>side&1u) {
        const Vec3 d=(length(kept.palms[side])>0?kept.palms[side]:kept.hands[side].position)-kept.pose.position;
        if(finite(d)) layers.palms[side]={-dot(d,kept.pose.basis[1]),dot(d,kept.pose.basis[2]),dot(d,kept.pose.basis[0]),hand_own_radius};
    }
    const auto& b=kept.pose.basis;
    layers.kept_axes={-b[1].x,-b[1].y,-b[1].z, b[2].x,b[2].y,b[2].z, b[0].x,b[0].y,b[0].z};
    return layers;
}

} // namespace d2vr
#include "afw2.hpp"
namespace d2vr {
// One full-screen pass: every pixel of the target view samples the kept image through the homography, Catmull-Rom through nine
// bilinear taps (a single bilinear tap would soften every warped frame). Read through an _SRGB view and written through an _SRGB target,
// so the filter works in linear light, like EyeSharpen. One instance serves both eyes; the caller copies the output out at once.
class StaleEyeWarp {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    ContextState state_;
    Ptr<ID3D11VertexShader> vertex_;
    Ptr<ID3D11PixelShader> pixel_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11Buffer> parameters_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11Texture2D> output_;
    std::array<Ptr<ID3D11Texture2D>,2> copies_{};          // read copies of an image without a read view: 0 kept, 1 fresh
    std::array<Ptr<ID3D11ShaderResourceView>,2> copy_views_{};
    Ptr<ID3D11RenderTargetView> output_rtv_;
    UINT width_{},height_{};
    // v811: the hand fit (FitSample, FitCoarse, FitFine, FitPick), its buffers, and a ring of read-backs a few Presents late for the trace
    static constexpr UINT fit_grid=48;
    std::array<Ptr<ID3D11ComputeShader>,4> fit_{};
    Ptr<ID3D11Buffer> fit_samples_,fit_counts_,fit_result_;
    Ptr<ID3D11UnorderedAccessView> fit_samples_uav_,fit_counts_uav_,fit_result_uav_;
    Ptr<ID3D11ShaderResourceView> fit_result_srv_;
    std::array<Ptr<ID3D11Buffer>,3> fit_staging_{};std::array<std::pair<std::uint64_t,unsigned>,3> fit_tag_{};std::array<bool,3> fit_waiting_{};
    unsigned fit_slot_{};
    Ptr<ID3D11Buffer> structured(UINT count,UINT stride,UINT bindings,Ptr<ID3D11UnorderedAccessView>& uav) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=count*stride;d.BindFlags=bindings;d.Usage=D3D11_USAGE_DEFAULT;
        d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=stride;
        Ptr<ID3D11Buffer> b;checked(device_->CreateBuffer(&d,nullptr,&b),"hand fit buffer");
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_UNKNOWN;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=count;
        checked(device_->CreateUnorderedAccessView(b.Get(),&u,&uav),"hand fit write view");
        return b;
    }
    // Takes the read-backs the GPU has finished (never waits).
    void collect_fits() {
        for(unsigned n=0;n<fit_staging_.size();++n) if(fit_waiting_[n]) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if(context_->Map(fit_staging_[n].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m)!=S_OK) continue;
            HandFit r{fit_tag_[n].first,fit_tag_[n].second,{}};
            const auto* v=static_cast<const float*>(m.pData);
            for(unsigned side=0;side<2;++side) r.hands[side]={v[side*4]*width_,v[side*4+1]*height_,v[side*4+2],v[side*4+3]};
            context_->Unmap(fit_staging_[n].Get(),0);fit_waiting_[n]=false;
            if(r.hands[0][0]!=0 || r.hands[0][1]!=0 || r.hands[1][0]!=0 || r.hands[1][1]!=0) ++fit_corrections;
            if(hand_fits.size()<64) hand_fits.push_back(r);
        }
    }
    static void checked(HRESULT result,const char* label) {
        if(FAILED(result)) throw std::runtime_error(std::string("Stale eye warp: ")+label+" ("+std::to_string(result)+")");
    }
    static Ptr<ID3DBlob> compile(const std::string& source,const char* entry,const char* profile) {
        Ptr<ID3DBlob> binary,errors;
        const auto result=D3DCompile(source.data(),source.size(),"D2VR stale eye warp",nullptr,nullptr,entry,profile,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(std::string("Stale eye warp shader ")+entry+": "+
            (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
        return binary;
    }
    Ptr<ID3D11Texture2D> texture(UINT width,UINT height,UINT bindings) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.BindFlags=bindings;desc.Usage=D3D11_USAGE_DEFAULT;
        Ptr<ID3D11Texture2D> result;checked(device_->CreateTexture2D(&desc,nullptr,&result),"texture allocation");return result;
    }
    Ptr<ID3D11ShaderResourceView> try_srv(ID3D11Texture2D* texture) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels=1;Ptr<ID3D11ShaderResourceView> result;device_->CreateShaderResourceView(texture,&desc,&result);return result;
    }
    void resize(UINT width,UINT height) {
        if(width==width_ && height==height_) return;
        if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Stale eye warp size exceeds D3D11 texture limits");
        auto output=texture(width,height,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        D3D11_RENDER_TARGET_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        Ptr<ID3D11RenderTargetView> write;checked(device_->CreateRenderTargetView(output.Get(),&desc,&write),"texture write view");
        output_=output;output_rtv_=write;width_=width;height_=height;
    }
    // v796: read views of the two kept depths (one per eye, so alternation does not rebuild them); nothing for a texture that is not R32.
    struct DepthView {Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> view;};
    std::array<DepthView,2> depth_views_{},motion_views_{},rig_views_{};unsigned depth_next_{},motion_next_{},rig_next_{};
    // v1150: and of the two motion textures (RGBA16F)
    ID3D11ShaderResourceView* read_view(ID3D11Texture2D* texture,UINT width,UINT height,DXGI_FORMAT typed,DXGI_FORMAT typeless,
                                        std::array<DepthView,2>& views,unsigned& next) {
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(!(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE) || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || width>desc.Width || height>desc.Height ||
            (desc.Format!=typed && desc.Format!=typeless)) return nullptr;
        for(auto& d:views) if(d.texture.Get()==texture) return d.view.Get();
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=typed;v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
        Ptr<ID3D11ShaderResourceView> view;if(FAILED(device_->CreateShaderResourceView(texture,&v,&view))) return nullptr;
        auto& slot=views[next++&1u];slot.texture=texture;slot.view=view;return view.Get();
    }
    ID3D11ShaderResourceView* depth_view(const StaleDepth& depth) {
        return read_view(depth.texture,depth.width,depth.height,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32_TYPELESS,depth_views_,depth_next_);
    }
    ID3D11ShaderResourceView* rig_view(const StaleRig& rig) {
        if(!rig.texture || !rig.width || !rig.height) return nullptr;
        return read_view(rig.texture,rig.width,rig.height,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8_TYPELESS,rig_views_,rig_next_);
    }
    ID3D11ShaderResourceView* motion_view(const StaleMotion& motion) {
        if(!motion.texture || !motion.width || !motion.height) return nullptr;
        return read_view(motion.texture,motion.width,motion.height,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS,motion_views_,motion_next_);
    }
    // An _SRGB read view of an eye image, or of a copy of it in `slot` when it has none (CopyResource still takes it).
    ID3D11ShaderResourceView* image_view(ID3D11Texture2D* image,std::size_t slot,Ptr<ID3D11ShaderResourceView>& own) {
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
        if(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE && desc.MipLevels==1) {own=try_srv(image);if(own) return own.Get();}
        auto& copy=copies_[slot];
        D3D11_TEXTURE2D_DESC have{};if(copy) copy->GetDesc(&have);
        if(!copy || have.Width!=desc.Width || have.Height!=desc.Height) {
            auto fresh_copy=texture(desc.Width,desc.Height,D3D11_BIND_SHADER_RESOURCE);
            auto view=try_srv(fresh_copy.Get());if(!view) throw std::runtime_error("Stale eye warp: copy read view");
            copy=fresh_copy;copy_views_[slot]=view;
        }
        context_->CopySubresourceRegion(copy.Get(),0,0,0,0,image,0,nullptr);
        return copy_views_[slot].Get();
    }
public:
    std::uint64_t parallax_passes{},depth_refusals{}; // v796: passes that used the kept depth; kept depths that had no read view
    std::uint64_t fresh_passes{},fresh_refusals{};    // v798: passes with the fresh eye's layer; fresh inputs refused (format, size)
    std::uint64_t motion_passes{},motion_refusals{};  // v1150: passes with the mover rules; motion textures refused (format, size)
    std::uint64_t rig_passes{},rig_refusals{};        // v1159: passes with the rig masks; masks refused (format, size)
    std::uint64_t cut_passes{};                       // v1171: frames rebuilt from the fresh eye alone (a camera cut)
    std::uint64_t rig_target_passes{};                // v1190: AFW v2 passes given the rig target (t15)
    std::unique_ptr<Afw2Pass> afw2;                   // v1177: the forward-splat rebuild (StaleLayers::afw2), made on first use
    // v811: hand fits run, and read back with a correction; each read-back: the Present and eye, per hand the correction (target pixels
    // x, y; 0 0 none), the fraction of its kept pixels the fresh eye sees there and with no correction. The caller takes hand_fits.
    struct HandFit {std::uint64_t frame{};unsigned eye{};std::array<std::array<float,4>,2> hands{};};
    std::uint64_t fit_passes{},fit_corrections{};
    std::vector<HandFit> hand_fits;
    void poll_hand_fits() {ContextState::Scope scope(state_);collect_fits();}
    explicit StaleEyeWarp(ID3D11Device* device):device_(device),state_(device) {
        device_->GetImmediateContext(&context_);
        const std::string source=R"HLSL(
Texture2D sourceTex : register(t0);
Texture2D<float> depthTex : register(t1);
Texture2D freshTex : register(t2);
Texture2D<float> freshDepthTex : register(t3);
SamplerState sourceSampler : register(s0);
// row*: target pixel -> kept pixel without depth (the tracking-space rotation). scene / fresh: the rotation homography (target pixel ->
// kept / fresh pixel) and the target camera's centre in that image (w=1: on). depthMap: kept m10, m11, the near range (m), depth on;
// freshMap: the fresh camera's m10, m11, -, the hands held still (v802); depthSize: the kept depth rectangle (xy) and the fresh one (zw), texels.
// v810: carried, handL, handR: the motion layers (kept pixel of what kept its place in the view, of what moved with each drawn hand).
// v811: keptFov: the kept image's ray tangents (left, right, up, down); palmL, palmR: each drawn palm in the kept camera (right, up, forward
// metres) and the reach it owns (w; 0: none); backL, backR: each hand layer from the kept side (kept pixel and distance -> target pixel and
// depth; [3] its shift); fitGrid: the hand fit's sample grid (x, y) and coarse step (z, target pixels).
cbuffer Parameters : register(b0) {
    float4 row0; float4 row1; float4 row2; float4 size;
    float4 scene[4]; float4 fresh[4]; float4 depthMap; float4 freshMap; float4 depthSize;
    float4 carried[4]; float4 handL[4]; float4 handR[4];
    float4 keptFov; float4 palmL; float4 palmR; float4 backL[4]; float4 backR[4]; float4 fitGrid;
    float4 motionSize; float4 motionMap; float4 keptAxes[3];
    float4 rigSize; float4 rigMap; float4 validRect; float4 frameMap;
}
// v1150: motionSize: the kept motion rectangle (xy) and the fresh one (zw), texels; motionMap: fresh_part, kept_part, on (both motion
// textures and the rules), mover_px; keptAxes: world -> kept camera (right, up, forward).
StructuredBuffer<float4> handFit : register(t4);          // v811: each hand's measured correction (target uv), from FitPick
Texture2D<float4> keptMotionTex : register(t5);           // v1150: the kept frame's motion (xyz world metres, w object motion px)
Texture2D<float4> freshMotionTex : register(t6);          // v1150: the fresh frame's
// v1159: the first-person rig (arms, body, what the hands hold) per eye, nonzero there; rigSize: the kept mask rectangle (xy) and the
// fresh one (zw); rigMap: on (both masks), the reach of a hand over the rig (m)
Texture2D<float> keptRigTex : register(t7);
Texture2D<float> freshRigTex : register(t8);
// v1167: where the kept (xy) and the fresh (zw) images hold scene: uv up to these (StaleLayers::border). The eye images carry a bright
// band at their right and bottom edges (8-11 or 36-46 px of 2496 x 2688 in v1160-v1165 dumps, the same in kept, fresh and next, both
// eyes; depth has none): texels there are no scene and are neither a source nor moved.
bool InKept(float2 q) {return all(q>=0) && all(q<=validRect.xy);}
bool InFresh(float2 p) {return all(p>=0) && all(p<=validRect.zw);}
bool KeptRig(float2 q) {return keptRigTex.Load(int3(clamp(int2(floor(q*rigSize.xy)),int2(0,0),int2(rigSize.xy)-1),0))>0;}
bool FreshRig(float2 p) {return freshRigTex.Load(int3(clamp(int2(floor(p*rigSize.zw)),int2(0,0),int2(rigSize.zw)-1),0))>0;}
RWStructuredBuffer<float4> fitSamples : register(u0);     // v811: the hand fit's kept samples (target uv, target depth, hand + luma)
RWStructuredBuffer<uint4> fitCounts : register(u1);       // v811: per shift (2 x 169 coarse, then 2 x 49 fine): depth+brightness inliers,
                                                          // samples, outline depth inliers, outline samples
RWStructuredBuffer<float4> fitResult : register(u2);      // v811: what handFit reads
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex WarpVS(uint id : SV_VertexID) {
    const float2 uv=float2((id<<1)&2,id&2);
    Vertex v;v.uv=uv;v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float4 Fetch(Texture2D image,float2 texel) {return image.SampleLevel(sourceSampler,texel*size.zw,0);}
// Catmull-Rom through nine bilinear taps at uv q.
float4 Smooth(Texture2D image,float2 q) {
    const float2 p=q*size.xy;
    const float2 c=floor(p-0.5)+0.5,f=p-c;
    const float2 w0=f*(-0.5+f*(1-0.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(0.5+f*(2-1.5*f)),w3=f*f*(-0.5+0.5*f);
    const float2 w12=w1+w2,c12=c+w2/w12,c0=c-1,c3=c+2;
    const float4 r=Fetch(image,float2(c0.x,c0.y))*w0.x*w0.y+Fetch(image,float2(c12.x,c0.y))*w12.x*w0.y+Fetch(image,float2(c3.x,c0.y))*w3.x*w0.y
                  +Fetch(image,float2(c0.x,c12.y))*w0.x*w12.y+Fetch(image,float2(c12.x,c12.y))*w12.x*w12.y+Fetch(image,float2(c3.x,c12.y))*w3.x*w12.y
                  +Fetch(image,float2(c0.x,c3.y))*w0.x*w3.y+Fetch(image,float2(c12.x,c3.y))*w12.x*w3.y+Fetch(image,float2(c3.x,c3.y))*w3.x*w3.y;
    // v803 (headset v802: "the distance is much less sharp than on the monitor"; band bursts: every rebuilt frame had 11-16% less fine
    // detail than the eye's own frames around it, and the monitor shows only own frames): sampling between pixels smooths detail. What
    // the cubic gained over a plain bilinear tap there is added again, 1.5 times (nothing where the sample sits on a pixel), kept within
    // the four pixels around the sample so a bright thin line rings no halo. The replayed v802 dumps: 0.997 of the own frames' detail.
    const float4 restored=r+1.5*(r-image.SampleLevel(sourceSampler,q,0));
    const int2 i0=int2(floor(p-0.5)),top=int2(size.xy)-1;
    const float4 t00=image.Load(int3(clamp(i0,int2(0,0),top),0)),t10=image.Load(int3(clamp(i0+int2(1,0),int2(0,0),top),0)),
                 t01=image.Load(int3(clamp(i0+int2(0,1),int2(0,0),top),0)),t11=image.Load(int3(clamp(i0+int2(1,1),int2(0,0),top),0));
    return saturate(clamp(restored,min(min(t00,t10),min(t01,t11)),max(max(t00,t10),max(t01,t11))));
}
float Ahead(Texture2D<float> depth,float2 extent,float2 m,float2 q) { // metres ahead of that image's camera; 0: the sky
    const float d=depth.Load(int3(clamp(int2(floor(q*extent)),int2(0,0),int2(extent)-1),0));
    return d<=1e-7?0:m.y/(d+m.x);
}
static const float reach=1.5; // v802: metres; the arms, what they hold or carry, the body
// The nearest of the four texels around q: a surface counts as gone only where none of them still has it.
float AheadNearest(Texture2D<float> depth,float2 extent,float2 m,float2 q) {
    const int2 c=clamp(int2(floor(q*extent-0.5)),int2(0,0),int2(extent)-2);
    const float d=max(max(depth.Load(int3(c,0)),depth.Load(int3(c+int2(1,0),0))),max(depth.Load(int3(c+int2(0,1),0)),depth.Load(int3(c+int2(1,1),0))));
    return d<=1e-7?0:m.y/(d+m.x);
}
// The fixed point on target pixel t's ray in one image (homography rows h[0..2], the target camera's centre h[3] in it): the pixel whose
// own depth puts it on the ray, iterated from target-side distance k0 (0: infinity). k: the target-side distance (1e30: the sky),
// negative without a solution; ok: the last step moved it less than a pixel.
float2 Solve(float4 h[4],Texture2D<float> depth,float2 extent,float2 m,float3 t,float k0,out float k,out bool ok) {
    const float3 s=float3(dot(h[0].xyz,t),dot(h[1].xyz,t),dot(h[2].xyz,t)),shift=h[3].xyz;
    k=-1;ok=false;
    if(s.z<=1e-6) return float2(0,0);
    float2 q=k0>0?(k0*s.xy+shift.xy)/(k0*s.z+shift.z):s.xy/s.z,last=q;
    [unroll] for(int n=0;n<5;++n) {
        const float ahead=Ahead(depth,extent,m,q);
        if(ahead<=0) {k=1e30;ok=true;return s.xy/s.z;}
        const float next=(ahead-shift.z)/s.z;
        if(next<=0) {k=-1;return q;}
        last=q;q=(next*s.xy+shift.xy)/ahead;k=next;
    }
    ok=all(abs(q-last)*size.xy<1);
    return q;
}
// Is the point at inverse target-side distance w on the ray (fresh image coordinates s*k+shift) at or behind what the fresh eye sees there?
// v1162 (headset v1158: "white shimmering around the right arm near the corner of the view"; "artifacts around the arms/body"; v1160
// dumps 5 and 2): what the fresh eye sees at fresh image point p, metres ahead; 0 outside its image. Ahead clamps to the edge texel, so the
// near search "met" whatever the fresh image has at its edge for a ray that leaves it: the 1-3 px white border of the eye image at the
// bottom-right corner (dump 5), the fresh eye's own sleeve at its left edge drawn over the floor this eye sees beside it (dump 2).
float AheadSeen(float2 p) {return !InFresh(p)?0:Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p);}
bool Behind(float3 s,float3 shift,float w) {
    const float k=1/w,ray=k*s.z+shift.z;
    const float a=AheadSeen((k*s.xy+shift.xy)/ray);
    return a>0 && ray>=a;
}
// The front-most surface on target pixel t's ray in the fresh image between 0.1 m and 1.25 reaches (headset v798: a hand 0.3 m
// away, nearer than any start, tore into stripes where a fixed point from 0.4 m stopped short): even steps along the epipolar
// line (inverse distance), the first crossing bisected five times, and kept only where the ray meets that surface; a crossing behind a
// near edge (what only this eye sees beside it) is none. k: the target-side distance. 1: a hit; 0: no near surface on the ray; -1: the
// ray passes behind a near edge first (edge: the fresh pixel 15 px in front of that crossing, the scenery beside the edge).
int NearHit(float3 t,out float2 hit,out float k,out float2 edge) {
    hit=float2(0,0);k=0;edge=float2(0,0);
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t)),shift=fresh[3].xyz;
    if(s.z<=1e-6) return 0;
    // v803 (headset v802 dump: a sword blade seen edge-on vanished from the rebuilt frame while walking): ten steps were 26 px apart
    // along the epipolar line, so a thin blade fell between them. 32 steps from 0.25 m to 1.25 reaches, about 6 px apart.
    // v809 (headset v807 dump: the sword hand held 17-25 cm from the eyes, walking, showed the sky through holes in it): the march
    // started at 0.25 m, so a hand nearer than that was never met and its old place counted as gone. 12 steps first from 0.1 m to
    // 0.25 m (coarser: anything that near is large in the view).
    const float wn=10,w0=4,w1=1/(1.25*reach);
    float front=wn;
    [loop] for(int i=1;i<=44;++i) {
        const float w=i<=12?lerp(wn,w0,i/12.0):lerp(w0,w1,(i-12)/32.0);
        if(Behind(s,shift,w)) {
            float lo=front,hi=w;
            [unroll] for(int j=0;j<5;++j) {const float m=0.5*(lo+hi);if(Behind(s,shift,m)) hi=m; else lo=m;}
            // A surface the ray meets is there on both sides of the crossing. Passing behind a near edge, the ray had the far
            // scene just before it (the edge's shadow starts only centimetres behind the edge, so a gap test alone took it).
            const float k_front=1/lo,ray_front=k_front*s.z+shift.z;
            const float a_front=AheadSeen((k_front*s.xy+shift.xy)/ray_front);
            k=1/hi;const float ray=k*s.z+shift.z;
            hit=(k*s.xy+shift.xy)/ray;
            const float a=AheadSeen(hit);
            if(a>0 && abs(ray-a)<0.02+0.03*a && a_front>0 && a_front-ray_front<0.02+0.03*a) return 1;
            // v804: 15 px in front (4 px sampled the edge's own dark rim on some rows and not others: stripes of it over the fill)
            const float k_edge=1/min(wn,lo+0.15);
            edge=(k_edge*s.xy+shift.xy)/(k_edge*s.z+shift.z);
            return -1;
        }
        front=w;
    }
    return 0;
}
// v805 (headset v804: the strip beside a moving hand, filled from the fresh eye, was 10-25% lighter or darker than this eye's own frames:
// a band behind each finger and the sword): the kept frame's own scenery for target ray t where that frame had it behind a near edge
// that has moved since. Walking carries the hands with the head, so where the ray's infinity lands in the kept image is beside the
// hand there: scenery a few centimetres off, in this eye's own shading. false: something within reach is there too, or (v807) that
// point lies outside the kept image.
bool KeptBeside(float3 t,out float2 q) {
    const float3 s=float3(dot(scene[0].xyz,t),dot(scene[1].xyz,t),dot(scene[2].xyz,t));
    q=s.z>1e-6?s.xy/s.z:float2(-1,-1);
    if(!InKept(q)) return false;
    const float d=Ahead(depthTex,depthSize.xy,depthMap.xy,q);
    return d<=0 || d>=reach;
}
)HLSL" R"HLSL(// v810: what the fresh eye sees now where the point at target-side distance k on ray t would be: 1 the same surface (within its depth
// tolerance), -1 something farther (no such surface there now), 0 something nearer hides it, 2 outside the fresh eye's view. p: that
// point in the fresh image.
int Confirm(float3 t,float k,out float2 p) {
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t));
    const float ray=k*s.z+fresh[3].z;
    p=float2(-1,-1);
    if(ray<=0) return 2;
    p=(k*s.xy+fresh[3].xy)/ray;
    if(!InFresh(p)) return 2;
    // within a texel: a surface seen at a grazing angle lands a texel beside its own silhouette in the other view (a still hand's side
    // read as gone); what moved lies tens of texels away
    const float tol=0.02+0.03*ray;bool farther=true;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        const float a=AheadNearest(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(x,y)/depthSize.zw);
        if(abs(a-ray)<tol) return 1;
        farther=farther && (a<=0 || a>ray);
    }
    return farther?-1:0;
}
// v813 (headset v812, snap turns: "artifacts on the hands"): the fresh eye sees only what lies farther within 6 texels around where the
// point at target-side distance k on ray t would be: that surface is not there now (a hand moved by a motion it did not make), not merely
// beside its own silhouette (a hand placed a few pixels off keeps its edges)
bool GoneWide(float3 t,float k) {
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t));
    const float ray=k*s.z+fresh[3].z;
    if(ray<=0) return false;
    const float2 p=(k*s.xy+fresh[3].xy)/ray;
    const float tol=0.02+0.03*ray;
    [unroll] for(int y=-2;y<=2;++y) [unroll] for(int x=-2;x<=2;++x) {
        const float a=Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(x,y)*3/depthSize.zw);
        if(a>0 && a<ray+tol && (rigMap.x<=0 || FreshRig(p+float2(x,y)*3/depthSize.zw))) return false; // v1159: the rig, not a prop beside it
    }
    return true;
}
// v810: fresh image point p (its surface `k` metres ahead) lies on that surface's edge: a texel beside it is clearly farther.
bool FreshEdge(float2 p,float k) {
    const float2 d=1.5/depthSize.zw;
    const float a=min(min(Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(d.x,0)),Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p-float2(d.x,0))),
                      min(Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(0,d.y)),Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p-float2(0,d.y))));
    const float b=max(max(Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(d.x,0)),Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p-float2(d.x,0))),
                      max(Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p+float2(0,d.y)),Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p-float2(0,d.y))));
    return a<=0 || b>1.2*k+0.05;
}
// v810: the kept pixel that shows ray t's surface under motion layer h (k: its target-side distance); false without one (the layer off,
// no fixed point, outside the kept image).
bool Candidate(float4 h[4],float3 t,out float2 q,out float k) {
    bool ok;q=Solve(h,depthTex,depthSize.xy,depthMap.xy,t,h[3].w,k,ok); // from the layer's own start (w), near what it moves
    return h[3].w>0 && ok && k>0 && InKept(q);
}
// v811: the kept point under kept pixel q (metres: right, up, forward of the kept camera); z 0 for the sky.
float3 KeptPoint(float2 q) {
    const float a=Ahead(depthTex,depthSize.xy,depthMap.xy,q);
    return float3(lerp(keptFov.x,keptFov.y,q.x)*a,lerp(keptFov.z,keptFov.w,q.y)*a,a);
}
// v811: the drawn hand that owns kept pixel q: 2 the left, 3 the right (the nearer palm, within its reach), 0 none (the world, the body).
int OwnerAt(float3 p) { // v1150: of kept point p (kept camera metres)
    if(p.z<=0) return 0;
    const float dl=palmL.w>0?distance(p,palmL.xyz):1e9,dr=palmR.w>0?distance(p,palmR.xyz):1e9;
    if(dl<=dr && dl<palmL.w) return 2;
    if(dr<dl && dr<palmR.w) return 3;
    return 0;
}
// v1159 (headset v1158: "artifacts when the arms come close to objects"): with the rig masks a kept pixel belongs to the player only if
// its mask says so. Then the nearer drawn palm within rigMap.y (a hand's reach over its forearm and what it holds) moves it, else the head
// (1: carried: the body, the upper arm); every other pixel is the world's however near a palm (v811's 22 cm sphere took a door handle,
// a bottle, the wall the hand touched along with the hand).
int Owner(float2 q) {
    if(rigMap.x>0) {
        if(!KeptRig(q)) return 0;
        const float3 p=KeptPoint(q);
        const float dl=palmL.w>0 && p.z>0?distance(p,palmL.xyz):1e9,dr=palmR.w>0 && p.z>0?distance(p,palmR.xyz):1e9;
        // v1164 (v1163 dumps 1-3: the hood 8 cm before the eye lay within 45 cm of the left palm and followed that hand's 0.05 mm recorded
        // motion, ~0.7 px at that distance: its dithered edge shimmered): a rig pixel nearer to the eye than to the palm is the head's
        if(min(dl,dr)>=min(rigMap.y,length(p))) return 1;
        return dl<=dr?2:3;
    }
    return OwnerAt(KeptPoint(q));
}
float Luma(float4 c) {return dot(c.rgb,float3(0.2126,0.7152,0.0722));}
)HLSL" R"HLSL(// v811 hand fit (headset v810 dumps: in a fall or a run both drawn hands moved 10-16 px in the view between two frames while the
// recorded grips held their place relative to the head, so the rebuilt hands stayed a frame behind; the game's drawn hand is not the
// recorded grip). The fresh eye shows each hand as the game drew it now: of the shifts of the hand's own kept pixels (moved by its
// recorded motion) the one whose points the fresh eye sees there, at their depth and in their brightness, corrects that motion.
// On its surface's outline: a kept texel 6 px away is clearly farther, or the sky (a hand's edge says where it is; its smooth inside does not).
bool KeptOutline(float2 q,float a) {
    float m=0;bool sky=false;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) if(x!=0 || y!=0) {
        const float b=Ahead(depthTex,depthSize.xy,depthMap.xy,q+float2(x,y)*6*size.zw);sky=sky || b<=0;m=max(m,b);
    }
    return sky || m>a*1.3+0.05;
}
// FitSample: per hand (z) a fitGrid.x x fitGrid.y grid over the square its reach covers in the kept image; its own pixels moved by its
// layer to the target (uv, depth) with their luma (gamma).
[numthreads(8,8,1)] void FitSample(uint3 id : SV_DispatchThreadID) {
    const uint n=uint(fitGrid.x)*uint(fitGrid.y),slot=id.z*n+id.y*uint(fitGrid.x)+id.x;
    if(any(id.xy>=uint2(fitGrid.xy))) return;
    const float4 palm=id.z?palmR:palmL;
    float4 r=float4(0,0,0,0);
    if(palm.w>0 && palm.z>0.05) {
        const float2 span=float2(keptFov.y-keptFov.x,keptFov.w-keptFov.z);
        const float2 centre=(palm.xy/palm.z-keptFov.xz)/span,extent=palm.w/palm.z/abs(span);
        const float2 q=centre+((float2(id.xy)+0.5)/fitGrid.xy*2-1)*extent;
        if(InKept(q) && Owner(q)==2+int(id.z)) {
            const float a=Ahead(depthTex,depthSize.xy,depthMap.xy,q);
            float4 b[4];if(id.z) b=backR; else b=backL;
            const float3 u=float3(q,1),s=float3(dot(b[0].xyz,u),dot(b[1].xyz,u),dot(b[2].xyz,u))*a+b[3].xyz;
            if(s.z>1e-4) r=float4(s.xy/s.z,s.z,1+id.z+(KeptOutline(q,a)?0.5:0)+0.499*saturate(Luma(pow(sourceTex.Load(int3(int2(q*size.xy),0)),1/2.2))));
        }
    }
    fitSamples[slot]=r;
}
// Where sample f (target uv, target depth, hand + outline + luma) moved by d (target uv) lands, does the fresh eye see that surface:
// bit 0 at its depth, bit 1 at its depth and in its brightness.
uint FitMatch(float4 f,float2 d) {
    const float3 t=float3(f.xy+d,1),s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t));
    const float ray=f.z*s.z+fresh[3].z;
    if(ray<=0) return 0;
    const float2 p=(f.z*s.xy+fresh[3].xy)/ray;
    if(!InFresh(p)) return 0;
    const float a=Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p);
    if(!(a>0) || abs(a-ray)>=0.01+0.02*ray) return 0;
    if(rigMap.x>0 && !FreshRig(p)) return 0; // v1159: a hand is matched against the fresh eye's hand, not the wall it touches
    const float e=frac(f.w)>=0.5?0.5:0,lk=(frac(f.w)-e)/0.499,lf=Luma(pow(freshTex.Load(int3(int2(p*size.xy),0)),1/2.2));
    return abs(lk-lf)<0.12*max(lk,lf)+0.024?3:1;
}
groupshared uint4 fitAcc[256];
void FitCount(uint hand,float2 d,uint i,uint slot) {
    uint4 acc=uint4(0,0,0,0);
    const uint n=uint(fitGrid.x)*uint(fitGrid.y);
    for(uint j=(hand-1)*n+i;j<hand*n;j+=256) {
        const float4 f=fitSamples[j];
        if(uint(f.w)!=hand) continue;
        const uint m=FitMatch(f,d);const bool edge=frac(f.w)>=0.5;
        acc+=uint4(m>>1,1,edge?m&1:0,edge?1:0);
    }
    fitAcc[i]=acc;GroupMemoryBarrierWithGroupSync();
    [unroll] for(uint s=128;s>0;s>>=1) {if(i<s) fitAcc[i]+=fitAcc[i+s];GroupMemoryBarrierWithGroupSync();}
    if(i==0) fitCounts[slot]=fitAcc[0];
}
// How much better counts c explain the hand than no shift (z): the gains of both fractions; -1 unless both gain (brightness alone is
// fooled by a glass in front of the fingers, the outline alone by the floor beside the hand).
float FitGain(uint4 c,uint4 z) {
    if(z.y<32 || z.w<16) return -1;
    const float ga=(float(c.x)-float(z.x))/z.y,ge=(float(c.z)-float(z.z))/z.w;
    return ga>0.02 && ge>0.02?ga+ge:-1;
}
// The coarse shift of hand h that gains most (target pixels; none: 0 0): the middle of all within 1% of the best, so a plateau of equally
// good shifts (a smooth hand) gives its centre, not its nearer end (a lag).
float2 CoarseBest(uint h) {
    const uint4 z=fitCounts[h*169+84];float bg=0;
    for(uint k=0;k<169;++k) bg=max(bg,FitGain(fitCounts[h*169+k],z));
    if(bg<=0) return float2(0,0);
    float2 sum=0;float n=0;
    for(uint m=0;m<169;++m) if(FitGain(fitCounts[h*169+m],z)>=bg-0.01) {sum+=float2(int2(m%13,m/13)-6);n+=1;}
    return round(sum/n*fitGrid.z);
}
// FitCoarse: group (shift, hand), 13 x 13 shifts fitGrid.z target pixels apart. FitFine: 7 x 7 one pixel apart around the coarse best.
[numthreads(256,1,1)] void FitCoarse(uint3 g : SV_GroupID,uint i : SV_GroupIndex) {
    FitCount(g.y+1,float2(int2(g.x%13,g.x/13)-6)*fitGrid.z*size.zw,i,g.y*169+g.x);
}
groupshared float2 fitCentre;
[numthreads(256,1,1)] void FitFine(uint3 g : SV_GroupID,uint i : SV_GroupIndex) {
    if(i==0) fitCentre=CoarseBest(g.y);
    GroupMemoryBarrierWithGroupSync();
    FitCount(g.y+1,(fitCentre+float2(int2(g.x%7,g.x/7)-3))*size.zw,i,338+g.y*49+g.x);
}
// FitPick: per hand the fine shift that gains most (the middle of all within 1% of it), kept only if it gains clearly (0.10 together);
// fitResult: the shift (target uv), the depth+brightness fraction there and with none.
[numthreads(2,1,1)] void FitPick(uint i : SV_GroupIndex) {
    const uint4 z=fitCounts[i*169+84];
    const float2 centre=CoarseBest(i);
    float bg=-1;uint best=24;
    for(uint k=0;k<49;++k) {const float g=FitGain(fitCounts[338+i*49+k],z);if(g>bg) {bg=g;best=k;}}
    float2 sum=0;float n=0;
    for(uint m=0;m<49;++m) if(FitGain(fitCounts[338+i*49+m],z)>=bg-0.01) {sum+=float2(int2(m%7,m/7)-3);n+=1;}
    const float2 px=centre+sum/max(n,1);
    const uint4 c=fitCounts[338+i*49+best];
    const float base=z.y?float(z.x)/z.y:0;
    fitResult[i]=bg>=0.10?float4(px*size.zw,float(c.x)/max(c.y,1u),base):float4(0,0,base,base);
}
)HLSL" R"HLSL(// v1150 movers (design 2026-11-11 section 3; the motion textures, see motionMap). Only called where motionMap.z is on.
float4 MotionAt(Texture2D<float4> m,float2 extent,float2 q) {return m.Load(int3(clamp(int2(floor(q*extent)),int2(0,0),int2(extent)-1),0));}
bool Mover(float4 m) {return m.w>=motionMap.w;}
bool FreshMover(float2 p) {return InFresh(p) && Mover(MotionAt(freshMotionTex,motionSize.zw,p));}
// a world displacement in the kept camera (right, up, forward), times part
float3 KeptStep(float3 d,float part) {return part*float3(dot(keptAxes[0].xyz,d),dot(keptAxes[1].xyz,d),dot(keptAxes[2].xyz,d));}
// a kept camera point (right, up, forward metres) <-> kept image homogeneous (u D, v D, D): KeptPoint's map, both ways (linear)
float3 KeptImage(float3 p) {return float3((p.x-keptFov.x*p.z)/(keptFov.y-keptFov.x),(p.y-keptFov.z*p.z)/(keptFov.w-keptFov.z),p.z);}
float3 KeptFrom(float3 h) {return float3(keptFov.x*h.z+(keptFov.y-keptFov.x)*h.x,keptFov.z*h.z+(keptFov.w-keptFov.z)*h.y,h.z);}
// the kept frame has a surface d metres ahead at kept pixel q (within a texel and Confirm's tolerance)
// M2's test: kept pixel q (its surface d metres ahead) shows a mover, or the fringe a mover's edge leaves on what lies behind it (v1152
// dump 5: anti-aliasing darkens 1-2 px of floor beyond a boot's edge, motion and depth there say "still floor", and that fringe stayed at
// the boot's old place as a dark line): a mover texel 2 px away, 8 directions, nearer than d (by any margin: a sole lies 8 cm before the
// floor, within every depth tolerance). A mover at least 2 px thick covers the texel 2 px beyond the fringe in some direction.
bool KeptMoverAt(float2 q,float d) {
    if(Mover(MotionAt(keptMotionTex,motionSize.xy,q))) return true;
    [unroll] for(int i=0;i<8;++i) {
        float2 dir;sincos(i*0.7853982,dir.y,dir.x);
        const float2 o=q+round(dir*2)/motionSize.xy;
        if(Mover(MotionAt(keptMotionTex,motionSize.xy,o)) && Ahead(depthTex,depthSize.xy,depthMap.xy,o)<0.995*d) return true;
    }
    return false;
}
bool KeptHas(float2 q,float d) {
    const float tol=0.02+0.03*d;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
        if(abs(AheadNearest(depthTex,depthSize.xy,depthMap.xy,q+float2(x,y)/depthSize.xy)-d)<tol) return true;
    return false;
}
// The front-most surface on target ray t in the fresh image beyond NearHit's range (1.25 reaches). The far part of the epipolar line is
// short (the eyes' disparity at 1.25 reaches: ~38 px at 1129 px per tangent, ~16 px in the lab): 15 even steps in inverse distance
// (even in pixels), the first crossing bisected five times and kept only where the ray meets that surface (NearHit's test: not a
// crossing behind an edge). No crossing up to 30 m: the fixed point from 30 m (the rest of the line is a few pixels).
bool FarHit(float3 t,out float2 hit,out float k) {
    hit=float2(0,0);k=0;
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t)),shift=fresh[3].xyz;
    if(s.z<=1e-6) return false;
    const float w1=1/(1.25*reach);
    float front=w1;
    [loop] for(int i=1;i<=15;++i) {
        const float w=w1*(16-i)/16.0;
        if(Behind(s,shift,w)) {
            float lo=front,hi=w;
            [unroll] for(int j=0;j<5;++j) {const float m=0.5*(lo+hi);if(Behind(s,shift,m)) hi=m; else lo=m;}
            const float k_front=1/lo,ray_front=k_front*s.z+shift.z;
            const float a_front=AheadSeen((k_front*s.xy+shift.xy)/ray_front);
            k=1/hi;const float ray=k*s.z+shift.z;
            hit=(k*s.xy+shift.xy)/ray;
            const float a=AheadSeen(hit);
            return a>0 && abs(ray-a)<0.02+0.03*a && a_front>0 && a_front-ray_front<0.02+0.03*a;
        }
        front=w;
    }
    bool ok;hit=Solve(fresh,freshDepthTex,depthSize.zw,freshMap.xy,t,16/w1,k,ok);
    return ok && k>0 && k<1e29;
}
// M1, a mover the fresh eye sees: the fresh eye's front-most surface on ray t, where it moves, shows there (the fresh geometry) in this
// eye's own kept pixel of it a tick ago (P_now - fresh_part * its motion) when the kept depth has it there, else in the fresh eye's (what
// the kept frame did not show). Looked for only where the fresh image has a mover on the ray's epipolar line at infinity, 1.25 reaches,
// 1 m or 0.5 m (a mover wider along that line than the gaps between them covers one). Hands stay on the hand layers. k: 1e38 none.
float MoverSeen(float3 t,out float4 colour) {
    colour=float4(0,0,0,0);
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t)),shift=fresh[3].xyz;
    if(s.z<=1e-6) return 1e38;
    bool some=FreshMover(s.xy/s.z);
    [unroll] for(int i=0;i<3;++i) {
        const float g=i==0?1.25*reach:i==1?1.0:0.5,ray=g*s.z+shift.z;
        if(ray>0) some=some || FreshMover((g*s.xy+shift.xy)/ray);
    }
    if(!some) return 1e38;
    float2 p,edge;float k;
    const int near=NearHit(t,p,k,edge);
    if(near<0 || (near==0 && !FarHit(t,p,k))) return 1e38;
    // v1152: movers are a rule for the world beyond reach only. Within reach the fresh eye already places what moved (NearHit), and the
    // arms, a drawn sword and what the hands carry have no usable engine motion (measured 2026-11-11: ~300 px off).
    if((rigMap.x>0?FreshRig(p):k<reach) || !FreshMover(p)) return 1e38;
    const float3 now=KeptFrom(k*float3(dot(scene[0].xyz,t),dot(scene[1].xyz,t),dot(scene[2].xyz,t))+scene[3].xyz);
    const float3 then=now-KeptStep(MotionAt(freshMotionTex,motionSize.zw,p).xyz,motionMap.x);
    if(rigMap.x<=0 && (OwnerAt(now)!=0 || OwnerAt(then)!=0)) return 1e38;
    const float3 h=KeptImage(then);
    const float2 q=h.z>1e-4?h.xy/h.z:float2(-1,-1);
    colour=InKept(q) && KeptHas(q,h.z)?Smooth(sourceTex,q):Smooth(freshTex,p);
    return k;
}
// M3, a mover only this eye sees: the kept pixel whose surface, carried on by kept_part of its own motion, lies on ray t. Two fixed-point
// steps from a seed motion: the ray's own kept pixel's, else the first mover on two rings of 8 around it (0.03 and 0.08 tangent: a mover
// moves as one, so any of its pixels seeds it); kept only where the motion it lands on is the one it was carried by, and not where the
// fresh eye sees farther at that point (the mover has gone): hidden behind something nearer, outside its view, or confirmed.
float MoverHidden(float3 t,float2 q,out float4 colour) {
    colour=float4(0,0,0,0);
    float2 at=q;float4 seed=MotionAt(keptMotionTex,motionSize.xy,q);
    const float2 ring=1/abs(float2(keptFov.y-keptFov.x,keptFov.w-keptFov.z));
    [unroll] for(int r=0;r<2;++r) [unroll] for(int i=0;i<8;++i) if(!Mover(seed)) {
        float2 dir;sincos(i*0.7853982,dir.y,dir.x);
        at=q+dir*(r?0.08:0.03)*ring;
        if(InKept(at)) seed=MotionAt(keptMotionTex,motionSize.xy,at);
    }
    if(!Mover(seed)) return 1e38;
    float4 h[4];h[0]=scene[0];h[1]=scene[1];h[2]=scene[2];h[3]=scene[3];
    float3 d=KeptStep(seed.xyz,motionMap.y);
    float k=0,start=Ahead(depthTex,depthSize.xy,depthMap.xy,at);float2 c=at;
    [unroll] for(int n=0;n<2;++n) {
        h[3].xyz=scene[3].xyz-KeptImage(d);   // the target ray moved back by d: its fixed point is the kept surface d carries onto it
        bool ok;c=Solve(h,depthTex,depthSize.xy,depthMap.xy,t,start,k,ok);
        if(!ok || !(k>0) || k>=1e29 || !InKept(c)) return 1e38;
        const float4 m=MotionAt(keptMotionTex,motionSize.xy,c);
        if(!Mover(m)) return 1e38;
        const float3 e=KeptStep(m.xyz,motionMap.y);
        if(n==1 && distance(e,d)>0.01+0.01*k) return 1e38;
        d=e;start=k;
    }
    if((rigMap.x<=0 && k<reach) || Owner(c)!=0) return 1e38;
    // (Confirm's texel beside also confirms: the ray that grazes the edge hiding it, which NearHit refuses so M1 missed it. Then its own
    // texel must hide it (nearer) or be a mover too: v1152 dump 5, a boot lifting off the floor: the floor 8 cm behind the sole is within
    // Confirm's depth tolerance, and "confirmed" by the floor M3 carried the old sole on by stale motion into a dark band under the boot.)
    float2 p;const int seen=Confirm(t,k,p);
    if(seen<0) return 1e38;
    if(seen==1) {
        const float ray=k*dot(fresh[2].xyz,t)+fresh[3].z,a=Ahead(freshDepthTex,depthSize.zw,freshMap.xy,p);
        if(!(a>0 && a<ray-0.02-0.03*ray) && !FreshMover(p)) return 1e38;
    }
    colour=Smooth(sourceTex,c);
    return k;
}
)HLSL" R"HLSL(// v1173: the drawn hand (2 left, 3 right) a rig point (kept camera metres) belongs to, if it moves (frameMap.y left, .z right); 0 none
int MovingHandAt(float3 p) {
    const float dl=palmL.w>0?distance(p,palmL.xyz):1e9,dr=palmR.w>0?distance(p,palmR.xyz):1e9;
    if(min(dl,dr)>=min(rigMap.y,length(p))) return 0;
    const int h=dl<=dr?2:3;
    return (h==2?frameMap.y:frameMap.z)>0?h:0;
}
// v1173: kept pixel q is a moving hand's, or its 2 px fringe on what lies behind (KeptMoverAt's rule)
bool KeptMovingHand(float2 q) {
    if(KeptRig(q) && MovingHandAt(KeptPoint(q))!=0) return true;
    const float d=Ahead(depthTex,depthSize.xy,depthMap.xy,q);
    [unroll] for(int i=0;i<8;++i) {
        float2 dir;sincos(i*0.7853982,dir.y,dir.x);
        const float2 o=q+round(dir*2)/depthSize.xy;
        if(InKept(o) && KeptRig(o) && Ahead(depthTex,depthSize.xy,depthMap.xy,o)<0.995*(d>0?d:1e30) && MovingHandAt(KeptPoint(o))!=0) return true;
    }
    return false;
}
// v1171: a camera cut (frameMap.x): the target view from the fresh eye alone: its near surface on the ray (NearHit), else its fixed
// point, else the nearest valid fresh pixel to the ray's far point (never the kept image, never black, never the edge band)
float4 FreshOnly(float3 t) {
    float2 now,edge;float k;
    if(NearHit(t,now,k,edge)>0 && InFresh(now)) return Smooth(freshTex,now);
    bool ok;const float2 f=Solve(fresh,freshDepthTex,depthSize.zw,freshMap.xy,t,0,k,ok);
    if(k>0 && InFresh(f)) return Smooth(freshTex,f);
    const float3 s=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t));
    const float2 g=s.z>1e-6?s.xy/s.z:t.xy;
    return Smooth(freshTex,clamp(g,size.zw,validRect.zw-size.zw));
}
float4 WarpPS(Vertex v) : SV_TARGET {
    if(depthMap.w<=0) {
        const float3 t0=float3(v.uv,1);
        const float3 s=float3(dot(row0.xyz,t0),dot(row1.xyz,t0),dot(row2.xyz,t0));
        if(s.z<=1e-6) return float4(0,0,0,1);
        return Smooth(sourceTex,s.xy/s.z);
    }
    // v1174 (headset v1173: artifacts at the view's edges in turns): a pixel in the image's own band is rebuilt like the rest, as the ray at
    // the valid rectangle's inner edge (the last valid column and row replicated outward), not this eye's frozen kept pixel
    const float3 t=float3(min(v.uv,validRect.xy),1);
    if(frameMap.x>0) return FreshOnly(t);                         // v1171: a camera cut: the fresh eye alone, the band too
    float kept_k;bool kept_ok;
    const float2 q=Solve(scene,depthTex,depthSize.xy,depthMap.xy,t,0,kept_k,kept_ok);
    const bool kept_in=kept_k>=0 && InKept(q);
    if(fresh[3].w<=0) return kept_k<0?float4(0,0,0,1):Smooth(sourceTex,q); // no fresh eye: the scene layer alone (v796)
    // v1173 (headset v1172: a halo around the hands while they move: the recorded grip over-moves the drawn hand, and the scenery around was
    // filled around that wrong place): a moving hand comes from the fresh eye, gated by the masks; where this eye's kept pixel is that hand's
    // (its old place, with its 2 px fringe), the fresh eye's surface on the ray; only where the fresh eye cannot see the spot, today's rules.
    // Where the fresh eye sees scenery on the ray, a moving hand's layer (its recorded grip) places nothing either (fresh_scenery).
    bool fresh_scenery=false;
    if(frameMap.y>0 || frameMap.z>0 || frameMap.w>0) {
        float2 p,edge;float k;
        const int nh=NearHit(t,p,k,edge);
        // v1175: under motion, near scenery (not rig) the fresh eye sees on the ray is taken from it as a whole: one source and one
        // registration per near object (v1174 dumps: the kept candidates a tick behind, 7.5-11.5 levels off the next frame, the fresh 2.7)
        if(frameMap.w>0 && nh>0 && k<reach && InFresh(p) && !FreshRig(p)) return Smooth(freshTex,p);
        if(nh>0 && InFresh(p) && FreshRig(p) &&
           MovingHandAt(KeptFrom(k*float3(dot(scene[0].xyz,t),dot(scene[1].xyz,t),dot(scene[2].xyz,t))+scene[3].xyz))!=0) return Smooth(freshTex,p);
        float2 f=float2(-1,-1);
        if(nh>0 && InFresh(p) && !FreshRig(p)) {fresh_scenery=true;f=p;}
        else if(nh==0) {bool ok;float kf;f=Solve(fresh,freshDepthTex,depthSize.zw,freshMap.xy,t,0,kf,ok);fresh_scenery=ok && kf>0 && InFresh(f);}
        if(fresh_scenery && kept_in && KeptMovingHand(q)) return Smooth(freshTex,f);
        // v1174 (headset v1173: moving hands near the view's edge): a moving hand's kept rig pixel at a spot outside the fresh eye's view
        // shows in place (this eye's own pixel a tick old), not moved by the recorded grip
        if(kept_in && kept_ok && KeptRig(q) && MovingHandAt(KeptPoint(q))!=0) {float2 cp;if(Confirm(t,kept_k,cp)==2) return Smooth(sourceTex,q);}
    }
    // v810 (headset v809, see StaleLayers): this eye's own kept image wherever one of the motions explains it. Each layer gives its kept
    // pixel for this ray; the fresh eye, which sees this moment, confirms or refutes the surface there; the nearest confirmed one shows
    // (a hand in front of the wall it moved past). The world beyond reach is taken as it is (v805: never the other eye's shading there;
    // guards walking far away step a frame behind). Walking, jumping, falling and turning carry the body, arms and hands with the head;
    // moving hands carry what they hold. The old rules for each case (v801 P-test, v804 walking strip, v807 snap turn, v809 hands near
    // the face) are the fallbacks below, for what no motion explains: what only the fresh eye shows now, what a move uncovered.
    // The candidates: 0 the world, 1 carried with the head, 2 and 3 with each drawn hand (the motion layers count within reach only).
    // v811 (headset v810: "many artifacts on the hands and what they hold, very unstable; the glass full of artifacts"): every motion
    // competed for every pixel, so on a hand they tied and neighbouring pixels took the left hand's, the right hand's, the head's or the
    // world's kept pixel, a few pixels apart (a grain on a still hand); a colour check against the other eye sent the glass, which that eye
    // sees through differently, to it in fragments. Now each kept pixel has one owner: a drawn hand owns what lies within its reach of its
    // palm, and only that hand's motion moves it (the world's candidate on it is the hand's old place, what its move uncovered). The
    // hand's motion is the recorded one corrected by the fresh eye (FitPick): the hand where the game drew it. No colour from the other eye
    // replaces a kept pixel that a motion explains.
    const float2 fitL=handFit[0].xy,fitR=handFit[1].xy;
    float2 cq[4],cp[4];float ck[4];int ca[4];
    cq[0]=q;ck[0]=kept_k;cp[0]=float2(-1,-1);ca[0]=kept_in && kept_ok?Confirm(t,kept_k,cp[0]):-2;
    ca[1]=Candidate(carried,t,cq[1],ck[1])?Confirm(t,ck[1],cp[1]):-2;
    ca[2]=Candidate(handL,t-float3(fitL,0),cq[2],ck[2])?Confirm(t,ck[2],cp[2]):-2;
    ca[3]=Candidate(handR,t-float3(fitR,0),cq[3],ck[3])?Confirm(t,ck[3],cp[3]):-2;
    [unroll] for(int i=1;i<4;++i) if(ca[i]!=-2 && ck[i]>=reach) ca[i]=-2;
    if(fresh_scenery) {if(frameMap.y>0) ca[2]=-2;if(frameMap.z>0) ca[3]=-2;} // v1173
    if(frameMap.y>0 && ca[2]==-1) ca[2]=-2;if(frameMap.z>0 && ca[3]==-1) ca[3]=-2; // v1173: a moving hand the fresh eye refutes there is not there
    int own[4];
    own[0]=kept_in?Owner(q):0;
    [unroll] for(int o=1;o<4;++o) own[o]=ca[o]!=-2?Owner(cq[o]):0;
    if(own[0]!=0 && ca[0]!=-2) ca[0]=-1;                                       // a hand's pixel is no world
    if(own[1]!=0 && own[1]!=1) ca[1]=-2;                                       // nor the head's (v1159: the body is, with masks)
    [unroll] for(int h=2;h<4;++h) if(ca[h]!=-2 && own[h]!=0 && own[h]!=h) ca[h]=-2; // nor the other hand's
    // v1150 M2: a kept pixel that moves is no world (the far rule below does not keep it either): its old place shows what is there now.
    // Hand-owned pixels keep the hand layers. Without motion textures nothing is read here.
    // v1159: with the rig masks "not the player's" replaces "beyond reach" (a guard within reach moves too; arms never count)
    const bool movers=motionMap.z>0 && own[0]==0;
    bool kept_mover=false;
    if(movers && kept_in && (rigMap.x>0?!KeptRig(q):kept_k>=reach)) kept_mover=KeptMoverAt(q,kept_k);
    if(kept_mover && ca[0]!=-2) ca[0]=-1;
    const int world=ca[0];
    // The world beyond reach is taken as it is (v805: never the other eye's shading there; guards walking far away step a frame behind),
    // unless the fresh eye sees something nearer there: then something may cover this ray now, and the fallbacks look.
    const bool far_world=world!=-2 && kept_k>=reach && !kept_mover;
    bool usable[4];float front=far_world && world!=0?kept_k:1e38;
    [unroll] for(int i=0;i<4;++i) {
        // a hand's own pixels as its motion puts them (the fresh eye sees them from elsewhere, or not at all), unless the fresh eye sees
        // past that place altogether (v813: the hand is not there: after a snap turn the recorded grip turned in place while the game moved
        // the hand away, and v811 left a dark ghost of hand and sleeve); the rest confirmed by the fresh eye; a motion layer also where the
        // fresh eye cannot see the point (outside its view) and the world is not confirmed there (canted eyes do not overlap at their outer edges)
        // v1159: with the masks the body's own pixels go with the head the same way
        usable[i]=i==0?!far_world && world==1:(i>=1 && own[i]==i)?ca[i]!=-2 && !(ca[i]==-1 && GoneWide(t,ck[i])):ca[i]==1 || (ca[i]==2 && world!=1);
        if(usable[i]) front=min(front,ck[i]);
    }
    // v1150 M1 (the fresh eye sees it), else M3 (only this eye does): a mover on this ray, its distance and colour; ranked after a hand's
    // own pixels, before the world
    float4 mover_colour=float4(0,0,0,0);float mover_k=1e38;
    if(movers) {
        mover_k=MoverSeen(t,mover_colour);
        if(mover_k>=1e37 && kept_in && world!=1) mover_k=MoverHidden(t,q,mover_colour);
        if(mover_k<1e37) front=min(front,mover_k);
    }
    if(front<1e37) {
        if(far_world && front>=kept_k) return Smooth(sourceTex,q);
        // Near-ties (two motions both a little off) go by a fixed preference, never pixel by pixel: a hand's own pixels, the world, the
        // head's, then a hand's motion for what no hand owns (the far end of a sword).
        int best=-1;float rank=9;
        [unroll] for(int j=0;j<4;++j) if(usable[j] && ck[j]<=front*1.03+0.01) {
            const float r=(j>=1 && own[j]==j)?0:j==0?1:j==1?2:3;
            if(r<rank) {rank=r;best=j;}
        }
        if(mover_k<=front*1.03+0.01 && rank>0.5) return mover_colour;
        return Smooth(sourceTex,cq[best]);
    }
    float k;
    // nothing of the kept image is confirmed
    float2 now,edge;
    const int near=NearHit(t,now,k,edge);
    // near, moving its own way (a hand untracked), as it is now; where the world was only hidden from the fresh eye, not on that near
    // surface's very edge (the fresh depth's texels can claim the pixel beside a still silhouette)
    if(near>0 && k<reach && (world!=0 || (k<kept_k-0.02 && !FreshEdge(now,k)))) return Smooth(freshTex,now);
    if(kept_in && (world==0 || world==2)) return Smooth(sourceTex,q);                 // hidden from the fresh eye, or beyond its view: this eye's own
    float2 b;if(KeptBeside(t,b)) {                                                      // what a move uncovered: this eye's own scenery beside it (v805),
        bool gone=false;                                                                 // not a mover's old place or its fringe (v1150 M2)
        if(movers) {const float a=Ahead(depthTex,depthSize.xy,depthMap.xy,b);gone=KeptMoverAt(b,a>0?a:1e30);}
        if(!gone) return Smooth(sourceTex,b);
    }
    bool ok;const float2 f=Solve(fresh,freshDepthTex,depthSize.zw,freshMap.xy,t,0,k,ok);
    const float2 g=ok || near==0?f:edge;
    if(k>0 && InFresh(g)) return Smooth(freshTex,g);                       // what only the fresh eye has (a snap turn's new view)
    if(kept_in) return Smooth(sourceTex,q);                                             // v810: never the fresh image's clamped edge (stretched)
    // v1166 (v1165 hold dump 2, a camera cut: the kept view barely overlapped and NearHit's edge point lay outside the fresh image): black
    // only where the fresh eye has no point on the ray either
    if(k>0 && InFresh(f)) return Smooth(freshTex,f);
    // v1174: never black: the nearest valid fresh pixel to the ray's far point (as on a cut)
    const float3 sf=float3(dot(fresh[0].xyz,t),dot(fresh[1].xyz,t),dot(fresh[2].xyz,t));
    return Smooth(freshTex,clamp(sf.z>1e-6?sf.xy/sf.z:t.xy,size.zw,validRect.zw-size.zw));
}
)HLSL";
        auto v=compile(source,"WarpVS","vs_5_0"),p=compile(source,"WarpPS","ps_5_0");
        checked(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex_),"vertex shader");
        checked(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel_),"pixel shader");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        checked(device_->CreateSamplerState(&sampler,&sampler_),"sampler");
        D3D11_BUFFER_DESC constants{};constants.ByteWidth=768;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.Usage=D3D11_USAGE_DEFAULT;
        checked(device_->CreateBuffer(&constants,nullptr,&parameters_),"parameter buffer");
        const char* fit_entries[]{"FitSample","FitCoarse","FitFine","FitPick"};
        for(std::size_t n=0;n<fit_.size();++n) {
            const auto c=compile(source,fit_entries[n],"cs_5_0");
            checked(device_->CreateComputeShader(c->GetBufferPointer(),c->GetBufferSize(),nullptr,&fit_[n]),"hand fit shader");
        }
        fit_samples_=structured(2*fit_grid*fit_grid,16,D3D11_BIND_UNORDERED_ACCESS,fit_samples_uav_);
        fit_counts_=structured(2*169+2*49,16,D3D11_BIND_UNORDERED_ACCESS,fit_counts_uav_);
        fit_result_=structured(2,16,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE,fit_result_uav_);
        D3D11_SHADER_RESOURCE_VIEW_DESC fit_read{};fit_read.Format=DXGI_FORMAT_UNKNOWN;fit_read.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;fit_read.Buffer.NumElements=2;
        checked(device_->CreateShaderResourceView(fit_result_.Get(),&fit_read,&fit_result_srv_),"hand fit read view");
        for(auto& s:fit_staging_) {
            D3D11_BUFFER_DESC d{};d.ByteWidth=32;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=16;
            checked(device_->CreateBuffer(&d,nullptr,&s),"hand fit read-back");
        }
        D3D11_RASTERIZER_DESC rasterizer{};rasterizer.FillMode=D3D11_FILL_SOLID;rasterizer.CullMode=D3D11_CULL_NONE;rasterizer.DepthClipEnable=TRUE;
        checked(device_->CreateRasterizerState(&rasterizer,&raster_),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC depthState{};depthState.DepthFunc=D3D11_COMPARISON_ALWAYS;
        checked(device_->CreateDepthStencilState(&depthState,&depth_),"depth state");
    }
    // The kept image warped by `h` (target pixel -> kept pixel); same size, RGBA8 typeless with _SRGB views. With `layers` and the kept
    // frame's depth: near content from `fresh` (the fresh eye's image of this moment, its depth `fresh_depth`) when the layers have its
    // camera, the rest from the kept image with parallax; without, `h` alone.
    // v1150: with both eyes' motion textures (kept_motion: the kept frame's, fresh_motion: the fresh frame's) and layers->motion_rules,
    // movers go where the fresh eye has them (M1-M3); without, nothing changes.
    // v1159: with both eyes' rig masks (kept_rig, fresh_rig) the player's own pixels are the masks' (Owner); without, the palm spheres.
    ID3D11Texture2D* apply(ID3D11Texture2D* source,const WarpMatrix& h,const StaleLayers* layers=nullptr,const StaleDepth& depth={},
                           ID3D11Texture2D* fresh=nullptr,const StaleDepth& fresh_depth={},const StaleMotion& kept_motion={},
                           const StaleMotion& fresh_motion={},const StaleRig& kept_rig={},const StaleRig& fresh_rig={},const StaleRig& rig_target={}) {
        if(!source) throw std::runtime_error("Stale eye warp input is missing");
        D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        Ptr<ID3D11Device> owner;source->GetDevice(&owner);
        const auto image_ok=[&](const D3D11_TEXTURE2D_DESC& d) {
            return d.SampleDesc.Count==1 && d.ArraySize==1 &&
                (d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS);
        };
        if(owner.Get()!=device_.Get() || !image_ok(desc)) throw std::runtime_error("Stale eye warp input format or device differs");
        ContextState::Scope scope(state_);
        resize(desc.Width,desc.Height);
        Ptr<ID3D11ShaderResourceView> kept_own,fresh_own;
        ID3D11ShaderResourceView* read=image_view(source,0,kept_own);
        ID3D11ShaderResourceView *depth_read=nullptr,*fresh_read=nullptr,*fresh_depth_read=nullptr;
        if(layers && depth.texture && depth.width && depth.height) {depth_read=depth_view(depth);if(!depth_read) ++depth_refusals;}
        if(depth_read && layers->fresh && fresh && fresh_depth.texture && fresh_depth.width && fresh_depth.height) {
            D3D11_TEXTURE2D_DESC f{};fresh->GetDesc(&f);Ptr<ID3D11Device> fresh_owner;fresh->GetDevice(&fresh_owner);
            if(fresh_owner.Get()==device_.Get() && image_ok(f) && f.Width==desc.Width && f.Height==desc.Height) fresh_depth_read=depth_view(fresh_depth);
            if(fresh_depth_read) fresh_read=image_view(fresh,1,fresh_own);
            if(!fresh_read) {fresh_depth_read=nullptr;++fresh_refusals;}
        }
        ID3D11ShaderResourceView *kept_motion_read=nullptr,*fresh_motion_read=nullptr;
        if(fresh_read && layers->motion_rules && !layers->cut && kept_motion.texture && fresh_motion.texture) { // v1171: no movers on a cut
            kept_motion_read=motion_view(kept_motion);fresh_motion_read=motion_view(fresh_motion);
            if(!kept_motion_read || !fresh_motion_read) {kept_motion_read=fresh_motion_read=nullptr;++motion_refusals;}
        }
        ID3D11ShaderResourceView *kept_rig_read=nullptr,*fresh_rig_read=nullptr;
        if(fresh_read && kept_rig.texture && fresh_rig.texture) {
            kept_rig_read=rig_view(kept_rig);fresh_rig_read=rig_view(fresh_rig);
            if(!kept_rig_read || !fresh_rig_read) {kept_rig_read=fresh_rig_read=nullptr;++rig_refusals;}
        }
        // v1177 AFW v2: the forward splat when the layers ask for it and every input it needs is here (render-sized motion and masks of the
        // depth's rectangle); a cut keeps the fresh-eye-only path below
        if(layers && layers->afw2 && depth_read && fresh_read && !layers->cut) {
            const bool same=depth.width==fresh_depth.width && depth.height==fresh_depth.height;
            const bool motion_ok=kept_motion_read && kept_motion.width==depth.width && kept_motion.height==depth.height && same;
            const bool rig_ok=kept_rig_read && kept_rig.width==depth.width && kept_rig.height==depth.height && same;
            if(!afw2) afw2=std::make_unique<Afw2Pass>(device_.Get());
            ++parallax_passes;++fresh_passes;if(motion_ok) ++motion_passes;if(rig_ok) ++rig_passes;
            // v1190 (engine side): the rig target at t15 (compute and pixel stages) for the pass; unbound after it
            Ptr<ID3D11ShaderResourceView> target_view;
            if(layers->rig_target && rig_target.texture && rig_target.width==depth.width && rig_target.height==depth.height) {
                D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=DXGI_FORMAT_R32G32_FLOAT;v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
                if(FAILED(device_->CreateShaderResourceView(rig_target.texture,&v,&target_view))) target_view.Reset();
            }
            if(target_view) {ID3D11ShaderResourceView* t=target_view.Get();context_->CSSetShaderResources(15,1,&t);context_->PSSetShaderResources(15,1,&t);++rig_target_passes;}
            ID3D11Texture2D* afw2_out=afw2->run(*layers,width_,height_,read,depth_read,depth.width,depth.height,fresh_read,fresh_depth_read,fresh_depth.width,fresh_depth.height,
                             motion_ok?kept_motion_read:nullptr,motion_ok?fresh_motion_read:nullptr,rig_ok?kept_rig_read:nullptr,rig_ok?fresh_rig_read:nullptr,
                             target_view!=nullptr);  // v1190: the rig target bound at t15
            if(target_view) {ID3D11ShaderResourceView* none{};context_->CSSetShaderResources(15,1,&none);context_->PSSetShaderResources(15,1,&none);}
            return afw2_out;
        }
        std::array<float,192> parameters{};std::size_t at=0;
        const auto put=[&](double x,double y,double z,double w) {
            parameters[at++]=static_cast<float>(x);parameters[at++]=static_cast<float>(y);parameters[at++]=static_cast<float>(z);parameters[at++]=static_cast<float>(w);
        };
        const auto rows=[&](const WarpMatrix& m) {put(m[0],m[1],m[2],0);put(m[3],m[4],m[5],0);put(m[6],m[7],m[8],0);};
        const auto layer=[&](const StaleParallax* p) {if(p) {rows(p->world);put(p->shift[0],p->shift[1],p->shift[2],1);} else {rows(WarpMatrix{});put(0,0,0,0);}};
        rows(h);put(width_,height_,1./width_,1./height_);
        layer(depth_read?&layers->world:nullptr);layer(fresh_read?&*layers->fresh:nullptr);
        put(depth_read?layers->world.m10:0,depth_read?layers->world.m11:0,depth_read?layers->near_range:0,depth_read?1:0);
        put(fresh_read?layers->fresh->m10:0,fresh_read?layers->fresh->m11:0,0,fresh_read && layers->near_from_kept?1:0);
        put(depth.width,depth.height,fresh_depth.width,fresh_depth.height);
        // the motion layers: w carries where their search starts (0: off)
        const auto optional_layer=[&](const std::optional<StaleParallax>& p,double start) {
            if(depth_read && fresh_read && p) {rows(p->world);put(p->shift[0],p->shift[1],p->shift[2],start);} else {rows(WarpMatrix{});put(0,0,0,0);}
        };
        optional_layer(layers?layers->carried:std::nullopt,layers?layers->start[0]:0);
        optional_layer(layers?layers->hand[0]:std::nullopt,layers?layers->start[1]:0);optional_layer(layers?layers->hand[1]:std::nullopt,layers?layers->start[2]:0);
        // v811: the kept rays, the palms (ownership, with both depths: without the fresh eye nothing competes), each hand layer from the
        // kept side (the fit), the fit grid
        const bool owned=depth_read && fresh_read;
        if(owned) put(std::tan(layers->kept_fov.left),std::tan(layers->kept_fov.right),std::tan(layers->kept_fov.up),std::tan(layers->kept_fov.down));
        else put(0,0,0,0);
        for(unsigned side=0;side<2;++side) {
            const auto& p=layers?layers->palms[side]:std::array<double,4>{};
            if(owned) put(p[0],p[1],p[2],p[3]); else put(0,0,0,0);
        }
        bool fit_on=false;
        for(unsigned side=0;side<2;++side) {
            const auto& p=layers?layers->hand[side]:std::nullopt;
            if(owned && p && layers->palms[side][3]>0) {const auto& b=p->back;put(b[0],b[1],b[2],0);put(b[3],b[4],b[5],0);put(b[6],b[7],b[8],0);
                                                        put(p->back_shift[0],p->back_shift[1],p->back_shift[2],0);fit_on=true;}
            else {rows(WarpMatrix{});put(0,0,0,0);}
        }
        put(fit_grid,fit_grid,4,0);
        // v1150: the motion textures' rectangles, the two parts, on, mover_px; the kept camera's axes
        if(kept_motion_read) {
            put(kept_motion.width,kept_motion.height,fresh_motion.width,fresh_motion.height);
            put(layers->fresh_part,layers->kept_part,1,mover_px);rows(layers->kept_axes);++motion_passes;
        } else {put(0,0,0,0);put(0,0,0,0);rows(WarpMatrix{});}
        // v1159: the rig masks' rectangles, on, a hand's reach over the rig
        if(kept_rig_read) {put(kept_rig.width,kept_rig.height,fresh_rig.width,fresh_rig.height);put(1,hand_rig_radius,0,0);++rig_passes;}
        else {put(0,0,0,0);put(0,0,0,0);}
        if(layers) {const double u=1-std::clamp(layers->border[0],0.,.25),v=1-std::clamp(layers->border[1],0.,.25);put(u,v,u,v);} else put(1,1,1,1); // v1167
        const bool cut=fresh_read && layers->cut; // v1171: the fresh eye alone (needs it)
        const bool fresh_hands=kept_rig_read && !cut && layers->rig_from_fresh; // v1173
        // v1175: w: near scenery from the fresh eye (masks live, the camera moving)
        // v1176: the camera only. Replay of real frames with a still camera and a moving hand (v1165 door 2 and 5): the whole near field
        // from the fresh eye was 2.6 and 2.1 levels off the eye's next frame against 1.8 and 1.7 from its own kept frame, on scenery that
        // changes 1.1-1.3 levels between its own frames: a flicker over all near objects whenever a hand moves. The moving hand itself and
        // its old place are handled by the hand rules above.
        const bool near_fresh=fresh_hands && layers->camera_moving;
        put(cut?1:0,fresh_hands && layers->hand_moving[0]?1:0,fresh_hands && layers->hand_moving[1]?1:0,near_fresh?1:0);
        if(cut) {fit_on=false;++cut_passes;}
        if(depth_read) ++parallax_passes;
        if(fresh_read) ++fresh_passes;
        context_->UpdateSubresource(parameters_.Get(),0,nullptr,parameters.data(),0,0);
        auto constants=parameters_.Get();
        // v811: each hand's correction (none unless the fit runs and is sure)
        const UINT zeros[4]{};context_->ClearUnorderedAccessViewUint(fit_result_uav_.Get(),zeros);
        if(fit_on) {
            ID3D11ShaderResourceView* fit_inputs[]{read,depth_read,fresh_read,fresh_depth_read,nullptr,nullptr,nullptr,kept_rig_read,fresh_rig_read}; // v1159: Owner, FitMatch
            context_->CSSetShaderResources(0,9,fit_inputs);
            ID3D11UnorderedAccessView* fit_outputs[]{fit_samples_uav_.Get(),fit_counts_uav_.Get(),fit_result_uav_.Get()};
            context_->CSSetUnorderedAccessViews(0,3,fit_outputs,nullptr);context_->CSSetConstantBuffers(0,1,&constants);
            context_->CSSetShader(fit_[0].Get(),nullptr,0);context_->Dispatch(fit_grid/8,fit_grid/8,2);
            context_->CSSetShader(fit_[1].Get(),nullptr,0);context_->Dispatch(169,2,1);
            context_->CSSetShader(fit_[2].Get(),nullptr,0);context_->Dispatch(49,2,1);
            context_->CSSetShader(fit_[3].Get(),nullptr,0);context_->Dispatch(1,1,1);
            ID3D11UnorderedAccessView* no_outputs[3]{};context_->CSSetUnorderedAccessViews(0,3,no_outputs,nullptr);
            ID3D11ShaderResourceView* no_inputs[9]{};context_->CSSetShaderResources(0,9,no_inputs);context_->CSSetShader(nullptr,nullptr,0);
            ++fit_passes;
        }
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
        context_->RSSetViewports(1,&viewport);context_->RSSetState(raster_.Get());
        context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->OMSetDepthStencilState(depth_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context_->PSSetConstantBuffers(0,1,&constants);
        ID3D11SamplerState* samplers[]{sampler_.Get()};context_->PSSetSamplers(0,1,samplers);
        ID3D11ShaderResourceView* inputs[]{read,depth_read,fresh_read,fresh_depth_read,fit_result_srv_.Get(),kept_motion_read,fresh_motion_read,kept_rig_read,fresh_rig_read};
        context_->PSSetShaderResources(0,9,inputs);
        auto* target=output_rtv_.Get();context_->OMSetRenderTargets(1,&target,nullptr);
        context_->VSSetShader(vertex_.Get(),nullptr,0);context_->PSSetShader(pixel_.Get(),nullptr,0);
        context_->Draw(3,0);
        ID3D11ShaderResourceView* empty[9]{};context_->PSSetShaderResources(0,9,empty);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
        if(fit_on && layers) { // v811 trace: this fit, read back once the GPU is done with it
            if(fit_waiting_[fit_slot_]) collect_fits();
            if(!fit_waiting_[fit_slot_]) {
                context_->CopyResource(fit_staging_[fit_slot_].Get(),fit_result_.Get());
                fit_tag_[fit_slot_]={layers->frame,layers->eye};fit_waiting_[fit_slot_]=true;fit_slot_=(fit_slot_+1)%fit_staging_.size();
            }
        }
        collect_fits();
        return output_.Get();
    }
};
} // namespace d2vr
