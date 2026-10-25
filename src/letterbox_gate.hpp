#pragma once
// Track T C3: the two black letterbox bars the game paints in an immersive (stereo projection) cutscene are two DrawIndexed calls of 39
// indices (Scaleform vertex shader, untextured pixel shader; the external mod identifies them by the DXBC hash 7ACC1A9A27C4D9E2). This gate
// decides, from facts the draw hook can read cheaply, whether a draw is one of them: exactly 39 indices, one instance, triangle list,
// nothing bound at pixel-shader texture slot 0, at most two per frame. The shader hash itself is not read (no CreateVertexShader
// hook exists in this mod); the combination is narrow enough, and the knob build/no-letterbox-hide turns it off.
#include <cstdint>

namespace d2vr {
struct LetterboxDraw {unsigned indices{},instances{};bool triangle_list{},textured{};};
class LetterboxGate {
    std::uint64_t frame_{};unsigned seen_{};
public:
    static constexpr unsigned indices=39,per_frame=2;
    // `active`: an immersive cutscene scene frame with the knob absent. Returns true when this draw is to be skipped.
    bool skip(bool active,std::uint64_t frame,const LetterboxDraw& draw) noexcept {
        if(!active || draw.indices!=indices || draw.instances!=1 || !draw.triangle_list || draw.textured) return false;
        if(frame!=frame_) {frame_=frame;seen_=0;}
        if(seen_>=per_frame) return false;
        ++seen_;return true;
    }
};
} // namespace d2vr
