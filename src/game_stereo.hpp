#pragma once
#include "game_camera.hpp"

namespace d2vr {
// Exact-build, disassembly-derived profile. Runtime checks are still required.
inline constexpr std::uintptr_t prepared_views_rva=0x2bc58a0;
inline constexpr std::uintptr_t selected_mode_rva=0x2be6910;
inline constexpr std::uintptr_t single_mode_rva=0x1aca0b8;
inline constexpr std::uintptr_t side_by_side_mode_rva=0x1aca220;
inline constexpr std::uintptr_t separation_value_rva=0x2be694c;
inline constexpr std::uintptr_t screen_separation_value_rva=0x2be69cc;
inline constexpr std::size_t prepared_view_stride=0x3ab0;

// The matched native begin-view calls delimit complete eyes independently of
// how many compositor/GUI passes bind the backbuffer. Reset at every Present.
class NativeEyePair {
    int phase_{};
    bool invalid_{};
public:
    bool begin(int eye) noexcept {
        if(eye<0) return false; // Auxiliary view, no eye identity.
        if(eye>1 || invalid_ || (eye==0?phase_!=0:phase_!=1)) {invalid_=true;return false;}
        ++phase_;
        return eye==1;
    }
    bool complete() const noexcept {return phase_==2 && !invalid_;}
    void reset() noexcept {phase_=0;invalid_=false;}
};

struct EyeFov {float left,right,up,down;};
// Diagnostic frustum: keep both scalar FOV and explicit projection symmetric.
// This isolates passes that reconstruct a frustum from scalar FOV values.
inline std::optional<EyeFov> enclosing_symmetric_fov(EyeFov fov) {
    for(float x:{fov.left,fov.right,fov.up,fov.down})
        if(!std::isfinite(x) || std::abs(x)>=1.56f) return std::nullopt;
    if(fov.left>=0 || fov.right<=0 || fov.up<=0 || fov.down>=0) return std::nullopt;
    float horizontal=std::max(-fov.left,fov.right),vertical=std::max(fov.up,-fov.down);
    return EyeFov{-horizontal,horizontal,vertical,-vertical};
}
// Row-major, column-vector RH reverse-Z, as read from the native projection.
// Preserve its depth coefficients; replace only the optical frustum.
inline std::optional<std::array<float,16>> eye_projection(const std::array<float,16>& native,EyeFov fov) {
    for(float x:native) if(!std::isfinite(x)) return std::nullopt;
    for(float x:{fov.left,fov.right,fov.up,fov.down})
        if(!std::isfinite(x) || std::abs(x)>=1.56f) return std::nullopt;
    if(fov.left>=fov.right || fov.down>=fov.up || native[10]<0 || native[11]<=0 ||
       std::abs(native[14]+1)>1e-5f || std::abs(native[15])>1e-5f) return std::nullopt;
    float l=std::tan(fov.left),r=std::tan(fov.right),u=std::tan(fov.up),d=std::tan(fov.down);
    if(r-l<.01f || u-d<.01f) return std::nullopt;
    std::array<float,16> out{};
    // The native projection is symmetric, so its off-centre terms carry only the
    // TAA sub-pixel jitter; keep it so temporal AA accumulates detail per eye.
    // Anything beyond a few pixels is not jitter and is dropped.
    const auto jitter=[](float v) {return std::abs(v)<.01f?v:0.0f;};
    out[0]=2/(r-l);out[2]=(r+l)/(r-l)+jitter(native[2]);out[5]=2/(u-d);out[6]=(u+d)/(u-d)+jitter(native[6]);
    out[10]=native[10];out[11]=native[11];out[14]=-1;
    return out;
}

// The engine's TAA jitter (0x3742b0, exact build): the D3D 8x pattern in 1/16 px (table RVA 0x1b35f10),
// indexed by frame counter & 7. It shifts the frustum window by +j (UV of the render size), so the
// projection gets [2]+=2jx, [6]+=2jy, and stores j at view+0x7850. Setup copies +0x7850 to +0x7858
// (0x375a98) before; cb_jittervectors=(cur,prev) and cb_jitterrelativetopreviousframe=2(prev-cur)
// (0x3586a0) dejitter TAA and motion vectors. A forced projection (+0x140) skips the write, so
// forced views must add the jitter with this sign and store it themselves.
struct TaaJitter {float x,y;}; // UV units, engine sign
inline constexpr std::array<std::array<float,2>,8> engine_taa_pattern{{{1,-3},{-1,3},{5,1},{-3,-5},{-5,5},{-7,1},{3,7},{7,-7}}};
inline TaaJitter engine_taa_jitter(std::uint32_t counter,float width,float height) {
    const auto& p=engine_taa_pattern[counter%8];
    return {p[0]/16/width,p[1]/16/height};
}
inline void apply_taa_jitter(std::array<float,16>& projection,TaaJitter j) {projection[2]+=2*j.x;projection[6]+=2*j.y;}

inline bool valid_stereo_metadata(const std::array<std::byte,80>& bytes,std::uintptr_t base) {
    for(int eye=0;eye<2;++eye) {
        auto data=bytes.data()+eye*40;
        std::uintptr_t label{};
        std::uint32_t view_index{};
        std::array<float,4> viewport{};
        float side{};
        std::memcpy(&label,data,8);
        std::memcpy(&view_index,data+8,4);
        std::memcpy(viewport.data(),data+12,16);
        std::memcpy(&side,data+32,4);
        if(label!=base+0x1ab2848 || view_index!=0 ||
           viewport!=std::array<float,4>{eye==0?0.0f:0.5f,0.0f,0.5f,1.0f} ||
           side!=(eye==0?-1.0f:1.0f) || data[28]!=std::byte{1} || data[29]!=std::byte{1}) return false;
    }
    return true;
}
} // namespace d2vr
