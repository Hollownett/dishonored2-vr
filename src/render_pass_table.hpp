#pragma once
// Static map of the engine's ARK_RENDERPASS_* job items (v999).
//
// Evidence (Dishonored2.exe Steam build, sha256 b671f824...6180):
//  - 0x34fe00 is the render-pass registration function.  It calls 0x1402c2960 once per pass with
//    rcx = a 0x18-byte vector object at (manager+0xd7f0+offset), rdx = the ARK_RENDERPASS_* name
//    (0x1b2e700..0x1b2edf0), r8d = item count (always 1), r9d = a constant, stack +0x20 = flags and
//    stack +0x28 = the job callback.  The 52 call sites are at 0x35002a..0x35113a.
//  - 0x1402c29a0 is the append half of 0x1402c2960: it grows the vector to 0x120-byte items and writes
//    item+0xd8 = callback, item+0xf0 = 0xffffffff, item+0xf4 |= flags, item+0xf8 = index,
//    item+0xfc = r8d (count).  So the callback the dispatcher reads at [item+0xd8] is exactly the RVA
//    in this table.
//  - The table below is registration order (pass id = index).  POSTPROC 0x327270 and POST_TONEMAP
//    0x323e70 match the two callbacks the existing post-process hooks already use.
//
// ASYNC_KICKOFF (id 1) and WAIT_FOR_ASYNC (id 44) are both registered with callback 0x34fbd0.  That
// callback only sets deferred-context flag +0x12ad4 and returns false (0x34fbd0-0x34fbe6); it never
// creates a command list, so the duplicate cannot associate a listed pass with the wrong GPU work.
// callback_rva_to_pass() therefore returns the first match (ASYNC_KICKOFF); WAIT_FOR_ASYNC has no
// separate list bucket.  Empty/unknown callbacks are -1 (untagged).
#include <array>
#include <cstdint>

namespace d2vr {
inline constexpr unsigned render_pass_count=52;
// The timer's pass-id space is 0..51 for named passes and 63 for "no list->pass mapping".  52..62 are
// deliberately unused so a truncated/unknown id can never alias a real pass.
inline constexpr unsigned render_pass_untagged=63;
struct RenderPassInfo {const char* name;std::uintptr_t callback_rva;};
inline constexpr std::array<RenderPassInfo,render_pass_count> render_passes{{
    {"ARK_RENDERPASS_CLEAR", 0x3110E0}, // 0
    {"ARK_RENDERPASS_ASYNC_KICKOFF", 0x34FBD0}, // 1
    {"ARK_RENDERPASS_ASYNC_CLEAR", 0x310FB0}, // 2
    {"ARK_RENDERPASS_UPDATE_NPC_DECALS", 0x340F10}, // 3
    {"ARK_RENDERPASS_CULL_OCEAN", 0x31A320}, // 4
    {"ARK_RENDERPASS_GATHER_UMBRA", 0x31C710}, // 5
    {"ARK_RENDERPASS_GATHER_LIGHTS", 0x31BCC0}, // 6
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_GATHER_LIGHTS", 0x3101D0}, // 7
    {"ARK_RENDERPASS_GATHER_DYN_MODELS", 0x31AD30}, // 8
    {"ARK_RENDERPASS_GATHER_DYN_MODELS_FINALIZE", 0x31A150}, // 9
    {"ARK_RENDERPASS_SORT", 0x33D320}, // 10
    {"ARK_RENDERPASS_SHADOWS_PREPARE", 0x333550}, // 11
    {"ARK_RENDERPASS_COLLECT_DYNAMIC_CASTERS", 0x330020}, // 12
    {"ARK_RENDERPASS_GATHER_CASTERS", 0x330C10}, // 13
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_DYNAMIC_MODELS", 0x310230}, // 14
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_WORLD_STATE_AND_OCCLUDERS", 0x310200}, // 15
    {"ARK_RENDERPASS_GPUCULLING", 0x2FC030}, // 16
    {"ARK_RENDERPASS_DEPTHPASS", 0x2FD8D0}, // 17
    {"ARK_RENDERPASS_DEPTHMINMAX", 0x2FC130}, // 18
    {"ARK_RENDERPASS_SSAO", 0x305BE0}, // 19
    {"ARK_RENDERPASS_SHADOWS_WORLD", 0x32F390}, // 20
    {"ARK_RENDERPASS_SHADOWS_DYNAMIC", 0x32F380}, // 21
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_ACCUMULATE_FINAL_SH", 0x310190}, // 22
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_IBL", 0x3102A0}, // 23
    {"ARK_RENDERPASS_INDIRECT_LIGHTING_UPDATE_PROBES", 0x310260}, // 24
    {"ARK_RENDERPASS_SCREENSHADOWS", 0x32C7F0}, // 25
    {"ARK_RENDERPASS_TLFLIGHTCULL", 0x2F7260}, // 26
    {"ARK_RENDERPASS_FORWARD_WORLD", 0x3133D0}, // 27
    {"ARK_RENDERPASS_FORWARD_DYNAMIC", 0x3133B0}, // 28
    {"ARK_RENDERPASS_FOG", 0x309430}, // 29
    {"ARK_RENDERPASS_UPDATE_OCEAN_FFT", 0x3053B0}, // 30
    {"ARK_RENDERPASS_UPDATE_OCEAN", 0x305370}, // 31
    {"ARK_RENDERPASS_RENDER_WATER", 0x3045A0}, // 32
    {"ARK_RENDERPASS_DEPTHALPHA_LIT", 0x3222A0}, // 33
    {"ARK_RENDERPASS_DOF_AUTOFOCUS", 0x3150D0}, // 34
    {"ARK_RENDERPASS_DOF_ALPHA", 0x314B10}, // 35
    {"ARK_RENDERPASS_DOF", 0x315450}, // 36
    {"ARK_RENDERPASS_EMISSIVE", 0x31F220}, // 37
    {"ARK_RENDERPASS_ALPHA_LIT", 0x31DCE0}, // 38
    {"ARK_RENDERPASS_EMISSIVE_ONLY", 0x323210}, // 39
    {"ARK_RENDERPASS_DARKVISION", 0x31F960}, // 40
    {"ARK_RENDERPASS_DISTORTION", 0x3222F0}, // 41
    {"ARK_RENDERPASS_POSTPROC", 0x327270}, // 42
    {"ARK_RENDERPASS_POST_TONEMAP", 0x323E70}, // 43
    {"ARK_RENDERPASS_WAIT_FOR_ASYNC", 0x34FBD0}, // 44
    {"ARK_RENDERPASS_TEXTURE_STREAMER", 0x34FD00}, // 45
    {"ARK_RENDERPASS_IGGY_MANAGE_TEXTURES_PACKS", 0x31A690}, // 46
    {"ARK_RENDERPASS_IGGY_3D", 0x31A710}, // 47
    {"ARK_RENDERPASS_IGGY_FULLSCREEN", 0x31A8E0}, // 48
    {"ARK_RENDERPASS_IGGY_TEXTURES_SUBSTITUTIONS", 0x31AC40}, // 49
    {"ARK_RENDERPASS_POST_IGGY", 0x31AB10}, // 50
    {"ARK_RENDERPASS_OCCLUSION_QUERIES", 0x31D7E0}, // 51
}};
inline const char* render_pass_name(unsigned id) noexcept {
    return id<render_pass_count?render_passes[id].name:"untagged";
}
inline int render_pass_for_callback_rva(std::uintptr_t rva) noexcept {
    for(unsigned i=0;i<render_pass_count;++i) if(render_passes[i].callback_rva==rva) return static_cast<int>(i);
    return -1;
}
} // namespace d2vr
