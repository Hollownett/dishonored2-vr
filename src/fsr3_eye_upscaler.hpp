#pragma once
// Per-eye AMD FSR 3.1 upscaler wrapper (upscaling only, D3D11 backend).
// Link: third_party/fidelityfx/libs/*.lib (release libs need /MD, see THIRD_PARTY_NOTICES.md).
#include <d3d11_1.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#pragma warning(push, 0)
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#pragma warning(pop)

namespace d2vr {

struct Fsr3EyeUpscaler {
  struct Desc {
    uint32_t render_w = 0, render_h = 0;    // max render size
    uint32_t display_w = 0, display_h = 0;  // output size
    bool inverted_depth = true;             // reversed-Z, infinite far
    bool hdr = true;
    bool auto_exposure = true;
    bool mv_jittered = false;               // motion vectors already contain jitter
    bool debug_checks = false;
  };

  ~Fsr3EyeUpscaler() { destroy(); }
  Fsr3EyeUpscaler() = default;
  Fsr3EyeUpscaler(const Fsr3EyeUpscaler&) = delete;
  Fsr3EyeUpscaler& operator=(const Fsr3EyeUpscaler&) = delete;

  bool valid() const { return has_context_; }
  int phase_count() const { return phase_count_; }

  // (Re)creates the context. Call again to resize; no-op if desc unchanged.
  bool create(ID3D11Device* device, const Desc& d) {
    if (has_context_ && device == device_ && d.render_w == desc_.render_w && d.render_h == desc_.render_h &&
        d.display_w == desc_.display_w && d.display_h == desc_.display_h && d.inverted_depth == desc_.inverted_depth &&
        d.hdr == desc_.hdr && d.auto_exposure == desc_.auto_exposure && d.mv_jittered == desc_.mv_jittered &&
        d.debug_checks == desc_.debug_checks) return true;
    destroy();
    if (!device || !d.render_w || !d.render_h || !d.display_w || !d.display_h) return false;
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_1) return false;  // FSR needs 11_1
    size_t sz = ffxGetScratchMemorySizeDX11(1);
    scratch_ = std::calloc(sz, 1);
    if (!scratch_) return false;
    FfxFsr3ContextDescription cd{};
    if (ffxGetInterfaceDX11(&cd.backendInterfaceUpscaling, ffxGetDeviceDX11(device), scratch_, sz, 1) != FFX_OK) {
      std::free(scratch_); scratch_ = nullptr; return false;
    }
    cd.displaySize = {d.display_w, d.display_h};
    cd.maxUpscaleSize = {d.display_w, d.display_h};
    cd.maxRenderSize = {d.render_w, d.render_h};
    cd.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY;
    if (d.hdr) cd.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
    if (d.auto_exposure) cd.flags |= FFX_FSR3_ENABLE_AUTO_EXPOSURE;
    if (d.inverted_depth) cd.flags |= FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DEPTH_INFINITE;
    if (d.mv_jittered) cd.flags |= FFX_FSR3_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (d.debug_checks) {  // fpMessage MUST be set when checking is on (null call otherwise)
      cd.flags |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
      cd.fpMessage = [](FfxMsgType t, const wchar_t* m) { OutputDebugStringW(t == FFX_MESSAGE_TYPE_ERROR ? L"FSR3 error: " : L"FSR3 warn: "); OutputDebugStringW(m ? m : L""); OutputDebugStringW(L" | "); };
    }
    cd.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;  // frame-gen only; unused
    if (ffxFsr3ContextCreate(&ctx_, &cd) != FFX_OK) {
      std::free(scratch_); scratch_ = nullptr; return false;
    }
    desc_ = d;
    device_ = device;
    has_context_ = true;
    phase_count_ = ffxFsr3GetJitterPhaseCount((int32_t)d.render_w, (int32_t)d.display_w);
    if (phase_count_ <= 0) phase_count_ = 8;
    return true;
  }

  void destroy() {
    if (has_context_) ffxFsr3ContextDestroy(&ctx_);
    std::memset(&ctx_, 0, sizeof(ctx_));
    std::free(scratch_);
    scratch_ = nullptr;
    has_context_ = false;
    device_ = nullptr;
  }

  // Jitter in render-target pixels for frame index. Projection offset (NDC) is
  // x: 2*jx/render_w, y: -2*jy/render_h (FSR convention; verify sign vs engine).
  bool jitter(uint32_t frame, float& jx, float& jy) const {
    jx = jy = 0.f;
    if (!has_context_) return false;
    return ffxFsr3GetJitterOffset(&jx, &jy, (int32_t)(frame % (uint32_t)phase_count_), phase_count_) == FFX_OK;
  }

  struct Inputs {
    ID3D11Resource* colour = nullptr;   // render size, SRV-capable
    ID3D11Resource* depth = nullptr;    // R32_FLOAT / R32_TYPELESS (not D32 typeless w/o SRV bind)
    ID3D11Resource* motion = nullptr;   // R16G16_FLOAT, render size
    ID3D11Resource* output = nullptr;   // display size, SRV + UAV binds required by the DX11 backend
    ID3D11Resource* reactive = nullptr; // optional R8 render-size mask, 1 = do not trust history
    uint32_t render_w = 0, render_h = 0;
    float jitter_px_x = 0, jitter_px_y = 0;
    float mv_scale_x = 1, mv_scale_y = 1;
    bool reset = false;
    float frame_time_ms = 11.1f;
    float sharpness = -1.f;             // <0 disables RCAS
    float cam_near = 0.1f, cam_far = 1000.f, vfov_rad = 1.5f;
    // v1107 foveated upscaling (FSR4 only): the region of the sources read (origin; size render_w x render_h) and where the display-size
    // result is written in a larger output. All zero = the whole image, the output exactly display size.
    uint32_t src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
    bool output_region = false;
    float pre_exposure = 1.f;
  };

  // Records the upscale onto ctx (immediate or deferred). Overwrites CS/shader
  // state on ctx; caller must restore what it needs.
  bool dispatch(ID3D11DeviceContext* ctx, const Inputs& in) {
    if (!has_context_ || !ctx || !in.colour || !in.depth || !in.motion || !in.output) return false;
    const uint32_t rw = in.render_w ? in.render_w : desc_.render_w;
    const uint32_t rh = in.render_h ? in.render_h : desc_.render_h;
    if (!rw || !rh || rw > desc_.render_w || rh > desc_.render_h ||
        !std::isfinite(in.jitter_px_x) || !std::isfinite(in.jitter_px_y) ||
        !std::isfinite(in.mv_scale_x) || !std::isfinite(in.mv_scale_y)) return false;
    ID3D11Texture2D* output_texture{};
    if (FAILED(in.output->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&output_texture)))) return false;
    D3D11_TEXTURE2D_DESC output_desc{}; output_texture->GetDesc(&output_desc); output_texture->Release();
    const UINT output_binds = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if ((output_desc.BindFlags & output_binds) != output_binds || output_desc.Width != desc_.display_w ||
        output_desc.Height != desc_.display_h || output_desc.SampleDesc.Count != 1 || output_desc.ArraySize != 1) return false;
    FfxFsr3DispatchUpscaleDescription u{};
    u.commandList = ffxGetCommandListDX11(ctx);
    u.color = res(in.colour, FFX_RESOURCE_STATE_COMPUTE_READ);
    u.depth = res(in.depth, FFX_RESOURCE_STATE_COMPUTE_READ);
    u.motionVectors = res(in.motion, FFX_RESOURCE_STATE_COMPUTE_READ);
    u.upscaleOutput = res(in.output, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    if (in.reactive) u.reactive = res(in.reactive, FFX_RESOURCE_STATE_COMPUTE_READ);
    u.jitterOffset = {in.jitter_px_x, in.jitter_px_y};
    u.motionVectorScale = {in.mv_scale_x, in.mv_scale_y};
    u.renderSize = {in.render_w ? in.render_w : desc_.render_w, in.render_h ? in.render_h : desc_.render_h};
    u.upscaleSize = {desc_.display_w, desc_.display_h};
    u.enableSharpening = in.sharpness >= 0.f;
    u.sharpness = u.enableSharpening ? in.sharpness : 0.f;
    u.frameTimeDelta = in.frame_time_ms;
    u.preExposure = in.pre_exposure > 0.f ? in.pre_exposure : 1.f;
    u.reset = in.reset;
    // Luma swaps near/far for inverted depth.
    u.cameraNear = desc_.inverted_depth ? in.cam_far : in.cam_near;
    u.cameraFar = desc_.inverted_depth ? in.cam_near : in.cam_far;
    u.cameraFovAngleVertical = in.vfov_rad;
    u.viewSpaceToMetersFactor = 1.f;
    return ffxFsr3ContextDispatchUpscale(&ctx_, &u) == FFX_OK;
  }

 private:
  static FfxResource res(ID3D11Resource* r, FfxResourceStates s) {
    FfxResource o{};
    o.resource = r;
    o.state = s;
    o.description = GetFfxResourceDescriptionDX11(r);
    return o;
  }
  FfxFsr3Context ctx_{};
  ID3D11Device* device_ = nullptr; // identity only; the live FSR context owns its device resources
  Desc desc_{};
  void* scratch_ = nullptr;
  int phase_count_ = 8;
  bool has_context_ = false;
};

}  // namespace d2vr
