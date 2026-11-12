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
