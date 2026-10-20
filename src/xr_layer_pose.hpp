#pragma once
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
namespace d2vr {
// SteamVR rejects (XR_ERROR_POSE_INVALID) layers whose orientation is not unit length or whose pose is non-finite; fix or drop them before xrEndFrame.
enum class LayerKind {projection,quad,cylinder,other,count};
struct LayerPoseGuard {
    std::array<std::uint64_t,4> repaired{},dropped{};
    int bad_kind{-1};XrPosef bad_pose{};bool submit_none{};
};
inline bool finite_pose(const XrPosef& p) noexcept {
    return std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z)&&
           std::isfinite(p.orientation.x)&&std::isfinite(p.orientation.y)&&std::isfinite(p.orientation.z)&&std::isfinite(p.orientation.w);
}
// 0 = already unit, 1 = normalized in place, -1 = unusable (non-finite, or norm outside the sane band).
inline int repair_layer_pose(XrPosef& p) noexcept {
    if(!finite_pose(p)) return -1;
    auto& q=p.orientation;const double n=std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    if(!(n>1e-3 && n<1e3)) return -1;
    if(std::fabs(n-1.0)<=1e-6) return 0;
    q.x=float(q.x/n);q.y=float(q.y/n);q.z=float(q.z/n);q.w=float(q.w/n);return 1;
}
// Repairs poses in place (layers are caller-owned), drops unusable overlays into `storage` (order kept), submits nothing if a projection view is unusable.
inline LayerPoseGuard guard_frame_layers(XrFrameEndInfo& end,const XrCompositionLayerBaseHeader** storage,std::size_t capacity) noexcept {
    LayerPoseGuard g;if(!end.layers||!end.layerCount) return g;
    std::uint32_t kept=0;bool none=false;
    auto note=[&](LayerKind k,const XrPosef& raw,bool drop){const auto i=std::size_t(k);if(drop)++g.dropped[i];else ++g.repaired[i];g.bad_kind=int(k);g.bad_pose=raw;};
    auto check=[&](LayerKind k,XrPosef& p){const XrPosef raw=p;const int r=repair_layer_pose(p);if(r>0) note(k,raw,false);else if(r<0) note(k,raw,true);return r>=0;};
    for(std::uint32_t i=0;i<end.layerCount&&!none;++i) {
        const auto* h=end.layers[i];if(!h) continue;bool ok=true;LayerKind k=LayerKind::other;
        if(h->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            k=LayerKind::projection;auto* l=const_cast<XrCompositionLayerProjection*>(reinterpret_cast<const XrCompositionLayerProjection*>(h));
            for(std::uint32_t v=0;v<l->viewCount&&l->views;++v) ok=check(k,const_cast<XrCompositionLayerProjectionView*>(l->views)[v].pose)&&ok;
            if(!ok) {none=true;continue;}
        } else if(h->type==XR_TYPE_COMPOSITION_LAYER_QUAD) {k=LayerKind::quad;ok=check(k,const_cast<XrCompositionLayerQuad*>(reinterpret_cast<const XrCompositionLayerQuad*>(h))->pose);}
        else if(h->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {k=LayerKind::cylinder;ok=check(k,const_cast<XrCompositionLayerCylinderKHR*>(reinterpret_cast<const XrCompositionLayerCylinderKHR*>(h))->pose);}
        if(ok && kept<capacity) storage[kept++]=h;
    }
    if(none) {g.submit_none=true;end.layerCount=0;return g;}
    end.layerCount=kept;end.layers=storage;return g;
}
// SteamVR implements a quad/cylinder sub-rect as a copy into an intermediate image and rejects frames with several of them
// (POSE_INVALID / SWAPCHAIN_RECT_INVALID): every quad and cylinder must show its whole swapchain at offset (0,0).
using ChainSizes=std::unordered_map<XrSwapchain,std::array<std::uint32_t,2>>;
inline bool full_extent(const XrSwapchainSubImage& s,const ChainSizes& sizes) noexcept {
    const auto at=sizes.find(s.swapchain);if(at==sizes.end()) return false;
    const auto& r=s.imageRect;
    return r.offset.x==0 && r.offset.y==0 && r.extent.width>=0 && r.extent.height>=0 &&
           static_cast<std::uint32_t>(r.extent.width)==at->second[0] && static_cast<std::uint32_t>(r.extent.height)==at->second[1];
}
// Counts the quad/cylinder layers of `end` (after guard_frame_layers) that are not full-extent; an unknown swapchain counts too.
inline std::uint64_t count_subrect_layers(const XrFrameEndInfo& end,const ChainSizes& sizes) noexcept {
    std::uint64_t n=0;
    for(std::uint32_t i=0;i<end.layerCount && end.layers;++i) {
        const auto* h=end.layers[i];if(!h) continue;
        if(h->type==XR_TYPE_COMPOSITION_LAYER_QUAD) n+=!full_extent(reinterpret_cast<const XrCompositionLayerQuad*>(h)->subImage,sizes);
        else if(h->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) n+=!full_extent(reinterpret_cast<const XrCompositionLayerCylinderKHR*>(h)->subImage,sizes);
    }
    return n;
}
// One line per submitted layer (kind, pose, size, sub-image rect, swapchain) for the recovery log when xrEndFrame fails.
inline std::string describe_frame_layers(const XrFrameEndInfo& end) {
    std::string out;char b[256];
    auto pose=[&](const XrPosef& p){std::snprintf(b,sizeof b," q=(%.9g,%.9g,%.9g,%.9g) p=(%.6g,%.6g,%.6g)",p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w,p.position.x,p.position.y,p.position.z);out+=b;};
    auto sub=[&](const XrSwapchainSubImage& s){std::snprintf(b,sizeof b," chain=%llx rect=(%d,%d %dx%d) array=%u",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(s.swapchain)),s.imageRect.offset.x,s.imageRect.offset.y,s.imageRect.extent.width,s.imageRect.extent.height,s.imageArrayIndex);out+=b;};
    for(std::uint32_t i=0;i<end.layerCount && end.layers;++i) {
        const auto* h=end.layers[i];std::snprintf(b,sizeof b,"%s#%u",i?"; ":"",i);out+=b;if(!h) {out+=" null";continue;}
        if(h->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* l=reinterpret_cast<const XrCompositionLayerProjection*>(h);std::snprintf(b,sizeof b," projection flags=%llx",static_cast<unsigned long long>(l->layerFlags));out+=b;
            for(std::uint32_t v=0;v<l->viewCount && l->views;++v) {const auto& w=l->views[v];std::snprintf(b,sizeof b," v%u fov=(%.4g,%.4g,%.4g,%.4g)",v,w.fov.angleLeft,w.fov.angleRight,w.fov.angleUp,w.fov.angleDown);out+=b;pose(w.pose);sub(w.subImage);}
        } else if(h->type==XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* l=reinterpret_cast<const XrCompositionLayerQuad*>(h);std::snprintf(b,sizeof b," quad eye=%d size=(%.4g,%.4g)",static_cast<int>(l->eyeVisibility),l->size.width,l->size.height);out+=b;pose(l->pose);sub(l->subImage);
        } else if(h->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) {
            const auto* l=reinterpret_cast<const XrCompositionLayerCylinderKHR*>(h);std::snprintf(b,sizeof b," cylinder radius=%.4g angle=%.4g aspect=%.4g",l->radius,l->centralAngle,l->aspectRatio);out+=b;pose(l->pose);sub(l->subImage);
        } else {std::snprintf(b,sizeof b," type=%d",static_cast<int>(h->type));out+=b;}
    }
    return out;
}
}
