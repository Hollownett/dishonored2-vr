#pragma once
// In-game frame bridge. Uses the game's existing D3D11 device; never clears its
// pipeline state or changes the registered runtime. Frame calls stay on Present.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "context_state.hpp"
#include "controller_input.hpp"
#include "render_size.hpp"
#include "canted_fov.hpp"
#include "menu_panel.hpp"
#include "xr_continuity.hpp"
#include "eye_antialiasing.hpp"
#include "eye_sharpen.hpp"
#include "desktop_mirror.hpp"
#include "settings_panel_image.hpp"
#include "settings_menu_layout.hpp"
#include "vr_preferences.hpp"
#include "menu_input.hpp"
#include "menu_pointer.hpp"
#include "controls_hint_image.hpp"
#include "grab_beam.hpp"
#include "world_ui_pose.hpp"
#include "hud_window.hpp"
#include "wrist_hud_image.hpp"
#include "native_eye_art.hpp"
#include "head_calibration.hpp"
#include "xr_floor_datum.hpp"
#include "vr_settings_native.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace d2vr {
// Object-art destination for in-game tutorials: not object-anchored, so it is drawn on a
// panel locked ~1.2 m ahead of the head, slightly below eye level (publisher point ignored).
inline constexpr unsigned tutorial_art_slot=10;
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
    std::unique_ptr<DesktopMirror> mirror_;
    VrSettings settings_;
    AvatarCalibration avatar_calibration_;
    std::uint64_t avatar_prompt_reference_epoch_{};
    std::optional<NativeAvatarMeasurements> avatar_measurements_;
    bool settings_enabled_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> settings_texture_;
    std::unique_ptr<DesktopMirror> settings_compositor_;
    bool native_pointer_enabled_{};
public:
    bool wheel_native_pointer=true; // build/wheel-no-pointer: the wheel selects by stick only (pointer events switch it to keyboard glyphs)
private:
    XrSwapchain pointer_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> pointer_images_;
    XrSwapchain hint_chain_{};
    XrSwapchain dot_chain_{};std::vector<XrSwapchainImageD3D11KHR> dot_images_;std::vector<bool> dot_filled_;
    std::optional<Vec3> wheel_cursor_;
    bool cylinder_supported_{};XrCompositionLayerCylinderKHR hud_cylinder_{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    bool world_ui_stale_{},hud_window_stale_{};std::uint64_t world_ui_fresh_tick_{},hud_window_fresh_tick_{};
    std::array<XrSwapchain,2> wrist_chains_{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> wrist_images_;
    std::array<std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>>,2> wrist_targets_;
    std::array<bool,2> wrist_acquired_{};
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>,2> wrist_retirement_;
    std::array<bool,2> wrist_recreate_{};
    Microsoft::WRL::ComPtr<ID3D11Query> close_retirement_;
    std::array<UINT64,2> wrist_retry_ticks_{};
    std::array<UINT64,2> wrist_image_ticks_{};
    std::array<std::vector<std::uint8_t>,2> wrist_pixels_;
    WristHudSnapshot wrist_data_{};
    struct NativeArtSource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        D3D11_BOX crop{};Vec3 point{};double width{};
        UINT64 epoch{},tick{};std::uintptr_t owner{};bool premultiplied{true};
        bool fresh(UINT64 current_epoch,UINT64 now) const noexcept {return texture && owner && epoch==current_epoch && now>=tick && now-tick<=250;}
    };
    std::array<NativeArtSource,3> native_wrist_art_; // left ammo, right health, right mana
    std::array<std::array<UINT,2>,2> native_wrist_sizes_{};
    std::array<NativeArtSource,33> native_object_art_; // prompt, then bounded objective markers
    std::array<unsigned,33> native_object_art_masks_{};
    std::array<bool,3> native_wrist_art_shown_{};
    std::unique_ptr<NativeEyeArt> native_eye_art_;
    std::vector<XrSwapchainImageD3D11KHR> hint_images_;
    std::vector<bool> hint_filled_;
    std::vector<std::uint8_t> hint_pixels_;
    MenuTriggerGate menu_trigger_gate_;
    std::optional<double> resolution_change_;
    std::optional<TurnControl::Config> turning_change_;
    std::optional<int> temporal_change_,hud_change_;
    std::filesystem::path preferences_file_;
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
    UINT width_{},height_{};
    DXGI_FORMAT format_{};
    std::array<EyeSize,2> maximum_eye_sizes_{};
    EyeSize system_maximum_{};
    MenuPanel menu_panel_;
    std::uint64_t native_rows_tick_{}; // next publication of the native options page rows
    bool menu_grip_was_{};
    MenuPanel authored_panel_{2.5};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> world_ui_source_;
    unsigned world_ui_kind_{};bool world_ui_full_{};
    XrSwapchain world_ui_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> world_ui_images_;
    UINT world_ui_width_{},world_ui_height_{};
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
    enum InputAction : UINT {Grip,Aim,Stick,Trigger,Squeeze,FirstButton,SecondButton,StickClick,Menu,InputCount};
    XrActionSet action_set_{XR_NULL_HANDLE};
    std::array<XrAction,InputCount> actions_{};
    XrAction haptic_action_{XR_NULL_HANDLE};
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
        const auto pixels=settings_panel_image(settings_,effective_scale_,width_,height_,pointer);
        context_->UpdateSubresource(settings_texture_.Get(),0,nullptr,pixels.data(),640*4,0);
        if(observe_settings) observe_settings(settings_texture_.Get(),pixels.data(),pixels.size());
    }
    const TrackedPose& panel_pose(MenuPanel& panel) {
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        return panel.place({{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}});
    }
    const TrackedPose& native_menu_pose() {return panel_pose(menu_panel_);}
    // Wheel .8 m (fixed in front), notes and black market .45 m (held in the left hand), journal 1 m.
    // VR settings "HUD size" scales every UI element alike (65% = 1): world UI, prompts, markers, wrist, HUD screen.
public:
    // Notes, and the black market (the only full-height journal-kind panel: logic 1024) while the left hand is tracked, are held in
    // that hand like a page (headset 2026-10-14: "render the Black Market on the left arm like notes"). Untracked, the shop stays 1 m ahead.
    bool world_ui_in_hand() const noexcept {return (world_ui_kind_==2 || (world_ui_kind_==3 && world_ui_full_)) && (grip_tracked_hands&1u);}
    std::array<unsigned,2> world_ui_size() const {if(!world_ui_source_) return {};D3D11_TEXTURE2D_DESC d{};world_ui_source_->GetDesc(&d);return {d.Width,d.Height};}
    float hud_scale() const noexcept {return static_cast<float>(settings_.hud_percent())/65.f;}
    // Rows of the world UI image shown: the whole image (wheel, black market) or the centred 16:9 band.
    std::int32_t world_ui_rows(const D3D11_TEXTURE2D_DESC& d) const noexcept {
        return world_ui_full_?static_cast<std::int32_t>(d.Height):static_cast<std::int32_t>(std::clamp(std::lround(d.Width*menu_band),1l,static_cast<long>(d.Height)));
    }
    float world_ui_width() const noexcept {return (world_ui_kind_==1?.8f:world_ui_kind_==2 || world_ui_in_hand()?.45f:1.f)*std::min(hud_scale(),100.f/65.f);} // panels stop at the 100% size
private:
    const TrackedPose& world_ui_pose() {
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        const TrackedPose head{{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}};
        if(world_ui_in_hand()) { // notes and the black market follow the left hand every frame
            const auto& g=grip_locations[0].pose.position; // the taller shop page rests its bottom edge on the hand, as a note does (.12 m)
            world_ui_pose_=world_ui_hand_pose(head.position,{g.x,g.y,g.z},world_ui_kind_==2?.12:displayed_menu_view().height*.5);world_ui_pose_valid_=false;return world_ui_pose_;
        }
        if(world_ui_pose_valid_) return world_ui_pose_;
        world_ui_pose_=world_ui_front_pose(head,world_ui_kind_==1?.9:1.05);
        world_ui_pose_valid_=true;return world_ui_pose_;
    }
    XrCompositionLayerQuad world_ui_layer() {
        D3D11_TEXTURE2D_DESC desc{};world_ui_source_->GetDesc(&desc);
        if(!world_ui_chain_ || world_ui_width_!=desc.Width || world_ui_height_!=desc.Height) {
            if(world_ui_chain_) drain_gpu_for_surface();
            world_ui_images_.clear();if(world_ui_chain_) checked(xrDestroySwapchain(world_ui_chain_),"Destroy old world UI surface");
            world_ui_chain_=XR_NULL_HANDLE;
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;info.format=format_;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.width=desc.Width;info.height=desc.Height;info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&world_ui_chain_),"Create world UI surface");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(world_ui_chain_,0,&count,nullptr),"Count world UI images");
            world_ui_images_.resize(count);for(auto& i:world_ui_images_) i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(world_ui_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(world_ui_images_.data())),"Read world UI images");
            world_ui_width_=desc.Width;world_ui_height_=desc.Height;
        }
        if(!world_ui_stale_) { // stale: the quad re-shows the last released image
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(world_ui_chain_,&acquire,&index),"Acquire world UI image");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(world_ui_chain_,&wait);checked(ready,"Wait world UI image");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("World UI image wait timed out");
        context_->CopyResource(world_ui_images_.at(index).texture,world_ui_source_.Get());
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        checked(xrReleaseSwapchainImage(world_ui_chain_,&release),"Release world UI image");
        }
        const auto& pose=world_ui_pose();
        // The native selector lays potion shortcuts and controls at the top
        // and bottom of the actual movie image, outside the pause-menu band.
        const int rows=world_ui_rows(desc);
        const float width=world_ui_width();
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
        layer.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
        layer.size={width,width*rows/desc.Width};layer.subImage.swapchain=world_ui_chain_;
        layer.subImage.imageRect.offset={0,static_cast<int>((desc.Height-rows)/2)};layer.subImage.imageRect.extent={static_cast<int>(desc.Width),rows};return layer;
    }
    XrCompositionLayerQuad hud_window_layer() {
        D3D11_TEXTURE2D_DESC desc{};hud_window_source_->GetDesc(&desc);
        if(!hud_window_chain_ || hud_window_width_!=desc.Width || hud_window_height_!=desc.Height) {
            if(hud_window_chain_) drain_gpu_for_surface();
            hud_window_images_.clear();if(hud_window_chain_) checked(xrDestroySwapchain(hud_window_chain_),"Destroy old HUD window surface");
            hud_window_chain_=XR_NULL_HANDLE;
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;info.format=format_;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.width=desc.Width;info.height=desc.Height;info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&hud_window_chain_),"Create HUD window surface");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(hud_window_chain_,0,&count,nullptr),"Count HUD window images");
            hud_window_images_.resize(count);for(auto& i:hud_window_images_) i.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(hud_window_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(hud_window_images_.data())),"Read HUD window images");
            hud_window_width_=desc.Width;hud_window_height_=desc.Height;
        }
        if(!hud_window_stale_) { // stale: re-show the last released image
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(hud_window_chain_,&acquire,&index),"Acquire HUD window image");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(hud_window_chain_,&wait);checked(ready,"Wait HUD window image");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("HUD window image wait timed out");
        context_->CopyResource(hud_window_images_.at(index).texture,hud_window_source_.Get());
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        checked(xrReleaseSwapchainImage(hud_window_chain_,&release),"Release HUD window image");
        }
        const auto now=std::chrono::steady_clock::now();
        const double dt=hud_window_time_.time_since_epoch().count()?std::chrono::duration<double>(now-hud_window_time_).count():0;
        hud_window_time_=now;
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        const auto pose=hud_window_.update({{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}},dt,hud_window_config_);
        // Crop to the relaid content (hud_percent scale about the stage centre) and size it to ~38 deg wide.
        // The whole 16:9 stage (fitted by width into the texture) on one screen. Width in degrees: at most
        // 58% of this headset's horizontal FOV and 60 deg, scaled by the HUD size setting (55-100%).
        const double hfov_deg=(views_[0].fov.angleRight-views_[0].fov.angleLeft)*57.29577951308232;
        const double window_deg=hud_screen_degrees(hfov_deg,settings_.hud_percent());
        const auto crop=hud_window_crop(desc.Width,desc.Height,1.,hud_window_config_.distance,window_deg);
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
        layer.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
        layer.size={static_cast<float>(crop.width_m),static_cast<float>(crop.height_m)};layer.subImage.swapchain=hud_window_chain_;
        layer.subImage.imageRect.offset={crop.x,crop.y};layer.subImage.imageRect.extent={crop.w,crop.h};
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
    XrCompositionLayerQuad small_quad(XrSwapchain& chain,std::vector<XrSwapchainImageD3D11KHR>& images,
        const std::array<std::uint8_t,32*32*4>& pixels,const TrackedPose& p,float size) {
        if(!chain) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=32;info.height=32;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&chain),"Create small quad");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(chain,0,&count,nullptr),"Count small quad images");
            images.resize(count);for(auto& image:images) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Read small quad images");
            if(images.empty()) throw std::runtime_error("XR small quad has no images");
        }
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
        const auto rows=static_cast<std::int32_t>(std::clamp(std::lround(width_*menu_band),1l,static_cast<long>(height_)));
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
        checked(xrDestroySwapchain(wrist_chains_[side]),"Destroy retired wrist HUD");
        wrist_chains_[side]=XR_NULL_HANDLE;wrist_targets_[side].clear();wrist_images_[side].clear();
        native_wrist_sizes_[side]={};wrist_retirement_[side].Reset();wrist_recreate_[side]=false;return true;
    }
    std::optional<XrCompositionLayerQuad> wrist_layer(unsigned side) {
        // Optional layers must never stall the scene while the compositor is
        // busy. A timeout retains acquisition until a later successful wait.
        if(!release_pending_wrist(side)) {++wrist_timeouts;return {};}
        if(wrist_recreate_[side] && !retire_wrist(side)) return {};
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
        if(chain && (native_wrist_sizes_[side][0]<width || native_wrist_sizes_[side][1]<height)) {
            wrist_recreate_[side]=true;if(!retire_wrist(side)) return {};
        }
        if(!chain) {
            // Grow in blocks; animated native bounds do not recreate a chain
            // for every one-pixel change, and shrinking can reuse the canvas.
            const auto canvas_width=(width+63u)&~63u,canvas_height=(height+63u)&~63u;
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=format_;info.width=canvas_width;info.height=canvas_height;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&chain),"Create wrist HUD");
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
            native_wrist_sizes_[side]={canvas_width,canvas_height};
        }
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
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        if(!art.premultiplied) layer.layerFlags|=XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(contained->orientation.x),static_cast<float>(contained->orientation.y),static_cast<float>(contained->orientation.z),static_cast<float>(contained->orientation.w)};
        layer.pose.position={static_cast<float>(contained->position.x),static_cast<float>(contained->position.y),static_cast<float>(contained->position.z)};
        layer.subImage.swapchain=chain;layer.subImage.imageRect.extent={static_cast<int32_t>(width),static_cast<int32_t>(height)};
        native_wrist_art_shown_[static_cast<unsigned>(primary-native_wrist_art_.data())]=true;
        if(mana)native_wrist_art_shown_[2]=true;
        layer.size={panel_width,panel_height};
        return layer;
    }
    std::optional<XrCompositionLayerQuad> optional_wrist_layer(unsigned side) {
        if(GetTickCount64()<wrist_retry_ticks_[side]) return {};
        try {return wrist_layer(side);}
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
    // Wrist controls card: shown above the left controller while it is raised in front of the face
    // with its side turned toward the eyes (a watch glance), always facing the head.
    std::optional<XrCompositionLayerQuad> hint_layer() {
        constexpr XrSpaceLocationFlags valid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        const auto& grip=grip_locations[0];
        if(!views_valid_ || (grip.locationFlags&valid)!=valid) return {};
        const Vec3 hand{grip.pose.position.x,grip.pose.position.y,grip.pose.position.z};
        const Vec3 head{(views_[0].pose.position.x+views_[1].pose.position.x)*.5,(views_[0].pose.position.y+views_[1].pose.position.y)*.5,
            (views_[0].pose.position.z+views_[1].pose.position.z)*.5};
        const auto to_head=head-hand;const auto distance=length(to_head);
        if(distance<.15 || distance>.65 || hand.y<head.y-.45) return {};
        const auto side=rotated(normalized(Quaternion{grip.pose.orientation.x,grip.pose.orientation.y,grip.pose.orientation.z,grip.pose.orientation.w}),{1,0,0});
        if(dot(side,to_head*(1/distance))<.6) return {}; // palm (+X of the left grip) toward the eyes; the back of the wrist is the health watch
        constexpr unsigned width=512,height=384;
        if(!hint_chain_) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=width;info.height=height;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&hint_chain_),"Create controls card");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(hint_chain_,0,&count,nullptr),"Count controls card images");
            hint_images_.resize(count);for(auto& image:hint_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(hint_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(hint_images_.data())),"Read controls card images");
            if(hint_images_.empty()) throw std::runtime_error("XR controls card has no images");
            hint_filled_.assign(count,false);hint_pixels_=controls_hint_image(width,height);
        }
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(hint_chain_,&acquire,&index),"Acquire controls card");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(hint_chain_,&wait);checked(ready,"Wait controls card");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Controls card wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            // The card never changes: fill each runtime image once.
            if(!hint_filled_.at(index)) {
                context_->UpdateSubresource(hint_images_.at(index).texture,0,nullptr,hint_pixels_.data(),width*4,0);hint_filled_[index]=true;
            }
        } catch(...) {xrReleaseSwapchainImage(hint_chain_,&release);throw;}
        checked(xrReleaseSwapchainImage(hint_chain_,&release),"Release controls card");
        const auto& h=views_[0].pose.orientation;
        const auto facing=normalized(Quaternion{h.x,h.y,h.z,h.w});
        const auto centre=hand+rotated(facing,{0,.11,0});
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(facing.x),static_cast<float>(facing.y),static_cast<float>(facing.z),static_cast<float>(facing.w)};
        layer.pose.position={static_cast<float>(centre.x),static_cast<float>(centre.y),static_cast<float>(centre.z)};
        layer.size={.2f,.15f};layer.subImage.swapchain=hint_chain_;layer.subImage.imageRect.extent={static_cast<int32_t>(width),static_cast<int32_t>(height)};
        return layer;
    }
    // Wheel pointer dot: a ~3 cm quad at the hand-ray hit, facing the head, bright disc with a dark rim.
    std::optional<XrCompositionLayerQuad> dot_layer() {
        if(!wheel_cursor_ || !views_valid_) return {};
        constexpr unsigned size=64;
        if(!dot_chain_) {
            XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=info.height=size;
            info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
            checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&dot_chain_),"Create wheel dot");
            auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
            checked(enumerate(dot_chain_,0,&count,nullptr),"Count wheel dot images");
            dot_images_.resize(count);for(auto& image:dot_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            checked(enumerate(dot_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(dot_images_.data())),"Read wheel dot images");
            if(dot_images_.empty()) throw std::runtime_error("XR wheel dot has no images");
            dot_filled_.assign(count,false);
        }
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
        const auto& h=views_[0].pose.orientation;
        XrCompositionLayerQuad layer{};layer.type=XR_TYPE_COMPOSITION_LAYER_QUAD;layer.space=local_;
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;layer.pose.orientation=h;
        layer.pose.position={static_cast<float>(wheel_cursor_->x),static_cast<float>(wheel_cursor_->y),static_cast<float>(wheel_cursor_->z)};
        layer.size={.03f,.03f};layer.subImage.swapchain=dot_chain_;layer.subImage.imageRect.extent={size,size};
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
        XrInteractionProfileSuggestedBinding suggest{};suggest.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggest.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
        suggest.countSuggestedBindings=static_cast<uint32_t>(bindings.size());suggest.suggestedBindings=bindings.data();
        checked(xrSuggestInteractionProfileBindings(instance_,&suggest),"Bind Quest controls");
        XrSessionActionSetsAttachInfo attach{};attach.type=XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attach.countActionSets=1;attach.actionSets=&action_set_;
        checked(xrAttachSessionActionSets(session_,&attach),"Attach game controls");
        for(UINT kind=0;kind<2;++kind) for(UINT side=0;side<2;++side) {
            XrActionSpaceCreateInfo space{};space.type=XR_TYPE_ACTION_SPACE_CREATE_INFO;
            space.action=actions_[kind];space.subactionPath=hand_paths_[side];space.poseInActionSpace.orientation.w=1;
            checked(xrCreateActionSpace(session_,&space,&hand_spaces_[kind][side]),"Create controller space");
        }
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
        if(controller_input.active) ++controller_samples;
    }
public:
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
    TransferObserver observe_transfer{};
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
    std::array<UINT64,2> wrist_layers{};
    UINT64 wrist_failures{},wrist_timeouts{},wrist_target_creations{},wrist_retirement_waits{};
    HRESULT wrist_failure_hresult{S_OK},wrist_device_removed{S_OK};
    D3D11_TEXTURE2D_DESC wrist_failure_texture{};
    std::string wrist_failure_reason;
    void (*observe_wrist_failure)(unsigned,const char*){};
    void (*observe_wrist)(unsigned,const std::uint8_t*,std::size_t,const WristHudSnapshot&){};
    void wrist_hud(WristHudSnapshot data) {wrist_data_=data;}
    void (*observe_native_wrist_art)(unsigned,ID3D11Texture2D*,std::uintptr_t){};
    UINT64 native_object_art_draws{};
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
    std::array<std::optional<Vec3>,2> interaction_targets;
    void interaction_target(int side,std::optional<Vec3> point) noexcept {if(side==0 || side==1) interaction_targets[static_cast<unsigned>(side)]=point;}
    MirrorMode mirror_mode() const noexcept {return settings_.mirror();}
    double sharpness() const noexcept {return settings_.sharpness();}
    UINT64 mirrored_frames{};
    UINT64 antialiased_eyes() const noexcept {return antialiasing_?antialiasing_->eye_passes:0;}
    bool settings_open() const noexcept {return settings_.open();}
    std::atomic<bool> settings_requested{false}; // native menu "VR Settings" item (native_menu_entry.hpp)
    void avatar_measurements(NativeAvatarMeasurements value) {
        avatar_measurements_=value;
        // Keep the player's body across reloads/avatar/reference changes: refit without a new pose.
        if(avatar_calibration_.restore(reference_epoch(),value)) settings_.calibration_status(avatar_calibration_.state());
    }
    void avatar_runtime_status(AvatarFitRuntimeStatus status) {settings_.calibration_runtime(status);}
    AvatarFitRuntimeStatus avatar_runtime_status() const {return settings_.calibration_runtime();}
    UINT64 grip_pose_tick() const {return grip_pose_tick_;}
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
    UINT64 settings_layers{};
    UINT64 world_ui_layers{};
    std::array<UINT64,3> world_ui_layers_by_kind{};
    bool last_world_ui_layer{};
    std::optional<double> take_resolution_request() noexcept {const auto request=resolution_change_;resolution_change_.reset();return request;}
    std::optional<int> take_hud_request() noexcept {const auto request=hud_change_;hud_change_.reset();return request;}
    std::optional<int> take_temporal_request() noexcept {const auto request=temporal_change_;temporal_change_.reset();return request;}
    std::optional<TurnControl::Config> take_turning_request() noexcept {const auto request=turning_change_;turning_change_.reset();return request;}
    void resolution_status(double effective) noexcept {effective_scale_=effective;}
    UINT64 submitted{},dropped{},menu_layers{};
    UINT64 controller_samples{};
    UINT64 authored_camera_layers{};
    bool last_should_render{},last_views_valid{},last_tracked_pair{};
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
    // Latest completed HUD window surface (or null to hide it); set each frame by the producer.
    // Same hold as world_ui: a frame without a fresh panel capture re-shows the last image (up to 400 ms)
    // and keeps the window's placement, instead of blinking and re-placing the window.
    void hud_window(ID3D11Texture2D* source) {
        const auto now=GetTickCount64();
        if(!source && hud_window_source_ && hud_window_chain_ && now-hud_window_fresh_tick_<400) {hud_window_stale_=true;return;}
        if(!source) hud_window_.reset();
        hud_window_source_=source;hud_window_stale_=false;if(source) hud_window_fresh_tick_=now;
    }
    UINT64 hud_window_layers{};
    // A frame without a completed capture re-shows the last released image (up to 400 ms) instead of
    // dropping the layer: dropping it made the open wheel blink while its art updated.
    // full_height: the movie lays controls at its top/bottom edges, outside the 16:9 band (the black market, sim v381: its
    // category bar, money and Purchase/Back rows sat at 78-96% of the height and were cut off).
    void world_ui(ID3D11Texture2D* source,unsigned kind,bool full_height=false) {
        if(kind>3) kind=0;world_ui_full_=kind==1 || full_height;
        if(!kind) {world_ui_kind_=0;world_ui_source_.Reset();world_ui_pose_valid_=false;world_ui_stale_=false;return;}
        const auto now=GetTickCount64();
        if(!source && kind==world_ui_kind_ && world_ui_source_ && world_ui_chain_ && now-world_ui_fresh_tick_<400) {world_ui_stale_=true;return;}
        if(kind!=world_ui_kind_) world_ui_pose_valid_=false;
        world_ui_kind_=kind;world_ui_source_=source;world_ui_stale_=false;if(source) world_ui_fresh_tick_=now;
    }
    ControllerInput controller_input{};
    NativeMenuInput native_menu_input{};
    bool last_pointer_layer{};
    UINT64 pointer_layers{};
    std::array<XrSpaceLocation,2> grip_locations{},aim_locations{};
    // Wheel pointer dot (local space point, or none): drawn for the next submitted frame only; re-set each frame.
    void wheel_cursor(std::optional<Vec3> point) {wheel_cursor_=point;}
    bool test_unlock_requested=false; // VR settings "Test: unlock all"; dxgi applies and clears it
    bool cylinder_layers() const noexcept {return cylinder_supported_;} // curved HUD screen in use
    int menu_pointer_hand=1; // native Iggy pointer hand: right, or the wheel cursor hand while the wheel is open
    std::array<float,2> grip_speeds{}; // Metres per second, when the runtime reports velocity.
    unsigned grip_velocity_hands{}; // Missing velocity is not a stationary hand.
    std::array<XrVector3f,2> grip_velocities{}; // Same, as vectors in the local space.
    std::array<XrExtent2Di,2> recommended_eye_sizes{},swapchain_eye_sizes{};
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
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    bool exit_requested=false;
    GameXr(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,const wchar_t* loader,bool controllers=false,bool tracked_gameplay=false,bool edge_antialiasing=false,bool settings=false,double multiplier=1,
        TurnControl::Config turning={},std::filesystem::path preferences_file={},bool native_pointer=false,PFN_xrGetInstanceProcAddr simulated_dispatch=nullptr,
        MirrorMode mirror=MirrorMode::both_eyes,double sharpness=.5,int temporal_aa=2,int hud_percent=65,bool controls_hint=true,AvatarPlayerMeasurements player_measurements={})
        :graphics_state_(device),settings_enabled_(settings),native_pointer_enabled_(native_pointer),controller_enabled_(controllers),tracked_gameplay_required_(tracked_gameplay) {
        ContextState::Scope graphics_scope(graphics_state_);
        try {
            if(edge_antialiasing) antialiasing_=std::make_unique<EyeAntialiasing>(device);
            antialiasing_enabled_=edge_antialiasing;settings_.initialize(multiplier,edge_antialiasing,turning,mirror,sharpness,temporal_aa,hud_percent,controls_hint,player_measurements);
            preferences_file_=std::move(preferences_file);
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
                        for(const auto& prop:props) cylinder_supported_=cylinder_supported_ || !std::strcmp(prop.extensionName,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
                }
            } catch(...) {cylinder_supported_=false;}
            const std::array<const char*,2> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME};
            XrInstanceCreateInfo create{};create.type=XR_TYPE_INSTANCE_CREATE_INFO;
            strcpy_s(create.applicationInfo.applicationName,"Dishonored 2 game-image preview");
            create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
            create.enabledExtensionCount=cylinder_supported_?2:1;create.enabledExtensionNames=extensions.data();
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
            device->GetImmediateContext(&context_);
        } catch(...) {close();throw;}
    }
    GameXr(const GameXr&)=delete;
    GameXr& operator=(const GameXr&)=delete;
    ~GameXr() {close();}
    // Called only on the game's Present thread, between XR frames. Replace both
    // eye chains together; a failed allocation leaves the old pair intact.
    bool resize(UINT width,UINT height,DXGI_FORMAT format) {
        if(width==width_ && height==height_ && format==format_) return false;
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
                checked(xrDestroySwapchain(chain),"Destroy replaced eye swapchain");chain=XR_NULL_HANDLE;
            }
        } catch(...) {for(auto chain:next) if(chain) xrDestroySwapchain(chain);throw;}
        chains_=next;images_=std::move(images);width_=width;height_=height;format_=format;rendered_mask_=0;
        for(UINT eye=0;eye<2;++eye) {
            D3D11_TEXTURE2D_DESC actual{};images_[eye][0].texture->GetDesc(&actual);eye_texture_formats[eye]=actual.Format;
        }
        for(auto& size:swapchain_eye_sizes) size={static_cast<int32_t>(width),static_cast<int32_t>(height)};
        return true;
    }
    const std::array<XrView,2>* tracking_views() const {
        return running_ && views_valid_ && continuity_.usable()?&views_:nullptr;
    }
    void record_rendered_eye(UINT eye,XrFovf rendered_fov,const XrQuaternionf* rendered_orientation=nullptr) {
        rendered_views_[eye]=views_[eye];rendered_views_[eye].fov=rendered_fov;rendered_mask_|=1u<<eye;
        if(rendered_orientation) rendered_views_[eye].pose.orientation=*rendered_orientation; // canted eye
        rendered_display_times_[eye]=locate_display_time_;
        rendered_locate_cpu_times_[eye]=locate_cpu_time_;
    }
    bool ready_to_close() {return !session_ || gpu_retirement_ready(close_retirement_);}
    void close() noexcept {
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
        context_.Reset();
        settings_texture_.Reset();settings_compositor_.reset();
        world_ui_source_.Reset();world_ui_images_.clear();
        hud_window_source_.Reset();hud_window_images_.clear();
        if(hud_window_chain_ && xrDestroySwapchain) xrDestroySwapchain(hud_window_chain_);
        hud_window_chain_=XR_NULL_HANDLE;hud_window_width_=hud_window_height_=0;hud_window_.reset();
        if(world_ui_chain_ && xrDestroySwapchain) xrDestroySwapchain(world_ui_chain_);
        world_ui_chain_=XR_NULL_HANDLE;world_ui_width_=world_ui_height_=0;world_ui_kind_=0;world_ui_pose_valid_=false;
        pointer_images_.clear();
        if(pointer_chain_ && xrDestroySwapchain) {xrDestroySwapchain(pointer_chain_);pointer_chain_=XR_NULL_HANDLE;}
        hint_images_.clear();hint_filled_.clear();
        for(unsigned side=0;side<2;++side) {
            wrist_targets_[side].clear();
            wrist_images_[side].clear();wrist_pixels_[side].clear();wrist_image_ticks_[side]=0;
            if(wrist_chains_[side] && xrDestroySwapchain) xrDestroySwapchain(wrist_chains_[side]);
            wrist_chains_[side]=XR_NULL_HANDLE;
            wrist_acquired_[side]=false;native_wrist_sizes_[side]={};wrist_retry_ticks_[side]=0;
            wrist_retirement_[side].Reset();wrist_recreate_[side]=false;
        }
        if(hint_chain_ && xrDestroySwapchain) {xrDestroySwapchain(hint_chain_);hint_chain_=XR_NULL_HANDLE;}
        dot_images_.clear();dot_filled_.clear();wheel_cursor_.reset();
        if(dot_chain_ && xrDestroySwapchain) {xrDestroySwapchain(dot_chain_);dot_chain_=XR_NULL_HANDLE;}
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
    void present(ID3D11Texture2D* left,ID3D11Texture2D* right,float hfov,float vfov,bool gameplay=true,bool authored_camera=false,bool settings_available=false,
        ID3D11Texture2D* desktop=nullptr,std::array<UINT,2> desktop_view={},bool cinematic=false) {
        const bool world_scene=gameplay || world_ui_kind_!=0;
        last_world_ui_layer=false;
        timing={};timing.start_ns=cpu_time_ns();
        if(rendered_mask_==3) {
            timing.rendered_display_times=rendered_display_times_;
            timing.rendered_locate_cpu_times=rendered_locate_cpu_times_;
        }
        ContextState::Scope graphics_scope(graphics_state_);
        last_should_render=false;last_views_valid=false;last_tracked_pair=false;
        native_menu_input={};
        last_pointer_layer=false;
        last_layer_submitted=false;last_menu_layer=false;last_authored_camera_layer=false;
        native_object_art_masks_={};native_wrist_art_shown_={};
        authored_camera=gameplay && authored_camera;
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
                    settings_.suspend();
                }
                if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) exit_requested=true;
                if(state==XR_SESSION_STATE_READY && !running_) {
                    XrSessionBeginInfo begin{};begin.type=XR_TYPE_SESSION_BEGIN_INFO;begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    checked(xrBeginSession(session_,&begin),"Begin game XR session");running_=true;
                } else if(state==XR_SESSION_STATE_STOPPING) {
                    if(running_) checked(xrEndSession(session_),"End game XR session");running_=false;
                }
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
        XrFrameWaitInfo wait{};wait.type=XR_TYPE_FRAME_WAIT_INFO;frame_={};frame_.type=XR_TYPE_FRAME_STATE;
        auto stage_start=cpu_time_ns();
        checked(xrWaitFrame(session_,&wait,&frame_),"Wait game XR frame");
        timing.waited=true;timing.wait_ns=cpu_time_ns()-stage_start;
        timing.display_time=frame_.predictedDisplayTime;timing.display_period=frame_.predictedDisplayPeriod;
        XrFrameBeginInfo begin{};begin.type=XR_TYPE_FRAME_BEGIN_INFO;
        stage_start=cpu_time_ns();
        checked(xrBeginFrame(session_,&begin),"Begin game XR frame");frame_open_=true;
        timing.begin_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        XrViewLocateInfo locate{};locate.type=XR_TYPE_VIEW_LOCATE_INFO;locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        // Native eyes render after this Present and are submitted at the next
        // Present. Predict that pipeline stage, using the runtime's own period.
        locate_display_time_=frame_.predictedDisplayTime+frame_.predictedDisplayPeriod;
        locate.displayTime=locate_display_time_;locate.space=local_;
        XrViewState view_state{};view_state.type=XR_TYPE_VIEW_STATE;
        for(auto& view:views_) {view={};view.type=XR_TYPE_VIEW;}
        uint32_t count{};checked(xrLocateViews(session_,&locate,&view_state,2,&count,views_.data()),"Locate game XR views");
        located_view_flags_=view_state.viewStateFlags;
        locate_cpu_time_=cpu_time_ns();
        constexpr XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        views_valid_=count==2 && (view_state.viewStateFlags&valid)==valid;
        last_should_render=frame_.shouldRender!=XR_FALSE;last_views_valid=views_valid_;
        if(continuity_.advance(state==XR_SESSION_STATE_VISIBLE || state==XR_SESSION_STATE_FOCUSED,
            last_should_render,views_valid_,frame_.predictedDisplayTime)) {
            menu_panel_.reset();authored_panel_.reset();world_ui_pose_valid_=false;rendered_mask_=0;
        }
        last_tracked_pair=rendered_mask_==3;
        sample_input();
        // Preserve squeeze actions before the paused overlay consumes controls.
        const bool raw_both_grips_held=controller_input.left_grip>=.8f && controller_input.right_grip>=.8f;
        // In menus a left-grip squeeze re-places the menu where the user is looking.
        const bool menu_grip=controller_input.left_grip>=.6f;
        if(menu_grip && !menu_grip_was_ && (!gameplay || settings_.open()) && controller_input.right_grip<.5f) {
            menu_panel_.summon();
        }
        menu_grip_was_=menu_grip;
        const float raw_right_trigger=controller_input.right_trigger;
        const bool raw_input_active=controller_input.active;
        bool settings_consumed=false;
        std::optional<PanelPointer> pointer;
        // The game's own options page shows these rows (also without saved settings: then changes last for this session).
        {const auto now_ms=GetTickCount64();if(now_ms>=native_rows_tick_) {native_rows_tick_=now_ms+250;NativeOptionsPage::publish(native_settings::rows(settings_));}}
        if(settings_enabled_) {
            if(settings_requested.exchange(false) && !gameplay) {settings_.open_from_menu();}
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
            auto changes=settings_.update(controller_input,gameplay,state==XR_SESSION_STATE_FOCUSED,pointer,settings_available);
            // The game's own options page ("VR Settings"): its values and actions take the panel's path (apply, save, calibrate).
            if(const auto values=NativeOptionsPage::take_values()) native_settings::merge(changes,native_settings::apply(settings_,*values));
            for(const int row:NativeOptionsPage::take_actions()) native_settings::merge(changes,native_settings::action(settings_,row));
            const auto old_calibration_state=avatar_calibration_.state();
            const auto calibration_state=avatar_calibration_.synchronize(reference_epoch(),
                settings_.open() && settings_.calibration_page() && !settings_.measurements_page() && !gameplay && state==XR_SESSION_STATE_FOCUSED);
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
            if(settings_.open() && !gameplay && settings_.calibration_page() && !settings_.measurements_page() && avatar_measurements_) {
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
            if(changes.temporal_aa) temporal_change_=changes.temporal_aa;
            if(changes.hud_percent) hud_change_=changes.hud_percent;
            if(changes.recenter) {
                continuity_.recenter();rendered_mask_=0;menu_panel_.reset();world_ui_pose_valid_=false;
                settings_.calibration_status(avatar_calibration_.synchronize(reference_epoch(),false));
            }
            if(changes.antialiasing) {
                if(*changes.antialiasing && !antialiasing_) {
                    Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
                    antialiasing_=std::make_unique<EyeAntialiasing>(device.Get());
                }
                antialiasing_enabled_=*changes.antialiasing;
            }
            if(!preferences_file_.empty() && (changes.resolution || changes.antialiasing || changes.turning || changes.mirror || changes.sharpness || changes.temporal_aa || changes.hud_percent || changes.controls_hint || changes.player_measurements))
                save_preferences(preferences_file_,{settings_.requested(),settings_.antialiasing(),settings_.turning(),settings_.mirror(),settings_.sharpness(),settings_.temporal_aa(),settings_.hud_percent(),settings_.controls_hint(),settings_.player_measurements()});
            // Includes squeeze: holding the calibration pose must not open the native weapon selector.
            if(changes.consume) {controller_input={};settings_consumed=true;}
            if(changes.test_unlock) test_unlock_requested=true;
        }
        // Notes (2) get no pointer: pointer input switches the game to keyboard glyphs ("Esc Close") and a note has nothing to click.
        // The wheel keeps it: its sector selection runs through the native pointer (without it the wheel ignored the cursor).
        if(native_pointer_enabled_ && world_ui_kind_!=2 && (world_ui_kind_!=1 || wheel_native_pointer) && (world_ui_kind_==0 || world_ui_source_) && !gameplay && !cinematic && !settings_.open() && continuity_.usable() && state==XR_SESSION_STATE_FOCUSED && controller_input.active) {
            constexpr XrSpaceLocationFlags valid_pose=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            const auto& aim=aim_locations[menu_pointer_hand&1];
            if((aim.locationFlags&valid_pose)==valid_pose) {
                const auto& p=aim.pose;const auto view=displayed_menu_view();
                const auto hit=panel_pointer({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},world_ui_kind_?world_ui_pose():native_menu_pose(),view.width,view.height);
                // The panel shows the middle band; the game expects coordinates over the whole image.
                if(hit) native_menu_input={hit->u,view.v0+hit->v*view.vspan,GetTickCount64(),continuity_.epoch(),true,
                    ((menu_pointer_hand&1)?controller_input.right_trigger:controller_input.left_trigger)>.55f};
            }
        }
        controller_input.right_trigger=menu_trigger_gate_.update(raw_right_trigger,!gameplay || settings_.open() || settings_consumed,raw_input_active);
        timing.locate_input_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        XrCompositionLayerProjection projection{};projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        XrCompositionLayerQuad menu{};menu.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
        std::optional<XrCompositionLayerQuad> world_ui_panel,hud_window_panel;
        std::optional<XrCompositionLayerQuad> hint_panel,dot_panel;
        std::array<std::optional<XrCompositionLayerQuad>,2> wrist_panels;
        std::array<const XrCompositionLayerBaseHeader*,12> menu_layers_with_pointer{};
        projection.space=local_;projection.viewCount=2;
        std::array<XrCompositionLayerProjectionView,2> layers{};projection.views=layers.data();
        XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=frame_.predictedDisplayTime;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        if(gameplay) menu_panel_.reset();
        if(continuity_.usable() && left && right &&
           (!world_scene || !tracked_gameplay_required_ || rendered_mask_==3)) {
            std::array<ID3D11Texture2D*,2> sources{left,right};
            if(settings_.open()) {update_settings_image(pointer);++settings_layers;}
            const auto sharpness=static_cast<float>(settings_.sharpness());
            if(sharpness>0 && !sharpen_) {
                Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);sharpen_=std::make_unique<EyeSharpen>(device.Get());
            }
            std::array<std::optional<MirrorRect>,2> mirror_rects{};D3D11_TEXTURE2D_DESC desktop_desc{};
            if(gameplay && desktop) {
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
            for(UINT eye=0;eye<2;++eye) {
                XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
                checked(xrAcquireSwapchainImage(chains_[eye],&acquire,&index),"Acquire game eye");
                XrSwapchainImageWaitInfo image_wait{};image_wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;image_wait.timeout=1'000'000'000;
                auto ready=xrWaitSwapchainImage(chains_[eye],&image_wait);checked(ready,"Wait game eye");
                if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Game eye wait timed out");
                XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                try {
                D3D11_TEXTURE2D_DESC desc{};sources[eye]->GetDesc(&desc);
                if(!eye_texture_matches(desc,width_,height_,format_))
                    throw std::runtime_error("Game eye source does not match the current XR swapchain");
                // Independent per-eye processing; menus retain the exact native image.
                auto processed=gameplay && antialiasing_enabled_ && settings_.temporal_aa()<2?antialiasing_->apply(sources[eye]):sources[eye]; // FSR 3 already anti-aliased
                // Sharpening also helps menu text (menus get no SMAA).
                if(sharpness>0) processed=sharpen_->apply(processed,sharpness);
                if(settings_.open()) {
                    const auto view=menu_view();
                    const auto rect=SettingsMenuLayout::image_rect(width_,height_,view.y,view.rows);
                    settings_compositor_->begin(width_,height_,format_);
                    settings_compositor_->draw(processed,{0,0,static_cast<float>(width_),static_cast<float>(height_)});
                    settings_compositor_->draw(settings_texture_.Get(),{static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height)});
                    settings_compositor_->end(images_[eye].at(index).texture);
                    if(observe_settings_composition) observe_settings_composition(processed,images_[eye].at(index).texture,rect);
                } else context_->CopyResource(images_[eye].at(index).texture,processed);
                layers[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                layers[eye].pose=rendered_mask_==3?rendered_views_[eye].pose:views_[eye].pose;
                layers[eye].fov=rendered_mask_==3?rendered_views_[eye].fov:XrFovf{-hfov*.5f,hfov*.5f,vfov*.5f,-vfov*.5f};
                if(gameplay && !authored_camera && rendered_mask_==3) {
                    const auto now=GetTickCount64();XrView view{XR_TYPE_VIEW};view.pose=layers[eye].pose;view.fov=layers[eye].fov;
                    for(unsigned art_slot=0;art_slot<native_object_art_.size();++art_slot) if(const auto& art=native_object_art_[art_slot];art.fresh(pose_epoch(),now)) {
                        const auto aspect=static_cast<double>(art.crop.right-art.crop.left)/(art.crop.bottom-art.crop.top);
                        const auto locked=art_slot==tutorial_art_slot?head_locked_art_point(rendered_views_[0].pose,rendered_views_[1].pose):std::optional<Vec3>{};
                        if(art_slot==tutorial_art_slot && !locked)continue;
                        if(const auto rect=native_art_rect(view,locked?*locked:art.point,art.width*hud_scale(),aspect)) {
                            if(!native_eye_art_) {Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);native_eye_art_=std::make_unique<NativeEyeArt>(device.Get());}
                            native_eye_art_->draw(images_[eye].at(index).texture,art.texture.Get(),art.crop,*rect,format_,art.premultiplied);++native_object_art_draws;
                            native_object_art_masks_[art_slot]|=1u<<eye;
                        }
                    }
                }
                if(mirror_rects[eye]) mirror_->draw(images_[eye].at(index).texture,*mirror_rects[eye]);
                layers[eye].subImage.swapchain=chains_[eye];
                layers[eye].subImage.imageRect.extent={static_cast<int32_t>(width_),static_cast<int32_t>(height_)};
                    if(observe_transfer && !settings_.open()) observe_transfer(eye,processed,images_[eye].at(index).texture,layers[eye],gameplay,rendered_mask_==3,sources[eye]);
                } catch(...) {xrReleaseSwapchainImage(chains_[eye],&release);throw;}
                checked(xrReleaseSwapchainImage(chains_[eye],&release),"Release game eye");
            }
            if(mirror_rects[0] || mirror_rects[1]) {mirror_->end(desktop);++mirrored_frames;}
            if(!world_scene) {
                // Native menus use a pointer-enabled panel; cutscenes play on a larger, farther "TV".
                const auto& pose=cinematic?panel_pose(authored_panel_):native_menu_pose();
                const auto view=menu_view(cinematic);
                if(!authored_camera) last_menu_pose=pose;
                menu.space=local_;menu.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                menu.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),
                    static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
                menu.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
                menu.size={view.width,view.height};menu.subImage=layers[0].subImage;
                menu.subImage.imageRect.offset={0,view.y};menu.subImage.imageRect.extent={static_cast<int32_t>(width_),view.rows};
                layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu);
            }
            end.layerCount=1;end.layers=&layer;
            if(world_scene && !authored_camera) {
                // Projection first, then the wrist controls card.
                end.layerCount=0;menu_layers_with_pointer[end.layerCount++]=layer;
                if(world_ui_kind_ && world_ui_source_) {
                    world_ui_panel=world_ui_layer();menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*world_ui_panel);
                    ++world_ui_layers;++world_ui_layers_by_kind[world_ui_kind_-1];last_world_ui_layer=true;
                }
                if(gameplay && !world_ui_kind_ && hud_window_source_) {
                    hud_window_panel=hud_window_layer();
                    menu_layers_with_pointer[end.layerCount++]=cylinder_supported_?reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud_cylinder_):
                        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hud_window_panel);
                    ++hud_window_layers;
                }
                for(unsigned side:{1u,0u}) if(gameplay) { // health/mana first: the ammo screen copy sits below it
                    if((wrist_panels[side]=optional_wrist_layer(side))) {
                        menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*wrist_panels[side]);++wrist_layers[side];}
                }
                if(gameplay && !wrist_panels[0] && settings_.controls_hint() && (hint_panel=hint_layer())) {
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hint_panel);++hint_layers;
                }
                if((gameplay || world_ui_kind_==1) && wheel_cursor_ && (dot_panel=dot_layer())) // last: drawn over the wheel (open wheel is not gameplay)
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*dot_panel);
                end.layers=menu_layers_with_pointer.data();
            }
        }
        timing.transfer_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        checked(xrEndFrame(session_,&end),"Submit game XR frame");frame_open_=false;
        timing.submit_ns=cpu_time_ns()-stage_start;timing.end_ns=cpu_time_ns();
        last_layer_submitted=end.layerCount!=0;last_menu_layer=last_layer_submitted && !world_scene;
        last_authored_camera_layer=last_layer_submitted && authored_camera && !settings_.open();
        if(last_authored_camera_layer) ++authored_camera_layers;
        rendered_mask_=0;
        if(end.layerCount) {++submitted;if(!world_scene) ++menu_layers;}else ++dropped;
    }
};
} // namespace d2vr
