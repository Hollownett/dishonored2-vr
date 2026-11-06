#pragma once
#include "native_wrist_hud.hpp"
#include "tracked_pose.hpp"
#include "grab_beam.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2vr {
// face_sign: the grip X direction (+1/-1) that must point toward the eyes; 0 = no wrist-side requirement. Grip +X points
// out of the back of the right hand and into the palm of the left (WebXR/OpenXR grip convention), so the inner (palm)
// side is +1 for the left hand and -1 for the right.
inline std::optional<TrackedPose> wrist_hud_pose(const TrackedPose& hand,const TrackedPose& head,int face_sign=0) {
    if(!finite(hand.position) || !finite(head.position)) return {};
    if(face_sign) {
        const auto dorsal=rotated(normalized(hand.orientation),{static_cast<double>(face_sign),0,0});
        const auto to_head=head.position-hand.position;const auto d=length(to_head);
        if(d<1e-3 || dot(dorsal,to_head*(1/d))<.3) return {}; // back of the hand roughly toward the eyes
    }
    // Watch face on the forearm: grip -Z points toward the fingers, so +Z runs
    // up the forearm. Sit behind the wrist, lifted toward the viewer, and spin
    // about the forearm so the face looks at the head; no wrist-twist gate.
    const auto back=rotated(normalized(hand.orientation),{0,0,1});
    // v232: .14 up the forearm (clear of the hand/controller model) and .07 off the axis (clear of the sleeve/forearm mesh).
    const auto centre=hand.position+back*.14;
    const auto toward=head.position-centre;const auto distance=length(toward);
    if(distance<.15 || distance>.80 || hand.position.y<head.position.y-.75) return {};
    const auto across=toward-back*dot(toward,back);const auto across_length=length(across);
    if(across_length<.35*distance) return {}; // looking straight down the forearm
    const auto gaze=rotated(normalized(head.orientation),{0,0,-1});
    if(dot(gaze,toward*(-1/distance))<.8) return {}; // only when glanced at (~37 degrees)
    const auto normal=across*(1/across_length);
    auto up=rotated(normalized(head.orientation),{0,1,0});up=up-normal*dot(up,normal);
    if(length(up)<.2) up=back-normal*dot(back,normal);
    up=unit(up);
    return TrackedPose{centre+normal*.07,basis_quaternion(cross(up,normal),up,normal)};
}
// ---------------------------------------------------------------------------------------------------------------
// The left-wrist watch (v1013 placement and visibility, v1015 content). The rival mod anchors a square crop of the
// game's own health/mana/equipped-item cluster to the left controller: face out of the back of the hand, 5 cm out of
// it and 8 cm back along the forearm, 8.5 cm wide, shown only while the face and the head's gaze point at it. These
// are physical offsets, derived here from the same OpenXR grip pose the hands are built from; no headset-specific
// position is used.
//
// OpenXR/WebXR grip convention used by this project: +X points into the left palm (and out of the back of the right
// hand), +Z runs from the fingers back up the forearm. So the left hand's dorsal normal is -X.
inline constexpr double wrist_watch_size_m=.085;    // 8.5 cm default; the build/wrist-hud-size knob is 4..15 cm
inline constexpr double wrist_watch_back_m=.05;     // out of the back of the hand
inline constexpr double wrist_watch_along_m=.08;    // back from the controller along the forearm
inline constexpr double wrist_watch_side_m=0.;      // towards the thumb
inline constexpr double wrist_watch_on_deg=48.,wrist_watch_off_deg=65.;
inline constexpr double wrist_watch_gaze_on_deg=40.,wrist_watch_gaze_off_deg=55.;
inline constexpr double wrist_watch_max_distance_m=.85,wrist_watch_keep_margin_m=.10;
inline constexpr double wrist_watch_fade_in_s=.12,wrist_watch_fade_out_s=.22;
inline constexpr double wrist_watch_size_min_cm=4.,wrist_watch_size_max_cm=15.,wrist_watch_size_default_cm=8.5;

struct WristWatchAngles {
    double facing_deg{}; // between the face normal and the eye
    double gaze_deg{};   // between the head's forward ray and the face
    double distance_m{};
};
struct WristWatchPose {
    TrackedPose pose;
    WristWatchAngles angles;
};
// Face on the back of the left hand, upright to the head. Pure geometry: returns the pose and the two visibility
// angles even when the watch would be hidden, so tests and the state machine see the same numbers the rival uses.
inline std::optional<WristWatchPose> wrist_watch_pose(const TrackedPose& left_grip,const TrackedPose& head) {
    if(!finite(left_grip.position) || !finite(head.position)) return {};
    try {
        const auto grip=normalized(left_grip.orientation);
        const auto head_q=normalized(head.orientation);
        auto normal=rotated(grip,{-1,0,0});
        const double normal_length=length(normal);
        if(!std::isfinite(normal_length) || normal_length<.5) return {};
        normal=normal*(1/normal_length);
        const auto centre=left_grip.position+rotated(grip,{-wrist_watch_back_m,wrist_watch_side_m,wrist_watch_along_m});
        const auto to_eye=head.position-centre;const double distance=length(to_eye);
        if(!std::isfinite(distance) || distance<1e-3) return {};
        const auto toward_eye=to_eye*(1/distance);
        const auto gaze=rotated(head_q,{0,0,-1});
        constexpr double pi=3.14159265358979323846;
        const double facing=std::acos(std::clamp(dot(normal,toward_eye),-1.,1.))*180./pi;
        const double gaze_deg=std::acos(std::clamp(dot(gaze,toward_eye*-1.),-1.,1.))*180./pi;
        // "Upright": the head's up projected on the face. When the head looks along the face plane, fall back to the
        // hand's own up and then to the forearm, so the quaternion is always finite; the rival keeps its last roll here.
        auto up=rotated(head_q,{0,1,0});
        up=up-normal*dot(up,normal);
        if(length(up)<.2) {up=rotated(grip,{0,1,0});up=up-normal*dot(up,normal);}
        if(length(up)<.2) {up=rotated(grip,{0,0,1});up=up-normal*dot(up,normal);}
        const double up_length=length(up);
        if(!std::isfinite(up_length) || up_length<1e-3) return {};
        up=up*(1/up_length);
        auto right=cross(up,normal);const double right_length=length(right);
        if(!std::isfinite(right_length) || right_length<1e-3) return {};
        right=right*(1/right_length);
        return WristWatchPose{{centre,basis_quaternion(right,up,normal)},{facing,gaze_deg,distance}};
    } catch(...) {return {};}
}
inline bool wrist_watch_want_on(const WristWatchAngles& a) noexcept {
    return std::isfinite(a.facing_deg) && std::isfinite(a.gaze_deg) && std::isfinite(a.distance_m) &&
        a.facing_deg<wrist_watch_on_deg && a.gaze_deg<wrist_watch_gaze_on_deg && a.distance_m<wrist_watch_max_distance_m;
}
inline bool wrist_watch_keep(const WristWatchAngles& a) noexcept {
    return std::isfinite(a.facing_deg) && std::isfinite(a.gaze_deg) && std::isfinite(a.distance_m) &&
        a.facing_deg<wrist_watch_off_deg && a.gaze_deg<wrist_watch_gaze_off_deg &&
        a.distance_m<wrist_watch_max_distance_m+wrist_watch_keep_margin_m;
}
// Visibility state: 48/65 degrees face, 40/55 degrees gaze, 0.85 m (+0.10 to keep), fades 0.12 s in and 0.22 s out.
// Trigger and menus hide it; a tick is reported once per appearance (the caller decides whether haptics are on).
class WristWatchState {
public:
    struct Frame {bool shown{},visible{},haptic{};double alpha{};};
    Frame update(const WristWatchAngles& angles,bool tracked,bool trigger,bool menu,double dt) noexcept {
        if(!tracked || menu) {shown_=false;alpha_=0;return {};}
        bool haptic=false;
        if(trigger) shown_=false;
        else if(!shown_ && wrist_watch_want_on(angles)) {shown_=true;haptic=true;}
        else if(shown_ && !wrist_watch_keep(angles)) shown_=false;
        if(!std::isfinite(dt) || dt<0) dt=0;
        if(shown_) alpha_=std::min(1.,alpha_+(wrist_watch_fade_in_s>0?dt/wrist_watch_fade_in_s:1.));
        else alpha_=std::max(0.,alpha_-(wrist_watch_fade_out_s>0?dt/wrist_watch_fade_out_s:1.));
        return {shown_,alpha_>0.,haptic,alpha_};
    }
    void reset() noexcept {shown_=false;alpha_=0;}
    bool shown() const noexcept {return shown_;}
    double alpha() const noexcept {return alpha_;}
private:
    bool shown_{};double alpha_{};
};
// ---------------------------------------------------------------------------------------------------------------
// The game's MasterHUD cluster inside the captured HUD surface (v1015). These are the rival mod's measured constants
// (WristHud.cpp ClusterCentre, measured at 1902x973, 1280x720, 2560x1080, 1896x1968 and 1440x1440): the interface is
// laid out in a centred 16:9 frame of height hs = min(H, W*9/16). The ring centre is 0.1465*hs right of that frame's
// left edge and 0.133*hs + 0.0327*(H - hs) down from the top of the surface. The crop side is 0.33*hs. The constants
// are numbers/ideas only; no rival code is copied.
inline constexpr double wrist_watch_cluster_centre_x=.1465;
inline constexpr double wrist_watch_cluster_centre_y=.133;
inline constexpr double wrist_watch_cluster_letterbox=.0327;
inline constexpr double wrist_watch_cluster_side_ratio=.33;

struct WristWatchClusterRect {
    double left{},top{},right{},bottom{},side{},frame_height{};
    bool valid() const noexcept {return side>0 && right>left && bottom>top;}
};
// The captured HUD surface is the game's per-eye interface target (for example 2316x2476 in the v1014 session). The
// formula is applied in that surface's top-left pixel space, exactly as the rival applies it to its mirrored texture.
inline WristWatchClusterRect wrist_watch_cluster_rect(unsigned surface_width,unsigned surface_height) noexcept {
    if(!surface_width || !surface_height) return {};
    const double w=static_cast<double>(surface_width),h=static_cast<double>(surface_height);
    const double hs=std::min(h,w*9./16.);
    if(!(hs>0)) return {};
    const double frame_left=(w-hs*16./9.)*.5;
    const double cx=frame_left+wrist_watch_cluster_centre_x*hs;
    const double cy=wrist_watch_cluster_centre_y*hs+wrist_watch_cluster_letterbox*(h-hs);
    const double side=wrist_watch_cluster_side_ratio*hs;
    if(!(side>1)) return {};
    WristWatchClusterRect out;
    out.left=cx-side*.5;out.top=cy-side*.5;out.right=cx+side*.5;out.bottom=cy+side*.5;
    out.side=side;out.frame_height=hs;return out;
}
// The cluster texture's integer pixel extent: its origin is the floor of the floating crop, so no visible edge is lost.
inline std::pair<unsigned,unsigned> wrist_watch_cluster_texture_size(const WristWatchClusterRect& rect) noexcept {
    if(!rect.valid()) return {0,0};
    const int left=static_cast<int>(std::floor(rect.left)),top=static_cast<int>(std::floor(rect.top));
    const int right=static_cast<int>(std::ceil(rect.right)),bottom=static_cast<int>(std::ceil(rect.bottom));
    if(right<=left || bottom<=top) return {0,0};
    return {static_cast<unsigned>(right-left),static_cast<unsigned>(bottom-top)};
}
// One captured child crop (a compact native art texture) placed at its ORIGINAL position inside the cluster. source
// is relative to the child's compact crop; destination is relative to the cluster texture. No mod placement is used.
struct WristWatchClusterPlacement {
    bool valid{};
    int source_left{},source_top{},source_right{},source_bottom{};
    int destination_left{},destination_top{},destination_right{},destination_bottom{};
};
inline WristWatchClusterPlacement wrist_watch_cluster_place(const WristWatchClusterRect& rect,
    int crop_left,int crop_top,int crop_right,int crop_bottom,unsigned cluster_width,unsigned cluster_height) noexcept {
    if(!rect.valid() || !cluster_width || !cluster_height || crop_left>=crop_right || crop_top>=crop_bottom) return {};
    const int cluster_left=static_cast<int>(std::floor(rect.left));
    const int cluster_top=static_cast<int>(std::floor(rect.top));
    const int left=std::max(crop_left,cluster_left);
    const int top=std::max(crop_top,cluster_top);
    const int right=std::min(crop_right,cluster_left+static_cast<int>(cluster_width));
    const int bottom=std::min(crop_bottom,cluster_top+static_cast<int>(cluster_height));
    if(left>=right || top>=bottom) return {};
    const int destination_left=left-cluster_left,destination_top=top-cluster_top;
    if(destination_left<0 || destination_top<0 ||
       static_cast<unsigned>(destination_left)>=cluster_width || static_cast<unsigned>(destination_top)>=cluster_height) return {};
    WristWatchClusterPlacement out;
    out.valid=true;
    out.source_left=left-crop_left;out.source_top=top-crop_top;
    out.source_right=right-crop_left;out.source_bottom=bottom-crop_top;
    out.destination_left=destination_left;out.destination_top=destination_top;
    out.destination_right=std::min(destination_left+(right-left),static_cast<int>(cluster_width));
    out.destination_bottom=std::min(destination_top+(bottom-top),static_cast<int>(cluster_height));
    return out;
}
} // namespace d2vr
