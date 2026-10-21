#pragma once
// Runtime-style validation of one XrCompositionLayerQuad, as the OpenXR spec and SteamVR enforce it. Header-only and pure so
// it runs on Linux; the simulated backend calls it from xrEndFrame so a layer the real runtime would reject is rejected there too.
#include <openxr/openxr.h>
#include <cmath>
#include <cstdint>
namespace d2vr {
struct QuadChainFacts {bool known{};std::uint32_t width{},height{};}; // the swapchain the quad shows
// space_valid: the quad's `space` handle is a live space. color_scale_enabled: XR_KHR_composition_layer_color_scale_bias was enabled.
inline XrResult validate_quad_layer(const XrCompositionLayerQuad& q,bool space_valid,const QuadChainFacts& chain,bool color_scale_enabled) noexcept {
    if(q.type!=XR_TYPE_COMPOSITION_LAYER_QUAD) return XR_ERROR_VALIDATION_FAILURE;
    if(!space_valid || q.space==XR_NULL_HANDLE) return XR_ERROR_HANDLE_INVALID;
    const auto& o=q.pose.orientation;const auto& p=q.pose.position;
    if(!std::isfinite(o.x)||!std::isfinite(o.y)||!std::isfinite(o.z)||!std::isfinite(o.w)||
       !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return XR_ERROR_POSE_INVALID;
    const double n=std::sqrt(double(o.x)*o.x+double(o.y)*o.y+double(o.z)*o.z+double(o.w)*o.w);
    if(std::fabs(n-1.0)>1e-3) return XR_ERROR_POSE_INVALID;
    if(!std::isfinite(q.size.width)||!std::isfinite(q.size.height)||!(q.size.width>0)||!(q.size.height>0)) return XR_ERROR_VALIDATION_FAILURE;
    if(q.eyeVisibility!=XR_EYE_VISIBILITY_BOTH && q.eyeVisibility!=XR_EYE_VISIBILITY_LEFT && q.eyeVisibility!=XR_EYE_VISIBILITY_RIGHT) return XR_ERROR_VALIDATION_FAILURE;
    const auto& r=q.subImage.imageRect;
    if(!chain.known || !q.subImage.swapchain) return XR_ERROR_HANDLE_INVALID;
    if(r.offset.x<0||r.offset.y<0||r.extent.width<=0||r.extent.height<=0||
       std::int64_t(r.offset.x)+r.extent.width>std::int64_t(chain.width)||std::int64_t(r.offset.y)+r.extent.height>std::int64_t(chain.height)) return XR_ERROR_SWAPCHAIN_RECT_INVALID;
    for(const auto* n2=static_cast<const XrBaseInStructure*>(q.next);n2;n2=n2->next)
        if(n2->type==XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR && !color_scale_enabled) return XR_ERROR_VALIDATION_FAILURE;
    return XR_SUCCESS;
}
}
