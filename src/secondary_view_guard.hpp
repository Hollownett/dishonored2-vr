#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// v1246 (Timepiece in stereo). The engine renders at most one SECONDARY view per frame: the game thread names it with the renderer's
// SetSecondaryView (vfunc +0x1b8 = 0x366970: scene +0x88, view +0x90, rect +0x98, plane +0xb0), the render thread draws it first in the
// next frame with vfunc +0x1c8 = 0x366590 (copies the main entry's FOV/flags into it, clears the forced-projection flag +0x140, Setup
// through 0x374b60, Begin from 0x3668d1). The Timepiece's caller (0x44d230) takes that view as GetView(GetViewCount()-1) (0x44d38d) and
// copies its own camera (the other timeline) into it (0x444c0). In the side-by-side stereo mode that object IS the eye-1 view: its second
// Setup at the start of the frame (v1245 trace: caller 0x3668a6, same view address, lens Begin first) re-published eye 1 one frame early
// and left the eye-1 view holding the lens's def and render copy. Engine copy facts from 0x35dc60/0x374e40: an eye's def is 0x444c0
// (view, prepared+0x30), then 0x374b60(view, prepared, &1.0f pair at 0x22fd9f0, 0); Setup rolls the history rows below only when the
// frame stamp +0x77b4 changes, the jitter roll (+0x7858 = +0x7850) is unconditional.
namespace d2vr::secondary_view {
inline constexpr std::uintptr_t render_rva=0x366590,set_rva=0x366970,def_copy_rva=0x444c0,setup_wrapper_rva=0x374b60,eye_fraction_rva=0x22fd9f0;
inline constexpr std::array<unsigned char,16> render_prologue{0x4c,0x8b,0xdc,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x78,0x48,0x83,0xb9,0x88,0x00};
inline constexpr std::array<unsigned char,16> set_prologue{0x48,0x89,0x91,0x88,0x00,0x00,0x00,0x4c,0x89,0x81,0x90,0x00,0x00,0x00,0x41,0x8b};
inline constexpr std::size_t window_offset=0x9c,window_size=0xa8; // the head write: fov, position, basis, projection +0x100, forced flag +0x140
// The view rows an eye's own Setup leaves that the secondary pass must not change for that eye's render (offset, bytes).
struct Span {std::size_t offset,size;};
inline constexpr std::array<Span,6> history_spans{{{0x7410,0x40},{0x74d0,0x40},{0x7590,0x40},{0x3a84,0x0c},{0x7850,0x10},{0x77b4,4}}};
inline constexpr std::size_t history_bytes=0x40*3+0x0c+0x10+4;
inline constexpr std::array<Span,2> verify_spans{{{0x7510,0x40},{0x39d0+0xa8,0x0c}}}; // the eye's VP and render-copy position after a replay

// The secondary camera follows the player camera (Timepiece: same orientation, a fixed translation to the other timeline) when its basis
// equals the engine camera's; then the lens is drawn from the eye-0 HMD camera moved by the same translation. Otherwise it is an independent
// camera and keeps its own pose and the engine projection.
inline bool follows(const std::array<float,9>& secondary,const std::array<float,9>& engine) noexcept {
    for(std::size_t i=0;i<9;++i) if(!std::isfinite(secondary[i]) || !std::isfinite(engine[i]) || std::abs(secondary[i]-engine[i])>.02f) return false;
    return true;
}
inline std::array<float,3> translation(const std::array<float,3>& secondary,const std::array<float,3>& engine_a,const std::array<float,3>& engine_b) noexcept {
    std::array<float,3> t{};for(std::size_t i=0;i<3;++i) t[i]=secondary[i]-.5f*(engine_a[i]+engine_b[i]);return t;
}
// v1256 (user: "in the Time device display the image is rendered very poorly and the image for each eye is mixed"): the lens pass is ONE mono
// image shown to both eyes. Drawn from the eye-0 pose its frustum sits half an interpupillary distance to the left, so each eye composites a
// different crop of it - the "mixed" image. The camera is the head's centre: the midpoint of the two eyes' poses (a missing or non-finite eye
// pose keeps the other one).
inline std::array<float,3> centre(const std::array<float,3>& a,const std::array<float,3>& b) noexcept {
    for(std::size_t i=0;i<3;++i) if(!std::isfinite(a[i]) || !std::isfinite(b[i])) return a;
    std::array<float,3> c{};for(std::size_t i=0;i<3;++i) c[i]=.5f*(a[i]+b[i]);return c;
}

// v1257 (user, v1256 headset: "has depth but is misaligned between the eyes"): the glass (rendering/timetravelportal) samples the secondary
// image by SCREEN position (no projective lookup), so the image an eye composites must be rendered with that eye's own head window (pose,
// FOV, forced projection) moved by the secondary camera's translation; one mono image cannot be right for both eyes. The engine runs the
// secondary pass once, at the frame start (0x361fc2), and the eyes render serially (eye 0's lists are executed before eye 1's Begin: the
// paired eye capture copies eye 0's finished image there). So the frame-start pass is drawn for eye 0 and the pass is run again inside eye
// 1's Begin, drawn for eye 1: the portal target then holds each eye's own image when that eye's glass samples it.
// build/lens-mono: one pass from the head centre (v1256); build/lens-eye0-camera: one pass from eye 0 (v1246).
enum class LensMode : unsigned {per_eye,centre,eye0};
// The record slot whose head window draws a pass: per_eye uses the pass's own eye, the one-pass modes eye 0's window.
inline unsigned pass_record(LensMode mode,unsigned pass_eye) noexcept {return mode==LensMode::per_eye && pass_eye<2?pass_eye:0u;}
// Why a lens frame gets no eye-1 pass (status order): a one-pass mode, an independent camera (a camera feed is mono by nature) or none, a
// missing eye record, or no lens Begin in the frame-start pass.
enum class SecondRefusal : unsigned {none,mode,camera,record,no_first};
inline SecondRefusal second_pass_refusal(LensMode mode,bool camera_valid,bool camera_follows,bool eye0_record,bool eye1_record,bool first_began) noexcept {
    if(mode!=LensMode::per_eye) return SecondRefusal::mode;
    if(!camera_valid || !camera_follows) return SecondRefusal::camera;
    if(!eye0_record || !eye1_record) return SecondRefusal::record;
    if(!first_began) return SecondRefusal::no_first;
    return SecondRefusal::none;
}
// The render (0x366590) consumes its request: it clears renderer+0x88 (0x36674c). The eye-1 pass re-arms it with the image the frame-start
// pass consumed; if the game thread already posted the next frame's request (+0x88 set again), that request is used and put back after.
struct Rearm {std::uintptr_t before,after;};
inline Rearm rearm(std::uintptr_t current,std::uintptr_t consumed) noexcept {return {current?current:consumed,current};}
// A pass drew with the camera it was given: the view's render copy (what the Begin draws and the shadow PREPARE of the same view reads)
// holds the written position and basis (Setup re-orthonormalises the basis, so a tolerance, not bit equality).
inline bool camera_matches(const std::array<float,3>& written_p,const std::array<float,9>& written_b,const std::array<float,3>& copy_p,const std::array<float,9>& copy_b) noexcept {
    for(std::size_t i=0;i<3;++i) if(!(std::abs(written_p[i]-copy_p[i])<=1e-3f)) return false;
    for(std::size_t i=0;i<9;++i) if(!(std::abs(written_b[i]-copy_b[i])<=1e-3f)) return false;
    return true;
}

// v1265 (user, v1264 headset: "timepiece display render got broken ... regressed to a previous state before fix"; status camera_follow_own_none
// [23442,3613,2]: 23.6% of lens frames took the independent-camera path, v1257 5.6%): follows() runs on the game thread right after the
// Timepiece's update (0x44d230) against the PREPARED views, which that tick has not rebuilt yet: while the player camera turns, the basis is one
// tick stale and fails the 0.02 test, and the frame was drawn as an independent camera (the Timepiece's flat pose, the engine's projection, one
// image for both eyes). A camera that followed keeps following while its translation stays the one it followed with (a stale basis, a snap
// turn); the translation used is the one from its last exact frame. It is an independent camera only when it never matched or its translation
// moved away (latch_m), or the latch went latch_gap frames without a camera.
struct FollowLatch {bool valid{};std::array<float,3> t{};std::uint64_t frame{};};
enum class CameraPath : unsigned {follow,latched,own,none};
inline constexpr float latch_m=1.f;inline constexpr std::uint64_t latch_gap=8;
inline CameraPath follow_path(FollowLatch& l,bool basis_follows,const std::array<float,3>& t_now,std::uint64_t frame) noexcept {
    for(float v:t_now) if(!std::isfinite(v)) {l.valid=false;return CameraPath::own;}
    if(basis_follows) {l={true,t_now,frame};return CameraPath::follow;}
    float d=0;for(std::size_t i=0;i<3;++i) d+=(t_now[i]-l.t[i])*(t_now[i]-l.t[i]);
    if(l.valid && frame>=l.frame && frame<=l.frame+latch_gap && std::sqrt(d)<=latch_m) {l.frame=frame;return CameraPath::latched;}
    l.valid=false;return CameraPath::own;
}
// v1265 certificate: the pixel grid a lens image is rendered in against the eye that composites it (the glass samples by screen position):
// projection (view+0x7390), render rect (+0x7860) and resolution fraction (+0x78c0), read from the eye's view before the pass and from the
// lens view at its Begin.
struct Grid {std::array<float,16> projection{};std::array<std::int32_t,4> rect{};std::array<float,2> fraction{};};
inline bool same_grid(const Grid& a,const Grid& b) noexcept {
    for(std::size_t i=0;i<16;++i) if(!(std::abs(a.projection[i]-b.projection[i])<=1e-5f*(1.f+std::abs(b.projection[i])))) return false;
    return a.rect==b.rect && a.fraction==b.fraction;
}

// Begin classification (status timepiece.begins).
enum class Kind : unsigned {eye0,eye1,secondary,null_view,other};
inline Kind classify(std::uintptr_t view,bool secondary_active,std::uintptr_t secondary_view,std::uintptr_t eye0,std::uintptr_t eye1) noexcept {
    if(!view) return Kind::null_view;
    if(secondary_active && view==secondary_view) return Kind::secondary; // the secondary pass, whichever object it borrowed
    if(eye0 && eye0!=eye1 && view==eye0) return Kind::eye0;
    if(eye1 && eye0!=eye1 && view==eye1) return Kind::eye1;
    return Kind::other;
}

// An eye Setup's record: what its own Setup consumed and left, to re-create that state after a secondary pass borrowed the view.
struct Record {
    std::uintptr_t view{},prepared{},allocation{};
    std::uint64_t frame{};
    std::array<std::byte,window_size> written{},engine{}; // the head write it rendered with; the engine window restored after it
    std::array<std::byte,history_bytes> history{};
    std::array<std::byte,0x4c> verify{};
    bool valid{};
};
// Replay only the record of the borrowed view, from this frame cycle (its Setup ran at most one Present ago), with the prepared views
// array and that prepared entry's position unchanged since.
enum class Refusal : unsigned {none,no_record,stale,allocation,prepared_moved};
inline Refusal replay_refusal(const Record& r,std::uintptr_t view,std::uint64_t frame_now,std::uintptr_t allocation_now,
                              const std::array<float,3>& prepared_position,const std::array<float,3>& record_engine_position) noexcept {
    if(!r.valid || r.view!=view || !r.prepared) return Refusal::no_record;
    if(frame_now>r.frame+1) return Refusal::stale;
    if(allocation_now!=r.allocation) return Refusal::allocation;
    for(std::size_t i=0;i<3;++i) if(prepared_position[i]!=record_engine_position[i]) return Refusal::prepared_moved;
    return Refusal::none;
}
// v1248 (review of v1246: 0x366590 returns early when the main view's byte +0x48 is set, without clearing +0x88, so the hook ran every frame
// with no lens pass while the replay and the alternate-eye paired hold followed it): a lens pass ran this frame only when its Begin happened
// inside the hook. The frame of the last real lens Begin is kept; the paired hold expires hold_frames after it.
inline constexpr std::uint64_t hold_frames=2;
inline std::uint64_t lens_pass_frame(bool began,std::uint64_t frame,std::uint64_t last) noexcept {return began?frame:last;}
inline bool lens_paired_hold(std::uint64_t last,std::uint64_t frame) noexcept {return last && frame<=last+hold_frames;}
} // namespace d2vr::secondary_view
