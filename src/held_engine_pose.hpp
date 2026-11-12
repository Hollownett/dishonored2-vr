#pragma once
// v1254 (user, v1252 walking bursts: a carried ashtray sits at two places in a steady hand, about one render in three or four ~4 cm off;
// held_census.prop unplaced 2589 of 6691 renders). The bursts show why: 5 of 16 consecutive presented frames carry the SAME physics tick
// (dt 0), so on those renders the engine's render frame of the held prop was never written by its own physics sync and the draw composed
// from it is a tick old, while the mod's draw-time placement is built from this render's drawn hand. The placement wins where it matches
// and the engine frame shows where it does not: two positions, one flicker.
//
// v1249 already makes the ENGINE pose of a physically held RIGID prop the hold's goal - but only inside the body's POST_PHYSICS sync
// (0x80b7c0 -> 0x754ea0), which runs once per physics step, not once per render. v1254 writes the render frame itself, once per
// presented render, on the publisher thread, exactly as place_weapons() has written a held weapon's model frame since v864:
//
//     model+0x1860 (origin 3, three axis ROWS 9, scales 3) := the pose the draws are placed at (B)
//
// The game rewrites the frame from the entity at its own tick, so nothing accumulates and the physics body is untouched. Every pass the
// renderer composes from the frame - both eyes, shadow maps, culling bounds, and any draw the placement did not match - is then at the
// hand on every rendered frame, which is what a bottle (the game's own keyframed carry) already does.
//
// Frame layout (weapon_draw::model_of reads it the same way): origin f[0..2]; axis c component r at f[3+c*3+r]; scale c at f[12+c];
// column c of the column-vector matrix = axis c * scale c, the last column the origin. A pose B is packed back by normalising each
// column into its axis row and taking the column length as that axis's scale, so a model that natively carries a scale keeps it.
#include <array>
#include <cmath>
#include <cstddef>

namespace d2vr {
struct HeldEnginePose {
    static constexpr double min_scale=1e-4,max_scale=100.,max_move_m=1.5,write_eps=.0001;
    inline static bool on=true;   // build/no-held-engine-pose: the frame is left as the engine writes it (v1253 behaviour)
    // B (a weapon_draw::Mat: 4x4 row-major, column vectors) as the engine's 15-float model frame. false: not a rigid, finite frame.
    static bool frame_from_pose(const double* b,float* out) noexcept {
        if(!b || !out) return false;
        for(unsigned i=0;i<16;++i) if(!std::isfinite(b[i])) return false;
        double len[3]{};
        for(unsigned c=0;c<3;++c) {
            const double x=b[c],y=b[4+c],z=b[8+c];
            len[c]=std::sqrt(x*x+y*y+z*z);
            if(!(len[c]>=min_scale && len[c]<=max_scale)) return false;
        }
        out[0]=static_cast<float>(b[3]);out[1]=static_cast<float>(b[7]);out[2]=static_cast<float>(b[11]);
        for(unsigned c=0;c<3;++c) {
            const double inv=1/len[c];
            out[3+c*3]=static_cast<float>(b[c]*inv);out[3+c*3+1]=static_cast<float>(b[4+c]*inv);out[3+c*3+2]=static_cast<float>(b[8+c]*inv);
            out[12+c]=static_cast<float>(len[c]);
        }
        for(unsigned i=0;i<15;++i) if(!std::isfinite(out[i])) return false;
        return true;
    }
    // Largest absolute difference between two frames (metres for the origin, unitless for the axes and scales).
    static double frame_gap(const float* a,const float* b) noexcept {
        double d=0;
        for(unsigned i=0;i<15;++i) {const double v=std::abs(static_cast<double>(a[i])-static_cast<double>(b[i]));if(v>d) d=v;}
        return d;
    }
    static double origin_gap(const float* a,const float* b) noexcept {
        return std::hypot(static_cast<double>(a[0])-static_cast<double>(b[0]),
                          std::hypot(static_cast<double>(a[1])-static_cast<double>(b[1]),
                                     static_cast<double>(a[2])-static_cast<double>(b[2])));
    }
};
} // namespace d2vr
