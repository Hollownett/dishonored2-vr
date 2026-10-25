#pragma once
// G2/G3 VR cutscene views (camera plan Task 6). An immersive exact-Cinematic32 frame renders from the scene camera (the player camera in
// camera mode 10, g2-spike.md) in one of two styles:
// - VR comfort: authored_head_view at the scene camera's position, AuthoredHeading for the heading and ScriptedRecenter for the
//   alignment. On a hard camera cut (detect_cut) the heading takes the new shot and the re-aim is instant (ScriptedRecenter instant);
//   a 30 deg drift inside a continuous shot re-aims with the 0.4 s ease from the last view (ScriptedRecenterParams::continuous_reaim). The comfort
//   vignette follows the scene camera's translation speed above the locomotion vignette's own onset (cutscene_vignette_speed).
// - VR full: cutscene_full_view, the scene camera's full rotation with the head on top. Cuts show exactly as directed (no re-aim).
// A cutscene view lasts until the authored camera ends: the camera-mode-10 frames the game still shows at logic 2 after the cinematic
// (about 185 ms in void_01) keep the cutscene's view (authored_tail), so VR full never drops the scene's pitch and roll in one frame.
// Both start aligned through ScriptedRecenter's entry (eased from the last gameplay view when the scene follows gameplay directly,
// instant after a load or menu, or on a cut) and fold into the head origin at exit, so the direction is kept.
// CutsceneViewTracker is the once-per-displayed-frame decision both eyes share. Pure; no allocation.
#include "authored_view.hpp"
#include "comfort_vignette.hpp"
#include "cutscene_mode.hpp"
#include "game_camera.hpp"
#include "tracked_pose.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace d2vr {
inline constexpr double cut_jump_m=.5;                            // a cut moves the camera more than this in one frame
inline constexpr double cut_yaw_rad=3.14159265358979323846/6;      // or turns it at least 30 degrees
// Comfort vignette onset for a moving scene camera: the locomotion vignette's own onset (the stick dead zone 0.15 x the 3 m/s
// full-strength speed); above it the strength rises with speed exactly as for stick locomotion (ComfortVignette::full_move_mps).
inline constexpr double cutscene_vignette_onset_mps=.45; // = 0.15 x ComfortVignette::full_move_mps

struct CameraCut {bool cut;double jump_m,jump_yaw_rad;};

// prev -> now in one displayed frame. Positions in game units (units_per_metre); yaw is AuthoredHeading::native_yaw (a vertical forward
// uses the left axis). An unusable pose is no cut (nothing to compare).
inline CameraCut detect_cut(const GameCameraPose& prev,const GameCameraPose& now,double units_per_metre) {
    if(!std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000) return {false,0,0};
    double d2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(prev.position[i]) || !std::isfinite(now.position[i])) return {false,0,0};
        const double d=static_cast<double>(now.position[i])-prev.position[i];d2+=d*d;
    }
    const double jump_m=std::sqrt(d2)/units_per_metre;
    double jump_yaw=0;
    const auto a=AuthoredHeading::native_yaw(prev),b=AuthoredHeading::native_yaw(now);
    if(a && b) jump_yaw=std::fabs(std::remainder(*b-*a,2*3.14159265358979323846));
    return {jump_m>cut_jump_m || jump_yaw>=cut_yaw_rad,jump_m,jump_yaw};
}

// VR full: the scene camera's full rotation (pitch and roll too) with the head's rotation relative to the yaw-only `origin` on top, at
// the scene position plus the eye's offset from the head centre (and the bounded physical offset, as authored_head_view takes it), turned
// into the scene frame. Identity head = the scene camera exactly. Throws std::invalid_argument on unusable input (the caller then draws no
// head view, exactly as for authored_head_view).
inline GameCameraPose cutscene_full_view(const GameCameraPose& scene,const TrackedPose& centre,const TrackedPose& eye,
    const Quaternion& origin,double units_per_metre,const Vec3& physical_offset={}) {
    if(!finite(centre.position) || !finite(eye.position) || !std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000)
        throw std::invalid_argument("Invalid cutscene full view input");
    const Vec3 offset=eye.position-centre.position;
    if(length(offset)>.15) throw std::invalid_argument("Eye offset is stale or mismatched");
    if(!finite(physical_offset) || std::hypot(physical_offset.x,physical_offset.z)>.450001 ||
       physical_offset.y<-.700001 || physical_offset.y>1.200001)
        throw std::invalid_argument("Physical head offset is outside the gameplay volume");
    std::array<Vec3,3> frame{};
    for(unsigned i=0;i<3;++i) {
        frame[i]={scene.basis[i*3],scene.basis[i*3+1],scene.basis[i*3+2]};
        if(!finite(frame[i]) || !std::isfinite(scene.position[i]) || std::abs(dot(frame[i],frame[i])-1)>.02)
            throw std::invalid_argument("Invalid scene camera frame");
    }
    if(std::abs(dot(frame[0],frame[1]))>.02 || std::abs(dot(frame[0],frame[2]))>.02 || std::abs(dot(frame[1],frame[2]))>.02 ||
       dot(cross(frame[0],frame[1]),frame[2])<.98)
        throw std::invalid_argument("Scene camera frame must be orthonormal and right handed");
    auto reference=normalized(origin);
    if(std::abs(reference.x)>1e-6 || std::abs(reference.z)>1e-6) throw std::invalid_argument("Recenter orientation must contain yaw only");
    reference={-reference.x,-reference.y,-reference.z,reference.w};
    const auto relative=normalized(multiplied(reference,normalized(eye.orientation)));
    const auto convert=[&](Vec3 xr) {return frame[0]*(-xr.z)+frame[1]*(-xr.x)+frame[2]*xr.y;}; // XR (x right, y up, -z forward) -> scene
    const Vec3 base{scene.position[0],scene.position[1],scene.position[2]};
    const Vec3 position=base+convert(rotated(reference,physical_offset+offset))*units_per_metre;
    const std::array<Vec3,3> axes{convert(rotated(relative,{0,0,-1})),convert(rotated(relative,{-1,0,0})),convert(rotated(relative,{0,1,0}))};
    GameCameraPose out=scene;
    for(unsigned i=0;i<3;++i) {
        out.position[i]=static_cast<float>(i==0?position.x:i==1?position.y:position.z);
        out.basis[i*3]=static_cast<float>(axes[i].x);out.basis[i*3+1]=static_cast<float>(axes[i].y);out.basis[i*3+2]=static_cast<float>(axes[i].z);
    }
    for(float f:out.position) if(!std::isfinite(f)) throw std::invalid_argument("Cutscene full view overflow");
    return out;
}

// The comfort vignette's move speed for a cutscene: the scene camera's speed, 0 at or below the onset.
inline double cutscene_vignette_speed(double scene_speed_mps) noexcept {
    return std::isfinite(scene_speed_mps) && scene_speed_mps>cutscene_vignette_onset_mps?scene_speed_mps:0;
}

// The scene camera's translation speed, low-passed (tau 0.1 s) so an engine that updates the camera on alternate frames reads steady.
// A cut is a discontinuity, not motion: it rebases without a sample. A gap (> 0.25 s, a pause or stall) starts afresh at 0.
class SceneMotion {
    bool have_{};double t_{},speed_{};std::array<float,3> p_{};
public:
    static constexpr double tau_s=.1,max_dt=.25;
    double update(const std::array<float,3>& position,double t,bool cut,double units_per_metre=1) noexcept {
        for(float f:position) if(!std::isfinite(f)) {have_=false;speed_=0;return speed_;}
        if(!std::isfinite(t) || !std::isfinite(units_per_metre) || units_per_metre<=0) {have_=false;speed_=0;return speed_;}
        const double dt=t-t_;
        if(have_ && !(dt>0)) return speed_; // the same instant again: nothing new
        if(!have_ || dt>max_dt) {have_=true;speed_=0;t_=t;p_=position;return speed_;} // start afresh
        if(!cut) {
            double d2=0;for(int i=0;i<3;++i) {const double d=static_cast<double>(position[i])-p_[i];d2+=d*d;}
            const double raw=std::sqrt(d2)/units_per_metre/dt;
            speed_+=(raw-speed_)*(1-std::exp(-dt/tau_s));
        }
        t_=t;p_=position;return speed_;
    }
    void reset() noexcept {have_=false;speed_=0;}
    double speed() const noexcept {return speed_;}
};

enum class CutsceneViewKind {none,comfort,full};
inline const char* to_string(CutsceneViewKind kind) noexcept {return kind==CutsceneViewKind::comfort?"vr_comfort":kind==CutsceneViewKind::full?"vr_full":"none";}

struct CutsceneViewStep {
    CutsceneViewKind kind; // the view this displayed frame renders with (none = not an immersive cutscene view)
    bool entry;            // the first cutscene frame (the mode is latched here for the whole cutscene)
    bool instant;          // align at once: a cut, or an entry that does not follow a rendered frame directly (a load, a menu)
    CameraCut cut;         // this frame against the previous displayed frame (cut.cut only between consecutive frames of one owner)
};

// Fed once per displayed frame with the scene pose (the render view's centre), on every scene frame, gameplay included, so the first
// cutscene frame is compared with the last gameplay frame. `cutscene` = an immersive cutscene frame drawn with the authored camera;
// `authored_tail` = a gameplay frame still drawn with the authored camera, which continues a cutscene view already in progress (never
// starts one).
class CutsceneViewTracker {
    bool have_{},in_{};
    std::uint64_t frame_{},epoch_{},cuts_{},entries_{};
    CutsceneViewKind kind_{CutsceneViewKind::none};
    GameCameraPose prev_{};
    CameraCut last_cut_{false,0,0};
public:
    static constexpr std::uint64_t max_frame_gap=2; // Presents between two views that still count as consecutive
    CutsceneViewStep step(bool cutscene,CutsceneMode mode,std::uint64_t frame,std::uint64_t epoch,const GameCameraPose& scene,
        bool authored_tail=false,double units_per_metre=1) noexcept {
        const bool consecutive=have_ && epoch==epoch_ && frame>frame_ && frame-frame_<=max_frame_gap;
        CutsceneViewStep out{CutsceneViewKind::none,false,false,{false,0,0}}; // outside a cutscene nothing is compared (gameplay is untouched)
        if(cutscene || (in_ && authored_tail && epoch==epoch_)) {
            const CameraCut cut=consecutive?detect_cut(prev_,scene,units_per_metre):CameraCut{false,0,0};
            out.cut=cut;
            out.entry=!in_;
            if(!in_) {in_=true;++entries_;kind_=mode==CutsceneMode::vr_full?CutsceneViewKind::full:CutsceneViewKind::comfort;} // theater never opens the gates
            out.kind=kind_;
            out.instant=cut.cut || (out.entry && !consecutive);
            if(cut.cut) {++cuts_;last_cut_=cut;}
        } else {in_=false;kind_=CutsceneViewKind::none;}
        have_=true;frame_=frame;epoch_=epoch;prev_=scene;
        return out;
    }
    void reset() noexcept {*this=CutsceneViewTracker{};}
    std::uint64_t cuts() const noexcept {return cuts_;}
    std::uint64_t entries() const noexcept {return entries_;}
    CameraCut last_cut() const noexcept {return last_cut_;}
    CutsceneViewKind kind() const noexcept {return kind_;}
};
} // namespace d2vr
