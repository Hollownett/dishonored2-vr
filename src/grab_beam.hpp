#pragma once
#include "tracked_pose.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr {
// Exact inverse of the position half of to_game_pose: undo the heading frame and
// the recenter rotation/translation, then scale back to XR metres. A non-yaw
// recenter or an invalid heading frame is rejected rather than guessed at.
inline std::optional<Vec3> world_to_xr(const Vec3& world,const TrackedPose& head_origin,
                                       const WorldTrackedPose& anchor,double units_per_metre) {
    if(!finite(world) || !finite(head_origin.position) || !finite(anchor.position) ||
       !std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000) return std::nullopt;
    const auto& raw=head_origin.orientation;
    const double norm=raw.x*raw.x+raw.y*raw.y+raw.z*raw.z+raw.w*raw.w;
    if(!std::isfinite(norm) || norm<0.5 || norm>1.5) return std::nullopt;
    const auto recenter=normalized(raw);
    if(std::abs(recenter.x)>1e-6 || std::abs(recenter.z)>1e-6) return std::nullopt;
    std::array<Vec3,3> frame{};
    for(std::size_t i=0;i<3;++i) {
        frame[i]=anchor.basis[i];
        if(!finite(frame[i]) || std::abs(dot(frame[i],frame[i])-1)>0.02) return std::nullopt;
    }
    if(std::abs(dot(frame[0],frame[1]))>0.02 || std::abs(dot(frame[0],frame[2]))>0.02 ||
       std::abs(dot(frame[1],frame[2]))>0.02 || dot(cross(frame[0],frame[1]),frame[2])<0.98 ||
       std::abs(frame[2].x)>0.02 || std::abs(frame[2].y)>0.02 || frame[2].z<0.98) return std::nullopt;
    // convert(xr)=frame[0]*(-z)+frame[1]*(-x)+frame[2]*y is a rotation, so its
    // inverse is the transpose applied to the unit-scaled world offset.
    const Vec3 local=(world-anchor.position)*(1/units_per_metre);
    const Vec3 aligned{-dot(frame[1],local),dot(frame[2],local),-dot(frame[0],local)};
    const Vec3 position=head_origin.position+rotated(recenter,aligned);
    if(!finite(position)) return std::nullopt;
    return position;
}
// The caller routes hand poses through camera_relative_controller before
// to_game_pose, so undo that recenter translation as well to recover the raw
// tracked controller position.
inline std::optional<Vec3> world_to_xr_controller(const Vec3& world,const TrackedPose& head_origin,
        const WorldTrackedPose& anchor,double units_per_metre,
        const Vec3& current_head,const Vec3& calibrated_head) {
    if(!finite(current_head) || !finite(calibrated_head))
        throw std::invalid_argument("Invalid camera-relative controller position");
    const auto relative=world_to_xr(world,head_origin,anchor,units_per_metre);
    if(!relative) return std::nullopt;
    const Vec3 position=*relative+current_head-calibrated_head;
    if(!finite(position)) return std::nullopt;
    return position;
}

// Quaternion whose local axes are the given right-handed columns.
inline Quaternion basis_quaternion(Vec3 x,Vec3 y,Vec3 z) {
    const double trace=x.x+y.y+z.z;
    if(trace>0) {const double s=std::sqrt(trace+1)*2;return {(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,.25*s};}
    if(x.x>y.y && x.x>z.z) {const double s=std::sqrt(1+x.x-y.y-z.z)*2;return {.25*s,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};}
    if(y.y>z.z) {const double s=std::sqrt(1+y.y-x.x-z.z)*2;return {(y.x+x.y)/s,.25*s,(z.y+y.z)/s,(z.x-x.z)/s};}
    const double s=std::sqrt(1+z.z-x.x-y.y)*2;return {(z.x+x.z)/s,(z.y+y.z)/s,.25*s,(x.y-y.x)/s};
}
// Viewer-facing ribbon for the magic line: +X is the width axis the quad is
// extruded across, +Y runs along the beam, +Z faces the eye.
struct BeamQuad {Vec3 centre;Quaternion orientation;float width,length;};
inline std::optional<BeamQuad> beam_quad(Vec3 from,Vec3 to,Vec3 eye,float width=0.012f) {
    if(!finite(from) || !finite(to) || !finite(eye) ||
       !std::isfinite(width) || width<=0 || width>0.5f) return std::nullopt;
    const Vec3 along=to-from;
    const double span=length(along);
    if(!std::isfinite(span) || span<0.02 || span>1e4) return std::nullopt;
    const Vec3 centre=(from+to)*.5;
    const Vec3 y=along*(1/span);
    const Vec3 side=cross(centre-eye,y); // perpendicular to the beam and the sight line
    const double facing=length(side);
    if(!std::isfinite(facing) || facing<1e-4) return std::nullopt; // eye on the beam axis
    const Vec3 x=side*(1/facing);
    const Vec3 z=cross(x,y);
    if(dot(z,eye-centre)<=0) return std::nullopt;
    return BeamQuad{centre,basis_quaternion(x,y,z),width,static_cast<float>(span)};
}

// Dishonored void ink: white-blue core through a cyan-blue falloff, alpha 0 on
// the outer columns, both ends faded and a gentle bead pattern along the length.
inline std::array<std::uint8_t,16*128*4> beam_image() {
    std::array<std::uint8_t,16*128*4> pixels{};
    constexpr double pi=3.14159265358979323846;
    for(int y=0;y<128;++y) {
        const double v=(y+.5)/128;
        const double fade=std::pow(std::sin(pi*v),.7);
        const double beads=.88+.12*std::sin(2*pi*v*9);
        for(int x=0;x<16;++x) {
            if(x==0 || x==15) continue;
            const double t=((x+.5)/16-.5)*2;
            const double core=std::exp(-(t*t)/.36);
            const double tint=std::exp(-(t*t)/.0625);
            const auto offset=static_cast<std::size_t>((y*16+x)*4);
            pixels[offset]=static_cast<std::uint8_t>(96+150*tint+.5);
            pixels[offset+1]=static_cast<std::uint8_t>(152+96*tint+.5);
            pixels[offset+2]=255;
            pixels[offset+3]=static_cast<std::uint8_t>(fade*beads*core*255+.5);
        }
    }
    return pixels;
}
// Target marker in the same palette: a soft ring over a dim halo.
inline std::array<std::uint8_t,32*32*4> target_glow_image() {
    std::array<std::uint8_t,32*32*4> pixels{};
    for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
        const double radius=std::hypot(x-15.5,y-15.5);
        if(radius>14.5) continue;
        const double ring=std::exp(-std::pow((radius-9.5)/2.4,2));
        const double intensity=std::min(1.0,ring+.34*std::exp(-std::pow(radius/6.5,2)));
        const auto offset=static_cast<std::size_t>((y*32+x)*4);
        pixels[offset]=static_cast<std::uint8_t>(96+150*ring+.5);
        pixels[offset+1]=static_cast<std::uint8_t>(152+96*ring+.5);
        pixels[offset+2]=255;
        pixels[offset+3]=static_cast<std::uint8_t>(intensity*255+.5);
    }
    return pixels;
}
} // namespace d2vr
