// Standalone runtime/device probe; optional short tracking session. No game injection or registry writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

struct Library {
    HMODULE handle{};
    explicit Library(const wchar_t* path) {
        handle = LoadLibraryExW(path, nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle) throw std::runtime_error("Cannot load OpenXR loader; Windows error " +
                                             std::to_string(GetLastError()));
    }
    ~Library() { if (handle) FreeLibrary(handle); }
    Library(const Library&)=delete;
    Library& operator=(const Library&)=delete;
};

void check(XrResult result, const char* operation) {
    if (XR_FAILED(result)) throw std::runtime_error(std::string(operation) +
                               " failed, XrResult=" + std::to_string(result));
}

template<class T>
T function(PFN_xrGetInstanceProcAddr get, XrInstance instance, const char* name) {
    PFN_xrVoidFunction pointer{};
    check(get(instance, name, &pointer), name);
    if (!pointer) throw std::runtime_error(std::string("Missing function: ")+name);
    return reinterpret_cast<T>(pointer);
}

// FARPROC is a function pointer with an unrelated prototype. memcpy avoids
// MSVC's unsafe function-cast warning while preserving its pointer bits.
template<class T>
T exported(HMODULE library, const char* name) {
    auto symbol = GetProcAddress(library, name);
    if (!symbol) throw std::runtime_error(std::string("Missing loader export: ")+name);
    static_assert(sizeof(T)==sizeof(symbol));
    T pointer{};
    memcpy(&pointer, &symbol, sizeof(pointer));
    return pointer;
}

struct Instance {
    XrInstance handle{XR_NULL_HANDLE};
    PFN_xrDestroyInstance destroy{};
    ~Instance() { if (handle && destroy) destroy(handle); }
    Instance()=default;
    Instance(const Instance&)=delete;
    Instance& operator=(const Instance&)=delete;
};

// A short standalone session with solid-colour diagnostic eye textures. It
// samples runtime poses/actions; it does not render game geometry or an avatar.
int sample_tracking(PFN_xrGetInstanceProcAddr get, XrInstance instance, XrSystemId system,
                    ID3D11Device* device) {
#define LOAD(name) auto name=function<PFN_##name>(get, instance, #name)
    LOAD(xrCreateSession); LOAD(xrDestroySession); LOAD(xrPollEvent);
    LOAD(xrBeginSession); LOAD(xrEndSession); LOAD(xrRequestExitSession);
    LOAD(xrCreateReferenceSpace); LOAD(xrCreateActionSpace); LOAD(xrDestroySpace);
    LOAD(xrStringToPath); LOAD(xrCreateActionSet); LOAD(xrDestroyActionSet);
    LOAD(xrCreateAction); LOAD(xrSuggestInteractionProfileBindings);
    LOAD(xrAttachSessionActionSets); LOAD(xrSyncActions); LOAD(xrGetActionStatePose);
    LOAD(xrWaitFrame); LOAD(xrBeginFrame); LOAD(xrEndFrame);
    LOAD(xrLocateViews); LOAD(xrLocateSpace);
    LOAD(xrEnumerateSwapchainFormats); LOAD(xrCreateSwapchain); LOAD(xrDestroySwapchain);
    LOAD(xrEnumerateSwapchainImages); LOAD(xrAcquireSwapchainImage);
    LOAD(xrWaitSwapchainImage); LOAD(xrReleaseSwapchainImage);
#undef LOAD
    XrSession session{XR_NULL_HANDLE};
    XrActionSet action_set{XR_NULL_HANDLE};
    XrSpace local{XR_NULL_HANDLE}, head{XR_NULL_HANDLE}, hands[2]{XR_NULL_HANDLE,XR_NULL_HANDLE};
    XrSwapchain swapchains[2]{XR_NULL_HANDLE,XR_NULL_HANDLE};
    std::vector<XrSwapchainImageD3D11KHR> images[2];
    std::vector<ComPtr<ID3D11RenderTargetView>> targets[2];
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    auto cleanup=[&]() {
        if (context) context->ClearState();
        for (auto& eye : targets) eye.clear();
        for (auto swapchain : swapchains) if (swapchain) xrDestroySwapchain(swapchain);
        for (auto hand : hands) if (hand) xrDestroySpace(hand);
        if (head) xrDestroySpace(head);
        if (local) xrDestroySpace(local);
        if (session) xrDestroySession(session);
        if (action_set) xrDestroyActionSet(action_set);
    };
    try {
        XrGraphicsBindingD3D11KHR binding{};
        binding.type=XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
        binding.device=device;
        XrSessionCreateInfo session_info{};
        session_info.type=XR_TYPE_SESSION_CREATE_INFO;
        session_info.next=&binding;
        session_info.systemId=system;
        check(xrCreateSession(instance, &session_info, &session), "Create tracking session");
        uint32_t format_count{};
        check(xrEnumerateSwapchainFormats(session, 0, &format_count, nullptr), "Get colour format count");
        std::vector<int64_t> formats(format_count);
        check(xrEnumerateSwapchainFormats(session, format_count, &format_count, formats.data()), "Get colour formats");
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        for (auto preferred : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                                DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
            if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(preferred))!=formats.end()) {
                format=preferred;
                break;
            }
        }
        if (format==DXGI_FORMAT_UNKNOWN) throw std::runtime_error("No supported probe colour format");
        std::cout << "Diagnostic swapchain format: " << format << '\n';
        for (int eye=0; eye<2; ++eye) {
            XrSwapchainCreateInfo swapchain_info{};
            swapchain_info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
            swapchain_info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            swapchain_info.format=format;
            swapchain_info.sampleCount=1;
            swapchain_info.width=512;
            swapchain_info.height=512;
            swapchain_info.faceCount=1;
            swapchain_info.arraySize=1;
            swapchain_info.mipCount=1;
            check(xrCreateSwapchain(session, &swapchain_info, &swapchains[eye]), "Create diagnostic eye swapchain");
            uint32_t count{};
            check(xrEnumerateSwapchainImages(swapchains[eye], 0, &count, nullptr), "Get eye image count");
            images[eye].resize(count);
            for (auto& image : images[eye]) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            check(xrEnumerateSwapchainImages(swapchains[eye], count, &count,
                  reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())), "Get eye images");
            targets[eye].resize(count);
            for (uint32_t i=0; i<count; ++i) {
                D3D11_TEXTURE2D_DESC texture{};
                images[eye][i].texture->GetDesc(&texture);
                D3D11_RENDER_TARGET_VIEW_DESC view{};
                view.Format=format;
                view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
                auto result=device->CreateRenderTargetView(images[eye][i].texture, &view, &targets[eye][i]);
                if (FAILED(result))
                    throw std::runtime_error("Cannot create eye render target: HRESULT="+std::to_string(result)+
                        ", texture format="+std::to_string(texture.Format)+", bind flags="+std::to_string(texture.BindFlags));
            }
        }
        XrActionSetCreateInfo set_info{};
        set_info.type=XR_TYPE_ACTION_SET_CREATE_INFO;
        strcpy_s(set_info.actionSetName, "tracking_probe");
        strcpy_s(set_info.localizedActionSetName, "Standalone tracking probe");
        check(xrCreateActionSet(instance, &set_info, &action_set), "Create action set");
        auto path=[&](const char* value) {
            XrPath result{};
            check(xrStringToPath(instance, value, &result), value);
            return result;
        };
        XrPath subactions[2]{path("/user/hand/left"), path("/user/hand/right")};
        XrActionCreateInfo action_info{};
        action_info.type=XR_TYPE_ACTION_CREATE_INFO;
        action_info.actionType=XR_ACTION_TYPE_POSE_INPUT;
        action_info.countSubactionPaths=2;
        action_info.subactionPaths=subactions;
        strcpy_s(action_info.actionName, "grip_pose");
        strcpy_s(action_info.localizedActionName, "Controller grip pose");
        XrAction grip{XR_NULL_HANDLE};
        check(xrCreateAction(action_set, &action_info, &grip), "Create grip action");
        XrActionSuggestedBinding suggestions[2]{{grip,path("/user/hand/left/input/grip/pose")},
                                               {grip,path("/user/hand/right/input/grip/pose")}};
        XrInteractionProfileSuggestedBinding suggested{};
        suggested.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggested.interactionProfile=path("/interaction_profiles/oculus/touch_controller");
        suggested.countSuggestedBindings=2;
        suggested.suggestedBindings=suggestions;
        check(xrSuggestInteractionProfileBindings(instance, &suggested), "Suggest Quest grip bindings");
        XrSessionActionSetsAttachInfo attach{};
        attach.type=XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attach.countActionSets=1;
        attach.actionSets=&action_set;
        check(xrAttachSessionActionSets(session, &attach), "Attach actions");
        XrReferenceSpaceCreateInfo space_info{};
        space_info.type=XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        space_info.poseInReferenceSpace.orientation.w=1;
        space_info.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        check(xrCreateReferenceSpace(session, &space_info, &local), "Create local space");
        space_info.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        check(xrCreateReferenceSpace(session, &space_info, &head), "Create head space");
        for (int i=0; i<2; ++i) {
            XrActionSpaceCreateInfo info{};
            info.type=XR_TYPE_ACTION_SPACE_CREATE_INFO;
            info.action=grip;
            info.subactionPath=subactions[i];
            info.poseInActionSpace.orientation.w=1;
            check(xrCreateActionSpace(session, &info, &hands[i]), "Create hand space");
        }
        XrActiveActionSet active{action_set,XR_NULL_PATH};
        XrActionsSyncInfo sync{};
        sync.type=XR_TYPE_ACTIONS_SYNC_INFO;
        sync.countActiveActionSets=1;
        sync.activeActionSets=&active;
        bool running=false, finished=false;
        uint32_t frames=0, head_valid=0, hand_valid[2]{0,0}, submitted_layers=0;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        auto sampling_end=deadline;
        auto sampling_start=std::chrono::steady_clock::now();
        int last_second=-1;
        std::cout << "Beginning five-second standalone pose sample (solid-colour diagnostic eyes).\n";
        while (!finished && std::chrono::steady_clock::now()<deadline) {
            XrEventDataBuffer event{};
            event.type=XR_TYPE_EVENT_DATA_BUFFER;
            XrResult event_result{};
            while ((event_result=xrPollEvent(instance, &event))==XR_SUCCESS) {
                if (event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                    std::cout << "Session state: " << state << '\n';
                    if (state==XR_SESSION_STATE_READY && !running) {
                        XrSessionBeginInfo begin{};
                        begin.type=XR_TYPE_SESSION_BEGIN_INFO;
                        begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        check(xrBeginSession(session, &begin), "Begin tracking session");
                        running=true;
                        sampling_start=std::chrono::steady_clock::now();
                        sampling_end=sampling_start+std::chrono::seconds(5);
                    } else if (state==XR_SESSION_STATE_STOPPING) {
                        if (running) check(xrEndSession(session), "End tracking session");
                        running=false;
                        finished=true;
                    } else if (state==XR_SESSION_STATE_LOSS_PENDING || state==XR_SESSION_STATE_EXITING) {
                        finished=true;
                    }
                }
                event={};
                event.type=XR_TYPE_EVENT_DATA_BUFFER;
            }
            if (event_result!=XR_EVENT_UNAVAILABLE) check(event_result, "Poll tracking events");
            if (finished) break;
            if (!running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (std::chrono::steady_clock::now()>=sampling_end) {
                check(xrRequestExitSession(session), "Request tracking session exit");
                // Continue pacing until STOPPING; the session timeout remains bounded.
                sampling_end=deadline;
            }
            XrFrameWaitInfo wait{};
            wait.type=XR_TYPE_FRAME_WAIT_INFO;
            XrFrameState frame{};
            frame.type=XR_TYPE_FRAME_STATE;
            check(xrWaitFrame(session, &wait, &frame), "Wait tracking frame");
            XrFrameBeginInfo begin{};
            begin.type=XR_TYPE_FRAME_BEGIN_INFO;
            check(xrBeginFrame(session, &begin), "Begin tracking frame");
            XrViewLocateInfo locate{};
            locate.type=XR_TYPE_VIEW_LOCATE_INFO;
            locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime=frame.predictedDisplayTime;
            locate.space=local;
            XrViewState state{};
            state.type=XR_TYPE_VIEW_STATE;
            XrView views[2]{};
            for (auto& view : views) view.type=XR_TYPE_VIEW;
            uint32_t count{};
            check(xrLocateViews(session, &locate, &state, 2, &count, views), "Locate stereo eye poses");
            XrSpaceLocation location{};
            location.type=XR_TYPE_SPACE_LOCATION;
            check(xrLocateSpace(head, local, frame.predictedDisplayTime, &location), "Locate head pose");
            constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                                     XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            constexpr XrViewStateFlags eyes_required=XR_VIEW_STATE_POSITION_VALID_BIT |
                                                       XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            if ((location.locationFlags & required)==required && count==2 &&
                (state.viewStateFlags & eyes_required)==eyes_required) ++head_valid;
            check(xrSyncActions(session, &sync), "Sync grip actions");
            bool valid[2]{false,false};
            XrSpaceLocation hand_locations[2]{};
            for (int i=0; i<2; ++i) {
                XrActionStateGetInfo action_get{};
                action_get.type=XR_TYPE_ACTION_STATE_GET_INFO;
                action_get.action=grip;
                action_get.subactionPath=subactions[i];
                XrActionStatePose pose{};
                pose.type=XR_TYPE_ACTION_STATE_POSE;
                check(xrGetActionStatePose(session, &action_get, &pose), "Get grip action state");
                hand_locations[i].type=XR_TYPE_SPACE_LOCATION;
                check(xrLocateSpace(hands[i], local, frame.predictedDisplayTime, &hand_locations[i]), "Locate grip pose");
                valid[i]=pose.isActive && (hand_locations[i].locationFlags & required)==required;
                if (valid[i]) ++hand_valid[i];
            }
            XrFrameEndInfo end{};
            end.type=XR_TYPE_FRAME_END_INFO;
            end.displayTime=frame.predictedDisplayTime;
            end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            XrCompositionLayerProjectionView projection_views[2]{};
            XrCompositionLayerProjection projection{};
            projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            projection.space=local;
            projection.viewCount=2;
            projection.views=projection_views;
            auto layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            if (frame.shouldRender && count==2 && (state.viewStateFlags & eyes_required)==eyes_required) {
                for (int eye=0; eye<2; ++eye) {
                    XrSwapchainImageAcquireInfo acquire{};
                    acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
                    uint32_t image{};
                    check(xrAcquireSwapchainImage(swapchains[eye], &acquire, &image), "Acquire diagnostic image");
                    XrSwapchainImageWaitInfo image_wait{};
                    image_wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
                    image_wait.timeout=1'000'000'000;
                    auto image_result=xrWaitSwapchainImage(swapchains[eye], &image_wait);
                    check(image_result, "Wait diagnostic image");
                    if (image_result==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Diagnostic eye image wait timed out");
                    const float colour[4]{0.035f,0.06f,0.1f,1.0f};
                    context->ClearRenderTargetView(targets[eye].at(image).Get(), colour);
                    XrSwapchainImageReleaseInfo release{};
                    release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                    check(xrReleaseSwapchainImage(swapchains[eye], &release), "Release diagnostic image");
                    projection_views[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projection_views[eye].pose=views[eye].pose;
                    projection_views[eye].fov=views[eye].fov;
                    projection_views[eye].subImage.swapchain=swapchains[eye];
                    projection_views[eye].subImage.imageRect.extent={512,512};
                }
                end.layerCount=1;
                end.layers=&layer;
            }
            check(xrEndFrame(session, &end), "Submit tracking diagnostic frame");
            submitted_layers+=end.layerCount;
            ++frames;
            int second=static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now()-sampling_start).count());
            if (second!=last_second) {
                last_second=second;
                auto p=location.pose.position;
                std::cout << "Head/local metres: " << p.x << ',' << p.y << ',' << p.z
                          << "; left/right valid: " << valid[0] << '/' << valid[1] << '\n';
            }
        }
        std::cout << "Sampled frames=" << frames << ", valid head/stereo=" << head_valid
                  << ", valid grips=" << hand_valid[0] << '/' << hand_valid[1]
                  << ", submitted stereo layers=" << submitted_layers << '\n';
        cleanup();
        if (head_valid && hand_valid[0] && hand_valid[1] && submitted_layers) {
            std::cout << "PASS: live head/stereo poses and both Quest grip action poses\n"
                         "Only solid-colour eye textures were submitted; no game scene or arms/body.\n";
            return 0;
        }
        std::cout << "TRACKING_INCOMPLETE: head or controller poses unavailable in this session\n";
        return 5;
    } catch (...) {
        cleanup();
        throw;
    }
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2 && !(argc==3 && wcscmp(argv[2],L"--track")==0)) {
            std::cerr << "Usage: xr_probe.exe <absolute path to openxr_loader.dll> [--track]\n";
            return 2;
        }
        auto loader = std::filesystem::path(argv[1]);
        if (!loader.is_absolute() || !std::filesystem::is_regular_file(loader)) {
            throw std::runtime_error("Pass an existing absolute loader path");
        }
        std::cout << "Standalone OpenXR/Direct3D 11 probe; no game modifications\n";
        Library library(loader.c_str());
        auto get = exported<PFN_xrGetInstanceProcAddr>(library.handle, "xrGetInstanceProcAddr");
        auto enumerate = function<PFN_xrEnumerateInstanceExtensionProperties>(get, XR_NULL_HANDLE,
                                              "xrEnumerateInstanceExtensionProperties");
        auto create = function<PFN_xrCreateInstance>(get, XR_NULL_HANDLE, "xrCreateInstance");
        uint32_t count{};
        check(enumerate(nullptr, 0, &count, nullptr), "Enumerate extension count");
        std::vector<XrExtensionProperties> extensions(count);
        for (auto& extension : extensions) extension.type=XR_TYPE_EXTENSION_PROPERTIES;
        check(enumerate(nullptr, count, &count, extensions.data()), "Enumerate extensions");
        bool d3d11 = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
            return strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME)==0;
        });
        std::cout << "XR_KHR_D3D11_enable: " << (d3d11 ? "available" : "missing") << '\n';
        if (!d3d11) return 4;
        const char* enabled=XR_KHR_D3D11_ENABLE_EXTENSION_NAME;
        XrInstanceCreateInfo info{};
        info.type=XR_TYPE_INSTANCE_CREATE_INFO;
        strcpy_s(info.applicationInfo.applicationName, "Dishonored2 VR feasibility probe");
        info.applicationInfo.applicationVersion=1;
        info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        info.enabledExtensionCount=1;
        info.enabledExtensionNames=&enabled;
        Instance instance;
        check(create(&info, &instance.handle), "Create OpenXR instance");
        instance.destroy=function<PFN_xrDestroyInstance>(get, instance.handle, "xrDestroyInstance");
        auto properties=function<PFN_xrGetInstanceProperties>(get, instance.handle, "xrGetInstanceProperties");
        XrInstanceProperties runtime{};
        runtime.type=XR_TYPE_INSTANCE_PROPERTIES;
        check(properties(instance.handle, &runtime), "Get runtime properties");
        std::cout << "Runtime: " << runtime.runtimeName << " ("
                  << XR_VERSION_MAJOR(runtime.runtimeVersion) << '.'
                  << XR_VERSION_MINOR(runtime.runtimeVersion) << '.'
                  << XR_VERSION_PATCH(runtime.runtimeVersion) << ")\n";
        auto get_system=function<PFN_xrGetSystem>(get, instance.handle, "xrGetSystem");
        XrSystemGetInfo system_info{};
        system_info.type=XR_TYPE_SYSTEM_GET_INFO;
        system_info.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system{};
        auto result=get_system(instance.handle, &system_info, &system);
        if (result==XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            std::cout << "HEADSET_UNAVAILABLE: Connect Quest through Virtual Desktop or Steam Link,\n"
                         "then rerun. A registered runtime does not prove headset tracking.\n";
            return 3;
        }
        check(result, "Get headset system");
        auto system_properties=function<PFN_xrGetSystemProperties>(get, instance.handle, "xrGetSystemProperties");
        XrSystemProperties headset{};
        headset.type=XR_TYPE_SYSTEM_PROPERTIES;
        check(system_properties(instance.handle, system, &headset), "Get headset properties");
        std::cout << "System: " << headset.systemName << "\nTracking capability: orientation="
                  << headset.trackingProperties.orientationTracking << ", position="
                  << headset.trackingProperties.positionTracking << '\n';
        auto views=function<PFN_xrEnumerateViewConfigurationViews>(get, instance.handle,
                                                     "xrEnumerateViewConfigurationViews");
        check(views(instance.handle, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    0, &count, nullptr), "Enumerate stereo eye count");
        if (count!=2) throw std::runtime_error("Primary stereo did not return two eyes");
        std::vector<XrViewConfigurationView> eyes(count);
        for (auto& eye : eyes) eye.type=XR_TYPE_VIEW_CONFIGURATION_VIEW;
        check(views(instance.handle, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    count, &count, eyes.data()), "Enumerate stereo views");
        for (size_t i=0; i<eyes.size(); ++i) {
            std::cout << "Eye " << i << ": recommended " << eyes[i].recommendedImageRectWidth
                      << 'x' << eyes[i].recommendedImageRectHeight << '\n';
        }
        auto requirements=function<PFN_xrGetD3D11GraphicsRequirementsKHR>(get, instance.handle,
                                                     "xrGetD3D11GraphicsRequirementsKHR");
        XrGraphicsRequirementsD3D11KHR graphics{};
        graphics.type=XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
        check(requirements(instance.handle, system, &graphics), "Get D3D11 requirements");
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) throw std::runtime_error("Cannot enumerate DXGI adapters");
        ComPtr<IDXGIAdapter1> selected;
        for (UINT i=0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            hr=factory->EnumAdapters1(i, &adapter);
            if (hr==DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) throw std::runtime_error("DXGI enumeration failed");
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc))) throw std::runtime_error("Cannot read adapter");
            if (desc.AdapterLuid.HighPart==graphics.adapterLuid.HighPart &&
                desc.AdapterLuid.LowPart==graphics.adapterLuid.LowPart) {
                selected=adapter;
                std::wcout << L"Runtime GPU: " << desc.Description << '\n';
                break;
            }
        }
        if (!selected) throw std::runtime_error("Runtime's GPU is not present in DXGI");
        std::vector<D3D_FEATURE_LEVEL> levels;
        for (auto level : {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                           D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0}) {
            if (level>=graphics.minFeatureLevel) levels.push_back(level);
        }
        if (levels.empty()) throw std::runtime_error("Unsupported required D3D11 feature level");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL actual{};
        hr=D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                             levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
                             &device, &actual, &context);
        if (FAILED(hr)) throw std::runtime_error("D3D11 device creation failed; HRESULT="+
                                               std::to_string(static_cast<unsigned long>(hr)));
        std::cout << "PASS: runtime, stereo view configuration, and matching D3D11 device available\n"
                     "Live head/controller poses and stereo rendering have NOT been tested.\n";
        if (argc==3) return sample_tracking(get, instance.handle, system, device.Get());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PROBE_FAILED: " << e.what() << '\n';
        return 1;
    }
}
