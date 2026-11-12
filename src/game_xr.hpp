#pragma once
// In-game frame bridge. Uses the game's existing D3D11 device; never clears its
// pipeline state or changes the registered runtime. Frame calls stay on Present.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "alternate_eye.hpp"
#include "frame_pacer.hpp"
#include "stale_eye_warp.hpp"
#include "warp_play_space.hpp"
#include "context_state.hpp"
#include "mod_gpu_timer.hpp"
#include "controller_input.hpp"
#include "render_size.hpp"
#include "canted_fov.hpp"
#include "visibility_mask.hpp"
#include "visibility_stamp.hpp"
#include "foveated_upscale.hpp"
#include "menu_panel.hpp"
#include "y_hold.hpp"
#include "xr_continuity.hpp"
#include "xr_layer_pose.hpp"
#include "xr_frame_hardening.hpp"
#include "eye_antialiasing.hpp"
#include "eye_sharpen.hpp"
#include "eye_resolve.hpp"
#include "desktop_mirror.hpp"
#include "settings_panel_image.hpp"
#include "settings_menu_layout.hpp"
#include "vr_preferences.hpp"
#include "menu_input.hpp"
#include "menu_pointer.hpp"
#include "controls_guide_image.hpp"
#include "guide_navigation.hpp"
#include "comfort_vignette.hpp"
#include "overlay_dirty.hpp"
#include "xr_cpu_spans.hpp"
#include "grab_beam.hpp"
#include "world_ui_pose.hpp"
#include "note_reading.hpp"
#include "note_eye_art.hpp"
#include "note_hand_mesh.hpp"
#include "note_hand_pose.hpp"
#include "native_arm_binding_snapshot.hpp"
#include "hud_window.hpp"
#include "wrist_hud_image.hpp"
#include "native_hud_fallback.hpp"
#include "native_eye_art.hpp"
#include "eye_reticle.hpp"
#include "spyglass_lens.hpp"
#include "spyglass_scope.hpp"
#include "head_calibration.hpp"
#include "xr_floor_datum.hpp"
#include "vr_settings_native.hpp"
#include <algorithm>
#include <array>
#include <utility>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>
#include <sstream>

namespace d2vr {
// Object-art destination for in-game tutorials: not object-anchored, so it is drawn on a
// panel locked ~1.2 m ahead of the head, slightly below eye level (publisher point ignored).
inline constexpr unsigned tutorial_art_slot=10;
// Processing provenance for the exact source passed to the synchronous base
// transfer observer. A kept eye inherits the work done when it was rendered.
struct EyeTransferProcessing {
    bool valid{},smaa_applied{},cas_applied{},kept{};
    int temporal_mode{};float cas_sharpness{};
};
inline std::optional<Vec3> head_locked_art_point(const XrPosef& left,const XrPosef& right,Vec3 offset={0,-.25,-1.2}) {
    try {
        const Vec3 head{(left.position.x+right.position.x)*.5,(left.position.y+right.position.y)*.5,(left.position.z+right.position.z)*.5};
        const auto q=normalized({left.orientation.x,left.orientation.y,left.orientation.z,left.orientation.w});
        const auto point=head+rotated(q,offset);
        if(!finite(point))return {};
        return point;
    }catch(...){return {};}
}
class GameXr {
    ContextState graphics_state_;
    std::unique_ptr<EyeAntialiasing> antialiasing_;
    bool antialiasing_enabled_{};
    std::unique_ptr<EyeSharpen> sharpen_;
    std::unique_ptr<EyeResolve> resolve_;
    std::array<EyeTransferProcessing,2> transfer_processing_{},alt_processing_{};
    std::unique_ptr<DesktopMirror> mirror_;
    VrSettings settings_;
    AvatarCalibration avatar_calibration_;
    std::uint64_t avatar_prompt_reference_epoch_{};
    std::optional<NativeAvatarMeasurements> avatar_measurements_;
    bool settings_enabled_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> settings_texture_;
    std::unique_ptr<DesktopMirror> settings_compositor_;
    std::vector<unsigned char> settings_pixels_;XrSwapchain settings_chain_{};std::vector<XrSwapchainImageD3D11KHR> settings_images_;
    bool settings_in_world_{}; // the panel was opened during play: a world-locked quad, not part of the eye image
    bool native_pointer_enabled_{};
    YHoldMenu y_hold_menu_;
public:
    bool wheel_native_pointer=true; // build/wheel-no-pointer: the wheel selects by stick only (pointer events switch it to keyboard glyphs)
private:
    XrSwapchain pointer_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> pointer_images_;
    XrSwapchain hint_chain_{};
    XrSwapchain dot_chain_{};std::vector<XrSwapchainImageD3D11KHR> dot_images_;std::vector<bool> dot_filled_;
    // Frame-submission hardening (xr_frame_hardening.hpp): an opaque-black chain made between frames for the "never an empty frame" fallback.
    static constexpr unsigned blank_texture_size=64;
    XrSwapchain blank_chain_{};std::vector<XrSwapchainImageD3D11KHR> blank_images_;std::vector<bool> blank_filled_;
    LastGoodProjection last_good_projection_;
    std::uint64_t last_good_scene_{};  // scene_id of the authored scene the last good projection came from (0: gameplay or none)
    EventLogLimiter chain_event_limiter_{120,40,10000},empty_event_limiter_{120,20,10000};
    UINT64 present_count_{};
    // Locomotion comfort vignette: a head-locked quad showing a black ring texture, created by prepare_overlays between frames.
    static constexpr unsigned vignette_texture_size=96; // not a multiple of 64: the wrist test tells wrist canvases apart by width
    XrSwapchain vignette_chain_{};std::vector<XrSwapchainImageD3D11KHR> vignette_images_;std::vector<bool> vignette_filled_;
    std::vector<std::uint8_t> vignette_ring_,vignette_baked_;
    XrSpace view_space_{}; // unused: the VIEW-space quad was rejected by SteamVR (POSE_INVALID); the ring is rebuilt from the head pose in LOCAL space
    bool color_scale_supported_{}; // XR_KHR_composition_layer_color_scale_bias enabled on the instance
    // v1105 probe (read-only): the runtime's extension list and, with XR_KHR_visibility_mask, each eye's hidden area, logged once to
    // xr-recoveries.log (and again after a mask-changed event). Nothing is drawn with the mask yet.
    std::string xr_extensions_;bool visibility_mask_supported_{};PFN_xrGetVisibilityMaskKHR xrGetVisibilityMaskKHR_{};
    bool extensions_logged_{};std::array<bool,2> visibility_mask_logged_{};
    // v1106: each eye's hidden mesh (tangent xy pairs + indices), kept for the depth stamp, and the inputs its geometry was built from
    std::array<std::vector<float>,2> mask_xy_{};std::array<std::vector<std::uint32_t>,2> mask_indices_{};
    std::array<std::array<float,4>,2> stamp_fov_{};bool stamp_canted_{};bool stamp_built_{};unsigned stamp_mask_epoch_{},stamp_built_epoch_{~0u};
    void update_visibility_stamp_() {
        if(!views_valid_ || !VisibilityStamp::enabled.load(std::memory_order_relaxed)) return;
        bool same=stamp_built_ && stamp_canted_==canted_views && stamp_built_epoch_==stamp_mask_epoch_;
        for(unsigned eye=0;eye<2 && same;++eye) {
            const auto& f=views_[eye].fov;const std::array<float,4> now{f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
            for(int k=0;k<4;++k) if(std::abs(now[k]-stamp_fov_[eye][k])>1e-5f) same=false;
        }
        if(same) return;
        if(!VisibilityStamp::create(device_for_stamp_())) return;
        for(unsigned eye=0;eye<2;++eye) {
            const auto& f=views_[eye].fov;stamp_fov_[eye]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
            RenderFrustum render{{0,0,0,1},std::tan(std::max(-f.angleLeft,f.angleRight)),std::tan(std::max(f.angleUp,-f.angleDown))};
            if(canted_views) if(const auto c=canted_fov(f.angleLeft,f.angleRight,f.angleUp,f.angleDown)) render={c->rotation,std::tan(c->half_x),std::tan(c->half_y)};
            const auto& xy=mask_xy_[eye];const auto& ix=mask_indices_[eye];
            VisibilityStamp::publish(eye,visibility_stamp_triangles(xy.empty()?nullptr:xy.data(),xy.size()/2,ix.empty()?nullptr:ix.data(),ix.size(),
                f.angleLeft,f.angleRight,f.angleUp,f.angleDown,render,.01));
        }
        stamp_canted_=canted_views;stamp_built_epoch_=stamp_mask_epoch_;stamp_built_=true;
    }
    // v1107: each eye's optical centre and fovea half extents in the render frustum's NDC, for FsrTaa's foveated FSR 4 box.
    void update_fovea_() {
        if(!views_valid_) return;
        const double t=std::tan(static_cast<double>(fovea::half_deg.load())*0.017453292519943295);
        for(unsigned eye=0;eye<2;++eye) {
            const auto& f=views_[eye].fov;
            RenderFrustum render{{0,0,0,1},std::tan(std::max(-f.angleLeft,f.angleRight)),std::tan(std::max(f.angleUp,-f.angleDown))};
            if(canted_views) if(const auto c=canted_fov(f.angleLeft,f.angleRight,f.angleUp,f.angleDown)) render={c->rotation,std::tan(c->half_x),std::tan(c->half_y)};
            const auto centre=visibility_mask_detail::project(render,0,0);
            if(std::isnan(centre[0])) return;
            fovea::centre_x[eye]=static_cast<float>(centre[0]/render.tan_x);fovea::centre_y[eye]=static_cast<float>(centre[1]/render.tan_y);
            fovea::half_x[eye]=static_cast<float>(t/render.tan_x);fovea::half_y[eye]=static_cast<float>(t/render.tan_y);
        }
        fovea::published=true;
    }
    ID3D11Device* device_for_stamp_() {Microsoft::WRL::ComPtr<ID3D11Device> d;if(context_) context_->GetDevice(&d);return d.Get();} // the game's device outlives the call
    void log_visibility_mask_() {
        if(!observe_xr_event || !views_valid_) return;
        if(!extensions_logged_) {extensions_logged_=true;observe_xr_event(("xr-probe runtime extensions: "+xr_extensions_).c_str());}
        for(std::uint32_t eye=0;eye<2;++eye) {
            if(visibility_mask_logged_[eye]) continue;
            visibility_mask_logged_[eye]=true;
            char line[512];
            if(!xrGetVisibilityMaskKHR_) {std::snprintf(line,sizeof line,"xr-probe %s eye: XR_KHR_visibility_mask unavailable",eye?"right":"left");observe_xr_event(line);continue;}
            XrVisibilityMaskKHR mask{XR_TYPE_VISIBILITY_MASK_KHR};
            const auto kind=XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;
            XrResult r=xrGetVisibilityMaskKHR_(session_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,kind,&mask);
            std::vector<XrVector2f> vertices(mask.vertexCountOutput);std::vector<std::uint32_t> indices(mask.indexCountOutput);
            if(XR_SUCCEEDED(r) && !vertices.empty() && !indices.empty()) {
                mask.vertexCapacityInput=static_cast<std::uint32_t>(vertices.size());mask.vertices=vertices.data();
                mask.indexCapacityInput=static_cast<std::uint32_t>(indices.size());mask.indices=indices.data();
                r=xrGetVisibilityMaskKHR_(session_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,kind,&mask);
            }
            if(XR_FAILED(r) || vertices.empty() || indices.empty()) {
                std::snprintf(line,sizeof line,"xr-probe %s eye: hidden mesh result %d, %u vertices, %u indices",eye?"right":"left",static_cast<int>(r),mask.vertexCountOutput,mask.indexCountOutput);
                observe_xr_event(line);continue;
            }
            const auto& f=views_[eye].fov;
            RenderFrustum render{{0,0,0,1},std::tan(std::max(-f.angleLeft,f.angleRight)),std::tan(std::max(f.angleUp,-f.angleDown))}; // plain symmetric frustum
            if(canted_views) if(const auto c=canted_fov(f.angleLeft,f.angleRight,f.angleUp,f.angleDown)) render={c->rotation,std::tan(c->half_x),std::tan(c->half_y)};
            static_assert(sizeof(XrVector2f)==2*sizeof(float));
            mask_xy_[eye].assign(&vertices[0].x,&vertices[0].x+2*vertices.size());mask_indices_[eye]=indices;++stamp_mask_epoch_;
            const auto s=visibility_mask_stats(&vertices[0].x,vertices.size(),indices.data(),indices.size(),f.angleLeft,f.angleRight,f.angleUp,f.angleDown,render);
            std::snprintf(line,sizeof line,"xr-probe %s eye: hidden mesh %zu vertices %zu triangles; hidden %.1f%% of the runtime FOV; %.1f%% of our %s render wasted (mask + outside FOV); "
                "mask x %.3f..%.3f y %.3f..%.3f; fov tan l %.3f r %.3f u %.3f d %.3f%s",eye?"right":"left",vertices.size(),indices.size()/3,s.hidden_of_fov*100,s.wasted_of_render*100,
                canted_views?"canted":"symmetric",s.min_x,s.max_x,s.min_y,s.max_y,std::tan(f.angleLeft),std::tan(f.angleRight),std::tan(f.angleUp),std::tan(f.angleDown),s.valid?"":" (stats invalid)");
            observe_xr_event(line);
        }
    }
    XrCompositionLayerColorScaleBiasKHR vignette_scale_{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR}; // chained to the quad; lives until xrEndFrame
    bool vignette_runtime_off_{}; // the runtime rejected a frame carrying the vignette: it stays off for this session instead of dropping frames until recovery
    float vignette_baked_opacity_{-1};UINT64 vignette_bake_tick_{};
    std::optional<Vec3> wheel_cursor_;
    bool cylinder_supported_{};XrCompositionLayerCylinderKHR hud_cylinder_{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    bool world_ui_stale_{},hud_window_stale_{};std::uint64_t world_ui_fresh_tick_{},hud_window_fresh_tick_{};
    // Plan E task 4: an overlay quad whose content did not change re-submits its last released image instead of acquire/draw/release.
    // Each kind has a gate (overlay_dirty.hpp); a swapchain's generation is bumped whenever it is (re)created, so the first frame of a new
    // chain, an epoch change, a failed render and a session restart always render. Knob build/no-overlay-skip draws every frame as before.
    std::array<OverlayDirty,overlay_kind_count> overlay_gate_{};
    std::unordered_map<XrSwapchain,std::uint64_t> chain_generations_;std::uint64_t chain_generation_counter_{};
    std::uint64_t settings_pixel_generation_{},world_ui_draw_tick_{},hud_window_draw_tick_{},unknown_draw_tick_{};
    std::uint64_t chain_generation(XrSwapchain chain) const {const auto at=chain_generations_.find(chain);return at==chain_generations_.end()?0:at->second;}
    // True: draw a fresh image now (the gate stays invalid until overlay_rendered(), so any throw or timeout between the two leaves it
    // dirty). False: re-submit the last released image (counted as a skip).
    bool overlay_needs_image(OverlayKind kind,XrSwapchain chain,std::uint64_t content,std::uint64_t max_hold_ms,OverlayKey& key) {
        auto& gate=overlay_gate_[static_cast<unsigned>(kind)];
        key={chain_generation(chain),pose_epoch(),content};
        if(!overlay_skip_enabled || gate.must_render(key,GetTickCount64(),max_hold_ms)) {gate.invalidate();return true;}
        overlay_stats.note(kind,false);return false;
    }
    void overlay_rendered(OverlayKind kind,const OverlayKey& key) {
        overlay_gate_[static_cast<unsigned>(kind)].rendered(key,GetTickCount64());overlay_stats.note(kind,true);
    }
    void invalidate_overlays() {for(auto& gate:overlay_gate_) gate.invalidate();}
public:
    OverlayStats overlay_stats;
    XrCpuSpans xr_spans; // measurement only: CPU time of each xr-facing step of the frame, both eyes summed
    struct SpanClock { // adds the elapsed CPU time to one span when it goes out of scope
        XrCpuSpans& spans;XrSpan span;std::int64_t start;
        SpanClock(XrCpuSpans& s,XrSpan which) noexcept:spans(s),span(which),start(cpu_time_ns()) {}
        ~SpanClock() {spans.add(span,cpu_time_ns()-start);}
        SpanClock(const SpanClock&)=delete;SpanClock& operator=(const SpanClock&)=delete;
    };
    static inline bool overlay_skip_enabled=true; // build/no-overlay-skip clears it before the first GameXr exists
private:
    std::array<XrSwapchain,2> wrist_chains_{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> wrist_images_;
    std::array<std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>>,2> wrist_targets_;
    std::array<bool,2> wrist_acquired_{};
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>,2> wrist_retirement_;
    std::array<bool,2> wrist_recreate_{};
    std::array<std::array<UINT,2>,2> wrist_want_{}; // canvas size wrist_layer needs; prepare_overlays creates it between frames
    Microsoft::WRL::ComPtr<ID3D11Query> close_retirement_;
    Microsoft::WRL::ComPtr<ID3D11Query> frame_fence_; // v840: GPU fence after the last xrEndFrame; present() waits on it (one frame in flight)
    bool frame_fence_pending_{};
    FramePacer pacer_; // v841: steady paired cadence and the period poses are predicted with (frame_pacer.hpp)
    DisplayHorizon display_horizon_; // v890: recent predicted display times; the median step is the locate horizon while unlocked
    std::array<UINT64,2> wrist_retry_ticks_{};
    std::array<UINT64,2> wrist_image_ticks_{};
    std::array<std::vector<std::uint8_t>,2> wrist_pixels_;
    WristHudSnapshot wrist_data_{};
    // v1015: the watch face is the game's own MasterHUD cluster. dxgi_probe captures the native health/mana/equipment
    // child draws, composites them at their original relative positions at the rival's measured crop, and publishes
    // one cluster texture here. The Watch copies that texture with the GPU alpha fade; no CPU image is generated.
    static constexpr UINT wrist_watch_canvas=512;
    struct WristWatchClusterSource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        UINT width{},height{};
        UINT64 epoch{},tick{},generation{};
        std::uintptr_t owner{};
        bool fresh(UINT64 current_epoch,UINT64 now) const noexcept {
            return texture && width && height && owner && epoch==current_epoch && tick && now>=tick && now-tick<=250;
        }
    };
    NativeHudFallback wrist_watch_blitter_;
    WristWatchClusterSource wrist_watch_cluster_;
    WristWatchState wrist_watch_state_;
    std::optional<WristWatchPose> wrist_watch_pose_;
    UINT64 wrist_watch_tick_{};
    double wrist_watch_alpha_{};
    bool wrist_watch_submitted_{};
    UINT64 wrist_watch_blit_rejects_{},wrist_watch_cluster_rejects_{},wrist_watch_draws_{},wrist_watch_cluster_updates_{};
    struct NativeArtSource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        D3D11_BOX crop{};Vec3 point{};double width{};
        UINT64 epoch{},tick{};std::uintptr_t owner{};bool premultiplied{true};
        bool locked{};Vec3 lock_offset{}; // v710: head-locked at this head-space offset, resolved at draw time (no pose lag)
        bool fresh(UINT64 current_epoch,UINT64 now) const noexcept {return texture && owner && epoch==current_epoch && now>=tick && now-tick<=250;}
    };
    std::array<NativeArtSource,3> native_wrist_art_; // left ammo, right health, right mana
    std::array<std::array<UINT,2>,2> native_wrist_sizes_{};
    std::array<NativeArtSource,33> native_object_art_; // prompt, then bounded objective markers
    std::array<unsigned,33> native_object_art_masks_{};
    std::array<bool,3> native_wrist_art_shown_{};
    std::unique_ptr<NativeEyeArt> native_eye_art_;
    std::unique_ptr<NoteEyeArt> note_eye_art_;
    std::array<NoteHandReceipt,2> note_hands_{};
    std::array<NoteHandReceipt,2> wheel_hands_{};bool wheel_eye_art_disabled_{}; // v776: the hands the hand wheel's ring goes behind; a ring eye-art fault falls back to the quad layer
    EyeReticleFrames eye_reticles_;
    struct CrosshairArt {Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_BOX crop{};float anchor_x{},anchor_y{},scale{1.f};UINT64 epoch{},tick{};};
    CrosshairArt crosshair_art_;
    bool eye_reticle_failed_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> eye_reticle_texture_;
    std::vector<XrSwapchainImageD3D11KHR> hint_images_;
    std::vector<std::uint64_t> hint_revisions_;
    std::array<std::vector<std::uint8_t>,14> guide_pages_;
    std::uint64_t guide_art_generation_{},hint_revision_{};int hint_page_{-1},hint_character_{-1};
    std::atomic<int> guide_page_{0};std::atomic<bool> guide_refresh_{true};
    GuideNavigation guide_navigation_;
    // The controls and gestures page (controls_guide_image.hpp, user after v646): open while guide_open_, placed once in front of the head.
    std::atomic<bool> guide_open_{false};bool guide_placed_{},guide_from_menu_{};TrackedPose guide_pose_{}; // from_menu: opened from VR Settings, closes when play resumes
    MenuTriggerGate menu_trigger_gate_;
    std::optional<double> resolution_change_;
    std::optional<TurnControl::Config> turning_change_;
    std::optional<int> temporal_change_,hud_change_;
    std::filesystem::path preferences_file_;
    // G5/C6: grip-to-palm offsets per side and OpenXR interaction profile. Everything here is touched from the XR update and the render
    // thread, so it sits behind one mutex; file errors never leave this class (the hands keep the defaults and status says so).
    mutable std::mutex palm_mutex_;
    PalmPreviewMailbox palm_preview_; // the hand-fit preview, published by the XR thread after every settings change; palm_offset() reads this, never settings_
    PalmCalibration palm_;       // the store plus each hand's interaction profile (and a fit deferred until the profile is named)
    std::filesystem::path palm_file_;
    ULONGLONG profile_refresh_tick_{};
    PalmErrorLatch palm_error_;  // a load/confirm error reads as cleared after the next successful save
    std::chrono::steady_clock::time_point settings_update_time_{};
    double palm_fit_dt_{};
    double effective_scale_=1;
    HMODULE loader_{};
    XrInstance instance_{XR_NULL_HANDLE};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace local_{XR_NULL_HANDLE},floor_space_{XR_NULL_HANDLE};
    std::optional<double> calibration_floor_y_;
    UINT64 grip_pose_tick_{};
    std::array<XrSwapchain,2> chains_{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> images_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    PFN_xrGetInstanceProcAddr get_{};
#define XR_MEMBER(name) PFN_##name name{};
    XR_MEMBER(xrDestroyInstance) XR_MEMBER(xrDestroySession) XR_MEMBER(xrDestroySpace)
    XR_MEMBER(xrDestroySwapchain) XR_MEMBER(xrPollEvent) XR_MEMBER(xrBeginSession)
    XR_MEMBER(xrEndSession) XR_MEMBER(xrWaitFrame) XR_MEMBER(xrBeginFrame)
    XR_MEMBER(xrEndFrame) XR_MEMBER(xrLocateViews) XR_MEMBER(xrAcquireSwapchainImage)
    XR_MEMBER(xrWaitSwapchainImage) XR_MEMBER(xrReleaseSwapchainImage)
    XR_MEMBER(xrStringToPath) XR_MEMBER(xrCreateActionSet) XR_MEMBER(xrDestroyActionSet)
    XR_MEMBER(xrCreateAction) XR_MEMBER(xrSuggestInteractionProfileBindings) XR_MEMBER(xrAttachSessionActionSets)
    XR_MEMBER(xrCreateActionSpace) XR_MEMBER(xrSyncActions) XR_MEMBER(xrGetActionStatePose)
    XR_MEMBER(xrGetActionStateFloat) XR_MEMBER(xrGetActionStateVector2f) XR_MEMBER(xrGetActionStateBoolean)
    XR_MEMBER(xrLocateSpace) XR_MEMBER(xrApplyHapticFeedback)
#undef XR_MEMBER
    PFN_xrGetCurrentInteractionProfile xrGetCurrentInteractionProfile{};PFN_xrPathToString xrPathToString{}; // optional: a runtime without them keeps the suggested profile
    UINT width_{},height_{};
    DXGI_FORMAT format_{};
    std::array<EyeSize,2> maximum_eye_sizes_{};
    EyeSize system_maximum_{};
    MenuPanel menu_panel_;
    std::uint64_t native_rows_tick_{}; // next publication of the native options page rows
    std::uint64_t upscale_due_{};      // the Upscaler scale slider fires per step: the render-size request waits until it has been quiet
    bool menu_grip_was_{};
    MenuPanel authored_panel_{2.5};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> world_ui_source_;
    unsigned world_ui_kind_{};int wheel_hand_{-1};unsigned wheel_open_gen_{};mutable unsigned wheel_anchor_gen_{~0u};mutable std::optional<TrackedPose> wheel_anchor_;mutable std::mutex wheel_anchor_m_;bool world_ui_full_{},world_ui_dialogue_{};std::optional<Vec3> world_ui_speaker_{};double world_ui_dialogue_m_{1.6};
    // v721 (user, headset v720: the dialogue choice looked like a theater screen; "the dialogue window must be inside the game",
    // chosen: "in the world at the person you talk to"): choices stand in the scene at conversation distance, without the backdrop.
    static constexpr double dialogue_distance_m=1.6,dialogue_drop_m=.2;

    std::optional<NotePanelReceipt> note_panel_;
    NoteTouchScroll note_touch_scroll_;
    float note_scroll_axis_{};
    bool note_tablet_enabled_{};
    std::uint64_t note_generation_{};
    std::shared_ptr<const rig::NativeArmBindingSnapshot> note_binding_;
    std::array<std::shared_ptr<const note_hand::Mesh>,2> note_models_;
    std::array<std::unique_ptr<NoteHandPose>,2> note_posers_;
    std::array<note_hand::Geometry,2> note_geometry_;
    bool note_models_active_{};
    std::optional<Vec3> note_index_tip_;

    XrSwapchain world_ui_chain_{};
    ChainSizes chain_sizes_; // every live swapchain's size: the submit guard checks quad/cylinder sub-rects against it
    XrSwapchain menu_band_chain_{};std::vector<XrSwapchainImageD3D11KHR> menu_band_images_;UINT menu_band_width_{},menu_band_height_{}; // the menu/cutscene panel's 16:9 band
    std::vector<XrSwapchainImageD3D11KHR> world_ui_images_;
    UINT world_ui_width_{},world_ui_height_{}; // the chain's size: width x the displayed rows
    bool world_ui_pose_valid_{};
    TrackedPose world_ui_pose_{};
    // HUD window: gameplay HUD (prompts, tutorials, notifications) captured into one offscreen
    // surface by the native HUD hook and shown as a lazy-following, world-up panel.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hud_window_source_;
    XrSwapchain hud_window_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> hud_window_images_;
    UINT hud_window_width_{},hud_window_height_{};
    HudWindow hud_window_;HudWindowConfig hud_window_config_;
    std::chrono::steady_clock::time_point hud_window_time_{};

    XrContinuity continuity_;
    bool running_=false,frame_open_=false,views_valid_=false;
    XrFrameState frame_{};
    std::array<XrView,2> views_{};
    std::array<XrView,2> rendered_views_{};
    std::array<XrTime,2> rendered_display_times_{};
    std::array<std::int64_t,2> rendered_locate_cpu_times_{};
    std::int64_t locate_cpu_time_{};
    XrTime locate_display_time_{};
    XrViewStateFlags located_view_flags_{};
    UINT rendered_mask_{};
    // alternate-eye (plan F task 1): the kept image + view of each eye, written only while alternate_enabled. Records carry the exact view
    // (pose + FOV) an image was rendered with, so the eye that was not rendered this frame is submitted with its own render pose.
    std::array<EyeRecord<XrView>,2> alt_records_{};
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> alt_kept_{};
    // v792: the target view of each eye for the Present stale_target_frame_; the Present that warped it; the warp back (kept image ->
    // target), which moves the reticle point drawn for the kept frame into the warped image.
    std::array<XrView,2> stale_target_{};
    std::array<std::uint64_t,2> stale_target_frame_{},stale_warped_frame_{};
    std::array<WarpMatrix,2> stale_back_{};
    std::unique_ptr<StaleEyeWarp> uncant_;                  // v808: canted eye -> its own located view (its own output, apart from stale_warp_'s)
    std::unique_ptr<SpyglassLens> spyglass_lens_;           // v880: the R3 spyglass lens, made on the first zoom
    std::unique_ptr<SpyglassScopeMask> spyglass_scope_mask_; // v1040: the right-eye scope's mask, made on the first zoom
    std::array<WarpMatrix,2> uncant_back_{};std::array<bool,2> uncanted_{}; // this frame: turned image point -> submitted image point
    std::unique_ptr<StaleEyeWarp> stale_warp_;
public:
    std::string stale_warp_error; // v1197: the first 300 characters of the exception that turned the warp off (exception text, HRESULT, shader log)
    const Afw2Pass* afw2_pass() const noexcept {return stale_warp_ && stale_warp_->afw2?stale_warp_->afw2.get():nullptr;} // v1197: read-only (stage_ms)
private:
    bool stale_warp_failed_{};
    // v796: the game camera of each eye's image shown by a Present (and of each kept image), each eye's target game camera and kept depth
    // handed over for the Present in the *_frame_ stamps, and the parallax of each eye's last warp (the reticle follows it).
    std::array<WarpCamera,2> rendered_cameras_{},kept_cameras_{},stale_target_cameras_{};
    std::array<WarpPlaySpace,2> rendered_play_{},kept_play_{},stale_target_play_{}; // v1184: each camera's play space (warp_play_space.hpp)
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> stale_rig_target_{};std::array<UINT,2> stale_rig_target_w_{},stale_rig_target_h_{}; // v1190
    std::array<std::uint64_t,2> stale_rig_target_frame_{};std::array<bool,2> stale_rig_target_valid_{};
    std::array<std::uint64_t,2> rendered_camera_frame_{},stale_target_camera_frame_{},stale_depth_frame_{};
    std::array<bool,2> kept_camera_valid_{};
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> stale_depth_{};
    std::array<UINT,2> stale_depth_width_{},stale_depth_height_{};
    // v1151: each eye's motion texture (FsrTaa::eye_motion of its last render, like stale_depth_) and the Present it was handed in
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> stale_motion_{};std::array<UINT,2> stale_motion_width_{},stale_motion_height_{};
    std::array<std::uint64_t,2> stale_motion_frame_{};
    // v1160: each eye's rig mask of its last render (FsrTaa::eye_rig, v1163), handed in like the motion textures
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> stale_rig_{};std::array<UINT,2> stale_rig_width_{},stale_rig_height_{};
    std::array<std::uint64_t,2> stale_rig_frame_{};std::array<bool,2> stale_rig_live_{},stale_rig_motion_{}; // v1162: a rig mask of a one-view render (rig_mask.hpp)
    std::array<std::optional<StaleLayers>,2> stale_layers_{}; // v797: scene, player and hand layers of each eye's last warp
    std::array<std::array<double,2>,2> stale_camera_motion_{}; // v1177: per eye, the kept -> target camera step (m) and turn (deg) of its last layers
    bool stale_warped_(UINT eye) const noexcept {return eye<2 && alternate_frame && stale_warped_frame_[eye]==alternate_frame;}
    enum InputAction : UINT {Grip,Aim,Stick,Trigger,Squeeze,FirstButton,SecondButton,StickClick,Menu,InputCount};
    XrActionSet action_set_{XR_NULL_HANDLE};
    std::array<XrAction,InputCount> actions_{};
    XrAction haptic_action_{XR_NULL_HANDLE};
    // Optional capacitive-touch booleans (visual finger curl). Any failure leaves them null/unbound and the controls above untouched.
    enum TouchAction : UINT {TriggerTouch,ThumbstickTouch,FirstTouch,SecondTouch,ThumbrestTouch,TouchCount};
    std::array<XrAction,TouchCount> touch_actions_{};
    bool touch_bound_{},touch_create_failed_{};
    std::array<XrPath,2> hand_paths_{};
    std::array<std::array<XrSpace,2>,2> hand_spaces_{};
    bool controller_enabled_{};
    bool tracked_gameplay_required_{};
    void update_settings_image(std::optional<PanelPointer> pointer) {
        if(!settings_texture_) {
            Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
            D3D11_TEXTURE2D_DESC desc{};desc.Width=640;desc.Height=560;desc.MipLevels=1;desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&settings_texture_))) throw std::runtime_error("Cannot create native menu settings image");
            settings_compositor_=std::make_unique<DesktopMirror>(device.Get());
        }
        {auto next=settings_panel_image(settings_,effective_scale_,width_,height_,pointer);
         if(next!=settings_pixels_) {settings_pixels_=std::move(next);++settings_pixel_generation_;}}
        const auto& pixels=settings_pixels_;
        context_->UpdateSubresource(settings_texture_.Get(),0,nullptr,pixels.data(),640*4,0);
        if(observe_settings) observe_settings(settings_texture_.Get(),pixels.data(),pixels.size());
    }
    TrackedPose head_pose_now() const {
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        return {{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}};
    }
    const TrackedPose& panel_pose(MenuPanel& panel) {return panel.place(head_pose_now());}
    // The settings panel opened during play stays where it was placed when it opened (world-locked); menus follow the head gently.
    const TrackedPose& native_menu_pose() {return settings_in_world_?menu_panel_.hold(head_pose_now()):panel_pose(menu_panel_);}
    // Gameplay settings panel: its own 640x560 quad (like the paused menu's band, at the same pose) so the pointer hits what is drawn.
    void ensure_settings_chain() {
        if(!settings_enabled_ || settings_chain_) return;
        XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=640;info.height=560;
        info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        create_overlay_chain(info,settings_chain_,"Create settings panel");
        auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
        checked(enumerate(settings_chain_,0,&count,nullptr),"Count settings panel images");
        settings_images_.resize(count);for(auto& image:settings_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        checked(enumerate(settings_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(settings_images_.data())),"Read settings panel images");
        if(settings_images_.empty()) throw std::runtime_error("XR settings panel has no images");
    }
    std::optional<XrCompositionLayerQuad> settings_layer() {
        if(!settings_chain_ || settings_pixels_.size()!=640u*560u*4u || !views_valid_) return {}; // chain is created between frames
        OverlayKey overlay_key;
        if(overlay_needs_image(OverlayKind::settings,settings_chain_,settings_overlay_content(settings_pixel_generation_),0,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(settings_chain_,&acquire,&index),"Acquire settings panel");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(settings_chain_,&wait);
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        if(XR_FAILED(ready) || ready==XR_TIMEOUT_EXPIRED) {xrReleaseSwapchainImage(settings_chain_,&release);checked(ready,"Wait settings panel");throw std::runtime_error("Settings panel wait timed out");}
        context_->UpdateSubresource(settings_images_.at(index).texture,0,nullptr,settings_pixels_.data(),640*4,0);
        checked(xrReleaseSwapchainImage(settings_chain_,&release),"Release settings panel");
        overlay_rendered(OverlayKind::settings,overlay_key);
        }
        const auto& panel=native_menu_pose();const auto view=menu_view();
        const auto fit=SettingsMenuLayout::quad_fit(view.width,view.height);
        const auto centre=panel.position+rotated(panel.orientation,{fit.dx,fit.dy,0});
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(panel.orientation.x),static_cast<float>(panel.orientation.y),static_cast<float>(panel.orientation.z),static_cast<float>(panel.orientation.w)};
        layer.pose.position={static_cast<float>(centre.x),static_cast<float>(centre.y),static_cast<float>(centre.z)};
        layer.size={static_cast<float>(fit.width),static_cast<float>(fit.height)};
        layer.subImage.swapchain=settings_chain_;layer.subImage.imageRect.extent={640,560};return layer;
    }
    // Wheel .8 m (fixed in front), notes and black market .45 m (held in the left hand), journal 1 m.
    // VR settings "HUD size" scales every UI element alike (65% = 1): world UI, prompts, markers, wrist, HUD screen.
public:
    // Notes, and the black market (the only full-height journal-kind panel: logic 1024) while the left hand is tracked, are held in
    // that hand like a page (headset 2026-10-14: "render the Black Market on the left arm like notes"). Untracked, the shop stays 1 m ahead.
    bool note_composition_ready() const noexcept {
        const auto now=GetTickCount64();
        return note_tablet_enabled_ && note_models_active_ && note_binding_ && world_ui_kind_==2 && world_ui_source_ && note_panel_ &&
            !alternate_enabled && grip_tracked_hands==3 &&
            note_panel_->current(now,pose_epoch(),reference_epoch(),alternate_frame) &&
            note_hands_[0].generation==note_panel_->generation && note_hands_[1].generation==note_panel_->generation &&
            note_hands_[0].current(now,pose_epoch(),reference_epoch(),alternate_frame) &&
            note_hands_[1].current(now,pose_epoch(),reference_epoch(),alternate_frame);
    }
    bool world_ui_in_hand() const noexcept {return (grip_tracked_hands&1u) &&
        (world_ui_kind_==2 || (world_ui_kind_==3 && world_ui_full_));}
    std::uint64_t note_eye_draws{};
    // v776 (headset v775: "the hands must still be rendered", the ring's quad layer covered them): the dual-wield hand wheel is drawn INTO each eye like a note
    // page (NoteEyeArt ring mode: the movie's dim layer un-composited, behind the hands in front of it) and then has no quad layer; a fault in that draw
    // falls back to the quad. wheel_eye_*: ring draws (per eye), draws with a hand over the ring, hands one frame old (kept so a missing frame never flickers), faults.
    UINT64 wheel_eye_draws{},wheel_eye_occluded{},wheel_eye_grace{},wheel_eye_failures{};
    // v880: the spyglass (R3) lens (spyglass_lens.hpp): the game's zoom as a magnification, set per frame by the Setup hook (1 = no lens); draws, faults.
    float spyglass_magnification=1.f;UINT64 spyglass_lens_draws{},spyglass_lens_failures{};
    // v1040: the right-eye scope (spyglass_scope.hpp): the zoom factor the right eye's image shown at this Present was rendered with (1 = no scope),
    // set by the probe from the Setup that rendered that image; draws, faults (a fault turns the mask off for the session).
    float spyglass_scope_mag=1.f;UINT64 spyglass_scope_draws{},spyglass_scope_failures{};
    bool unpaired_flat_frame{};   // v1248: this present() is the unpaired-gameplay flat fallback (gameplay passed false): an open VR settings panel is kept, not suspended
    int wheel_hand() const noexcept {return wheel_hand_;} // the hand the wheel is at, -1 none
    bool wheel_eye_art_enabled=true; // build/wheel-quad: the hand wheel keeps its quad layer (the headset A/B for the ring in the eyes)
    bool wheel_eye_ready() const noexcept {
        return wheel_eye_art_enabled && hand_wheel_mode.load() && world_ui_kind_==1 && world_ui_source_ && !alternate_enabled && !wheel_eye_art_disabled_; // (a stale frame redraws the last capture, as the quad re-shows its last image)
    }
    void accept_wheel_hands(std::array<NoteHandReceipt,2> hands) {wheel_hands_=std::move(hands);}
    void enable_note_tablet(bool enabled) {note_tablet_enabled_=enabled;}
    void note_generation(std::uint64_t generation) {
        note_generation_=generation;
    }
    const auto& note_model_owner() const {return note_binding_;}
    void note_models(std::shared_ptr<const rig::NativeArmBindingSnapshot> binding,
                     std::array<std::shared_ptr<const note_hand::Mesh>,2> meshes,const std::array<double,2>& scales) {
        note_binding_.reset();note_models_={};note_posers_={};note_models_active_=false;
        note_panel_.reset();note_hands_={};note_geometry_={};note_index_tip_.reset();note_scroll_axis_=0;note_touch_scroll_={};
        if(!binding || !meshes[0] || !meshes[1])return;
        std::array<std::unique_ptr<NoteHandPose>,2> posers;
        for(unsigned side=0;side<2;++side) {
            posers[side]=std::make_unique<NoteHandPose>(binding->bindings[side],scales[side]);
        }
        note_posers_=std::move(posers);note_models_=std::move(meshes);note_binding_=std::move(binding);
    }
    float note_scroll_axis() const noexcept {return note_scroll_axis_;}
    std::optional<NotePanelReceipt> note_displayed_panel() const {return note_panel_;}
    const note_hand::Geometry* note_drawn_hand(unsigned side) const {return note_models_active_ && side<2?&note_geometry_[side]:nullptr;}
    std::optional<Vec3> note_index_tip() const {return note_index_tip_;}
    const NoteHandPose* note_poser(unsigned side) const {return side<2?note_posers_[side].get():nullptr;}
    const note_hand::Mesh* note_mesh(unsigned side) const {return side<2?note_models_[side].get():nullptr;}
    std::optional<NotePanelReceipt> note_target_panel(std::uint64_t frame) {
        if(!note_tablet_enabled_ || !note_binding_ || world_ui_kind_!=2 || !world_ui_source_ ||
           !world_ui_in_hand() || grip_tracked_hands!=3 || settings_.open() ||
           state!=XR_SESSION_STATE_FOCUSED || last_action_sync!=XR_SUCCESS)return {};
        const auto size=note_dimensions();
        return NotePanelReceipt{world_ui_pose(),size[0],size[1],GetTickCount64(),pose_epoch(),reference_epoch(),frame,note_generation_};
    }
    // Geometry is an occlusion/contact witness of the original native draw;
    // it is never drawn as a second pair of arms.
    void accept_native_note(const NotePanelReceipt& panel,std::array<note_hand::Geometry,2> geometry,Vec3 tip) {
        std::array<NoteHandReceipt,2> receipts{};
        for(unsigned side=0;side<2;++side)
            receipts[side]={note_hand::triangles(geometry[side]),panel.tick,panel.pose_epoch,panel.reference_epoch,panel.frame,panel.generation};
        note_geometry_=std::move(geometry);note_hands_=std::move(receipts);note_panel_=panel;note_index_tip_=tip;
    }
    std::array<double,2> note_dimensions() const {
        const double width=.45*std::min(hud_scale(),100.f/65.f);
        if(world_ui_kind_==2 && world_ui_source_) {D3D11_TEXTURE2D_DESC d{};world_ui_source_->GetDesc(&d);
            if(d.Width) return {width,width*world_ui_rows(d)/d.Width};}
        return {width,width*menu_band};
    }
    std::array<unsigned,2> world_ui_size() const {if(!world_ui_source_) return {};D3D11_TEXTURE2D_DESC d{};world_ui_source_->GetDesc(&d);return {d.Width,d.Height};}
    float hud_scale() const noexcept {return static_cast<float>(settings_.hud_percent())/65.f;}
    // v688: one angular scale for native HUD art. Metres of art width per (stage fraction x depth) at which an element keeps
    // the share of the HUD screen the flat game gives it (the eye draw multiplies by hud_scale(), divided out here).
    double hud_stage_metres_per_depth() const noexcept {
        const double hfov=(views_[0].fov.angleRight-views_[0].fov.angleLeft)*57.29577951308232;
        return 2*std::tan(hud_screen_degrees(hfov,settings_.hud_percent())*.5*0.017453292519943295)/hud_scale();
    }
    // Rows of the world UI image shown: the whole image (wheel, black market) or the centred 16:9 band.
    std::int32_t world_ui_rows(const D3D11_TEXTURE2D_DESC& d) const noexcept {
        return world_ui_full_?static_cast<std::int32_t>(d.Height):static_cast<std::int32_t>(std::clamp(std::lround(d.Width*menu_band),1l,static_cast<long>(d.Height)));
    }
    float world_ui_width() const noexcept {return (world_ui_kind_==1?(wheel_hand_pose()?static_cast<float>(wheel_hand_width_m):.8f):world_ui_kind_==2 || world_ui_in_hand()?.45f:world_ui_dialogue_?static_cast<float>(world_ui_dialogue_m_/1.05):1.f)*
        std::min(hud_scale(),100.f/65.f);} // panels stop at the 100% size; dialogue choices keep the old panel's angular size
    // Dual wield: side 0/1 draws the open wheel at that hand (set by the hand that opened it); -1, the default, is the head-front wheel.
    void set_wheel_hand(int side) noexcept {const int h=side==0||side==1?side:-1;if(h!=wheel_hand_) ++wheel_open_gen_;wheel_hand_=h;}
    // The open wheel's drawn quad in local_ space (at the hand, or head-front when that hand is untracked), for the pointer; none when no wheel.
    std::optional<WheelPlane> wheel_plane() const noexcept {
        if(world_ui_kind_!=1 || !views_valid_) return {};
        try {
            if(const auto at_hand=wheel_hand_pose()) return wheel_plane_of(*at_hand,world_ui_width());
            return wheel_plane_of(world_ui_pose_valid_?world_ui_pose_:world_ui_front_pose(eye_centre(),.9),world_ui_width());
        } catch(const std::exception&) {return {};}
    }
private:
    TrackedPose eye_centre() const {
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        return {{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}};
    }
    // The wheel ring in front of set_wheel_hand's hand: centred on its aim ray, wheel_hand_ahead_m (x the HUD size factor, as the quad's width) from the
    // aim origin, facing the eyes. v776 (user, headset v775): the hand chooses by pointing at the ring, so it stays where it was when the wheel opened and
    // is placed once per opening (wheel_open_gen_). A hand whose aim is untracked has no ring (the caller falls back).
    std::optional<TrackedPose> wheel_hand_pose() const noexcept {
        if(wheel_hand_<0 || world_ui_kind_!=1 || !views_valid_) return {};
        std::lock_guard<std::mutex> lock(wheel_anchor_m_);
        if(wheel_anchor_gen_==wheel_open_gen_ && wheel_anchor_) return wheel_anchor_;
        constexpr XrSpaceLocationFlags valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        const auto& aim=aim_locations[wheel_hand_];if((aim.locationFlags&valid)!=valid) return {};
        try {const auto& p=aim.pose.position;const auto& q=aim.pose.orientation;
            wheel_anchor_=wheel_aim_pose(eye_centre().position,{{p.x,p.y,p.z},{q.x,q.y,q.z,q.w}},std::min(hud_scale(),100.f/65.f)); // the factor of world_ui_width()
            wheel_anchor_gen_=wheel_open_gen_;return wheel_anchor_;}
        catch(const std::exception&) {return {};}
    }
    const TrackedPose& world_ui_pose() {
        const TrackedPose head=eye_centre();
        if(world_ui_kind_==2 && note_tablet_enabled_ && note_posers_[0]) try { // a held note is a tablet in the left palm
            const auto& p=grip_locations[0].pose;const Quaternion q{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
            const WorldTrackedPose grip{{p.position.x,p.position.y,p.position.z},{rotated(q,{0,0,-1}),rotated(q,{-1,0,0}),rotated(q,{0,1,0})}};
            const auto size=note_dimensions();
            world_ui_pose_=note_posers_[0]->page(apply_palm_offset(grip,palm_offset(0),true),size[0],size[1]);world_ui_pose_valid_=false;return world_ui_pose_;
        } catch(const std::exception&) {} // untracked/degenerate palm: the plain held placement below
        if(world_ui_in_hand()) { // notes and the black market follow the left hand every frame
            const auto& g=grip_locations[0].pose.position; // the taller shop page rests its bottom edge on the hand, as a note does (.12 m)
            world_ui_pose_=world_ui_hand_pose(head.position,{g.x,g.y,g.z},world_ui_kind_==2?.12:displayed_menu_view().height*.5);world_ui_pose_valid_=false;return world_ui_pose_;
        }
        if(const auto at_hand=wheel_hand_pose()) {world_ui_pose_=*at_hand;world_ui_pose_valid_=false;return world_ui_pose_;} // the wheel follows its hand
        if(world_ui_pose_valid_) return world_ui_pose_;
        if(world_ui_dialogue_ && world_ui_speaker_) { // v722: at the person spoken to, where their Talk prompt stood
            const auto [pose,distance]=world_ui_speaker_pose(head,*world_ui_speaker_);world_ui_pose_=pose;world_ui_dialogue_m_=distance;
        } else {
            world_ui_pose_=world_ui_front_pose(head,world_ui_kind_==1?.9:world_ui_dialogue_?dialogue_distance_m:1.05);
            if(world_ui_dialogue_) {world_ui_pose_.position.y-=dialogue_drop_m;world_ui_dialogue_m_=dialogue_distance_m;}
        }
        world_ui_pose_valid_=true;return world_ui_pose_;
    }
    // SteamVR fails xrEndFrame for a swapchain created (or destroyed) inside a frame: every overlay chain is built by prepare_overlays()
    // between frames; a layer whose chain is missing or the wrong size shows nothing this frame.
    void count_chain_op() {if(frame_open_) ++swapchain_creates_in_frame;}
    // Wall-clock stamps (UTC and local) so a vrserver.txt / driver_vrlink.txt line can be matched without offset arithmetic.
    static std::pair<std::string,std::string> wall_stamps_() {
        SYSTEMTIME utc{},local{};GetSystemTime(&utc);GetLocalTime(&local);
        return {format_clock(utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds),format_clock(local.wHour,local.wMinute,local.wSecond,local.wMilliseconds)};
    }
    void log_chain_event_(bool create,const char* name,std::uint32_t width,std::uint32_t height,XrSwapchain chain) {
        if(create) ++chain_create_events;else ++chain_destroy_events;
        if(!observe_xr_event) return;
        const auto decision=chain_event_limiter_.admit(GetTickCount64());if(!decision.emit) return;
        if(decision.final_notice) {observe_xr_event("xr-event swapchain log cap reached; further create/destroy lines are counted in xr-live-status.json only");return;}
        const auto [utc,local]=wall_stamps_();
        observe_xr_event(format_chain_event(create,name,width,height,frame_open_,reinterpret_cast<std::uintptr_t>(chain),utc,local,decision.suppressed_before).c_str());
    }
    void create_overlay_chain(const XrSwapchainCreateInfo& info,XrSwapchain& chain,const char* name) {
        count_chain_op();checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&chain),name);chain_sizes_[chain]={info.width,info.height};chain_generations_[chain]=++chain_generation_counter_;
        log_chain_event_(true,name,info.width,info.height,chain);
    }
    void destroy_overlay_chain(XrSwapchain chain,const char* name) {
        count_chain_op();std::array<std::uint32_t,2> size{};if(const auto known=chain_sizes_.find(chain);known!=chain_sizes_.end()) size=known->second;
        log_chain_event_(false,name,size[0],size[1],chain); // logged before the call: a destroy that hangs the runtime is still on record
        chain_sizes_.erase(chain);chain_generations_.erase(chain);checked(xrDestroySwapchain(chain),name);
    }
    // A quad/cylinder swapchain is exactly the size of what it shows (SteamVR rejects sub-rects): rebuilt here, between frames, when that size changes.
    void rebuild_overlay_chain(XrSwapchain& chain,std::vector<XrSwapchainImageD3D11KHR>& images,UINT& have_width,UINT& have_height,UINT width,UINT height,const std::string& label) {
        if(chain && have_width==width && have_height==height) return;
        if(chain) drain_gpu_for_surface();
        images.clear();if(chain) destroy_overlay_chain(chain,("Destroy old "+label).c_str());
        chain=XR_NULL_HANDLE;
        XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;info.format=format_;
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.width=width;info.height=height;info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        create_overlay_chain(info,chain,("Create "+label).c_str());
        auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
        checked(enumerate(chain,0,&count,nullptr),("Count "+label+" images").c_str());
        images.resize(count);for(auto& i:images) i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        checked(enumerate(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),("Read "+label+" images").c_str());
        have_width=width;have_height=height;
    }
    void ensure_world_ui_chain() {
        if(!world_ui_source_) return;
        D3D11_TEXTURE2D_DESC desc{};world_ui_source_->GetDesc(&desc);
        rebuild_overlay_chain(world_ui_chain_,world_ui_images_,world_ui_width_,world_ui_height_,desc.Width,static_cast<UINT>(world_ui_rows(desc)),"world UI surface");
    }
    void ensure_menu_band_chain() {
        if(width_) rebuild_overlay_chain(menu_band_chain_,menu_band_images_,menu_band_width_,menu_band_height_,width_,static_cast<UINT>(menu_view().rows),"menu panel surface");
    }
    // Copies the menu/cutscene band of the left eye image into the band chain the panel shows; false when the chain is not (yet) the band size.
    bool copy_menu_band(ID3D11Texture2D* eye) {
        const auto view=menu_view();
        if(!menu_band_chain_ || menu_band_width_!=width_ || menu_band_height_!=static_cast<UINT>(view.rows)) return false;
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(menu_band_chain_,&acquire,&index),"Acquire menu panel image");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        const auto ready=xrWaitSwapchainImage(menu_band_chain_,&wait);
        if(XR_FAILED(ready) || ready==XR_TIMEOUT_EXPIRED) {xrReleaseSwapchainImage(menu_band_chain_,&release);checked(ready,"Wait menu panel image");throw std::runtime_error("Menu panel image wait timed out");}
        const D3D11_BOX band{0,static_cast<UINT>(view.y),0,width_,static_cast<UINT>(view.y+view.rows),1};
        context_->CopySubresourceRegion(menu_band_images_.at(index).texture,0,0,0,0,eye,0,&band);
        checked(xrReleaseSwapchainImage(menu_band_chain_,&release),"Release menu panel image");return true;
    }
    std::optional<XrCompositionLayerQuad> world_ui_layer() {
        D3D11_TEXTURE2D_DESC desc{};world_ui_source_->GetDesc(&desc);
        const int rows=world_ui_rows(desc);
        if(!world_ui_chain_ || world_ui_width_!=desc.Width || world_ui_height_!=static_cast<UINT>(rows)) return {};
        OverlayKey overlay_key;
        if(world_ui_stale_) overlay_stats.note(OverlayKind::world_ui,false); // stale: the quad re-shows the last released image
        else if(overlay_needs_image(OverlayKind::world_ui,world_ui_chain_,world_ui_overlay_content(reinterpret_cast<std::uintptr_t>(world_ui_source_.Get()),world_ui_draw_tick_,
                world_ui_kind_,world_ui_full_,desc.Width,static_cast<unsigned>(rows)),overlay_refresh_ms,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(world_ui_chain_,&acquire,&index),"Acquire world UI image");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(world_ui_chain_,&wait);checked(ready,"Wait world UI image");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("World UI image wait timed out");
        const UINT top=(desc.Height-rows)/2;const D3D11_BOX band{0,top,0,desc.Width,top+static_cast<UINT>(rows),1}; // only the shown band
        bool keyed=false;
        if((world_ui_dialogue_ || (world_ui_kind_==1 && wheel_hand_pose())) && native_eye_art_) { // v762 (user, headset v761): the hand wheel without its dark backdrop
            const auto result=native_eye_art_->draw(world_ui_images_.at(index).texture,world_ui_source_.Get(),band,{-1,1,1,-1},format_,true,DXGI_FORMAT_R8G8B8A8_UNORM,true,!world_ui_dialogue_); // the wheel: its dim layer un-composited (v763)
            keyed=result.drawn;++(keyed?world_ui_keyed:world_ui_key_failures);
        }
        if(!keyed) context_->CopySubresourceRegion(world_ui_images_.at(index).texture,0,0,0,0,world_ui_source_.Get(),0,&band);
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        checked(xrReleaseSwapchainImage(world_ui_chain_,&release),"Release world UI image");
        overlay_rendered(OverlayKind::world_ui,overlay_key);
        }
        const auto& pose=world_ui_pose();
        // The native selector lays potion shortcuts and controls at the top
        // and bottom of the actual movie image, outside the pause-menu band.
        const float width=world_ui_width();
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
        layer.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
        layer.size={width,width*rows/desc.Width};layer.subImage.swapchain=world_ui_chain_;
        layer.subImage.imageRect.extent={static_cast<int>(desc.Width),rows};return layer;
    }
    void ensure_hud_window_chain() {
        if(!hud_window_source_) return;
        D3D11_TEXTURE2D_DESC desc{};hud_window_source_->GetDesc(&desc);
        const auto crop=hud_window_crop(desc.Width,desc.Height,1.); // the crop's size depends only on the texture
        rebuild_overlay_chain(hud_window_chain_,hud_window_images_,hud_window_width_,hud_window_height_,crop.w,crop.h,"HUD window surface");
    }
    std::optional<XrCompositionLayerQuad> hud_window_layer() {
        D3D11_TEXTURE2D_DESC desc{};hud_window_source_->GetDesc(&desc);
        // Crop to the relaid content (hud_percent scale about the stage centre) and size it to ~38 deg wide.
        // The whole 16:9 stage (fitted by width into the texture) on one screen. Width in degrees: at most
        // 58% of this headset's horizontal FOV and 60 deg, scaled by the HUD size setting (55-100%).
        const double hfov_deg=(views_[0].fov.angleRight-views_[0].fov.angleLeft)*57.29577951308232;
        const double window_deg=hud_screen_degrees(hfov_deg,settings_.hud_percent());
        const auto crop=hud_window_crop(desc.Width,desc.Height,1.,hud_window_config_.distance,window_deg);
        if(!hud_window_chain_ || hud_window_width_!=static_cast<UINT>(crop.w) || hud_window_height_!=static_cast<UINT>(crop.h)) return {};
        OverlayKey overlay_key;
        if(hud_window_stale_) overlay_stats.note(OverlayKind::hud_window,false); // stale: re-show the last released image
        else if(overlay_needs_image(OverlayKind::hud_window,hud_window_chain_,hud_window_overlay_content(reinterpret_cast<std::uintptr_t>(hud_window_source_.Get()),hud_window_draw_tick_,
                crop.x,crop.y,crop.w,crop.h),overlay_refresh_ms,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(hud_window_chain_,&acquire,&index),"Acquire HUD window image");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(hud_window_chain_,&wait);checked(ready,"Wait HUD window image");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("HUD window image wait timed out");
        const D3D11_BOX box{static_cast<UINT>(crop.x),static_cast<UINT>(crop.y),0,static_cast<UINT>(crop.x+crop.w),static_cast<UINT>(crop.y+crop.h),1}; // only the shown crop
        context_->CopySubresourceRegion(hud_window_images_.at(index).texture,0,0,0,0,hud_window_source_.Get(),0,&box);
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        checked(xrReleaseSwapchainImage(hud_window_chain_,&release),"Release HUD window image");
        overlay_rendered(OverlayKind::hud_window,overlay_key);
        }
        const auto now=std::chrono::steady_clock::now();
        const double dt=hud_window_time_.time_since_epoch().count()?std::chrono::duration<double>(now-hud_window_time_).count():0;
        hud_window_time_=now;
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        const auto pose=hud_window_.update({{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}},dt,hud_window_config_);
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
        layer.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
        layer.size={static_cast<float>(crop.width_m),static_cast<float>(crop.height_m)};layer.subImage.swapchain=hud_window_chain_;
        layer.subImage.imageRect.extent={crop.w,crop.h};
        if(cylinder_supported_) { // same screen, curved: every point at the window distance from the eyes
            hud_cylinder_={XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};hud_cylinder_.layerFlags=layer.layerFlags;hud_cylinder_.space=local_;
            hud_cylinder_.eyeVisibility=XR_EYE_VISIBILITY_BOTH;hud_cylinder_.subImage=layer.subImage;
            const double radius=hud_window_config_.distance*std::cos(hud_window_config_.pitch_deg*.017453292519943295);
            const auto back=rotated(normalized(pose.orientation),{0,0,radius}); // the panel faces its origin: +Z points back to the axis
            hud_cylinder_.pose.orientation=layer.pose.orientation;
            hud_cylinder_.pose.position={static_cast<float>(pose.position.x+back.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z+back.z)};
            hud_cylinder_.radius=static_cast<float>(radius);hud_cylinder_.centralAngle=static_cast<float>(window_deg*.017453292519943295);
            hud_cylinder_.aspectRatio=static_cast<float>(crop.w)/static_cast<float>(crop.h);
        }
        return layer;
    }
    // A 32x32 alpha-blended quad (menu pointer) uploaded every frame.
    void ensure_small_chain(XrSwapchain& chain,std::vector<XrSwapchainImageD3D11KHR>& images) {
        if(!chain) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=32;info.height=32;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            create_overlay_chain(info,chain,"Create small quad");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(chain,0,&count,nullptr),"Count small quad images");
            images.resize(count);for(auto& image:images) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Read small quad images");
            if(images.empty()) throw std::runtime_error("XR small quad has no images");
        }
    }
    XrCompositionLayerQuad small_quad(XrSwapchain& chain,std::vector<XrSwapchainImageD3D11KHR>& images,
        const std::array<std::uint8_t,32*32*4>& pixels,const TrackedPose& p,float size) {
        if(!chain) throw std::logic_error("small quad chain is created by prepare_overlays");
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(chain,&acquire,&index),"Acquire small quad");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(chain,&wait);checked(ready,"Wait small quad");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Small quad wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            D3D11_TEXTURE2D_DESC desc{};images.at(index).texture->GetDesc(&desc);
            if(!eye_texture_matches(desc,32,32,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                throw std::runtime_error("XR small quad image differs");
            context_->UpdateSubresource(images.at(index).texture,0,nullptr,pixels.data(),32*4,0);
        } catch(...) {xrReleaseSwapchainImage(chain,&release);throw;}
        checked(xrReleaseSwapchainImage(chain,&release),"Release small quad");
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(p.orientation.x),static_cast<float>(p.orientation.y),static_cast<float>(p.orientation.z),static_cast<float>(p.orientation.w)};
        layer.pose.position={static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z)};
        layer.size={size,size};layer.subImage.swapchain=chain;layer.subImage.imageRect.extent={32,32};return layer;
    }
public:
    // Native menus and cutscenes are laid out in the 16:9 middle band of the portrait eye
    // image; the panel shows only that band (rows menu_band*width), so no black margins.
    double menu_band=9.0/16.0;
    struct MenuView {float width,height;std::int32_t y,rows;double v0,vspan;};
    MenuView menu_view(bool cinematic=false) const {
        const auto rows=menu_band_rows(width_,height_,menu_band);
        const auto y=static_cast<std::int32_t>((height_-rows)/2);
        const float width=cinematic?2.6f:1.4f;
        return {width,width*rows/static_cast<float>(width_),y,rows,static_cast<double>(y)/height_,static_cast<double>(rows)/height_};
    }
private:
    MenuView displayed_menu_view() const {
        if(!world_ui_source_) return menu_view();
        D3D11_TEXTURE2D_DESC desc{};world_ui_source_->GetDesc(&desc);
        const auto rows=world_ui_rows(desc);
        const auto y=static_cast<std::int32_t>((desc.Height-rows)/2);
        const float width=world_ui_width();
        return {width,width*rows/desc.Width,y,rows,static_cast<double>(y)/desc.Height,static_cast<double>(rows)/desc.Height};
    }
    XrCompositionLayerQuad pointer_layer() {
        const auto view=displayed_menu_view();
        const auto p=menu_pointer_pose(world_ui_kind_?world_ui_pose():native_menu_pose(),{native_menu_input.u,std::clamp((native_menu_input.v-view.v0)/view.vspan,0.,1.)},view.width,view.height);
        return small_quad(pointer_chain_,pointer_images_,menu_pointer_image(native_menu_input.trigger),p,.02f);
    }
    struct XrOperationError:std::runtime_error {
        XrResult result;
        XrOperationError(XrResult value,const char* name):std::runtime_error(std::string(name)+": "+std::to_string(value)),result(value){}
    };
    bool release_pending_wrist(unsigned side) {
        if(!wrist_acquired_[side]) return true;
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=0;
        const auto ready=xrWaitSwapchainImage(wrist_chains_[side],&wait);
        checked(ready,"Poll pending wrist HUD");
        if(ready==XR_TIMEOUT_EXPIRED) return false;
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        checked(xrReleaseSwapchainImage(wrist_chains_[side],&release),"Release pending wrist HUD");
        wrist_acquired_[side]=false;return true;
    }
    bool gpu_retirement_ready(Microsoft::WRL::ComPtr<ID3D11Query>& query) {
        if(!context_) return true;
        Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
        if(FAILED(device->GetDeviceRemovedReason())) return true;
        if(!query) {
            D3D11_QUERY_DESC desc{};desc.Query=D3D11_QUERY_EVENT;
            if(FAILED(device->CreateQuery(&desc,&query))) throw std::runtime_error("Cannot fence XR surface retirement");
            context_->End(query.Get());context_->Flush();return false;
        }
        const auto result=context_->GetData(query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(result)) throw std::runtime_error("Cannot poll XR surface retirement");
        return result==S_OK;
    }
    void drain_gpu_for_surface() {
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        const auto deadline=GetTickCount64()+1000;
        while(!gpu_retirement_ready(query)) {
            if(GetTickCount64()>=deadline) throw std::runtime_error("XR surface GPU retirement timed out");
            Sleep(1);
        }
    }
    bool retire_wrist(unsigned side) {
        if(!wrist_chains_[side]) {wrist_recreate_[side]=false;return true;}
        if(wrist_acquired_[side] || !gpu_retirement_ready(wrist_retirement_[side])) {++wrist_retirement_waits;return false;}
        destroy_overlay_chain(wrist_chains_[side],"Destroy retired wrist HUD");
        wrist_chains_[side]=XR_NULL_HANDLE;wrist_targets_[side].clear();wrist_images_[side].clear();
        native_wrist_sizes_[side]={};wrist_retirement_[side].Reset();wrist_recreate_[side]=false;return true;
    }
    // v1013: the left-wrist watch. Placement, visibility and fades follow the rival watch's physical numbers; all
    // geometry comes from the runtime frame (head and grip poses), never from a headset-specific constant.
    void update_wrist_watch(bool allowable) noexcept {
        const auto now=GetTickCount64();
        const double dt=wrist_watch_tick_ && now>=wrist_watch_tick_?std::min(.25,(now-wrist_watch_tick_)/1000.):0.;
        wrist_watch_tick_=now;
        if(!wrist_hud_enabled || legacy_wrist_hud) {
            wrist_watch_state_.reset();wrist_watch_pose_.reset();wrist_watch_alpha_=0;wrist_watch_submitted_=false;return;
        }
        constexpr XrSpaceLocationFlags valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        bool tracked=views_valid_ && (grip_tracked_hands&1u) && (grip_locations[0].locationFlags&valid)==valid;
        WristWatchAngles angles{};angles.distance_m=1e9;
        std::optional<WristWatchPose> pose;
        if(tracked) {
            const auto& l=views_[0].pose;const auto& r=views_[1].pose;
            const TrackedPose head{{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
                {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}};
            const auto& p=grip_locations[0].pose;
            pose=wrist_watch_pose({{p.position.x,p.position.y,p.position.z},
                {p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},head);
            if(pose) angles=pose->angles;else tracked=false;
        }
        const bool menu=!allowable,trigger=controller_input.left_trigger>.5f;
        const auto frame=wrist_watch_state_.update(angles,tracked,trigger,menu,dt);
        wrist_watch_alpha_=frame.alpha;wrist_watch_pose_=pose;
        if(frame.haptic && wrist_haptic_enabled) pulse(0,.12f,.018f);
    }
    DXGI_FORMAT wrist_watch_face_format() const noexcept {
        return format_==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:format_;
    }
public:
    // dxgi_probe owns the game-cluster texture (it composites the native child draws at their original surface
    // positions). This is a bounded publication only: the texture stays alive through the ComPtr, and the watch
    // rejects a stale owner/epoch/tick instead of keeping old art. No image data is generated here.
    bool wrist_watch_cluster_source(ID3D11Texture2D* texture,UINT texture_width,UINT texture_height,
        UINT64 generation,UINT64 tick,std::uintptr_t owner,UINT64 epoch) {
        if(!texture) {wrist_watch_cluster_={};return true;}
        if(!texture_width || !texture_height || !owner || !tick || !epoch) {wrist_watch_cluster_={};++wrist_watch_cluster_rejects_;return false;}
        D3D11_TEXTURE2D_DESC texture_desc{};texture->GetDesc(&texture_desc);
        Microsoft::WRL::ComPtr<ID3D11Device> device,actual;
        context_->GetDevice(&device);texture->GetDevice(&actual);
        if(!device || device.Get()!=actual.Get() || texture_desc.Width<texture_width || texture_desc.Height<texture_height ||
           texture_desc.MipLevels!=1 || texture_desc.ArraySize!=1 || texture_desc.SampleDesc.Count!=1 ||
           (texture_desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && texture_desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM &&
            texture_desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
            wrist_watch_cluster_={};++wrist_watch_cluster_rejects_;return false;
        }
        const bool content_changed=wrist_watch_cluster_.texture.Get()!=texture ||
            wrist_watch_cluster_.width!=texture_width || wrist_watch_cluster_.height!=texture_height ||
            wrist_watch_cluster_.generation!=generation || wrist_watch_cluster_.owner!=owner || wrist_watch_cluster_.epoch!=epoch;
        if(!content_changed) {
            wrist_watch_cluster_.tick=tick; // freshness only: no overlay content changed, so no redraw
            return true;
        }
        wrist_watch_cluster_={texture,texture_width,texture_height,epoch,tick,generation,owner};
        ++wrist_watch_cluster_updates_;
        return true;
    }
private:
    // The acquired watch image is cleared and the single game-cluster texture is alpha-blitted over it with the fade.
    // The optional dark backdrop is the only mod-drawn fill and is off unless build/wrist-hud-backdrop is present.
    bool draw_wrist_watch(unsigned index) {
        const auto now=GetTickCount64(),epoch=pose_epoch();
        const double alpha=std::clamp(wrist_watch_alpha_,0.,1.);
        const float fade=static_cast<float>(alpha*alpha*(3.-2.*alpha));
        const auto face_format=wrist_watch_face_format();
        ContextState::Scope graphics_scope(graphics_state_);
        ID3D11RenderTargetView* target=wrist_targets_[0].at(index).Get();
        if(!target) {++wrist_watch_blit_rejects_;return false;}
        ID3D11RenderTargetView* bound=target;
        context_->OMSetRenderTargets(1,&bound,nullptr);
        const bool cluster_ready=wrist_watch_cluster_.fresh(epoch,now);
        if(wrist_hud_backdrop && cluster_ready) {
            const float backdrop[4]{.02f*fade,.024f*fade,.028f*fade,.45f*fade};
            context_->ClearRenderTargetView(target,backdrop);
        } else {
            constexpr float transparent[4]{};
            context_->ClearRenderTargetView(target,transparent);
        }
        ++wrist_watch_draws_;
        if(!cluster_ready) return true;
        const D3D11_BOX source{0,0,0,wrist_watch_cluster_.width,wrist_watch_cluster_.height,1};
        const D3D11_RECT destination{0,0,static_cast<LONG>(wrist_watch_canvas),static_cast<LONG>(wrist_watch_canvas)};
        if(!wrist_watch_blitter_.blit_scaled(context_.Get(),target,wrist_watch_cluster_.texture.Get(),source,destination,face_format,true,fade))
            ++wrist_watch_blit_rejects_;
        return true;
    }
    std::optional<XrCompositionLayerQuad> wrist_watch_layer() {
        if(!wrist_hud_enabled || legacy_wrist_hud) return {};
        // Optional layers must never stall the scene while the compositor is busy. A timeout retains acquisition until a
        // later successful wait, even if the watch has since faded out.
        if(!release_pending_wrist(0)) {++wrist_timeouts;return {};}
        if(!views_valid_ || wrist_watch_alpha_<=0 || !wrist_watch_pose_) return {};
        const auto now=GetTickCount64(),epoch=pose_epoch();
        const bool cluster_ready=wrist_watch_cluster_.fresh(epoch,now);
        auto& chain=wrist_chains_[0];
        if(chain && (native_wrist_sizes_[0][0]!=wrist_watch_canvas || native_wrist_sizes_[0][1]!=wrist_watch_canvas)) wrist_recreate_[0]=true;
        if(wrist_recreate_[0] || !chain) {wrist_want_[0]={wrist_watch_canvas,wrist_watch_canvas};return {};} // prepare_overlays creates it between frames
        const auto q=[](double value) {return static_cast<std::uint64_t>(std::lround(std::clamp(value,0.,1.)*1000.));};
        // The overlay key is the cluster source identity and generation, the fade and the size. A cluster recomposition
        // (new generation) or a fade step forces exactly one new GPU blend; no CPU image is generated. `cluster_ready`
        // is part of the key so a cluster that ages out clears the last released watch image.
        const auto content=overlay_content({cluster_ready?reinterpret_cast<std::uint64_t>(wrist_watch_cluster_.texture.Get()):0u,
            wrist_watch_cluster_.generation,static_cast<std::uint64_t>(wrist_watch_cluster_.width),
            static_cast<std::uint64_t>(wrist_watch_cluster_.height),cluster_ready?1u:0u,wrist_hud_backdrop?1u:0u,
            q(wrist_watch_alpha_),
            static_cast<std::uint64_t>(std::lround(std::clamp(wrist_hud_size_cm,wrist_watch_size_min_cm,wrist_watch_size_max_cm)*100.)),1u});
        const auto wrist_kind=OverlayKind::wrist_left;OverlayKey overlay_key;
        if(overlay_needs_image(wrist_kind,chain,content,overlay_refresh_ms,overlay_key)) {
            XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
            checked(xrAcquireSwapchainImage(chain,&acquire,&index),"Acquire wrist watch");
            wrist_acquired_[0]=true;
            XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=0;
            const auto ready=xrWaitSwapchainImage(chain,&wait);checked(ready,"Wait wrist watch");
            if(ready==XR_TIMEOUT_EXPIRED) {++wrist_timeouts;return {};}
            XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            try {
                if(!draw_wrist_watch(index)) {xrReleaseSwapchainImage(chain,&release);wrist_acquired_[0]=false;return {};}
            } catch(...) {
                const auto released=xrReleaseSwapchainImage(chain,&release);
                if(XR_SUCCEEDED(released)) wrist_acquired_[0]=false;
                checked(released,"Release failed wrist watch");throw;
            }
            checked(xrReleaseSwapchainImage(chain,&release),"Release wrist watch");
            wrist_acquired_[0]=false;
            overlay_rendered(wrist_kind,overlay_key);
        }
        const auto& pose=wrist_watch_pose_->pose;
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // premultiplied game-cluster texture
        layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),
            static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
        layer.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
        const double size_m=std::clamp(wrist_hud_size_cm,wrist_watch_size_min_cm,wrist_watch_size_max_cm)*.01;
        layer.size={static_cast<float>(size_m),static_cast<float>(size_m)};
        layer.subImage.swapchain=chain;layer.subImage.imageRect.extent={wrist_watch_canvas,wrist_watch_canvas};
        native_wrist_art_shown_={cluster_ready,cluster_ready,cluster_ready};wrist_watch_submitted_=true;++wrist_layers[0];
        return layer;
    }
    std::optional<XrCompositionLayerQuad> legacy_wrist_layer(unsigned side) {
        // Optional layers must never stall the scene while the compositor is
        // busy. A timeout retains acquisition until a later successful wait.
        if(!release_pending_wrist(side)) {++wrist_timeouts;return {};}
        constexpr XrSpaceLocationFlags valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        const auto now=GetTickCount64(),epoch=pose_epoch();
        const auto* primary=&native_wrist_art_[side];
        if(side==1 && !primary->fresh(epoch,now))primary=&native_wrist_art_[2];
        const auto& art=*primary;
        // Health/mana (side 1) on the RIGHT wrist, ammo (side 0) on the LEFT (user request), inner (palm) side.
        const unsigned arm=side;
        if(!views_valid_ || !art.fresh(epoch,now)) return {};
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        const TrackedPose head{{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}};
        // Watch on the inner wrist (user request): only while the palm side of that wrist faces the eyes.
        std::optional<TrackedPose> pose;
        if((grip_tracked_hands&(1u<<arm)) && (grip_locations[arm].locationFlags&valid)==valid) {
            const auto& p=grip_locations[arm].pose;
            pose=wrist_hud_pose({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},head,arm?-1:1);
        }
        if(!pose) return {};
        const auto* mana=side==1 && primary==&native_wrist_art_[1] && native_wrist_art_[2].fresh(epoch,now) &&
            native_wrist_art_[2].premultiplied==art.premultiplied?&native_wrist_art_[2]:nullptr;
        const unsigned primary_width=art.crop.right-art.crop.left,primary_height=art.crop.bottom-art.crop.top;
        const unsigned mana_width=mana?mana->crop.right-mana->crop.left:0,mana_height=mana?mana->crop.bottom-mana->crop.top:0;
        // Native pixels remain 1:1 relative to one another. A transparent
        // canvas joins the two isolated crops, without cards or replacement art.
        const unsigned width=primary_width+mana_width,height=std::max(primary_height,mana_height);
        const float scale=std::min(static_cast<float>(settings_.hud_percent())/65.f,100.f/65.f); // the watch stops at the 100% size
        const float pixel_scale=.07f*scale/width; // smaller watch face (was .11 m)
        const float panel_width=pixel_scale*width,panel_height=pixel_scale*height;
        const auto contained=native_panel_fit(views_,*pose,panel_width,panel_height);
        if(!contained) return {};
        auto& chain=wrist_chains_[side];auto& images=wrist_images_[side];
        // Grow in blocks; animated native bounds do not recreate a chain for every one-pixel change, and shrinking can reuse the canvas.
        if(chain && (native_wrist_sizes_[side][0]<width || native_wrist_sizes_[side][1]<height)) wrist_recreate_[side]=true;
        if(wrist_recreate_[side] || !chain) {wrist_want_[side]={(width+63u)&~63u,(height+63u)&~63u};return {};} // prepare_overlays retires/creates it between frames
        const auto art_facts=[](const NativeArtSource& a) {return WristArtFacts{reinterpret_cast<std::uintptr_t>(a.texture.Get()),a.tick,a.owner,a.crop.left,a.crop.top,a.crop.right,a.crop.bottom,a.premultiplied};};
        const auto primary_facts=art_facts(art);const auto mana_facts=mana?art_facts(*mana):WristArtFacts{};
        const auto wrist_kind=side?OverlayKind::wrist_right:OverlayKind::wrist_left;OverlayKey overlay_key;
        if(overlay_needs_image(wrist_kind,chain,wrist_overlay_content(primary_facts,mana?&mana_facts:nullptr,native_wrist_sizes_[side][0],native_wrist_sizes_[side][1]),overlay_refresh_ms,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(chain,&acquire,&index),"Acquire wrist HUD");
        wrist_acquired_[side]=true;
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=0;
        const auto ready=xrWaitSwapchainImage(chain,&wait);checked(ready,"Wait wrist HUD");
        if(ready==XR_TIMEOUT_EXPIRED) {++wrist_timeouts;return {};}
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
        constexpr float transparent[4]{};context_->ClearRenderTargetView(wrist_targets_[side].at(index).Get(),transparent);
        context_->CopySubresourceRegion(images.at(index).texture,0,0,(height-primary_height)/2,0,art.texture.Get(),0,&art.crop);
        if(mana)context_->CopySubresourceRegion(images.at(index).texture,0,primary_width,(height-mana_height)/2,0,mana->texture.Get(),0,&mana->crop);
        if(observe_native_wrist_art) observe_native_wrist_art(side,images.at(index).texture,art.owner);
        } catch(...) {
            const auto released=xrReleaseSwapchainImage(chain,&release);
            if(XR_SUCCEEDED(released)) wrist_acquired_[side]=false;
            checked(released,"Release failed wrist draw");throw;
        }
        checked(xrReleaseSwapchainImage(chain,&release),"Release wrist HUD");
        wrist_acquired_[side]=false;
        overlay_rendered(wrist_kind,overlay_key);
        }
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        if(!art.premultiplied) layer.layerFlags|=XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(contained->orientation.x),static_cast<float>(contained->orientation.y),static_cast<float>(contained->orientation.z),static_cast<float>(contained->orientation.w)};
        layer.pose.position={static_cast<float>(contained->position.x),static_cast<float>(contained->position.y),static_cast<float>(contained->position.z)};
        // The whole (transparent-padded) canvas is shown; the quad is moved so the content keeps its exact place and size.
        const auto canvas=native_wrist_sizes_[side];
        const auto shift=rotated(normalized(contained->orientation),{(canvas[0]-static_cast<double>(width))*.5*pixel_scale,-(canvas[1]-static_cast<double>(height))*.5*pixel_scale,0});
        layer.pose.position={static_cast<float>(contained->position.x+shift.x),static_cast<float>(contained->position.y+shift.y),static_cast<float>(contained->position.z+shift.z)};
        layer.subImage.swapchain=chain;layer.subImage.imageRect.extent={static_cast<int32_t>(canvas[0]),static_cast<int32_t>(canvas[1])};
        native_wrist_art_shown_[static_cast<unsigned>(primary-native_wrist_art_.data())]=true;
        if(mana)native_wrist_art_shown_[2]=true;
        layer.size={pixel_scale*canvas[0],pixel_scale*canvas[1]};
        return layer;
    }
    void create_wrist_chain(unsigned side,UINT canvas_width,UINT canvas_height) {
        auto& chain=wrist_chains_[side];auto& images=wrist_images_[side];
        {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=format_;info.width=canvas_width;info.height=canvas_height;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            create_overlay_chain(info,chain,"Create wrist HUD");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(chain,0,&count,nullptr),"Count wrist HUD images");
            images.resize(count);for(auto& i:images) i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Read wrist HUD images");
            if(images.empty()) throw std::runtime_error("XR wrist HUD has no images");
            Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
            D3D11_RENDER_TARGET_VIEW_DESC target{};target.Format=format_;target.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            wrist_targets_[side].resize(count);
            for(unsigned i=0;i<count;++i) {
                images[i].texture->GetDesc(&wrist_failure_texture);
                wrist_failure_hresult=device->CreateRenderTargetView(images[i].texture,&target,&wrist_targets_[side][i]);
                if(FAILED(wrist_failure_hresult)) throw std::runtime_error("Create typed native wrist target failed: "+std::to_string(wrist_failure_hresult));
                ++wrist_target_creations;
            }
            native_wrist_sizes_[side]={canvas_width,canvas_height};wrist_want_[side]={};
        }
    }
    template<class F> std::optional<XrCompositionLayerQuad> guarded_wrist(unsigned side,F&& run) {
        try {return run();}
        catch(const XrOperationError& error) {
            // A lost runtime/device is a session failure; optional allocation
            // pressure can drop this layer while the projection keeps running.
            if(error.result!=XR_ERROR_OUT_OF_MEMORY && error.result!=XR_ERROR_LIMIT_REACHED) throw;
            wrist_failure_reason=error.what();
        } catch(const std::exception& error) {wrist_failure_reason=error.what();}
        Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
        wrist_device_removed=device->GetDeviceRemovedReason();
        if(FAILED(wrist_device_removed)) throw std::runtime_error("Wrist HUD device removed: "+std::to_string(wrist_device_removed));
        if(wrist_acquired_[side]) throw std::runtime_error("Wrist HUD failed while image remained acquired");
        // Destruction also needs the app's submitted GPU work to finish.
        // Poll a retirement fence on a later frame rather than blocking here.
        wrist_recreate_[side]=true;
        wrist_retry_ticks_[side]=GetTickCount64()+500;++wrist_failures;
        if(observe_wrist_failure) observe_wrist_failure(side,wrist_failure_reason.c_str());
        return {};
    }
    std::optional<XrCompositionLayerQuad> optional_wrist_layer(unsigned side) {
        if(!wrist_hud_enabled) return {};
        if(!legacy_wrist_hud && side!=0) return {};
        if(GetTickCount64()<wrist_retry_ticks_[side]) return {};
        return guarded_wrist(side,[&]{return legacy_wrist_hud?legacy_wrist_layer(side):wrist_watch_layer();});
    }
    void prepare_wrist(unsigned side) {
        if(!wrist_hud_enabled) return;
        if(!legacy_wrist_hud && side!=0) return;
        if(GetTickCount64()<wrist_retry_ticks_[side]) return;
        guarded_wrist(side,[&]()->std::optional<XrCompositionLayerQuad> {
            if(wrist_recreate_[side] && !retire_wrist(side)) return {};
            if(!wrist_chains_[side] && wrist_want_[side][0]) create_wrist_chain(side,wrist_want_[side][0],wrist_want_[side][1]);
            return {};
        });
    }
    // Everything that creates or destroys a swapchain after the session starts runs here, outside xrBeginFrame..xrEndFrame:
    // fixed-size panels once, size-dependent panels (world UI, HUD window, wrist) when their size changed or a retirement fence passed.
    void prepare_overlays() {
        if(frame_open_) return;
        ensure_small_chain(pointer_chain_,pointer_images_);ensure_settings_chain();ensure_hint_chain();ensure_dot_chain();ensure_vignette_chain();
        ensure_world_ui_chain();ensure_hud_window_chain();ensure_menu_band_chain();ensure_blank_chain();
        for(unsigned side=0;side<2;++side) prepare_wrist(side);
    }
    void ensure_hint_chain() {
        constexpr unsigned width=1920,height=1200;
        if(!hint_chain_) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=width;info.height=height;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            create_overlay_chain(info,hint_chain_,"Create controls card");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(hint_chain_,0,&count,nullptr),"Count controls card images");
            hint_images_.resize(count);for(auto& image:hint_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(hint_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(hint_images_.data())),"Read controls card images");
            if(hint_images_.empty()) throw std::runtime_error("XR controls card has no images");
            hint_revisions_.assign(count,0);
        }
    }
    // The controls and gestures page: a 1.1 m card 0.85 m in front of the head where it was opened, level, facing the player; it stays
    // in the world while open (look around it, step back to read it whole). Replaces the wrist controls card (its bindings were outdated).
    std::optional<XrCompositionLayerQuad> hint_layer() {
        if(!guide_open_.load() || !views_valid_) return {};
        if(!guide_placed_) {
            const auto& l=views_[0].pose;const auto& r=views_[1].pose;
            guide_pose_=world_ui_front_pose({{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5-.06,(l.position.z+r.position.z)*.5},
                {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}},.85);guide_placed_=true;
        }
        constexpr unsigned width=1920,height=1200;
        if(!hint_chain_) return {}; // created by prepare_overlays
        // User's illustrated guide: cache by page, playable character and imported-art generation; retain its world pose.
        if(guide_refresh_.exchange(false)) game_art().refresh();
        const auto generation=game_art().generation();const int page=guide_page_.load(),character=game_character.load()==2?1:0;
        if(generation!=guide_art_generation_) {for(auto& pixels:guide_pages_) pixels.clear();guide_art_generation_=generation;hint_page_=-1;}
        auto& pixels=guide_pages_[character*7+page];
        if(pixels.empty()) pixels=controls_guide_image(controls_guide_markdown(),width,height,nullptr,page,character!=0);
        if(page!=hint_page_ || character!=hint_character_) {hint_page_=page;hint_character_=character;++hint_revision_;}
        OverlayKey overlay_key;
        if(overlay_needs_image(OverlayKind::hint,hint_chain_,hint_revision_,0,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(hint_chain_,&acquire,&index),"Acquire controls card");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(hint_chain_,&wait);checked(ready,"Wait controls card");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Controls card wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            // A runtime may acquire any image: every image must receive the current page revision.
            if(hint_revisions_.at(index)!=hint_revision_) {
                context_->UpdateSubresource(hint_images_.at(index).texture,0,nullptr,pixels.data(),width*4,0);hint_revisions_[index]=hint_revision_;
            }
        } catch(...) {xrReleaseSwapchainImage(hint_chain_,&release);throw;}
        checked(xrReleaseSwapchainImage(hint_chain_,&release),"Release controls card");
        overlay_rendered(OverlayKind::hint,overlay_key);
        }
        const auto& facing=guide_pose_.orientation;const auto& centre=guide_pose_.position;
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(facing.x),static_cast<float>(facing.y),static_cast<float>(facing.z),static_cast<float>(facing.w)};
        layer.pose.position={static_cast<float>(centre.x),static_cast<float>(centre.y),static_cast<float>(centre.z)};
        layer.size={1.1f,.6875f};layer.subImage.swapchain=hint_chain_;layer.subImage.imageRect.extent={static_cast<int32_t>(width),static_cast<int32_t>(height)};
        return layer;
    }
    void ensure_blank_chain() {
        if(blank_chain_) return;
        XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=info.height=blank_texture_size;
        info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        create_overlay_chain(info,blank_chain_,"Create black fallback quad");
        auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
        checked(enumerate(blank_chain_,0,&count,nullptr),"Count black fallback images");
        blank_images_.resize(count);for(auto& image:blank_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        checked(enumerate(blank_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(blank_images_.data())),"Read black fallback images");
        if(blank_images_.empty()) throw std::runtime_error("XR black fallback has no images");
        blank_filled_.assign(count,false);
    }
    // The black quad's image: every runtime image is filled with opaque black once, then re-released each time (acquire/wait/release is legal in a frame).
    bool blank_ready_() const noexcept {return blank_chain_ && !blank_images_.empty() && context_;}
    std::optional<XrCompositionLayerQuad> black_fallback_layer_() {
        if(!blank_ready_()) return {};
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        if(XR_FAILED(xrAcquireSwapchainImage(blank_chain_,&acquire,&index)) || index>=blank_images_.size()) return {};
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=100'000'000;
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        const auto ready=xrWaitSwapchainImage(blank_chain_,&wait);
        if(XR_FAILED(ready) || ready==XR_TIMEOUT_EXPIRED) {xrReleaseSwapchainImage(blank_chain_,&release);return {};}
        try {
            if(!blank_filled_.at(index)) {
                std::vector<std::uint8_t> opaque(blank_texture_size*blank_texture_size*4,0);for(std::size_t i=3;i<opaque.size();i+=4) opaque[i]=255;
                context_->UpdateSubresource(blank_images_[index].texture,0,nullptr,opaque.data(),blank_texture_size*4,0);blank_filled_[index]=true;
            }
        } catch(...) {xrReleaseSwapchainImage(blank_chain_,&release);return {};}
        if(XR_FAILED(xrReleaseSwapchainImage(blank_chain_,&release))) return {};
        std::optional<XrPosef> head;if(views_valid_) head=views_[0].pose;
        auto quad=make_black_quad(local_,blank_chain_,blank_texture_size,head?&*head:nullptr);
        if(!black_quad_valid(quad,blank_texture_size)) return {};
        return quad;
    }
    void ensure_dot_chain() {
        constexpr unsigned size=64;
        if(!dot_chain_) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=info.height=size;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            create_overlay_chain(info,dot_chain_,"Create wheel dot");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(dot_chain_,0,&count,nullptr),"Count wheel dot images");
            dot_images_.resize(count);for(auto& image:dot_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(dot_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(dot_images_.data())),"Read wheel dot images");
            if(dot_images_.empty()) throw std::runtime_error("XR wheel dot has no images");
            dot_filled_.assign(count,false);
        }
    }
    // Wheel pointer dot: a ~3 cm quad at the hand-ray hit, facing the head, bright disc with a dark rim.
    std::optional<XrCompositionLayerQuad> dot_layer() {
        if(!wheel_cursor_ || !views_valid_) return {};
        constexpr unsigned size=64;
        if(!dot_chain_) return {}; // created by prepare_overlays
        OverlayKey overlay_key;
        if(overlay_needs_image(OverlayKind::dot,dot_chain_,static_overlay_content(),0,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(dot_chain_,&acquire,&index),"Acquire wheel dot");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(dot_chain_,&wait);checked(ready,"Wait wheel dot");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Wheel dot wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            if(!dot_filled_.at(index)) { // never changes: fill each runtime image once (straight alpha)
                std::vector<std::uint8_t> px(size*size*4);
                for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
                    const float d=std::hypot(x+.5f-size*.5f,y+.5f-size*.5f)/(size*.5f);
                    const bool core=d<.62f;const float edge=std::clamp((.98f-d)*size*.5f,0.f,1.f);
                    auto* p=&px[(y*size+x)*4];p[0]=p[1]=p[2]=core?255:20;p[3]=static_cast<std::uint8_t>(edge*255);
                }
                context_->UpdateSubresource(dot_images_.at(index).texture,0,nullptr,px.data(),size*4,0);dot_filled_[index]=true;
            }
        } catch(...) {xrReleaseSwapchainImage(dot_chain_,&release);throw;}
        checked(xrReleaseSwapchainImage(dot_chain_,&release),"Release wheel dot");
        overlay_rendered(OverlayKind::dot,overlay_key);
        }
        const auto& h=views_[0].pose.orientation;
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;layer.pose.orientation=h;
        layer.pose.position={static_cast<float>(wheel_cursor_->x),static_cast<float>(wheel_cursor_->y),static_cast<float>(wheel_cursor_->z)};
        layer.size={.03f,.03f};layer.subImage.swapchain=dot_chain_;layer.subImage.imageRect.extent={size,size};
        return layer;
    }
    void ensure_vignette_chain() {
        if(!vignette_enabled || vignette_chain_) return;
        constexpr unsigned size=vignette_texture_size;
        XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=info.height=size;
        info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        create_overlay_chain(info,vignette_chain_,"Create comfort vignette");
        auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
        checked(enumerate(vignette_chain_,0,&count,nullptr),"Count comfort vignette images");
        vignette_images_.resize(count);for(auto& image:vignette_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        checked(enumerate(vignette_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(vignette_images_.data())),"Read comfort vignette images");
        if(vignette_images_.empty()) throw std::runtime_error("XR comfort vignette has no images");
        vignette_filled_.assign(count,false);vignette_ring_=vignette_ring_image(size);vignette_baked_.clear();vignette_baked_opacity_=-1;
    }
    // Black ring over everything else, fixed to the head: shown only while the stick is moving or turning the player.
    // With XR_KHR_composition_layer_color_scale_bias the opacity is the layer's alpha scale over a texture that never changes.
    // Without it the opacity is baked into the texture alpha, at most 10 times a second (a cheap alpha ramp, no per-frame work).
    std::optional<XrCompositionLayerQuad> vignette_layer() {
        const float wanted=vignette_opacity.load(std::memory_order_relaxed);
        if(!vignette_enabled || !vignette_chain_ || !std::isfinite(wanted) || wanted<.01f) return {};
        if(vignette_runtime_off_ || !views_valid_) return {};
        const float opacity=std::min(wanted,1.f);
        constexpr unsigned size=vignette_texture_size;
        const std::uint8_t* upload=vignette_ring_.data();
        if(!color_scale_supported_) {
            const auto now=GetTickCount64();
            if(vignette_baked_.empty() || (now-vignette_bake_tick_>=50 && std::fabs(opacity-vignette_baked_opacity_)>=1.f/255)) {
                vignette_baked_=vignette_ring_;
                for(std::size_t i=3;i<vignette_baked_.size();i+=4) vignette_baked_[i]=static_cast<std::uint8_t>(std::lround(vignette_ring_[i]*opacity));
                vignette_baked_opacity_=opacity;vignette_bake_tick_=now;++vignette_bakes;
            }
            upload=vignette_baked_.data();
        }
        OverlayKey overlay_key;
        if(overlay_needs_image(OverlayKind::vignette,vignette_chain_,vignette_overlay_content(color_scale_supported_,vignette_bakes),0,overlay_key)) {
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(vignette_chain_,&acquire,&index),"Acquire comfort vignette");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(vignette_chain_,&wait);checked(ready,"Wait comfort vignette");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Comfort vignette wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            if(!color_scale_supported_ || !vignette_filled_.at(index)) { // scaled layer: each runtime image is filled once
                context_->UpdateSubresource(vignette_images_.at(index).texture,0,nullptr,upload,size*4,0);vignette_filled_[index]=true;
            }
        } catch(...) {xrReleaseSwapchainImage(vignette_chain_,&release);throw;}
        checked(xrReleaseSwapchainImage(vignette_chain_,&release),"Release comfort vignette");
        overlay_rendered(OverlayKind::vignette,overlay_key);
        }
        constexpr float distance=1.f,extent=3.f; // 3 m square at 1 m: +-56 degrees; the clear centre (radius .55) is +-39 degrees
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        {   // LOCAL space like every other quad we submit (SteamVR/wineopenxr rejected the VIEW-space quad with POSE_INVALID):
            // the pose is built each frame from the located head pose
            const auto& h=views_[0].pose.orientation;
            const auto facing=normalized(Quaternion{h.x,h.y,h.z,h.w});
            const auto head=head_pose_now();
            const auto centre=Vec3{head.position.x,head.position.y,head.position.z}+rotated(facing,{0,0,-distance});
            layer.space=local_;
            layer.pose.orientation={static_cast<float>(facing.x),static_cast<float>(facing.y),static_cast<float>(facing.z),static_cast<float>(facing.w)};
            layer.pose.position={static_cast<float>(centre.x),static_cast<float>(centre.y),static_cast<float>(centre.z)};
        }
        if(color_scale_supported_) {
            vignette_scale_.next=nullptr;vignette_scale_.colorScale={1.f,1.f,1.f,opacity};vignette_scale_.colorBias={0.f,0.f,0.f,0.f};
            layer.next=&vignette_scale_;
        }
        layer.size={extent,extent};layer.subImage.swapchain=vignette_chain_;layer.subImage.imageRect.extent={static_cast<int32_t>(size),static_cast<int32_t>(size)};
        return layer;
    }
    static void checked(XrResult value,const char* name) {
        if(XR_FAILED(value)) throw XrOperationError(value,name);
    }
    template<class T> T api(const char* name,XrInstance instance) {
        PFN_xrVoidFunction address{};
        checked(get_(instance,name,&address),name);
        if(!address) throw std::runtime_error(std::string("Missing XR function ")+name);
        return reinterpret_cast<T>(address);
    }
    XrPath path(const char* name) {
        XrPath value{};checked(xrStringToPath(instance_,name,&value),name);return value;
    }
    void create_actions() {
        XrActionSetCreateInfo set{};set.type=XR_TYPE_ACTION_SET_CREATE_INFO;
        strcpy_s(set.actionSetName,"game_controls");strcpy_s(set.localizedActionSetName,"Dishonored 2 controls");
        checked(xrCreateActionSet(instance_,&set,&action_set_),"Create game action set");
        hand_paths_={path("/user/hand/left"),path("/user/hand/right")};
        constexpr std::array<const char*,InputCount> names{"grip_pose","aim_pose","stick","trigger","squeeze","first_button","second_button","stick_click","menu"};
        std::vector<XrActionSuggestedBinding> bindings;
        for(UINT index=0;index<InputCount;++index) {
            XrActionCreateInfo action{};action.type=XR_TYPE_ACTION_CREATE_INFO;
            action.actionType=index<=Aim?XR_ACTION_TYPE_POSE_INPUT:index==Stick?XR_ACTION_TYPE_VECTOR2F_INPUT:
                index<=Squeeze?XR_ACTION_TYPE_FLOAT_INPUT:XR_ACTION_TYPE_BOOLEAN_INPUT;
            action.countSubactionPaths=2;action.subactionPaths=hand_paths_.data();
            strcpy_s(action.actionName,names[index]);strcpy_s(action.localizedActionName,names[index]);
            checked(xrCreateAction(action_set_,&action,&actions_[index]),"Create controller action");
            for(UINT side=0;side<2;++side) {
                if(index==Menu && side==1) continue;
                const char* component=index==Grip?"grip/pose":index==Aim?"aim/pose":index==Stick?"thumbstick":
                    index==Trigger?"trigger/value":index==Squeeze?"squeeze/value":index==StickClick?"thumbstick/click":
                    index==Menu?"menu/click":index==FirstButton?(side?"a/click":"x/click"):(side?"b/click":"y/click");
                const auto binding=std::string(side?"/user/hand/right/input/":"/user/hand/left/input/")+component;
                bindings.push_back({actions_[index],path(binding.c_str())});
            }
        }
        {
            XrActionCreateInfo action{};action.type=XR_TYPE_ACTION_CREATE_INFO;action.actionType=XR_ACTION_TYPE_VIBRATION_OUTPUT;
            action.countSubactionPaths=2;action.subactionPaths=hand_paths_.data();
            strcpy_s(action.actionName,"haptic");strcpy_s(action.localizedActionName,"haptic");
            checked(xrCreateAction(action_set_,&action,&haptic_action_),"Create haptic action");
            bindings.push_back({haptic_action_,path("/user/hand/left/output/haptic")});
            bindings.push_back({haptic_action_,path("/user/hand/right/output/haptic")});
        }
        // Touch actions are optional: a failure to create them or bind them never costs an existing control.
        std::vector<XrActionSuggestedBinding> touch_bindings;
        try {
            constexpr std::array<const char*,TouchCount> touch_names{"trigger_touch","thumbstick_touch","first_touch","second_touch","thumbrest_touch"};
            for(UINT index=0;index<TouchCount;++index) {
                XrActionCreateInfo action{};action.type=XR_TYPE_ACTION_CREATE_INFO;action.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;
                action.countSubactionPaths=2;action.subactionPaths=hand_paths_.data();
                strcpy_s(action.actionName,touch_names[index]);strcpy_s(action.localizedActionName,touch_names[index]);
                checked(xrCreateAction(action_set_,&action,&touch_actions_[index]),"Create touch action");
                for(UINT side=0;side<2;++side) {
                    const char* component=index==TriggerTouch?"trigger/touch":index==ThumbstickTouch?"thumbstick/touch":
                        index==ThumbrestTouch?"thumbrest/touch":index==FirstTouch?(side?"a/touch":"x/touch"):(side?"b/touch":"y/touch");
                    const auto binding=std::string(side?"/user/hand/right/input/":"/user/hand/left/input/")+component;
                    touch_bindings.push_back({touch_actions_[index],path(binding.c_str())});
                }
            }
        } catch(...) {touch_actions_={};touch_bindings.clear();touch_create_failed_=true;}
        // OpenXR keeps only the latest suggestion per profile, so exactly one call may succeed: today's list plus touch first, today's list alone if the runtime refuses.
        XrInteractionProfileSuggestedBinding suggest{};suggest.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggest.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
        if(!touch_bindings.empty()) {
            auto combined=bindings;combined.insert(combined.end(),touch_bindings.begin(),touch_bindings.end());
            suggest.countSuggestedBindings=static_cast<uint32_t>(combined.size());suggest.suggestedBindings=combined.data();
            touch_bound_=XR_SUCCEEDED(xrSuggestInteractionProfileBindings(instance_,&suggest));
        }
        if(!touch_bound_) {
            suggest.countSuggestedBindings=static_cast<uint32_t>(bindings.size());suggest.suggestedBindings=bindings.data();
            checked(xrSuggestInteractionProfileBindings(instance_,&suggest),"Bind Quest controls");
        }
        // SteamVR binds a controller only to a profile the app suggested; PSVR2 Sense, Index and WMR-on-SteamVR map to Index or
        // Vive wands, not Touch. Same actions, nearest components; optional, so a refusal never costs the Touch binding.
        auto suggest_extra=[&](const char* profile,auto component_for) noexcept {
            try {
                std::vector<XrActionSuggestedBinding> extra;
                for(UINT index=0;index<InputCount;++index) for(UINT side=0;side<2;++side) {
                    const char* component=component_for(index,side);
                    if(!component) continue;
                    extra.push_back({actions_[index],path((std::string(side?"/user/hand/right/input/":"/user/hand/left/input/")+component).c_str())});
                }
                extra.push_back({haptic_action_,path("/user/hand/left/output/haptic")});
                extra.push_back({haptic_action_,path("/user/hand/right/output/haptic")});
                if(!touch_bindings.empty() && std::string(profile).find("index")!=std::string::npos)
                    for(UINT index=0;index<TouchCount;++index) for(UINT side=0;side<2;++side) {
                        const char* component=index==TriggerTouch?"trigger/touch":index==ThumbstickTouch?"thumbstick/touch":
                            index==FirstTouch?"a/touch":index==SecondTouch?"b/touch":nullptr; // Index has no thumbrest
                        if(component) extra.push_back({touch_actions_[index],path((std::string(side?"/user/hand/right/input/":"/user/hand/left/input/")+component).c_str())});
                    }
                XrInteractionProfileSuggestedBinding other{};other.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
                other.interactionProfile=path(profile);other.countSuggestedBindings=static_cast<uint32_t>(extra.size());other.suggestedBindings=extra.data();
                xrSuggestInteractionProfileBindings(instance_,&other);
            } catch(...) {}
        };
        suggest_extra("/interaction_profiles/valve/index_controller",[](UINT index,UINT side)->const char* {
            switch(index) {
                case Grip:return "grip/pose";case Aim:return "aim/pose";case Stick:return "thumbstick";case Trigger:return "trigger/value";
                case Squeeze:return "squeeze/value";case StickClick:return "thumbstick/click";case FirstButton:return "a/click";
                case SecondButton:return side?"b/click":nullptr; // left B is the pause menu: Index has no menu button
                case Menu:return side?nullptr:"b/click";default:return nullptr;
            }
        });
        suggest_extra("/interaction_profiles/htc/vive_controller",[](UINT index,UINT side)->const char* {
            switch(index) {
                case Grip:return "grip/pose";case Aim:return "aim/pose";case Stick:return "trackpad";case Trigger:return "trigger/value";
                case Squeeze:return "squeeze/click";case StickClick:return "trackpad/click";case Menu:return side?nullptr:"menu/click";
                case FirstButton:return side?"menu/click":nullptr;default:return nullptr; // wands have no face buttons
            }
        });
        XrSessionActionSetsAttachInfo attach{};attach.type=XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attach.countActionSets=1;attach.actionSets=&action_set_;
        checked(xrAttachSessionActionSets(session_,&attach),"Attach game controls");
        for(UINT kind=0;kind<2;++kind) for(UINT side=0;side<2;++side) {
            XrActionSpaceCreateInfo space{};space.type=XR_TYPE_ACTION_SPACE_CREATE_INFO;
            space.action=actions_[kind];space.subactionPath=hand_paths_[side];space.poseInActionSpace.orientation.w=1;
            checked(xrCreateActionSpace(session_,&space,&hand_spaces_[kind][side]),"Create controller space");
        }
    }
    PalmOffset palm_offset_stored(unsigned side) const {
        std::lock_guard<std::mutex> lock(palm_mutex_);
        return palm_.get(side);
    }
    // A confirmed fit is used at once and written by the worker thread from a snapshot (provisionally under Touch while no profile is known).
    // Only the in-memory update runs under palm_mutex_.
    void confirm_palm_offset(unsigned side,const PalmOffset& value) noexcept {
        try {
            std::optional<PalmOffsetStore> snapshot;
            {std::lock_guard<std::mutex> lock(palm_mutex_);if(palm_.confirm(side,value)) snapshot=palm_.store();}
            if(snapshot) PalmSaver::process().submit(std::move(*snapshot));
        } catch(...) {palm_error_.raise(PalmSaver::process().successes());}
    }
    // The runtime reports the bound profile only after the session is focused and it has chosen one; re-ask once a second, never throw.
    void refresh_interaction_profiles() noexcept {
        try {
            const auto now=GetTickCount64();
            if(now<profile_refresh_tick_) return;
            profile_refresh_tick_=now+1000;
            if(!xrGetCurrentInteractionProfile || !xrPathToString) return;
            for(unsigned side=0;side<2;++side) {
                XrInteractionProfileState profile_state{};profile_state.type=XR_TYPE_INTERACTION_PROFILE_STATE;
                if(!XR_SUCCEEDED(xrGetCurrentInteractionProfile(session_,hand_paths_[side],&profile_state)) || profile_state.interactionProfile==XR_NULL_PATH) continue;
                char text[XR_MAX_PATH_LENGTH]{};uint32_t count{};
                if(!XR_SUCCEEDED(xrPathToString(instance_,profile_state.interactionProfile,sizeof(text),&count,text))) continue;
                std::optional<PalmOffsetStore> snapshot;
                {std::lock_guard<std::mutex> lock(palm_mutex_);if(palm_.set_profile(side,text)) snapshot=palm_.store();}
                if(snapshot) PalmSaver::process().submit(std::move(*snapshot)); // a fit deferred until now is keyed under the real profile
            }
        } catch(...) {}
    }
    void sample_input() {
        grip_pose_tick_=0;controller_input={};grip_locations={};aim_locations={};grip_speeds={};grip_velocities={};grip_velocity_hands=0;
        action_sync_attempted=false;last_action_sync=XR_SESSION_NOT_FOCUSED;active_input_hands=0;grip_tracked_hands=0;stick_active_hands=0;
        // Input only while FOCUSED: Virtual Desktop's runtime crashed (LibOVRRT null deref) right after
        // the session left focus while we kept syncing actions / locating hands.
        if(!controller_enabled_ || !continuity_.usable() || !views_valid_ || state!=XR_SESSION_STATE_FOCUSED) return;
        XrActiveActionSet active{action_set_,XR_NULL_PATH};
        XrActionsSyncInfo sync{};sync.type=XR_TYPE_ACTIONS_SYNC_INFO;sync.countActiveActionSets=1;sync.activeActionSets=&active;
        const auto synced=xrSyncActions(session_,&sync);last_action_sync=synced;action_sync_attempted=true;++action_sync_calls;
        checked(synced,"Sync game controls");
        if(synced==XR_SESSION_NOT_FOCUSED) ++action_not_focused;
        if(synced!=XR_SUCCESS) return;
        for(UINT kind=0;kind<2;++kind) for(UINT side=0;side<2;++side) {
            XrActionStateGetInfo get{};get.type=XR_TYPE_ACTION_STATE_GET_INFO;
            get.action=actions_[kind];get.subactionPath=hand_paths_[side];
            XrActionStatePose pose{};pose.type=XR_TYPE_ACTION_STATE_POSE;
            checked(xrGetActionStatePose(session_,&get,&pose),"Read controller pose action");
            auto& location=kind?aim_locations[side]:grip_locations[side];location.type=XR_TYPE_SPACE_LOCATION;
            XrSpaceVelocity velocity{};velocity.type=XR_TYPE_SPACE_VELOCITY;
            if(!kind) location.next=&velocity;
            if(pose.isActive) checked(xrLocateSpace(hand_spaces_[kind][side],local_,locate_display_time_,&location),"Locate game hand");
            location.next=nullptr;
            if(!kind && pose.isActive && (velocity.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT) &&
               std::isfinite(velocity.linearVelocity.x) && std::isfinite(velocity.linearVelocity.y) && std::isfinite(velocity.linearVelocity.z)) {
                const auto speed=std::hypot(velocity.linearVelocity.x,velocity.linearVelocity.y,velocity.linearVelocity.z);
                if(std::isfinite(speed)) {grip_velocities[side]=velocity.linearVelocity;grip_speeds[side]=speed;grip_velocity_hands|=1u<<side;}
            }
        }
        // VALID, not TRACKED: Quest drops the optical TRACKED bits for a moment whenever a controller leaves the
        // cameras' view (low at the hip, at the face, behind the head) while the runtime keeps a valid predicted pose.
        // Requiring TRACKED made the hand vanish and reset the hand's target (blinking prompts).
        constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        for(UINT side=0;side<2;++side) if((grip_locations[side].locationFlags&required)==required) grip_tracked_hands|=1u<<side;
        if(grip_tracked_hands)grip_pose_tick_=GetTickCount64();
        refresh_interaction_profiles();
        // Buttons/sticks are independent actions. A missing grip pose must not
        // disable navigation; pose-driven rig work validates tracking separately.
        auto get_info=[&](UINT action,UINT side) {
            XrActionStateGetInfo info{};info.type=XR_TYPE_ACTION_STATE_GET_INFO;
            info.action=actions_[action];info.subactionPath=hand_paths_[side];return info;
        };
        auto scalar=[&](UINT action,UINT side) {
            auto info=get_info(action,side);XrActionStateFloat value{};value.type=XR_TYPE_ACTION_STATE_FLOAT;
            checked(xrGetActionStateFloat(session_,&info,&value),"Read controller scalar");
            if(value.isActive) active_input_hands|=1u<<side;
            return value.isActive?value.currentState:0.f;
        };
        auto button=[&](UINT action,UINT side) {
            auto info=get_info(action,side);XrActionStateBoolean value{};value.type=XR_TYPE_ACTION_STATE_BOOLEAN;
            checked(xrGetActionStateBoolean(session_,&info,&value),"Read controller button");
            if(value.isActive) active_input_hands|=1u<<side;
            return value.isActive && value.currentState;
        };
        for(UINT side=0;side<2;++side) {
            auto info=get_info(Stick,side);XrActionStateVector2f value{};value.type=XR_TYPE_ACTION_STATE_VECTOR2F;
            checked(xrGetActionStateVector2f(session_,&info,&value),"Read controller stick");
            if(value.isActive) {
                stick_active_hands|=1u<<side;
                active_input_hands|=1u<<side;
                if(side) {controller_input.turn_x=value.currentState.x;controller_input.turn_y=value.currentState.y;}
                else {controller_input.move_x=value.currentState.x;controller_input.move_y=value.currentState.y;}
            }
        }
        controller_input.left_trigger=scalar(Trigger,0);controller_input.right_trigger=scalar(Trigger,1);
        controller_input.left_grip=scalar(Squeeze,0);controller_input.right_grip=scalar(Squeeze,1);
        controller_input.x=button(FirstButton,0);controller_input.a=button(FirstButton,1);
        controller_input.y=button(SecondButton,0);controller_input.b=button(SecondButton,1);
        controller_input.left_click=button(StickClick,0);controller_input.right_click=button(StickClick,1);
        controller_input.menu=button(Menu,0);controller_input.active=active_input_hands!=0;
        {   // Touch never marks a hand active and never throws: an absent, inactive or failing action reads false.
            auto touch=[&](UINT action,UINT side) {
                if(!touch_actions_[action]) return false;
                XrActionStateGetInfo info{};info.type=XR_TYPE_ACTION_STATE_GET_INFO;info.action=touch_actions_[action];info.subactionPath=hand_paths_[side];
                XrActionStateBoolean value{};value.type=XR_TYPE_ACTION_STATE_BOOLEAN;
                return XR_SUCCEEDED(xrGetActionStateBoolean(session_,&info,&value)) && value.isActive && value.currentState;
            };
            controller_input.left_trigger_touch=touch(TriggerTouch,0);controller_input.right_trigger_touch=touch(TriggerTouch,1);
            controller_input.left_thumb_touch=touch(ThumbstickTouch,0)||touch(FirstTouch,0)||touch(SecondTouch,0)||touch(ThumbrestTouch,0);
            controller_input.right_thumb_touch=touch(ThumbstickTouch,1)||touch(FirstTouch,1)||touch(SecondTouch,1)||touch(ThumbrestTouch,1);
            controller_input.touch_bound=touch_bound_;
        }
        if(controller_input.active) ++controller_samples;
    }
public:
    struct TouchStatus {bool bound;bool create_failed;};
    TouchStatus touch_status() const noexcept {return {touch_bound_,touch_create_failed_};}
    // Development harness aims a simulated controller through the real panel
    // geometry. The resulting pose still uses normal XR ray/click processing.
    TrackedPose simulated_native_menu_ray(double u,double v) {
        const auto& panel=native_menu_pose();const auto view=menu_view();
        auto ray=menu_pointer_pose(panel,{u,std::clamp((v-view.v0)/view.vspan,0.,1.)},view.width,view.height);
        ray.position=ray.position+rotated(panel.orientation,{0,0,.4});return ray;
    }
    // CPU spans include driver blocking; they are not GPU execution timings.
    static std::int64_t cpu_time_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    struct FrameTiming {
        bool waited{};
        std::int64_t start_ns{},end_ns{},wait_ns{},begin_ns{},locate_input_ns{},transfer_ns{},submit_ns{};
        XrTime display_time{};
        XrDuration display_period{};
        std::array<XrTime,2> rendered_display_times{};
        std::array<std::int64_t,2> rendered_locate_cpu_times{};
    } timing;
    using TransferObserver=void (*)(UINT,ID3D11Texture2D*,ID3D11Texture2D*,const XrCompositionLayerProjectionView&,bool,bool,ID3D11Texture2D*);
    TransferObserver observe_transfer{}; // base eye copy before reticle/native-art composition; not final submitted pixels
    // v798 diagnostic: each layered stale-eye warp, with what it read (kept image and depth, layers, cameras) and the fresh eye's image.
    using StaleWarpObserver=void (*)(UINT,ID3D11Texture2D*,ID3D11Texture2D*,ID3D11Texture2D*,const StaleDepth&,const StaleDepth&,const StaleLayers&);
    StaleWarpObserver observe_stale_warp{};
    std::array<double,2> stale_camera_motion(UINT eye) const noexcept {return eye<2?stale_camera_motion_[eye]:std::array<double,2>{};} // v1177
    void (*observe_fresh_eye)(UINT eye,ID3D11Texture2D* image,std::uint64_t frame){}; // v802 diagnostic: each fresh eye image as it is kept
    using NoteCompositionObserver=void (*)(unsigned,ID3D11Texture2D*,const XrCompositionLayerProjectionView&,const NotePanelReceipt&);
    NoteCompositionObserver observe_note_composition{};
    void (*observe_final_eye)(unsigned eye,ID3D11Texture2D* texture){}; // v700 diagnostics (build/final-eye-capture): the eye image with every native widget drawn
    // Read only from observe_transfer on the presenting thread. Pointer
    // inequality alone cannot distinguish SMAA from CAS or a kept eye.
    EyeTransferProcessing transfer_processing(UINT eye) const noexcept {return eye<2?transfer_processing_[eye]:EyeTransferProcessing{};}
    using SettingsObserver=void (*)(ID3D11Texture2D*,const unsigned char*,std::size_t);
    SettingsObserver observe_settings{};
    using SettingsCompositionObserver=void (*)(ID3D11Texture2D*,ID3D11Texture2D*,const SettingsMenuLayout::Rect&);
    SettingsCompositionObserver observe_settings_composition{};
    // Short controller vibration; ignored when unfocused or unsupported.
    void pulse(unsigned side,float amplitude,float seconds) noexcept {
        // Only a currently tracked controller: VD's runtime crashed (null+8 in LibOVRRT) three times
        // on the render thread; vibrating an asleep/untracked controller is the suspected trigger.
        if(!haptic_action_ || side>1 || state!=XR_SESSION_STATE_FOCUSED || !running_ || !(grip_tracked_hands&(1u<<side))) return;
        XrHapticVibration vibration{};vibration.type=XR_TYPE_HAPTIC_VIBRATION;vibration.amplitude=amplitude;
        vibration.duration=static_cast<XrDuration>(seconds*1e9);vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
        XrHapticActionInfo info{};info.type=XR_TYPE_HAPTIC_ACTION_INFO;info.action=haptic_action_;info.subactionPath=hand_paths_[side];
        xrApplyHapticFeedback(session_,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    }
    bool edge_antialiasing_enabled() const noexcept {return antialiasing_enabled_;}
    UINT64 hint_layers{};
    // Comfort vignette: the opacity (0..1) is set by the render thread each frame; knob `no-comfort-vignette` clears the enable flag
    // before the session starts, so no chain, space or extension is ever made.
    std::atomic<float> vignette_opacity{0};static inline bool vignette_enabled=true,vignette_color_scale_requested=false; UINT64 vignette_layers{},vignette_bakes{}; // vignette_enabled is class-wide: set before the first GameXr exists (the instance extensions and VIEW space follow it)
    bool vignette_color_scale() const noexcept {return color_scale_supported_;}
    std::array<UINT64,4> layer_pose_repaired{},layer_pose_dropped{};UINT64 layer_pose_no_submit{};
    std::string layer_pose_last; // kind and raw pose of the most recent offending layer
    UINT64 end_frame_rejections{};unsigned end_frame_rejected_run_{};void (*observe_frame_rejection)(const char*){}; // layer errors dropped as one frame
    UINT64 subrect_layers{}; // quad/cylinder layers submitted with a rect smaller than their swapchain; must stay 0
    UINT64 swapchain_creates_in_frame{}; // xrCreateSwapchain/xrDestroySwapchain between xrBeginFrame and xrEndFrame; must stay 0
    // Frames that would have gone out with zero layers while the session shows frames (xr_frame_hardening.hpp), and what was sent instead.
    UINT64 empty_frames_seen{},empty_fallback_projection{},empty_fallback_black{},empty_fallback_none{};
    std::array<UINT64,static_cast<unsigned>(EmptyFrameReason::count)> empty_frames_by_reason{};
    UINT64 chain_create_events{},chain_destroy_events{};
    void (*observe_xr_event)(const char*){}; // timestamped swapchain / empty-frame lines for xr-recoveries.log (already rate limited here)
    std::array<UINT64,2> wrist_layers{};
    UINT64 wrist_failures{},wrist_timeouts{},wrist_target_creations{},wrist_retirement_waits{};
    HRESULT wrist_failure_hresult{S_OK},wrist_device_removed{S_OK};
    D3D11_TEXTURE2D_DESC wrist_failure_texture{};
    std::string wrist_failure_reason;
    void (*observe_wrist_failure)(unsigned,const char*){};
    void (*observe_wrist)(unsigned,const std::uint8_t*,std::size_t,const WristHudSnapshot&){};
    void wrist_hud(WristHudSnapshot data) {wrist_data_=data;}
    // v1013/v1015 knobs (set before the first frame): the watch is on by default, build/no-wrist-hud turns it off,
    // build/legacy-wrist-hud restores the pre-v1013 two-wrist inner-side layout, build/wrist-hud-size is 4..15 cm.
    // build/wrist-hud-backdrop opts into a dark card behind the game cluster for readability A/B; it is off by default.
    bool wrist_hud_enabled=true,legacy_wrist_hud=false,wrist_haptic_enabled=true,wrist_hud_backdrop=false;
    double wrist_hud_size_cm=8.5;
    bool wrist_watch_wanted() const noexcept {return wrist_hud_enabled && !legacy_wrist_hud && running_ && state==XR_SESSION_STATE_FOCUSED;}
    double wrist_watch_opacity() const noexcept {return wrist_watch_alpha_;}
    bool wrist_watch_shown() const noexcept {return wrist_watch_state_.shown();}
    bool wrist_watch_cluster_ready() const noexcept {return wrist_watch_cluster_.fresh(pose_epoch(),GetTickCount64());}
    DXGI_FORMAT wrist_watch_target_format() const noexcept {return wrist_watch_face_format();}
    UINT64 wrist_watch_composite_rejects() const noexcept {return wrist_watch_blit_rejects_;}
    UINT64 wrist_watch_cluster_rejects() const noexcept {return wrist_watch_cluster_rejects_;}
    UINT64 wrist_watch_draws() const noexcept {return wrist_watch_draws_;}
    UINT64 wrist_watch_cluster_updates() const noexcept {return wrist_watch_cluster_updates_;}
    void (*observe_native_wrist_art)(unsigned,ID3D11Texture2D*,std::uintptr_t){};
    UINT64 native_object_art_draws{};
    // v715: per object-art slot, eye draws / fresh art with no rect (off view or behind) / fresh art skipped (lock unresolved); last rect.
    std::array<UINT64,33> native_object_art_slot_draws{},native_object_art_slot_norect{},native_object_art_slot_unlocked{};std::array<std::array<float,4>,33> native_object_art_slot_rect{};
    // v685: eye art is decoration. A failed draw is recorded and its art dropped; it never reaches the
    // render callback's session recovery (that ended VR three times in the v684 session).
    UINT64 native_eye_art_failures{},native_eye_art_rejects{},native_note_eye_art_failures{};
    UINT64 world_ui_keyed{},world_ui_key_failures{}; // v721 dialogue choice art keyed / fell back to a plain copy
    bool native_eye_art_disabled_{},native_note_art_disabled_{};
    HRESULT native_eye_art_last_hr{S_OK};DXGI_FORMAT native_eye_art_eye_view{},native_eye_art_art_view{};
    std::string native_eye_art_last_call;
    void (*observe_eye_art_failure)(const char*){};
    void note_eye_art_fault(const NativeEyeArtResult& result,const char* who) noexcept {
        if(result.rejected) {++native_eye_art_rejects;return;}
        ++native_eye_art_failures;native_eye_art_last_hr=result.hr;
        native_eye_art_last_call=result.call?result.call:"draw";
        native_eye_art_eye_view=result.eye_view;native_eye_art_art_view=result.art_view;
        if(native_eye_art_failures!=1 || !observe_eye_art_failure) return;
        char text[240];
        std::snprintf(text,sizeof(text),"Eye art draw failed (%s hr=0x%08lx eye=%u art=%u); the overlay is dropped and VR keeps presenting",
            who,static_cast<unsigned long>(result.hr),static_cast<unsigned>(result.eye_view),static_cast<unsigned>(result.art_view));
        observe_eye_art_failure(text);
    }
    bool ensure_native_eye_art() noexcept {
        if(native_eye_art_disabled_) return false;
        if(native_eye_art_) return true;
        try {
            Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
            if(!device) throw std::runtime_error("Missing native eye artwork device");
            native_eye_art_=std::make_unique<NativeEyeArt>(device.Get());
            return true;
        } catch(const std::exception& error) {
            native_eye_art_disabled_=true;native_eye_art_last_call="NativeEyeArt";
            if(observe_eye_art_failure) {
                const std::string text=std::string("Eye art unavailable (")+error.what()+"); VR keeps presenting";
                observe_eye_art_failure(text.c_str());
            }
            return false;
        }
    }
    bool native_art_presented(unsigned slot,bool wrist) const noexcept {
        return last_layer_submitted && (wrist?(slot<native_wrist_art_shown_.size() && native_wrist_art_shown_[slot]):
            (slot<native_object_art_masks_.size() && native_object_art_masks_[slot]==3));
    }
    bool native_art_in_eye(unsigned eye) const noexcept {
        if(eye>=2)return false;
        for(auto mask:native_object_art_masks_)if(mask&(1u<<eye))return true;
        return false;
    }
    // Producer contract: only isolated native art, exact child owner and fresh
    // capture may be published. A stale/replaced owner produces no replica.
    bool native_art_source(unsigned slot,bool wrist,ID3D11Texture2D* texture,D3D11_BOX crop,
        std::uintptr_t owner,UINT64 epoch,UINT64 tick,Vec3 point={},double width=.18,bool premultiplied=true) {
        if((wrist && slot>=native_wrist_art_.size()) || (!wrist && slot>=native_object_art_.size()))return false;
        auto& source=wrist?native_wrist_art_[slot]:native_object_art_[slot];
        if(!texture){source={};return true;}
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);Microsoft::WRL::ComPtr<ID3D11Device> device,actual;
        context_->GetDevice(&device);texture->GetDevice(&actual);
        if(!owner || device.Get()!=actual.Get() || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
           (desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) ||
           crop.left>=crop.right || crop.top>=crop.bottom || crop.right>desc.Width || crop.bottom>desc.Height || crop.front || crop.back!=1 ||
           (!wrist && (!finite(point) || !std::isfinite(width) || width<=0 || width>64))) {source={};return false;}
        source={texture,crop,point,width,epoch,tick,owner,premultiplied};return true;
    }
    // v710: pin an object-art slot published this frame to the head (offset in head space, metres: x right, y up, -z ahead).
    void native_art_lock(unsigned slot,Vec3 offset) noexcept {
        if(slot<native_object_art_.size() && finite(offset)) {native_object_art_[slot].locked=true;native_object_art_[slot].lock_offset=offset;}
    }
    // Publication is authorized by ranged_aim_gate.reticle. Native HUD clipping
    // cannot authorize visibility of a reticle drawn across the full eye FOV.
    UINT64 eye_reticle_draws{},crosshair_art_draws{};
    // v688: the game's own crosshair element (captured live, every frame) replaces the static dot at the same reticle point.
    // anchor: the element's origin inside crop (source pixels); scale: eye pixels per source pixel.
    void crosshair_art(ID3D11Texture2D* texture,D3D11_BOX crop,float anchor_x,float anchor_y,float source_width,UINT64 epoch,UINT64 tick) noexcept {
        crosshair_art_={};
        if(!texture || !(source_width>0.f) || !std::isfinite(anchor_x) || !std::isfinite(anchor_y))return;
        crosshair_art_={texture,crop,anchor_x,anchor_y,static_cast<float>(width_)/source_width,epoch,tick};
    }
    bool eye_reticle_available() const noexcept {return !eye_reticle_failed_;}
    void eye_reticle(unsigned eye,EyeReticle value) noexcept {eye_reticles_.publish(eye,value);}
    void clear_eye_reticles() noexcept {eye_reticles_.clear();}
    std::array<std::optional<Vec3>,2> interaction_targets;
    void interaction_target(int side,std::optional<Vec3> point) noexcept {if(side==0 || side==1) interaction_targets[static_cast<unsigned>(side)]=point;}
    MirrorMode mirror_mode() const noexcept {return settings_.mirror();}
    double sharpness() const noexcept {return settings_.sharpness();}
    UINT64 mirrored_frames{};
    UINT64 antialiased_eyes() const noexcept {return antialiasing_?antialiasing_->eye_passes:0;}
    bool settings_open() const noexcept {return settings_.open();}
    // The controls page: the right temple squeeze and the VR Settings "Controls & gestures" row toggle it; it opens in front of the head.
    void toggle_guide(bool from_menu=false) noexcept {guide_placed_=false;guide_open_=!guide_open_.load();guide_from_menu_=from_menu && guide_open_.load();
        if(guide_open_) {guide_page_=0;guide_refresh_=true;guide_navigation_.claim();}}
    void guide_page_delta(int delta) noexcept {if(delta && guide_open_) {guide_page_=guide_page_step(guide_page_.load(),delta);guide_refresh_=true;}}
    bool guide_open() const noexcept {return guide_open_.load();}
    bool guide_stick_blocked() const noexcept {return guide_open() || guide_navigation_.blocked();}
    std::atomic<bool> settings_requested{false}; // native menu "VR Settings" item (native_menu_entry.hpp)
    // Hold left Y in gameplay opens native Pause; a fresh hold in Pause opens settings. The game thread sets y_hold_blocked while a Talk prompt is
    // offered to Y or the wheel owns it; build/no-hold-y-menu clears y_hold_enabled.
    std::atomic<bool> y_hold_blocked{false};
    // Asked on the fire frame of a gameplay hold: does a Talk offer or the wheel own Y right now (this frame, not the last controls pass)?
    std::atomic<bool (*)()> y_hold_owned_now{nullptr};
    bool y_hold_enabled=true,legacy_hold_y_settings=false;
    std::atomic<UINT64> y_hold_fires{0},y_hold_blocked_presses{0},y_hold_last_ms{0},old_panel_blocked{0};   // old_panel_blocked (v1073): drawn-panel opens closed before drawing
    void avatar_measurements(NativeAvatarMeasurements value) {
        avatar_measurements_=value;
        // Keep the player's body across reloads/avatar/reference changes: refit without a new pose.
        if(avatar_calibration_.restore(reference_epoch(),value)) settings_.calibration_status(avatar_calibration_.state());
    }
    void avatar_runtime_status(AvatarFitRuntimeStatus status) {settings_.calibration_runtime(status);}
    AvatarFitRuntimeStatus avatar_runtime_status() const {return settings_.calibration_runtime();}
    UINT64 grip_pose_tick() const {return grip_pose_tick_;}
    // The interaction profile bound to a hand, or the one this mod suggests while the runtime has not said (Plan Spec deviation 7).
    std::string interaction_profile(unsigned side) const {
        std::lock_guard<std::mutex> lock(palm_mutex_);
        return palm_.profile(side);
    }
    // XR thread only (the thread that owns settings_): hand the current preview to the other threads.
    void publish_palm_preview() noexcept {try {palm_preview_.publish({settings_.palm_preview(0),settings_.palm_preview(1)});} catch(...) {}}
    // The grip-to-palm offset the hand uses now: the preview while the hand-fit page is open, else the saved value for this side and
    // profile, else the default (C6, C12). saved: the value came from the file or from a confirmed fit.
    PalmOffset palm_offset(unsigned side,bool* saved=nullptr) const {
        std::lock_guard<std::mutex> lock(palm_mutex_);
        side&=1u;
        bool stored_saved{};const auto stored=palm_.get(side,&stored_saved);
        if(saved) *saved=stored_saved;
        if(const auto preview=palm_preview_.get(side)) return *preview;
        return stored;
    }
    // Status JSON fragment: "palm_offset":{...} (no leading comma).
    std::string palm_offset_status() const {
        std::ostringstream o;bool error;
        error=palm_error_.active(PalmSaver::process().successes())||PalmSaver::process().error();
        o<<"\"palm_offset\":{\"source\":[";
        for(unsigned side=0;side<2;++side) {bool saved{};palm_offset(side,&saved);o<<(side?",":"")<<(saved?"\"saved\"":"\"default\"");}
        o<<"],\"profile\":[";
        for(unsigned side=0;side<2;++side) o<<(side?",":"")<<'"'<<interaction_profile(side)<<'"';
        o<<"],\"error\":"<<(error?"true":"false")<<'}';
        return o.str();
    }
    std::optional<double> calibration_floor_y() const {return calibration_floor_y_;}
    AvatarCalibrationState avatar_calibration_state() const {return avatar_calibration_.state();}
    void suspend_avatar() {
        avatar_measurements_.reset();
        if(avatar_calibration_.result(reference_epoch()))settings_.calibration_runtime(AvatarFitRuntimeStatus::verifying);
        settings_.calibration_status(avatar_calibration_.synchronize(reference_epoch(),false));
    }
    void reset_avatar() {avatar_measurements_.reset();avatar_calibration_.invalidate_rig();settings_.calibration_status(avatar_calibration_.state());}
    // Called only for a confirmed native owner/resource identity replacement.
    // Accepted compatible reuse is independently reapplied by the native fit lane.
    void avatar_identity_changed() {
        avatar_measurements_.reset();avatar_calibration_.invalidate_rig();
        settings_.calibration_status(avatar_calibration_.state());settings_.calibration_identity_changed();
    }
    std::optional<AvatarCalibrationResult> avatar_calibration() const {return avatar_calibration_.result(reference_epoch());}
    void reject_avatar_dimensions() {avatar_calibration_.reject_dimensions();settings_.calibration_status(avatar_calibration_.state());}
    UINT64 settings_layers{},settings_world_layers{};
    UINT64 world_ui_layers{};
    std::array<UINT64,3> world_ui_layers_by_kind{};
    bool last_world_ui_layer{};
    std::optional<double> take_resolution_request() noexcept {const auto request=resolution_change_;resolution_change_.reset();return request;}
    std::optional<int> take_hud_request() noexcept {const auto request=hud_change_;hud_change_.reset();return request;}
    bool crosshair_visible() const noexcept {return settings_.crosshair();}
    VrPreferences preferences() const {return settings_.preferences();}
    bool scripted_recenter() const noexcept {return settings_.scripted_recenter();}
    CutsceneMode cutscene_mode() const noexcept {return settings_.cutscene_mode();}
    // The player's own Upscaler scale (percent) for the selected upscaler mode; 0 = that mode's preset.
    int upscale_custom_percent() const noexcept {return settings_.upscale_custom_percent();}
    void dlss_quality_percent(int percent) noexcept {settings_.dlss_quality_percent(percent);}
    // v609 first-start AA: the decided mode (none = keep the saved one) goes out as a temporal request, the marker is set and the file is written once.
    void adopt_first_start_aa(std::optional<int> mode) {
        if(mode) {settings_.set_temporal_aa(*mode);temporal_change_=settings_.temporal_aa();} else settings_.mark_aa_chosen();
        persist_preferences();
    }
    void(*log_note)(const char*) noexcept=nullptr; // the host's recoverable-fault log, set from Present
    // Writes the saved settings; false when there is no file or the save failed (logged), so callers can keep legacy knobs until it works.
    bool persist_preferences() {
        if(preferences_file_.empty()) return false;
        try {save_preferences(preferences_file_,settings_.preferences());return true;}
        catch(const std::exception&) {if(log_note) log_note("v609 first-start save failed: the settings stay unsaved and the next start decides again");return false;}
    }
    std::optional<int> take_temporal_request() noexcept {const auto request=temporal_change_;temporal_change_.reset();return request;}
    std::optional<TurnControl::Config> take_turning_request() noexcept {const auto request=turning_change_;turning_change_.reset();return request;}
    void resolution_status(double effective) noexcept {effective_scale_=effective;}
    UINT64 submitted{},dropped{},menu_layers{};
    UINT64 controller_samples{};
    UINT64 authored_camera_layers{};
    bool last_should_render{},last_views_valid{},last_tracked_pair{};
    // G2 cutscene hold (camera plan Task 5, fix round 1), set by the probe before each present() and consumed by it. The probe passes no eye
    // images on a held frame. black: a neutral black frame (a cutscene's pair is forming, or head-written images are still in flight after a
    // fallback). replay: the previous immersive stereo layer of THIS scene (scene_id), else black; never any other projection.
    enum class SceneHold {none,black,replay};
    SceneHold scene_hold{SceneHold::none};
    std::uint64_t scene_id{};          // the cutscene an authored_scene_render frame belongs to (0 = none)
    // alternate-eye (plan F task 1), set by the probe before each present(). Off by default: every branch below is gated on alternate_enabled.
    bool alternate_enabled{};          // knob on and not fallen back
    int alternate_fresh_eye{-1};       // the one eye rendered for this displayed frame, -1 = both (paired frame)
    std::uint64_t alternate_frame{};   // displayed-frame number of this present (the probe's Present counter, first frame 1)
    bool alternate_error{};            // an eye image could not be kept; the probe falls back to paired for the session
    std::uint64_t alternate_kept_frame(int eye) const noexcept {return (eye==0 || eye==1) && alt_records_[static_cast<std::size_t>(eye)].valid?alt_records_[static_cast<std::size_t>(eye)].frame:0;}
    // v792 stale-eye warp (knob build/no-stale-eye-warp turns it off): the probe hands over, for the Present `frame`, the view the eye that
    // was NOT rendered has at the fresh eye's Setup; that eye's kept image is warped to it and its layer carries it (stale_eye_warp.hpp).
    bool stale_eye_warp_enabled{};
    bool steady_pacing{true}; // v841: paired cadence locked to whole refresh periods (frame_pacer.hpp); knob build/legacy-pacing
    bool pacer_miss_lock{false}; // v890: the v844 miss-rate lock (45 Hz when >15% of frames miss a slot); knob build/pacer-miss-lock
    bool median_horizon{true}; // v890: locate at D + the median measured display step (DisplayHorizon); knob build/legacy-display-horizon
    bool gpu_queue_limit{true}; // v840: one frame in flight (present waits for the previous submit's GPU fence); knob build/legacy-gpu-queue
    int locate_periods{2}; // v794: display periods between this Present's predicted display and the one that shows the image located now
    std::uint64_t stale_eye_warps{},stale_eye_warp_failures{};
    std::uint64_t hand_fits() const noexcept {return stale_warp_?stale_warp_->fit_passes:0;}             // v811: hand fits run
    std::uint64_t hand_fit_corrections() const noexcept {return stale_warp_?stale_warp_->fit_corrections:0;} // and read back with a correction
    std::uint64_t uncanted_eyes{}; // v808: canted eye images resampled into their own located view before submission
    void stale_eye_target(UINT eye,const XrView& view,std::uint64_t frame) noexcept {if(eye<2) {stale_target_[eye]=view;stale_target_frame_[eye]=frame;}}
    // v796 parallax (knob build/no-stale-eye-parallax turns it off): for the Present `frame`, the game camera of an image it shows, the
    // stale eye's game camera at the fresh eye's Setup, and the scene depth the stale eye's kept image was rendered with (FSR/DLSS copy).
    bool stale_eye_parallax_enabled{};
    std::uint64_t stale_eye_parallax_misses{}; // warps with parallax on but no camera pair, no depth or no valid geometry: rotation only
    std::uint64_t stale_parallax_passes() const noexcept {return stale_warp_?stale_warp_->parallax_passes:0;}
    // v798: near content of the stale eye from the fresh eye's image of this Present (its camera, depth and kept image), within this
    // many metres (knob build/stale-near-range; 0: the kept image alone, v796).
    double stale_near_range{near_range_default};
    std::uint64_t stale_fresh_passes() const noexcept {return stale_warp_?stale_warp_->fresh_passes:0;}
    std::uint64_t stale_fresh_refusals() const noexcept {return stale_warp_?stale_warp_->fresh_refusals:0;}
    std::uint64_t stale_motion_passes() const noexcept {return stale_warp_?stale_warp_->motion_passes:0;}   // v1151
    std::uint64_t stale_motion_refusals() const noexcept {return stale_warp_?stale_warp_->motion_refusals:0;}
    // v984: how the paired cadence moved (frame_pacer.hpp). k=2 means half rate; locks/unlocks should track scene changes, not a
    // probe timer. Read from the status thread while the render thread writes: telemetry only.
    std::string pacer_status_json() const {
        std::ostringstream o;
        o<<"{\"k\":"<<pacer_.k<<",\"locks\":"<<pacer_.locks<<",\"unlocks\":"<<pacer_.unlocks
         <<",\"paced_frames\":"<<pacer_.paced_frames<<",\"frames\":"<<pacer_.frames
         <<",\"work_ms_p50\":"<<(static_cast<double>(pacer_.work_median())/1e6)<<",\"base_ms\":"<<(static_cast<double>(pacer_.base)/1e6)<<'}';
        return o.str();
    }
    void record_rendered_camera(UINT eye,const WarpCamera& camera,std::uint64_t frame) noexcept {if(eye<2) {rendered_cameras_[eye]=camera;rendered_camera_frame_[eye]=frame;}}
    void stale_eye_target_camera(UINT eye,const WarpCamera& camera,std::uint64_t frame) noexcept {if(eye<2) {stale_target_cameras_[eye]=camera;stale_target_camera_frame_[eye]=frame;}}
    // v1184: the play spaces recorded with those cameras (same frames); kept follows the kept camera. play_space(eye) = {kept, target} of the
    // eye's last rebuild (the warp engineer's input; the dump meta writes it)
    void record_rendered_play(UINT eye,const WarpPlaySpace& play) noexcept {if(eye<2) rendered_play_[eye]=play;}
    // v1192 (sim-pose-hold dumps): the stale eye's target camera and this Present's rendered camera / play space / depth of an eye
    WarpCamera stale_target_camera(UINT eye) const noexcept {return eye<2?stale_target_cameras_[eye]:WarpCamera{};}
    WarpCamera rendered_camera(UINT eye) const noexcept {return eye<2 && rendered_camera_frame_[eye]==alternate_frame?rendered_cameras_[eye]:WarpCamera{};}
    WarpPlaySpace rendered_play(UINT eye) const noexcept {return eye<2?rendered_play_[eye]:WarpPlaySpace{};}
    StaleDepth stale_depth_now(UINT eye) const noexcept {
        if(eye>1 || stale_depth_frame_[eye]!=alternate_frame || !stale_depth_[eye]) return {};
        return {stale_depth_[eye].Get(),stale_depth_width_[eye],stale_depth_height_[eye]};
    }
    void stale_eye_target_play(UINT eye,const WarpPlaySpace& play) noexcept {if(eye<2) stale_target_play_[eye]=play;}
    std::array<WarpPlaySpace,2> play_space(UINT eye) const noexcept {return eye<2?std::array<WarpPlaySpace,2>{kept_play_[eye],stale_target_play_[eye]}:std::array<WarpPlaySpace,2>{};}
    bool stale_afw2=false; // v1184: knob build/afw2 (src/afw2.hpp, opt-in): StaleLayers::afw2
    std::uint64_t stale_afw2_passes() const noexcept {return stale_warp_ && stale_warp_->afw2?stale_warp_->afw2->passes:0;}
    // v1151 movers (design 2026-11-11 section 3): the eye's motion texture for this Present (FsrTaa::eye_motion; null clears it), the
    // two frame-time parts and the rules switch (build/no-stale-eye-motion), copied into the layers of each warp.
    void stale_eye_motion(UINT eye,ID3D11Texture2D* motion,UINT width,UINT height,std::uint64_t frame) noexcept {
        if(eye<2) {stale_motion_[eye]=motion;stale_motion_width_[eye]=width;stale_motion_height_[eye]=height;stale_motion_frame_[eye]=motion?frame:0;}
    }
    // v1190: the rig target the fresh eye's render drew for `eye` (the stale eye's target camera; FsrTaa::eye_rig_target), for this Present
    void stale_eye_rig_target(UINT eye,ID3D11Texture2D* target,UINT width,UINT height,std::uint64_t frame,bool valid) noexcept {
        if(eye<2) {stale_rig_target_[eye]=target;stale_rig_target_w_[eye]=width;stale_rig_target_h_[eye]=height;stale_rig_target_frame_[eye]=target?frame:0;stale_rig_target_valid_[eye]=target && valid;}
    }
    StaleRig stale_rig_target(UINT eye) const noexcept {
        if(eye>1 || !stale_rig_target_valid_[eye] || stale_rig_target_frame_[eye]!=alternate_frame || !stale_rig_target_[eye]) return {};
        return {stale_rig_target_[eye].Get(),stale_rig_target_w_[eye],stale_rig_target_h_[eye]};
    }
    void stale_eye_rig(UINT eye,ID3D11Texture2D* rig,UINT width,UINT height,std::uint64_t frame,bool live=false,bool motion=false) noexcept { // v1160; v1162 live; v1167 motion
        if(eye<2) {stale_rig_[eye]=rig;stale_rig_width_[eye]=width;stale_rig_height_[eye]=height;stale_rig_frame_[eye]=rig?frame:0;stale_rig_live_[eye]=rig && live;stale_rig_motion_[eye]=rig && live && motion;}
    }
    // v1167: that eye's last render's rig pixels carry its own-eye motion (FsrTaa rig_motion), for this Present only
    bool stale_rig_motion(UINT eye) const noexcept {return eye<2 && stale_rig_motion_[eye] && stale_rig_frame_[eye]==alternate_frame;}
    bool stale_rig_motion_rules=true; // v1167: build/no-stale-eye-rig-motion = false
    // v1162: the warp takes the rig masks only when both renders' masks are marked ones of this Present (knob build/no-stale-eye-rig: never)
    StaleRig stale_rig_mask(UINT eye) const noexcept {
        if(!stale_rig_masks || eye>1 || !stale_rig_live_[eye] || stale_rig_frame_[eye]!=alternate_frame || !stale_rig_[eye]) return {};
        return {stale_rig_[eye].Get(),stale_rig_width_[eye],stale_rig_height_[eye]};
    }
    bool stale_rig_masks=true;std::uint64_t stale_rig_warps=0,stale_rig_one_sided=0; // v1162: warps given both masks; warps with only one live
    StaleDepth stale_rig(UINT eye) const noexcept { // v1160: this Present's (R8, 0/255), else none
        if(eye>1 || stale_rig_frame_[eye]!=alternate_frame || !stale_rig_[eye]) return {};
        return {stale_rig_[eye].Get(),stale_rig_width_[eye],stale_rig_height_[eye]};
    }
    StaleMotion stale_motion(UINT eye) const noexcept { // this Present's, else none
        if(eye>1 || stale_motion_frame_[eye]!=alternate_frame || !stale_motion_[eye]) return {};
        return {stale_motion_[eye].Get(),stale_motion_width_[eye],stale_motion_height_[eye]};
    }
    double stale_fresh_part=.5,stale_kept_part=.5;bool stale_motion_rules=true;
    void stale_eye_depth(UINT eye,ID3D11Texture2D* depth,UINT width,UINT height,std::uint64_t frame) noexcept {
        if(eye<2) {stale_depth_[eye]=depth;stale_depth_width_[eye]=width;stale_depth_height_[eye]=height;stale_depth_frame_[eye]=frame;}
    }
    bool last_layer_submitted{},last_menu_layer{};
    bool last_authored_camera_layer{};
    UINT64 state_events{},reference_events{};
    UINT64 action_sync_calls{},action_not_focused{};
    XrResult last_action_sync=XR_SESSION_NOT_FOCUSED;
    bool action_sync_attempted{};
    UINT active_input_hands{},grip_tracked_hands{},stick_active_hands{};
    TrackedPose last_menu_pose{};
    std::uint64_t pose_epoch() const noexcept {return continuity_.epoch();}
    std::uint64_t reference_epoch() const noexcept {return continuity_.reference_epoch();}
    XrTime tracking_prediction_time() const noexcept {return locate_display_time_;}
    XrViewStateFlags tracking_flags() const noexcept {return located_view_flags_;}
    void upscaler_generation(unsigned generation) noexcept {settings_.upscaler_generation(generation);}
    void performance_mode(bool value) noexcept {settings_.performance_mode(value);}
    void effective_temporal_aa(int mode) noexcept {settings_.effective_temporal_aa(mode);}
    void dlss_available(bool value) noexcept {settings_.dlss_available(value);} // offers the DLAA/DLSS Quality rows (NVIDIA GPU + user's nvngx_dlss.dll)
    // Latest completed HUD window surface (or null to hide it); set each frame by the producer.
    // Same hold as world_ui: a frame without a fresh panel capture re-shows the last image (up to 400 ms)
    // and keeps the window's placement, instead of blinking and re-placing the window.
    // draw_tick: when the engine last drew into the captured surface (0 = unknown: the image is then redrawn every frame).
    void hud_window(ID3D11Texture2D* source,std::uint64_t draw_tick=0) {
        const auto now=GetTickCount64();
        if(!source && hud_window_source_ && hud_window_chain_ && now-hud_window_fresh_tick_<400) {hud_window_stale_=true;return;}
        if(!source) hud_window_.reset();
        hud_window_source_=source;hud_window_stale_=false;if(source) hud_window_fresh_tick_=now;
        hud_window_draw_tick_=draw_tick?draw_tick:++unknown_draw_tick_;
    }
    UINT64 hud_window_layers{};
    // A frame without a completed capture re-shows the last released image (up to 400 ms) instead of
    // dropping the layer: dropping it made the open wheel blink while its art updated.
    // full_height: the movie lays controls at its top/bottom edges, outside the 16:9 band (the black market, sim v381: its
    // category bar, money and Purchase/Back rows sat at 78-96% of the height and were cut off).
    void world_ui(ID3D11Texture2D* source,unsigned kind,bool full_height=false,std::uint64_t draw_tick=0,bool dialogue=false,std::optional<Vec3> speaker={}) {
        if(kind>3) kind=0;world_ui_full_=kind==1 || full_height;
        if(dialogue!=world_ui_dialogue_) {world_ui_dialogue_=dialogue;world_ui_pose_valid_=false;}
        if(!world_ui_pose_valid_) world_ui_speaker_=dialogue?speaker:std::nullopt; // fixed where the choices opened
        if(!kind) {world_ui_kind_=0;world_ui_source_.Reset();world_ui_pose_valid_=false;world_ui_stale_=false;return;}
        const auto now=GetTickCount64();
        if(!source && kind==world_ui_kind_ && world_ui_source_ && world_ui_chain_ && now-world_ui_fresh_tick_<400) {world_ui_stale_=true;return;}
        if(kind!=world_ui_kind_) world_ui_pose_valid_=false;
        world_ui_kind_=kind;world_ui_source_=source;world_ui_stale_=false;if(source) world_ui_fresh_tick_=now;
        world_ui_draw_tick_=draw_tick?draw_tick:++unknown_draw_tick_;
    }
    ControllerInput controller_input{};
    ControllerInput guide_temple_input{}; // raw pre-settings squeeze/stick sample for the guide's temple ownership
    NativeMenuInput native_menu_input{};
    bool last_pointer_layer{};
    UINT64 pointer_layers{};
    std::array<XrSpaceLocation,2> grip_locations{},aim_locations{};
    // Wheel pointer dot (local space point, or none): drawn for the next submitted frame only; re-set each frame.
    void wheel_cursor(std::optional<Vec3> point) {wheel_cursor_=point;}
    bool test_unlock_requested=false; // VR settings "Test: unlock all"; dxgi applies and clears it
    bool cylinder_layers() const noexcept {return cylinder_supported_;} // curved HUD screen in use
    int menu_pointer_hand=1; // native Iggy pointer hand: right, or the wheel cursor hand while the wheel is open
    // v773 (user, headset v772: the hand wheel stopped answering once the hand passed through it): the hand wheel's choice is a direction (v776: the
    // wedge its hand points at, WheelPointer::update_plane), so the game's pointer is put on the ring in that direction (ring_m: the ring radius in
    // metres on the quad), never where the aim ray meets the quad. on=false: no pointer (the hand points at the centre or at a button).
    void wheel_dial(bool on,float x,float y,float ring_m) noexcept {wheel_dial_x_.store(x);wheel_dial_y_.store(y);wheel_dial_r_.store(ring_m);wheel_dial_on_.store(on);}
    std::atomic<unsigned> wheel_gate_fail_or{};std::atomic<unsigned long long> wheel_gate_frames{},wheel_gate_pass{}; // v1129 diagnostic (pointer gate on dial frames)
    std::atomic<bool> wheel_dial_on_{},hand_wheel_mode{};std::atomic<float> wheel_dial_x_{},wheel_dial_y_{},wheel_dial_r_{}; // hand_wheel_mode: dual wield (set every frame)
    std::array<float,2> grip_speeds{}; // Metres per second, when the runtime reports velocity.
    unsigned grip_velocity_hands{}; // Missing velocity is not a stationary hand.
    std::array<XrVector3f,2> grip_velocities{}; // Same, as vectors in the local space.
    std::array<XrExtent2Di,2> recommended_eye_sizes{},swapchain_eye_sizes{};
    std::array<EyeSize,2> source_eye_sizes{}; // v977: actual game eye size this Present (may exceed the XR eye under internal SSAA)
    UINT64 eye_source_pending{}; // v1021: frames held because the game's output had not reached the just-resized eye yet
    UINT64 eye_resolves{}; // v1021: eyes downsampled from a game render above the XR eye (internal SSAA / canted density)
    bool views_valid() const noexcept {return views_valid_;} // optical sizes need this Present's located views
    std::array<DXGI_FORMAT,2> eye_texture_formats{};
    EyeSize recommended_output_size() const {
        std::array<EyeSize,2> sizes{};
        for(UINT eye=0;eye<2;++eye) sizes[eye]={static_cast<UINT>(recommended_eye_sizes[eye].width),
            static_cast<UINT>(recommended_eye_sizes[eye].height)};
        return recommended_output(sizes,maximum_eye_sizes_,system_maximum_);
    }
    bool canted_views=false; // eyes render canted symmetric frusta (canted_fov.hpp); sizes follow them
    EyeSize optical_output_size() const {
        if(!views_valid_) throw std::runtime_error("Optical output needs located XR views");
        std::array<EyeSize,2> sizes{};std::array<EyeFov,2> fovs{};
        for(UINT eye=0;eye<2;++eye) {
            sizes[eye]={static_cast<UINT>(recommended_eye_sizes[eye].width),static_cast<UINT>(recommended_eye_sizes[eye].height)};
            const auto& f=views_[eye].fov;fovs[eye]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
        }
        if(canted_views) {
            std::array<EyeSize,2> canted{};
            for(UINT eye=0;eye<2;++eye) {
                const auto& f=fovs[eye];const auto c=canted_fov(f.left,f.right,f.up,f.down);
                if(!c) throw std::runtime_error("Invalid optical frustum for a canted eye");
                const auto s=canted_size(sizes[eye].width,sizes[eye].height,f.left,f.right,f.up,f.down,*c);canted[eye]={s[0],s[1]};
            }
            return recommended_output(canted,maximum_eye_sizes_,system_maximum_);
        }
        return symmetric_output(sizes,maximum_eye_sizes_,system_maximum_,fovs);
    }
    ScaledEyeSize scaled_output_size(double multiplier,bool optical) const {
        auto maximum=system_maximum_;
        for(const auto& eye:maximum_eye_sizes_) {
            maximum.width=std::min(maximum.width,eye.width);maximum.height=std::min(maximum.height,eye.height);
        }
        return scaled_eye_size(optical?optical_output_size():recommended_output_size(),multiplier,maximum);
    }
    // v976: the game's internal render size. The runtime's maxImageRect often clamps the XR eye to 125% of the
    // recommended size (Virtual Desktop High: 3120x3360), so a 150% request must render larger than the XR image and
    // be resolved down before submission. This size is bounded only by D3D11, not by maxImageRect.
    EyeSize game_output_size(double multiplier) const {
        const EyeSize maximum{D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION};
        return scaled_eye_size(recommended_output_size(),multiplier,maximum).size;
    }
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    bool exit_requested=false;
    GameXr(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,const wchar_t* loader,bool controllers=false,bool tracked_gameplay=false,bool edge_antialiasing=false,bool settings=false,double multiplier=1,
        TurnControl::Config turning={},std::filesystem::path preferences_file={},bool native_pointer=false,PFN_xrGetInstanceProcAddr simulated_dispatch=nullptr,
        MirrorMode mirror=MirrorMode::right_eye,double sharpness=.5,int temporal_aa=2,int hud_percent=65,bool controls_hint=true,AvatarPlayerMeasurements player_measurements={},bool aa_chosen=false,bool crosshair=true,bool scripted_recenter=true,CutsceneMode cutscene_mode=CutsceneMode::vr_comfort,int upscale_percent=0)
        :graphics_state_(device),settings_enabled_(settings),native_pointer_enabled_(native_pointer),controller_enabled_(controllers),tracked_gameplay_required_(tracked_gameplay) {
        ContextState::Scope graphics_scope(graphics_state_);
        try {
            if(edge_antialiasing) antialiasing_=std::make_unique<EyeAntialiasing>(device);
            antialiasing_enabled_=edge_antialiasing;settings_.initialize(multiplier,edge_antialiasing,turning,mirror,sharpness,temporal_aa,hud_percent,controls_hint,player_measurements,aa_chosen,crosshair,scripted_recenter,cutscene_mode,upscale_percent);
            preferences_file_=std::move(preferences_file);
            if(!preferences_file_.empty()) {
                palm_file_=preferences_file_.parent_path()/L"vr-palm-offsets.txt";
                PalmSaver::process().set_file(palm_file_);
                try {palm_.store().load(palm_file_);} catch(...) {palm_error_.raise(PalmSaver::process().successes());} // corrupt, oversized or locked: defaults, game keeps running
            }
            get_=simulated_dispatch;
            if(!get_) {
                loader_=LoadLibraryExW(loader,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if(!loader_) throw std::runtime_error("Cannot load installed OpenXR loader");
                auto symbol=GetProcAddress(loader_,"xrGetInstanceProcAddr");
                if(!symbol) throw std::runtime_error("Missing XR loader export");
                static_assert(sizeof(symbol)==sizeof(get_));std::memcpy(&get_,&symbol,sizeof(get_));
            }
            // Cylinder layers (curved HUD screen) when the runtime offers them; flat quads otherwise.
            try {
                auto enumerate=api<PFN_xrEnumerateInstanceExtensionProperties>("xrEnumerateInstanceExtensionProperties",XR_NULL_HANDLE);
                uint32_t count{};
                if(XR_SUCCEEDED(enumerate(nullptr,0,&count,nullptr)) && count) {
                    std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});
                    if(XR_SUCCEEDED(enumerate(nullptr,count,&count,props.data())))
                        for(const auto& prop:props) {
                            xr_extensions_+=(xr_extensions_.empty()?"":" ")+std::string(prop.extensionName)+"/"+std::to_string(prop.extensionVersion);
                            visibility_mask_supported_=visibility_mask_supported_ || !std::strcmp(prop.extensionName,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
                            cylinder_supported_=cylinder_supported_ || !std::strcmp(prop.extensionName,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
                            color_scale_supported_=color_scale_supported_ || !std::strcmp(prop.extensionName,XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
                        }
                }
            } catch(...) {cylinder_supported_=false;color_scale_supported_=false;visibility_mask_supported_=false;}
            if(!vignette_enabled || !vignette_color_scale_requested) color_scale_supported_=false; // nothing would use it; the chained struct is opt-in (build/vignette-color-scale)
            std::vector<const char*> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
            if(cylinder_supported_) extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
            if(color_scale_supported_) extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
            if(visibility_mask_supported_) extensions.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
            XrInstanceCreateInfo create{};create.type=XR_TYPE_INSTANCE_CREATE_INFO;
            strcpy_s(create.applicationInfo.applicationName,"Dishonored 2 game-image preview");
            create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
            create.enabledExtensionCount=static_cast<uint32_t>(extensions.size());create.enabledExtensionNames=extensions.data();
            checked(api<PFN_xrCreateInstance>("xrCreateInstance",XR_NULL_HANDLE)(&create,&instance_),"Create XR instance");
#define XR_LOAD(name) name=api<PFN_##name>(#name,instance_);
            XR_LOAD(xrDestroyInstance) XR_LOAD(xrDestroySession) XR_LOAD(xrDestroySpace)
            XR_LOAD(xrDestroySwapchain) XR_LOAD(xrPollEvent) XR_LOAD(xrBeginSession)
            XR_LOAD(xrEndSession) XR_LOAD(xrWaitFrame) XR_LOAD(xrBeginFrame)
            XR_LOAD(xrEndFrame) XR_LOAD(xrLocateViews) XR_LOAD(xrAcquireSwapchainImage)
            XR_LOAD(xrWaitSwapchainImage) XR_LOAD(xrReleaseSwapchainImage)
            XR_LOAD(xrStringToPath) XR_LOAD(xrCreateActionSet) XR_LOAD(xrDestroyActionSet)
            XR_LOAD(xrCreateAction) XR_LOAD(xrSuggestInteractionProfileBindings) XR_LOAD(xrAttachSessionActionSets)
            XR_LOAD(xrCreateActionSpace) XR_LOAD(xrSyncActions) XR_LOAD(xrGetActionStatePose)
            XR_LOAD(xrGetActionStateFloat) XR_LOAD(xrGetActionStateVector2f) XR_LOAD(xrGetActionStateBoolean) XR_LOAD(xrLocateSpace) XR_LOAD(xrApplyHapticFeedback)
#undef XR_LOAD
            {   // optional (core OpenXR 1.0, but a missing one must not fail the XR start): without them the suggested profile is the key
                PFN_xrVoidFunction address{};
                if(XR_SUCCEEDED(get_(instance_,"xrGetCurrentInteractionProfile",&address)) && address) xrGetCurrentInteractionProfile=reinterpret_cast<PFN_xrGetCurrentInteractionProfile>(address);
                address=nullptr;
                if(XR_SUCCEEDED(get_(instance_,"xrPathToString",&address)) && address) xrPathToString=reinterpret_cast<PFN_xrPathToString>(address);
                palm_.profile_query_available=xrGetCurrentInteractionProfile && xrPathToString;
                address=nullptr;
                if(visibility_mask_supported_ && XR_SUCCEEDED(get_(instance_,"xrGetVisibilityMaskKHR",&address)) && address) xrGetVisibilityMaskKHR_=reinterpret_cast<PFN_xrGetVisibilityMaskKHR>(address);
            }
            XrSystemGetInfo query{};query.type=XR_TYPE_SYSTEM_GET_INFO;query.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
            XrSystemId system{};
            checked(api<PFN_xrGetSystem>("xrGetSystem",instance_)(instance_,&query,&system),"Find headset");
            std::array<XrViewConfigurationView,2> eye_configuration{};
            for(auto& eye:eye_configuration) eye.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
            uint32_t count{};
            checked(api<PFN_xrEnumerateViewConfigurationViews>("xrEnumerateViewConfigurationViews",instance_)(instance_,system,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,eye_configuration.data()),"Read recommended game eye sizes");
            if(count!=2) throw std::runtime_error("Expected two recommended XR eye views");
            for(UINT eye=0;eye<2;++eye) recommended_eye_sizes[eye]={static_cast<int32_t>(eye_configuration[eye].recommendedImageRectWidth),
                static_cast<int32_t>(eye_configuration[eye].recommendedImageRectHeight)};
            for(UINT eye=0;eye<2;++eye) maximum_eye_sizes_[eye]={eye_configuration[eye].maxImageRectWidth,eye_configuration[eye].maxImageRectHeight};
            XrSystemProperties system_properties{};system_properties.type=XR_TYPE_SYSTEM_PROPERTIES;
            checked(api<PFN_xrGetSystemProperties>("xrGetSystemProperties",instance_)(instance_,system,&system_properties),"Read XR texture limits");
            system_maximum_={system_properties.graphicsProperties.maxSwapchainImageWidth,system_properties.graphicsProperties.maxSwapchainImageHeight};
            XrGraphicsRequirementsD3D11KHR requirements{};requirements.type=XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
            checked(api<PFN_xrGetD3D11GraphicsRequirementsKHR>("xrGetD3D11GraphicsRequirementsKHR",instance_)(instance_,system,&requirements),"XR device requirements");
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            DXGI_ADAPTER_DESC desc{};
            if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc)) ||
               desc.AdapterLuid.HighPart!=requirements.adapterLuid.HighPart || desc.AdapterLuid.LowPart!=requirements.adapterLuid.LowPart ||
               device->GetFeatureLevel()<requirements.minFeatureLevel) throw std::runtime_error("Game device differs from XR device requirements");
            XrGraphicsBindingD3D11KHR binding{};binding.type=XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;binding.device=device;
            XrSessionCreateInfo session{};session.type=XR_TYPE_SESSION_CREATE_INFO;session.next=&binding;session.systemId=system;
            checked(api<PFN_xrCreateSession>("xrCreateSession",instance_)(instance_,&session,&session_),"Create game XR session");
            XrReferenceSpaceCreateInfo space{};space.type=XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
            space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;space.poseInReferenceSpace.orientation.w=1;
            checked(api<PFN_xrCreateReferenceSpace>("xrCreateReferenceSpace",instance_)(session_,&space,&local_),"Create local XR space");
            PFN_xrVoidFunction enumerate_raw{};
            if(get_(instance_,"xrEnumerateReferenceSpaces",&enumerate_raw)==XR_SUCCESS && enumerate_raw) {
                PFN_xrEnumerateReferenceSpaces enumerate{};std::memcpy(&enumerate,&enumerate_raw,sizeof enumerate);
                uint32_t spaces_count{};
                if(enumerate(session_,0,&spaces_count,nullptr)==XR_SUCCESS && spaces_count && spaces_count<=16) {
                    std::array<XrReferenceSpaceType,16> spaces{};
                    if(enumerate(session_,spaces_count,&spaces_count,spaces.data())==XR_SUCCESS && spaces_count<=spaces.size() &&
                       std::find(spaces.begin(),spaces.begin()+spaces_count,XR_REFERENCE_SPACE_TYPE_STAGE)!=spaces.begin()+spaces_count) {
                        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
                        (void)api<PFN_xrCreateReferenceSpace>("xrCreateReferenceSpace",instance_)(session_,&space,&floor_space_);
                    }
                }
            }
            if(controller_enabled_) create_actions();
            auto enumerate_formats=api<PFN_xrEnumerateSwapchainFormats>("xrEnumerateSwapchainFormats",instance_);
            checked(enumerate_formats(session_,0,&count,nullptr),"Get XR formats");
            std::vector<int64_t> formats(count);checked(enumerate_formats(session_,count,&count,formats.data()),"Read XR formats");
            if(std::find(formats.begin(),formats.end(),static_cast<int64_t>(format))==formats.end()) throw std::runtime_error("Game colour format is unsupported by XR");
            resize(width,height,format);
            device->GetImmediateContext(&context_);frame_fence_.Reset();frame_fence_pending_=false;
            prepare_overlays();
        } catch(...) {close();throw;}
    }
    GameXr(const GameXr&)=delete;
    GameXr& operator=(const GameXr&)=delete;
    ~GameXr() {close();}
    // Called only on the game's Present thread, between XR frames. Replace both
    // eye chains together; a failed allocation leaves the old pair intact.
    bool resize(UINT width,UINT height,DXGI_FORMAT format) {
        if(width==width_ && height==height_ && format==format_) return false;
        const bool format_changed=format!=format_;
        if(frame_open_) throw std::runtime_error("Cannot resize an open XR frame");
        if(!width || !height || width>system_maximum_.width || height>system_maximum_.height ||
           width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Game eye size exceeds XR texture limits");
        for(const auto& eye:maximum_eye_sizes_) if(width>eye.width || height>eye.height)
            throw std::runtime_error("Game output exceeds an XR eye's limits");
        ContextState::Scope graphics_scope(graphics_state_);
        auto create_chain=api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_);
        auto enumerate_images=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);
        std::array<XrSwapchain,2> next{};
        std::array<std::vector<XrSwapchainImageD3D11KHR>,2> images;
        try {
            for(UINT eye=0;eye<2;++eye) {
                XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
                info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
                info.format=format;info.width=width;info.height=height;info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
                checked(create_chain(session_,&info,&next[eye]),"Create game eye swapchain");
                log_chain_event_(true,"Create game eye swapchain",width,height,next[eye]);
                uint32_t count{};checked(enumerate_images(next[eye],0,&count,nullptr),"Get eye images");
                images[eye].resize(count);for(auto& image:images[eye]) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
                checked(enumerate_images(next[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),"Read eye images");
                if(images[eye].empty()) throw std::runtime_error("XR returned no swapchain images");
                for(const auto& image:images[eye]) {
                    D3D11_TEXTURE2D_DESC actual{};image.texture->GetDesc(&actual);
                    if(!eye_texture_matches(actual,width,height,format))
                        throw std::runtime_error("XR eye texture differs: actual "+std::to_string(actual.Width)+"x"+
                            std::to_string(actual.Height)+" format="+std::to_string(actual.Format)+"; requested "+
                            std::to_string(width)+"x"+std::to_string(height)+" format="+std::to_string(format));
                }
            }
        } catch(...) {for(auto chain:next) if(chain) xrDestroySwapchain(chain);throw;}
        try {
            drain_gpu_for_surface();
            for(auto& chain:chains_) if(chain) {
                log_chain_event_(false,"Destroy replaced eye swapchain",width_,height_,chain);
                chain_sizes_.erase(chain);checked(xrDestroySwapchain(chain),"Destroy replaced eye swapchain");chain=XR_NULL_HANDLE;
            }
        } catch(...) {for(auto chain:next) if(chain) xrDestroySwapchain(chain);throw;}
        chains_=next;images_=std::move(images);width_=width;height_=height;format_=format;rendered_mask_=0;menu_band_width_=0; // prepare_overlays rebuilds the band chain
        resolve_.reset();
        // The alpha blit requires the watch target and face to share a typed format; a format change must retire both
        // wrist chains rather than draw through a stale-format RTV.
        if(format_changed) {
            for(unsigned side=0;side<2;++side) if(wrist_chains_[side]) wrist_recreate_[side]=true;
            if(wrist_chains_[0]) wrist_want_[0]={wrist_watch_canvas,wrist_watch_canvas};
        }
        wrist_watch_blitter_.clear(); // drop the cached cluster SRV; dxgi_probe republishes in the new format
        for(auto chain:chains_) chain_sizes_[chain]={width,height};
        for(UINT eye=0;eye<2;++eye) {
            D3D11_TEXTURE2D_DESC actual{};images_[eye][0].texture->GetDesc(&actual);eye_texture_formats[eye]=actual.Format;
        }
        for(auto& size:swapchain_eye_sizes) size={static_cast<int32_t>(width),static_cast<int32_t>(height)};
        last_good_projection_.reset(); // it references the replaced eye chains
        if(context_) ensure_menu_band_chain(); // the band chain is rebuilt at its final size now, between frames, not on first use by a menu or authored frame
        return true;
    }
    const std::array<XrView,2>* tracking_views() const {
        return running_ && views_valid_ && continuity_.usable()?&views_:nullptr;
    }
    // The eye views of this frame as the layers will carry them. Alternate mode: the eye rendered this frame is the recorded
    // render view, the other eye is its kept record (its own pose, never this frame's). Paired mode: rendered_views_ (unchanged).
    std::array<EyeRecord<XrView>,2> alt_effective_() const {
        std::array<EyeRecord<XrView>,2> out=alt_records_;
        for(int eye=0;eye<2;++eye) if(alternate_fresh_eye<0 || eye==alternate_fresh_eye) {
            const auto e=static_cast<std::size_t>(eye);
            out[e]={rendered_views_[e],alternate_frame,pose_epoch(),true,rendered_display_times_[e],rendered_locate_cpu_times_[e]};
        } else if(stale_warped_(static_cast<UINT>(eye))) out[static_cast<std::size_t>(eye)].view=stale_target_[static_cast<std::size_t>(eye)]; // v792: warped to it
        return out;
    }
    // True when both eyes can be submitted with views that match their images. Paired mode: the old rendered_mask_==3 rule.
    bool tracked_pair_() const {
        if(!alternate_enabled) return rendered_mask_==3;
        return alternate_pair_ready(rendered_mask_,alternate_fresh_eye,build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).ok);
    }
    XrView tracked_view_(UINT eye) const {return alternate_enabled?build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).views[eye]:rendered_views_[eye];}
    // The eye that was not rendered this frame needs its kept image, at the swapchain's size.
    bool alt_kept_ok_(bool nonfree) const {
        if(!alternate_enabled || alternate_fresh_eye<0) return true;
        if(nonfree) return false; // authored scene / settings panel: a single fresh eye is never paired with a kept one (the probe drops these frames already)
        const auto& kept=alt_kept_[static_cast<std::size_t>(1-alternate_fresh_eye)];
        if(!kept) return false;
        D3D11_TEXTURE2D_DESC desc{};kept->GetDesc(&desc);
        return eye_texture_matches(desc,width_,height_,format_);
    }
    // Copies the eye image that was just rendered (after anti-aliasing / sharpening, before overlays) and its view for later frames.
    void keep_alternate_eye_(UINT eye,ID3D11Texture2D* processed) {
        auto& record=alt_records_[eye];
        record={};alt_processing_[eye]={};kept_camera_valid_[eye]=false;
        D3D11_TEXTURE2D_DESC desc{};processed->GetDesc(&desc);
        if(!(rendered_mask_&(1u<<eye))) return; // no view was recorded for this image: keep nothing
        if(!eye_texture_matches(desc,width_,height_,format_)) {alternate_error=true;return;}
        if(alt_kept_[eye]) {D3D11_TEXTURE2D_DESC have{};alt_kept_[eye]->GetDesc(&have);if(!eye_texture_matches(have,width_,height_,format_)) alt_kept_[eye].Reset();}
        if(!alt_kept_[eye]) {
            Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=0;desc.CPUAccessFlags=0;desc.MiscFlags=0;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&alt_kept_[eye]))) {alternate_error=true;return;}
        }
        context_->CopyResource(alt_kept_[eye].Get(),processed);
        alt_processing_[eye]=transfer_processing_[eye];
        record={rendered_views_[eye],alternate_frame,pose_epoch(),true,rendered_display_times_[eye],rendered_locate_cpu_times_[eye]};
        kept_cameras_[eye]=rendered_cameras_[eye];kept_camera_valid_[eye]=rendered_camera_frame_[eye]==alternate_frame; // v796
        kept_play_[eye]=rendered_play_[eye];kept_play_[eye].valid=kept_play_[eye].valid && kept_camera_valid_[eye]; // v1184
    }
    // perf-mono-view diagnostic: the one engine view sits at the midpoint of the two eye poses, faces their mean orientation and
    // covers both eyes' FOV (the union), so the one image is valid for both projection layers (flat, no stereo depth).
    static XrView mono_centre_view(const std::array<XrView,2>& v) {
        const auto pose=[](const XrPosef& p){return TrackedPose{{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}};};
        const auto centre=eye_centre_pose(pose(v[0].pose),pose(v[1].pose));
        XrView out=v[0];
        out.pose.position={static_cast<float>(centre.position.x),static_cast<float>(centre.position.y),static_cast<float>(centre.position.z)};
        out.pose.orientation={static_cast<float>(centre.orientation.x),static_cast<float>(centre.orientation.y),static_cast<float>(centre.orientation.z),static_cast<float>(centre.orientation.w)};
        out.fov={std::min(v[0].fov.angleLeft,v[1].fov.angleLeft),std::max(v[0].fov.angleRight,v[1].fov.angleRight),
                 std::max(v[0].fov.angleUp,v[1].fov.angleUp),std::min(v[0].fov.angleDown,v[1].fov.angleDown)};
        return out;
    }
    // alternate-eye: the view an eye image is rendered with (this frame's located view with the rendered FOV and, for a canted eye, the
    // rendered orientation). The probe records it in its PendingViews for the Present that shows the image (one engine frame later than
    // the next Present) and promotes it there with record_rendered_view, so the layer carries exactly the pose and FOV of its image.
    XrView rendered_view(UINT eye,XrFovf rendered_fov,const XrQuaternionf* rendered_orientation=nullptr) const {
        XrView view=views_[eye];view.fov=rendered_fov;
        if(rendered_orientation) view.pose.orientation=*rendered_orientation;
        return view;
    }
    std::int64_t locate_cpu_time() const noexcept {return locate_cpu_time_;}
    bool last_projection_usable() const {return last_good_projection_.usable(chains_,GetTickCount64(),pose_epoch());} // v1244: an empty frame would re-send it, not black
    bool rendered_pair_ready() const {return tracked_pair_();} // both eyes have a recorded rendered view for this Present (an authored scene requires it)
    XrView recorded_rendered_view(UINT eye) const {return rendered_views_[eye];} // diagnostic (alt-pose-trace): what the layer of `eye` will carry
    void record_rendered_view(UINT eye,const XrView& view,XrTime display_time,std::int64_t locate_cpu) {
        rendered_views_[eye]=view;rendered_mask_|=1u<<eye;
        rendered_display_times_[eye]=display_time;rendered_locate_cpu_times_[eye]=locate_cpu;
    }
    // rendered_position: mono only (both eyes record the same centre position; the orientation comes through rendered_orientation).
    void record_rendered_eye(UINT eye,XrFovf rendered_fov,const XrQuaternionf* rendered_orientation=nullptr,const XrVector3f* rendered_position=nullptr) {
        rendered_views_[eye]=views_[eye];rendered_views_[eye].fov=rendered_fov;rendered_mask_|=1u<<eye;
        if(rendered_orientation) rendered_views_[eye].pose.orientation=*rendered_orientation; // canted eye / mono centre
        if(rendered_position) rendered_views_[eye].pose.position=*rendered_position;
        rendered_display_times_[eye]=locate_display_time_;
        rendered_locate_cpu_times_[eye]=locate_cpu_time_;
    }
    bool ready_to_close() {return !session_ || gpu_retirement_ready(close_retirement_);}
    void close() noexcept {
        PalmSaver::process().flush_for(200); // a fit confirmed just before stop reaches the disk (bounded wait)
        ContextState::Scope graphics_scope(graphics_state_);
        // Production polls ready_to_close on the owner Present thread first.
        // Direct fixture/partial-construction destruction gets a bounded drain.
        // An unretired runtime and loader must remain alive until process exit.
        try {
            const auto deadline=GetTickCount64()+1000;
            while(!ready_to_close()) {if(GetTickCount64()>=deadline)return;Sleep(1);}
        } catch(...) {return;}
        if(frame_open_ && xrEndFrame) {
            XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=frame_.predictedDisplayTime;
            end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session_,&end);frame_open_=false;
        }
        context_.Reset();invalidate_overlays();chain_generations_.clear(); // a restarted session re-renders every overlay
        settings_texture_.Reset();settings_compositor_.reset();
        world_ui_source_.Reset();world_ui_images_.clear();
        hud_window_source_.Reset();hud_window_images_.clear();
        if(hud_window_chain_ && xrDestroySwapchain) xrDestroySwapchain(hud_window_chain_);
        hud_window_chain_=XR_NULL_HANDLE;hud_window_width_=hud_window_height_=0;hud_window_.reset();
        if(world_ui_chain_ && xrDestroySwapchain) xrDestroySwapchain(world_ui_chain_);
        world_ui_chain_=XR_NULL_HANDLE;world_ui_width_=world_ui_height_=0;world_ui_kind_=0;world_ui_pose_valid_=false;
        if(menu_band_chain_ && xrDestroySwapchain) xrDestroySwapchain(menu_band_chain_);
        menu_band_chain_=XR_NULL_HANDLE;menu_band_images_.clear();menu_band_width_=menu_band_height_=0;chain_sizes_.clear();
        pointer_images_.clear();
        if(pointer_chain_ && xrDestroySwapchain) {xrDestroySwapchain(pointer_chain_);pointer_chain_=XR_NULL_HANDLE;}
        hint_images_.clear();hint_revisions_.clear();
        for(unsigned side=0;side<2;++side) {
            wrist_targets_[side].clear();
            wrist_images_[side].clear();wrist_pixels_[side].clear();wrist_image_ticks_[side]=0;
            if(wrist_chains_[side] && xrDestroySwapchain) xrDestroySwapchain(wrist_chains_[side]);
            wrist_chains_[side]=XR_NULL_HANDLE;
            wrist_acquired_[side]=false;native_wrist_sizes_[side]={};wrist_retry_ticks_[side]=0;
            wrist_retirement_[side].Reset();wrist_recreate_[side]=false;
        }
        wrist_watch_cluster_={};wrist_watch_blitter_.clear();
        wrist_watch_state_.reset();wrist_watch_pose_.reset();wrist_watch_tick_=0;wrist_watch_alpha_=0;wrist_watch_submitted_=false;
        if(hint_chain_ && xrDestroySwapchain) {xrDestroySwapchain(hint_chain_);hint_chain_=XR_NULL_HANDLE;}
        settings_images_.clear();
        if(settings_chain_ && xrDestroySwapchain) {xrDestroySwapchain(settings_chain_);settings_chain_=XR_NULL_HANDLE;}
        dot_images_.clear();dot_filled_.clear();wheel_cursor_.reset();
        blank_images_.clear();blank_filled_.clear();last_good_projection_.reset();
        if(blank_chain_ && xrDestroySwapchain) {xrDestroySwapchain(blank_chain_);blank_chain_=XR_NULL_HANDLE;}
        if(dot_chain_ && xrDestroySwapchain) {xrDestroySwapchain(dot_chain_);dot_chain_=XR_NULL_HANDLE;}
        vignette_images_.clear();vignette_filled_.clear();
        if(vignette_chain_ && xrDestroySwapchain) {xrDestroySwapchain(vignette_chain_);vignette_chain_=XR_NULL_HANDLE;}
        if(view_space_ && xrDestroySpace) {xrDestroySpace(view_space_);view_space_=XR_NULL_HANDLE;}
        for(auto& kind:hand_spaces_) for(auto& space:kind) if(space && xrDestroySpace) {xrDestroySpace(space);space=XR_NULL_HANDLE;}
        for(auto& chain:chains_) if(chain && xrDestroySwapchain) {xrDestroySwapchain(chain);chain=XR_NULL_HANDLE;}
        if(floor_space_ && xrDestroySpace) {xrDestroySpace(floor_space_);floor_space_=XR_NULL_HANDLE;}calibration_floor_y_.reset();grip_pose_tick_=0;grip_velocity_hands=0;
        if(local_ && xrDestroySpace) {xrDestroySpace(local_);local_=XR_NULL_HANDLE;}
        if(session_ && xrDestroySession) {xrDestroySession(session_);session_=XR_NULL_HANDLE;}
        if(action_set_ && xrDestroyActionSet) {xrDestroyActionSet(action_set_);action_set_=XR_NULL_HANDLE;}
        if(instance_ && xrDestroyInstance) {xrDestroyInstance(instance_);instance_=XR_NULL_HANDLE;}
        if(loader_) {FreeLibrary(loader_);loader_=nullptr;}
        close_retirement_.Reset();
    }
    // Tracked frames use the pose/FOV recorded by the native Setup hook. Menu or
    // untracked frames retain the native symmetric FOV presentation.
    // desktop: the game's backbuffer; in gameplay it receives the processed eyes per the monitor-view setting.
    // authored_scene_render (docs/native-authored-scene-presentation.md section 5): an immersive authored scene outside gameplay (an exact
    // Cinematic32 frame the probe certified). It renders like the gameplay scene (projection layers, a mandatory tracked eye pair) but only with
    // the authored camera; the native menu pointer is off and the raw trigger passes. Raw gameplay keeps the settings/calibration semantics.
    void present(ID3D11Texture2D* left,ID3D11Texture2D* right,float hfov,float vfov,bool gameplay=true,bool authored_camera=false,bool settings_available=false,
        ID3D11Texture2D* desktop=nullptr,std::array<UINT,2> desktop_view={},bool cinematic=false,bool authored_scene_render=false) {
        if(!alternate_enabled && (alt_kept_[0] || alt_kept_[1])) {alt_kept_[0].Reset();alt_kept_[1].Reset();alt_records_={};alt_processing_={};} // fallen back: release the kept copies
        authored_scene_render=authored_scene_render && authored_camera && !gameplay;
        const auto scene_hold_now=scene_hold;const auto hold_scene=scene_id;scene_hold=SceneHold::none; // one frame only
        const bool scene_render=gameplay || authored_scene_render;
        const bool world_scene=scene_render || world_ui_kind_!=0;
        last_world_ui_layer=false;
        // v840: one frame in flight. GPU-bound, the render thread ran up to three frames ahead and then blocked in the desktop Present
        // after the next frame's views were located (v832 headset: 12-15 ms on every third frame), so images showed stale poses and the
        // queue added latency. Waiting here for the previous submit's fence moves that wait before xrWaitFrame and the locate; it stays
        // outside timing.wait_ns, so the perf governor counts it as frame work.
        if(frame_fence_pending_) {
            frame_fence_pending_=false;
            const auto deadline=cpu_time_ns()+40'000'000;
            while(context_->GetData(frame_fence_.Get(),nullptr,0,0)==S_FALSE && cpu_time_ns()<deadline) SwitchToThread();
        }
        timing={};timing.start_ns=cpu_time_ns();++present_count_;
        if(tracked_pair_()) {
            timing.rendered_display_times=rendered_display_times_;
            timing.rendered_locate_cpu_times=rendered_locate_cpu_times_;
            if(alternate_enabled) { // the kept eye's times are those of the locate it was rendered from
                const auto records=alt_effective_();
                for(std::size_t eye=0;eye<2;++eye) {timing.rendered_display_times[eye]=records[eye].display_time;timing.rendered_locate_cpu_times[eye]=records[eye].locate_cpu_time;}
            }
        }
        ContextState::Scope graphics_scope(graphics_state_);
        last_should_render=false;last_views_valid=false;last_tracked_pair=false;
        native_menu_input={};
        last_pointer_layer=false;
        last_layer_submitted=false;last_menu_layer=false;last_authored_camera_layer=false;
        native_object_art_masks_={};native_wrist_art_shown_={};
        authored_camera=scene_render && authored_camera;
        if(!authored_camera) authored_panel_.reset();
        XrEventDataBuffer event{};event.type=XR_TYPE_EVENT_DATA_BUFFER;
        XrResult result{};
        while((result=xrPollEvent(instance_,&event))==XR_SUCCESS) {
            if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) exit_requested=true;
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                ++state_events;
                state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                if(state!=XR_SESSION_STATE_VISIBLE && state!=XR_SESSION_STATE_FOCUSED) {
                    continuity_.suspend();menu_panel_.reset();authored_panel_.reset();world_ui_pose_valid_=false;views_valid_=false;rendered_mask_=0;
                    settings_.suspend();publish_palm_preview();
                }
                if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) exit_requested=true;
                if(state==XR_SESSION_STATE_READY && !running_) {
                    XrSessionBeginInfo begin{};begin.type=XR_TYPE_SESSION_BEGIN_INFO;begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    checked(xrBeginSession(session_,&begin),"Begin game XR session");running_=true;
                } else if(state==XR_SESSION_STATE_STOPPING) {
                    if(running_) checked(xrEndSession(session_),"End game XR session");running_=false;
                }
            }
            if(event.type==XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR) {
                const auto* changed=reinterpret_cast<const XrEventDataVisibilityMaskChangedKHR*>(&event);
                if(changed->session==session_ && changed->viewIndex<2) visibility_mask_logged_[changed->viewIndex]=false;
            }
            if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto* change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
                if(change->session==session_ && (change->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL || (floor_space_ && change->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_STAGE))) {
                    ++reference_events;continuity_.reference_change(change->changeTime);
                }
            }
            event={};event.type=XR_TYPE_EVENT_DATA_BUFFER;
        }
        if(result!=XR_EVENT_UNAVAILABLE) checked(result,"Poll game XR events");
        if(!running_ || exit_requested) {
            controller_input={};views_valid_=false;rendered_mask_=0;
            settings_.calibration_status(avatar_calibration_.synchronize(reference_epoch(),false));
            return;
        }
        prepare_overlays();
        XrFrameWaitInfo wait{};wait.type=XR_TYPE_FRAME_WAIT_INFO;frame_={};frame_.type=XR_TYPE_FRAME_STATE;
        auto stage_start=cpu_time_ns();
        if(const auto wake=pacer_.wake(stage_start)) { // v841: sleep to the locked cadence (the bulk on a high-resolution timer, the rest spun)
            static thread_local const HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
            for(auto remaining=wake-cpu_time_ns();remaining>0;remaining=wake-cpu_time_ns()) {
                LARGE_INTEGER due{};due.QuadPart=-(remaining-1'000'000)/100;
                if(remaining>1'500'000 && timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)) WaitForSingleObject(timer,50); else SwitchToThread();
            }
        }
        checked(xrWaitFrame(session_,&wait,&frame_),"Wait game XR frame");
        timing.waited=true;timing.wait_ns=cpu_time_ns()-stage_start; // includes the pacing sleep: the governor counts both as idle
        pacer_.miss_lock=pacer_miss_lock;
        pacer_.update(cpu_time_ns(),timing.wait_ns,frame_.predictedDisplayTime,frame_.predictedDisplayPeriod,steady_pacing && !alternate_enabled);
        timing.display_time=frame_.predictedDisplayTime;timing.display_period=frame_.predictedDisplayPeriod;
        XrFrameBeginInfo begin{};begin.type=XR_TYPE_FRAME_BEGIN_INFO;
        stage_start=cpu_time_ns();
        checked(xrBeginFrame(session_,&begin),"Begin game XR frame");frame_open_=true;
        timing.begin_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        XrViewLocateInfo locate{};locate.type=XR_TYPE_VIEW_LOCATE_INFO;locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        // Native eyes are prepared after this Present, rendered in the next interval and submitted at the Present after that (the engine
        // pipeline is one frame deep, alternate_display_frame): v794 locates views and hands for that display, two of the runtime's periods
        // ahead. locate_periods 1 (knob build/legacy-locate-horizon) is the pre-v794 horizon, one period short of the image.
        // v890: unlocked, the median measured step (frame_pacer.hpp DisplayHorizon; knob build/legacy-display-horizon = the period rule).
        display_horizon_.add(frame_.predictedDisplayTime);
        const auto period_horizon=pacer_.period(frame_.predictedDisplayPeriod)*locate_periods; // v841: the locked cadence, not the runtime's flip-flopping period
        locate_display_time_=frame_.predictedDisplayTime+(median_horizon && !(pacer_.active && pacer_.k>1)?
            display_horizon_.ahead(static_cast<unsigned>(locate_periods),period_horizon):period_horizon);
        locate.displayTime=locate_display_time_;locate.space=local_;
        XrViewState view_state{};view_state.type=XR_TYPE_VIEW_STATE;
        for(auto& view:views_) {view={};view.type=XR_TYPE_VIEW;}
        uint32_t count{};checked(xrLocateViews(session_,&locate,&view_state,2,&count,views_.data()),"Locate game XR views");
        located_view_flags_=view_state.viewStateFlags;
        locate_cpu_time_=cpu_time_ns();
        constexpr XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        views_valid_=count==2 && (view_state.viewStateFlags&valid)==valid;
        last_should_render=frame_.shouldRender!=XR_FALSE;last_views_valid=views_valid_;
        log_visibility_mask_();
        update_visibility_stamp_();
        update_fovea_();
        if(continuity_.advance(state==XR_SESSION_STATE_VISIBLE || state==XR_SESSION_STATE_FOCUSED,
            last_should_render,views_valid_,frame_.predictedDisplayTime)) {
            menu_panel_.reset();authored_panel_.reset();world_ui_pose_valid_=false;rendered_mask_=0;
        }
        last_tracked_pair=tracked_pair_();
        sample_input();
        // User's illustrated guide owns the right stick before settings or native menu navigation can see it.
        guide_temple_input=controller_input;
        const auto guide_nav=guide_navigation_.update(guide_open(),controller_input.turn_x,controller_input.turn_y,controller_input.right_click,
            controller_input.active && state==XR_SESSION_STATE_FOCUSED);
        guide_page_delta(guide_nav.delta);
        if(guide_nav.consume) {controller_input.turn_x=controller_input.turn_y=0;controller_input.right_click=false;}
        // Preserve squeeze actions before the paused overlay consumes controls.
        // Recognize raw Y before settings consumes input. Only the exact native pause state permits the paused shortcut;
        // wheel/title/loading are not Pause. Legacy keeps the previous gameplay settings panel and paused short-Y shortcut.
        const bool panel_drawable=SettingsMenuLayout::gameplay_drawable(gameplay,authored_camera,continuity_.usable() && left && right,
            !tracked_gameplay_required_ || tracked_pair_(),views_valid_,settings_chain_!=XR_NULL_HANDLE);
        const bool hold_ready=y_hold_enabled && state==XR_SESSION_STATE_FOCUSED && controller_input.active;
        const bool hold_gameplay=gameplay && !authored_camera && continuity_.usable() && views_valid_ &&
            (!legacy_hold_y_settings || (settings_enabled_ && panel_drawable));
        const auto hold_screen=!hold_ready?YHoldMenu::Screen::unavailable:
            hold_gameplay?YHoldMenu::Screen::gameplay:
            !gameplay && settings_available && settings_enabled_?YHoldMenu::Screen::pause:YHoldMenu::Screen::unavailable;
        const auto hold=y_hold_menu_.update(controller_input.y,y_hold_blocked.load(),hold_screen,settings_.open(),legacy_hold_y_settings,
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
            [&]() {const auto owned=y_hold_owned_now.load();return controller_input.menu_modifier || (owned && owned());});
        if(hold.blocked_press) ++y_hold_blocked_presses;
        if(hold.pause || hold.settings) {++y_hold_fires;y_hold_last_ms=static_cast<UINT64>(hold.held_s*1000);}
        controller_input.pause_request=hold.pause;
        // An opening settings update must see the held Y to seed its edge state. A native Pause event suppresses it immediately.
        if(hold.suppress_y && !hold.settings) {controller_input.y=false;controller_input.suppress_y=true;}
        const bool raw_both_grips_held=controller_input.left_grip>=.8f && controller_input.right_grip>=.8f;
        // In menus a left-grip squeeze re-places the menu where the user is looking.
        const bool menu_grip=controller_input.left_grip>=.6f;
        if(menu_grip && !menu_grip_was_ && (!scene_render || settings_.open()) && controller_input.right_grip<.5f) {
            menu_panel_.summon();
        }
        menu_grip_was_=menu_grip;
        const float raw_right_trigger=controller_input.right_trigger;
        const bool raw_input_active=controller_input.active;
        bool settings_consumed=false;
        std::optional<PanelPointer> pointer;
        // The game's own options page shows these rows (also without saved settings: then changes last for this session).
        {const auto now_ms=GetTickCount64();if(now_ms>=native_rows_tick_) {
            native_rows_tick_=now_ms+250;
            // While the page is open it also gets the rule for what its rows become after a change (a copy of the settings, read by the drain thread).
            NativeOptionsPage::publish(native_settings::rows(settings_),NativeOptionsPage::showing.load()?native_settings::resolver(settings_):NativeOptionsPage::Resolver{});}}
        if(settings_enabled_) {
            // v1073 (user: "delete the old VR menu; the user must not see it when holding any buttons; the only way to VR settings is Pause >
            // VR Settings"): nothing opens the drawn panel any more - not the menu item's fallback request, not a Y hold, not a Y press.
            settings_requested.store(false);
            if(settings_.gameplay_panel_open() && !panel_drawable && !unpaired_flat_frame) settings_.suspend(); // never consume input while the panel is not drawn (v1248: a flat-fallback frame keeps it)
            settings_in_world_=gameplay && settings_.open();
            if(settings_.open()) {
                constexpr XrSpaceLocationFlags valid_pose=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
                const auto& aim=aim_locations[1];
                if((aim.locationFlags&valid_pose)==valid_pose) {
                    const auto& p=aim.pose;
                    const auto view=menu_view();
                    if(const auto hit=panel_pointer({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},native_menu_pose(),view.width,view.height)) pointer=SettingsMenuLayout::pointer(*hit);
                }
            }
            const auto current_reference=reference_epoch();
            if(avatar_prompt_reference_epoch_ && current_reference!=avatar_prompt_reference_epoch_) {
                avatar_calibration_.synchronize(current_reference,false);
                settings_.calibration_status(avatar_calibration_.state());settings_.calibration_identity_changed();
            }
            avatar_prompt_reference_epoch_=current_reference;
            {   // the fit starts from what the game uses now (palm_offsets() is ignored while the page is open)
                const auto now=std::chrono::steady_clock::now();
                palm_fit_dt_=settings_update_time_.time_since_epoch().count()?std::clamp(std::chrono::duration<double>(now-settings_update_time_).count(),0.,.1):0.;
                settings_update_time_=now;
                settings_.palm_offsets(palm_offset_stored(0),palm_offset_stored(1));
            }
            auto settings_input=controller_input;
            // A paused tap of Y must not bypass the new hold, but Y still closes an open panel and serves calibration pages.
            if(!settings_.open()) settings_input.y=false;   // v1073: Y never opens the drawn panel
            auto changes=settings_.update(settings_input,gameplay || unpaired_flat_frame,state==XR_SESSION_STATE_FOCUSED,pointer,settings_available,palm_fit_dt_);
            // v1073: the drawn panel may show only the avatar calibration page started from the game's own VR Settings page (its Calibrate row).
            // Anything else - the old settings page, the automatic recalibration prompt in a menu - is closed before it is ever drawn.
            if(settings_.open() && !(settings_.calibration_page() && settings_.native_calibration())) {settings_.suspend();changes.opened=false;++old_panel_blocked;}
            if(changes.palm_offset) confirm_palm_offset(changes.palm_offset->side,changes.palm_offset->value);
            // The game's own options page ("VR Settings"): its values and actions take the panel's path (apply, save, calibrate).
            {std::vector<NativeOptionsPage::Kind> page_kinds;
             if(const auto values=NativeOptionsPage::take_values(&page_kinds))
                native_settings::merge(changes,native_settings::apply(settings_,*values,page_kinds.size()>static_cast<std::size_t>(native_settings::edge_aa) && page_kinds[native_settings::edge_aa]==NativeOptionsPage::Kind::slider));}
            for(const int row:NativeOptionsPage::take_actions()) native_settings::merge(changes,native_settings::action(settings_,row));
            const auto old_calibration_state=avatar_calibration_.state();
            const auto calibration_state=avatar_calibration_.synchronize(reference_epoch(),
                settings_.open() && settings_.calibration_page() && !settings_.measurements_page() && !settings_.hand_fit_page() && !gameplay && state==XR_SESSION_STATE_FOCUSED);
            if(calibration_state!=old_calibration_state) settings_.calibration_status(calibration_state);
            if(changes.calibrate) {
                calibration_floor_y_.reset();
                if(floor_space_) {
                    XrSpaceLocation floor{};floor.type=XR_TYPE_SPACE_LOCATION;
                    if(xrLocateSpace(floor_space_,local_,locate_display_time_,&floor)==XR_SUCCESS)calibration_floor_y_=xr_floor_datum(floor);
                }
                if(avatar_measurements_) avatar_calibration_.begin(reference_epoch(),*avatar_measurements_,calibration_floor_y_,settings_.player_measurements());
                else settings_.calibration_status(AvatarCalibrationState::dimensions_unsupported);
            }
            if(settings_.open() && !gameplay && settings_.calibration_page() && !settings_.measurements_page() && !settings_.hand_fit_page() && avatar_measurements_) {
                std::array<TrackedPose,2> hands{};
                for(unsigned side=0;side<2;++side) {
                    const auto& p=grip_locations[side].pose;
                    hands[side]={{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}};
                }
                TrackedPose head{};
                if(views_valid_) head=eye_centre_pose(
                    {{views_[0].pose.position.x,views_[0].pose.position.y,views_[0].pose.position.z},{views_[0].pose.orientation.x,views_[0].pose.orientation.y,views_[0].pose.orientation.z,views_[0].pose.orientation.w}},
                    {{views_[1].pose.position.x,views_[1].pose.position.y,views_[1].pose.position.z},{views_[1].pose.orientation.x,views_[1].pose.orientation.y,views_[1].pose.orientation.z,views_[1].pose.orientation.w}});
                constexpr XrViewStateFlags tracked_head=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
                const auto observed=avatar_calibration_.observe(head,hands,grip_tracked_hands,
                    views_valid_ && (view_state.viewStateFlags&tracked_head)==tracked_head && state==XR_SESSION_STATE_FOCUSED,
                    raw_both_grips_held,GetTickCount64(),reference_epoch());
                settings_.calibration_status(observed,avatar_calibration_.progress());
                settings_.calibration_environment(avatar_calibration_.stage(),calibration_floor_y_.has_value());
            }
            if(changes.resolution) resolution_change_=changes.resolution;
            if(changes.turning) turning_change_=changes.turning;
            if(changes.temporal_aa) {temporal_change_=changes.temporal_aa;upscale_due_=0;} // a picked mode carries its own scale (the preset)
            if(changes.upscale_percent) upscale_due_=GetTickCount64()+250;
            if(upscale_due_ && GetTickCount64()>=upscale_due_) {upscale_due_=0;temporal_change_=settings_.temporal_aa();} // same path as an AA mode change (FSR 4 Native <-> Quality)
            if(changes.hud_percent) hud_change_=changes.hud_percent;
            if(changes.controls_guide) toggle_guide(!gameplay);
            if(guide_stick_blocked()) {controller_input.turn_x=controller_input.turn_y=0;controller_input.right_click=false;}
            if(guide_from_menu_ && gameplay) {guide_open_=false;guide_from_menu_=false;} // opened over the pause menu: back in the game, it goes
            if(changes.recenter) {
                continuity_.recenter();rendered_mask_=0;menu_panel_.reset();world_ui_pose_valid_=false;guide_placed_=false;
                settings_.calibration_status(avatar_calibration_.synchronize(reference_epoch(),false));
            }
            if(changes.antialiasing) {
                if(*changes.antialiasing && !antialiasing_) {
                    Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
                    antialiasing_=std::make_unique<EyeAntialiasing>(device.Get());
                }
                antialiasing_enabled_=*changes.antialiasing;
            }
            if(!preferences_file_.empty() && (changes.resolution || changes.antialiasing || changes.turning || changes.mirror || changes.sharpness || changes.temporal_aa || changes.upscale_percent || changes.hud_percent || changes.controls_hint || changes.crosshair || changes.scripted_recenter || changes.cutscene_mode || changes.player_measurements))
                save_preferences(preferences_file_,settings_.preferences());
            // Includes squeeze: holding the calibration pose must not open the native weapon selector.
            if(changes.consume) {controller_input={};settings_consumed=true;}
            if(changes.test_unlock) test_unlock_requested=true;
            publish_palm_preview(); // the fit page opened, was nudged, confirmed or left
        }
        // Notes (2) get no pointer: pointer input switches the game to keyboard glyphs ("Esc Close") and a note has nothing to click.
        // The wheel keeps it: its sector selection runs through the native pointer (without it the wheel ignored the cursor).
        if(wheel_dial_on_.load()) { // v1129 diagnostic: on frames the hand points at an item, which check below holds the pointer back (journal wheel_pointer)
            const unsigned fail=(!native_pointer_enabled_?1u:0u)|(world_ui_kind_==2?2u:0u)|(world_ui_kind_==1 && !wheel_native_pointer?4u:0u)|
                (world_ui_kind_!=0 && !world_ui_source_?8u:0u)|(scene_render?16u:0u)|(cinematic?32u:0u)|(settings_.open()?64u:0u)|
                (!continuity_.usable()?128u:0u)|(state!=XR_SESSION_STATE_FOCUSED?256u:0u)|(!controller_input.active?512u:0u)|
                (world_ui_kind_!=1?1024u:0u)|(!hand_wheel_mode.load()?2048u:0u);
            wheel_gate_fail_or.fetch_or(fail);wheel_gate_frames.fetch_add(1);if(!fail) wheel_gate_pass.fetch_add(1);
        }
        if(native_pointer_enabled_ && world_ui_kind_!=2 && (world_ui_kind_!=1 || wheel_native_pointer) && (world_ui_kind_==0 || world_ui_source_) && !scene_render && !cinematic && !settings_.open() && continuity_.usable() && state==XR_SESSION_STATE_FOCUSED && controller_input.active) {
            constexpr XrSpaceLocationFlags valid_pose=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            const auto& aim=aim_locations[menu_pointer_hand&1];
            if(world_ui_kind_==1 && hand_wheel_mode.load()) { // the hand wheel: the hand's direction (wheel_dial), never the aim ray (also in its closing tail)
                if(wheel_dial_on_.load()) {
                    const auto view=displayed_menu_view();const double r=wheel_dial_r_.load()*.9; // just inside the ring: on the item
                    const double u=std::clamp(.5+wheel_dial_x_.load()*r/view.width,0.,1.),v=std::clamp(.5-wheel_dial_y_.load()*r/view.height,0.,1.);
                    native_menu_input={u,view.v0+v*view.vspan,GetTickCount64(),continuity_.epoch(),true,
                        ((menu_pointer_hand&1)?controller_input.right_trigger:controller_input.left_trigger)>.55f};
                }
            } else if((aim.locationFlags&valid_pose)==valid_pose) {
                const auto& p=aim.pose;const auto view=displayed_menu_view();
                const auto hit=panel_pointer({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},world_ui_kind_?world_ui_pose():native_menu_pose(),view.width,view.height);
                // The panel shows the middle band; the game expects coordinates over the whole image.
                if(hit) native_menu_input={hit->u,view.v0+hit->v*view.vspan,GetTickCount64(),continuity_.epoch(),true,
                    ((menu_pointer_hand&1)?controller_input.right_trigger:controller_input.left_trigger)>.55f};
            }
        }
        const bool note_input=note_tablet_enabled_ && world_ui_kind_==2 && world_ui_source_ &&
            !settings_.open() && !settings_consumed && continuity_.usable() && state==XR_SESSION_STATE_FOCUSED && controller_input.active;
        const auto now_note=GetTickCount64();
        std::optional<Vec3> touch;
        note_models_active_=note_input && note_binding_ && note_panel_ &&
            note_panel_->generation==note_generation_ &&
            note_panel_->current(now_note,pose_epoch(),reference_epoch(),alternate_frame) &&
            note_hands_[0].current(now_note,pose_epoch(),reference_epoch(),alternate_frame) &&
            note_hands_[1].current(now_note,pose_epoch(),reference_epoch(),alternate_frame);
        if(note_models_active_)touch=note_index_tip_;
        else {note_panel_.reset();note_hands_={};note_index_tip_.reset();}
        note_scroll_axis_=note_touch_scroll_.update(note_input,touch,note_panel_,controller_input.turn_y,
            now_note,pose_epoch(),reference_epoch(),alternate_frame);
        note_index_tip_=touch;
        controller_input.right_trigger=menu_trigger_gate_.update(raw_right_trigger,!scene_render || settings_.open() || settings_consumed,raw_input_active);
        timing.locate_input_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        XrCompositionLayerProjection projection{};projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        XrCompositionLayerQuad menu{};menu.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
        std::optional<XrCompositionLayerQuad> world_ui_panel,hud_window_panel;
        const bool composite_note=note_composition_ready() && world_scene && !authored_camera && tracked_pair_();
        // v776: the hand wheel at its hand goes into each eye (no quad layer); wheel_ring is its pose, none = the quad layer as before
        const auto wheel_ring=wheel_eye_ready() && world_scene && !authored_camera && tracked_pair_()?wheel_hand_pose():std::optional<TrackedPose>{};
        std::optional<XrCompositionLayerQuad> hint_panel,dot_panel,vignette_panel;
        std::array<std::optional<XrCompositionLayerQuad>,2> wrist_panels;
        std::array<const XrCompositionLayerBaseHeader*,12> menu_layers_with_pointer{};
        projection.space=local_;projection.viewCount=2;
        std::array<XrCompositionLayerProjectionView,2> layers{};projection.views=layers.data();
        XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=frame_.predictedDisplayTime;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        if(scene_render && !settings_.open()) menu_panel_.reset();
        const bool settings_world=gameplay && settings_.open();settings_in_world_=settings_world;
        // The watch is a gameplay layer: a menu, the wheel, a cutscene or a non-world frame hides it at once.
        update_wrist_watch(gameplay && world_scene && !authored_camera && !settings_.open() && world_ui_kind_==0);
        std::optional<XrCompositionLayerQuad> settings_panel;
        EmptyFrameFacts gates;gates.should_render=frame_.shouldRender!=XR_FALSE;
        // v1021: a resolution change resizes the XR eye chains at once; the game's own swapchain follows on its next resize.
        // A source still smaller than the new eye (an upscale with no image) is that transient, not a session fault: hold the
        // frame (the empty-frame fallback below) instead of throwing "Game eye source does not match" into recover_xr_session.
        // A larger source keeps working (internal SSAA resolves it down).
        const auto source_holds=[&](ID3D11Texture2D* texture) {
            if(!texture) return false;
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            return eye_source_holds(desc,width_,height_,format_);
        };
        const bool sources_hold=left && right && source_holds(left) && source_holds(right);
        if(left && right && !sources_hold) ++eye_source_pending;
        gates.continuity_usable=continuity_.usable();gates.have_sources=sources_hold;
        gates.tracked_ok=!world_scene || (!tracked_gameplay_required_ && !authored_scene_render) || tracked_pair_();
        gates.alternate_ok=alt_kept_ok_(authored_camera || settings_.open());
        if(gates.continuity_usable && gates.have_sources && gates.tracked_ok && gates.alternate_ok) {
            std::array<ID3D11Texture2D*,2> sources{left,right};bool menu_band_ready=false;
            if(settings_.open()) {update_settings_image(pointer);++settings_layers;}
            const auto sharpness=static_cast<float>(settings_.sharpness());
            if(sharpness>0 && !sharpen_) {
                Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);sharpen_=std::make_unique<EyeSharpen>(device.Get());
            }
            std::array<std::optional<MirrorRect>,2> mirror_rects{};D3D11_TEXTURE2D_DESC desktop_desc{};
            if(gameplay && desktop && mirror_draws(static_cast<int>(settings_.mirror()))) { // monitor view off: no layout, canvas or copy
                desktop->GetDesc(&desktop_desc);
                // Lay out in the displayed window's pixels, then map into the stretched backbuffer.
                const auto view=desktop_view[0] && desktop_view[1]?desktop_view:std::array<UINT,2>{desktop_desc.Width,desktop_desc.Height};
                mirror_rects=mirror_layout(static_cast<int>(settings_.mirror()),view[0],view[1],width_,height_);
                const float sx=static_cast<float>(desktop_desc.Width)/view[0],sy=static_cast<float>(desktop_desc.Height)/view[1];
                for(auto& r:mirror_rects) if(r) *r={r->x*sx,r->y*sy,r->width*sx,r->height*sy};
                if(!mirror_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);mirror_=std::make_unique<DesktopMirror>(device.Get());}
                // Composed off-screen: the right eye's source may be this backbuffer.
                if(mirror_rects[0] || mirror_rects[1]) mirror_->begin(desktop_desc.Width,desktop_desc.Height,desktop_desc.Format);
            }
            for(UINT pass=0;pass<2;++pass) {
                // v798: alternate-eye processes the fresh eye first, so the stale eye's warp can take near content from its kept image
                const UINT eye=alternate_enabled && alternate_fresh_eye==1?1u-pass:pass;
                // alternate-eye: the eye that was not rendered this frame is resubmitted from its kept image (sources[eye] is that copy)
                const bool alt_stale=alternate_enabled && alternate_fresh_eye>=0 && static_cast<int>(eye)!=alternate_fresh_eye;
                if(alt_stale) sources[eye]=alt_kept_[eye].Get();
                XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
                {SpanClock span(xr_spans,XrSpan::acquire_wait);
                checked(xrAcquireSwapchainImage(chains_[eye],&acquire,&index),"Acquire game eye");
                XrSwapchainImageWaitInfo image_wait{};image_wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;image_wait.timeout=1'000'000'000;
                auto ready=xrWaitSwapchainImage(chains_[eye],&image_wait);checked(ready,"Wait game eye");
                if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Game eye wait timed out");}
                XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                try {
                D3D11_TEXTURE2D_DESC desc{};sources[eye]->GetDesc(&desc);source_eye_sizes[eye]={desc.Width,desc.Height};
                ID3D11Texture2D* processed=sources[eye];
                if(!eye_texture_matches(desc,width_,height_,format_)) {
                    // v976: internal supersampling. The game rendered larger than the XR maxImageRect clamped eye;
                    // resolve it down before sharpening/SMAA/submission. Alternate-eye keeps exact-size kept images.
                    if(alt_stale || desc.Width<width_ || desc.Height<height_ || desc.SampleDesc.Count!=1 ||
                       !rgba8_copy_compatible(desc.Format,format_))
                        throw std::runtime_error("Game eye source does not match the current XR swapchain");
                    if(!resolve_) {
                        Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
                        resolve_=std::make_unique<EyeResolve>(device.Get(),width_,height_);
                    }
                    processed=resolve_->apply(sources[eye]);++eye_resolves;
                }
                // Independent per-eye processing; menus retain the exact native image.
                // v965: start from the eye source. v963 moved sharpening before SMAA but left this null, so CAS was
                // handed a null input every frame ("CAS input is missing"): no layer was submitted and VR never started.
                auto processing=alt_stale?alt_processing_[eye]:EyeTransferProcessing{true,false,false,false,settings_.effective_temporal_aa(),0};
                processing.kept=alt_stale;
                {ModGpuScope aa_scope(context_.Get(),ModSpan::aa_sharpen); // task 3b
                SpanClock span(xr_spans,XrSpan::aa_sharpen);
                // Sharpening also helps menu text (menus get no SMAA), but it must not undo the edge AA: CAS
                // re-steepens every edge it touches, so the 0.5 Medium default sharpening on top of SMAA was
                // the reason the SMAA row looked like it "did nothing". Sharpen first, then let SMAA be the
                // last spatial step (v963).
                if(sharpness>0 && !alt_stale) {
                    const auto prior_sharpen=processed;processed=sharpen_->apply(processed,sharpness);
                    processing.cas_applied=processed!=prior_sharpen;processing.cas_sharpness=processing.cas_applied?sharpness:0;
                }
                const auto prior_smaa_passes=antialiasing_?antialiasing_->eye_passes:0;
                processed=gameplay && antialiasing_enabled_ && alternate_smaa_applies(alternate_enabled,settings_.effective_temporal_aa()) && !alt_stale?antialiasing_->apply(processed):processed;
                if(!alt_stale)processing.smaa_applied=antialiasing_ && antialiasing_->eye_passes>prior_smaa_passes;
                }
                // v792: the kept image of the eye not rendered now, warped to the view that eye has at the fresh eye's Setup (one head pose for
                // both layers). A failure keeps the kept image and its own view, and turns the warp off for the session.
                if(alt_stale && stale_eye_warp_enabled && !stale_warp_failed_ && stale_target_frame_[eye]==alternate_frame && alt_records_[eye].valid) try {
                    const auto q=[](const XrQuaternionf& o) {return Quaternion{o.x,o.y,o.z,o.w};};
                    const auto f=[](const XrFovf& v) {return WarpFov{v.angleLeft,v.angleRight,v.angleUp,v.angleDown};};
                    const auto& from=alt_records_[eye].view;const auto& to=stale_target_[eye];
                    const auto h=stale_eye_homography(q(from.pose.orientation),f(from.fov),q(to.pose.orientation),f(to.fov));
                    const auto back=stale_eye_homography(q(to.pose.orientation),f(to.fov),q(from.pose.orientation),f(from.fov));
                    if(h && back) {
                        if(!stale_warp_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);stale_warp_=std::make_unique<StaleEyeWarp>(device.Get());}
                        stale_layers_[eye].reset();
                        // v798: the fresh eye was processed first (see the loop), so its kept image is this Present's; with its camera and depth
                        // it gives the near content of this eye as it is now.
                        const UINT fresh_eye=static_cast<UINT>(alternate_fresh_eye);
                        const bool fresh_now=alt_records_[fresh_eye].valid && alt_records_[fresh_eye].frame==alternate_frame && alt_kept_[fresh_eye] &&
                            rendered_camera_frame_[fresh_eye]==alternate_frame && stale_depth_frame_[fresh_eye]==alternate_frame && stale_depth_[fresh_eye];
                        if(stale_eye_parallax_enabled) { // v796: with parallax (both game cameras and the kept depth, for this Present)
                            if(kept_camera_valid_[eye] && stale_target_camera_frame_[eye]==alternate_frame && stale_depth_frame_[eye]==alternate_frame && stale_depth_[eye])
                                stale_layers_[eye]=stale_eye_layers(kept_cameras_[eye],f(from.fov),stale_target_cameras_[eye],f(to.fov),
                                    fresh_now?&rendered_cameras_[fresh_eye]:nullptr,f(alt_records_[fresh_eye].view.fov),stale_near_range);
                            if(stale_layers_[eye]) { // v802: the hands held still; v803: and how much they moved (diagnostic)
                                stale_layers_[eye]->near_from_kept=stale_hands_still(kept_cameras_[eye],stale_target_cameras_[eye]);
                                stale_layers_[eye]->hands_motion=stale_hands_motion(kept_cameras_[eye],stale_target_cameras_[eye]);
                                { // v1177 diagnostic: how far the rebuilt eye's camera moved (m) and turned (deg) from the kept to the target tick
                                    const auto& a=kept_cameras_[eye].pose;const auto& b=stale_target_cameras_[eye].pose;
                                    const double turn=std::acos(std::clamp(std::min(dot(a.basis[0],b.basis[0]),dot(a.basis[2],b.basis[2])),-1.,1.))*180/3.14159265358979;
                                    stale_camera_motion_[eye]={length(a.position-b.position),turn};
                                }
                                stale_layers_[eye]->frame=alternate_frame;stale_layers_[eye]->eye=eye; // v811: the hand-fit read-backs' tags
                                stale_layers_[eye]->fresh_part=stale_fresh_part;stale_layers_[eye]->kept_part=stale_kept_part; // v1151
                                stale_layers_[eye]->motion_rules=stale_motion_rules;
                                stale_layers_[eye]->rig_motion_rules=stale_rig_motion_rules; // v1167: rig pixels by their own motion (knob no-stale-eye-rig-motion)
                                stale_layers_[eye]->rig_motion={stale_rig_motion(eye),fresh_now && stale_rig_motion(fresh_eye)};
                                stale_layers_[eye]->afw2=stale_afw2; // v1184: knob build/afw2
                                stale_layers_[eye]->rig_target=static_cast<bool>(stale_rig_target(eye).texture); // v1190: the exact rig in this eye's target camera
                            }
                            if(!stale_layers_[eye]) ++stale_eye_parallax_misses;
                        }
                        const auto parallax_before=stale_warp_->parallax_passes;
                        const StaleDepth kept_depth{stale_depth_[eye].Get(),stale_depth_width_[eye],stale_depth_height_[eye]};
                        const StaleDepth fresh_depth{fresh_now?stale_depth_[fresh_eye].Get():nullptr,stale_depth_width_[fresh_eye],stale_depth_height_[fresh_eye]};
                        ID3D11Texture2D* const fresh_image=fresh_now?alt_kept_[fresh_eye].Get():nullptr;
                        // v1151: the kept frame's motion and the fresh frame's (both, or the mover rules stay off)
                        const StaleMotion kept_motion=stale_motion(eye),fresh_motion=fresh_now?stale_motion(fresh_eye):StaleMotion{};
                        // v1162: both rig masks (marked renders of this Present), else none: the warp keeps its palm-sphere ownership
                        StaleRig kept_rig=stale_rig_mask(eye),fresh_rig=fresh_now?stale_rig_mask(fresh_eye):StaleRig{};
                        if(!kept_rig.texture || !fresh_rig.texture) {if(kept_rig.texture || fresh_rig.texture) ++stale_rig_one_sided;kept_rig={};fresh_rig={};}
                        else if(stale_layers_[eye]) ++stale_rig_warps;
                        {ModGpuScope warp_scope(context_.Get(),ModSpan::stale_warp); // v1191 timing (old path or AFW v2)
                        processed=stale_layers_[eye]?stale_warp_->apply(processed,*h,&*stale_layers_[eye],kept_depth,fresh_image,fresh_depth,kept_motion,fresh_motion,kept_rig,fresh_rig,stale_rig_target(eye)):stale_warp_->apply(processed,*h);}
                        if(stale_warp_->parallax_passes==parallax_before) stale_layers_[eye].reset(); // depth refused: rotation only, the reticle too
                        if(stale_layers_[eye] && observe_stale_warp) observe_stale_warp(eye,sources[eye],processed,fresh_image,kept_depth,fresh_depth,*stale_layers_[eye]);
                        stale_warped_frame_[eye]=alternate_frame;stale_back_[eye]=*back;++stale_eye_warps;
                    }
                } catch(const std::exception& e) {stale_warp_failed_=true;stale_warped_frame_[eye]=0;++stale_eye_warp_failures;processed=sources[eye];
                    try {stale_warp_error=std::string(e.what()).substr(0,300);} catch(...) {}} // v1197: the reason, for the status
                transfer_processing_[eye]=processing;
                // v808 (headset v806: canted eyes "clearly sharper", but "the eyes felt pulled toward the centre"): a canted eye is rendered
                // turned and goes out in its own located view, resampled here (the turn taken back off, the runtime's own FOV), so the
                // runtime gets the layers any application submits. Only images rendered turned (authored cameras keep the enclosing frustum).
                ID3D11Texture2D* eye_image=processed;uncanted_[eye]=false;XrPosef straight_pose{};XrFovf straight_fov{};
                std::array<float,2> scope_axis{0.f,0.f}; // v1040: where the rendered view's axis falls in the straight layer (a canted view is turned)
                if(canted_views && tracked_pair_() && !settings_.open()) {
                    const auto turned=tracked_view_(eye);const auto& lf=views_[eye].fov;
                    const auto c=canted_fov(lf.angleLeft,lf.angleRight,lf.angleUp,lf.angleDown);
                    if(c && std::abs(turned.fov.angleRight-c->half_x)<1e-4f && std::abs(turned.fov.angleUp-c->half_y)<1e-4f) {
                        const Quaternion t{turned.pose.orientation.x,turned.pose.orientation.y,turned.pose.orientation.z,turned.pose.orientation.w};
                        const Quaternion s=normalized(multiplied(t,Quaternion{-c->rotation[0],-c->rotation[1],-c->rotation[2],c->rotation[3]}));
                        const WarpFov tf{turned.fov.angleLeft,turned.fov.angleRight,turned.fov.angleUp,turned.fov.angleDown},sf{lf.angleLeft,lf.angleRight,lf.angleUp,lf.angleDown};
                        const auto h=stale_eye_homography(t,tf,s,sf),back=stale_eye_homography(s,sf,t,tf);
                        if(h && back) {
                            if(!uncant_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);uncant_=std::make_unique<StaleEyeWarp>(device.Get());}
                            eye_image=uncant_->apply(processed,*h);uncant_back_[eye]=*back;uncanted_[eye]=true;++uncanted_eyes;
                            scope_axis=spyglass_scope::axis_tangent(c->rotation);
                            straight_pose=turned.pose;straight_pose.orientation={static_cast<float>(s.x),static_cast<float>(s.y),static_cast<float>(s.z),static_cast<float>(s.w)};straight_fov=lf;
                        }
                    }
                }
                {ModGpuScope copy_scope(context_.Get(),ModSpan::eye_copy); // task 3b
                SpanClock span(xr_spans,XrSpan::copy);
                if(settings_.open() && !gameplay) {
                    const auto view=menu_view();
                    const auto rect=SettingsMenuLayout::image_rect(width_,height_,view.y,view.rows);
                    settings_compositor_->begin(width_,height_,format_);
                    settings_compositor_->draw(processed,{0,0,static_cast<float>(width_),static_cast<float>(height_)});
                    settings_compositor_->draw(settings_texture_.Get(),{static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height)});
                    settings_compositor_->end(images_[eye].at(index).texture);
                    if(observe_settings_composition) observe_settings_composition(processed,images_[eye].at(index).texture,rect);
                } else context_->CopyResource(images_[eye].at(index).texture,eye_image);}
                layers[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                layers[eye].pose=uncanted_[eye]?straight_pose:tracked_pair_()?tracked_view_(eye).pose:views_[eye].pose;
                layers[eye].fov=uncanted_[eye]?straight_fov:tracked_pair_()?tracked_view_(eye).fov:XrFovf{-hfov*.5f,hfov*.5f,vfov*.5f,-vfov*.5f};
                layers[eye].subImage.swapchain=chains_[eye];
                layers[eye].subImage.imageRect.extent={static_cast<int32_t>(width_),static_cast<int32_t>(height_)};
                // Validate the exact base transfer before ANY destination
                // composition. Overlay pixels are independently verified.
                if(observe_transfer && !settings_.open()) {SpanClock span(xr_spans,XrSpan::transfer_observer);observe_transfer(eye,eye_image,images_[eye].at(index).texture,layers[eye],gameplay,tracked_pair_(),sources[eye]);}
                if(alternate_enabled && !alt_stale && gameplay) {
                    if(alternate_may_keep(gameplay,authored_camera,settings_.open())) {
                        keep_alternate_eye_(eye,processed);
                        if(observe_fresh_eye) observe_fresh_eye(eye,processed,alternate_frame); // v802 diagnostic: the frame after a stale-warp dump
                    }
                    else alt_records_[eye]={}; // a cut, takedown or settings image is shown now and never kept
                }
                {ModGpuScope overlay_scope(context_.Get(),ModSpan::eye_overlay); // task 3b: native object art + desktop mirror draws
                SpanClock span(xr_spans,XrSpan::overlay_draws);
                // v880: the spyglass lens over the copied image, under every overlay; a fault turns it off for the session.
                if(gameplay && tracked_pair_() && !authored_camera && !settings_.open() && spyglass_magnification>1.02f && !spyglass_lens_failures) try {
                    if(!spyglass_lens_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);spyglass_lens_=std::make_unique<SpyglassLens>(device.Get());}
                    spyglass_lens_->draw(images_[eye].at(index).texture,eye_image,layers[eye].fov,spyglass_magnification);++spyglass_lens_draws;
                } catch(const std::exception&) {++spyglass_lens_failures;}
                // v1040: the right-eye scope: the right view was rendered with the zoom-narrowed projection, so its image is already the magnified
                // scene; here everything outside the round scope goes dark. The left eye keeps its normal view.
                if(eye==1 && gameplay && tracked_pair_() && !authored_camera && !settings_.open() && spyglass_scope_mag>1.001f && !spyglass_scope_failures) try {
                    if(!spyglass_scope_mask_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);spyglass_scope_mask_=std::make_unique<SpyglassScopeMask>(device.Get());}
                    spyglass_scope_mask_->draw(images_[eye].at(index).texture,layers[eye].fov,scope_axis,spyglass_art_probe().art());++spyglass_scope_draws;
                } catch(const std::exception&) {++spyglass_scope_failures;}
                if(composite_note && !native_note_art_disabled_) try { // v685: a note-art fault disables the note overlay, never the frame
                    if(!note_eye_art_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);note_eye_art_=std::make_unique<NoteEyeArt>(device.Get());}
                    D3D11_TEXTURE2D_DESC art_desc{};world_ui_source_->GetDesc(&art_desc);
                    const auto rows=static_cast<UINT>(world_ui_rows(art_desc)),top=(art_desc.Height-rows)/2;
                    XrView note_view{XR_TYPE_VIEW};note_view.pose=layers[eye].pose;note_view.fov=layers[eye].fov;
                    note_eye_art_->draw(images_[eye].at(index).texture,world_ui_source_.Get(),format_,
                        {0,top,0,art_desc.Width,top+rows,1},note_view,*note_panel_,note_hands_);++note_eye_draws;
                } catch(const std::exception& error) {
                    native_note_art_disabled_=true;++native_note_eye_art_failures;
                    if(observe_eye_art_failure) {
                        const std::string text=std::string("Note eye art disabled (")+error.what()+"); VR keeps presenting";
                        observe_eye_art_failure(text.c_str());
                    }
                }
                if(wheel_ring && !wheel_eye_art_disabled_) try { // v776: a ring-art fault disables the wheel's eye art (the quad layer takes over), never the frame
                    if(!note_eye_art_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);note_eye_art_=std::make_unique<NoteEyeArt>(device.Get());}
                    D3D11_TEXTURE2D_DESC art_desc{};world_ui_source_->GetDesc(&art_desc);
                    const auto rows=static_cast<UINT>(world_ui_rows(art_desc)),top=(art_desc.Height-rows)/2;
                    XrView ring_view{XR_TYPE_VIEW};ring_view.pose=layers[eye].pose;ring_view.fov=layers[eye].fov;
                    const double ring_width=world_ui_width();const auto now_ring=GetTickCount64();
                    const NotePanelReceipt ring_panel{*wheel_ring,ring_width,ring_width*rows/art_desc.Width,now_ring,pose_epoch(),reference_epoch(),alternate_frame,0};
                    std::array<NoteHandReceipt,2> over{};unsigned over_count=0;bool grace=false;
                    for(unsigned side=0;side<2;++side) { // the hands drawn this frame, or (a frame's hands missing) the last frame's: a hand never flickers over/under the ring
                        const auto& hand=wheel_hands_[side];
                        if(hand.current(now_ring,pose_epoch(),reference_epoch(),alternate_frame)) {over[side]=hand;++over_count;}
                        else if(alternate_frame>1 && hand.current(now_ring,pose_epoch(),reference_epoch(),alternate_frame-1)) {over[side]=hand;++over_count;grace=true;}
                    }
                    note_eye_art_->draw(images_[eye].at(index).texture,world_ui_source_.Get(),format_,{0,top,0,art_desc.Width,top+rows,1},ring_view,ring_panel,over,true);
                    ++wheel_eye_draws;if(over_count)++wheel_eye_occluded;if(grace)++wheel_eye_grace;
                } catch(const std::exception& error) {
                    wheel_eye_art_disabled_=true;++wheel_eye_failures;
                    if(observe_eye_art_failure) {
                        const std::string text=std::string("Hand wheel eye art disabled (")+error.what()+"); the quad layer takes over";
                        observe_eye_art_failure(text.c_str());
                    }
                }
                // v718 (headset v717: Dr. Hypatia's "Press [Grip] to inject counter-serum." is shown by the game only during the choke,
                // whose camera is authored, and was never drawn): head-locked messages need no world anchor and draw in any gameplay camera.
                // The reticle and world-anchored art (prompts, markers) still wait for the player's own camera.
                if(gameplay && tracked_pair_()) {
                    const auto now=GetTickCount64();XrView view{XR_TYPE_VIEW};view.pose=layers[eye].pose;view.fov=layers[eye].fov;
                    if(!authored_camera && eye_reticle_available() && settings_.crosshair() && !settings_.open() && state==XR_SESSION_STATE_FOCUSED && controller_input.active) try {
                        const auto image_frame=alternate_enabled?alt_effective_()[eye].frame:alternate_frame;
                        auto reticle=eye_reticles_.find(eye,image_frame,pose_epoch(),reference_epoch(),now);
                        if(reticle && stale_warped_(eye)) { // v792: the point was placed in the kept image; move it into the warped one
                            const double ru=(reticle->x+1.)*.5,rv=(1.-reticle->y)*.5; // v796: with parallax, at the reticle's own distance
                            const auto moved=stale_layers_[eye] && reticle->depth>0?parallax_target(stale_layers_[eye]->world,ru,rv,reticle->depth):warp_point(stale_back_[eye],ru,rv);
                            if(moved) {reticle->x=static_cast<float>((*moved)[0]*2.-1.);reticle->y=static_cast<float>(1.-(*moved)[1]*2.);}
                            else reticle.reset();
                        }
                        if(reticle && uncanted_[eye]) { // v808: and from the turned image into the one submitted
                            const auto moved=warp_point(uncant_back_[eye],(reticle->x+1.)*.5,(1.-reticle->y)*.5);
                            if(moved) {reticle->x=static_cast<float>((*moved)[0]*2.-1.);reticle->y=static_cast<float>(1.-(*moved)[1]*2.);}
                            else reticle.reset();
                        }
                        bool game_crosshair=false;
                        if(reticle && crosshair_art_.texture && crosshair_art_.epoch==pose_epoch() && now>=crosshair_art_.tick && now-crosshair_art_.tick<=150 && ensure_native_eye_art()) {
                            // The element's anchor sits on the reticle point at the game's own pixel size.
                            const auto& a=crosshair_art_;const float w=static_cast<float>(width_),h=static_cast<float>(height_);
                            const float px=(reticle->x+1.f)*.5f*w-a.anchor_x*a.scale,py=(1.f-reticle->y)*.5f*h-a.anchor_y*a.scale;
                            const float cw=static_cast<float>(a.crop.right-a.crop.left)*a.scale,ch=static_cast<float>(a.crop.bottom-a.crop.top)*a.scale;
                            const auto art_result=native_eye_art_->draw(images_[eye].at(index).texture,a.texture.Get(),a.crop,
                                {px/w*2.f-1.f,1.f-py/h*2.f,(px+cw)/w*2.f-1.f,1.f-(py+ch)/h*2.f},format_,true);
                            if(art_result.drawn) {++eye_reticle_draws;++crosshair_art_draws;game_crosshair=true;}
                            else {note_eye_art_fault(art_result,"game crosshair");crosshair_art_={};}
                        }
                        if(reticle && !game_crosshair)
                            if(const auto rect=eye_reticle_rect(reticle->x,reticle->y,width_,height_,10.f)) { // the game dot reads as large as the old ring
                                if(!eye_reticle_texture_) {
                                    Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
                                    if(!device) throw std::runtime_error("Missing eye reticle device");
                                    const auto pixels=eye_reticle_pixels();D3D11_TEXTURE2D_DESC d{};
                                    d.Width=d.Height=32;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                                    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                                    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=pixels.data();initial.SysMemPitch=32*4;
                                    if(FAILED(device->CreateTexture2D(&d,&initial,&eye_reticle_texture_))) throw std::runtime_error("Cannot create eye reticle");
                                }
                                if(ensure_native_eye_art()) {
                                    const D3D11_BOX crop{0,0,0,32,32,1};
                                    const auto art_result=native_eye_art_->draw(images_[eye].at(index).texture,eye_reticle_texture_.Get(),crop,
                                        {(*rect)[0],(*rect)[1],(*rect)[2],(*rect)[3]},format_,false,DXGI_FORMAT_R8G8B8A8_UNORM);
                                    if(art_result.drawn) ++eye_reticle_draws;
                                    else note_eye_art_fault(art_result,"eye reticle"); // the game crosshair child still draws
                                }
                            }
                    } catch(...) {
                        // Fail locally. The following prepared frame restores
                        // the engine's own validated crosshair child.
                        eye_reticle_failed_=true;eye_reticle_texture_.Reset();eye_reticles_.clear();crosshair_art_={};
                    }
                    for(unsigned art_slot=0;art_slot<native_object_art_.size();++art_slot) if(const auto& art=native_object_art_[art_slot];art.fresh(pose_epoch(),now)) {
                        if(authored_camera && !art.locked) continue;
                        const auto aspect=static_cast<double>(art.crop.right-art.crop.left)/(art.crop.bottom-art.crop.top);
                        const auto locked=art.locked?head_locked_art_point(tracked_view_(0).pose,tracked_view_(1).pose,art.lock_offset):std::optional<Vec3>{};
                        if(art.locked && !locked) {++native_object_art_slot_unlocked[art_slot];continue;}
                        const auto rect=native_art_rect(view,locked?*locked:art.point,art.width*hud_scale(),aspect);
                        if(!rect) ++native_object_art_slot_norect[art_slot];
                        else native_object_art_slot_rect[art_slot]={rect->left,rect->top,rect->right,rect->bottom};
                        if(rect) {
                            if(ensure_native_eye_art()) {
                                const auto art_result=native_eye_art_->draw(images_[eye].at(index).texture,art.texture.Get(),art.crop,*rect,format_,art.premultiplied);
                                if(art_result.drawn) {++native_object_art_draws;++native_object_art_slot_draws[art_slot];native_object_art_masks_[art_slot]|=1u<<eye;}
                                else {note_eye_art_fault(art_result,"native object art");native_object_art_[art_slot]={};}
                            }
                        }
                    }
                }
                if(mirror_rects[eye]) mirror_->draw(images_[eye].at(index).texture,*mirror_rects[eye]);}
                if(composite_note && observe_note_composition)observe_note_composition(eye,images_[eye].at(index).texture,layers[eye],*note_panel_);
                if(observe_final_eye && world_scene)observe_final_eye(eye,images_[eye].at(index).texture);
                if(eye==0 && !world_scene) gates.band_ready=menu_band_ready=copy_menu_band(images_[0].at(index).texture);
                } catch(...) {xrReleaseSwapchainImage(chains_[eye],&release);throw;}
                {SpanClock span(xr_spans,XrSpan::release);checked(xrReleaseSwapchainImage(chains_[eye],&release),"Release game eye");}
            }
            if(mirror_rects[0] || mirror_rects[1]) {mirror_->end(desktop);++mirrored_frames;}
            if(!world_scene) {
                // A quad frame copied its duplicated mono image into both eye chains: an older projection replayed later would show that mono
                // image with its own stereo poses, so it is dropped here (Ruling 15: only after quad frames). A scene frame keeps the previous
                // projection until its own is captured below, so a frame whose projection the guard drops (a non-finite pose) still replays
                // stereo (the chains hold this frame's stereo images); a cutscene hold replays only its own scene's layer (last_good_scene_).
                last_good_projection_.reset();last_good_scene_=0;
                // Native menus use a pointer-enabled panel; cutscenes play on a larger, farther "TV".
                const auto& pose=cinematic?panel_pose(authored_panel_):native_menu_pose();
                const auto view=menu_view(cinematic);
                if(!authored_camera) last_menu_pose=pose;
                menu.space=local_;menu.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                menu.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),
                    static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
                menu.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
                menu.size={view.width,view.height};menu.subImage.swapchain=menu_band_chain_; // the band chain shows whole (no sub-rect)
                menu.subImage.imageRect.extent={static_cast<int32_t>(width_),view.rows};
                layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu);
            }
            end.layerCount=world_scene || menu_band_ready?1:0;end.layers=&layer; // no band chain yet (between a resize and prepare_overlays): nothing this frame
            if(!world_scene && menu_band_ready && (hint_panel=hint_layer())) { // the controls page opened from VR Settings shows over the pause menu
                menu_layers_with_pointer[0]=layer;menu_layers_with_pointer[1]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hint_panel);
                end.layerCount=2;end.layers=menu_layers_with_pointer.data();++hint_layers;}
            if(world_scene && !authored_camera) {
                ModGpuScope layers_scope(context_.Get(),ModSpan::layer_overlays); // task 3b: HUD/wrist/vignette panel updates
                SpanClock layers_span(xr_spans,XrSpan::overlay_layers);
                // Projection first, then the wrist controls card.
                end.layerCount=0;menu_layers_with_pointer[end.layerCount++]=layer;
                if(world_ui_kind_ && world_ui_source_ && !composite_note && !(wheel_ring && !wheel_eye_art_disabled_) &&
                   world_ui_quad_allowed(world_ui_kind_==1,hand_wheel_mode.load(),wheel_hand_)) {
                    if((world_ui_panel=world_ui_layer())) {
                        menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*world_ui_panel);
                        ++world_ui_layers;++world_ui_layers_by_kind[world_ui_kind_-1];last_world_ui_layer=true;
                    }
                }
                if(gameplay && !world_ui_kind_ && hud_window_source_) {
                    if((hud_window_panel=hud_window_layer())) {
                        menu_layers_with_pointer[end.layerCount++]=cylinder_supported_?reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud_cylinder_):
                            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hud_window_panel);
                        ++hud_window_layers;
                    }
                }
                if(gameplay) {
                    if(legacy_wrist_hud) { // A/B: the old health/mana right wrist and ammo left wrist, inner side
                        for(unsigned side:{1u,0u}) {
                            wrist_panels[side]=optional_wrist_layer(side);
                            if(wrist_panels[side]) {
                                menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*wrist_panels[side]);++wrist_layers[side];}
                        }
                    } else { // v1013: one watch on the back of the left wrist
                        wrist_panels[0]=optional_wrist_layer(0);
                        if(wrist_panels[0]) {
                            menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*wrist_panels[0]);++wrist_layers[0];}
                    }
                }
                if((hint_panel=hint_layer())) {
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hint_panel);++hint_layers;
                }
                if(settings_world && (settings_panel=settings_layer())) {
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*settings_panel);++settings_world_layers;
                }
                if((gameplay || world_ui_kind_==1) && wheel_cursor_ && (dot_panel=dot_layer())) // last: drawn over the wheel (open wheel is not gameplay)
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*dot_panel);
                if(gameplay && (vignette_panel=vignette_layer())) { // submitted last: the ring sits over the HUD panels too
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*vignette_panel);++vignette_layers;
                }
                end.layers=menu_layers_with_pointer.data();
                overlay_stats.end_frame();
            } else if(scene_render && authored_camera && (vignette_panel=vignette_layer())) {
                // An authored scene (a G1 scripted moment's 0.4 s ease, a VR comfort cutscene's moving camera) shows no HUD panels, only
                // the comfort ring over the projection; the probe keeps its opacity 0 everywhere else.
                end.layerCount=0;menu_layers_with_pointer[end.layerCount++]=layer;
                menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*vignette_panel);++vignette_layers;
                end.layers=menu_layers_with_pointer.data();
                overlay_stats.end_frame();
            }
        }
        timing.transfer_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        xr_spans.end_frame();
        std::array<const XrCompositionLayerBaseHeader*,12> kept{}; // end.layers points here through xrEndFrame and the failure dump
        {static const char* const kinds[]={"projection","quad","cylinder","other"};
        const auto guard=guard_frame_layers(end,kept.data(),kept.size());
        subrect_layers+=count_subrect_layers(end,chain_sizes_);
        for(size_t k=0;k<4;++k) {layer_pose_repaired[k]+=guard.repaired[k];layer_pose_dropped[k]+=guard.dropped[k];}
        if(guard.submit_none) ++layer_pose_no_submit;
        if(guard.bad_kind>=0) {char text[160];std::snprintf(text,sizeof(text),"%s q=(%g,%g,%g,%g) p=(%g,%g,%g)",kinds[guard.bad_kind],guard.bad_pose.orientation.x,guard.bad_pose.orientation.y,guard.bad_pose.orientation.z,guard.bad_pose.orientation.w,guard.bad_pose.position.x,guard.bad_pose.position.y,guard.bad_pose.position.z);layer_pose_last=text;}}
        // Never an empty frame while the session shows frames (xr_frame_hardening.hpp): the runtime answers zero layers by rebuilding its direct-mode
        // swap sets, and on Linux that was followed by "Couldn't find a backbuffer" / "Connection inactive". Re-send the last real projection (its own
        // poses) or, with none, an opaque black quad. Every frame that would have been empty is counted and logged with a reason.
        XrCompositionLayerQuad fallback_quad{};const XrCompositionLayerBaseHeader* fallback_layer=nullptr;
        const unsigned real_layers=end.layerCount;
        if(real_layers && end.layers[0]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION &&
           last_good_projection_.capture(*reinterpret_cast<const XrCompositionLayerProjection*>(end.layers[0]),GetTickCount64(),pose_epoch()))
            last_good_scene_=authored_scene_render?scene_id:0;
        if(!real_layers) {
            gates.layer_count=0;
            const auto reason=classify_empty_frame(gates);
            const auto now_ms=GetTickCount64();
            // A cutscene hold replays only this scene's own immersive layer (replay), or nothing but black (black).
            const bool replay_ok=scene_hold_replays(scene_hold_now==SceneHold::none?0u:scene_hold_now==SceneHold::black?1u:2u,hold_scene,last_good_scene_);
            auto choice=choose_frame_fallback({0,state,gates.should_render,reason,replay_ok && last_good_projection_.usable(chains_,now_ms,pose_epoch()),blank_ready_()});
            if(choice==FrameFallback::last_projection) {
                fallback_layer=last_good_projection_.header();end.layers=&fallback_layer;end.layerCount=1;
            } else if(choice==FrameFallback::black_quad) {
                if(const auto quad=black_fallback_layer_()) {
                    fallback_quad=*quad;fallback_layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fallback_quad);end.layers=&fallback_layer;end.layerCount=1;
                } else choice=FrameFallback::none;
            }
            ++empty_frames_seen;++empty_frames_by_reason[static_cast<unsigned>(reason)];
            if(choice==FrameFallback::last_projection) ++empty_fallback_projection;else if(choice==FrameFallback::black_quad) ++empty_fallback_black;else ++empty_fallback_none;
            if(observe_xr_event) {
                const auto decision=empty_event_limiter_.admit(now_ms);
                if(decision.final_notice) observe_xr_event("xr-event empty-frame log cap reached; further empty frames are counted in xr-live-status.json only");
                else if(decision.emit) {
                    const auto [utc,local]=wall_stamps_();
                    observe_xr_event(format_empty_frame_event(present_count_,state,reason,choice,gates.should_render,gameplay,authored_camera,world_scene,gates.band_ready,utc,local,decision.suppressed_before).c_str());
                }
            }
        }
        if(const auto ended=xrEndFrame(session_,&end);XR_FAILED(ended)) {
            // SteamVR fails single frames with layer errors ("compositor failed EndFrame()"): drop that frame instead of recreating the
            // session (the user sees SteamVR home); a run of 30 still recovers. The layer dump goes to xr-recoveries.log either way.
            const auto name="Submit game XR frame ["+describe_frame_layers(end)+"]";
            const bool layer_error=ended==XR_ERROR_POSE_INVALID || ended==XR_ERROR_SWAPCHAIN_RECT_INVALID || ended==XR_ERROR_LAYER_INVALID || ended==XR_ERROR_LAYER_LIMIT_EXCEEDED;
            if(layer_error && vignette_panel && !vignette_runtime_off_) { // the optional ring is the likeliest culprit and costs nothing to lose
                vignette_runtime_off_=true;end_frame_rejected_run_=0;
                if(observe_frame_rejection) observe_frame_rejection("Comfort vignette disabled for this session after the runtime rejected a frame carrying it");
            }
            if(!layer_error || ++end_frame_rejected_run_>30) checked(ended,name.c_str());
            ++end_frame_rejections;if(observe_frame_rejection) observe_frame_rejection((name+": "+std::to_string(ended)+" (frame dropped)").c_str());
        } else end_frame_rejected_run_=0;
        frame_open_=false;
        if(gpu_queue_limit && context_) {
            if(!frame_fence_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};device->CreateQuery(&desc,&frame_fence_);}
            if(frame_fence_) {context_->End(frame_fence_.Get());frame_fence_pending_=true;}
        }
        timing.submit_ns=cpu_time_ns()-stage_start;timing.end_ns=cpu_time_ns();
        last_layer_submitted=real_layers!=0;last_menu_layer=last_layer_submitted && !world_scene; // real mod layers only: a fallback frame is not content
        last_authored_camera_layer=last_layer_submitted && authored_camera && !authored_scene_render && !settings_.open(); // gameplay's authored camera; a cutscene counts apart
        if(last_authored_camera_layer) ++authored_camera_layers;
        rendered_mask_=0;
        if(real_layers) {++submitted;if(!world_scene) ++menu_layers;}else ++dropped;
    }
};
} // namespace d2vr
