#include "../src/xr_quad_validation.hpp"
#include <cstdio>
#include <limits>
using namespace d2vr;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
static XrSwapchain chain_handle(){return reinterpret_cast<XrSwapchain>(std::uintptr_t(0x1000));}
static XrSpace space_handle(){return reinterpret_cast<XrSpace>(std::uintptr_t(0x2000));}
// The comfort vignette as plan D shipped it (VIEW-space pose, identity orientation, 3 m square, 96x96 chain) and as it is now.
static XrCompositionLayerQuad vignette() {
    XrCompositionLayerQuad q{};q.type=XR_TYPE_COMPOSITION_LAYER_QUAD;q.space=space_handle();q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    q.pose.orientation={0,0,0,1};q.pose.position={0,0,-1};q.size={3,3};
    q.subImage.swapchain=chain_handle();q.subImage.imageRect.extent={96,96};return q;
}
static const QuadChainFacts chain96{true,96,96};
int main() {
    const float nan=std::numeric_limits<float>::quiet_NaN();
    CHECK(validate_quad_layer(vignette(),true,chain96,false)==XR_SUCCESS);
    // simulated_runtime_rejects_unnormalized_or_spaceless_quad
    {auto q=vignette();q.pose.orientation={0,0,0,2};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_POSE_INVALID);}
    {auto q=vignette();q.pose.orientation={0,0,0,1.002f};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_POSE_INVALID);}
    {auto q=vignette();q.pose.orientation={0,0,0,1.0005f};CHECK(validate_quad_layer(q,true,chain96,false)==XR_SUCCESS);}
    {auto q=vignette();q.pose.position.y=nan;CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_POSE_INVALID);}
    {auto q=vignette();q.space=XR_NULL_HANDLE;CHECK(validate_quad_layer(q,false,chain96,false)==XR_ERROR_HANDLE_INVALID);}
    {auto q=vignette();CHECK(validate_quad_layer(q,false,chain96,false)==XR_ERROR_HANDLE_INVALID);} // destroyed space handle
    {auto q=vignette();q.size={0,3};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_VALIDATION_FAILURE);}
    {auto q=vignette();q.size={3,nan};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_VALIDATION_FAILURE);}
    {auto q=vignette();q.size={-1,3};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_VALIDATION_FAILURE);}
    {auto q=vignette();q.subImage.imageRect.extent={97,96};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_SWAPCHAIN_RECT_INVALID);}
    {auto q=vignette();q.subImage.imageRect.offset={-1,0};CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_SWAPCHAIN_RECT_INVALID);}
    {auto q=vignette();CHECK(validate_quad_layer(q,true,QuadChainFacts{},false)==XR_ERROR_HANDLE_INVALID);}
    {auto q=vignette();q.eyeVisibility=static_cast<XrEyeVisibility>(7);CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_VALIDATION_FAILURE);}
    // a color-scale struct chained while the extension was never enabled is invalid; with it enabled it is fine
    {XrCompositionLayerColorScaleBiasKHR cs{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};auto q=vignette();q.next=&cs;
     CHECK(validate_quad_layer(q,true,chain96,false)==XR_ERROR_VALIDATION_FAILURE);CHECK(validate_quad_layer(q,true,chain96,true)==XR_SUCCESS);}
    if(failures) {std::printf("simulated_runtime_rejects_unnormalized_or_spaceless_quad: %d failure(s)\n",failures);return 1;}
    std::printf("simulated_runtime_rejects_unnormalized_or_spaceless_quad ok\n");return 0;
}
