#pragma once
// Development simulator only. Runs real bridge/D3D11 code; no compositor or headset.
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include "controller_input.hpp"
#include "xr_quad_validation.hpp"
#include "tracked_pose.hpp"
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace d2vr::simxr {
using Microsoft::WRL::ComPtr;
struct Space {int hand=-1,kind=-1;};
struct Chain {
    std::array<ComPtr<ID3D11Texture2D>,3> textures;
    unsigned next{},current{};
    bool acquired{},ready{},released{};
    XrSwapchainCreateInfo info{};
};
struct Control {
    ControllerInput input{};
    XrPosef head{{0,0,0,1},{0,1.65f,0}};
    std::array<XrPosef,2> grips{{{{0,0,0,1},{-.2f,1.3f,-.4f}},{{0,0,0,1},{.2f,1.3f,-.4f}}}};
    std::array<XrPosef,2> aims=grips;
    bool should_render=true,poses_valid=true,scripted=false;
    float scripted_pitch{};
    unsigned hands=3;
    XrDuration period=11'111'111;
    XrDuration advance=11'111'111;
};
struct Summary {
    std::uint64_t waits{},begins{},ends{},layers{},protocol_errors{},chain_ops_in_frame{},subrect_layers{}; // chain_ops_in_frame: create/destroy swapchain between begin and end (SteamVR rejects the frame)
    XrTime display_time{};
    XrStructureType layer_type{};
    std::array<XrCompositionLayerProjectionView,2> eyes{};
    XrCompositionLayerQuad quad{};
    std::vector<XrCompositionLayerQuad> overlays;
    std::array<ComPtr<ID3D11Texture2D>,2> images;
};
inline Control control;
inline Summary summary;
inline ComPtr<ID3D11Device> device;
inline LUID luid{};
inline int instance_token{},session_token{},actionset_token{};
inline XrInstance instance=reinterpret_cast<XrInstance>(&instance_token);
inline XrSession session=reinterpret_cast<XrSession>(&session_token);
inline XrActionSet actionset=reinterpret_cast<XrActionSet>(&actionset_token);
inline bool instantiated{},session_created{},running{},waited{},begun{};
inline XrSessionState state=XR_SESSION_STATE_UNKNOWN;
inline XrTime display_time=1'000'000'000;
inline std::deque<XrEventDataBuffer> events;
inline std::map<XrSpace,std::unique_ptr<Space>> spaces;
inline bool color_scale_enabled=true; // the simulated instance offers XR_KHR_composition_layer_color_scale_bias
inline std::map<XrSwapchain,std::unique_ptr<Chain>> chains;
inline std::map<std::string,XrPath> paths;
inline std::map<XrAction,std::string> actions;
inline XrResult invalid() {++summary.protocol_errors;return XR_ERROR_CALL_ORDER_INVALID;}
inline void configure(ID3D11Device* source,bool scripted=false) {
    if(instantiated || session_created || !spaces.empty() || !chains.empty()) throw std::runtime_error("Simulator still owns XR resources");
    device=source;ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    if(FAILED(source->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc)))
        throw std::runtime_error("Cannot select simulator device");
    luid=desc.AdapterLuid;control={};control.input.active=true;control.scripted=scripted;summary={};
    display_time=1'000'000'000;state=XR_SESSION_STATE_UNKNOWN;events.clear();paths.clear();actions.clear();
    running=waited=begun=false;
}
inline void push_state(XrSessionState value) {
    XrEventDataSessionStateChanged event{};event.type=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    event.session=session;event.state=value;event.time=display_time;
    XrEventDataBuffer buffer{};std::memcpy(&buffer,&event,sizeof(event));events.push_back(buffer);
}
inline void reference_change(XrTime when) {
    XrEventDataReferenceSpaceChangePending event{};event.type=XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
    event.session=session;event.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;event.changeTime=when;
    XrEventDataBuffer buffer{};std::memcpy(&buffer,&event,sizeof(event));events.push_back(buffer);
}
inline XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo*,XrInstance* out) {
    if(instantiated) return invalid();instantiated=true;*out=instance;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL destroy_instance(XrInstance) {
    if(session_created || !chains.empty() || !spaces.empty()) return invalid();
    instantiated=false;device.Reset();return XR_SUCCESS;
}
inline XrResult XRAPI_CALL get_system(XrInstance,const XrSystemGetInfo*,XrSystemId* out) {*out=1;return XR_SUCCESS;}
inline XrResult XRAPI_CALL system_properties(XrInstance,XrSystemId,XrSystemProperties* out) {
    strcpy_s(out->systemName,"D2VR development simulator (no headset)");
    out->graphicsProperties={4096,4096,16};out->trackingProperties={XR_TRUE,XR_TRUE};return XR_SUCCESS;
}
// Development load testing: build/sim-eye-size ("2496 2688" = Quest 3 recommended) sets the
// simulated runtime's recommended eye size; default 640 x 640.
inline uint32_t recommended_size[2]={640,640};
// build/sim-fov "left right up down" (radians, right eye; the left eye mirrors x) sets the simulated optical
// frustum, e.g. Quest 3 via Virtual Desktop: -0.698132 0.942478 0.767945 -0.959931. Default symmetric 0.8.
inline float optical_fov[4]={-.8f,.8f,.8f,-.8f};
inline XrResult XRAPI_CALL configuration_views(XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t capacity,uint32_t* count,XrViewConfigurationView* out) {
    if(type!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    *count=2;if(!capacity) return XR_SUCCESS;if(capacity<2) return XR_ERROR_SIZE_INSUFFICIENT;
    for(unsigned i=0;i<2;++i) {out[i].recommendedImageRectWidth=recommended_size[0];out[i].recommendedImageRectHeight=recommended_size[1];
        out[i].maxImageRectWidth=4096;out[i].maxImageRectHeight=4096;out[i].recommendedSwapchainSampleCount=out[i].maxSwapchainSampleCount=1;}
    return XR_SUCCESS;
}
inline XrResult XRAPI_CALL requirements(XrInstance,XrSystemId,XrGraphicsRequirementsD3D11KHR* out) {
    out->adapterLuid=luid;out->minFeatureLevel=D3D_FEATURE_LEVEL_11_0;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL create_session(XrInstance,const XrSessionCreateInfo* info,XrSession* out) {
    if(session_created || !info->next) return invalid();
    const auto* binding=static_cast<const XrGraphicsBindingD3D11KHR*>(info->next);
    if(binding->type!=XR_TYPE_GRAPHICS_BINDING_D3D11_KHR || binding->device!=device.Get()) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    session_created=true;*out=session;push_state(XR_SESSION_STATE_READY);push_state(XR_SESSION_STATE_FOCUSED);return XR_SUCCESS;
}
inline XrResult XRAPI_CALL destroy_session(XrSession) {
    if(begun || !chains.empty() || !spaces.empty()) return invalid();
    session_created=false;running=waited=false;summary.images={};return XR_SUCCESS;
}
inline XrResult XRAPI_CALL begin_session(XrSession,const XrSessionBeginInfo*) {
    if(running || state!=XR_SESSION_STATE_READY) return invalid();running=true;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL end_session(XrSession) {
    if(begun || !running) return invalid();running=false;waited=false;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL poll_event(XrInstance,XrEventDataBuffer* out) {
    if(events.empty()) return XR_EVENT_UNAVAILABLE;
    *out=events.front();events.pop_front();
    if(out->type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) state=reinterpret_cast<XrEventDataSessionStateChanged*>(out)->state;
    return XR_SUCCESS;
}
inline XrResult XRAPI_CALL wait_frame(XrSession,const XrFrameWaitInfo*,XrFrameState* out) {
    if(!running) return XR_ERROR_SESSION_NOT_RUNNING;if(waited || begun) return invalid();
    waited=true;++summary.waits;display_time+=control.advance;
    if(control.scripted) {
        const double angle=std::sin(static_cast<double>(summary.waits)*.02)*.12;
        const auto yaw=Quaternion{0,std::sin(angle*.5),0,std::cos(angle*.5)};
        const auto pitch=Quaternion{std::sin(control.scripted_pitch*.5),0,0,std::cos(control.scripted_pitch*.5)};
        const auto q=multiplied(yaw,pitch);
        control.head.orientation={static_cast<float>(q.x),static_cast<float>(q.y),static_cast<float>(q.z),static_cast<float>(q.w)};
    }
    out->predictedDisplayTime=display_time;out->predictedDisplayPeriod=control.period;
    out->shouldRender=control.should_render?XR_TRUE:XR_FALSE;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL begin_frame(XrSession,const XrFrameBeginInfo*) {
    if(!running || !waited || begun) return invalid();waited=false;begun=true;++summary.begins;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL locate_views(XrSession,const XrViewLocateInfo*,XrViewState* out,uint32_t capacity,uint32_t* count,XrView* views) {
    if(capacity<2) return XR_ERROR_SIZE_INSUFFICIENT;*count=2;
    out->viewStateFlags=control.poses_valid?XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT|
        XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT:0;
    for(unsigned side=0;side<2;++side) {
        views[side].pose=control.head;
        // Rotate IPD with the scripted head, like a real rigid stereo pair.
        const auto& q=control.head.orientation;const float x=side?.032f:-.032f;
        views[side].pose.position.x+=(1-2*(q.y*q.y+q.z*q.z))*x;
        views[side].pose.position.y+=2*(q.x*q.y+q.z*q.w)*x;
        views[side].pose.position.z+=2*(q.x*q.z-q.y*q.w)*x;
        views[side].fov=side?XrFovf{optical_fov[0],optical_fov[1],optical_fov[2],optical_fov[3]}:XrFovf{-optical_fov[1],-optical_fov[0],optical_fov[2],optical_fov[3]};
    }
    return XR_SUCCESS;
}
inline XrResult XRAPI_CALL reference_space(XrSession,const XrReferenceSpaceCreateInfo*,XrSpace* out) {
    auto space=std::make_unique<Space>();*out=reinterpret_cast<XrSpace>(space.get());spaces.emplace(*out,std::move(space));return XR_SUCCESS;
}
inline XrResult XRAPI_CALL destroy_space(XrSpace space) {return spaces.erase(space)?XR_SUCCESS:XR_ERROR_HANDLE_INVALID;}
inline XrResult XRAPI_CALL formats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out) {
    *count=2;if(!capacity) return XR_SUCCESS;if(capacity<2) return XR_ERROR_SIZE_INSUFFICIENT;
    out[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;out[1]=DXGI_FORMAT_R8G8B8A8_UNORM;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL create_chain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    if(begun) ++summary.chain_ops_in_frame;
    if(!info->width || !info->height || info->width>4096 || info->height>4096 || info->sampleCount!=1) return XR_ERROR_VALIDATION_FAILURE;
    auto chain=std::make_unique<Chain>();chain->info=*info;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=info->width;desc.Height=info->height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=static_cast<DXGI_FORMAT>(info->format);desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    for(auto& texture:chain->textures) if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture))) return XR_ERROR_OUT_OF_MEMORY;
    *out=reinterpret_cast<XrSwapchain>(chain.get());chains.emplace(*out,std::move(chain));return XR_SUCCESS;
}
inline XrResult XRAPI_CALL destroy_chain(XrSwapchain chain) {
    if(begun) ++summary.chain_ops_in_frame;
    const auto at=chains.find(chain);if(at==chains.end()) return XR_ERROR_HANDLE_INVALID;
    if(at->second->acquired) return invalid();chains.erase(at);return XR_SUCCESS;
}
inline XrResult XRAPI_CALL chain_images(XrSwapchain chain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    const auto at=chains.find(chain);if(at==chains.end()) return XR_ERROR_HANDLE_INVALID;
    *count=3;if(!capacity) return XR_SUCCESS;if(capacity<3) return XR_ERROR_SIZE_INSUFFICIENT;
    auto* images=reinterpret_cast<XrSwapchainImageD3D11KHR*>(out);
    for(unsigned i=0;i<3;++i) images[i].texture=at->second->textures[i].Get();return XR_SUCCESS;
}
inline XrResult XRAPI_CALL acquire(XrSwapchain chain,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    const auto at=chains.find(chain);if(at==chains.end()) return XR_ERROR_HANDLE_INVALID;
    auto& c=*at->second;if(c.acquired) return invalid();c.acquired=true;c.ready=false;c.released=false;c.current=c.next++%3;*index=c.current;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL wait_image(XrSwapchain chain,const XrSwapchainImageWaitInfo*) {
    const auto at=chains.find(chain);if(at==chains.end()) return XR_ERROR_HANDLE_INVALID;
    auto& c=*at->second;if(!c.acquired || c.ready) return invalid();c.ready=true;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL release(XrSwapchain chain,const XrSwapchainImageReleaseInfo*) {
    const auto at=chains.find(chain);if(at==chains.end()) return XR_ERROR_HANDLE_INVALID;
    auto& c=*at->second;if(!c.acquired || !c.ready) return invalid();c.acquired=false;c.ready=false;c.released=true;return XR_SUCCESS;
}
inline bool image(const XrSwapchainSubImage& sub,unsigned side) {
    const auto at=chains.find(sub.swapchain);if(at==chains.end() || !at->second->released) return false;
    const auto& c=*at->second;const auto& r=sub.imageRect;
    if(r.offset.x<0 || r.offset.y<0 || r.extent.width<=0 || r.extent.height<=0 ||
       r.offset.x+r.extent.width>static_cast<int>(c.info.width) || r.offset.y+r.extent.height>static_cast<int>(c.info.height)) return false;
    if(side<2) summary.images[side]=c.textures[c.current];return true;
}
// SteamVR rejects frames whose quad/cylinder shows less than its whole swapchain.
inline bool full_extent(const XrSwapchainSubImage& sub) {
    const auto at=chains.find(sub.swapchain);if(at==chains.end()) return false;
    const auto& r=sub.imageRect;const auto& info=at->second->info;
    return r.offset.x==0 && r.offset.y==0 && r.extent.width==static_cast<int>(info.width) && r.extent.height==static_cast<int>(info.height);
}
inline XrResult XRAPI_CALL end_frame(XrSession,const XrFrameEndInfo* info) {
    if(!begun || info->displayTime!=display_time) return invalid();
    // An acquired, not-yet-ready OPTIONAL surface may remain pending across
    // frames. Only submitted layers must reference a released image.
    summary.images={};summary.overlays.clear();summary.layer_type=XR_TYPE_UNKNOWN;summary.display_time=info->displayTime;
    for(unsigned i=0;i<info->layerCount;++i) {
        const auto* layer=info->layers[i];
        if(layer->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if(p->viewCount!=2) return XR_ERROR_VALIDATION_FAILURE;
            for(unsigned side=0;side<2;++side) {if(!image(p->views[side].subImage,side)) return invalid();summary.eyes[side]=p->views[side];}
        } else if(layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* p=reinterpret_cast<const XrCompositionLayerQuad*>(layer);
            {   // what the spec and SteamVR require of every quad: live space, unit orientation, positive size, rect inside the swapchain
                const auto chain=chains.find(p->subImage.swapchain);
                const QuadChainFacts facts{chain!=chains.end(),chain!=chains.end()?chain->second->info.width:0u,chain!=chains.end()?chain->second->info.height:0u};
                if(const auto bad=validate_quad_layer(*p,spaces.count(p->space)!=0,facts,color_scale_enabled);XR_FAILED(bad)) return bad;
            }
            if(!full_extent(p->subImage)) {++summary.subrect_layers;return XR_ERROR_SWAPCHAIN_RECT_INVALID;}
            if(i==0) {if(!image(p->subImage,0)) return invalid();summary.quad=*p;}
            else {if(!image(p->subImage,2)) return invalid();summary.overlays.push_back(*p);}
        } else return XR_ERROR_LAYER_INVALID;
        if(i==0) summary.layer_type=layer->type;
    }
    begun=false;++summary.ends;summary.layers+=info->layerCount;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL string_path(XrInstance,const char* name,XrPath* out) {
    auto found=paths.find(name);if(found==paths.end()) found=paths.emplace(name,paths.size()+1).first;*out=found->second;return XR_SUCCESS;
}
// Both hands report the Touch profile (Plan Spec deviation 7: xrGetCurrentInteractionProfile + xrPathToString).
inline XrResult XRAPI_CALL current_profile(XrSession,XrPath,XrInteractionProfileState* out) {
    XrPath touch{};string_path(XR_NULL_HANDLE,"/interaction_profiles/oculus/touch_controller",&touch);out->interactionProfile=touch;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL path_to_string(XrInstance,XrPath path,uint32_t capacity,uint32_t* count,char* buffer) {
    for(const auto& [name,value]:paths) if(value==path) {
        const auto needed=static_cast<uint32_t>(name.size()+1);if(count) *count=needed;
        if(capacity<needed || !buffer) return XR_ERROR_SIZE_INSUFFICIENT;
        std::memcpy(buffer,name.c_str(),needed);return XR_SUCCESS;
    }
    return XR_ERROR_PATH_INVALID;
}
inline XrResult XRAPI_CALL create_actionset(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out) {*out=actionset;return XR_SUCCESS;}
inline XrResult XRAPI_CALL destroy_actionset(XrActionSet) {actions.clear();return XR_SUCCESS;}
inline XrResult XRAPI_CALL create_action(XrActionSet,const XrActionCreateInfo* info,XrAction* out) {
    *out=reinterpret_cast<XrAction>(static_cast<std::uintptr_t>(actions.size()+1));actions.emplace(*out,info->actionName);return XR_SUCCESS;
}
inline XrResult XRAPI_CALL suggest(XrInstance,const XrInteractionProfileSuggestedBinding*) {return XR_SUCCESS;}
inline XrResult XRAPI_CALL attach(XrSession,const XrSessionActionSetsAttachInfo*) {return XR_SUCCESS;}
inline int hand(XrPath path) {return path==paths.at("/user/hand/left")?0:1;}
inline XrResult XRAPI_CALL action_space(XrSession,const XrActionSpaceCreateInfo* info,XrSpace* out) {
    auto space=std::make_unique<Space>();space->hand=hand(info->subactionPath);space->kind=actions.at(info->action)=="aim_pose"?1:0;
    *out=reinterpret_cast<XrSpace>(space.get());spaces.emplace(*out,std::move(space));return XR_SUCCESS;
}
inline XrResult XRAPI_CALL sync(XrSession,const XrActionsSyncInfo*) {return state==XR_SESSION_STATE_FOCUSED?XR_SUCCESS:XR_SESSION_NOT_FOCUSED;}
inline XrResult XRAPI_CALL pose_action(XrSession,const XrActionStateGetInfo* info,XrActionStatePose* out) {
    out->isActive=(control.hands&(1u<<hand(info->subactionPath)))?XR_TRUE:XR_FALSE;return XR_SUCCESS;
}
inline XrResult XRAPI_CALL locate_space(XrSpace space,XrSpace,XrTime time,XrSpaceLocation* out) {
    const auto at=spaces.find(space);if(at==spaces.end()) return XR_ERROR_HANDLE_INVALID;
    const auto& s=*at->second;out->locationFlags=control.poses_valid?XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT:0;
    out->pose=s.hand<0?control.head:s.kind?control.aims[s.hand]:control.grips[s.hand];
    // Finite-difference linear velocity between located display times.
    struct Previous {XrTime time{};XrVector3f position{};};
    static std::map<XrSpace,Previous> previous;
    if(auto* velocity=reinterpret_cast<XrSpaceVelocity*>(const_cast<void*>(out->next));velocity && velocity->type==XR_TYPE_SPACE_VELOCITY) {
        auto& last=previous[space];velocity->velocityFlags=0;
        if(last.time && time>last.time) {
            const float dt=static_cast<float>(time-last.time)*1e-9f;
            velocity->linearVelocity={(out->pose.position.x-last.position.x)/dt,(out->pose.position.y-last.position.y)/dt,(out->pose.position.z-last.position.z)/dt};
            velocity->velocityFlags=XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
        }
        if(time!=last.time) last={time,out->pose.position};
    }
    return XR_SUCCESS;
}
inline XrResult XRAPI_CALL float_action(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* out) {
    const int side=hand(info->subactionPath);const auto& name=actions.at(info->action);out->isActive=control.input.active?XR_TRUE:XR_FALSE;
    out->currentState=name=="trigger"?(side?control.input.right_trigger:control.input.left_trigger):
        (side?control.input.right_grip:control.input.left_grip);return XR_SUCCESS;
}
inline XrResult XRAPI_CALL vector_action(XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* out) {
    const int side=hand(info->subactionPath);out->isActive=control.input.active?XR_TRUE:XR_FALSE;
    out->currentState=side?XrVector2f{control.input.turn_x,control.input.turn_y}:XrVector2f{control.input.move_x,control.input.move_y};return XR_SUCCESS;
}
inline XrResult XRAPI_CALL bool_action(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out) {
    const int side=hand(info->subactionPath);const auto& name=actions.at(info->action);out->isActive=control.input.active?XR_TRUE:XR_FALSE;
    out->currentState=(name=="first_button"?(side?control.input.a:control.input.x):name=="second_button"?(side?control.input.b:control.input.y):
        name=="stick_click"?(side?control.input.right_click:control.input.left_click):control.input.menu)?XR_TRUE:XR_FALSE;return XR_SUCCESS;
}
inline unsigned haptic_pulses{};
inline XrResult XRAPI_CALL haptic(XrSession,const XrHapticActionInfo*,const XrHapticBaseHeader*) {++haptic_pulses;return XR_SUCCESS;}
inline XrResult XRAPI_CALL dispatch(XrInstance,const char* name,PFN_xrVoidFunction* out) {
    *out=nullptr;
#define SIM_ENTRY(api,impl) if(std::strcmp(name,#api)==0) {*out=reinterpret_cast<PFN_xrVoidFunction>(&impl);return XR_SUCCESS;}
    SIM_ENTRY(xrCreateInstance,create_instance) SIM_ENTRY(xrDestroyInstance,destroy_instance)
    SIM_ENTRY(xrGetSystem,get_system) SIM_ENTRY(xrGetSystemProperties,system_properties)
    SIM_ENTRY(xrEnumerateViewConfigurationViews,configuration_views) SIM_ENTRY(xrGetD3D11GraphicsRequirementsKHR,requirements)
    SIM_ENTRY(xrCreateSession,create_session) SIM_ENTRY(xrDestroySession,destroy_session)
    SIM_ENTRY(xrBeginSession,begin_session) SIM_ENTRY(xrEndSession,end_session) SIM_ENTRY(xrPollEvent,poll_event)
    SIM_ENTRY(xrWaitFrame,wait_frame) SIM_ENTRY(xrBeginFrame,begin_frame) SIM_ENTRY(xrEndFrame,end_frame)
    SIM_ENTRY(xrLocateViews,locate_views) SIM_ENTRY(xrCreateReferenceSpace,reference_space) SIM_ENTRY(xrDestroySpace,destroy_space)
    SIM_ENTRY(xrEnumerateSwapchainFormats,formats) SIM_ENTRY(xrCreateSwapchain,create_chain)
    SIM_ENTRY(xrDestroySwapchain,destroy_chain) SIM_ENTRY(xrEnumerateSwapchainImages,chain_images)
    SIM_ENTRY(xrAcquireSwapchainImage,acquire) SIM_ENTRY(xrWaitSwapchainImage,wait_image) SIM_ENTRY(xrReleaseSwapchainImage,release)
    SIM_ENTRY(xrStringToPath,string_path) SIM_ENTRY(xrCreateActionSet,create_actionset) SIM_ENTRY(xrDestroyActionSet,destroy_actionset)
    SIM_ENTRY(xrCreateAction,create_action) SIM_ENTRY(xrSuggestInteractionProfileBindings,suggest) SIM_ENTRY(xrAttachSessionActionSets,attach)
    SIM_ENTRY(xrCreateActionSpace,action_space) SIM_ENTRY(xrSyncActions,sync) SIM_ENTRY(xrGetActionStatePose,pose_action)
    SIM_ENTRY(xrGetActionStateFloat,float_action) SIM_ENTRY(xrGetActionStateVector2f,vector_action)
    SIM_ENTRY(xrGetActionStateBoolean,bool_action) SIM_ENTRY(xrLocateSpace,locate_space) SIM_ENTRY(xrApplyHapticFeedback,haptic)
    SIM_ENTRY(xrGetCurrentInteractionProfile,current_profile) SIM_ENTRY(xrPathToString,path_to_string)
#undef SIM_ENTRY
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
} // namespace d2vr::simxr
