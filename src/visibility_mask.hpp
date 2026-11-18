#pragma once
// v1105 probe (read-only, Metro Exodus VR research 2026-11-08): how much of each eye image the lenses hide (XR_KHR_visibility_mask,
// hidden triangles) and how much of the image we actually render is wasted (hidden by the mask, or outside the runtime's FOV because
// the render frustum is symmetric or canted). Pure math, no OpenXR types, so the lab check runs it.
//
// Mask vertices are view-space points on the z = -1 plane (tangent coordinates), per the extension spec. A render frustum is the
// eye-local rotation (canted_fov.hpp; identity for the plain symmetric frustum) plus symmetric half angles. A perspective map keeps
// straight lines straight, so a triangle (or the FOV quad) projected into the render frustum is still a polygon with the projected corners.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace d2vr {

struct RenderFrustum {
    std::array<float,4> rotation{0,0,0,1}; // x,y,z,w; eye-local
    double tan_x{},tan_y{};                // tangents of the symmetric half angles
};

struct VisibilityMaskStats {
    double hidden_of_fov=0;     // mask area / runtime FOV area (tangent space = pixel space of an uncanted runtime-FOV image)
    double wasted_of_render=0;  // 1 - visible area / render frustum area: mask plus what the render frustum adds outside the FOV
    double min_x=0,max_x=0,min_y=0,max_y=0; // mask vertex bounds (sanity check against tan(fov))
    bool valid=false;
};

namespace visibility_mask_detail {
inline std::array<double,2> project(const RenderFrustum& f,double x,double y) {
    // v' = q^-1 v q, v = (x, y, -1), then onto the render frustum's z = -1 plane
    const double qx=-f.rotation[0],qy=-f.rotation[1],qz=-f.rotation[2],qw=f.rotation[3];
    const double vx=x,vy=y,vz=-1;
    const double tx=2*(qy*vz-qz*vy),ty=2*(qz*vx-qx*vz),tz=2*(qx*vy-qy*vx);
    const double rx=vx+qw*tx+(qy*tz-qz*ty),ry=vy+qw*ty+(qz*tx-qx*tz),rz=vz+qw*tz+(qx*ty-qy*tx);
    if(rz>=-1e-9) return {std::numeric_limits<double>::quiet_NaN(),0};
    return {rx/-rz,ry/-rz};
}
inline double triangle_area(std::array<double,2> a,std::array<double,2> b,std::array<double,2> c) {
    return std::abs((b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1]))*.5;
}
} // namespace visibility_mask_detail

// xy: vertex_count (x, y) pairs; indices: index_count entries, three per triangle. fov in radians (left/down negative).
inline VisibilityMaskStats visibility_mask_stats(const float* xy,std::size_t vertex_count,const std::uint32_t* indices,std::size_t index_count,
    float left,float right,float up,float down,const RenderFrustum& render) {
    using namespace visibility_mask_detail;
    VisibilityMaskStats s;
    const double fov_area=(std::tan(right)-std::tan(left))*(std::tan(up)-std::tan(down));
    const double render_area=4*render.tan_x*render.tan_y;
    if(!xy || !indices || !vertex_count || index_count%3 || !(fov_area>0) || !(render_area>0)) return s;
    s.min_x=s.min_y=std::numeric_limits<double>::infinity();s.max_x=s.max_y=-s.min_x;
    for(std::size_t i=0;i<vertex_count;++i) {
        s.min_x=std::min<double>(s.min_x,xy[2*i]);s.max_x=std::max<double>(s.max_x,xy[2*i]);
        s.min_y=std::min<double>(s.min_y,xy[2*i+1]);s.max_y=std::max<double>(s.max_y,xy[2*i+1]);
    }
    double hidden=0,hidden_in_render=0;
    for(std::size_t t=0;t<index_count;t+=3) {
        std::array<std::array<double,2>,3> p{},q{};
        for(int k=0;k<3;++k) {
            const auto v=indices[t+k];if(v>=vertex_count) return {};
            p[k]={xy[2*v],xy[2*v+1]};q[k]=project(render,p[k][0],p[k][1]);
            if(std::isnan(q[k][0])) return {};
        }
        hidden+=triangle_area(p[0],p[1],p[2]);hidden_in_render+=triangle_area(q[0],q[1],q[2]);
    }
    // the runtime FOV quad in the render frustum (a convex quadrilateral: two triangles)
    const double l=std::tan(left),r=std::tan(right),u=std::tan(up),d=std::tan(down);
    const auto a=project(render,l,d),b=project(render,r,d),c=project(render,r,u),e=project(render,l,u);
    if(std::isnan(a[0]) || std::isnan(b[0]) || std::isnan(c[0]) || std::isnan(e[0])) return {};
    const double fov_in_render=triangle_area(a,b,c)+triangle_area(a,c,e);
    s.hidden_of_fov=hidden/fov_area;
    s.wasted_of_render=1-(fov_in_render-hidden_in_render)/render_area;
    s.valid=true;
    return s;
}

// Plan 1.4 (docs/PERF-PLAN-90HZ-2026-11-19.md, E8): a built-in hidden-area mesh for a runtime whose XR_KHR_visibility_mask hidden mesh
// is EMPTY (SteamVR / Steam Link with a Quest 3 on 2026-11-19: "hidden mesh result 0, 0 vertices"). Never used when the runtime returns
// a mesh, nor when the extension is missing or the call fails. Kill switch build/no-mask-fallback.
//
// Shape: the four FOV corners outside an ellipse centred on the FOV rectangle, with semi-axes `scale` x the rectangle's half extents in
// tangent space (scale 1 = the inscribed ellipse; sqrt 2 passes through the corners and hides nothing). No primary source publishes the
// Quest 3 geometry: VDXR forwards ovr_GetFovStencil (mbucchia/VirtualDesktop-OpenXR visibility_mask.cpp) and our 2026-11-19 log has
// only its share and bounds. So the scale is chosen to be safe for any plausible lens outline that hides what the VDXR Quest 3 mesh
// hides (9.1 % of the FOV, about 7.1 % inside it: the mesh's outer ring overhangs the FOV by ~1 % on every side), with the logged
// FOV tangents l -0.933 r 1.000 u 1.111 d -1.192 (mirrored for the right eye). Outlines checked (tools/lens_mask_fallback_study.py,
// at a 7.1 % share): an ellipse on the rectangle centre needs scale >= 1.16; a round lens about the optical axis (tangent 0,0)
// >= 1.25, decentred by up to 0.04 tangent >= 1.30; superellipses n = 3 / 4 >= 1.17 / 1.19; chamfered corners >= 1.18.
// default_scale 1.32 clears the worst (1.30) by 0.02 of the half extent (~25 px of a 2496 px eye) and hides ~0.9 % of that FOV (the
// VDXR mesh hides 7-9 %). It is a floor, not an estimate of the real mask: the stamp it feeds must never darken a visible pixel.
// build/mask-fallback-scale (1.05..1.41) overrides it for a headset A/B; a smaller scale hides more and risks visible corners.
//
// Geometry: in the rectangle-normalised domain the ellipse is the circle |p| = scale. Each corner is a fan from the corner over the
// chain E1, P1..Pn, E2, where E1/E2 are where the circle meets the two edges and P1..Pn are the vertices of the circumscribed polygon
// (tangent lines at evenly spaced angles), so every chord lies outside the circle and no triangle reaches inside it. For
// 1 < scale <= sqrt 2 the corner sees the whole chain (1 + sqrt(scale^2 - 1) >= scale^2), so the fan never folds into the circle.
// Output: tangent-space xy pairs and triangle indices, exactly what xrGetVisibilityMaskKHR would return (visibility_stamp_triangles
// consumes either).
struct LensMaskFallback {
    static constexpr double default_scale=1.32;
    static constexpr double min_scale=1.05,max_scale=1.41421356237;
    std::vector<float> xy;std::vector<std::uint32_t> indices;
};
inline LensMaskFallback lens_mask_fallback(float left,float right,float up,float down,double scale=LensMaskFallback::default_scale,unsigned segments=8) {
    LensMaskFallback m;
    const double l=std::tan(left),r=std::tan(right),u=std::tan(up),d=std::tan(down);
    if(!(r>l) || !(u>d) || !(scale>=LensMaskFallback::min_scale) || !(scale<LensMaskFallback::max_scale) || segments<1 || segments>64) return m;
    const double cx=(l+r)/2,cy=(u+d)/2,hx=(r-l)/2,hy=(u-d)/2;
    const double e=std::sqrt(scale*scale-1);               // the circle meets x = 1 at y = e (and y = 1 at x = e)
    const double a0=std::atan2(e,1.),a1=std::atan2(1.,e),step=(a1-a0)/segments;
    const double out=scale/std::cos(step/2);              // radius of the circumscribed polygon's vertices
    std::vector<std::array<double,2>> chain;chain.reserve(segments+2);
    chain.push_back({1.,e});
    for(unsigned k=1;k<=segments;++k) {const double a=a0+(k-.5)*step;chain.push_back({out*std::cos(a),out*std::sin(a)});}
    chain.push_back({e,1.});
    for(const int sx:{1,-1}) for(const int sy:{1,-1}) {
        const auto first=static_cast<std::uint32_t>(m.xy.size()/2);
        auto put=[&](double x,double y) {m.xy.push_back(static_cast<float>(cx+hx*sx*x));m.xy.push_back(static_cast<float>(cy+hy*sy*y));};
        put(1.,1.);
        for(const auto& p:chain) put(p[0],p[1]);
        for(std::uint32_t k=0;k+1<chain.size();++k) {m.indices.push_back(first);m.indices.push_back(first+1+k);m.indices.push_back(first+2+k);}
    }
    return m;
}
// Which hidden mesh the stamp uses for an eye, from what xrGetVisibilityMaskKHR returned (pure, so the lab covers the rule).
enum class LensMaskSource {none,runtime,fallback};
inline LensMaskSource lens_mask_source(bool call_succeeded,std::size_t runtime_vertices,std::size_t runtime_indices,bool fallback_allowed) {
    if(!call_succeeded) return LensMaskSource::none;               // extension missing or the call failed: an unknown runtime, no guess
    if(runtime_vertices && runtime_indices) return LensMaskSource::runtime;
    return fallback_allowed?LensMaskSource::fallback:LensMaskSource::none; // success with an empty mesh: the runtime has no mask
}

} // namespace d2vr
