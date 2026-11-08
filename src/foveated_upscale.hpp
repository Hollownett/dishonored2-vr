#pragma once
// v1107 foveated upscaling (Metro Exodus VR research, docs/METRO-EXODUS-VR-PERF-RESEARCH.md): FSR 4 upscales only a box around each eye's
// optical centre; FSR 3 (measured near-free on the RX 9070 XT) upscales the whole eye underneath. Pure box math here, shared by the game
// path (fsr_taa.hpp) and the GPU lab (tests/fsr4_fovea_lab.cpp).
//
// The crop must keep the full image's pixel correspondence: a render pixel x maps to output x * W / R. axis() picks the box origin and sizes
// on the output grid so that origin and far edge both land within a small fraction of a pixel of that mapping.
#include <algorithm>
#include <array>
#include <atomic>
#include <sstream>
#include <string>
#include <cmath>
#include <cstdint>

namespace d2vr::fovea {

struct Axis {std::uint32_t src=0,render=0,dst=0,display=0;double error_px=0;bool valid=false;};

// full_display/full_render: the eye's output and render sizes on this axis. centre: the box centre on the output grid (pixels);
// half: the wanted half size (output pixels). Searches origins and sizes near the wanted ones for the best alignment.
inline Axis axis(std::uint32_t full_display,std::uint32_t full_render,double centre,double half) {
    Axis best;
    if(!full_display || !full_render || full_render>full_display || !(half>16)) return best;
    const double scale=static_cast<double>(full_display)/full_render; // output px per render px
    const double want_size=(std::min)(2*half/scale,static_cast<double>(full_render));
    const double want_src=std::clamp((centre-half)/scale,0.0,full_render-want_size); // a box past the edge slides inside
    best.error_px=1e9;
    for(int ds=-24;ds<=24;++ds) for(int dz=-24;dz<=24;++dz) {
        const long src=std::lround(want_src)+ds,size=std::lround(want_size)+dz;
        if(src<0 || size<32 || src+size>static_cast<long>(full_render)) continue;
        const double o0=src*scale,o1=(src+size)*scale;
        const long dst=std::lround(o0),end=std::lround(o1);
        if(end>static_cast<long>(full_display) || end-dst<32) continue;
        const double error=(std::max)(std::abs(o0-dst),std::abs(o1-end));
        const double drift=std::abs(src-want_src)+std::abs(size-want_size)*.5; // prefer the wanted box among equal errors
        if(error+drift*1e-4<best.error_px+(best.valid?(std::abs(best.src-want_src)+std::abs(best.render-want_size)*.5)*1e-4:0)) {
            best={static_cast<std::uint32_t>(src),static_cast<std::uint32_t>(size),static_cast<std::uint32_t>(dst),static_cast<std::uint32_t>(end-dst),error,true};
        }
    }
    return best;
}

struct Box {Axis x,y;bool valid() const noexcept {return x.valid && y.valid;}};

// The eye's box: centre in NDC of the render frustum (optical axis), half extents in NDC, and the eye's sizes.
inline Box box(std::uint32_t display_w,std::uint32_t display_h,std::uint32_t render_w,std::uint32_t render_h,
    double centre_ndc_x,double centre_ndc_y,double half_ndc_x,double half_ndc_y) {
    Box b;
    const double cx=(centre_ndc_x+1)*.5*display_w,cy=(1-centre_ndc_y)*.5*display_h; // NDC +y up, pixels +y down
    b.x=axis(display_w,render_w,cx,half_ndc_x*.5*display_w);
    b.y=axis(display_h,render_h,cy,half_ndc_y*.5*display_h);
    return b;
}

// Published by GameXr (Present thread) each located frame, read by FsrTaa::prepare: each eye's optical centre and the box half extents in
// the render frustum's NDC. half_deg 0 = off (build/no-foveated-upscale, or the live command "fovea off").
inline std::atomic<float> half_deg{30.f};
inline std::array<std::atomic<float>,2> centre_x{},centre_y{},half_x{},half_y{};
inline std::atomic<bool> published{false};
// Status counters.
inline std::array<std::atomic<unsigned long long>,2> dispatches{},failures{};
inline std::atomic<unsigned long long> rebuilds{};
inline std::array<std::atomic<unsigned>,2> box_w{},box_h{},box_x{},box_y{};
inline std::string json() {
    std::ostringstream o;
    o<<"{\"half_deg\":"<<half_deg.load()<<",\"published\":"<<(published.load()?"true":"false")<<",\"dispatches\":["<<dispatches[0].load()<<','<<dispatches[1].load()
     <<"],\"failures\":["<<failures[0].load()<<','<<failures[1].load()<<"],\"rebuilds\":"<<rebuilds.load()<<",\"box\":[["<<box_x[0].load()<<','<<box_y[0].load()<<','
     <<box_w[0].load()<<','<<box_h[0].load()<<"],["<<box_x[1].load()<<','<<box_y[1].load()<<','<<box_w[1].load()<<','<<box_h[1].load()<<"]]}";
    return o.str();
}

} // namespace d2vr::fovea
