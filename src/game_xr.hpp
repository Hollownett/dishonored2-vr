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
#include "menu_panel.hpp"
#include "xr_continuity.hpp"
#include "eye_antialiasing.hpp"
#include "eye_sharpen.hpp"
#include "desktop_mirror.hpp"
#include "settings_panel_image.hpp"
#include "vr_preferences.hpp"
#include "menu_input.hpp"
#include "menu_pointer.hpp"
#include "controls_hint_image.hpp"
#include "head_calibration.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace d2vr {
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
    MenuPanel settings_placement_;
    bool settings_enabled_{};
    XrSwapchain settings_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> settings_images_;
    bool native_pointer_enabled_{};
    XrSwapchain pointer_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> pointer_images_;
    XrSwapchain hint_chain_{};
    std::vector<XrSwapchainImageD3D11KHR> hint_images_;
    std::vector<bool> hint_filled_;
    std::vector<std::uint8_t> hint_pixels_;
    MenuTriggerGate menu_trigger_gate_;
    std::optional<double> resolution_change_;
    std::optional<TurnControl::Config> turning_change_;
    std::optional<bool> temporal_change_;
    std::filesystem::path preferences_file_;
    double effective_scale_=1;
    HMODULE loader_{};
    XrInstance instance_{XR_NULL_HANDLE};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace local_{XR_NULL_HANDLE};
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
    bool menu_grip_was_{};
    MenuPanel authored_panel_;
    XrContinuity continuity_;
    bool running_=false,frame_open_=false,views_valid_=false;
    XrFrameState frame_{};
    std::array<XrView,2> views_{};
    std::array<XrView,2> rendered_views_{};
    std::array<XrTime,2> rendered_display_times_{};
    std::array<std::int64_t,2> rendered_locate_cpu_times_{};
    std::int64_t locate_cpu_time_{};
    XrTime locate_display_time_{};
    UINT rendered_mask_{};
    enum InputAction : UINT {Grip,Aim,Stick,Trigger,Squeeze,FirstButton,SecondButton,StickClick,Menu,InputCount};
    XrActionSet action_set_{XR_NULL_HANDLE};
    std::array<XrAction,InputCount> actions_{};
    XrAction haptic_action_{XR_NULL_HANDLE};
    std::array<XrPath,2> hand_paths_{};
    std::array<std::array<XrSpace,2>,2> hand_spaces_{};
    bool controller_enabled_{};
    bool tracked_gameplay_required_{};
    void create_settings_chain() {
        XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;info.width=640;info.height=560;
        info.sampleCount=1;info.faceCount=1;info.arraySize=1;info.mipCount=1;
        checked(api<PFN_xrCreateSwapchain>("xrCreateSwapchain",instance_)(session_,&info,&settings_chain_),"Create settings panel");
        auto enumerate=api<PFN_xrEnumerateSwapchainImages>("xrEnumerateSwapchainImages",instance_);uint32_t count{};
        checked(enumerate(settings_chain_,0,&count,nullptr),"Count settings images");
        settings_images_.resize(count);for(auto& image:settings_images_) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        checked(enumerate(settings_chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(settings_images_.data())),"Read settings images");
        if(settings_images_.empty()) throw std::runtime_error("XR settings panel has no images");
    }
    XrCompositionLayerQuad settings_layer(std::optional<PanelPointer> pointer) {
        if(!settings_chain_) create_settings_chain();
        const auto pixels=settings_panel_image(settings_,effective_scale_,width_,height_,pointer);
        XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;uint32_t index{};
        checked(xrAcquireSwapchainImage(settings_chain_,&acquire,&index),"Acquire settings panel");
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=1'000'000'000;
        const auto ready=xrWaitSwapchainImage(settings_chain_,&wait);checked(ready,"Wait settings panel");
        if(ready==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Settings panel wait timed out");
        XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        try {
            D3D11_TEXTURE2D_DESC desc{};settings_images_.at(index).texture->GetDesc(&desc);
            if(!eye_texture_matches(desc,640,560,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                throw std::runtime_error("XR settings panel image differs");
            context_->UpdateSubresource(settings_images_.at(index).texture,0,nullptr,pixels.data(),640*4,0);
            if(observe_settings) observe_settings(settings_images_.at(index).texture,pixels.data(),pixels.size());
        } catch(...) {xrReleaseSwapchainImage(settings_chain_,&release);throw;}
        checked(xrReleaseSwapchainImage(settings_chain_,&release),"Release settings panel");
        XrCompositionLayerQuad panel{};panel.type=XR_TYPE_COMPOSITION_LAYER_QUAD;panel.space=local_;
        panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        const auto& p=settings_pose();
        panel.pose.orientation={static_cast<float>(p.orientation.x),static_cast<float>(p.orientation.y),static_cast<float>(p.orientation.z),static_cast<float>(p.orientation.w)};
        panel.pose.position={static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z)};
        panel.size={1.28f,1.12f};panel.subImage.swapchain=settings_chain_;panel.subImage.imageRect.extent={640,560};return panel;
    }
    const TrackedPose& panel_pose(MenuPanel& panel) {
        const auto& l=views_[0].pose;const auto& r=views_[1].pose;
        return panel.place({{(l.position.x+r.position.x)*.5,(l.position.y+r.position.y)*.5,(l.position.z+r.position.z)*.5},
            {l.orientation.x,l.orientation.y,l.orientation.z,l.orientation.w}});
    }
    const TrackedPose& settings_pose() {return panel_pose(settings_placement_);}
    const TrackedPose& native_menu_pose() {return panel_pose(menu_panel_);}
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
        layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;layer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        layer.pose.orientation={static_cast<float>(p.orientation.x),static_cast<float>(p.orientation.y),static_cast<float>(p.orientation.z),static_cast<float>(p.orientation.w)};
        layer.pose.position={static_cast<float>(p.position.x),static_cast<float>(p.position.y),static_cast<float>(p.position.z)};
        layer.size={size,size};layer.subImage.swapchain=chain;layer.subImage.imageRect.extent={32,32};return layer;
    }
    XrCompositionLayerQuad pointer_layer() {
        const auto extent=MenuPanel::extent(width_,height_);
        const auto p=menu_pointer_pose(native_menu_pose(),{native_menu_input.u,native_menu_input.v},extent[0],extent[1]);
        return small_quad(pointer_chain_,pointer_images_,menu_pointer_image(native_menu_input.trigger),p,.02f);
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
        if(std::abs(dot(side,to_head*(1/distance)))<.6) return {};
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
    static void checked(XrResult value,const char* name) {
        if(XR_FAILED(value)) throw std::runtime_error(std::string(name)+": "+std::to_string(value));
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
        controller_input={};grip_locations={};aim_locations={};grip_speeds={};
        action_sync_attempted=false;last_action_sync=XR_SESSION_NOT_FOCUSED;active_input_hands=0;grip_tracked_hands=0;stick_active_hands=0;
        if(!controller_enabled_ || !continuity_.usable() || !views_valid_) return;
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
            if(!kind && pose.isActive && (velocity.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT))
                grip_speeds[side]=std::sqrt(velocity.linearVelocity.x*velocity.linearVelocity.x+
                    velocity.linearVelocity.y*velocity.linearVelocity.y+velocity.linearVelocity.z*velocity.linearVelocity.z);
        }
        constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        for(UINT side=0;side<2;++side) if((grip_locations[side].locationFlags&required)==required) grip_tracked_hands|=1u<<side;
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
        const auto& panel=native_menu_pose();const auto extent=MenuPanel::extent(width_,height_);
        auto ray=menu_pointer_pose(panel,{u,v},extent[0],extent[1]);
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
    // Short controller vibration; ignored when unfocused or unsupported.
    void pulse(unsigned side,float amplitude,float seconds) noexcept {
        if(!haptic_action_ || side>1 || state!=XR_SESSION_STATE_FOCUSED || !running_) return;
        XrHapticVibration vibration{};vibration.type=XR_TYPE_HAPTIC_VIBRATION;vibration.amplitude=amplitude;
        vibration.duration=static_cast<XrDuration>(seconds*1e9);vibration.frequency=XR_FREQUENCY_UNSPECIFIED;
        XrHapticActionInfo info{};info.type=XR_TYPE_HAPTIC_ACTION_INFO;info.action=haptic_action_;info.subactionPath=hand_paths_[side];
        xrApplyHapticFeedback(session_,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    }
    bool edge_antialiasing_enabled() const noexcept {return antialiasing_enabled_;}
    UINT64 hint_layers{};
    MirrorMode mirror_mode() const noexcept {return settings_.mirror();}
    double sharpness() const noexcept {return settings_.sharpness();}
    UINT64 mirrored_frames{};
    UINT64 antialiased_eyes() const noexcept {return antialiasing_?antialiasing_->eye_passes:0;}
    bool settings_open() const noexcept {return settings_.open();}
    void avatar_measurements(NativeAvatarMeasurements value) {avatar_measurements_=value;}
    void avatar_runtime_status(AvatarFitRuntimeStatus status) {settings_.calibration_runtime(status);}
    AvatarFitRuntimeStatus avatar_runtime_status() const {return settings_.calibration_runtime();}
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
    UINT64 settings_layers{};
    std::optional<double> take_resolution_request() noexcept {const auto request=resolution_change_;resolution_change_.reset();return request;}
    std::optional<bool> take_temporal_request() noexcept {const auto request=temporal_change_;temporal_change_.reset();return request;}
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
    ControllerInput controller_input{};
    NativeMenuInput native_menu_input{};
    bool last_pointer_layer{};
    UINT64 pointer_layers{};
    std::array<XrSpaceLocation,2> grip_locations{},aim_locations{};
    std::array<float,2> grip_speeds{}; // Metres per second, when the runtime reports velocity.
    std::array<XrExtent2Di,2> recommended_eye_sizes{},swapchain_eye_sizes{};
    std::array<DXGI_FORMAT,2> eye_texture_formats{};
    EyeSize recommended_output_size() const {
        std::array<EyeSize,2> sizes{};
        for(UINT eye=0;eye<2;++eye) sizes[eye]={static_cast<UINT>(recommended_eye_sizes[eye].width),
            static_cast<UINT>(recommended_eye_sizes[eye].height)};
        return recommended_output(sizes,maximum_eye_sizes_,system_maximum_);
    }
    EyeSize optical_output_size() const {
        if(!views_valid_) throw std::runtime_error("Optical output needs located XR views");
        std::array<EyeSize,2> sizes{};std::array<EyeFov,2> fovs{};
        for(UINT eye=0;eye<2;++eye) {
            sizes[eye]={static_cast<UINT>(recommended_eye_sizes[eye].width),static_cast<UINT>(recommended_eye_sizes[eye].height)};
            const auto& f=views_[eye].fov;fovs[eye]={f.angleLeft,f.angleRight,f.angleUp,f.angleDown};
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
        MirrorMode mirror=MirrorMode::both_eyes,double sharpness=.5,bool temporal_aa=false,bool aim_reticle=true,bool controls_hint=true)
        :graphics_state_(device),settings_enabled_(settings),native_pointer_enabled_(native_pointer),controller_enabled_(controllers),tracked_gameplay_required_(tracked_gameplay) {
        ContextState::Scope graphics_scope(graphics_state_);
        try {
            if(edge_antialiasing) antialiasing_=std::make_unique<EyeAntialiasing>(device);
            antialiasing_enabled_=edge_antialiasing;settings_.initialize(multiplier,edge_antialiasing,turning,mirror,sharpness,temporal_aa,aim_reticle,controls_hint);
            preferences_file_=std::move(preferences_file);
            get_=simulated_dispatch;
            if(!get_) {
                loader_=LoadLibraryExW(loader,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if(!loader_) throw std::runtime_error("Cannot load installed OpenXR loader");
                auto symbol=GetProcAddress(loader_,"xrGetInstanceProcAddr");
                if(!symbol) throw std::runtime_error("Missing XR loader export");
                static_assert(sizeof(symbol)==sizeof(get_));std::memcpy(&get_,&symbol,sizeof(get_));
            }
            const char* extension=XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
            XrInstanceCreateInfo create{};create.type=XR_TYPE_INSTANCE_CREATE_INFO;
            strcpy_s(create.applicationInfo.applicationName,"Dishonored 2 game-image preview");
            create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
            create.enabledExtensionCount=1;create.enabledExtensionNames=&extension;
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
    void record_rendered_eye(UINT eye,XrFovf rendered_fov) {
        rendered_views_[eye]=views_[eye];rendered_views_[eye].fov=rendered_fov;rendered_mask_|=1u<<eye;
        rendered_display_times_[eye]=locate_display_time_;
        rendered_locate_cpu_times_[eye]=locate_cpu_time_;
    }
    void close() noexcept {
        ContextState::Scope graphics_scope(graphics_state_);
        if(frame_open_ && xrEndFrame) {
            XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=frame_.predictedDisplayTime;
            end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;xrEndFrame(session_,&end);frame_open_=false;
        }
        context_.Reset();
        settings_images_.clear();
        if(settings_chain_ && xrDestroySwapchain) {xrDestroySwapchain(settings_chain_);settings_chain_=XR_NULL_HANDLE;}
        pointer_images_.clear();
        if(pointer_chain_ && xrDestroySwapchain) {xrDestroySwapchain(pointer_chain_);pointer_chain_=XR_NULL_HANDLE;}
        hint_images_.clear();hint_filled_.clear();
        if(hint_chain_ && xrDestroySwapchain) {xrDestroySwapchain(hint_chain_);hint_chain_=XR_NULL_HANDLE;}
        for(auto& kind:hand_spaces_) for(auto& space:kind) if(space && xrDestroySpace) {xrDestroySpace(space);space=XR_NULL_HANDLE;}
        for(auto& chain:chains_) if(chain && xrDestroySwapchain) {xrDestroySwapchain(chain);chain=XR_NULL_HANDLE;}
        if(local_ && xrDestroySpace) {xrDestroySpace(local_);local_=XR_NULL_HANDLE;}
        if(session_ && xrDestroySession) {xrDestroySession(session_);session_=XR_NULL_HANDLE;}
        if(action_set_ && xrDestroyActionSet) {xrDestroyActionSet(action_set_);action_set_=XR_NULL_HANDLE;}
        if(instance_ && xrDestroyInstance) {xrDestroyInstance(instance_);instance_=XR_NULL_HANDLE;}
        if(loader_) {FreeLibrary(loader_);loader_=nullptr;}
    }
    // Tracked frames use the pose/FOV recorded by the native Setup hook. Menu or
    // untracked frames retain the native symmetric FOV presentation.
    // desktop: the game's backbuffer; in gameplay it receives the processed eyes per the monitor-view setting.
    void present(ID3D11Texture2D* left,ID3D11Texture2D* right,float hfov,float vfov,bool gameplay=true,bool authored_camera=false,bool settings_available=false,
        ID3D11Texture2D* desktop=nullptr,std::array<UINT,2> desktop_view={}) {
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
                    continuity_.suspend();menu_panel_.reset();authored_panel_.reset();views_valid_=false;rendered_mask_=0;
                    settings_.suspend();settings_placement_.reset();
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
                if(change->session==session_ && change->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL) {
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
        locate_cpu_time_=cpu_time_ns();
        constexpr XrViewStateFlags valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        views_valid_=count==2 && (view_state.viewStateFlags&valid)==valid;
        last_should_render=frame_.shouldRender!=XR_FALSE;last_views_valid=views_valid_;
        if(continuity_.advance(state==XR_SESSION_STATE_VISIBLE || state==XR_SESSION_STATE_FOCUSED,
            last_should_render,views_valid_,frame_.predictedDisplayTime)) {
            menu_panel_.reset();authored_panel_.reset();rendered_mask_=0;
        }
        last_tracked_pair=rendered_mask_==3;
        sample_input();
        // Preserve squeeze actions before the paused overlay consumes controls.
        const bool raw_both_grips_held=controller_input.left_grip>=.8f && controller_input.right_grip>=.8f;
        // In menus a left-grip squeeze re-places the menu where the user is looking.
        const bool menu_grip=controller_input.left_grip>=.6f;
        if(menu_grip && !menu_grip_was_ && (!gameplay || settings_.open()) && controller_input.right_grip<.5f) {
            menu_panel_.summon();settings_placement_.summon();
        }
        menu_grip_was_=menu_grip;
        const float raw_right_trigger=controller_input.right_trigger;
        const bool raw_input_active=controller_input.active;
        bool settings_consumed=false;
        std::optional<PanelPointer> pointer;
        if(settings_enabled_) {
            if(settings_.open()) {
                constexpr XrSpaceLocationFlags valid_pose=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
                const auto& aim=aim_locations[1];
                if((aim.locationFlags&valid_pose)==valid_pose) {
                    const auto& p=aim.pose;
                    pointer=panel_pointer({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},settings_pose(),1.28,1.12);
                }
            }
            const auto current_reference=reference_epoch();
            if(avatar_prompt_reference_epoch_ && current_reference!=avatar_prompt_reference_epoch_) {
                avatar_calibration_.synchronize(current_reference,false);
                settings_.calibration_status(avatar_calibration_.state());settings_.calibration_identity_changed();
            }
            avatar_prompt_reference_epoch_=current_reference;
            const auto changes=settings_.update(controller_input,gameplay,state==XR_SESSION_STATE_FOCUSED,pointer,settings_available);
            const auto old_calibration_state=avatar_calibration_.state();
            const auto calibration_state=avatar_calibration_.synchronize(reference_epoch(),
                settings_.open() && settings_.calibration_page() && !gameplay && state==XR_SESSION_STATE_FOCUSED);
            if(calibration_state!=old_calibration_state) settings_.calibration_status(calibration_state);
            if(changes.calibrate) {
                if(avatar_measurements_) avatar_calibration_.begin(reference_epoch(),*avatar_measurements_);
                else settings_.calibration_status(AvatarCalibrationState::dimensions_unsupported);
            }
            if(settings_.open() && !gameplay && settings_.calibration_page() && avatar_measurements_) {
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
            }
            if(changes.opened) {settings_placement_.reset();settings_pose();}
            if(changes.resolution) resolution_change_=changes.resolution;
            if(changes.turning) turning_change_=changes.turning;
            if(changes.temporal_aa) temporal_change_=changes.temporal_aa;
            if(changes.recenter) {
                continuity_.recenter();rendered_mask_=0;menu_panel_.reset();settings_placement_.reset();settings_pose();
                settings_.calibration_status(avatar_calibration_.synchronize(reference_epoch(),false));
            }
            if(changes.antialiasing) {
                if(*changes.antialiasing && !antialiasing_) {
                    Microsoft::WRL::ComPtr<ID3D11Device> device;context_->GetDevice(&device);
                    antialiasing_=std::make_unique<EyeAntialiasing>(device.Get());
                }
                antialiasing_enabled_=*changes.antialiasing;
            }
            if(!preferences_file_.empty() && (changes.resolution || changes.antialiasing || changes.turning || changes.mirror || changes.sharpness || changes.temporal_aa || changes.aim_reticle || changes.controls_hint))
                save_preferences(preferences_file_,{settings_.requested(),settings_.antialiasing(),settings_.turning(),settings_.mirror(),settings_.sharpness(),settings_.temporal_aa(),settings_.aim_reticle(),settings_.controls_hint()});
            // Includes squeeze: holding the calibration pose must not open the native weapon selector.
            if(changes.consume) {controller_input={};settings_consumed=true;}
        }
        if(native_pointer_enabled_ && !gameplay && !settings_.open() && continuity_.usable() && state==XR_SESSION_STATE_FOCUSED && controller_input.active) {
            constexpr XrSpaceLocationFlags valid_pose=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            const auto& aim=aim_locations[1];
            if((aim.locationFlags&valid_pose)==valid_pose) {
                const auto& p=aim.pose;const auto extent=MenuPanel::extent(width_,height_);
                const auto hit=panel_pointer({{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},native_menu_pose(),extent[0],extent[1]);
                if(hit) native_menu_input={hit->u,hit->v,GetTickCount64(),continuity_.epoch(),true,controller_input.right_trigger>.55f};
            }
        }
        controller_input.right_trigger=menu_trigger_gate_.update(raw_right_trigger,!gameplay || settings_.open() || settings_consumed,raw_input_active);
        timing.locate_input_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        XrCompositionLayerProjection projection{};projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        XrCompositionLayerQuad menu{};menu.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
        XrCompositionLayerQuad settings_panel{};
        XrCompositionLayerQuad pointer_panel{};
        std::optional<XrCompositionLayerQuad> hint_panel;
        std::array<const XrCompositionLayerBaseHeader*,3> menu_layers_with_pointer{};
        projection.space=local_;projection.viewCount=2;
        std::array<XrCompositionLayerProjectionView,2> layers{};projection.views=layers.data();
        XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.displayTime=frame_.predictedDisplayTime;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
        if(gameplay) menu_panel_.reset();
        if(continuity_.usable() && settings_.open()) {
            settings_panel=settings_layer(pointer);layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&settings_panel);
            end.layerCount=1;end.layers=&layer;++settings_layers;
        } else if(continuity_.usable() && left && right &&
           (!gameplay || !tracked_gameplay_required_ || rendered_mask_==3)) {
            std::array<ID3D11Texture2D*,2> sources{left,right};
            const auto sharpness=static_cast<float>(settings_.sharpness());
            if(gameplay && sharpness>0 && !sharpen_) {
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
                auto processed=gameplay && antialiasing_enabled_?antialiasing_->apply(sources[eye]):sources[eye];
                if(gameplay && sharpness>0) processed=sharpen_->apply(processed,sharpness);
                context_->CopyResource(images_[eye].at(index).texture,processed);
                if(mirror_rects[eye]) mirror_->draw(processed,*mirror_rects[eye]);
                layers[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                layers[eye].pose=rendered_mask_==3?rendered_views_[eye].pose:views_[eye].pose;
                layers[eye].fov=rendered_mask_==3?rendered_views_[eye].fov:XrFovf{-hfov*.5f,hfov*.5f,vfov*.5f,-vfov*.5f};
                layers[eye].subImage.swapchain=chains_[eye];
                layers[eye].subImage.imageRect.extent={static_cast<int32_t>(width_),static_cast<int32_t>(height_)};
                    if(observe_transfer) observe_transfer(eye,processed,images_[eye].at(index).texture,layers[eye],gameplay,rendered_mask_==3,sources[eye]);
                } catch(...) {xrReleaseSwapchainImage(chains_[eye],&release);throw;}
                checked(xrReleaseSwapchainImage(chains_[eye],&release),"Release game eye");
            }
            if(mirror_rects[0] || mirror_rects[1]) {mirror_->end(desktop);++mirrored_frames;}
            if(!gameplay) {
                // Native menus use a stationary pointer-enabled panel.
                const auto& pose=native_menu_pose();
                const auto extent=MenuPanel::extent(width_,height_);
                if(!authored_camera) last_menu_pose=pose;
                menu.space=local_;menu.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                menu.pose.orientation={static_cast<float>(pose.orientation.x),static_cast<float>(pose.orientation.y),
                    static_cast<float>(pose.orientation.z),static_cast<float>(pose.orientation.w)};
                menu.pose.position={static_cast<float>(pose.position.x),static_cast<float>(pose.position.y),static_cast<float>(pose.position.z)};
                menu.size={extent[0],extent[1]};menu.subImage=layers[0].subImage;
                layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&menu);
            }
            end.layerCount=1;end.layers=&layer;
            if(!gameplay && native_menu_input.active) {
                pointer_panel=pointer_layer();
                menu_layers_with_pointer={layer,reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointer_panel)};
                end.layerCount=2;end.layers=menu_layers_with_pointer.data();last_pointer_layer=true;++pointer_layers;
            }
            if(gameplay && !authored_camera) {
                // Projection first, then the wrist controls card.
                end.layerCount=0;menu_layers_with_pointer[end.layerCount++]=layer;
                if(settings_.controls_hint() && (hint_panel=hint_layer())) {
                    menu_layers_with_pointer[end.layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&*hint_panel);++hint_layers;
                }
                end.layers=menu_layers_with_pointer.data();
            }
        }
        timing.transfer_ns=cpu_time_ns()-stage_start;stage_start=cpu_time_ns();
        checked(xrEndFrame(session_,&end),"Submit game XR frame");frame_open_=false;
        timing.submit_ns=cpu_time_ns()-stage_start;timing.end_ns=cpu_time_ns();
        last_layer_submitted=end.layerCount!=0;last_menu_layer=last_layer_submitted && !gameplay;
        last_authored_camera_layer=last_layer_submitted && authored_camera && !settings_.open();
        if(last_authored_camera_layer) ++authored_camera_layers;
        rendered_mask_=0;
        if(end.layerCount) {++submitted;if(!gameplay) ++menu_layers;}else ++dropped;
    }
};
} // namespace d2vr
