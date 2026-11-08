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

} // namespace d2vr
