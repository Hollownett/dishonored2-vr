#include "dlss_eye_upscaler.hpp"
#include <type_traits>
// Volatile globals consumed by main keep real static SDK symbol references in
// the final executable. This file is COMPILE/LINK ONLY; never execute this gate.
auto volatile dlss_init = &NVSDK_NGX_D3D11_Init_with_ProjectID;
auto volatile dlss_capability = &NVSDK_NGX_D3D11_GetCapabilityParameters;
auto volatile dlss_allocate = &NVSDK_NGX_D3D11_AllocateParameters;
auto volatile dlss_get_i = &NVSDK_NGX_Parameter_GetI;
auto volatile dlss_create = &NGX_D3D11_CREATE_DLSS_EXT;
auto volatile dlss_evaluate = &NGX_D3D11_EVALUATE_DLSS_EXT;
auto volatile dlss_release = &NVSDK_NGX_D3D11_ReleaseFeature;
auto volatile dlss_destroy = &NVSDK_NGX_D3D11_DestroyParameters;
auto volatile dlss_shutdown = &NVSDK_NGX_D3D11_Shutdown1;
static_assert(std::is_same<decltype(d2vr::DlssEyeUpscaler::Calls{}.init),
    decltype(&NVSDK_NGX_D3D11_Init_with_ProjectID)>::value, "vendor init ABI");
static_assert(NVSDK_NGX_Version_API == 0x15 && NVSDK_NGX_PerfQuality_Value_DLAA == 5, "pinned SDK identity");
int main() {
    return dlss_init && dlss_capability && dlss_allocate && dlss_get_i && dlss_create &&
        dlss_evaluate && dlss_release && dlss_destroy && dlss_shutdown ? 0 : 1;
}
