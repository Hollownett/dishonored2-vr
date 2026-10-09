#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <MinHook.h>
#include "game_camera.hpp"
#include "game_stereo.hpp"
#include "game_xr.hpp"
#include "tracked_pose.hpp"
#include "probe_session.hpp"
#include "game_rig.hpp"
#include "arm_tracking.hpp"
#include "native_arm_binding_snapshot.hpp"
#include "native_weapon_attachment_trace.hpp"
#include "native_graphics_owner.hpp"
#include "gpu_dispatch_bindings.hpp"
#include "native_eye_sources.hpp"
#include "body_anchor.hpp"
#include "head_calibration.hpp"
#include "camera_comfort.hpp"
#include "crash_observer.hpp"
#include "native_camera_trace.hpp"
#include "native_gameplay_aim_trace.hpp"
#include "native_gameplay_aim.hpp"
#include "native_turning.hpp"
#include "native_menu.hpp"
#include "native_pad_focus.hpp"
#include "native_interaction_aim.hpp"
#include "native_grab.hpp"
#include "simulated_xr.hpp"
#include "simulated_cleanup_exception.hpp"
#include "simulated_grab_fixture.hpp"
#include "avatar_fit_reuse.hpp"
#include "avatar_fit_events.hpp"
#include "menu_chord.hpp"
#include "lean_control.hpp"
#include "simulated_gameplay_script.hpp"
#include "simulated_load_confirm.hpp"
#include "native_cvars.hpp"
#include "taa_constants_capture.hpp"
#include "vr_controls.hpp"
#include "play_controls.hpp"
#include "native_crosshair.hpp"
#include "native_fire_seam_trace.hpp"
#include "native_shot_aim.hpp"
#include "native_hud_census.hpp"
#include "native_hand_occupancy.hpp"
#include "native_taa_history.hpp"
#include "native_attachment_pose.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
using PresentFn=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT);
using ResizeBuffersFn=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using OutputSizeFn=void (__fastcall*)(HDC,UINT*,UINT*);
using ViewportsFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,const D3D11_VIEWPORT*);
using TargetsFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
using TargetsUavFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
using DrawIndexedFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawIndexedInstancedFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using DrawFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,UINT);
using DrawInstancedFn=void (WINAPI*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
static HMODULE module{};
static PresentFn original_present{};
static std::ofstream xr_timing_csv;
static UINT64 xr_timing_samples{};
static std::atomic<std::int64_t> previous_present_end_ns{},previous_desktop_present_ns{};
static ResizeBuffersFn original_resize{};
static OutputSizeFn original_output_size{};
static std::atomic<IDXGISwapChain*> game_chain{nullptr};
static d2vr::GraphicsOwner graphics_owner;
static void* resize_target{};
static std::array<std::byte,16> resize_original_bytes{};
static bool resize_enabled=false,output_size_enabled=false,headset_size_requested=false;
static bool optical_density_requested=false,optical_density_applied=false;
static d2vr::NativeOutputSize native_output_size;
static d2vr::CachedOutputSize cached_output_size;
static bool cached_output_written=false;
static std::atomic<bool> cached_output_restored{true};
static std::atomic<bool> headset_size_written{false},headset_size_applied{false},headset_size_restored{true};
static std::atomic<UINT64> native_size_queries{0},native_resizes{0};
static std::atomic<HRESULT> native_resize_result{S_OK};
static std::atomic<ULONGLONG> headset_size_tick{0};
static d2vr::EyeSize requested_output_size{};
static double requested_resolution_multiplier=1.0,effective_resolution_multiplier=1.0;
static bool eye_antialiasing_requested=false;
static d2vr::MirrorMode mirror_mode_requested=d2vr::MirrorMode::both_eyes;
static double sharpness_requested=.5;
static bool taa_preference=false,reticle_preference=true,hint_preference=true;
static UINT64 xr_antialiased_eyes{};
static UINT64 xr_settings_layers{},xr_resolution_changes{};
static bool settings_transfer_captured=false;
static bool authored_pair_captured=false;
static unsigned authored_capture_stable_frames=0;
static constexpr std::uintptr_t output_size_rva=0x346320;
static constexpr std::array<std::byte,32> output_size_expected_bytes{
    std::byte{0x48},std::byte{0x89},std::byte{0x5c},std::byte{0x24},std::byte{0x08},std::byte{0x57},std::byte{0x48},std::byte{0x83},
    std::byte{0xec},std::byte{0x30},std::byte{0x49},std::byte{0x8b},std::byte{0xd8},std::byte{0x48},std::byte{0x8b},std::byte{0xfa},
    std::byte{0xff},std::byte{0x15},std::byte{0x1a},std::byte{0x87},std::byte{0x73},std::byte{0x01},std::byte{0x48},std::byte{0x85},
    std::byte{0xc0},std::byte{0x75},std::byte{0x1b},std::byte{0x89},std::byte{0x07},std::byte{0x48},std::byte{0x8d},std::byte{0x0d}};
void __fastcall output_size_hook(HDC context,UINT* width_out,UINT* height_out) {
    original_output_size(context,width_out,height_out);
    if(native_output_size.apply(context,width_out,height_out)) ++native_size_queries;
}
static void* hook_target{};
static std::array<std::byte,16> disabled_target_bytes{};
static std::atomic<bool> started{false},capturing{false};
static d2vr::ProbeSession probe_session;
static std::atomic<bool> xr_exit_requested{false};
static std::atomic<bool> callback_failed{false};
static std::string callback_error;
static std::mutex callback_error_mutex;
void record_callback_error(const char* message) noexcept {
    try {
        std::lock_guard<std::mutex> lock(callback_error_mutex);
        if(callback_error.empty()) callback_error=message;
    } catch(...) {}
    callback_failed.store(true,std::memory_order_release);
}
// Recoverable faults are logged and never end the session (see recover_xr_session).
static std::atomic<UINT64> recoverable_faults{0};
static fs::path recovery_log_path;
void note_recoverable(const char* message) noexcept {
    if(++recoverable_faults>200) return;
    try {std::ofstream log(recovery_log_path,std::ios::app);log<<GetTickCount64()<<' '<<message<<'\n';} catch(...) {}
}
std::string callback_error_snapshot() {
    std::lock_guard<std::mutex> lock(callback_error_mutex);return callback_error;
}
static std::atomic<UINT64> frames{0},valid_poses{0};
static std::atomic<UINT> width{0},height{0},format{0},feature{0};
static std::uintptr_t camera_address{},image_base{};
static std::mutex log_mutex;
static std::ofstream csv;
static fs::path output_directory;
static d2vr::CrashObserver crash_observer;
static d2vr::NativeCameraTrace native_camera_trace;
static d2vr::NativeGameplayAimTrace native_gameplay_aim_trace;
static d2vr::NativeGameplayAim native_left_aim;
static bool native_left_aim_requested=false;
static bool native_camera_trace_requested=false;
static d2vr::NativeTurning native_turning;
static d2vr::TurnControl::Config turning_config;
static bool native_turning_requested=false;
static bool saved_settings_requested=false;
static d2vr::NativeMenu native_menu;
static int native_menu_mode=0;
static bool simulated_vr_requested=false;
static bool simulated_cleanup_exception_requested=false;
static d2vr::MenuChord menu_chord;
// Controls: held physical X opens the native wheel (LB); the pointing hand's trigger interacts.
static d2vr::XWheelOwner x_wheel;static d2vr::PointingHandSelector pointing_hand;static d2vr::InteractOwner interact_owner;
// The game's own crosshair follows the active hand; the fire-seam trace finds the ranged-weapon aim source.
static d2vr::NativeCrosshair native_crosshair;static d2vr::NativeFireSeamTrace native_fire_seam_trace;
// Ranged shots leave along the active hand: the launch call 0x639970 gets the hand origin/axis.
static d2vr::NativeShotAim native_shot_aim;static bool native_shot_aim_installed=false;
static d2vr::NativeHudCensus native_hud_census;static bool native_hud_census_installed=false;
static bool native_crosshair_installed=false,fire_seam_installed=false;
static d2vr::HolsterHold holster;static d2vr::CrouchFlick crouch_flick;static d2vr::GripTriggerGuard grip_guard;
// Native per-hand occupancy (present thread): bit per side, 0=left 1=right. Fresh for 300 ms.
static std::mutex hand_state_mutex;static unsigned hand_empty_bits{},hand_item_bits{};static ULONGLONG hand_state_tick{};
static std::atomic<bool> hands_native{false};
static std::uint64_t controls_epoch{};static std::atomic<int> pointing_side{1};static UINT64 wheel_frames{};static int wheel_logic{-1};
static UINT64 script_capture_next{};
static bool force_xr_fault=false;
static std::array<bool,2> grab_was_holding{};
static ULONGLONG swing_until_tick{},swing_ready_tick{};static UINT64 physical_swings{};
static bool native_menu_failure_noted=false,native_turning_failure_noted=false,native_left_aim_failure_noted=false;
static UINT64 menu_chord_epoch{};
static d2vr::LeanControl lean_control;
static std::optional<d2vr::NativeLeanOwner> lean_owner;
static UINT64 lean_owner_refresh_tick{};
static UINT64 lean_snapshot_missing{},lean_keyboard_inactive{},lean_keyboard_active{},lean_output_y{};
static bool controller_aim_requested=false,native_interaction_requested=false;
static d2vr::NativeInteractionAim native_interaction_aim;
static d2vr::NativeGrab native_grab;
static bool physical_grab_requested=false;
static std::optional<d2vr::NativeRigidBody> simulated_grab_target;
static d2vr::SimulatedGrabClock simulated_grab_clock;
static d2vr::SimulatedGrabFixtureChoice simulated_grab_choice;
static XrPosef simulated_grab_ordinary_pose{};
static UINT64 simulated_grab_ordinary_frames{},simulated_grab_ranged_frames{},simulated_grab_last_frame=~UINT64{};
static bool simulated_grab_repositioned=false,simulated_grab_focus_lost=false,simulated_grab_focus_restored=false;
static bool simulated_input_requested=false,simulated_initial_pause=false,simulated_long_wizard=false;
static UINT64 simulated_grab_focus_lost_tick{};
static d2vr::SimulatedLoadConfirm simulated_load_confirm;
static bool simulated_gameplay_script_requested=false,simulated_gameplay_last_gameplay=false,simulated_gameplay_last_focused=false;
static d2vr::SimulatedGameplayScript simulated_gameplay_script;
static std::ofstream simulated_gameplay_csv;
static int simulated_gameplay_last_index=-2;
static UINT64 simulated_gameplay_rows{};
static bool avatar_calibration_applied=false;
static double avatar_calibration_arm_scale=0,avatar_calibration_shoulder_drop=0;
static d2vr::rig::AvatarFitReuse avatar_fit_reuse;
static d2vr::rig::AvatarFitEvents avatar_fit_events;
static std::ofstream avatar_fit_events_csv;
static std::optional<d2vr::rig::AvatarFitTemplate> current_avatar_template;
static std::optional<d2vr::rig::AvatarFitTemplate> last_verified_avatar_template;
static std::atomic<bool> avatar_pose_gate{true};
static ULONGLONG avatar_template_retry_tick{};
static ULONGLONG avatar_resource_check_tick{};
static UINT64 avatar_fit_template_checks{},avatar_fit_reuses{},avatar_fit_invalidations{},avatar_fit_suspensions{},avatar_fit_accepts{};
static bool avatar_template_verified=false;
static bool avatar_resource_validation_pending=false;
static double avatar_native_bind_palm_span{},avatar_native_bind_shoulder_span{};
static ULONGLONG simulated_input_start{};
using ArmUploadFn=void (__fastcall*)(void*,const void*,UINT);
static ArmUploadFn original_arm_upload{};
static bool arm_upload_requested=false,arm_upload_enabled=false;
static bool arm_copy_requested=false,tracked_arms_requested=false;
static bool native_weapon_attachment_requested=false;
static d2vr::NativeWeaponAttachmentTrace native_weapon_attachment;
static d2vr::rig::ArmTracking arm_tracking;
static d2vr::CameraComfort camera_comfort;
static d2vr::NativeCvars native_cvars;
static d2vr::NativeTaaHistory taa_history;
// Scripted-simulation diagnostic: per-eye TAA constants (current/previous matrices, jitter).
static d2vr::TaaConstantsCapture taa_capture;static std::ofstream taa_capture_csv;static ULONGLONG taa_capture_after_tick{};
bool read_self(std::uintptr_t address,void* data,std::size_t size);
static std::atomic<int> taa_capture_context_offset{-2}; // -2 unknown, -1 direct, >=0 offset inside the engine wrapper
// True only for an object whose vtable lies inside d3d11.dll; never call through anything else.
bool d3d11_object(std::uintptr_t object) {
    static const auto range=[] {
        const auto d3d_module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"d3d11.dll"));
        if(!d3d_module) return std::array<std::uintptr_t,2>{};
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(d3d_module);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(d3d_module+dos->e_lfanew);
        return std::array<std::uintptr_t,2>{d3d_module,d3d_module+nt->OptionalHeader.SizeOfImage};
    }();
    std::uintptr_t table{};
    return object && read_self(object,&table,sizeof table) && table>=range[0] && table<range[1];
}
// CPU-side fallback: raw per-eye view objects at the TAA pass, for offline matrix search.
static std::mutex taa_view_dump_mutex;static std::ofstream taa_view_dump;static unsigned taa_view_dumps{};
void dump_taa_view(void* ctx,int eye) {
    constexpr std::size_t bytes=0x7800;
    std::uintptr_t view{};
    if(!read_self(reinterpret_cast<std::uintptr_t>(ctx)+0x70,&view,sizeof view) || !view) return;
    std::lock_guard<std::mutex> lock(taa_view_dump_mutex);
    if(!taa_view_dump.is_open() || taa_view_dumps>=96) return;
    std::vector<std::byte> data(bytes);
    if(!read_self(view,data.data(),bytes)) return;
    const unsigned long long header[3]{frames.load(),static_cast<unsigned long long>(eye),view};
    taa_view_dump.write(reinterpret_cast<const char*>(header),sizeof header);
    taa_view_dump.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(bytes));
    ++taa_view_dumps;
}
void observe_taa_pass(void* ctx,int eye) {
    if(GetTickCount64()<taa_capture_after_tick) return;
    std::uintptr_t holder{};
    if(!read_self(reinterpret_cast<std::uintptr_t>(ctx)+0x108,&holder,sizeof holder) || !holder) return;
    // The job's deferred context, directly or one level inside the engine's context wrapper.
    std::uintptr_t candidate{};int offset=-1;
    if(d3d11_object(holder)) candidate=holder;
    else for(int o=0;o<0x100 && !candidate;o+=8) {
        std::uintptr_t inner{};
        if(read_self(holder+o,&inner,sizeof inner) && d3d11_object(inner)) {candidate=inner;offset=o;}
    }
    if(!candidate) {taa_capture_context_offset.store(-3);dump_taa_view(ctx,eye);return;}
    ComPtr<ID3D11DeviceContext> context;
    if(FAILED(reinterpret_cast<IUnknown*>(candidate)->QueryInterface(IID_PPV_ARGS(&context)))) {taa_capture_context_offset.store(-4);return;}
    taa_capture_context_offset.store(offset);
    taa_capture.record(context.Get(),eye,frames.load());
}
static d2vr::NativeAttachmentPose attachment_pose;
static bool attachment_pose_installed=false;
static bool taa_history_requested=false,taa_history_installed=false;
static unsigned native_cvar_overrides{};
static bool comfort_requested=false,comfort_applied=false,comfort_restored=true;
static d2vr::rig::NativeArmBindingCache arm_binding_cache;
static std::shared_ptr<const d2vr::rig::NativeArmBindingSnapshot> avatar_rig_snapshot,last_avatar_rig,last_verified_avatar_rig;
static UINT64 arm_rig_refresh_tick{};
static std::array<std::optional<d2vr::rig::Upload>,2> avatar_uploads{};
static std::array<std::atomic<UINT64>,2> arm_copy_calls{},arm_pose_calls{},arm_pose_fallbacks{},arm_pose_clamps{};
// Latest arm model origin seen at a native upload, with the Present count.
static std::mutex arm_origin_mutex;static d2vr::Vec3 arm_model_origin{};static UINT64 arm_model_origin_frame{};
static std::ofstream arm_origin_csv;
// Last tracked palette per side (model space): a brief tracked-pose gap reuses
// it instead of flashing the native arm animation for a frame.
static std::mutex last_arm_mutex;static std::array<std::array<d2vr::rig::Transform,69>,2> last_arm_palette{};static std::array<ULONGLONG,2> last_arm_tick{};
static std::atomic<UINT> arm_upload_depth{0};
static std::array<UINT,2> arm_upload_seen{},arm_upload_samples{},arm_upload_rejected{};
static std::mutex arm_log_mutex;
static std::ofstream arm_csv,arm_palettes;
static constexpr std::uintptr_t arm_upload_rva=0x25efe0;
static constexpr std::array<std::byte,32> arm_upload_expected_bytes{
    std::byte{0x40},std::byte{0x55},std::byte{0x41},std::byte{0x54},std::byte{0x41},std::byte{0x55},std::byte{0x41},std::byte{0x56},
    std::byte{0x41},std::byte{0x57},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x40},std::byte{0x48},std::byte{0x8d},
    std::byte{0x6c},std::byte{0x24},std::byte{0x20},std::byte{0x48},std::byte{0x8b},std::byte{0x81},std::byte{0xe8},std::byte{0x1f},
    std::byte{0x00},std::byte{0x00},std::byte{0x48},std::byte{0x89},std::byte{0x5d},std::byte{0x58},std::byte{0x48},std::byte{0x89}};
void observe_arm_upload(void* model,const void* source,UINT count,std::uintptr_t caller) noexcept;
void invoke_arm_upload(void* model,const void* source,UINT count);
void __fastcall arm_upload_hook(void* model,const void* source,UINT count) {
    arm_upload_depth.fetch_add(1,std::memory_order_acq_rel);
    __try {
        observe_arm_upload(model,source,count,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
        invoke_arm_upload(model,source,count);
    } __finally {arm_upload_depth.fetch_sub(1,std::memory_order_acq_rel);}
}
using ViewSetupFn=void (__fastcall*)(void*,int*,int*);
static std::atomic<ViewSetupFn> original_setup{nullptr};
static bool head_requested=false,head_wrote=false,symmetric_frustum_requested=false;
static std::atomic<UINT> head_depth{0};
static UINT64 head_setup_seen{},head_matched{},head_writes{},head_restores{};
static UINT64 head_pose_epoch{};
static d2vr::BodyAnchor head_body_anchor;
static d2vr::HeadCalibration head_calibration;
static std::uintptr_t head_body_camera{};
static std::ofstream head_anchor_csv;
static UINT64 head_anchor_samples{};
static DWORD xr_present_thread{};
static std::uintptr_t head_source_address{};
static std::array<std::byte,0xa8> head_saved{};
static d2vr::TrackedPose head_origin{};
static bool eye_boundary_requested=false,eye_boundary_enabled=false;
static bool native_eye_capture_requested=false;
static d2vr::NativeEyePair native_eye_pair;
static UINT64 native_eye_pairs{},native_eye_copy_count{};
static constexpr std::uintptr_t eye_begin_rva=0x351210;
static constexpr std::array<std::byte,32> eye_begin_expected_bytes{
    std::byte{0x40},std::byte{0x53},std::byte{0x48},std::byte{0x83},std::byte{0xec},std::byte{0x20},std::byte{0x48},std::byte{0x8b},
    std::byte{0xd9},std::byte{0x48},std::byte{0xc7},std::byte{0x81},std::byte{0x40},std::byte{0xa3},std::byte{0x5f},std::byte{0x00},
    std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x4d},std::byte{0x85},std::byte{0xc0},std::byte{0x0f},
    std::byte{0x84},std::byte{0xea},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x41},std::byte{0x8b},std::byte{0x80}};
using EyeBeginFn=void (__fastcall*)(void*,void*,void*,int);
static EyeBeginFn original_eye_begin{};
static d2vr::NativeEyeSources eye_sources;
static std::uintptr_t eye_view_allocation{};
static std::array<UINT64,3> eye_camera_owner{};
static std::atomic<UINT64> eye_begin_calls{0},eye_begin_matches{0};
static std::ofstream eye_boundary_csv;
static UINT eye_boundary_rows{};
void observe_eye_setup(void* view) noexcept;
void observe_eye_begin(void* view) noexcept;
void __fastcall eye_begin_hook(void* backend,void* world,void* view,int index) {
    d2vr::CrashOperation operation("native eye begin");
    observe_eye_begin(view);
    original_eye_begin(backend,world,view,index);
}
void prepare_head_view(void* view) noexcept;
void restore_head_view() noexcept;
void __fastcall setup_hook(void* view,int* window_rect,int* render_rect);
// The registered trampoline survives stopped sessions. Select the current mode
// on every call so a desktop run cannot leave VR using its observation callback.
void __fastcall view_setup_hook(void* view,int* window_rect,int* render_rect) {
    const auto previous_operation=d2vr::crash_operation;
    d2vr::crash_operation="native view setup";
    const auto original=original_setup.load(std::memory_order_acquire);
    const bool claimed=head_depth.fetch_add(1,std::memory_order_acq_rel)==0;
    __try {
        if(head_requested) {
            if(claimed) observe_eye_setup(view);
            if(claimed) prepare_head_view(view);
            original(view,window_rect,render_rect);
        } else setup_hook(view,window_rect,render_rect);
    } __finally {
        if(claimed && head_requested) restore_head_view();
        head_depth.fetch_sub(1,std::memory_order_acq_rel);
        d2vr::crash_operation=previous_operation;
    }
}
static bool setup_requested=false,setup_enabled=false;
static void* setup_target{};
static std::array<std::byte,32> setup_original_bytes{};
static std::atomic<UINT64> setup_calls{0},setup_valid_pairs{0},setup_matching_pairs{0};
static std::ofstream setup_csv;
static constexpr std::uintptr_t setup_rva=0x374e40;
static constexpr std::size_t render_copy_offset=0x39d0;
// Exact installed executable, independently checked against the MIT headtracking
// profile. Refuse an already modified entry instead of chaining an unknown hook.
static constexpr std::array<std::byte,32> setup_expected_bytes={
    std::byte{0x48},std::byte{0x8b},std::byte{0xc4},std::byte{0x4c},std::byte{0x89},std::byte{0x40},std::byte{0x18},std::byte{0x55},
    std::byte{0x53},std::byte{0x56},std::byte{0x57},std::byte{0x41},std::byte{0x54},std::byte{0x41},std::byte{0x56},std::byte{0x41},
    std::byte{0x57},std::byte{0x48},std::byte{0x8d},std::byte{0x68},std::byte{0xa1},std::byte{0x48},std::byte{0x81},std::byte{0xec},
    std::byte{0xe0},std::byte{0x00},std::byte{0x00},std::byte{0x00},std::byte{0x44},std::byte{0x0f},std::byte{0x29},std::byte{0x48}};

void __fastcall setup_hook(void* view,int* window_rect,int* render_rect) {
    const auto original=original_setup.load(std::memory_order_acquire);
    const UINT64 call=++setup_calls;
    observe_eye_setup(view);
    // Read-only observation. Never hold a diagnostic lock while calling engine
    // code, and never alter its source view or render copy, even on nested calls.
    const bool sample=capturing.load(std::memory_order_acquire) && view && call%60==1;
    std::array<std::byte,d2vr::camera_read_size> input{},rendered{};
    SIZE_T count{};
    bool input_read=sample && ReadProcessMemory(GetCurrentProcess(),view,input.data(),input.size(),&count) && count==input.size();
    original(view,window_rect,render_rect);
    if(!input_read || !capturing.load()) return;
    auto address=reinterpret_cast<std::uintptr_t>(view);
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address+render_copy_offset),rendered.data(),rendered.size(),&count) || count!=rendered.size()) return;
    try {
        auto before=d2vr::decode_render_view(input),after=d2vr::decode_render_view(rendered);
        if(!before || !after) return;
        const bool matched=before->position==after->position && before->basis==after->basis &&
            before->horizontal_fov==after->horizontal_fov && before->vertical_fov==after->vertical_fov;
        std::lock_guard<std::mutex> lock(log_mutex);
        if(!capturing.load()) return;
        ++setup_valid_pairs;++valid_poses;if(matched) ++setup_matching_pairs;
        for(UINT side=0;side<2;++side) {
            const auto& pose=side?*after:*before;
            setup_csv << call << ',' << GetCurrentThreadId() << ',' << address << ',' << side << ','
                      << pose.horizontal_fov << ',' << pose.vertical_fov;
            for(float value:pose.position) setup_csv << ',' << value;
            for(float value:pose.basis) setup_csv << ',' << value;
            setup_csv << ',' << (matched?1:0) << '\n';
        }
    } catch(...) {callback_failed=true;}
}

bool disable_setup_hook() {
    if(!setup_enabled) return true;
    if(MH_DisableHook(setup_target)!=MH_OK) return false;
    setup_enabled=false;
    const auto deadline=GetTickCount64()+1000;
    while(head_depth.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
    if(head_depth.load()) return false;
    std::array<std::byte,32> restored{};SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(),setup_target,restored.data(),restored.size(),&count) &&
        count==restored.size() && restored==setup_original_bytes;
}
static bool stereo_requested=false;
static bool single_eye_requested=false,left_captured=false,right_phase=false;
static bool paired_requested=false,paired_left_ready=false;
static bool xr_requested=false;
static bool controller_requested=false,controller_import_restored=true;
static d2vr::XInputBridge controller_bridge;
static d2vr::NativePadFocus native_pad_focus;
static UINT64 xr_controller_samples{};
static bool temporal_aa_requested=false,temporal_aa_active=false,temporal_aa_written=false,temporal_aa_restored=true;
static LONG saved_temporal_aa{};
static constexpr std::uintptr_t temporal_aa_value_rva=0x397bbf8;
static bool full_scale_requested=false,full_scale_written=false,full_scale_restored=true;
static std::array<bool,2> full_scale_active{};
static std::array<LONG,2> saved_scale{};
static constexpr std::array<std::uintptr_t,2> scale_value_rvas{0x397bfec,0x397c06c};
static std::array<XrExtent2Di,2> xr_recommended_eye_sizes{},xr_swapchain_eye_sizes{};
static std::array<DXGI_FORMAT,2> xr_eye_texture_formats{};
static std::unique_ptr<d2vr::GameXr> game_xr;
static std::atomic<bool> xr_close_requested{false},xr_close_complete{false};
static UINT64 xr_submitted{},xr_dropped{},xr_gameplay_frames{},xr_menu_layers{};
static UINT xr_transfer_mask{};
static UINT64 xr_tracked_transfer_frames{},xr_live_status_tick{};
static UINT64 xr_transfer_frame{};
static UINT xr_menu_transfer_mask{};
static UINT64 xr_menu_transfer_frame{},xr_capture_epoch{},xr_pose_epoch{},xr_state_events{},xr_reference_events{};
static UINT64 xr_reference_epoch{};
static std::array<double,2> xr_source_lit_fractions{};
static int xr_last_state{};
alignas(8) static std::array<std::byte,120> paired_metadata{};
static ComPtr<ID3D11Texture2D> paired_backbuffer,paired_left;
static UINT paired_buffer_binds=0,paired_capture_binds=0;
static UINT paired_binds_per_eye=0,paired_baseline_samples=0;
static std::array<UINT64,16> paired_bind_histogram{};
alignas(8) static std::array<std::byte,80> left_metadata{},right_metadata{};
static constexpr char metadata_end[]="D2VR diagnostic end";
static void* diagnostic_mode_pointer{};
static std::atomic<bool> stereo_active{false};
static bool mono_captured=false,stereo_captured=false;
static bool mode_restored=true,values_restored=true;
static std::atomic<UINT64> stereo_frames{0},distinct_view_frames{0};
static std::atomic<float> measured_separation{0};
static void* saved_mode{};
static LONG saved_separation{},saved_screen{};
static constexpr float diagnostic_half_separation=0.032f; // Game units; metre scale not calibrated.
static LONG diagnostic_separation{};
static UINT64 stereo_start_frame{};
static ULONGLONG stereo_start_tick{};
static UINT captured_logic_mask{};
static std::optional<d2vr::GameCameraPose> baseline_pose;
static bool camera_moved=false;
static bool stereo_ever_enabled=false;
static bool gpu_trace_requested=false,gpu_baseline_started=false,gpu_stereo_started=false;
static d2vr::GpuDispatchBindings<> gpu_bindings;
static std::array<bool,7> gpu_deferred_methods_shared{};
static ComPtr<IUnknown> gpu_bound_device_identity,gpu_bound_context_identity;
static DWORD gpu_bound_thread{};
static std::mutex gpu_hook_control_mutex;
static std::atomic<UINT> gpu_phase{0},gpu_traced_frames{0},gpu_events{0},gpu_dropped{0};
static std::atomic<UINT> gpu_callback_depth{0};
static std::array<UINT,4> gpu_draw_samples{};
static std::array<UINT,4> gpu_vertex_draw_samples{};
static std::atomic<ID3D11Device*> gpu_game_device{nullptr};
static bool gpu_settled_started=false;
static unsigned gpu_settled_passes{};
static std::atomic<DWORD> gpu_render_thread{0};
static std::atomic<ID3D11DeviceContext*> gpu_game_context{nullptr};
static std::mutex gpu_mutex;
static std::ofstream gpu_csv;
static std::ofstream gpu_hand_draw_csv;
using HandDrawKey=std::array<std::uint64_t,110>;
static std::array<std::array<HandDrawKey,64>,4> gpu_hand_draw_keys{};
static std::array<unsigned,4> gpu_hand_draw_counts{};
static thread_local bool xr_graphics_active=false;
struct XrGraphicsScope {
    XrGraphicsScope() {xr_graphics_active=true;}
    ~XrGraphicsScope() {xr_graphics_active=false;}
};
struct GpuCallbackDepth {
    GpuCallbackDepth() {gpu_callback_depth.fetch_add(1,std::memory_order_acq_rel);}
    ~GpuCallbackDepth() {gpu_callback_depth.fetch_sub(1,std::memory_order_acq_rel);}
};

HRESULT WINAPI resize_buffers_hook(IDXGISwapChain* chain,UINT count,UINT next_width,UINT next_height,DXGI_FORMAT next_format,UINT flags) {
    if(xr_graphics_active || chain!=game_chain.load(std::memory_order_acquire))
        return original_resize(chain,count,next_width,next_height,next_format,flags);
    // Serialize with the capture's local completed-eye references. DXGI must
    // see every retained backbuffer reference released before it can resize.
    std::lock_guard<std::mutex> capture_lock(log_mutex);
    {
        std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
        paired_backbuffer.Reset();paired_left.Reset();paired_left_ready=false;paired_buffer_binds=0;native_eye_pair.reset();
        eye_sources.invalidate();eye_view_allocation=0;eye_camera_owner={};
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    if(headset_size_requested && SUCCEEDED(chain->GetDesc(&desc))) {
        const auto desired=native_output_size.for_window(desc.OutputWindow);
        if(desired.width) {next_width=desired.width;next_height=desired.height;}
    }
    const auto result=original_resize(chain,count,next_width,next_height,next_format,flags);
    ++native_resizes;native_resize_result.store(result,std::memory_order_release);
    return result;
}

bool observe_game_context(ID3D11DeviceContext* context) noexcept {
    // D3D11 entry points are shared by the game and VDXR. Runtime callbacks
    // must pass through without taking our lock while Present waits for XR.
    return !xr_graphics_active && capturing.load(std::memory_order_acquire) &&
        GetCurrentThreadId()==gpu_render_thread.load(std::memory_order_acquire) &&
        context==gpu_game_context.load(std::memory_order_acquire);
}
bool trace_game_context(ID3D11DeviceContext* context) noexcept {
    if(xr_graphics_active || !capturing.load(std::memory_order_acquire) || !gpu_phase.load() || !context) return false;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    return device.Get()==gpu_game_device.load(std::memory_order_acquire);
}

bool is_paired_backbuffer(ID3D11RenderTargetView* target) {
    if(!target || !paired_backbuffer) return false;
    ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
    return resource.Get()==paired_backbuffer.Get();
}

void observe_paired_target_change(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets) {
    if(!paired_requested || !observe_game_context(context) || count>D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) return;
    std::lock_guard<std::mutex> lock(gpu_mutex);
    if(!capturing.load() || !paired_backbuffer || !paired_left) return;
    const bool next_is_backbuffer=count && targets && is_paired_backbuffer(targets[0]);
    ComPtr<ID3D11RenderTargetView> previous;
    context->OMGetRenderTargets(1,&previous,nullptr);
    const bool was_backbuffer=is_paired_backbuffer(previous.Get());
    if(!native_eye_capture_requested && stereo_active && paired_binds_per_eye && ((was_backbuffer && !next_is_backbuffer) || next_is_backbuffer) &&
       paired_buffer_binds==paired_binds_per_eye && !paired_left_ready) {
        // GPU copy precedes the next eye's commands. The source is only this
        // game's backbuffer; no desktop or unrelated application is captured.
        context->CopyResource(paired_left.Get(),paired_backbuffer.Get());
        paired_left_ready=true;
    }
    // Both eye compositors rebind the SAME buffer, including UI passes. The
    // normal frame establishes the number of bindings in one complete pipeline.
    if(next_is_backbuffer) ++paired_buffer_binds;
}

template<unsigned Binding> void WINAPI viewports_hook(ID3D11DeviceContext* context,UINT count,const D3D11_VIEWPORT* viewports) {
    GpuCallbackDepth depth_guard;
    reinterpret_cast<ViewportsFn>(gpu_bindings.original(0,Binding))(context,count,viewports);
    if(!trace_game_context(context)) return;
    try {
        std::lock_guard<std::mutex> lock(gpu_mutex);
        auto phase=gpu_phase.load();
        if(!phase) return;
        if(count>D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) {callback_failed=true;return;}
        for(UINT i=0;i<count;++i) {
            if(gpu_events>=32000) {++gpu_dropped;continue;}
            ++gpu_events;
            const auto& v=viewports[i];
            gpu_csv << phase << ",viewport," << GetCurrentThreadId() << ',' << reinterpret_cast<std::uintptr_t>(context)
                    << ',' << i << ',' << v.TopLeftX << ',' << v.TopLeftY << ',' << v.Width << ',' << v.Height
                    << ',' << v.MinDepth << ',' << v.MaxDepth << ",0,0,0,0," << context->GetType() << ",0,0,0\n";
        }
    } catch(...) {callback_failed=true;gpu_phase=0;}
}

void trace_targets(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets,ID3D11DepthStencilView* depth,
                   const char* event="target",const D3D11_VIEWPORT* viewport=nullptr,UINT elements=0,UINT instances=0,UINT draw=0,UINT expected_phase=0) noexcept {
    if(!trace_game_context(context)) return;
    try {
        std::lock_guard<std::mutex> lock(gpu_mutex);
        auto phase=gpu_phase.load();
        if(!phase || (expected_phase && phase!=expected_phase)) return;
        if(count>D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) {callback_failed=true;return;}
        for(UINT i=0;i<count;++i) {
            if(viewport && (!targets || !targets[i])) continue;
            if(gpu_events>=32000) {++gpu_dropped;continue;}
            ++gpu_events;
            ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};
            if(targets && targets[i]) {
                targets[i]->GetResource(&resource);
                if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
            }
            const D3D11_VIEWPORT v=viewport?*viewport:D3D11_VIEWPORT{};
            gpu_csv << phase << ',' << event << ',' << GetCurrentThreadId() << ',' << reinterpret_cast<std::uintptr_t>(context)
                    << ',' << i << ',' << v.TopLeftX << ',' << v.TopLeftY << ',' << v.Width << ',' << v.Height << ',' << v.MinDepth << ',' << v.MaxDepth << ',' << reinterpret_cast<std::uintptr_t>(resource.Get())
                    << ',' << desc.Width << ',' << desc.Height << ',' << desc.Format << ',' << context->GetType() << ',' << elements << ',' << instances << ',' << draw << '\n';
        }
        if(depth) {
            if(gpu_events>=32000) {++gpu_dropped;return;}
            ++gpu_events;
            ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};
            depth->GetResource(&resource);if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
            const D3D11_VIEWPORT v=viewport?*viewport:D3D11_VIEWPORT{};
            gpu_csv << phase << ',' << (viewport?std::string(event)+"_depth":"depth") << ',' << GetCurrentThreadId() << ',' << reinterpret_cast<std::uintptr_t>(context)
                    << ",0," << v.TopLeftX << ',' << v.TopLeftY << ',' << v.Width << ',' << v.Height << ',' << v.MinDepth << ',' << v.MaxDepth << ',' << reinterpret_cast<std::uintptr_t>(resource.Get())
                    << ',' << desc.Width << ',' << desc.Height << ',' << desc.Format << ',' << context->GetType() << ',' << elements << ',' << instances << ',' << draw << '\n';
        }
    } catch(...) {callback_failed=true;gpu_phase=0;}
}

template<unsigned Binding> void WINAPI targets_hook(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets,ID3D11DepthStencilView* depth) {
    GpuCallbackDepth depth_guard;
    try {observe_paired_target_change(context,count,targets);} catch(...) {callback_failed=true;}
    reinterpret_cast<TargetsFn>(gpu_bindings.original(1,Binding))(context,count,targets,depth);trace_targets(context,count,targets,depth);
}

template<unsigned Binding> void WINAPI targets_uav_hook(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets,ID3D11DepthStencilView* depth,UINT start,UINT uav_count,ID3D11UnorderedAccessView* const* uavs,const UINT* initial) {
    GpuCallbackDepth depth_guard;
    try {observe_paired_target_change(context,count,targets);} catch(...) {callback_failed=true;}
    reinterpret_cast<TargetsUavFn>(gpu_bindings.original(2,Binding))(context,count,targets,depth,start,uav_count,uavs,initial);
    trace_targets(context,count,targets,depth);
}
void trace_draw(ID3D11DeviceContext* context,const char* event,UINT elements,UINT instances,bool vertices=false,UINT start=0,INT base=0,UINT first=0) noexcept {
    if(!elements || !instances || !trace_game_context(context)) return;
    try {
        UINT draw{},phase{};
        {
            std::lock_guard<std::mutex> lock(gpu_mutex);
            phase=gpu_phase.load();
            if(!phase || phase>=gpu_draw_samples.size()) return;
            auto& samples=vertices?gpu_vertex_draw_samples[phase]:gpu_draw_samples[phase];
            if(samples>=(vertices?256u:1024u)) return;
            draw=++samples+(vertices?0x100000u:0u);
        }
        std::array<ID3D11RenderTargetView*,8> targets{};ComPtr<ID3D11DepthStencilView> depth;
        context->OMGetRenderTargets(static_cast<UINT>(targets.size()),targets.data(),&depth);
        std::array<ComPtr<ID3D11RenderTargetView>,8> owned;
        for(unsigned i=0;i<targets.size();++i) owned[i].Attach(targets[i]);
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
        if(count==1) trace_targets(context,static_cast<UINT>(targets.size()),targets.data(),depth.Get(),event,&viewport,elements,instances,draw,phase);
        constexpr std::array<UINT,8> hand_counts{3696,14013,13452,10353,489,12345,8700,531};
        if(!vertices && std::find(hand_counts.begin(),hand_counts.end(),elements)!=hand_counts.end()) {
            {std::lock_guard<std::mutex> lock(gpu_mutex);if(gpu_hand_draw_counts[phase]>=64 || !gpu_hand_draw_csv.is_open())return;}
            constexpr unsigned slots=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
            std::array<ID3D11Buffer*,slots> buffers{};std::array<UINT,slots> strides{},offsets{};
            context->IAGetVertexBuffers(0,slots,buffers.data(),strides.data(),offsets.data());
            std::array<ComPtr<ID3D11Buffer>,slots> vertex_owned;
            for(unsigned i=0;i<slots;++i)vertex_owned[i].Attach(buffers[i]);
            ComPtr<ID3D11Buffer> index;DXGI_FORMAT idx_format{};UINT index_offset{};
            context->IAGetIndexBuffer(&index,&idx_format,&index_offset);
            D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
            ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
            context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
            D3D11_BUFFER_DESC index_desc{};if(index)index->GetDesc(&index_desc);
            HandDrawKey key{};key[0]=reinterpret_cast<std::uintptr_t>(context);key[1]=elements;key[2]=instances;
            key[3]=start;key[4]=static_cast<std::uint64_t>(static_cast<std::int64_t>(base));key[5]=first;
            key[6]=reinterpret_cast<std::uintptr_t>(index.Get());key[7]=idx_format;key[8]=index_offset;key[9]=topology;
            key[10]=reinterpret_cast<std::uintptr_t>(vs.Get());key[11]=reinterpret_cast<std::uintptr_t>(ps.Get());
            for(unsigned i=0;i<slots;++i){key[12+i*3]=reinterpret_cast<std::uintptr_t>(buffers[i]);key[13+i*3]=strides[i];key[14+i*3]=offsets[i];}
            std::lock_guard<std::mutex> lock(gpu_mutex);auto& used=gpu_hand_draw_counts[phase];
            if(used>=64 || std::find(gpu_hand_draw_keys[phase].begin(),gpu_hand_draw_keys[phase].begin()+used,key)!=gpu_hand_draw_keys[phase].begin()+used)return;
            gpu_hand_draw_keys[phase][used++]=key;
            for(unsigned i=0;i<slots;++i)if(buffers[i] || i==0) {
                D3D11_BUFFER_DESC vertex_desc{};if(buffers[i])buffers[i]->GetDesc(&vertex_desc);
                gpu_hand_draw_csv<<phase<<','<<draw<<','<<used<<",count_candidate_only,"<<event<<','<<GetCurrentThreadId()<<','<<key[0]<<','<<context->GetType()<<','<<elements<<','<<instances<<','<<start<<','<<base<<','<<first<<','<<topology<<','<<key[10]<<','<<key[11]<<','<<key[6]<<','<<idx_format<<','<<index_offset<<','<<index_desc.ByteWidth<<','<<i<<','<<key[12+i*3]<<','<<strides[i]<<','<<offsets[i]<<','<<vertex_desc.ByteWidth<<'\n';
            }
        }
    } catch(...) {callback_failed=true;gpu_phase=0;}
}
template<unsigned Binding> void WINAPI draw_indexed_hook(ID3D11DeviceContext* context,UINT count,UINT start,INT base) {
    GpuCallbackDepth depth_guard;trace_draw(context,"draw_indexed",count,1,false,start,base);
    reinterpret_cast<DrawIndexedFn>(gpu_bindings.original(3,Binding))(context,count,start,base);
}
template<unsigned Binding> void WINAPI draw_instanced_hook(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,INT base,UINT first) {
    GpuCallbackDepth depth_guard;trace_draw(context,"draw_indexed_instanced",count,instances,false,start,base,first);
    reinterpret_cast<DrawIndexedInstancedFn>(gpu_bindings.original(4,Binding))(context,count,instances,start,base,first);
}
template<unsigned Binding> void WINAPI draw_hook(ID3D11DeviceContext* context,UINT count,UINT start) {
    GpuCallbackDepth depth_guard;trace_draw(context,"draw_vertices",count,1,true);
    reinterpret_cast<DrawFn>(gpu_bindings.original(5,Binding))(context,count,start);
}
template<unsigned Binding> void WINAPI draw_vertices_instanced_hook(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,UINT first) {
    GpuCallbackDepth depth_guard;trace_draw(context,"draw_instanced_vertices",count,instances,true);
    reinterpret_cast<DrawInstancedFn>(gpu_bindings.original(6,Binding))(context,count,instances,start,first);
}

bool verified_d3d11_target(void* address) {
    // Validate the actual loaded runtime path once; every target still checks
    // the current module identity and executable image page on every bind.
    static const HMODULE verified_runtime=[]() -> HMODULE {
        const auto runtime=GetModuleHandleW(L"d3d11.dll");
        if(!runtime) return nullptr;
        std::array<wchar_t,32768> module_name{},system_directory{};
        const auto module_length=GetModuleFileNameW(runtime,module_name.data(),static_cast<DWORD>(module_name.size()));
        const auto system_length=GetSystemDirectoryW(system_directory.data(),static_cast<UINT>(system_directory.size()));
        if(!module_length || module_length>=module_name.size() || !system_length || system_length>=system_directory.size()) return nullptr;
        const auto expected=fs::path(system_directory.data())/L"d3d11.dll";
        return _wcsicmp(module_name.data(),expected.c_str())?nullptr:runtime;
    }();
    const auto runtime=GetModuleHandleW(L"d3d11.dll");
    if(!address || !verified_runtime || runtime!=verified_runtime) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(address,&memory,sizeof(memory)) || memory.AllocationBase!=runtime || memory.State!=MEM_COMMIT ||
       memory.Type!=MEM_IMAGE || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto protection=memory.Protect&0xff;
    return protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}

void ensure_game_gpu_hooks(ID3D11Device* device,ID3D11DeviceContext* context) {
    constexpr std::array<unsigned,7> slots{44,33,34,12,20,13,21};
    const auto snapshot=[&](d2vr::GpuDispatchBindings<>::Targets& result) {
        const auto table=*reinterpret_cast<void***>(context);
        for(unsigned i=0;i<slots.size();++i) result[i]=table[slots[i]];
        return true;
    };
    d2vr::GpuDispatchBindings<>::Targets addresses{};snapshot(addresses);
    std::lock_guard<std::mutex> control(gpu_hook_control_mutex);
    if(!capturing.load(std::memory_order_acquire)) return; // Cleanup cannot be followed by a late Present re-enable.
    // Verify canonical identities independently of whether XR owns this session.
    ComPtr<ID3D11Device> context_device,chain_device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<IUnknown> device_identity,context_identity,context_device_identity,chain_device_identity;
    auto* chain=game_chain.load(std::memory_order_acquire);
    if(!device || !context || !chain || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE ||
       FAILED(device->QueryInterface(IID_PPV_ARGS(&device_identity))) ||
       FAILED(context->QueryInterface(IID_PPV_ARGS(&context_identity))) ||
       FAILED(chain->GetDevice(IID_PPV_ARGS(&chain_device))))
        throw std::runtime_error("Cannot verify canonical GPU observer owner");
    context->GetDevice(&context_device);device->GetImmediateContext(&immediate);
    if(!context_device || FAILED(context_device->QueryInterface(IID_PPV_ARGS(&context_device_identity))) ||
       FAILED(chain_device->QueryInterface(IID_PPV_ARGS(&chain_device_identity))) || immediate.Get()!=context ||
       context_device_identity.Get()!=device_identity.Get() || chain_device_identity.Get()!=device_identity.Get() ||
       GetCurrentThreadId()!=gpu_render_thread.load(std::memory_order_acquire))
        throw std::runtime_error("GPU observer context/device/thread ownership differs");
    if(gpu_bound_device_identity && (gpu_bound_device_identity.Get()!=device_identity.Get() || gpu_bound_context_identity.Get()!=context_identity.Get() || gpu_bound_thread!=GetCurrentThreadId()))
        throw std::runtime_error("GPU observer canonical owner changed before cleanup");
    if(graphics_owner.bound()) {
        std::lock_guard<std::mutex> owner_lock(log_mutex);
        graphics_owner.bind_or_verify(chain,device,context,resize_target);
    }
    gpu_bound_device_identity=device_identity;gpu_bound_context_identity=context_identity;gpu_bound_thread=GetCurrentThreadId();
#define D2VR_GPU_WRAPPERS(Name) {{reinterpret_cast<void*>(&Name<0>),reinterpret_cast<void*>(&Name<1>),reinterpret_cast<void*>(&Name<2>),reinterpret_cast<void*>(&Name<3>)}}
    const d2vr::GpuDispatchBindings<>::Wrappers replacements{{
        D2VR_GPU_WRAPPERS(viewports_hook),D2VR_GPU_WRAPPERS(targets_hook),D2VR_GPU_WRAPPERS(targets_uav_hook),
        D2VR_GPU_WRAPPERS(draw_indexed_hook),D2VR_GPU_WRAPPERS(draw_instanced_hook),D2VR_GPU_WRAPPERS(draw_hook),
        D2VR_GPU_WRAPPERS(draw_vertices_instanced_hook)}};
#undef D2VR_GPU_WRAPPERS
    const auto before=gpu_bindings.active();
    const auto read=[](void* address,void* bytes,std::size_t size) {
        SIZE_T count{};return ReadProcessMemory(GetCurrentProcess(),address,bytes,size,&count) && count==size;
    };
    const bool accepted=gpu_bindings.bind(addresses,replacements,read,&verified_d3d11_target,snapshot);
    if(before!=addresses || !accepted) {
        std::ofstream evidence(output_directory/L"native-context-dispatch-change.jsonl",std::ios::app);
        evidence<<"{\"present\":"<<frames.load()<<",\"thread\":"<<GetCurrentThreadId()
            <<",\"context\":"<<reinterpret_cast<std::uintptr_t>(context)
            <<",\"vtable\":"<<reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void***>(context))
            <<",\"accepted\":"<<(accepted?"true":"false")<<",\"reason\":"<<std::quoted(gpu_bindings.reason())<<",\"slots\":[";
        for(unsigned i=0;i<slots.size();++i) {
            if(i) evidence<<',';
            evidence<<"{\"slot\":"<<slots[i]<<",\"before\":"<<reinterpret_cast<std::uintptr_t>(before[i])
                <<",\"after\":"<<reinterpret_cast<std::uintptr_t>(addresses[i])<<'}';
        }
        evidence<<"]}\n";
        if(!evidence) throw std::runtime_error("Cannot record GPU dispatch transition");
    }
    if(!accepted) throw std::runtime_error(gpu_bindings.reason());
    if(std::strcmp(gpu_bindings.reason(),"unchanged")==0) return;
    ComPtr<ID3D11DeviceContext> deferred;gpu_deferred_methods_shared={};
    if(SUCCEEDED(device->CreateDeferredContext(0,&deferred))) {
        const auto deferred_table=*reinterpret_cast<void***>(deferred.Get());
        for(unsigned i=0;i<slots.size();++i) gpu_deferred_methods_shared[i]=deferred_table[slots[i]]==addresses[i];
    }
}

bool disable_gpu_hooks() noexcept {
    std::lock_guard<std::mutex> control(gpu_hook_control_mutex);
    gpu_phase=0;
    const auto read=[](void* address,void* bytes,std::size_t size) {
        SIZE_T count{};return ReadProcessMemory(GetCurrentProcess(),address,bytes,size,&count) && count==size;
    };
    const bool disabled=gpu_bindings.disable_all(gpu_callback_depth,read);
    if(disabled) {gpu_bound_device_identity.Reset();gpu_bound_context_identity.Reset();gpu_bound_thread=0;}
    return disabled;
}

bool read_self(std::uintptr_t address,void* data,std::size_t size) {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),data,size,&count) && count==size;
}

bool write_self_aim(std::uintptr_t address,const void* data,std::size_t size) noexcept {
    if(!address || !data || size!=sizeof(d2vr::GameplayAim)) return false;
    __try {std::memcpy(reinterpret_cast<void*>(address),data,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool exchange_cached_value(std::uintptr_t address,std::uint64_t before,std::uint64_t after) noexcept {
    if(address%8) return false;
    __try {
        return static_cast<std::uint64_t>(InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(address),static_cast<LONG64>(after),static_cast<LONG64>(before)))==before;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool read_self(std::uintptr_t address,void* data,std::size_t size);
static auto read_self_ref=&read_self;
bool restore_camera_comfort() noexcept {
    native_cvars.restore();
    try {
        auto reader=&read_self;auto exchange=&exchange_cached_value;
        comfort_restored=camera_comfort.restore(reader,exchange);return comfort_restored;
    } catch(...) {comfort_restored=false;return false;}
}

void invoke_arm_upload(void* model,const void* source,UINT count) {
    d2vr::CrashOperation operation("native arm consumer");
    // 0x25efe0 eagerly packs all 69 source transforms into its own stack buffer
    // before skinning/attachment updates. The caller-owned copy survives that
    // synchronous call. Never replace the native animation's source allocation.
    alignas(16) std::array<d2vr::rig::Transform,69> palette{};
    const void* submitted=source;
    const auto snapshot=arm_binding_cache.load();
    unsigned side=2;
    if(capturing.load(std::memory_order_acquire) && (arm_copy_requested || tracked_arms_requested) && snapshot)
        side=snapshot->side_for_model(reinterpret_cast<std::uintptr_t>(model)).value_or(2);
    if(side<2) {
        try {
            auto reader=&read_self;
            const auto native=d2vr::rig::arm_upload(reader,image_base,snapshot->arms[side],reinterpret_cast<std::uintptr_t>(source),count);
            if(native) {
                {std::lock_guard<std::mutex> origin_lock(arm_origin_mutex);
                 arm_model_origin={native->model_frame[0],native->model_frame[1],native->model_frame[2]};arm_model_origin_frame=frames.load();
                 if(arm_origin_csv.is_open()) arm_origin_csv << "upload," << arm_model_origin_frame << ',' << side << ',' << arm_model_origin.x << ',' << arm_model_origin.y << ',' << arm_model_origin.z << '\n';}
                if(tracked_arms_requested) {
                    const bool gate=avatar_pose_gate.load(std::memory_order_acquire);
                    const auto now=GetTickCount64();
                    const auto posed=gate?arm_tracking.pose(*native,snapshot->bindings[side],side,now):std::nullopt;
                    bool reused=false;
                    if(!posed && gate && !hands_native.load()) {
                        std::lock_guard<std::mutex> last_arm_lock(last_arm_mutex);
                        if(last_arm_tick[side] && now-last_arm_tick[side]<150) {palette=last_arm_palette[side];submitted=palette.data();reused=true;}
                    }
                    if(posed) {
                        palette=posed->joints;submitted=palette.data();++arm_pose_calls[side];
                        {std::lock_guard<std::mutex> last_arm_lock(last_arm_mutex);last_arm_palette[side]=palette;last_arm_tick[side]=now;}
                        if(attachment_pose_installed) attachment_pose.publish(side,snapshot->arms[side].animation,reinterpret_cast<std::uintptr_t>(source),palette);
                        if(posed->reach_clamped) ++arm_pose_clamps[side];
                    } else if(reused) {
                        if(attachment_pose_installed) attachment_pose.publish(side,snapshot->arms[side].animation,reinterpret_cast<std::uintptr_t>(source),palette);
                    } else ++arm_pose_fallbacks[side];
                } else {
                    palette=native->joints;submitted=palette.data();++arm_copy_calls[side];
                }
            } else ++arm_pose_fallbacks[side];
        } catch(...) {++arm_pose_fallbacks[side];}
    }
    // Exactly one native call, outside all mailbox/observer mutexes. Native
    // faults propagate through the outer depth guard; never retry the consumer.
    original_arm_upload(model,submitted,count);
}

void observe_arm_upload(void* model,const void* source,UINT count,std::uintptr_t caller) noexcept {
    if(!capturing.load(std::memory_order_acquire)) return;
    const auto snapshot=arm_binding_cache.load();
    const auto side=snapshot?snapshot->side_for_model(reinterpret_cast<std::uintptr_t>(model)).value_or(2):2;
    if(side==2) return;
    try {
        std::lock_guard<std::mutex> lock(arm_log_mutex);
        if(!capturing.load()) return;
        auto reader=&read_self;
        const auto upload=d2vr::rig::arm_upload(reader,image_base,snapshot->arms[side],reinterpret_cast<std::uintptr_t>(source),count);
        if(!upload) {++arm_upload_rejected[side];return;}
        if(!avatar_rig_snapshot || avatar_rig_snapshot!=snapshot) {avatar_uploads={};avatar_rig_snapshot=snapshot;}
        avatar_uploads[side]=*upload;
        const auto receipt=native_weapon_attachment_requested?
            native_weapon_attachment.match_upload(snapshot->arms[side],reinterpret_cast<std::uintptr_t>(source),upload->model_frame,caller):d2vr::NativeWeaponSourceMatch{};
        if(arm_upload_samples[side]>=100 || arm_upload_seen[side]>=400) return;
        ++arm_upload_seen[side];
        const auto offset=arm_palettes.tellp();
        arm_palettes.write(reinterpret_cast<const char*>(upload->joints.data()),sizeof(upload->joints));
        arm_palettes.write(reinterpret_cast<const char*>(upload->model_frame.data()),sizeof(upload->model_frame));
        arm_csv<<frames.load()<<','<<GetTickCount64()<<','<<GetCurrentThreadId()<<','<<side<<','
               <<snapshot->arms[side].animation<<','<<snapshot->arms[side].model<<','<<reinterpret_cast<std::uintptr_t>(source)
               <<','<<count<<','<<upload->first_transform<<','<<offset<<','<<receipt.receipt<<','<<receipt.native_frame<<','
               <<receipt.receipt_frame<<','<<receipt.producer_thread<<','<<receipt.epoch<<','<<(receipt.matched?"true":"false")<<','
               <<receipt.reason<<','<<receipt.game<<','<<receipt.receipt_source<<','<<receipt.receipt_remap<<','<<receipt.receipt_transform_count<<','
               <<receipt.producer_caller_rva<<','<<receipt.producer_stage<<','<<receipt.model.valid<<','<<receipt.receipt_model.valid<<','
               <<(receipt.model_matched && !std::memcmp(upload->model_frame.data(),receipt.model.values.data(),sizeof(upload->model_frame)))<<','
               <<receipt.tracking.revision<<','<<receipt.receipt_tracking.revision<<','<<receipt.tracking.epoch<<','
               <<receipt.tracking.reference_epoch<<','<<receipt.tracking.hands<<','<<receipt.tracking.calibrated<<','<<receipt.tracking_matched;
        for(float f:upload->model_frame) arm_csv<<','<<std::setprecision(9)<<f;
        for(float f:receipt.receipt_model.values) arm_csv<<','<<f;
        arm_csv<<'\n';
        if(!arm_palettes || !arm_csv) throw std::runtime_error("Cannot save native arm upload sample");
        ++arm_upload_samples[side];
    } catch(const std::exception& error) {record_callback_error(error.what());}
    catch(...) {callback_failed=true;}
}

bool disable_arm_upload_hook() noexcept {
    if(!original_arm_upload) return true;
    if(arm_upload_enabled) {
        if(MH_DisableHook(reinterpret_cast<void*>(image_base+arm_upload_rva))!=MH_OK) return false;
        arm_upload_enabled=false;
    }
    const auto deadline=GetTickCount64()+1000;
    while(arm_upload_depth.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
    if(arm_upload_depth.load()) return false;
    std::array<std::byte,32> bytes{};
    return read_self(image_base+arm_upload_rva,bytes.data(),bytes.size()) && bytes==arm_upload_expected_bytes;
}

bool disable_resolution_hooks() noexcept {
    native_output_size.clear();
    cached_output_restored=cached_output_size.restore();
    bool restored=true;
    if(output_size_enabled) {
        auto target=reinterpret_cast<void*>(image_base+output_size_rva);
        if(MH_DisableHook(target)!=MH_OK) restored=false;
        else {
            output_size_enabled=false;
            std::array<std::byte,32> bytes{};
            restored=read_self(image_base+output_size_rva,bytes.data(),bytes.size()) && bytes==output_size_expected_bytes && restored;
        }
    }
    if(resize_enabled) {
        if(MH_DisableHook(resize_target)!=MH_OK) restored=false;
        else {
            resize_enabled=false;
            std::array<std::byte,16> bytes{};
            restored=read_self(reinterpret_cast<std::uintptr_t>(resize_target),bytes.data(),bytes.size()) && bytes==resize_original_bytes && restored;
        }
    }
    return restored;
}

UINT game_logic_mask() {
    // MIT headtracking profile, corroborated against the live engine manager.
    // Fresh reads make menus/loading visible without foreground-key heuristics.
    std::uintptr_t game{},vtable{};int state{};
    if(!read_self(image_base+0x39a5290,&game,8) || !game ||
       !read_self(game,&vtable,8) || vtable!=image_base+0x1c940a8 ||
       !read_self(game+0x341058,&state,4) || state!=3) return 0;
    const auto manager=game+0x1f6ec8;
    std::array<std::uintptr_t,0x8b8/8> entries{};
    if(!read_self(manager,entries.data(),sizeof(entries))) return 0;
    UINT mask{};
    for(auto entry:entries) {
        if(!entry) continue;
        std::array<std::byte,0x41> bytes{};
        if(!read_self(entry,bytes.data(),bytes.size())) continue;
        std::uintptr_t backref{};int id{};
        std::memcpy(&backref,bytes.data()+8,8);std::memcpy(&id,bytes.data()+0x18,4);
        if(backref==manager && id>0 && id<17 && bytes[0x40]==std::byte{1}) mask|=1u<<id;
    }
    return mask;
}

void trace_avatar_fit(const char* reason,UINT64 present,const std::shared_ptr<const d2vr::rig::NativeArmBindingSnapshot>& rig) {
    if(!game_xr || !avatar_fit_events_csv.is_open())return;
    d2vr::rig::AvatarFitEventState state{};state.tick=GetTickCount64();state.present=present;
    state.reference_epoch=game_xr->reference_epoch();state.pose_epoch=game_xr->pose_epoch();state.logic_mask=game_logic_mask();
    state.accepted_epoch=avatar_fit_reuse.accepted_reference_epoch();state.accepted=avatar_fit_reuse.has_accepted_fit();
    const auto active=avatar_fit_reuse.active_fit();state.active=active.has_value();state.gate=avatar_pose_gate.load();
    state.pending=avatar_resource_validation_pending;state.template_verified=current_avatar_template.has_value();
    auto status=d2vr::AvatarFitRuntimeStatus::unmeasured;
    // Scripted simulations skip the one-time calibration prompt; hands work uncalibrated.
    if(tracked_arms_requested && !simulated_gameplay_script_requested && avatar_fit_reuse.recalibration_required())status=d2vr::AvatarFitRuntimeStatus::recalibration_required;
    else if(!rig || !current_avatar_template || state.pending || (state.accepted && (!state.active || !state.gate)) || state.logic_mask==0 || state.logic_mask==(1u<<15))status=d2vr::AvatarFitRuntimeStatus::verifying;
    else if(state.active && state.gate)status=d2vr::AvatarFitRuntimeStatus::active;
    game_xr->avatar_runtime_status(status);state.runtime_status=static_cast<unsigned>(status);
    state.wizard_state=static_cast<unsigned>(game_xr->avatar_calibration_state());
    if(rig)state.owners=rig->arms;
    if(current_avatar_template)for(unsigned side=0;side<2;++side){const auto& mesh=current_avatar_template->arms[side].mesh;
        state.resource_sources[side]=mesh.source;state.resource_primaries[side]=mesh.primary;
        state.surface_identity[side]=d2vr::rig::avatar_surface_diagnostic_id(mesh);state.surface_counts[side]=static_cast<unsigned>(mesh.surfaces.size());}
    state.accepts=avatar_fit_accepts;state.reuses=avatar_fit_reuses;state.invalidations=avatar_fit_invalidations;state.suspensions=avatar_fit_suspensions;
    state.native_palm_span=current_avatar_template?current_avatar_template->measurements.palm_span_metres:0;
    state.native_shoulder_span=current_avatar_template?current_avatar_template->measurements.shoulder_span_metres:0;
    if(active){state.arm_scale=active->arm_scale;state.shoulder_drop=active->eye_to_shoulder_metres;}
    avatar_fit_events.change(avatar_fit_events_csv,state,reason);
    if(!avatar_fit_events_csv)throw std::runtime_error("Cannot save avatar fit change events");
}

struct NativeView {
    std::array<int,4> viewport{};
    std::optional<d2vr::GameCameraPose> pose;
};

std::vector<NativeView> native_views(std::array<std::byte,16>* captured_header=nullptr) {
    std::array<std::byte,16> header{};
    if(!read_self(image_base+d2vr::prepared_views_rva,header.data(),header.size())) return {};
    std::uintptr_t address{};std::uint32_t packed{};
    std::memcpy(&address,header.data(),8);std::memcpy(&packed,header.data()+8,4);
    auto count=packed&0xffffff;
    if(!address || address%8 || count<1 || count>2) return {};
    std::vector<NativeView> result;
    for(std::uint32_t eye=0;eye<count;++eye) {
        std::array<std::byte,d2vr::camera_read_size> bytes{};
        if(!read_self(address+eye*d2vr::prepared_view_stride,bytes.data(),bytes.size())) return {};
        NativeView view;
        std::memcpy(view.viewport.data(),bytes.data(),16);
        view.pose=d2vr::decode_view_fields(bytes);
        if(!view.pose) return {};
        result.push_back(view);
    }
    if(captured_header) {
        std::array<std::byte,16> verify{};
        if(!read_self(image_base+d2vr::prepared_views_rva,verify.data(),verify.size()) || verify!=header) return {};
        *captured_header=header;
    }
    return result;
}

void observe_eye_setup(void* view) noexcept {
    if(!eye_boundary_requested || !capturing.load() || !view || !stereo_active.load()) return;
    try {
        std::array<std::byte,d2vr::camera_read_size> bytes{};
        if(!read_self(reinterpret_cast<std::uintptr_t>(view),bytes.data(),bytes.size())) return;
        const auto pose=d2vr::decode_render_view(bytes);
        std::array<std::byte,16> classified_header{};
        const auto native=native_views(&classified_header);
        if(!pose || native.size()!=2) return;
        int eye=-1;
        for(int i=0;i<2;++i) {
            float difference{};
            for(int axis=0;axis<3;++axis) difference+=std::abs(pose->position[axis]-native[i].pose->position[axis]);
            if(difference<.001f) {if(eye>=0) return;eye=i;}
        }
        if(eye<0) return;
        const auto address=reinterpret_cast<std::uintptr_t>(view);
        {
            std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
            if(game_logic_mask()!=2) {eye_sources.invalidate();native_eye_pair.reset();return;}
            std::array<std::byte,16> header{},verify{};
            if(!read_self(image_base+d2vr::prepared_views_rva,header.data(),header.size()) || header!=classified_header) {
                eye_sources.invalidate();native_eye_pair.reset();return;
            }
            std::uintptr_t allocation{};std::memcpy(&allocation,header.data(),8);
            const auto owner=d2vr::find_native_player_camera(read_self,image_base);
            if(!owner || !read_self(image_base+d2vr::prepared_views_rva,verify.data(),verify.size()) || verify!=header) {
                eye_sources.invalidate();native_eye_pair.reset();return;
            }
            const std::array<UINT64,3> identity{owner->player,owner->camera,static_cast<std::uint32_t>(owner->camera_identity.generation)};
            if(allocation!=eye_view_allocation || identity!=eye_camera_owner) {
                eye_sources.invalidate();native_eye_pair.reset();eye_view_allocation=allocation;eye_camera_owner=identity;
            }
            if(!eye_sources.publish(static_cast<unsigned>(eye),address,frames.load(),GetCurrentThreadId(),gpu_render_thread.load())) return;
            if(taa_history_installed) taa_history.publish_eye_view(static_cast<unsigned>(eye),address);
        }
        std::lock_guard<std::mutex> lock(log_mutex);
        if(capturing.load() && eye_boundary_rows<1500) {
            ++eye_boundary_rows;
            eye_boundary_csv << "setup," << frames.load() << ',' << GetCurrentThreadId() << ',' << address
                             << ',' << eye << ",0,0\n";
        }
    } catch(...) {callback_failed=true;}
}

void observe_eye_begin(void* view) noexcept {
    if(!eye_boundary_requested || !capturing.load()) return;
    try {
        ++eye_begin_calls;
        const auto address=reinterpret_cast<std::uintptr_t>(view);
        int eye=-1;
        UINT bindings{};bool same_context{};
        {
            std::lock_guard<std::mutex> lock(gpu_mutex);
            bindings=paired_buffer_binds;
            same_context=GetCurrentThreadId()==gpu_render_thread.load();
            if(game_logic_mask()!=2) {eye_sources.invalidate();native_eye_pair.reset();}
            // Actual native trace: Setup N is consumed by Begin N+1.
            // Qualify that exact pipeline stage, not an arbitrary older view.
            else if(const auto frame=frames.load();frame>0)
                eye=eye_sources.match(address,frame-1,GetCurrentThreadId(),gpu_render_thread.load());
            if(eye>=0) {
                std::uintptr_t allocation{};
                const auto owner=d2vr::find_native_player_camera(read_self,image_base);
                const std::array<UINT64,3> identity=owner?std::array<UINT64,3>{owner->player,owner->camera,static_cast<std::uint32_t>(owner->camera_identity.generation)}:std::array<UINT64,3>{};
                if(!owner || !read_self(image_base+d2vr::prepared_views_rva,&allocation,8) || allocation!=eye_view_allocation || identity!=eye_camera_owner) {
                    eye_sources.invalidate();native_eye_pair.reset();eye=-1;
                }
            }
            if(eye>=0) ++eye_begin_matches;
            if(native_eye_capture_requested && same_context && stereo_active.load() &&
               native_eye_pair.begin(eye) && paired_left && paired_backbuffer) {
                auto context=gpu_game_context.load(std::memory_order_acquire);
                if(context) {
                    // The previous native main-view's world and GUI commands
                    // are complete, before this second view can overwrite it.
                    context->CopyResource(paired_left.Get(),paired_backbuffer.Get());
                    paired_left_ready=true;++native_eye_copy_count;
                }
            }
        }
        // Right-eye private TAA history images are created/validated here, on the
        // render thread before the left eye records, outside our own locks.
        if(taa_history_installed && eye==0) {try {taa_history.maintain();} catch(...) {}}
        std::lock_guard<std::mutex> lock(log_mutex);
        if(capturing.load() && eye_boundary_rows<1500) {
            ++eye_boundary_rows;
            eye_boundary_csv << "begin," << frames.load() << ',' << GetCurrentThreadId() << ',' << address
                             << ',' << eye << ',' << bindings << ',' << (same_context?1:0) << '\n';
        }
    } catch(...) {callback_failed=true;}
}

bool disable_eye_boundary_hook() noexcept {
    if(!eye_boundary_enabled) return true;
    auto target=reinterpret_cast<void*>(image_base+eye_begin_rva);
    if(MH_DisableHook(target)!=MH_OK) return false;
    eye_boundary_enabled=false;
    std::array<std::byte,32> bytes{};
    return read_self(image_base+eye_begin_rva,bytes.data(),bytes.size()) && bytes==eye_begin_expected_bytes;
}

void prepare_head_view(void* view) noexcept {
    head_wrote=false;++head_setup_seen;
    if(GetCurrentThreadId()!=xr_present_thread) return;
    if(!capturing.load() || !view) {native_left_aim.clear();native_weapon_attachment.clear();return;}
    try {
        std::lock_guard<std::mutex> lock(log_mutex);
        if(!capturing.load() || !game_xr) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        if(head_pose_epoch!=game_xr->pose_epoch()) {
            native_left_aim.clear();native_weapon_attachment.clear();head_pose_epoch=game_xr->pose_epoch();head_body_anchor.clear();
        }
        if(game_logic_mask()!=2) {native_left_aim.clear();native_weapon_attachment.clear();head_body_anchor.clear();return;}
        auto xr_views=game_xr->tracking_views();
        if(!xr_views) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        std::array<std::byte,0x144> bytes{};
        auto source=reinterpret_cast<std::uintptr_t>(view);
        if(!read_self(source,bytes.data(),bytes.size())) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        std::array<std::byte,d2vr::camera_read_size> prefix{};std::memcpy(prefix.data(),bytes.data(),prefix.size());
        auto input=d2vr::decode_render_view(prefix);auto native=native_views();
        if(!input || native.size()!=2) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        int eye=-1;
        for(int i=0;i<2;++i) {
            float difference{};
            for(int axis=0;axis<3;++axis) difference+=std::abs(input->position[axis]-native[i].pose->position[axis]);
            if(difference<.001f) {if(eye>=0) {native_left_aim.clear();native_weapon_attachment.clear();return;}eye=i;}
        }
        if(eye<0) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        ++head_matched;
        std::array<float,16> projection{};std::memcpy(projection.data(),bytes.data()+0x100,64);
        const auto& xr_eye=(*xr_views)[eye];
        d2vr::EyeFov render_fov{xr_eye.fov.angleLeft,xr_eye.fov.angleRight,xr_eye.fov.angleUp,xr_eye.fov.angleDown};
        if(symmetric_frustum_requested) {
            auto symmetric=d2vr::enclosing_symmetric_fov(render_fov);
            if(!symmetric) {native_left_aim.clear();native_weapon_attachment.clear();return;}
            render_fov=*symmetric;
        }
        auto optical=d2vr::eye_projection(projection,render_fov);
        if(!optical) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        d2vr::Vec3 centre{
            ((*xr_views)[0].pose.position.x+(*xr_views)[1].pose.position.x)*.5,
            ((*xr_views)[0].pose.position.y+(*xr_views)[1].pose.position.y)*.5,
            ((*xr_views)[0].pose.position.z+(*xr_views)[1].pose.position.z)*.5};
        d2vr::Quaternion orientation{xr_eye.pose.orientation.x,xr_eye.pose.orientation.y,xr_eye.pose.orientation.z,xr_eye.pose.orientation.w};
        auto body=d2vr::native_player_camera(read_self,image_base,head_body_camera);
        if(!body) {body=d2vr::find_native_player_camera(read_self,image_base);if(body) head_body_camera=body->camera;}
        if(!body) {native_left_aim.clear();native_weapon_attachment.clear();head_body_anchor.clear();return;}
        auto source_anchor=*input;
        for(int axis=0;axis<3;++axis)
            source_anchor.position[axis]=(native[0].pose->position[axis]+native[1].pose->position[axis])*.5f;
        const auto compensation=native_turning_requested?native_turning.head_compensation(game_xr->reference_epoch()):0;
        const auto owned_anchor=head_body_anchor.get(frames.load(),head_pose_epoch,*body,source_anchor,compensation);
        if(!owned_anchor) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        const auto& anchor=*owned_anchor;
        const auto& left_pose=(*xr_views)[0].pose;const auto& right_pose=(*xr_views)[1].pose;
        const auto centre_pose=d2vr::eye_centre_pose(
            {{left_pose.position.x,left_pose.position.y,left_pose.position.z},{left_pose.orientation.x,left_pose.orientation.y,left_pose.orientation.z,left_pose.orientation.w}},
            {{right_pose.position.x,right_pose.position.y,right_pose.position.z},{right_pose.orientation.x,right_pose.orientation.y,right_pose.orientation.z,right_pose.orientation.w}});
        const auto calibration=head_calibration.get(centre_pose,game_xr->reference_epoch(),body->player,body->camera,body->camera_identity.generation,true);
        if(!calibration) {native_left_aim.clear();native_weapon_attachment.clear();return;}
        head_origin=*calibration;
        // 6DoF: physical head motion moves the render camera within a comfortable
        // volume around the calibrated origin; hands shift by the same offset so
        // they stay on the controllers. Native collision still uses the game camera.
        auto head_offset=centre-head_origin.position;
        if(const double h=std::hypot(head_offset.x,head_offset.z);h>.45) {head_offset.x*=.45/h;head_offset.z*=.45/h;}
        head_offset.y=std::clamp(head_offset.y,-.7,.3);
        const auto hand_centre=centre-head_offset;
        if(native_turning_requested && eye==0)
            native_turning.publish_head(d2vr::tracked_yaw(centre_pose.orientation,head_origin.orientation),GetTickCount64(),
                game_xr->reference_epoch(),*body,game_xr->state==XR_SESSION_STATE_FOCUSED && !body->authored_view());
        if(physical_grab_requested && tracked_arms_requested && !avatar_pose_gate.load(std::memory_order_acquire)) native_grab.clear();
        if(physical_grab_requested && eye==0 && (!tracked_arms_requested || avatar_pose_gate.load(std::memory_order_acquire))) {
            if(simulated_input_requested && !simulated_gameplay_script_requested && simulated_grab_target) {
                const auto elapsed=simulated_grab_clock.sample(GetTickCount64()-simulated_input_start,
                    tracked_arms_requested,avatar_pose_gate.load(std::memory_order_acquire),true).value_or(0);
                const bool refresh_second=elapsed>=3750 && !simulated_grab_repositioned &&
                    game_xr->controller_input.right_grip<.35f && native_grab.picked()<=native_grab.dropped()+native_grab.lost();
                if(!simulated_grab_choice.decided() || refresh_second) {
                    const auto current=d2vr::native_rigid_body(read_self,image_base,simulated_grab_target->entity);
                    if(!current || current->identity!=simulated_grab_target->identity ||
                       current->component!=simulated_grab_target->component || current->handle!=simulated_grab_target->handle ||
                       current->model!=simulated_grab_target->model || current->world!=simulated_grab_target->world || !current->bounds)
                        throw std::runtime_error("Controlled surface fixture lost native body identity/bounds");
                    simulated_grab_target=current;
                    if(refresh_second) simulated_grab_repositioned=true;
                }
                if(!simulated_grab_target->bounds) throw std::runtime_error("Controlled surface fixture lost verified bounds");
                if(!simulated_grab_choice.decided()) {
                    if(game_xr->state!=XR_SESSION_STATE_FOCUSED || game_xr->last_action_sync!=XR_SUCCESS || !(game_xr->grip_tracked_hands&2)) return;
                    const auto& p=game_xr->grip_locations[1].pose;
                    const auto ordinary=d2vr::to_game_pose(d2vr::camera_relative_controller(
                        {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},
                        hand_centre,head_origin.position),head_origin,anchor,1.0);
                    simulated_grab_choice.choose(simulated_grab_target->contact_distance(ordinary.position));
                    simulated_grab_ordinary_pose=p;
                }
                const auto fixture_frame=frames.load();
                if(fixture_frame!=simulated_grab_last_frame) {
                    simulated_grab_last_frame=fixture_frame;
                    if(simulated_grab_choice.ordinary()) ++simulated_grab_ordinary_frames;else ++simulated_grab_ranged_frames;
                }
                const auto lift=elapsed<3000?std::clamp((double(elapsed)-1500)/1000,0.,1.)*.25:
                    std::clamp((double(elapsed)-4000)/1000,0.,1.)*.25;
                if(simulated_grab_choice.ordinary()) {
                    d2vr::simxr::control.grips[1]=simulated_grab_ordinary_pose;
                    d2vr::simxr::control.grips[1].position.y+=static_cast<float>(lift);
                } else {
                auto query=simulated_grab_target->pose.position;query.z=simulated_grab_target->bounds->maximum.z+.28;
                auto target=simulated_grab_target->bounds->closest(query);target.z+=.28;
                target.z+=lift;
                const d2vr::Vec3 centre_world{anchor.position[0],anchor.position[1],anchor.position[2]};
                const auto delta=target-centre_world;
                const d2vr::Vec3 local{-d2vr::dot(delta,{anchor.basis[3],anchor.basis[4],anchor.basis[5]}),
                    d2vr::dot(delta,{anchor.basis[6],anchor.basis[7],anchor.basis[8]}),
                    -d2vr::dot(delta,{anchor.basis[0],anchor.basis[1],anchor.basis[2]})};
                const auto position=head_origin.position+d2vr::rotated(head_origin.orientation,local);
                d2vr::simxr::control.grips[1].position={static_cast<float>(position.x),static_cast<float>(position.y),static_cast<float>(position.z)};
                }
            }
            for(unsigned side=0;side<2;++side) {
                const bool tracked=game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS && (game_xr->grip_tracked_hands&(1u<<side));
                d2vr::WorldTrackedPose hand{};
                if(tracked) {
                    const auto& p=game_xr->grip_locations[side].pose;
                    hand=d2vr::to_game_pose(d2vr::camera_relative_controller(
                        {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},hand_centre,head_origin.position),head_origin,anchor,1.0);
                }
                bool occupied{};{std::lock_guard<std::mutex> hand_lock(hand_state_mutex);
                    occupied=hand_state_tick && GetTickCount64()-hand_state_tick<=300 && (hand_item_bits&(1u<<side));}
                native_grab.publish(side,*body,hand,side?game_xr->controller_input.right_grip:game_xr->controller_input.left_grip,
                    game_xr->pose_epoch(),GetTickCount64(),tracked,occupied);
            }
        }
        if(controller_aim_requested && eye==0) {
            constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|
                XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            // The hand held out ahead of the head is the pointing hand (dwell + hysteresis, latched while a trigger is held).
            {
                const auto forward=d2vr::rotated(orientation,{0,0,-1});
                std::array<bool,2> valid{};std::array<double,2> forwardness{};
                for(unsigned side=0;side<2;++side) {
                    const auto& location=game_xr->aim_locations[side];
                    valid[side]=(location.locationFlags&required)==required;
                    const auto& q=location.pose.orientation;
                    // Pointing = aimed along the view AND reached out: a hand pushed 20 cm further
                    // forward outweighs a slightly better aimed hand resting at the hip.
                    const d2vr::Vec3 hand{location.pose.position.x,location.pose.position.y,location.pose.position.z};
                    forwardness[side]=valid[side]?d2vr::dot(d2vr::rotated({q.x,q.y,q.z,q.w},{0,0,-1}),forward)+2.5*d2vr::dot(hand-centre,forward):-1;
                }
                const auto& raw=game_xr->controller_input;
                // A fresh grip or trigger press on a hand makes it the pointing hand at once.
                static std::array<bool,2> was_pressed{};
                const std::array<bool,2> pressing{raw.left_grip>.55f || raw.left_trigger>.55f,raw.right_grip>.55f || raw.right_trigger>.55f};
                const std::array<bool,2> fresh{pressing[0] && !was_pressed[0],pressing[1] && !was_pressed[1]};was_pressed=pressing;
                pointing_hand.update(valid,forwardness,{raw.left_grip>.55f,raw.right_grip>.55f},GetTickCount64(),fresh);
                pointing_side.store(pointing_hand.active());
            }
            const auto& aim=game_xr->aim_locations[pointing_hand.active()];
            if(game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS && (aim.locationFlags&required)==required) {
                const auto& p=aim.pose;
                const auto world_aim=d2vr::to_game_pose(d2vr::camera_relative_controller(
                    {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},hand_centre,head_origin.position),head_origin,anchor,1.0);
                native_interaction_aim.publish(*body,world_aim,game_xr->pose_epoch(),GetTickCount64(),true);
            } else native_interaction_aim.clear();
        }
        if((native_camera_trace_requested || native_left_aim_requested) && eye==0) {
            constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|
                XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            // Powers, pistol and crossbow live in the left hand: the game aim, the crosshair and the
            // shots always follow the left hand (the interaction ray picks its own hand).
            const auto& aim=game_xr->aim_locations[0];
            if(game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS &&
               (aim.locationFlags&required)==required && (!native_left_aim_requested ||
               (!body->authored_view() && game_xr->controller_input.active && game_xr->controller_input.left_grip<.5f &&
                // The native melee trace shares this aim; leave it to the head while a sword attack runs.
                GetTickCount64()>=swing_until_tick+500 && game_xr->controller_input.right_trigger<.3f))) {
                const auto& p=aim.pose;
                const auto left=d2vr::to_game_pose(d2vr::camera_relative_controller(
                    {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},hand_centre,head_origin.position),head_origin,anchor,1.0);
                if(native_left_aim_requested) native_left_aim.publish(*body,left,game_xr->pose_epoch(),GetTickCount64(),true);
                if(native_shot_aim_installed) native_shot_aim.publish(left,game_xr->pose_epoch(),GetTickCount64(),true);
                else native_gameplay_aim_trace.publish(*body,left,game_xr->pose_epoch(),GetTickCount64(),true);
            } else {native_gameplay_aim_trace.clear();native_left_aim.clear();native_weapon_attachment.clear();if(native_shot_aim_installed) native_shot_aim.clear();}
        }
        if((tracked_arms_requested || native_weapon_attachment_requested) && eye==0) {
            d2vr::rig::ArmTrackingFrame hands{};hands.tick=GetTickCount64();hands.epoch=game_xr->pose_epoch();
            hands.curls={std::clamp(game_xr->controller_input.left_grip,0.f,1.f),std::clamp(game_xr->controller_input.right_grip,0.f,1.f)};
            hands.index_curls={std::clamp(game_xr->controller_input.left_trigger,0.f,1.f),std::clamp(game_xr->controller_input.right_trigger,0.f,1.f)};
            hands.independent_index=true;hands.reference_epoch=game_xr->reference_epoch();hands.has_head_anchor=true;
            hands.head_anchor.position={anchor.position[0],anchor.position[1],anchor.position[2]};
            for(unsigned axis=0;axis<3;++axis) hands.head_anchor.basis[axis]={anchor.basis[axis*3],anchor.basis[axis*3+1],anchor.basis[axis*3+2]};
            if(avatar_pose_gate.load(std::memory_order_acquire) && game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS) {
                hands.hands=game_xr->grip_tracked_hands;
                for(unsigned side=0;side<2;++side) if(hands.hands&(1u<<side)) {
                    const auto& grip=game_xr->grip_locations[side].pose;
                    const d2vr::TrackedPose tracked_grip{
                        // A stationary controller stays fixed when the head leans.
                        {grip.position.x,grip.position.y,grip.position.z},
                        {grip.orientation.x,grip.orientation.y,grip.orientation.z,grip.orientation.w}};
                    hands.grips[side]=d2vr::to_game_pose(d2vr::camera_relative_controller(tracked_grip,hand_centre,head_origin.position),head_origin,anchor,1.0);
                }
            }
            {std::lock_guard<std::mutex> origin_lock(arm_origin_mutex);
             hands.model_origin=arm_model_origin;hands.has_model_origin=arm_model_origin_frame!=0;
             if(arm_origin_csv.is_open()) arm_origin_csv << "publish," << frames.load() << ",2," << anchor.position[0] << ',' << anchor.position[1] << ',' << anchor.position[2] << '\n';}
            {
                std::lock_guard<std::mutex> hand_lock(hand_state_mutex);
                hands.has_hand_state=hand_state_tick && GetTickCount64()-hand_state_tick<=300;
                // Fingers are ours only for a hand the game leaves empty; the right hand keeps its native grip.
                hands.empty_hands=~hand_item_bits&3u;
                // Scripted animations (takedowns, finishers, mantles: authored camera) play on the game's own arms.
                hands.native_hands=body->authored_view()?3u:0u;
                if(hands.native_hands) hands.has_hand_state=true;
                hands_native.store(hands.native_hands!=0);
            }
            if(tracked_arms_requested) arm_tracking.publish(hands);
            if(native_weapon_attachment_requested) native_weapon_attachment.publish(arm_binding_cache.load(),hands.epoch,hands.tick,
                game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS?game_xr->grip_tracked_hands:0,
                game_xr->controller_input.left_grip<.5f);

        }
        // Rotation and physical eye separation first. Roomscale translation is
        // deferred until the native collision trace has been validated locally.
        d2vr::Vec3 pupil{xr_eye.pose.position.x,xr_eye.pose.position.y,xr_eye.pose.position.z};
        d2vr::TrackedPose tracked{head_origin.position+head_offset+(pupil-centre),orientation};
        const auto world=d2vr::to_game_pose(tracked,head_origin,anchor,1.0);
        if(native_camera_trace_requested && head_anchor_samples<8000 && frames.load()%6==0) {
            head_anchor_csv << frames.load() << ',' << GetTickCount64() << ',' << eye << ',' << head_pose_epoch
                << ',' << game_xr->reference_epoch()
                << ',' << body->heading << ',' << body->pitch << ',' << body->yaw << ',' << body->roll;
            for(auto f:source_anchor.position) head_anchor_csv << ',' << f;
            for(auto f:source_anchor.basis) head_anchor_csv << ',' << f;
            for(auto f:anchor.position) head_anchor_csv << ',' << f;
            for(auto f:anchor.basis) head_anchor_csv << ',' << f;
            head_anchor_csv << ',' << orientation.x << ',' << orientation.y << ',' << orientation.z << ',' << orientation.w
                << ',' << world.position.x << ',' << world.position.y << ',' << world.position.z;
            for(auto v:world.basis) head_anchor_csv << ',' << v.x << ',' << v.y << ',' << v.z;
            head_anchor_csv << '\n';++head_anchor_samples;
            if(head_anchor_samples%120==0) head_anchor_csv.flush();
            if(!head_anchor_csv) throw std::runtime_error("Cannot save rendered camera anchor trace");
        }
        const std::array<float,3> position{static_cast<float>(world.position.x),static_cast<float>(world.position.y),static_cast<float>(world.position.z)};
        std::array<float,9> basis{};
        for(int row=0;row<3;++row) {
            basis[row*3]=static_cast<float>(world.basis[row].x);
            basis[row*3+1]=static_cast<float>(world.basis[row].y);
            basis[row*3+2]=static_cast<float>(world.basis[row].z);
        }
        std::memcpy(head_saved.data(),bytes.data()+0x9c,head_saved.size());head_source_address=source;
        float hfov=render_fov.right-render_fov.left,vfov=render_fov.up-render_fov.down;
        std::memcpy(bytes.data()+0x9c,&hfov,4);std::memcpy(bytes.data()+0xa0,&vfov,4);
        std::memcpy(bytes.data()+0xa8,position.data(),12);std::memcpy(bytes.data()+0xcc,basis.data(),36);
        std::memcpy(bytes.data()+0x100,optical->data(),64);bytes[0x140]=std::byte{1};
        if(eye==0 && native_crosshair_installed) {
            // Aim point 30 units along the active hand, projected into this eye's tangent-space frustum.
            constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            // The crosshair shows the hand that will grab/interact (the pointing hand). Shots and
            // powers still leave from the left hand (game aim + launch swap use aim_locations[0]).
            const auto& aim=game_xr->aim_locations[pointing_side.load()];
            bool published=false;
            if(!body->authored_view() && (aim.locationFlags&required)==required) {
                const auto& p=aim.pose;
                const auto hand=d2vr::to_game_pose(d2vr::camera_relative_controller(
                    {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}},hand_centre,head_origin.position),head_origin,anchor,1.0);
                const auto target=hand.position+hand.basis[0]*30.0;
                const auto d=target-world.position;
                const double depth=d2vr::dot(d,world.basis[0]);
                if(depth>.1) {
                    const double tx=-d2vr::dot(d,world.basis[1])/depth,ty=d2vr::dot(d,world.basis[2])/depth;
                    const double l=std::tan(render_fov.left),r=std::tan(render_fov.right),u=std::tan(render_fov.up),dn=std::tan(render_fov.down);
                    const double nx=(2*tx-(r+l))/(r-l),ny=(2*ty-(u+dn))/(u-dn);
                    if(std::abs(nx)<1.2 && std::abs(ny)<1.2) {
                        native_crosshair.publish(static_cast<float>(nx),static_cast<float>(ny),game_xr->pose_epoch(),GetTickCount64(),true);published=true;
                    }
                }
            }
            if(!published) native_crosshair.clear();
        }
        head_wrote=true;SIZE_T written{};
        if(!WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(source+0x9c),bytes.data()+0x9c,head_saved.size(),&written) || written!=head_saved.size())
            throw std::runtime_error("Cannot write temporary render-only head pose");
        ++head_writes;game_xr->record_rendered_eye(static_cast<UINT>(eye),
            {render_fov.left,render_fov.right,render_fov.up,render_fov.down});
    } catch(const std::exception& error) {note_recoverable(error.what());}
    catch(...) {note_recoverable("Unknown head-view exception");}
}

void restore_head_view() noexcept {
    if(!head_wrote) return;
    SIZE_T written{};
    if(WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(head_source_address+0x9c),head_saved.data(),head_saved.size(),&written) && written==head_saved.size()) ++head_restores;
    else {callback_failed=true;capturing=false;}
    head_wrote=false;
}

void prepare_stereo() {
    std::array<std::byte,80> metadata{};
    if(!read_self(image_base+d2vr::side_by_side_mode_rva,metadata.data(),metadata.size()) ||
       !d2vr::valid_stereo_metadata(metadata,image_base)) throw std::runtime_error("Stereo metadata differs from supported profile");
    diagnostic_mode_pointer=reinterpret_cast<void*>(image_base+d2vr::side_by_side_mode_rva);
    if(paired_requested) {
        paired_metadata={};std::memcpy(paired_metadata.data(),metadata.data(),80);
        const std::array<float,4> viewport{0,0,1,1};
        for(int eye=0;eye<2;++eye) std::memcpy(paired_metadata.data()+eye*40+12,viewport.data(),16);
        auto end=reinterpret_cast<std::uintptr_t>(metadata_end);
        std::memcpy(paired_metadata.data()+80,&end,8);
        diagnostic_mode_pointer=paired_metadata.data();
    }
    if(single_eye_requested) {
        // Clone one validated eye descriptor with a full viewport. The next
        // record's nonempty label terminates the engine's descriptor loop.
        left_metadata={};std::memcpy(left_metadata.data(),metadata.data(),40);
        const std::array<float,4> viewport{0,0,1,1};
        std::memcpy(left_metadata.data()+12,viewport.data(),16);
        auto end=reinterpret_cast<std::uintptr_t>(metadata_end);
        std::memcpy(left_metadata.data()+40,&end,8);
        right_metadata=left_metadata;
        const float side=1;std::memcpy(right_metadata.data()+32,&side,4);
        diagnostic_mode_pointer=left_metadata.data();
    }
    if(!read_self(image_base+d2vr::selected_mode_rva,&saved_mode,8) ||
       saved_mode!=reinterpret_cast<void*>(image_base+d2vr::single_mode_rva))
        throw std::runtime_error("Native renderer is not in single-camera mode");
    if(native_views().size()!=1) throw std::runtime_error("Enter a gameplay scene with one active camera first");
    if(!read_self(image_base+d2vr::separation_value_rva,&saved_separation,4) ||
       !read_self(image_base+d2vr::screen_separation_value_rva,&saved_screen,4))
        throw std::runtime_error("Cannot read stereo configuration values");
    std::memcpy(&diagnostic_separation,&diagnostic_half_separation,4);
}

void enable_stereo() {
    if(full_scale_requested) {
        for(std::size_t axis=0;axis<2;++axis) {
            if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(image_base+scale_value_rvas[axis]),0x3f800000,saved_scale[axis])!=saved_scale[axis])
                throw std::runtime_error("Render scale changed before comparison");
            full_scale_active[axis]=true;
        }
        full_scale_written=true;
    }
    if(temporal_aa_requested) {
        auto value=reinterpret_cast<volatile LONG*>(image_base+temporal_aa_value_rva);
        if(InterlockedCompareExchange(value,1,saved_temporal_aa)!=saved_temporal_aa)
            throw std::runtime_error("Temporal antialiasing changed before comparison");
        temporal_aa_active=true;temporal_aa_written=true;
    }
    auto separation=reinterpret_cast<volatile LONG*>(image_base+d2vr::separation_value_rva);
    auto screen=reinterpret_cast<volatile LONG*>(image_base+d2vr::screen_separation_value_rva);
    auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
    if(InterlockedCompareExchange(separation,diagnostic_separation,saved_separation)!=saved_separation)
        throw std::runtime_error("Stereo separation changed before test");
    if(InterlockedCompareExchange(screen,0,saved_screen)!=saved_screen) {
        InterlockedCompareExchange(separation,saved_separation,diagnostic_separation);
        throw std::runtime_error("Stereo projection setting changed before test");
    }
    if(InterlockedCompareExchangePointer(mode,diagnostic_mode_pointer,saved_mode)!=saved_mode) {
        InterlockedCompareExchange(separation,saved_separation,diagnostic_separation);
        InterlockedCompareExchange(screen,saved_screen,0);
        throw std::runtime_error("Native mode changed before test");
    }
    stereo_active=true;
    stereo_ever_enabled=true;
}

void restore_stereo() noexcept {
    for(std::size_t axis=0;axis<2;++axis) if(full_scale_active[axis]) {
        full_scale_restored=(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(image_base+scale_value_rvas[axis]),saved_scale[axis],0x3f800000)==0x3f800000) && full_scale_restored;
        full_scale_active[axis]=false;
    }
    if(temporal_aa_active) {
        temporal_aa_restored=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(image_base+temporal_aa_value_rva),saved_temporal_aa,1)==1;
        temporal_aa_active=false;
    }
    if(!stereo_active) return;
    auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
    mode_restored=InterlockedCompareExchangePointer(mode,saved_mode,
        diagnostic_mode_pointer)==diagnostic_mode_pointer;
    auto separation=reinterpret_cast<volatile LONG*>(image_base+d2vr::separation_value_rva);
    auto screen=reinterpret_cast<volatile LONG*>(image_base+d2vr::screen_separation_value_rva);
    values_restored=InterlockedCompareExchange(separation,saved_separation,diagnostic_separation)==diagnostic_separation;
    values_restored=(InterlockedCompareExchange(screen,saved_screen,0)==0) && values_restored;
    stereo_active=false;
}

bool capture_texture(ID3D11Texture2D* buffer,ID3D11Device* device,const wchar_t* filename) {
    (void)device; // Readback uses the texture's owning device.
    const auto pixels=d2vr::readback_rgba(buffer);
    if(pixels.bytes.empty()) return false; // Unsupported or oversized diagnostic resource.
    std::ofstream output(output_directory/filename,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(pixels.bytes.data()),static_cast<std::streamsize>(pixels.bytes.size()));
    return static_cast<bool>(output);
}

void capture_settings_transfer(ID3D11Texture2D* destination,const unsigned char* expected,std::size_t size) {
    if(settings_transfer_captured) return;
    const auto pixels=d2vr::readback_rgba(destination);
    const bool equal=pixels.width==640 && pixels.height==560 && pixels.bytes.size()==size &&
        std::memcmp(pixels.bytes.data(),expected,size)==0;
    if(!equal) throw std::runtime_error("Actual XR settings pixels differ from the generated panel");
    std::ofstream raw(output_directory/L"xr-settings.rgba",std::ios::binary|std::ios::trunc);
    raw.write(reinterpret_cast<const char*>(pixels.bytes.data()),static_cast<std::streamsize>(size));
    std::ofstream meta(output_directory/L"xr-settings-transfer.json",std::ios::trunc);
    meta << "{\"pid\":" << GetCurrentProcessId() << ",\"width\":640,\"height\":560,\"pixels_equal\":true,\"pose_epoch\":"
         << xr_pose_epoch << ",\"headset_scene_visually_verified\":false}\n";
    if(!raw || !meta) throw std::runtime_error("Cannot save actual XR settings capture");
    settings_transfer_captured=true;
}

// The desktop window stretches the headset-sized backbuffer; lay the mirror out
// in the window's proportions so it looks right on the monitor.
std::array<UINT,2> mirror_view(HWND window) {
    RECT client{};
    if(!GetClientRect(window,&client) || client.right<=0 || client.bottom<=0) return {};
    return {static_cast<UINT>(client.right),static_cast<UINT>(client.bottom)};
}

void capture_xr_transfer(UINT eye,ID3D11Texture2D* source,ID3D11Texture2D* destination,
                         const XrCompositionLayerProjectionView& layer,bool gameplay,bool tracked,ID3D11Texture2D* native_source) {
    if(game_xr && xr_capture_epoch!=game_xr->pose_epoch()) {
        xr_capture_epoch=game_xr->pose_epoch();xr_transfer_mask=0;xr_menu_transfer_mask=0;xr_tracked_transfer_frames=0;
    }
    if(eye==0 && gameplay && tracked) ++xr_tracked_transfer_frames;
    // Avoid capturing the first tracked view while temporal buffers still hold
    // the desktop/startup projection. Count submitted pairs, not idle Presents.
    auto& mask=gameplay?xr_transfer_mask:xr_menu_transfer_mask;
    auto& capture_present=gameplay?xr_transfer_frame:xr_menu_transfer_frame;
    // Scripted simulations also save a timed eye pair every two seconds for review.
    if(simulated_gameplay_script_requested && gameplay && eye==0) {
        const auto elapsed=GetTickCount64()-simulated_input_start;
        if(elapsed>=script_capture_next) {
            script_capture_next=(elapsed/2000+1)*2000;
            const auto pixels=d2vr::readback_rgba(source);
            if(pixels.bytes.empty()) return;
            std::ofstream timed(output_directory/(L"xr-t"+std::to_wstring(elapsed/1000)+L"-left.rgba"),std::ios::binary|std::ios::trunc);
            timed.write(reinterpret_cast<const char*>(pixels.bytes.data()),static_cast<std::streamsize>(pixels.bytes.size()));
        }
    }
    if(eye>=2 || mask==3 || (gameplay && head_requested && (!tracked || xr_tracked_transfer_frames<180)) ||
       (headset_size_requested && !headset_size_applied.load()) || frames.load()<20) return;
    const auto frame=frames.load();
    if(eye==0) {mask=0;capture_present=frame;}
    if(capture_present!=frame || (eye==1 && mask!=1)) return;
    const auto game=d2vr::readback_rgba(source),xr=d2vr::readback_rgba(destination);
    if(game.bytes.empty() || xr.bytes.empty()) return; // Diagnostic capture only; never fail the frame.
    const bool equal=game.width==xr.width && game.height==xr.height && game.bytes==xr.bytes;
    const auto suffix=eye?L"right":L"left";
    const std::wstring prefix=gameplay?L"xr-":L"xr-menu-";
    const auto save=[&](const d2vr::RgbaPixels& pixels,const std::wstring& name) {
        std::ofstream output(output_directory/name,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(pixels.bytes.data()),static_cast<std::streamsize>(pixels.bytes.size()));
        if(!output) throw std::runtime_error("Cannot save actual XR transfer pixels");
    };
    save(game,prefix+L"source-"+suffix+L".rgba");
    save(xr,prefix+L"destination-"+suffix+L".rgba");
    const bool processed=source!=native_source;
    UINT64 native_changed_pixels{};
    if(processed) {
        const auto native=d2vr::readback_rgba(native_source);
        if(native.bytes.empty()) return;
        if(native.width!=game.width || native.height!=game.height) throw std::runtime_error("SMAA native and processed dimensions differ");
        save(native,prefix+L"native-source-"+suffix+L".rgba");
        for(std::size_t i=0;i<native.bytes.size();i+=4)
            if(std::memcmp(native.bytes.data()+i,game.bytes.data()+i,4)!=0) ++native_changed_pixels;
    }
    UINT64 rgb_sum{},lit{},opaque{};
    const auto count=game.bytes.size()/4;
    for(std::size_t i=0;i<game.bytes.size();i+=4) {
        UINT maximum{};
        for(std::size_t channel=0;channel<3;++channel) {
            const auto value=std::to_integer<UINT>(game.bytes[i+channel]);rgb_sum+=value;maximum=std::max(maximum,value);
        }
        if(maximum>8) ++lit;
        if(game.bytes[i+3]==std::byte{255}) ++opaque;
    }
    const double lit_fraction=static_cast<double>(lit)/count;
    if(gameplay) xr_source_lit_fractions[eye]=lit_fraction;
    std::ofstream metadata(output_directory/(prefix+L"transfer-"+suffix+L".json"),std::ios::trunc);
    metadata << std::setprecision(9) << "{\"pid\":" << GetCurrentProcessId() << ",\"present\":" << frame
             << ",\"eye\":" << eye << ",\"width\":" << game.width << ",\"height\":" << game.height
             << ",\"pose_epoch\":" << xr_capture_epoch
             << ",\"tracked_gameplay_transfer_frames\":" << xr_tracked_transfer_frames
             << ",\"source_format\":" << game.format << ",\"destination_format\":" << xr.format
             << ",\"pixels_equal\":" << (equal?"true":"false") << ",\"gameplay\":" << (gameplay?"true":"false") << ",\"tracked_render_pose\":" << (tracked?"true":"false")
             << ",\"antialiasing\":\"" << (processed?"smaa1x_ultra":"native") << "\",\"native_changed_pixels\":" << native_changed_pixels
             << ",\"rgb_mean_8bit\":" << static_cast<double>(rgb_sum)/(count*3)
             << ",\"lit_pixel_fraction_gt8\":" << lit_fraction
             << ",\"opaque_pixel_fraction\":" << static_cast<double>(opaque)/count
             << ",\"submitted_orientation\":[" << layer.pose.orientation.x << ',' << layer.pose.orientation.y << ','
             << layer.pose.orientation.z << ',' << layer.pose.orientation.w << "],\"submitted_position\":["
             << layer.pose.position.x << ',' << layer.pose.position.y << ',' << layer.pose.position.z
             << "],\"submitted_fov\":[" << layer.fov.angleLeft << ',' << layer.fov.angleRight << ',' << layer.fov.angleUp << ',' << layer.fov.angleDown
             << "],\"headset_scene_visually_verified\":false}\n";
    if(!metadata) throw std::runtime_error("Cannot save XR transfer metadata");
    if(!equal) throw std::runtime_error("Actual XR destination pixels differ from the captured game eye");
    mask|=1u<<eye;
}

bool capture_frame(IDXGISwapChain* chain,const wchar_t* filename) {
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11Device> device;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))) || FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    return capture_texture(buffer.Get(),device.Get(),filename);
}

void log_pose(UINT64 number,UINT eye,const DXGI_SWAP_CHAIN_DESC& desc,const d2vr::GameCameraPose& pose) {
    csv << number << ',' << GetTickCount64() << ',' << eye << ',' << desc.BufferDesc.Width << ','
        << desc.BufferDesc.Height << ',' << desc.BufferDesc.Format << ',' << pose.horizontal_fov << ',' << pose.vertical_fov;
    for(float value:pose.position) csv << ',' << value;
    for(float value:pose.basis) csv << ',' << value;
    csv << '\n';
}

void log_view_snapshot(const wchar_t* filename,const std::vector<NativeView>& views) {
    std::ofstream output(output_directory/filename,std::ios::trunc);
    std::array<std::byte,16> header{};
    if(!read_self(image_base+d2vr::prepared_views_rva,header.data(),header.size())) throw std::runtime_error("Cannot snapshot view header");
    std::uintptr_t address{};std::memcpy(&address,header.data(),8);
    output << std::setprecision(9) << "{\"views\":[";
    for(std::size_t eye=0;eye<views.size();++eye) {
        std::array<std::byte,0x1a0> bytes{};
        if(!read_self(address+eye*d2vr::prepared_view_stride,bytes.data(),bytes.size())) throw std::runtime_error("Cannot snapshot prepared view");
        if(eye) output << ',';
        output << "{\"viewport\":[";
        for(int i=0;i<4;++i) {if(i) output << ',';output << views[eye].viewport[i];}
        output << "],\"position\":[";
        for(int i=0;i<3;++i) {if(i) output << ',';output << views[eye].pose->position[i];}
        output << "],\"projection\":[";
        for(int i=0;i<16;++i) {float value{};std::memcpy(&value,bytes.data()+0x130+i*4,4);if(i) output << ',';output << value;}
        output << "],\"projection_enabled\":" << (bytes[0x170]!=std::byte{}?"true":"false") << '}';
    }
    output << "]}\n";
}

void sample_stereo(IDXGISwapChain* chain,UINT64 number,const DXGI_SWAP_CHAIN_DESC& desc) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if(!capturing.load()) return;
    if(xr_requested && game_logic_mask()!=2) {
        if(taa_history_installed) taa_history.clear_eye_views();
        restore_stereo();
        std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
        eye_sources.invalidate();eye_view_allocation=0;eye_camera_owner={};
        native_eye_pair.reset();paired_left_ready=false;paired_buffer_binds=0;
        return;
    }
    auto views=native_views();
    if(paired_requested && !stereo_active && views.size()==1) {
        std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
        if(paired_buffer_binds>0 && paired_buffer_binds<16) {
            if(paired_binds_per_eye==paired_buffer_binds) ++paired_baseline_samples;
            else {paired_binds_per_eye=paired_buffer_binds;paired_baseline_samples=1;}
        }
    }
    if(single_eye_requested) {
        if(views.size()!=1) return;
        ++valid_poses;
        if(number%6==0) log_pose(number,right_phase?1:0,desc,*views[0].pose);
        if(!stereo_active && number>=10) {
            mono_captured=capture_frame(chain,L"dxgi-mono.rgba");
            log_view_snapshot(L"dxgi-mono-views.json",views);
            enable_stereo();stereo_start_tick=GetTickCount64();
        } else if(stereo_active && GetTickCount64()-stereo_start_tick>=2000) {
            ++stereo_frames;
            if(!right_phase) {
                left_captured=capture_frame(chain,L"dxgi-left.rgba");
                log_view_snapshot(L"dxgi-left-views.json",views);
                auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
                if(InterlockedCompareExchangePointer(mode,right_metadata.data(),diagnostic_mode_pointer)!=diagnostic_mode_pointer)
                    throw std::runtime_error("One-eye mode changed before right projection test");
                diagnostic_mode_pointer=right_metadata.data();right_phase=true;stereo_start_tick=GetTickCount64();
            } else if(!stereo_captured) {
                stereo_captured=capture_frame(chain,L"dxgi-stereo.rgba");
                log_view_snapshot(L"dxgi-right-views.json",views);
            }
        }
        return;
    }
    if(!stereo_active && views.size()==1) {
        if(!baseline_pose) baseline_pose=views[0].pose;
        float movement{};
        for(int axis=0;axis<3;++axis) movement+=std::abs(views[0].pose->position[axis]-baseline_pose->position[axis]);
        for(int axis=0;axis<9;++axis) movement+=std::abs(views[0].pose->basis[axis]-baseline_pose->basis[axis]);
        camera_moved=camera_moved || movement>0.01f;
    }
    if(stereo_active && views.size()==2) {
        ++stereo_frames;
        float distance{};
        for(int axis=0;axis<3;++axis) {
            float difference=views[0].pose->position[axis]-views[1].pose->position[axis];
            distance+=difference*difference;
        }
        distance=std::sqrt(distance);measured_separation=distance;
        if(distance>0.001f && distance<0.2f) {
            ++distinct_view_frames;
            if(!stereo_captured && number>stereo_start_frame+10 && GetTickCount64()-stereo_start_tick>=2000) {
                if(paired_requested) {
                    std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
                    if(!paired_left_ready || (native_eye_capture_requested?!native_eye_pair.complete():
                        (!paired_binds_per_eye || paired_buffer_binds!=paired_binds_per_eye*2))) return;
                    ComPtr<ID3D11Device> device;chain->GetDevice(IID_PPV_ARGS(&device));
                    if(!device) return;
                    left_captured=capture_texture(paired_left.Get(),device.Get(),L"dxgi-left.rgba");
                    paired_capture_binds=paired_buffer_binds;
                }
                stereo_captured=capture_frame(chain,L"dxgi-stereo.rgba");
                captured_logic_mask=game_logic_mask();
                log_view_snapshot(L"dxgi-stereo-views.json",views);
            }
        }
    }
    for(UINT eye=0;eye<views.size();++eye) {
        ++valid_poses;
        if(number%6==0) log_pose(number,eye,desc,*views[eye].pose);
    }
    if(!stereo_active && number>=10 && views.size()==1 && (camera_moved || paired_requested) &&
       (!paired_requested || native_eye_capture_requested || paired_baseline_samples>=3)) {
        // Native eye boundaries validate the completed pair independently.
        // Render-target binding histograms belong only to the older capture path.
        mono_captured=capture_frame(chain,L"dxgi-mono.rgba");
        log_view_snapshot(L"dxgi-mono-views.json",views);
        enable_stereo();stereo_start_frame=number;stereo_start_tick=GetTickCount64();
    }
}

bool focused_movement() {
    DWORD foreground_pid{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    if(foreground_pid!=GetCurrentProcessId()) return false;
    for(int key:{0x57,0x41,0x53,0x44,0x25,0x26,0x27,0x28})
        if(GetAsyncKeyState(key)&0x8000) return true;
    return false;
}

std::wstring module_path(HMODULE handle) {
    std::vector<wchar_t> path(32768);
    DWORD size=GetModuleFileNameW(handle,path.data(),static_cast<DWORD>(path.size()));
    if(!size || size>=path.size()) throw std::runtime_error("Cannot read module path");
    return std::wstring(path.data(),size);
}

std::string sha256(const fs::path& path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    auto cleanup=[&]() { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); };
    try {
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("Cannot open SHA-256 provider");
        DWORD size{},received{};
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&received,0)<0)
            throw std::runtime_error("Cannot size SHA-256 state");
        std::vector<UCHAR> object(size),buffer(256*1024);
        if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)
            throw std::runtime_error("Cannot create SHA-256 state");
        std::ifstream input(path,std::ios::binary);
        if(!input) throw std::runtime_error("Cannot read game executable");
        while(input) {
            input.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size()));
            auto count=input.gcount();
            if(count && BCryptHashData(hash,buffer.data(),static_cast<ULONG>(count),0)<0)
                throw std::runtime_error("Cannot hash game executable");
        }
        if(!input.eof()) throw std::runtime_error("Executable read failed");
        UCHAR digest[32]{};
        if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0) throw std::runtime_error("Cannot finish SHA-256");
        static constexpr char hex[]="0123456789abcdef";
        std::string result;
        for(auto value:digest) {result+=hex[value>>4]; result+=hex[value&15];}
        cleanup(); return result;
    } catch(...) {cleanup(); throw;}
}

// A faulting frame retires only the XR session, on this Present thread, and
// the next Present after a growing backoff recreates it. Hooks, headset-sized
// output and the game keep running, so a runtime hiccup, desktop/VR switch or
// unexpected engine state costs a second of black instead of ending VR.
static ULONGLONG xr_retry_tick{};
static UINT64 xr_recoveries{};
void recover_xr_session(const char* reason) noexcept {
    note_recoverable(reason);
    try {
        std::lock_guard<std::mutex> lock(log_mutex);
        XrGraphicsScope graphics_scope;
        game_xr.reset();restore_stereo();if(taa_history_installed) taa_history.clear_eye_views();
        {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);paired_left.Reset();paired_backbuffer.Reset();
            eye_sources.invalidate();native_eye_pair.reset();paired_left_ready=false;paired_buffer_binds=0;}
        native_grab.clear();arm_tracking.clear();native_interaction_aim.clear();native_left_aim.clear();native_weapon_attachment.clear();
        if(controller_requested) controller_bridge.publish({},false);
        ++xr_recoveries;
        xr_retry_tick=GetTickCount64()+std::min<ULONGLONG>(10000,1000ull<<std::min<UINT64>(xr_recoveries-1,4));
    } catch(...) {
        record_callback_error("XR session recovery failed");capturing=false;
    }
}
HRESULT WINAPI present_hook(IDXGISwapChain* chain,UINT interval,UINT flags) {
    d2vr::CrashOperation operation("native Present/OpenXR");
    if(xr_graphics_active) return original_present(chain,interval,flags);
    auto selected=game_chain.load(std::memory_order_acquire);
    if(!selected && capturing.load() && !(flags&DXGI_PRESENT_TEST)) {
        DXGI_SWAP_CHAIN_DESC candidate{};
        DWORD owner{};
        if(SUCCEEDED(chain->GetDesc(&candidate)) && candidate.BufferDesc.Width>64 && candidate.BufferDesc.Height>64 &&
           GetWindowThreadProcessId(candidate.OutputWindow,&owner) && owner==GetCurrentProcessId()) {
            game_chain.compare_exchange_strong(selected,chain,std::memory_order_acq_rel);
            selected=game_chain.load(std::memory_order_acquire);
        }
    }
    if(chain!=selected) {
        DXGI_SWAP_CHAIN_DESC replacement{};
        if(capturing.load() && graphics_owner.bound() && SUCCEEDED(chain->GetDesc(&replacement)) && graphics_owner.matches_window(replacement.OutputWindow)) {
            record_callback_error("Game swapchain replaced; stopping VR before rebinding graphics resources");capturing=false;
            xr_close_requested=true;
            if(graphics_owner.on_owner_thread()) {
                try {
                    std::lock_guard<std::mutex> xr_lock(log_mutex);XrGraphicsScope graphics_scope;
                    game_xr.reset();restore_stereo();
                    {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);paired_left.Reset();paired_backbuffer.Reset();eye_sources.invalidate();native_eye_pair.reset();}
                    graphics_owner.release();xr_close_complete=true;
                } catch(...) {record_callback_error("Replacement graphics owner could not retire on its Present thread");}
            }
        }
        return original_present(chain,interval,flags);
    }
    const auto present_start_ns=d2vr::GameXr::cpu_time_ns();
    const bool measure_present=capturing.load() && xr_requested && !(flags&DXGI_PRESENT_TEST);
    const auto previous_end_ns=previous_present_end_ns.load();
    const auto capture_before=std::array<UINT64,5>{xr_transfer_mask,xr_menu_transfer_mask,
        settings_transfer_captured?1u:0u,mono_captured?1u:0u,stereo_captured?1u:0u};
    try {
    // The game device can enter its own driver locks during Present. Destroy XR
    // objects and their D3D11 references on the same thread that used them,
    // before returning to the engine; a worker-thread teardown stalled live.
    if(xr_requested && xr_close_requested.load(std::memory_order_acquire) &&
       !xr_close_complete.load(std::memory_order_acquire) &&
       (!xr_present_thread || GetCurrentThreadId()==xr_present_thread)) {
        std::lock_guard<std::mutex> xr_lock(log_mutex);
        XrGraphicsScope graphics_scope;
        game_xr.reset();
        restore_stereo();
        {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);paired_left.Reset();paired_backbuffer.Reset();}
        graphics_owner.release();
        xr_close_complete.store(true,std::memory_order_release);
    }
    if(headset_size_written.load() && !capturing.load()) {
        DXGI_SWAP_CHAIN_DESC current{};RECT client{};
        if(SUCCEEDED(chain->GetDesc(&current)) && current.Windowed && GetClientRect(current.OutputWindow,&client) &&
           client.right>client.left && client.bottom>client.top && current.BufferDesc.Width==static_cast<UINT>(client.right-client.left) &&
           current.BufferDesc.Height==static_cast<UINT>(client.bottom-client.top)) headset_size_restored=true;
    }
    if(capturing.load() && !(flags&DXGI_PRESENT_TEST)) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if(SUCCEEDED(chain->GetDesc(&desc)) && desc.BufferDesc.Width>64 && desc.BufferDesc.Height>64) {
            if(headset_size_written.load()) {
                if(desc.BufferDesc.Width==requested_output_size.width && desc.BufferDesc.Height==requested_output_size.height)
                    headset_size_applied=true;
                else if(GetTickCount64()-headset_size_tick.load()>5000)
                    throw std::runtime_error("Native game output did not reach the recommended headset size");
                if(FAILED(native_resize_result.load())) throw std::runtime_error("Native DXGI resize failed");
            }
            const auto completed_gpu_phase=gpu_trace_requested?gpu_phase.exchange(0):0;
            if(completed_gpu_phase) ++gpu_traced_frames;
            auto number=++frames;
            width=desc.BufferDesc.Width; height=desc.BufferDesc.Height; format=desc.BufferDesc.Format;
            if(completed_gpu_phase==3) {
                float scale_x{},scale_y{};
                if(!read_self(image_base+scale_value_rvas[0],&scale_x,4) || !read_self(image_base+scale_value_rvas[1],&scale_y,4))
                    throw std::runtime_error("Cannot record settled native render scale");
                const auto settled_views=native_views();
                log_view_snapshot(L"dxgi-settled-views.json",settled_views);
                std::ofstream settled(output_directory/L"settled-render-state.json",std::ios::trunc);
                settled << "{\"pid\":" << GetCurrentProcessId() << ",\"present\":" << number
                        << ",\"game_logic_mask\":" << game_logic_mask() << ",\"trace_phase\":3"
                        << ",\"output_size\":[" << desc.BufferDesc.Width << ',' << desc.BufferDesc.Height << ']'
                        << ",\"requested_size\":[" << requested_output_size.width << ',' << requested_output_size.height << ']'
                        << ",\"native_scale\":[" << scale_x << ',' << scale_y << ']'
                        << ",\"pose_epoch\":" << xr_pose_epoch << ",\"tracked_gameplay_transfers\":" << xr_tracked_transfer_frames << "}\n";
                if(!settled) throw std::runtime_error("Cannot record settled scene resolution");
                {std::lock_guard<std::mutex> trace_lock(gpu_mutex);gpu_csv.flush();gpu_hand_draw_csv.flush();}
            }
            ComPtr<ID3D11Device> device;
            if(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device)))) feature=device->GetFeatureLevel();
            if(stereo_requested) {
                sample_stereo(chain,number,desc);
                if(gpu_trace_requested) {
                    if(!device) throw std::runtime_error("Cannot query game graphics device for pipeline trace");
                    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
                    if(xr_requested && paired_requested) {
                        std::lock_guard<std::mutex> xr_lock(log_mutex);
                        graphics_owner.bind_or_verify(chain,device.Get(),context.Get(),resize_target);
                    }
                    gpu_game_context.store(context.Get(),std::memory_order_release);
                    gpu_game_device.store(device.Get(),std::memory_order_release);
                    gpu_render_thread.store(GetCurrentThreadId(),std::memory_order_release);
                    if(paired_requested) {
                        std::unique_lock<std::mutex> xr_lock(log_mutex);
                        if(!capturing.load()) {xr_lock.unlock();return original_present(chain,interval,flags);}
                        ComPtr<ID3D11Texture2D> completed_left,completed_right;
                        const auto views=native_views();
                        const auto gameplay=game_logic_mask()==2;
                        auto owner=d2vr::native_player_camera(read_self,image_base,head_body_camera);
                        if(!owner && gameplay) owner=d2vr::find_native_player_camera(read_self,image_base);
                        const bool authored_camera=gameplay && owner && owner->authored_view();
                        bool pair_ready{};
                        {
                            std::lock_guard<std::mutex> gpu_lock(gpu_mutex);
                            if(!paired_backbuffer) {
                                if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&paired_backbuffer)))) throw std::runtime_error("Cannot retain paired backbuffer");
                                D3D11_TEXTURE2D_DESC texture{};paired_backbuffer->GetDesc(&texture);
                                if(texture.SampleDesc.Count!=1 || (texture.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && texture.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                                    throw std::runtime_error("Unsupported paired backbuffer format");
                                texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=0;texture.CPUAccessFlags=0;texture.MiscFlags=0;
                                if(FAILED(device->CreateTexture2D(&texture,nullptr,&paired_left))) throw std::runtime_error("Cannot allocate paired eye texture");
                            }
                            pair_ready=paired_left_ready && views.size()==2 && (native_eye_capture_requested?native_eye_pair.complete():
                                (paired_binds_per_eye && paired_buffer_binds==paired_binds_per_eye*2));
                            if(pair_ready && native_eye_capture_requested) ++native_eye_pairs;
                            if(pair_ready) {completed_left=paired_left;completed_right=paired_backbuffer;}
                            else if(!gameplay) {completed_left=paired_backbuffer;completed_right=paired_backbuffer;}
                            ++paired_bind_histogram[std::min<UINT>(paired_buffer_binds,15)];
                            paired_buffer_binds=0;paired_left_ready=false;native_eye_pair.reset();
                        }
                        // Do not hold the GPU-observer lock across OpenXR calls.
                        // VDXR dispatches D3D11 work on its own thread and waits
                        // for it synchronously in xrEndFrame.
                        if(xr_requested && (game_xr || GetTickCount64()>=xr_retry_tick)) {
                            XrGraphicsScope graphics_scope;
                            if(!game_xr) {
                                xr_present_thread=GetCurrentThreadId();
                                if(simulated_vr_requested) {
                                    d2vr::simxr::configure(device.Get(),!simulated_gameplay_script_requested);
                                    if(physical_grab_requested && simulated_input_requested && !simulated_gameplay_script_requested) {
                                        d2vr::simxr::control.scripted=false;d2vr::simxr::control.scripted_pitch=0;
                                        d2vr::simxr::control.head={{0,0,0,1},{0,1.65f,0}};
                                    }
                                    simulated_input_start=GetTickCount64();simulated_initial_pause=game_logic_mask()==4;simulated_load_confirm.reset();
                                    if(taa_capture_csv.is_open()) taa_capture_after_tick=simulated_input_start+4000;
                                }
                                game_xr=std::make_unique<d2vr::GameXr>(device.Get(),desc.BufferDesc.Width,desc.BufferDesc.Height,
                                    desc.BufferDesc.Format,L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll",controller_requested,head_requested,eye_antialiasing_requested,
                                    controller_requested && head_requested,requested_resolution_multiplier,
                                    turning_config,saved_settings_requested?output_directory.parent_path()/L"build"/L"vr-settings.cfg":fs::path{},native_menu_mode==2,
                                    simulated_vr_requested?&d2vr::simxr::dispatch:nullptr,mirror_mode_requested,sharpness_requested,taa_preference,reticle_preference,hint_preference);
                                game_xr->observe_transfer=&capture_xr_transfer;
                                game_xr->observe_settings=&capture_settings_transfer;
                                xr_recommended_eye_sizes=game_xr->recommended_eye_sizes;xr_swapchain_eye_sizes=game_xr->swapchain_eye_sizes;
                                xr_eye_texture_formats=game_xr->eye_texture_formats;
                                if(headset_size_requested) {
                                    if(!desc.Windowed) throw std::runtime_error("Recommended headset output requires a windowed game swapchain");
                                    const auto scaled=game_xr->scaled_output_size(requested_resolution_multiplier,false);
                                    requested_output_size=scaled.size;effective_resolution_multiplier=scaled.effective;
                                    if(cached_output_written && !cached_output_restored.load()) cached_output_size.update(requested_output_size); // XR session recreated.
                                    else cached_output_size.apply(reinterpret_cast<volatile LONG64*>(image_base+0x32faf80),requested_output_size);
                                    cached_output_written=true;cached_output_restored=false;
                                    native_output_size.request(desc.OutputWindow,requested_output_size);
                                    headset_size_tick=GetTickCount64();headset_size_restored=false;headset_size_written=true;
                                }
                            } else if(game_xr->resize(desc.BufferDesc.Width,desc.BufferDesc.Height,desc.BufferDesc.Format)) {
                                // Rendered poses belong to the old chain. Locate a
                                // fresh frame before submitting the resized pair.
                                completed_left.Reset();completed_right.Reset();
                                xr_swapchain_eye_sizes=game_xr->swapchain_eye_sizes;
                                xr_eye_texture_formats=game_xr->eye_texture_formats;
                            }
                            const auto submitted_before=game_xr->submitted;
                            const auto transfer_before=xr_transfer_mask;
                            if(simulated_input_requested) {
                                auto& input=d2vr::simxr::control.input;input={};input.active=true;
                                const auto elapsed=GetTickCount64()-simulated_input_start;
                                const auto grab_elapsed=simulated_grab_clock.sample(elapsed,tracked_arms_requested,
                                    avatar_pose_gate.load(std::memory_order_acquire),gameplay);
                                if(simulated_gameplay_script_requested) {
                                    d2vr::simxr::control.scripted=false;d2vr::simxr::control.scripted_pitch=0;
                                    d2vr::simxr::control.head={{0,0,0,1},{0,1.65f,0}};
                                    d2vr::simxr::control.grips={{{{0,0,0,1},{-.2f,1.3f,-.4f}},{{0,0,0,1},{.2f,1.3f,-.4f}}}};
                                    d2vr::simxr::control.aims=d2vr::simxr::control.grips;
                                }
                                d2vr::simxr::control.scripted_pitch=!simulated_gameplay_script_requested && tracked_arms_requested && !physical_grab_requested && !native_left_aim_requested && elapsed>=1000 && elapsed<7000?-.65f:0;
                                if(physical_grab_requested && !simulated_gameplay_script_requested && grab_elapsed && *grab_elapsed>=4700 && !simulated_grab_focus_lost) {
                                    d2vr::simxr::push_state(XR_SESSION_STATE_VISIBLE);simulated_grab_focus_lost=true;simulated_grab_focus_lost_tick=elapsed;
                                }
                                if(physical_grab_requested && !simulated_gameplay_script_requested && simulated_grab_focus_lost && elapsed>=simulated_grab_focus_lost_tick+500 && !simulated_grab_focus_restored) {
                                    d2vr::simxr::push_state(XR_SESSION_STATE_FOCUSED);simulated_grab_focus_restored=true;
                                }
                                if(controller_aim_requested && !simulated_gameplay_script_requested) d2vr::simxr::control.aims[1].orientation=
                                    gameplay?XrQuaternionf{-.258819f,0,0,.965926f}:XrQuaternionf{0,0,0,1};
                                // Use the same native pad path as physical Quest actions.
                                input.menu=simulated_initial_pause && ((native_menu_mode!=2 && elapsed>=500 && elapsed<700) || (!native_left_aim_requested && !simulated_gameplay_script_requested && elapsed>=8000 && elapsed<8200) ||
                                    (!simulated_gameplay_script_requested && native_menu_mode==2 && elapsed>=9300 && elapsed<9500));
                                if(!native_left_aim_requested && !simulated_gameplay_script_requested && !simulated_initial_pause && gameplay && elapsed>=8000 && elapsed<8200) input.menu=true;
                                // A single native VR confirm also covers a completed loading screen.
                                input.a=simulated_load_confirm.sample(elapsed,!gameplay && game_logic_mask()==(1u<<15),
                                    game_xr->state==XR_SESSION_STATE_FOCUSED);
                                if(native_menu_mode==2 && !gameplay && game_xr->state==XR_SESSION_STATE_FOCUSED) {
                                    const auto ray=game_xr->simulated_native_menu_ray(.60,.385);
                                    d2vr::simxr::control.aims[1]={{static_cast<float>(ray.orientation.x),static_cast<float>(ray.orientation.y),static_cast<float>(ray.orientation.z),static_cast<float>(ray.orientation.w)},
                                        {static_cast<float>(ray.position.x),static_cast<float>(ray.position.y),static_cast<float>(ray.position.z)}};
                                    input.right_trigger=simulated_initial_pause && ((elapsed>=500 && elapsed<700) || (!simulated_gameplay_script_requested && elapsed>=8700 && elapsed<8900))?.9f:0;
                                }
                                if(gameplay && !native_left_aim_requested && !simulated_gameplay_script_requested) {
                                    input.move_y=!physical_grab_requested && elapsed>=2000 && elapsed<3000?.5f:0;
                                    input.a=!physical_grab_requested && elapsed>=3500 && elapsed<3700;
                                    if(tracked_arms_requested && !physical_grab_requested)
                                        input.right_grip=elapsed>=2500 && elapsed<5500?.9f:0;
                                    input.turn_x=!physical_grab_requested && elapsed>=5000 && elapsed<5200?.8f:0;
                                    if(tracked_arms_requested) input.right_grip=elapsed>=1200 && elapsed<2000?.9f:0;
                                    if(physical_grab_requested) input.right_grip=grab_elapsed && ((*grab_elapsed>=1200 && *grab_elapsed<2500) || (*grab_elapsed>=3800 && *grab_elapsed<6000))?.9f:0;
                                    if(arm_upload_requested && !tracked_arms_requested && !physical_grab_requested) {
                                        // Unmodified native idle/holster/block grasp capture.
                                        input.x=elapsed>=1200 && elapsed<2200;
                                        input.move_y=0;input.left_click=false;
                                        input.a=false;input.turn_x=0;
                                        input.right_grip=elapsed>=3000 && elapsed<6500?.9f:0;
                                    }
                                }
                                // Native test: production wizard, raw tracked T pose.
                                if(tracked_arms_requested && !native_left_aim_requested && !simulated_gameplay_script_requested &&
                                   !simulated_initial_pause && native_menu_mode!=2) {
                                    const bool tpose=elapsed>=8500 && elapsed<14000;
                                    d2vr::simxr::control.scripted=!physical_grab_requested && !tpose;
                                    if(tpose) {
                                        d2vr::simxr::control.head={{0,0,0,1},{0,1.65f,0}};
                                        d2vr::simxr::control.grips={{{{0,0,0,1},{-.8f,1.45f,0}},{{0,0,0,1},{.8f,1.45f,0}}}};
                                    } else if(!physical_grab_requested || elapsed<14500) {
                                        d2vr::simxr::control.grips={{{{0,0,0,1},{-.25f,1.35f,-.35f}},{{0,0,0,1},{.25f,1.35f,-.35f}}}};
                                    }
                                    input.y=elapsed>=14000 && elapsed<14200;
                                    if(elapsed>=11000 && elapsed<11200) input.a=true;
                                    if(elapsed>=14500 && elapsed<14700) input.menu=true;
                                    // Long wizard tests pause again for a native checkpoint load
                                    // while the measured fit and XR reference remain in this session.
                                    if(!physical_grab_requested && simulated_long_wizard && avatar_fit_reuse.has_accepted_fit() &&
                                       gameplay && elapsed>=20000 && elapsed<20200) input.menu=true;
                                    if(physical_grab_requested && grab_elapsed && *grab_elapsed>=9000 && *grab_elapsed<9200) input.menu=true;
                                }
                                if(simulated_gameplay_script_requested) {
                                    const bool focused=game_xr->state==XR_SESSION_STATE_FOCUSED;int index=-1;
                                    float head_pitch_degrees{};
                                    const auto command=simulated_gameplay_script.sample(elapsed,gameplay || x_wheel.wheel(),focused,&index,&head_pitch_degrees); // the open wheel is a held-X gameplay state
                                    const auto head_orientation=d2vr::SimulatedGameplayScript::pitch_orientation(head_pitch_degrees);
                                    d2vr::simxr::control.head.orientation={head_orientation[0],head_orientation[1],head_orientation[2],head_orientation[3]};
                                    const auto offsets=simulated_gameplay_script.offsets(elapsed);
                                    d2vr::simxr::control.head.position={offsets[0],1.65f+offsets[1],offsets[2]};
                                    d2vr::simxr::control.grips[0].position={-.2f+offsets[3],1.3f+offsets[4],-.4f+offsets[5]};
                                    d2vr::simxr::control.grips[1].position={.2f+offsets[6],1.3f+offsets[7],-.4f+offsets[8]};
                                    d2vr::simxr::control.aims=d2vr::simxr::control.grips;
                                    if(gameplay || x_wheel.wheel() || !focused) input=command; // Keep only the existing initial menu/loading confirm outside gameplay.
                                    if(index!=simulated_gameplay_last_index || gameplay!=simulated_gameplay_last_gameplay || focused!=simulated_gameplay_last_focused) {
                                        simulated_gameplay_csv<<elapsed<<','<<index<<','<<gameplay<<','<<focused;
                                        for(float value:{command.move_x,command.move_y,command.turn_x,command.turn_y,command.left_trigger,command.right_trigger,command.left_grip,command.right_grip}) simulated_gameplay_csv<<','<<value;
                                        for(bool value:{command.a,command.b,command.x,command.y,command.left_click,command.right_click,command.menu}) simulated_gameplay_csv<<','<<value;
                                        simulated_gameplay_csv<<','<<head_pitch_degrees<<'\n';simulated_gameplay_csv.flush();
                                        if(!simulated_gameplay_csv) throw std::runtime_error("Cannot save simulated gameplay commands");
                                        ++simulated_gameplay_rows;simulated_gameplay_last_index=index;
                                        simulated_gameplay_last_gameplay=gameplay;simulated_gameplay_last_focused=focused;
                                    }
                                }
                            }
                            game_xr->resolution_status(effective_resolution_multiplier);
                            if(arm_upload_requested) {
                                const auto now=GetTickCount64();
                                const auto reference=game_xr->reference_epoch();
                                trace_avatar_fit("frame_change",number,arm_binding_cache.load());
                                const bool accepted_at_start=avatar_fit_reuse.has_accepted_fit();
                                if(game_logic_mask()==0 || game_logic_mask()==(1u<<15)) {
                                    if(avatar_fit_reuse.active_fit()) ++avatar_fit_suspensions;
                                    avatar_fit_reuse.suspend(reference);avatar_pose_gate=false;
                                    arm_tracking.clear();arm_tracking.clear_calibration();arm_binding_cache.clear();
                                    game_xr->suspend_avatar();current_avatar_template.reset();last_avatar_rig.reset();
                                    trace_avatar_fit("loading_suspend",number,arm_binding_cache.load());
                                }
                                if(gameplay && now-arm_rig_refresh_tick>=500) {auto reader=&read_self;arm_binding_cache.refresh(reader,image_base);arm_rig_refresh_tick=now;}
                                if(gameplay && tracked_arms_requested) {
                                    // Which hands the game itself occupies (weapon/item in the slot, or carrying).
                                    auto reader=&read_self;const auto state=d2vr::rig::native_hand_state(reader,image_base);
                                    std::lock_guard<std::mutex> hand_lock(hand_state_mutex);
                                    if(state) {
                                        hand_empty_bits=state->carrying?0u:(state->empty_candidates&3u);
                                        hand_item_bits=state->carrying?3u:(state->slots[0]?1u:0u); // the right hand always holds the sword: occupied only while carrying
                                        hand_state_tick=now;
                                    }
                                }
                                const auto rig=arm_binding_cache.load();
                                if(last_avatar_rig!=rig) {
                                    if(avatar_fit_reuse.active_fit()) ++avatar_fit_suspensions;
                                    avatar_fit_reuse.suspend(reference);avatar_pose_gate=false;
                                    arm_tracking.clear();arm_tracking.clear_calibration();
                                    if(rig && (!last_verified_avatar_rig || last_verified_avatar_rig->arms!=rig->arms ||
                                       !(last_verified_avatar_rig->sources==rig->sources))) game_xr->avatar_identity_changed();
                                    else game_xr->suspend_avatar();
                                    if(rig) last_verified_avatar_rig=rig;
                                    current_avatar_template.reset();last_avatar_rig=rig;avatar_template_retry_tick=0;
                                    trace_avatar_fit("owner_replaced",number,rig);
                                }
                                if(rig && current_avatar_template && now-avatar_resource_check_tick>=500) {
                                    avatar_resource_check_tick=now;auto reader=&read_self;
                                    if(!d2vr::rig::avatar_fit_resources_current(reader,image_base,*rig,*current_avatar_template)) {
                                        if(avatar_fit_reuse.active_fit()) ++avatar_fit_suspensions;
                                        avatar_fit_reuse.suspend(reference);avatar_pose_gate=false;
                                        arm_tracking.clear();arm_tracking.clear_calibration();game_xr->suspend_avatar();
                                        current_avatar_template.reset();avatar_template_retry_tick=0;avatar_resource_validation_pending=true;
                                        trace_avatar_fit("resource_check_failed",number,rig);
                                    }
                                }
                                if(accepted_at_start && !avatar_fit_reuse.has_accepted_fit()) ++avatar_fit_invalidations;
                                if(rig && !current_avatar_template && now-avatar_template_retry_tick>=500) {
                                    avatar_template_retry_tick=now;++avatar_fit_template_checks;
                                    auto reader=&read_self;current_avatar_template=d2vr::rig::avatar_fit_template(reader,image_base,*rig,1);
                                    if(current_avatar_template) {
                                        if(last_verified_avatar_template && !(*last_verified_avatar_template==*current_avatar_template)) {
                                            game_xr->avatar_identity_changed();
                                            trace_avatar_fit("resource_identity_changed",number,rig);
                                        }
                                        last_verified_avatar_template=current_avatar_template;
                                        avatar_resource_check_tick=now;avatar_resource_validation_pending=false;
                                        avatar_template_verified=true;
                                        avatar_native_bind_palm_span=current_avatar_template->measurements.palm_span_metres;
                                        avatar_native_bind_shoulder_span=current_avatar_template->measurements.shoulder_span_metres;
                                        trace_avatar_fit("template_verified",number,rig);
                                    }
                                }
                                const bool had_fit=avatar_fit_reuse.has_accepted_fit();
                                const bool was_active=avatar_fit_reuse.active_fit().has_value();
                                const auto accepted_reference=avatar_fit_reuse.accepted_reference_epoch();
                                const bool compatible=avatar_fit_reuse.verify(current_avatar_template,reference);
                                if(had_fit && !avatar_fit_reuse.has_accepted_fit()) {
                                    ++avatar_fit_invalidations;
                                    if(reference==accepted_reference) game_xr->reset_avatar();else game_xr->suspend_avatar();
                                    arm_tracking.clear();arm_tracking.clear_calibration();
                                    trace_avatar_fit("fit_invalidated",number,rig);
                                } else if(compatible && !was_active) {++avatar_fit_reuses;trace_avatar_fit("fit_reused",number,rig);}
                                if(current_avatar_template) {
                                    game_xr->avatar_measurements(current_avatar_template->measurements);
                                    if(const auto calibrated=game_xr->avatar_calibration()) {
                                        const auto active=avatar_fit_reuse.active_fit();
                                        if(!active || active->reference_epoch!=calibrated->reference_epoch || active->arm_scale!=calibrated->arm_scale ||
                                           active->palm_span_metres!=calibrated->palm_span_metres || active->shoulder_span_metres!=calibrated->shoulder_span_metres ||
                                           active->eye_to_shoulder_metres!=calibrated->eye_to_shoulder_metres || active->standing_eye_height_metres!=calibrated->standing_eye_height_metres) {
                                            if(avatar_fit_reuse.accept(*current_avatar_template,*calibrated,reference)) {++avatar_fit_accepts;trace_avatar_fit("fit_accepted",number,rig);}
                                        }
                                    }
                                }
                                if(const auto active=avatar_fit_reuse.active_fit()) {
                                    arm_tracking.configure_calibration(*active);avatar_calibration_applied=true;
                                    avatar_calibration_arm_scale=active->arm_scale;
                                    avatar_calibration_shoulder_drop=active->eye_to_shoulder_metres;
                                }
                                // Hands need only a verified rig; a measured fit refines the hidden shoulder.
                                avatar_pose_gate.store(rig && current_avatar_template && !avatar_resource_validation_pending,std::memory_order_release);
                                trace_avatar_fit("pose_gate_updated",number,rig);
                            }
                            if(!gameplay || game_xr->state!=XR_SESSION_STATE_FOCUSED) {native_gameplay_aim_trace.clear();native_left_aim.clear();native_weapon_attachment.clear();}
                            if(native_left_aim_requested && native_left_aim.failed() && !native_left_aim_failure_noted) {
                                native_left_aim_failure_noted=true;note_recoverable("Native left-hand aim disabled itself; aiming falls back to the head");}
                            game_xr->present(completed_left.Get(),completed_right.Get(),
                                views.empty()?1.4f:views[0].pose->horizontal_fov,views.empty()?.88f:views[0].pose->vertical_fov,gameplay,authored_camera,game_logic_mask()==4,
                                pair_ready?completed_right.Get():nullptr,mirror_view(desc.OutputWindow));
                            if(taa_capture_csv.is_open()) taa_capture.drain(context.Get(),frames.load(),taa_capture_csv);
                            if(simulated_gameplay_script_requested && authored_camera && owner->mode!=0 && pair_ready &&
                               game_xr->last_authored_camera_layer && game_xr->last_tracked_pair) ++authored_capture_stable_frames;
                            else authored_capture_stable_frames=0;
                            if(simulated_gameplay_script_requested && authored_capture_stable_frames>=3 && authored_camera && pair_ready &&
                               game_xr->last_authored_camera_layer && game_xr->last_tracked_pair && !authored_pair_captured) {
                                constexpr std::array<unsigned,7> slots{44,33,34,12,20,13,21};
                                const auto dispatch=[&] {
                                    std::array<std::uintptr_t,7> result{};
                                    const auto table=*reinterpret_cast<void***>(context.Get());
                                    for(unsigned i=0;i<slots.size();++i) result[i]=reinterpret_cast<std::uintptr_t>(table[slots[i]]);
                                    return result;
                                };
                                const auto dispatch_before=dispatch();
                                if(!capture_texture(completed_left.Get(),device.Get(),L"authored-native-left.rgba") ||
                                   !capture_texture(completed_right.Get(),device.Get(),L"authored-native-right.rgba"))
                                    throw std::runtime_error("Cannot preserve authored native stereo pair");
                                log_view_snapshot(L"authored-native-views.json",views);
                                D3D11_TEXTURE2D_DESC texture{};completed_left->GetDesc(&texture);
                                std::ofstream evidence(output_directory/L"authored-native-pair.json",std::ios::trunc);
                                evidence<<"{\"pid\":"<<GetCurrentProcessId()<<",\"present\":"<<number
                                    <<",\"pose_epoch\":"<<game_xr->pose_epoch()<<",\"camera\":"<<owner->camera
                                    <<",\"camera_generation\":"<<owner->camera_identity.generation
                                    <<",\"native_mode\":"<<owner->mode<<",\"native_target_mode\":"<<owner->target_mode
                                    <<",\"consecutive_active_authored_frames\":"<<authored_capture_stable_frames
                                    <<",\"code_lock\":"<<(owner->code_lock?"true":"false")
                                    <<",\"blend_body_yaw\":"<<(owner->blend_body_yaw?"true":"false")
                                    <<",\"width\":"<<texture.Width<<",\"height\":"<<texture.Height
                                    <<",\"native_pair_complete\":true,\"tracked_stereo_submitted\":true,\"headset_acceptance\":false"
                                    <<",\"dispatch_before\":[";
                                for(unsigned i=0;i<slots.size();++i) {if(i) evidence<<',';evidence<<dispatch_before[i];}
                                const auto dispatch_after=dispatch();evidence<<"],\"dispatch_after\":[";
                                for(unsigned i=0;i<slots.size();++i) {if(i) evidence<<',';evidence<<dispatch_after[i];}
                                evidence<<"]}\n";
                                if(!evidence) throw std::runtime_error("Cannot preserve authored stereo ownership");
                                authored_pair_captured=true;
                            }
                            if(game_xr->timing.waited && xr_timing_samples<20'000) {
                                const auto& t=game_xr->timing;
                                xr_timing_csv << number << ',' << t.start_ns << ',' << t.end_ns
                                    << ',' << (previous_end_ns?present_start_ns-previous_end_ns:-1)
                                    << ',' << previous_desktop_present_ns.load() << ',' << t.start_ns-present_start_ns
                                    << ',' << t.wait_ns << ',' << t.begin_ns << ',' << t.locate_input_ns
                                    << ',' << t.transfer_ns << ',' << t.submit_ns
                                    << ',' << t.display_time << ',' << t.display_period
                                    << ',' << t.rendered_display_times[0] << ',' << t.rendered_display_times[1]
                                    << ',' << t.rendered_locate_cpu_times[0] << ',' << t.rendered_locate_cpu_times[1]
                                    << ',' << static_cast<int>(game_xr->state) << ',' << game_xr->pose_epoch()
                                    << ',' << (game_xr->last_should_render?1:0) << ',' << (game_xr->last_layer_submitted?1:0)
                                    << ',' << (game_xr->last_tracked_pair?1:0) << ',' << (gameplay?1:0)
                                    << ',' << (game_xr->last_authored_camera_layer?1:0) << ',' << (game_xr->settings_open()?1:0)
                                    << ',' << desc.BufferDesc.Width << ',' << desc.BufferDesc.Height
                                    << ',' << interval << ',' << flags
                                    << ',' << (game_xr->edge_antialiasing_enabled()?1:0)
                                    << ',' << (capture_before!=std::array<UINT64,5>{xr_transfer_mask,xr_menu_transfer_mask,
                                        settings_transfer_captured?1u:0u,mono_captured?1u:0u,stereo_captured?1u:0u}?1:0) << '\n';
                                ++xr_timing_samples;
                                if(xr_timing_samples%120==0) xr_timing_csv.flush();
                                if(!xr_timing_csv) throw std::runtime_error("Cannot save XR frame timing");
                            }
                            if(tracked_arms_requested && (game_logic_mask()!=2 || !game_xr->tracking_views() ||
                               game_xr->state!=XR_SESSION_STATE_FOCUSED || !game_xr->grip_tracked_hands ||
                               game_xr->pose_epoch()!=head_pose_epoch)) arm_tracking.clear();
                            if(native_weapon_attachment_requested && (!gameplay || !game_xr->tracking_views() ||
                               game_xr->state!=XR_SESSION_STATE_FOCUSED || game_xr->pose_epoch()!=head_pose_epoch ||
                               game_xr->controller_input.left_grip>=.5f)) native_weapon_attachment.clear();
                            if(native_weapon_attachment_requested) native_weapon_attachment.refresh_equipment(
                                arm_binding_cache.load(),game_xr->pose_epoch(),gameplay && game_xr->tracking_views() &&
                                game_xr->state==XR_SESSION_STATE_FOCUSED && game_xr->last_action_sync==XR_SUCCESS &&
                                game_xr->pose_epoch()==head_pose_epoch && (game_xr->grip_tracked_hands&2u) &&
                                game_xr->controller_input.left_grip<.5f);
                            if(controller_aim_requested && (game_logic_mask()!=2 || !game_xr->tracking_views() ||
                               game_xr->state!=XR_SESSION_STATE_FOCUSED || game_xr->pose_epoch()!=head_pose_epoch)) native_interaction_aim.clear();
                            if(physical_grab_requested && (!gameplay || !game_xr->tracking_views() || game_xr->state!=XR_SESSION_STATE_FOCUSED || game_xr->pose_epoch()!=head_pose_epoch)) native_grab.clear();
                            if(native_left_aim_requested && (!gameplay || !game_xr->tracking_views() ||
                               game_xr->state!=XR_SESSION_STATE_FOCUSED || game_xr->pose_epoch()!=head_pose_epoch)) {native_left_aim.clear();native_weapon_attachment.clear();}
                            if(optical_density_requested && !optical_density_applied && game_xr->tracking_views()) {
                                const auto scaled=game_xr->scaled_output_size(requested_resolution_multiplier,true);
                                const auto density=scaled.size;effective_resolution_multiplier=scaled.effective;
                                cached_output_size.update(density);native_output_size.request(desc.OutputWindow,density);
                                requested_output_size=density;headset_size_applied=false;headset_size_tick=GetTickCount64();
                                xr_tracked_transfer_frames=0;optical_density_applied=true;
                            }
                            if(game_xr->exit_requested) throw std::runtime_error("XR runtime ended the session; reconnecting");
                            if(force_xr_fault && xr_gameplay_frames>=200) {force_xr_fault=false;throw std::runtime_error("Forced simulated XR fault (build/force-xr-fault)");}
                            xr_submitted=game_xr->submitted;xr_dropped=game_xr->dropped;xr_last_state=game_xr->state;
                            xr_antialiased_eyes=game_xr->antialiased_eyes();
                            xr_settings_layers=game_xr->settings_layers;
                            if(const auto request=game_xr->take_temporal_request()) native_cvars.set(d2vr::NativeCvars::skip_temporal_aa,*request?0:1);
                            if(const auto request=game_xr->take_turning_request()) {
                                native_turning.configure(*request);turning_config=*request;
                                native_turning_requested=request->mode!=d2vr::TurnControl::Mode::native;
                            }
                            if(const auto request=game_xr->take_resolution_request()) {
                                {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);eye_sources.invalidate();native_eye_pair.reset();}
                                const auto scaled=game_xr->scaled_output_size(*request,optical_density_requested && optical_density_applied);
                                cached_output_size.update(scaled.size);native_output_size.request(desc.OutputWindow,scaled.size);
                                requested_output_size=scaled.size;requested_resolution_multiplier=*request;effective_resolution_multiplier=scaled.effective;
                                headset_size_applied=false;headset_size_tick=GetTickCount64();
                                xr_transfer_mask=0;xr_menu_transfer_mask=0;xr_tracked_transfer_frames=0;
                                gpu_settled_started=false;++xr_resolution_changes;
                            }
                            xr_pose_epoch=game_xr->pose_epoch();xr_reference_epoch=game_xr->reference_epoch();xr_state_events=game_xr->state_events;xr_reference_events=game_xr->reference_events;
                            if(GetTickCount64()-xr_live_status_tick>=1000 || (xr_transfer_mask==3 && transfer_before!=3)) {
                                xr_live_status_tick=GetTickCount64();
                                // The HUD census keeps its findings on disk while playing (a closed game never runs disable()).
                                static unsigned census_writes{};
                                if(native_hud_census_installed && ++census_writes%5==0) {
                                    std::ofstream census(output_directory/L"native-hud-census-summary.txt",std::ios::trunc);census << native_hud_census.summary() << '\n';
                                }
                                std::ofstream live(output_directory/L"xr-live-status.json",std::ios::trunc);
                                live << "{\"pid\":" << GetCurrentProcessId() << ",\"game_layers_submitted\":" << xr_submitted
                                     << ",\"simulated_vr\":" << (simulated_vr_requested?"true":"false")
                                     << ",\"tick_ms\":" << xr_live_status_tick << ",\"dropped_layers\":" << xr_dropped
                                     << ",\"session_state\":" << static_cast<int>(game_xr->state)
                                     << ",\"runtime_should_render\":" << (game_xr->last_should_render?"true":"false")
                                     << ",\"views_valid\":" << (game_xr->last_views_valid?"true":"false")
                                     << ",\"tracked_render_pair\":" << (game_xr->last_tracked_pair?"true":"false")
                                     << ",\"pose_epoch\":" << xr_pose_epoch << ",\"reference_events\":" << xr_reference_events
                                     << ",\"reference_epoch\":" << xr_reference_epoch
                                     << ",\"state_events\":" << xr_state_events
                                     << ",\"action_sync_attempted\":" << (game_xr->action_sync_attempted?"true":"false")
                                     << ",\"action_sync_result\":" << static_cast<int>(game_xr->last_action_sync)
                                     << ",\"action_sync_calls\":" << game_xr->action_sync_calls
                                     << ",\"action_not_focused\":" << game_xr->action_not_focused
                                     << ",\"active_input_hands\":" << game_xr->active_input_hands
                                     << ",\"grip_tracked_hands\":" << game_xr->grip_tracked_hands
                                     << ",\"tracked_arms_requested\":" << (tracked_arms_requested?"true":"false")
                                     << ",\"native_arm_pose_calls\":[" << arm_pose_calls[0].load() << ',' << arm_pose_calls[1].load() << ']'
                                     << ",\"native_arm_pose_fallbacks\":[" << arm_pose_fallbacks[0].load() << ',' << arm_pose_fallbacks[1].load() << ']'
                                     << ",\"native_arm_reach_clamps\":[" << arm_pose_clamps[0].load() << ',' << arm_pose_clamps[1].load() << ']'
                                     << ",\"native_controller_reads\":" << controller_bridge.reads()
                                     << ",\"native_virtual_controller_reads\":" << controller_bridge.virtual_reads()
                                     << ",\"native_nonzero_controller_reads\":" << controller_bridge.nonzero_reads() << ",\"native_lb_reads\":" << controller_bridge.lb_reads() << ",\"wheel_frames\":" << wheel_frames << ",\"hint_layers\":" << (game_xr?game_xr->hint_layers:0) << ",\"lb_inputs\":" << controller_bridge.lb_inputs() << ",\"lb_published\":" << controller_bridge.lb_published() << ",\"wheel_logic\":" << wheel_logic << ",\"native_x_reads\":" << controller_bridge.x_reads()
                                     << ",\"native_pause_button_reads\":" << controller_bridge.pause_reads()
                                     << ",\"native_joystick_focus_calls\":" << native_pad_focus.calls()
                                     << ",\"native_joystick_focus_overrides\":" << native_pad_focus.overrides()
                                     << ",\"native_joystick_wakeups\":" << native_pad_focus.wakeups()
                                     << ",\"native_pad_poll_gates\":" << native_pad_focus.poll_calls()
                                     << ",\"native_pad_serializer_gates\":" << native_pad_focus.serializer_calls()
                                     << ",\"native_pad_window_gates\":" << native_pad_focus.window_calls()
                                     << ",\"native_pad_window_overrides\":" << native_pad_focus.window_overrides()
                                     << ",\"head_anchor_trace_samples\":" << head_anchor_samples
                                     << ",\"head_calibration_captures\":" << head_calibration.captures()
                                     << ",\"game_window_foreground\":" << (GetForegroundWindow()==desc.OutputWindow?"true":"false")
                                     << ",\"last_layer_submitted\":" << (game_xr->last_layer_submitted?"true":"false")
                                     << ",\"last_menu_layer\":" << (game_xr->last_menu_layer?"true":"false")
                                     << ",\"last_authored_camera_layer\":" << (game_xr->last_authored_camera_layer?"true":"false")
                                     << ",\"authored_camera_layers\":" << game_xr->authored_camera_layers
                                     << ",\"tracked_gameplay_transfer_frames\":" << xr_tracked_transfer_frames
                                     << ",\"width\":" << width.load() << ",\"height\":" << height.load()
                                     << ",\"gameplay\":" << (game_logic_mask()==2?"true":"false")
                                     << ",\"headset_size_applied\":" << (headset_size_applied.load()?"true":"false")
                                     << ",\"optical_density_requested\":" << (optical_density_requested?"true":"false")
                                     << ",\"optical_density_request_applied\":" << (optical_density_applied?"true":"false")
                                     << ",\"resolution_multiplier_requested\":" << requested_resolution_multiplier
                                     << ",\"resolution_multiplier_effective\":" << effective_resolution_multiplier
                                     << ",\"eye_antialiasing_requested\":" << (eye_antialiasing_requested?"true":"false")
                                     << ",\"smaa_eye_passes\":" << xr_antialiased_eyes
                                     << ",\"mirror_mode\":" << static_cast<int>(game_xr->mirror_mode()) << ",\"mirrored_frames\":" << game_xr->mirrored_frames
                                     << ",\"sharpness\":" << game_xr->sharpness()
                                     << ",\"edge_antialiasing_enabled\":" << (game_xr->edge_antialiasing_enabled()?"true":"false")
                                     << ",\"vr_settings_open\":" << (game_xr->settings_open()?"true":"false")
                                     << ",\"vr_settings_layers\":" << xr_settings_layers
                                     << ",\"native_menu_pointer_active\":" << (game_xr->native_menu_input.active?"true":"false")
                                     << ",\"native_menu_pointer_uv\":[" << game_xr->native_menu_input.u << ',' << game_xr->native_menu_input.v << ']'
                                     << ",\"native_menu_trigger\":" << (game_xr->native_menu_input.trigger?"true":"false")
                                     << ",\"native_menu_pointer_layers\":" << game_xr->pointer_layers
                                     << ",\"native_menu_pointer_moves\":" << native_menu.moves()
                                     << ",\"native_menu_button_events\":" << native_menu.buttons()
                                     << ",\"native_menu_rejected\":" << native_menu.rejected()
                                     << ",\"native_menu_failed\":" << (native_menu.failed()?"true":"false")
                                     << ",\"vr_resolution_changes\":" << xr_resolution_changes
                                     << ",\"image_transfer_pixels_equal\":" << (xr_transfer_mask==3 && xr_capture_epoch==xr_pose_epoch?"true":"false")
                                     << ",\"image_transfer_pose_epoch\":" << xr_capture_epoch
                                     << ",\"menu_transfer_pixels_equal\":" << (xr_menu_transfer_mask==3 && xr_capture_epoch==xr_pose_epoch?"true":"false")
                                     << ",\"menu_position\":[" << game_xr->last_menu_pose.position.x << ',' << game_xr->last_menu_pose.position.y << ',' << game_xr->last_menu_pose.position.z << ']'
                                     << ",\"native_fire_seam\":" << (fire_seam_installed?native_fire_seam_trace.summary():std::string("{}"))
                                     << ",\"shot_aim\":{\"installed\":" << native_shot_aim_installed << ",\"calls\":" << native_shot_aim.calls()
                                     << ",\"routed\":" << native_shot_aim.routed() << ",\"rejected\":" << native_shot_aim.rejected() << ",\"last_reason\":" << native_shot_aim.last_reason() << '}' 
                                     << ",\"headset_scene_visually_verified\":false}\n";
                                if(!live) throw std::runtime_error("Cannot save live XR session state");
                                std::ofstream lifecycle(output_directory/L"xr-lifecycle.csv",std::ios::app);
                                lifecycle << frames.load() << ',' << xr_live_status_tick << ',' << xr_last_state << ',' << xr_pose_epoch
                                          << ',' << xr_reference_events << ',' << (game_xr->last_should_render?1:0)
                                          << ',' << (game_xr->last_layer_submitted?1:0) << ',' << (game_xr->last_menu_layer?1:0)
                                          << ',' << (game_xr->last_tracked_pair?1:0) << ',' << xr_tracked_transfer_frames
                                          << ',' << static_cast<int>(game_xr->last_action_sync) << ',' << game_xr->active_input_hands
                                          << ',' << game_xr->grip_tracked_hands << ',' << controller_bridge.nonzero_reads() << '\n';
                                if(!lifecycle) throw std::runtime_error("Cannot save XR lifecycle state");
                            }
                            xr_menu_layers=game_xr->menu_layers;
                            xr_controller_samples=game_xr->controller_samples;
                            if(controller_requested) {
                                if(native_menu_mode) {
                                    native_menu.publish(game_xr->native_menu_input,width.load(),height.load());
                                    // Pointer adapter disabled itself; menus stay usable with buttons.
                                    if(native_menu.failed() && !native_menu_failure_noted) {native_menu_failure_noted=true;note_recoverable(("Native Iggy menu adapter: "+native_menu.error()).c_str());}
                                }
                                controller_bridge.set_game_window(desc.OutputWindow);
                                const auto logic=game_logic_mask();
                                auto input=game_xr->controller_input;
                                // Grips belong to the hands (fist/grab/block). The native
                                // Quick Access Wheel (LB, carried on the pad's left-grip
                                // channel) opens by holding the right stick click instead.
                                float physical_left_grip=input.left_grip;
                                if(menu_chord_epoch!=game_xr->pose_epoch()) {
                                    menu_chord={};menu_chord_epoch=game_xr->pose_epoch();
                                }
                                const auto chord=menu_chord.update(input.menu,input.active && game_xr->state==XR_SESSION_STATE_FOCUSED,
                                    logic==2,input.turn_x,input.turn_y,GetTickCount64(),(game_xr->stick_active_hands&2)!=0);
                                input.journal=chord.back;input.dpad=chord.dpad;
                                input.suppress_pause=chord.consumed;input.menu_modifier=chord.menu_modifier;
                                // Gameplay: physical X never interacts; holding it holds the native wheel (LB).
                                // Menus keep X as the native button. R3 stays native (zoom).
                                const bool controls_focus=input.active && game_xr->state==XR_SESSION_STATE_FOCUSED;
                                if(controls_epoch!=game_xr->pose_epoch()) {controls_epoch=game_xr->pose_epoch();interact_owner.reset();}
                                // Opening the native wheel switches the game state away from gameplay (logic 256);
                                // that UI belongs to the held X until release.
                                x_wheel.update(input.x,controls_focus && !input.menu_modifier && (logic==2 || x_wheel.wheel()));
                                if(logic==2 || x_wheel.wheel()) input.x=false;
                                input.left_grip=0; // Grips belong to the hands; LB comes only from the wheel, after lean filtering.
                                const auto& raw=game_xr->controller_input;
                                const bool interact_target=controller_aim_requested && native_interaction_aim.has_target(GetTickCount64());
                                // Grip = grab and interact; trigger = attack only. A grip press on a hand pointing at
                                // something the game highlights interacts (that grip then neither grabs nor blocks).
                                const auto interaction=interact_owner.update({raw.left_grip,raw.right_grip},
                                    pointing_side.load(),interact_target,x_wheel.wheel(),logic==2 && controls_focus && !input.menu_modifier,GetTickCount64());
                                // Grabbing never fires: a trigger pulled just after (or while holding with) the same grip is swallowed.
                                const auto grab_guard=grip_guard.update({raw.left_grip,raw.right_grip},{raw.left_trigger,raw.right_trigger},
                                    {physical_grab_requested && native_grab.holding(0),physical_grab_requested && native_grab.holding(1)},GetTickCount64());
                                // Present callback owns this cache under log_mutex. Only safe
                                // reads: coarse bounded owner discovery, fixed-slot double guards.
                                const auto lean_tick=GetTickCount64();
                                std::optional<d2vr::NativeLeanSnapshot> lean_snapshot;
                                if(logic==2 && input.active && game_xr->state==XR_SESSION_STATE_FOCUSED) {
                                    auto reader=&read_self;
                                    if(!lean_owner && (!lean_owner_refresh_tick || lean_tick-lean_owner_refresh_tick>=500)) {
                                        lean_owner_refresh_tick=lean_tick;
                                        lean_owner=d2vr::find_native_lean_owner(reader,image_base);
                                    }
                                    if(lean_owner) {
                                        lean_snapshot=d2vr::native_lean_snapshot(reader,image_base,*lean_owner);
                                        if(!lean_snapshot) lean_owner.reset();
                                    }
                                    if(GetTickCount64()-lean_tick>100) lean_snapshot.reset();
                                }
                                if(logic==2) {
                                    if(!lean_snapshot)++lean_snapshot_missing;
                                    else if(!lean_snapshot->gamepad) {
                                        if(lean_snapshot->active)++lean_keyboard_active;else ++lean_keyboard_inactive;
                                    }
                                }
                                input=lean_control.update(input,lean_snapshot,logic==2,
                                    game_xr->state==XR_SESSION_STATE_FOCUSED,game_xr->pose_epoch(),GetTickCount64());
                                if(logic==2 && input.y)++lean_output_y;
                                // Crouch is a right-stick flick down (native B toggle); physical B is the holster.
                                if(logic==2) input.b=crouch_flick.update(input.turn_y,controls_focus && !input.menu_modifier && !input.lean_suspended &&
                                    !input.y && !x_wheel.wheel(),GetTickCount64());
                                if(x_wheel.wheel()) {input.left_grip=1;++wheel_frames;if(logic!=2) wheel_logic=logic;}
                                if(logic==2) {
                                    if(interaction.consume_trigger[0]) physical_left_grip=0;
                                    if(interaction.consume_trigger[1]) input.right_grip=0;
                                }
                                if(physical_grab_requested) {
                                    const bool grab_focus=input.active && game_xr->state==XR_SESSION_STATE_FOCUSED;
                                    const bool grab_use=x_wheel.wheel() || interaction.interact || input.menu_modifier || input.lean_suspended;
                                    // Right grip without a prop stays native RB (block / choke).
                                    input.right_grip=native_grab.filter_native_grip(1,input.right_grip,game_xr->pose_epoch(),GetTickCount64(),logic==2,grab_focus,grab_use);
                                    (void)native_grab.filter_native_grip(0,physical_left_grip,game_xr->pose_epoch(),GetTickCount64(),logic==2,grab_focus,grab_use);
                                }
                                // The sword attacks only by swinging; the right trigger just interacts (pointing).
                                // A left trigger squeezed while the left grip is held (fist/grab) never fires.
                                if(logic==2) {input.right_trigger=0;if(raw.left_grip>.55f) input.left_trigger=0;}
                                // Physical sword swing: a fast right-hand swing presses the native
                                // attack briefly. The trigger keeps working; holding a prop never swings.
                                {
                                    const auto now=GetTickCount64();
                                    if(logic==2 && input.active && game_xr->grip_speeds[1]>2.0f && now>=swing_ready_tick &&
                                       !(physical_grab_requested && native_grab.holding(1)) && !grip_guard.swing_blocked(1,now)) {swing_until_tick=now+250;swing_ready_tick=now+300;++physical_swings;game_xr->pulse(1,.7f,.05f);}
                                    if(physical_grab_requested) for(unsigned side=0;side<2;++side) {
                                        const bool holding=native_grab.holding(side);
                                        if(holding && !grab_was_holding[side]) game_xr->pulse(side,.5f,.04f);
                                        grab_was_holding[side]=holding;
                                    }
                                    if(logic==2 && now<swing_until_tick) input.right_trigger=1;
                                }
                                // A grip press owned by Interact emits native Interact; grabbing never fires a trigger.
                                if(logic==2) {
                                    if(interaction.interact) input.x=true;
                                    if(grab_guard[0]) input.left_trigger=0;
                                    if(grab_guard[1]) input.right_trigger=0;
                                    // Holster: a B press holds native Interact (the game sheathes on a long Interact).
                                    if(holster.update(raw.b,controls_focus && !input.menu_modifier && !x_wheel.wheel() && !input.lean_suspended,
                                        interact_target,raw.left_trigger>.55f || raw.right_trigger>.55f,GetTickCount64())) input.x=true;
                                }
                                if(input.menu_modifier) {
                                    input.move_x=input.move_y=input.turn_x=input.turn_y=0;
                                    input.left_grip=input.right_grip=input.left_trigger=input.right_trigger=0;
                                }
                                if(logic!=2 && x_wheel.wheel()) {
                                    // The open wheel: only LB (held above) and the right stick selection reach the game.
                                    input.move_x=input.move_y=0;input.right_grip=input.left_trigger=input.right_trigger=0;
                                } else if(!logic) {
                                    // Loading/title controls need a fresh verified Iggy owner,
                                    // while world movement, grip actions and triggers stay off.
                                    input.active=input.active && native_menu.interactive_ready(GetTickCount64());
                                    input.move_x=input.move_y=input.turn_x=input.turn_y=0;
                                    input.left_grip=input.right_grip=input.left_trigger=input.right_trigger=0;
                                }
                                if(native_turning_requested) {
                                    native_turning.publish(input,logic==2 && !input.menu_modifier,GetTickCount64(),game_xr->pose_epoch());
                                    native_turning.orient_movement(input,logic==2,GetTickCount64());
                                    if(native_turning.failed() && !native_turning_failure_noted) {native_turning_failure_noted=true;note_recoverable("Native VR turning repeatedly mismatched its feedback; turning disabled");}
                                }
                                controller_bridge.publish(input,logic==2);
                                native_pad_focus.publish(input.active && game_xr->state==XR_SESSION_STATE_FOCUSED,GetTickCount64());
                            }
                            if(xr_submitted>submitted_before && game_logic_mask()==2) ++xr_gameplay_frames;
                        }
                    }
                    // The first GameXr construction/present above may alter
                    // context dispatch. Select the game's final methods now.
                    ensure_game_gpu_hooks(device.Get(),context.Get());
                    if(!gpu_baseline_started) {gpu_baseline_started=true;gpu_phase=1;}
                    else if(stereo_active && GetTickCount64()-stereo_start_tick>=2000 && !gpu_stereo_started) {gpu_stereo_started=true;gpu_phase=2;}
                    else if(headset_size_requested && !gpu_settled_started && gpu_stereo_started && headset_size_applied.load() &&
                            (!optical_density_requested || optical_density_applied) && xr_tracked_transfer_frames>=180 && game_logic_mask()==2) {
                        gpu_settled_started=true;++gpu_settled_passes;gpu_phase=3;
                    }
                }
            } else if(!setup_requested) {
            std::array<std::byte,d2vr::camera_read_size> bytes{};
            SIZE_T read{};
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(camera_address),bytes.data(),bytes.size(),&read) && read==bytes.size()) {
                auto pose=d2vr::decode_game_camera(bytes,image_base);
                if(pose) {
                    ++valid_poses;
                    if(number%6==0) {
                        std::lock_guard<std::mutex> lock(log_mutex);
                        if(capturing.load()) {
                            log_pose(number,0,desc,*pose);
                        }
                    }
                }
            }
            }
        }
    }
    } catch(const std::exception& error) {
        if(xr_requested && capturing.load()) recover_xr_session(error.what());
        else {native_output_size.clear();cached_output_restored=cached_output_size.restore();record_callback_error(error.what());capturing=false;}
    } catch(...) {
        // Never allow exceptions to escape into the game.
        if(xr_requested && capturing.load()) recover_xr_session("Unknown renderer callback exception");
        else {native_output_size.clear();cached_output_restored=cached_output_size.restore();record_callback_error("Unknown renderer callback exception");callback_failed=true;capturing=false;}
    }
    const auto desktop_start_ns=d2vr::GameXr::cpu_time_ns();
    const auto result=original_present(chain,interval,flags);
    if(measure_present) {
        const auto end_ns=d2vr::GameXr::cpu_time_ns();
        previous_desktop_present_ns=end_ns-desktop_start_ns;previous_present_end_ns=end_ns;
    }
    return result;
}

void* find_present() {
    const wchar_t* name=L"Dishonored2VRHiddenProbeWindow";
    WNDCLASSW type{}; type.lpfnWndProc=DefWindowProcW; type.hInstance=module; type.lpszClassName=name;
    if(!RegisterClassW(&type)) throw std::runtime_error("Cannot register hidden graphics probe window");
    HWND window=CreateWindowExW(0,name,L"",WS_POPUP,0,0,16,16,nullptr,nullptr,module,nullptr);
    if(!window) {UnregisterClassW(name,module); throw std::runtime_error("Cannot create hidden graphics probe window");}
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width=16; desc.BufferDesc.Height=16; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=1;
    desc.OutputWindow=window; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> chain;
    HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
                    D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context);
    void* target=nullptr;
    if(SUCCEEDED(result)) {
        target=(*reinterpret_cast<void***>(chain.Get()))[8];
        auto resize=(*reinterpret_cast<void***>(chain.Get()))[13];
        if(original_resize && resize!=resize_target) target=nullptr;
        else resize_target=resize;
    }
    chain.Reset(); context.Reset(); device.Reset(); DestroyWindow(window); UnregisterClassW(name,module);
    if(!target) throw std::runtime_error("Cannot create diagnostic D3D11 swapchain");
    return target;
}

extern "C" __declspec(dllexport) DWORD WINAPI ProbeStatus(void*) {
    return started.load(std::memory_order_acquire) || gpu_bindings.poisoned()?1:0;
}
extern "C" __declspec(dllexport) DWORD WINAPI ProbeCapabilities(void*) {
    return 524287; // Previous capabilities plus passive native weapon attachments.
}

extern "C" __declspec(dllexport) DWORD WINAPI StopProbe(void*) {
    if(!capturing.load(std::memory_order_acquire)) return 2;
    probe_session.request_stop();
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI StartProbe(void*) {
    if(gpu_bindings.poisoned() || started.exchange(true)) return 2;
    fs::path output;
    void* target=nullptr;
    bool enabled=false;
    bool cleanup_exception_triggered=false;
    try {
        {std::lock_guard<std::mutex> lock(log_mutex);
            if(game_xr || graphics_owner.bound())
                return 2; // Retained resources keep this diagnostic unavailable.
        }
        auto root=fs::path(module_path(module)).parent_path().parent_path();
        if(!fs::exists(root/L".git")) return 3;
        output=root/L"artifacts";
        output_directory=output;recovery_log_path=output/L"xr-recoveries.log";
        {std::ofstream reset(recovery_log_path,std::ios::trunc);}recoverable_faults=0;xr_recoveries=0;xr_retry_tick=0;script_capture_next=2000;physical_swings=0;swing_until_tick=swing_ready_tick=0;{std::lock_guard<std::mutex> last_arm_lock(last_arm_mutex);last_arm_tick={};}native_menu_failure_noted=native_turning_failure_noted=native_left_aim_failure_noted=false;
        fs::create_directories(output);
        auto game=fs::path(module_path(nullptr));
        if(_wcsicmp(game.filename().c_str(),L"Dishonored2.exe") || sha256(game)!=d2vr::game_sha256)
            throw std::runtime_error("Unsupported executable: diagnostic was not enabled");
        const auto own_base=reinterpret_cast<std::uintptr_t>(module);
        const auto own_dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto own_pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(own_base+own_dos->e_lfanew);
        crash_observer.install((output/L"native-exception-context.txt").c_str(),own_base,own_pe->OptionalHeader.SizeOfImage);
        std::ifstream config(root/L"build"/L"dxgi-probe.cfg");
        DWORD expected_pid{};
        if(!(config>>expected_pid>>camera_address) || expected_pid!=GetCurrentProcessId())
            throw std::runtime_error("Missing or mismatched process/camera configuration");
        int stereo_flag=0;config>>stereo_flag;
        if(stereo_flag<0 || stereo_flag>6) throw std::runtime_error("Unknown diagnostic mode");
        xr_requested=stereo_flag==5 || stereo_flag==6;head_requested=stereo_flag==6;
        image_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        xr_close_requested=false;xr_close_complete=false;
        game_chain.store(nullptr,std::memory_order_release);native_output_size.clear();
        headset_size_written=false;headset_size_applied=false;headset_size_restored=true;
        cached_output_written=false;cached_output_restored=true;
        native_size_queries=0;native_resizes=0;native_resize_result=S_OK;headset_size_tick=0;requested_output_size={};
        xr_recommended_eye_sizes={};xr_swapchain_eye_sizes={};xr_eye_texture_formats={};
        gpu_render_thread.store(0,std::memory_order_release);gpu_game_context.store(nullptr,std::memory_order_release);
        gpu_game_device.store(nullptr,std::memory_order_release);gpu_draw_samples={};gpu_vertex_draw_samples={};gpu_hand_draw_counts={};
        head_calibration={};head_setup_seen=0;head_matched=0;head_writes=0;head_restores=0;xr_present_thread=0;head_pose_epoch=0;
        head_body_anchor.clear();head_body_camera=0;
        head_anchor_samples=0;
        xr_submitted=0;xr_dropped=0;xr_gameplay_frames=0;xr_menu_layers=0;xr_last_state=0;
        xr_timing_samples=0;previous_present_end_ns=0;previous_desktop_present_ns=0;
        xr_transfer_mask=0;xr_transfer_frame=0;xr_source_lit_fractions={};xr_tracked_transfer_frames=0;xr_live_status_tick=0;
        xr_menu_transfer_mask=0;xr_menu_transfer_frame=0;xr_capture_epoch=0;xr_pose_epoch=0;xr_reference_epoch=0;xr_state_events=0;xr_reference_events=0;
        authored_pair_captured=false;
        authored_capture_stable_frames=0;
        setup_requested=stereo_flag==4;
        stereo_requested=(stereo_flag>0 && stereo_flag<4) || xr_requested;single_eye_requested=stereo_flag==2;paired_requested=stereo_flag==3 || xr_requested;
        int trace_flag=0;config>>trace_flag;
        if(trace_flag<0 || trace_flag>1 || (trace_flag && (!stereo_requested || single_eye_requested)))
            throw std::runtime_error("Unknown GPU diagnostic mode");
        gpu_trace_requested=trace_flag==1;
        int temporal_aa_flag=0;config>>temporal_aa_flag;
        if(temporal_aa_flag<0 || temporal_aa_flag>1 || (temporal_aa_flag && !head_requested))
            throw std::runtime_error("Temporal antialiasing comparison requires head-tracked XR mode");
        temporal_aa_requested=temporal_aa_flag==1;temporal_aa_active=false;temporal_aa_written=false;temporal_aa_restored=true;
        if(temporal_aa_requested) {
            // Exact-build registration gives this bool's name pointer and int
            // cache. Setup reads that cache directly into its skip-TAA flag.
            constexpr std::array<std::byte,7> read_instruction{std::byte{0x83},std::byte{0x3d},std::byte{0x47},std::byte{0x6b},std::byte{0x60},std::byte{0x03},std::byte{0x00}};
            std::array<std::byte,7> actual{};std::uintptr_t name{};
            if(!read_self(image_base+0x3750aa,actual.data(),actual.size()) || actual!=read_instruction ||
               !read_self(image_base+0x397bc00,&name,8) || name!=image_base+0x1b36ac8 ||
               !read_self(image_base+temporal_aa_value_rva,&saved_temporal_aa,4) || saved_temporal_aa!=0)
                throw std::runtime_error("Temporal antialiasing profile or baseline differs; override not enabled");
        }
        int full_scale_flag=0;config>>full_scale_flag;
        if(full_scale_flag<0 || full_scale_flag>1 || (full_scale_flag && !xr_requested))
            throw std::runtime_error("Full render scale comparison requires XR mode");
        full_scale_requested=full_scale_flag==1;full_scale_active={};full_scale_written=false;full_scale_restored=true;
        int symmetric_frustum_flag=0;config>>symmetric_frustum_flag;
        if(symmetric_frustum_flag<0 || symmetric_frustum_flag>1 || (symmetric_frustum_flag && !head_requested))
            throw std::runtime_error("Symmetric frustum comparison requires head-tracked XR mode");
        symmetric_frustum_requested=symmetric_frustum_flag==1;
        int controller_flag=0;config>>controller_flag;
        if(controller_flag<0 || controller_flag>1 || (controller_flag && !head_requested))
            throw std::runtime_error("Controller comparison requires head-tracked XR mode");
        controller_requested=controller_flag==1;controller_import_restored=true;xr_controller_samples=0;
        int headset_size_flag=0;config>>headset_size_flag;
        if(headset_size_flag<0 || headset_size_flag>1 || (headset_size_flag && (!head_requested || !full_scale_requested)))
            throw std::runtime_error("Recommended headset sizing requires head tracking and full render scale");
        headset_size_requested=headset_size_flag==1;
        int eye_boundary_flag=0;config>>eye_boundary_flag;
        if(eye_boundary_flag<0 || eye_boundary_flag>1 || (eye_boundary_flag && !paired_requested))
            throw std::runtime_error("Native eye observation requires paired rendering");
        eye_boundary_requested=eye_boundary_flag==1;
        int native_eye_capture_flag=0;config>>native_eye_capture_flag;
        if(native_eye_capture_flag<0 || native_eye_capture_flag>1 || (native_eye_capture_flag && !eye_boundary_requested))
            throw std::runtime_error("Native eye capture requires the verified eye observer");
        native_eye_capture_requested=native_eye_capture_flag==1;
        int duration_seconds=xr_requested?30:10;config>>duration_seconds;
        simulated_long_wizard=duration_seconds>=60;
        if(duration_seconds==0 && !paired_requested) throw std::runtime_error("Continuous sessions require paired rendering");
        probe_session.begin(duration_seconds,GetTickCount64());xr_exit_requested=false;
        int arm_upload_flag=0;config>>arm_upload_flag;
        if(arm_upload_flag<0 || arm_upload_flag>1 || (arm_upload_flag && !paired_requested))
            throw std::runtime_error("Native arm observation requires paired rendering");
        arm_upload_requested=arm_upload_flag==1;
        int optical_density_flag=0;config>>optical_density_flag;
        if(optical_density_flag<0 || optical_density_flag>1 ||
           (optical_density_flag && (!headset_size_requested || !symmetric_frustum_requested)))
            throw std::runtime_error("Optical density requires symmetric headset-sized rendering");
        optical_density_requested=optical_density_flag==1;optical_density_applied=false;
        int arm_copy_flag=0;config>>arm_copy_flag;
        int tracked_arms_flag=0;config>>tracked_arms_flag;
        if(arm_copy_flag<0 || arm_copy_flag>1 || tracked_arms_flag<0 || tracked_arms_flag>1 ||
           (arm_copy_flag && tracked_arms_flag) || ((arm_copy_flag || tracked_arms_flag) && !arm_upload_requested) ||
           (tracked_arms_flag && (!head_requested || !controller_requested)))
            throw std::runtime_error("Arm palette modes require the native observer; tracked arms require head/controller tracking");
        arm_copy_requested=arm_copy_flag==1;tracked_arms_requested=tracked_arms_flag==1;arm_tracking.clear();
        int comfort_flag=0;config>>comfort_flag;
        if(comfort_flag<0 || comfort_flag>1 || (comfort_flag && !head_requested))
            throw std::runtime_error("Camera comfort requires head-tracked XR mode");
        comfort_requested=comfort_flag==1;comfort_applied=false;comfort_restored=true;
        requested_resolution_multiplier=1.0;effective_resolution_multiplier=1.0;config>>requested_resolution_multiplier;
        if(!std::isfinite(requested_resolution_multiplier) || requested_resolution_multiplier<.5 || requested_resolution_multiplier>2 ||
           (requested_resolution_multiplier!=1.0 && !headset_size_requested))
            throw std::runtime_error("Resolution multiplier requires headset sizing and range 0.5 to 2.0");
        int eye_antialiasing_flag=0;config>>eye_antialiasing_flag;
        if(eye_antialiasing_flag<0 || eye_antialiasing_flag>1 || (eye_antialiasing_flag && !xr_requested))
            throw std::runtime_error("Eye antialiasing requires XR mode");
        eye_antialiasing_requested=eye_antialiasing_flag==1;xr_antialiased_eyes=0;xr_settings_layers=0;xr_resolution_changes=0;settings_transfer_captured=false;
        int native_camera_trace_flag=0;config>>native_camera_trace_flag;
        if(native_camera_trace_flag<0 || native_camera_trace_flag>1)
            throw std::runtime_error("Unknown native gameplay camera trace mode");
        native_camera_trace_requested=native_camera_trace_flag==1;
        if(native_camera_trace_requested && head_requested) {
            head_anchor_csv.open(output/L"rendered-head-anchor.csv",std::ios::trunc);
            if(!head_anchor_csv) throw std::runtime_error("Cannot open rendered camera anchor trace");
            head_anchor_csv << std::setprecision(12) << "present,tick_ms,eye,epoch,reference_epoch,physics_heading,native_pitch,native_yaw,native_roll";
            for(const auto prefix:{"source","anchor"}) {
                for(unsigned i=0;i<3;++i) head_anchor_csv << ',' << prefix << "_p" << i;
                for(unsigned i=0;i<9;++i) head_anchor_csv << ',' << prefix << "_b" << i;
            }
            head_anchor_csv << ",xr_qx,xr_qy,xr_qz,xr_qw,render_p0,render_p1,render_p2";
            for(unsigned i=0;i<9;++i) head_anchor_csv << ",render_b" << i;
            head_anchor_csv << '\n';
        }
        int turning_mode=0;double snap_degrees=45,smooth_rate=90;
        config>>turning_mode>>snap_degrees>>smooth_rate;
        if(turning_mode<0 || turning_mode>2 || !std::isfinite(snap_degrees) || snap_degrees<5 || snap_degrees>180 ||
           !std::isfinite(smooth_rate) || smooth_rate<15 || smooth_rate>360 || (turning_mode && !controller_requested))
            throw std::runtime_error("Native VR turning requires controllers and valid snap/smooth settings");
        turning_config={static_cast<d2vr::TurnControl::Mode>(turning_mode),snap_degrees,smooth_rate};
        native_turning_requested=turning_mode!=0;
        int saved_settings_flag=0;config>>saved_settings_flag;
        if(saved_settings_flag<0 || saved_settings_flag>1 || (saved_settings_flag && !(controller_requested && head_requested && headset_size_requested)))
            throw std::runtime_error("Saved VR settings require headset sizing, head tracking and controllers");
        saved_settings_requested=saved_settings_flag==1;
        if(saved_settings_requested) {
            const auto preferences=d2vr::load_preferences(root/L"build"/L"vr-settings.cfg");
            if(preferences) {
                requested_resolution_multiplier=preferences->resolution;eye_antialiasing_requested=preferences->antialiasing;
                mirror_mode_requested=preferences->mirror;sharpness_requested=preferences->sharpness;taa_preference=preferences->temporal_aa;reticle_preference=preferences->aim_reticle;hint_preference=preferences->controls_hint;
                turning_config=preferences->turning;native_turning_requested=turning_config.mode!=d2vr::TurnControl::Mode::native;
            }
        }
        native_menu_mode=0;config>>native_menu_mode;
        if(native_menu_mode<0 || native_menu_mode>2 || (native_menu_mode==2 && !(controller_requested && head_requested)))
            throw std::runtime_error("Native menu pointer requires tracked controllers");
        int simulated_flag=0;config>>simulated_flag;
        if(simulated_flag<0 || simulated_flag>2 || (simulated_flag && (!xr_requested || duration_seconds==0 || saved_settings_requested)) ||
           (simulated_flag==2 && !controller_requested))
            throw std::runtime_error("Simulated VR requires a timed XR diagnostic without saved preferences");
        simulated_vr_requested=simulated_flag!=0;simulated_input_requested=simulated_flag==2;
        force_xr_fault=simulated_vr_requested && fs::exists(root/L"build"/L"force-xr-fault");
        avatar_calibration_applied=false;avatar_calibration_arm_scale=avatar_calibration_shoulder_drop=0;
        menu_chord={};
        menu_chord_epoch=0;
        lean_control={};lean_owner.reset();lean_owner_refresh_tick=0;
        lean_snapshot_missing=lean_keyboard_inactive=lean_keyboard_active=lean_output_y=0;
        int aim_flag=0;config>>aim_flag;
        if(aim_flag<0 || aim_flag>1 || (aim_flag && !(head_requested && controller_requested)))
            throw std::runtime_error("Controller interaction aim requires tracked controllers");
        controller_aim_requested=aim_flag==1;native_interaction_requested=controller_aim_requested || native_camera_trace_requested;
        int grab_flag=0;config>>grab_flag;
        if(grab_flag<0 || grab_flag>1 || (grab_flag && !(head_requested && controller_requested))) throw std::runtime_error("Physical grabbing requires tracked controllers");
        physical_grab_requested=grab_flag==1;simulated_grab_target.reset();simulated_grab_clock={};simulated_grab_focus_lost_tick=0;
        int left_aim_flag=0;config>>left_aim_flag;
        if(left_aim_flag<0 || left_aim_flag>1 || (left_aim_flag &&
           (!head_requested || !controller_requested || !xr_requested || !paired_requested ||
            native_camera_trace_requested)))
            throw std::runtime_error("Native left-aim requires controller turning without passive aim hooks");
        native_left_aim_requested=left_aim_flag==1;
        int cleanup_exception_flag=0;config>>cleanup_exception_flag;
        if(!d2vr::valid_cleanup_exception_profile(cleanup_exception_flag,simulated_vr_requested,xr_requested,
           paired_requested,native_eye_capture_requested,head_requested,controller_requested,duration_seconds,
           native_left_aim_requested,saved_settings_requested))
            throw std::runtime_error("Cleanup exception fixture requires timed simulated paired native-eye XR with head/controllers and no left aim or saved settings");
        simulated_cleanup_exception_requested=cleanup_exception_flag==1;
        int gameplay_script_flag=0;config>>gameplay_script_flag;
        if(gameplay_script_flag<0 || gameplay_script_flag>1 || (gameplay_script_flag &&
           (!simulated_vr_requested || !simulated_input_requested || !xr_requested || !paired_requested ||
            !native_eye_capture_requested || !head_requested || !controller_requested || duration_seconds==0 ||
            simulated_cleanup_exception_requested || saved_settings_requested)))
            throw std::runtime_error("Gameplay script requires timed simulated paired native-eye head/controllers without cleanup exception or saved settings");
        simulated_gameplay_script_requested=gameplay_script_flag==1;
        if(simulated_gameplay_script_requested && tracked_arms_requested) {
            std::lock_guard<std::mutex> origin_lock(arm_origin_mutex);
            arm_origin_csv.open(output/L"arm-origin-trace.csv",std::ios::trunc);arm_origin_csv << std::setprecision(10) << "kind,present,side,x,y,z\n";
        }
        simulated_gameplay_script={};simulated_gameplay_last_index=-2;simulated_gameplay_rows=0;
        simulated_gameplay_last_gameplay=simulated_gameplay_last_focused=false;
        if(simulated_gameplay_script_requested) {
            std::ifstream source(root/L"build"/L"simulated-gameplay-script.cfg");
            simulated_gameplay_script.load(source,static_cast<unsigned>(duration_seconds));
            simulated_gameplay_csv.open(output/L"simulated-gameplay-commands.csv",std::ios::trunc);
            if(!simulated_gameplay_csv) throw std::runtime_error("Cannot log simulated gameplay commands");
            simulated_gameplay_csv<<std::setprecision(9)<<"elapsed_ms,segment,gameplay,focused,move_x,move_y,turn_x,turn_y,left_trigger,right_trigger,left_grip,right_grip,a,b,x,y,left_click,right_click,menu,head_pitch_degrees\n";
        }
        int attachment_flag=0;config>>attachment_flag;
        if(attachment_flag<0 || attachment_flag>1 || (attachment_flag &&
           (!paired_requested || !xr_requested || !native_eye_capture_requested || !head_requested || !controller_requested || !arm_upload_requested ||
            duration_seconds<10 || duration_seconds>120 || native_left_aim_requested || simulated_cleanup_exception_requested || saved_settings_requested)))
            throw std::runtime_error("Passive native attachments require timed paired native-eye XR head/controllers and arm observer without left aim or saved settings");
        native_weapon_attachment_requested=attachment_flag==1;native_weapon_attachment.clear();
        crash_observer.observe_native_faults(native_camera_trace_requested || native_left_aim_requested || native_weapon_attachment_requested);
        simulated_grab_repositioned=simulated_grab_focus_lost=simulated_grab_focus_restored=false;
        simulated_grab_choice={};simulated_grab_ordinary_pose={};
        simulated_grab_ordinary_frames=simulated_grab_ranged_frames=0;simulated_grab_last_frame=~UINT64{};
        if(physical_grab_requested && simulated_input_requested && !simulated_gameplay_script_requested) {
            const auto owner=d2vr::find_native_player_camera(read_self,image_base);
            if(!owner) throw std::runtime_error("Simulated grab needs a verified native player");
            const auto pos=d2vr::rig::value<std::array<float,3>>(read_self,owner->player+0x300);
            if(pos) {
                const d2vr::Vec3 origin{(*pos)[0],(*pos)[1],(*pos)[2]};
                simulated_grab_target=d2vr::nearest_grab_body(read_self,image_base,origin,2);
                if(!simulated_grab_target) simulated_grab_target=d2vr::nearest_grab_body(read_self,image_base,origin,4,nullptr,d2vr::GrabSearchScope::ControlledFixture);
            }
            if(!simulated_grab_target || !simulated_grab_target->bounds) throw std::runtime_error("No verified rendered dynamic surface for the controlled grab fixture");
        }
        if(comfort_requested) {
            // Exact native consumer reads the verified view_skipShakes cache.
            constexpr std::array<std::byte,7> expected{std::byte{0x83},std::byte{0x3d},std::byte{0xf5},
                std::byte{0x69},std::byte{0x11},std::byte{0x03},std::byte{0x00}};
            std::array<std::byte,7> actual{};
            if(!read_self(image_base+0x88d7cc,actual.data(),actual.size()) || actual!=expected)
                throw std::runtime_error("Native camera shake consumer differs from pinned profile");
        }
        for(unsigned side=0;side<2;++side) {arm_copy_calls[side]=0;arm_pose_calls[side]=0;arm_pose_fallbacks[side]=0;arm_pose_clamps[side]=0;}
        arm_upload_seen={};arm_upload_samples={};arm_upload_rejected={};
        avatar_uploads={};avatar_rig_snapshot.reset();last_avatar_rig.reset();last_verified_avatar_rig.reset();arm_binding_cache.clear();arm_rig_refresh_tick=0;arm_tracking.clear_calibration();
        avatar_fit_reuse.clear();current_avatar_template.reset();last_verified_avatar_template.reset();avatar_pose_gate=false;avatar_template_retry_tick=avatar_resource_check_tick=0;
        avatar_fit_events.clear();
        avatar_fit_template_checks=avatar_fit_reuses=avatar_fit_invalidations=avatar_fit_suspensions=avatar_fit_accepts=0;
        avatar_template_verified=false;avatar_resource_validation_pending=false;avatar_native_bind_palm_span=avatar_native_bind_shoulder_span=0;
        if(arm_upload_requested) {
            auto reader=&read_self;
            if(!arm_binding_cache.refresh(reader,image_base) && game_logic_mask()==2)
                throw std::runtime_error("Current player does not have the supported owned dual-arm rig and bindings");
        }
        native_eye_pair.reset();native_eye_pairs=0;native_eye_copy_count=0;
        eye_begin_calls=0;eye_begin_matches=0;eye_boundary_rows=0;
        {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);eye_sources.invalidate();eye_view_allocation=0;eye_camera_owner={};}
        if(controller_requested) {controller_bridge.install(image_base,0x1a7ecf0);controller_import_restored=false;}
        if(full_scale_requested) {
            constexpr std::array<std::byte,17> expected_reads{std::byte{0xf3},std::byte{0x44},std::byte{0x0f},std::byte{0x10},std::byte{0x05},std::byte{0x53},std::byte{0x4a},std::byte{0x60},std::byte{0x03},
                std::byte{0xf3},std::byte{0x0f},std::byte{0x10},std::byte{0x3d},std::byte{0xcb},std::byte{0x4a},std::byte{0x60},std::byte{0x03}};
            std::array<std::byte,17> actual{};
            if(!read_self(image_base+0x377590,actual.data(),actual.size()) || actual!=expected_reads)
                throw std::runtime_error("Native render-scale reads differ from the exact profile");
            constexpr std::array<std::uintptr_t,2> names{0x1b36f78,0x1b36fc8};
            for(std::size_t axis=0;axis<2;++axis) {
                std::uintptr_t name{};
                if(!read_self(image_base+scale_value_rvas[axis]+4,&name,8) || name!=image_base+names[axis] ||
                   !read_self(image_base+scale_value_rvas[axis],&saved_scale[axis],4) || saved_scale[axis]!=0)
                    throw std::runtime_error("Render-scale profile or baseline differs; override not enabled");
            }
        }
        if(paired_requested && !gpu_trace_requested) throw std::runtime_error("Paired rendering requires its target hooks");
        paired_buffer_binds=0;paired_capture_binds=0;paired_left_ready=false;
        paired_binds_per_eye=0;paired_baseline_samples=0;
        paired_bind_histogram={};captured_logic_mask=0;
        gpu_phase=0;gpu_traced_frames=0;gpu_events=0;gpu_dropped=0;gpu_baseline_started=false;gpu_stereo_started=false;gpu_settled_started=false;gpu_settled_passes=0;
        frames=0;valid_poses=0;stereo_frames=0;distinct_view_frames=0;measured_separation=0;
        callback_failed=false;stereo_active=false;mono_captured=false;stereo_captured=false;
        {std::lock_guard<std::mutex> error_lock(callback_error_mutex);callback_error.clear();}
        mode_restored=true;values_restored=true;baseline_pose.reset();camera_moved=false;
        stereo_ever_enabled=false;left_captured=false;right_phase=false;
        setup_calls=0;setup_valid_pairs=0;setup_matching_pairs=0;
        if(stereo_requested) {
            prepare_stereo();
        } else if(!setup_requested) {
        std::array<std::byte,d2vr::camera_read_size> bytes{}; SIZE_T count{};
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(camera_address),bytes.data(),bytes.size(),&count) ||
           count!=bytes.size() || !d2vr::decode_game_camera(bytes,image_base))
            throw std::runtime_error("Camera identity/pose check failed before hook installation");
        }
        csv.open(output/L"dxgi-camera-probe.csv",std::ios::trunc);
        if(!csv) throw std::runtime_error("Cannot open workspace diagnostic log");
        csv << std::setprecision(9) << "present,tick_ms,eye,width,height,format,hfov,vfov,x,y,z,b00,b01,b02,b10,b11,b12,b20,b21,b22\n";
        if(xr_requested) {
            xr_timing_csv.open(output/L"xr-frame-timing.csv",std::ios::trunc);
            if(!xr_timing_csv) throw std::runtime_error("Cannot open XR frame timing log");
            xr_timing_csv << "present,start_cpu_ns,end_cpu_ns,engine_cpu_gap_ns,previous_desktop_present_ns,pre_xr_cpu_ns,wait_cpu_ns,begin_cpu_ns,locate_input_cpu_ns,transfer_cpu_ns,submit_cpu_ns,display_time_ns,display_period_ns,rendered_display_left_ns,rendered_display_right_ns,rendered_locate_left_cpu_ns,rendered_locate_right_cpu_ns,state,epoch,should_render,submitted,tracked_pair,gameplay,authored_camera,settings,width,height,sync_interval,present_flags,smaa_enabled,capture_readback\n";
        }
        if(stereo_requested && !single_eye_requested && !paired_requested) {
            auto input_deadline=GetTickCount64()+60'000;
            while(!focused_movement() && GetTickCount64()<input_deadline) Sleep(50);
            if(!focused_movement()) throw std::runtime_error("No focused gameplay movement within 60 seconds; diagnostic not enabled");
        }
        target=find_present();
        MH_STATUS status{};
        if(!original_present) {
            auto initialized=MH_Initialize();
            if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED) throw std::runtime_error("MinHook initialization failed");
            if(!read_self(reinterpret_cast<std::uintptr_t>(target),disabled_target_bytes.data(),disabled_target_bytes.size()))
                throw std::runtime_error("Cannot verify Present code before hook installation");
            status=MH_CreateHook(target,reinterpret_cast<void*>(&present_hook),reinterpret_cast<void**>(&original_present));
            if(status!=MH_OK) throw std::runtime_error(std::string("Present hook creation failed: ")+MH_StatusToString(status));
            hook_target=target;
        } else {
            std::array<std::byte,16> current{};
            if(target!=hook_target || !read_self(reinterpret_cast<std::uintptr_t>(target),current.data(),current.size()) || current!=disabled_target_bytes)
                throw std::runtime_error("Present code changed since the prior diagnostic; restart the game");
        }
        if(controller_requested) native_pad_focus.install(image_base,&read_self);
        if(paired_requested) {
            std::array<std::byte,16> current{};
            if(!read_self(reinterpret_cast<std::uintptr_t>(resize_target),current.data(),current.size()))
                throw std::runtime_error("Cannot verify ResizeBuffers entry");
            if(!original_resize) {
                resize_original_bytes=current;
                if(MH_CreateHook(resize_target,reinterpret_cast<void*>(&resize_buffers_hook),reinterpret_cast<void**>(&original_resize))!=MH_OK)
                    throw std::runtime_error("Cannot create backbuffer resize hook");
            } else if(current!=resize_original_bytes) throw std::runtime_error("ResizeBuffers entry changed; restart the game");
            if(MH_EnableHook(resize_target)!=MH_OK) throw std::runtime_error("Cannot enable backbuffer resize hook");
            resize_enabled=true;
        }
        if(headset_size_requested) {
            // Native view construction calls these exact getters (vtable slots
            // +0x218/+0x220). Both read the cached pair at +0x32faf80.
            constexpr std::array<std::array<std::byte,7>,2> expected_getters{{
                {std::byte{0x8b},std::byte{0x05},std::byte{0x1a},std::byte{0x48},std::byte{0xfb},std::byte{0x02},std::byte{0xc3}},
                {std::byte{0x8b},std::byte{0x05},std::byte{0x2e},std::byte{0x48},std::byte{0xfb},std::byte{0x02},std::byte{0xc3}}}};
            constexpr std::array<std::uintptr_t,2> getter_rvas{0x346760,0x346750};
            for(UINT axis=0;axis<2;++axis) {
                std::array<std::byte,7> actual{};
                if(!read_self(image_base+getter_rvas[axis],actual.data(),actual.size()) || actual!=expected_getters[axis])
                    throw std::runtime_error("Native cached output getters differ from the supported executable");
            }
            std::array<std::byte,32> current{};
            auto size_target=reinterpret_cast<void*>(image_base+output_size_rva);
            if(!read_self(image_base+output_size_rva,current.data(),current.size()) || current!=output_size_expected_bytes)
                throw std::runtime_error("Native output-size helper differs from the supported executable");
            if(!original_output_size && MH_CreateHook(size_target,reinterpret_cast<void*>(&output_size_hook),reinterpret_cast<void**>(&original_output_size))!=MH_OK)
                throw std::runtime_error("Cannot create native output-size hook");
            if(MH_EnableHook(size_target)!=MH_OK) throw std::runtime_error("Cannot enable native output-size hook");
            output_size_enabled=true;
        }
        if(setup_requested || head_requested || eye_boundary_requested) {
            setup_target=reinterpret_cast<void*>(image_base+setup_rva);
            if(!read_self(image_base+setup_rva,setup_original_bytes.data(),setup_original_bytes.size()) || setup_original_bytes!=setup_expected_bytes)
                throw std::runtime_error("Native view setup entry differs from supported executable");
            setup_csv.open(output/L"native-view-setup.csv",std::ios::trunc);
            if(!setup_csv) throw std::runtime_error("Cannot open native view setup log");
            setup_csv << std::setprecision(9) << "call,thread,view,copy,hfov,vfov,x,y,z,b00,b01,b02,b10,b11,b12,b20,b21,b22,matched\n";
            if(!original_setup.load()) {
                ViewSetupFn trampoline{};
                if(MH_CreateHook(setup_target,reinterpret_cast<void*>(&view_setup_hook),reinterpret_cast<void**>(&trampoline))!=MH_OK)
                    throw std::runtime_error("Cannot create native view setup observer");
                original_setup.store(trampoline,std::memory_order_release);
            }
            if(MH_EnableHook(setup_target)!=MH_OK) throw std::runtime_error("Cannot enable native view setup observer");
            setup_enabled=true;
        }
        if(eye_boundary_requested) {
            std::array<std::byte,32> bytes{};
            auto eye_target=reinterpret_cast<void*>(image_base+eye_begin_rva);
            if(!read_self(image_base+eye_begin_rva,bytes.data(),bytes.size()) || bytes!=eye_begin_expected_bytes)
                throw std::runtime_error("Native eye begin entry differs from the supported executable");
            eye_boundary_csv.open(output/L"native-eye-boundary.csv",std::ios::trunc);
            if(!eye_boundary_csv) throw std::runtime_error("Cannot open native eye boundary log");
            eye_boundary_csv << "event,present,thread,view,eye,bindings,same_render_thread\n";
            if(!original_eye_begin && MH_CreateHook(eye_target,reinterpret_cast<void*>(&eye_begin_hook),reinterpret_cast<void**>(&original_eye_begin))!=MH_OK)
                throw std::runtime_error("Cannot create native eye begin observer");
            if(MH_EnableHook(eye_target)!=MH_OK) throw std::runtime_error("Cannot enable native eye begin observer");
            eye_boundary_enabled=true;
        }
        if(arm_upload_requested) {
            std::array<std::byte,32> bytes{};
            auto arm_target=reinterpret_cast<void*>(image_base+arm_upload_rva);
            if(!read_self(image_base+arm_upload_rva,bytes.data(),bytes.size()) || bytes!=arm_upload_expected_bytes)
                throw std::runtime_error("Native arm palette consumer differs from the supported executable");
            arm_csv.open(output/L"native-arm-upload.csv",std::ios::trunc);
            avatar_fit_events_csv.open(output/L"avatar-fit-events.csv",std::ios::trunc);
            if(!avatar_fit_events_csv)throw std::runtime_error("Cannot open avatar fit change events");
            d2vr::rig::AvatarFitEvents::header(avatar_fit_events_csv);
            arm_palettes.open(output/L"native-arm-palettes.bin",std::ios::binary|std::ios::trunc);
            if(!arm_csv || !arm_palettes) throw std::runtime_error("Cannot open native arm upload logs");
            arm_csv<<"present,tick_ms,thread,side,animation,model,source,caller_count,first_transform,palette_offset,receipt_serial,native_frame,receipt_frame,receipt_producer_thread,receipt_epoch,receipt_matched,receipt_reason,receipt_game,receipt_source,receipt_remap,receipt_transform_count,receipt_producer_caller_rva,receipt_producer_stage,model_valid,receipt_model_valid,model_matched,tracking_revision,receipt_tracking_revision,tracking_epoch,tracking_reference_epoch,tracking_hands,tracking_calibrated,tracking_matched";
            for(const char* prefix:{"model_base","receipt_model_base"}) for(unsigned i=0;i<15;++i) arm_csv<<','<<prefix<<i;
            arm_csv<<'\n';
            if(!original_arm_upload && MH_CreateHook(arm_target,reinterpret_cast<void*>(&arm_upload_hook),
                                                    reinterpret_cast<void**>(&original_arm_upload))!=MH_OK)
                throw std::runtime_error("Cannot create native arm palette observer");
            if(MH_EnableHook(arm_target)!=MH_OK) throw std::runtime_error("Cannot enable native arm palette observer");
            arm_upload_enabled=true;
        }
        if(gpu_trace_requested) {
            gpu_hand_draw_csv.open(output/L"dxgi-native-hand-draw-candidates.csv",std::ios::trunc);
            if(!gpu_hand_draw_csv)throw std::runtime_error("Cannot open hand draw candidate trace");
            gpu_hand_draw_csv<<"phase,draw_id,unique_candidate,evidence,event,thread,context,context_type,index_count,instances,start_index,base_vertex,first_instance,topology,vertex_shader,pixel_shader,index_buffer,index_format,index_offset,index_byte_width,vertex_slot,vertex_buffer,vertex_stride,vertex_offset,vertex_byte_width\n";
            gpu_csv.open(output/L"dxgi-gpu-trace.csv",std::ios::trunc);
            if(!gpu_csv) throw std::runtime_error("Cannot open GPU trace");
            gpu_csv << "phase,event,thread,context,slot,x,y,width,height,min_depth,max_depth,texture,texture_width,texture_height,format,context_type,element_count,instance_count,draw_id\n";
            // GPU hooks bind on the game render thread after OpenXR setup.
        }
        if(native_camera_trace_requested) {
            native_camera_trace.install(image_base,output/L"native-gameplay-camera.csv",&read_self,&game_logic_mask);
            native_gameplay_aim_trace.install(image_base,&read_self,&game_logic_mask,output/L"native-gameplay-aim.csv");
        }
        if(native_weapon_attachment_requested) {
            native_weapon_attachment.tracking_reader(+[](){return arm_tracking.stamp();});
            native_weapon_attachment.dry_reader(tracked_arms_requested?+[](){return arm_tracking.snapshot();}:nullptr,
                tracked_arms_requested?+[](){return avatar_pose_gate.load(std::memory_order_acquire);}:nullptr);
            native_weapon_attachment.install(image_base,&read_self,&game_logic_mask,output/L"native-weapon-attachment.csv");
        }
        if(native_left_aim_requested) native_left_aim.install(image_base,&read_self,&game_logic_mask,&write_self_aim,output/L"native-left-aim.csv",true);
        if(native_left_aim_requested && controller_aim_requested && !fs::exists(root/L"build"/L"no-hand-crosshair")) {
            try {native_crosshair.install(image_base,&read_self,&write_self_aim,&game_logic_mask,output/L"native-crosshair.csv",true);native_crosshair_installed=true;}
            catch(const std::exception& error) {note_recoverable((std::string("Native crosshair disabled: ")+error.what()).c_str());}
        }
        if(native_left_aim_requested && controller_aim_requested && !fs::exists(root/L"build"/L"no-hand-shots") && !fs::exists(root/L"build"/L"fire-probe")) {
            try {native_shot_aim.install(image_base,&read_self,&game_logic_mask,output/L"native-shot-aim.csv",true);native_shot_aim_installed=true;}
            catch(const std::exception& error) {note_recoverable((std::string("Hand-aimed shots disabled: ")+error.what()).c_str());}
        }
        // Read-only census of HUD element positions (build/hud-census): which elements can be moved into view.
        if(fs::exists(root/L"build"/L"hud-census")) {
            try {native_hud_census.install(image_base,&read_self,output/L"native-hud-census.csv");native_hud_census_installed=true;}
            catch(const std::exception& error) {note_recoverable((std::string("Native HUD census disabled: ")+error.what()).c_str());}
        }
        // Diagnostic only; it hooks the same launch call, so never together with hand-aimed shots.
        if((simulated_gameplay_script_requested || fs::exists(root/L"build"/L"fire-probe")) && !native_shot_aim_installed && !fs::exists(root/L"build"/L"no-fire-seam")) {
            try {native_fire_seam_trace.install(image_base,&read_self,output/L"native-fire-seam.csv");fire_seam_installed=true;}
            catch(const std::exception& error) {note_recoverable((std::string("Fire seam trace disabled: ")+error.what()).c_str());}
        }
        if(native_interaction_requested) native_interaction_aim.install(image_base,&read_self,&game_logic_mask,controller_aim_requested,output/L"native-interaction-rays.csv",native_camera_trace_requested,native_camera_trace_requested);
        if(physical_grab_requested) native_grab.install(image_base,&read_self,&game_logic_mask,output/L"native-physical-grab.csv");
        // Weapons/powers follow the tracked hands (native attachment readers).
        attachment_pose_installed=false;
        if(tracked_arms_requested) {
            try {attachment_pose.install(image_base,&read_self);attachment_pose_installed=true;}
            catch(const std::exception& error) {note_recoverable(error.what());}
        }
        // Game-grip fists; build/fist-mirror-axis containing "off" restores the procedural curl.
        {
            std::string mode;std::ifstream knob(root/L"build"/L"fist-mirror-axis");if(knob) knob>>mode;
            d2vr::rig::ArmTracking::game_grip_fists=mode!="off";
        }
        // Per-eye temporal AA history (docs/image-quality-research.md). build/no-per-eye-taa disables it.
        taa_history_requested=native_eye_capture_requested && head_requested && !fs::exists(root/L"build"/L"no-per-eye-taa");
        taa_history_installed=false;
        if(taa_history_requested) {
            try {taa_history.install(image_base,&read_self);taa_history_installed=true;
                if(simulated_gameplay_script_requested) {
                    taa_capture_csv.open(output/L"taa-constants.csv",std::ios::trunc);taa_capture_csv << std::setprecision(9);
                    {std::lock_guard<std::mutex> dump_lock(taa_view_dump_mutex);taa_view_dumps=0;taa_view_dump.open(output/L"taa-views.bin",std::ios::binary|std::ios::trunc);}
                    taa_capture_after_tick=~ULONGLONG{};taa_history.after_pass=&observe_taa_pass;
                }}
            catch(const std::exception& error) {note_recoverable(error.what());}
        }
        if(native_turning_requested || native_camera_trace_requested || (controller_requested && head_requested && headset_size_requested))
            native_turning.install(image_base,&read_self,&game_logic_mask,turning_config,output/L"native-turning-feedback.csv");
        if(native_menu_mode) native_menu.install(image_base,&read_self,&game_logic_mask,native_menu_mode==2,output/L"native-menu-input.csv");
        probe_session.begin(duration_seconds,GetTickCount64());
        if(comfort_requested) {
            auto reader=&read_self;auto exchange=&exchange_cached_value;
            camera_comfort.apply(image_base,reader,exchange);comfort_applied=true;comfort_restored=false;
        }
        native_cvar_overrides=head_requested?native_cvars.apply(image_base,read_self_ref,taa_history_installed):0;
        if(head_requested && taa_history_installed && !taa_preference) native_cvars.set(d2vr::NativeCvars::skip_temporal_aa,1);
        capturing=true;
        status=MH_EnableHook(target);
        if(status!=MH_OK) {capturing=false; throw std::runtime_error(std::string("Present hook enable failed: ")+MH_StatusToString(status));}
        enabled=true;
        while(probe_session.running(GetTickCount64()) && !callback_failed.load() && !xr_exit_requested.load()) {
            bool submitted{};
            if(simulated_cleanup_exception_requested) {
                std::lock_guard<std::mutex> lock(log_mutex);submitted=xr_submitted>0 && xr_gameplay_frames>=3;
            }
            if(submitted) {
                cleanup_exception_triggered=true;
                throw std::runtime_error("Controlled simulated cleanup exception after submitted XR layer");
            }
            Sleep(20);
        }
        native_output_size.clear();
        cached_output_restored=cached_output_size.restore();
        capturing=false;
        native_left_aim.clear();native_weapon_attachment.clear();
        const bool menu_disabled=native_menu.disable();
        const bool grab_disabled=native_grab.disable();
        if(!grab_disabled) throw std::runtime_error("Native physical grab hook did not restore or drain; restart the game");
        if(attachment_pose_installed && !attachment_pose.disable()) throw std::runtime_error("Native attachment hooks did not restore or drain; restart the game");
        if(taa_history_installed && !taa_history.disable()) throw std::runtime_error("Native TAA history hook did not restore or drain; restart the game");
        const bool interaction_disabled=native_interaction_aim.disable();
        if(!interaction_disabled) throw std::runtime_error("Native interaction ray hooks did not restore or drain; restart the game");
        const bool turning_disabled=native_turning.disable();
        const bool pad_disabled=native_pad_focus.disable();
        if(!turning_disabled) throw std::runtime_error("Native VR turning did not restore or drain; restart the game");
        if(!restore_camera_comfort()) throw std::runtime_error("Camera comfort restoration is unverified; restart the game");
        if(controller_requested) {controller_bridge.publish({},false);controller_import_restored=controller_bridge.disable();}
        if(xr_requested) {
            xr_close_requested.store(true,std::memory_order_release);
            const auto shutdown_deadline=GetTickCount64()+5000;
            while((!xr_close_complete.load(std::memory_order_acquire) || !headset_size_restored.load()) && GetTickCount64()<shutdown_deadline) Sleep(10);
            if(!xr_close_complete.load(std::memory_order_acquire) || !headset_size_restored.load()) {
                // Keep the pending render-thread shutdown and all referenced
                // objects alive. Do not fall back to unsafe worker destruction.
                std::ofstream failure(output/L"dxgi-probe-failure.txt",std::ios::trunc);
                failure << "Render-thread XR shutdown or native output restoration did not complete; restart the game before another test\n";
                failure << "xr_close_complete=" << xr_close_complete.load() << " headset_size_restored=" << headset_size_restored.load()
                        << " cached_output_restored=" << cached_output_restored.load() << " controller_import_restored=" << controller_import_restored
                        << " native_resize_hresult=" << native_resize_result.load() << " last_output=" << width.load() << 'x' << height.load() << '\n';
                return 1;
            }
        }
        const bool resolution_disabled=disable_resolution_hooks();
        const bool arm_upload_disabled=disable_arm_upload_hook();
        if(!arm_upload_disabled) throw std::runtime_error("Native arm observer did not restore or drain; restart the game");
        const bool left_aim_disabled=native_left_aim.disable() && native_crosshair.disable() && native_fire_seam_trace.disable() && native_shot_aim.disable() &&
            native_hud_census.disable();
        native_crosshair_installed=fire_seam_installed=native_shot_aim_installed=false;
        const bool gameplay_aim_disabled=native_gameplay_aim_trace.disable() && left_aim_disabled;
        const bool native_camera_disabled=native_camera_trace.disable() && gameplay_aim_disabled;
        const bool native_attachment_disabled=native_weapon_attachment.disable();
        if(!native_camera_disabled) throw std::runtime_error("Native gameplay camera observer did not restore or drain; restart the game");
        const bool eye_boundary_disabled=disable_eye_boundary_hook();
        const bool setup_disabled=disable_setup_hook();
        auto gpu_disabled=disable_gpu_hooks();
        auto disabled=MH_DisableHook(target);
        enabled=disabled!=MH_OK;
        // Keep the inert DLL and trampoline allocated until normal game exit:
        // a Present call can still have a return address into this DLL.
        std::lock_guard<std::mutex> lock(log_mutex);
        restore_stereo();
        csv.close();
        simulated_gameplay_csv.close();
        xr_timing_csv.close();
        setup_csv.close();
        eye_boundary_csv.close();
        if(setup_disabled) head_anchor_csv.close();
        {std::lock_guard<std::mutex> origin_lock(arm_origin_mutex);arm_origin_csv.close();arm_model_origin_frame=0;}
        taa_history.after_pass=nullptr;
        if(taa_capture_csv.is_open()) taa_capture_csv << "# context_offset," << taa_capture_context_offset.load() << "\n";
        taa_capture_csv.close();
        {std::lock_guard<std::mutex> dump_lock(taa_view_dump_mutex);taa_view_dump.close();}
        {std::lock_guard<std::mutex> arm_lock(arm_log_mutex);arm_csv.close();arm_palettes.close();avatar_fit_events_csv.close();}
        {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);gpu_csv.close();gpu_hand_draw_csv.close();paired_left.Reset();paired_backbuffer.Reset();}
        if(!crash_observer.remove()) throw std::runtime_error("Exception observer did not drain; restart the game");
        std::ofstream result(output/L"dxgi-probe-result.json",std::ios::trunc);
        result << "{\"pid\":" << GetCurrentProcessId() << ",\"presents\":" << frames.load() << ",\"valid_camera_poses\":" << valid_poses.load()
               << ",\"simulated_gameplay_script_requested\":" << (simulated_gameplay_script_requested?"true":"false")
               << ",\"simulated_gameplay_script_segments\":" << simulated_gameplay_script.size()
               << ",\"simulated_gameplay_command_rows\":" << simulated_gameplay_rows
               << ",\"simulated_vr\":" << (simulated_vr_requested?"true":"false")
               << ",\"xr_timing_samples\":" << xr_timing_samples
               << ",\"continuous_session\":" << (duration_seconds==0?"true":"false")
               << ",\"stop_requested\":" << (probe_session.stop_requested()?"true":"false")
               << ",\"xr_exit_requested\":" << (xr_exit_requested.load()?"true":"false")
               << ",\"width\":" << width.load() << ",\"height\":" << height.load() << ",\"format\":" << format.load()
               << ",\"feature_level\":" << feature.load() << ",\"hook_disabled\":" << (disabled==MH_OK?"true":"false")
               << ",\"first_chance_fault_records\":" << crash_observer.records()
               << ",\"controller_aim_requested\":" << (controller_aim_requested?"true":"false")
               << ",\"native_interaction_hook_disabled\":" << (interaction_disabled?"true":"false")
               << ",\"native_interaction_calls\":" << native_interaction_aim.calls()
               << ",\"physical_grab_requested\":" << (physical_grab_requested?"true":"false")
               << ",\"controlled_native_grab_fixture\":" << (physical_grab_requested && simulated_input_requested && !simulated_gameplay_script_requested?"true":"false")
               << ",\"fixture_search_max_native_units\":4,\"fixture_ranged_contact_gap_native_units\":0.28,\"human_reach_acceptance\":false"
               << ",\"fixture_grip_path\":\"" << (simulated_grab_choice.decided()?(simulated_grab_choice.ordinary()?"ordinary":"ranged-development"):"undecided") << '\"'
               << ",\"fixture_initial_ordinary_contact_gap_native_units\":" << simulated_grab_choice.initial_gap()
               << ",\"fixture_ordinary_frames\":" << simulated_grab_ordinary_frames
               << ",\"fixture_ranged_frames\":" << simulated_grab_ranged_frames
               << ",\"fixture_second_surface_refreshed\":" << (simulated_grab_repositioned?"true":"false")
               << ",\"physical_grab_hook_disabled\":" << (grab_disabled?"true":"false")
               << ",\"physical_grab_picked\":" << native_grab.picked()
               << ",\"physical_grab_moved\":" << native_grab.moved()
               << ",\"physical_grab_dropped\":" << native_grab.dropped()
               << ",\"physical_grab_lost_ownership\":" << native_grab.lost()
               << ",\"physical_grab_queries\":" << native_grab.queries()
               << ",\"physical_grab_callbacks\":" << native_grab.callbacks()
               << ",\"right_grip_native_presses\":" << native_grab.native_presses()
               << ",\"right_grip_physical_presses\":" << native_grab.physical_presses()
               << ",\"native_interaction_rays\":" << native_interaction_aim.rays()
               << ",\"native_interaction_routed\":" << native_interaction_aim.routed()
               << ",\"native_interaction_rejected\":" << native_interaction_aim.rejected()
               << ",\"native_interaction_overlapping_jobs\":" << native_interaction_aim.overlap()
               << ",\"native_lean_query_rows\":" << native_interaction_aim.lean_rows()
               << ",\"native_lean_snapshot_missing\":" << lean_snapshot_missing
               << ",\"native_lean_keyboard_inactive\":" << lean_keyboard_inactive
               << ",\"native_lean_keyboard_active\":" << lean_keyboard_active
               << ",\"native_lean_output_y_frames\":" << lean_output_y
               << ",\"native_lean_query_dropped\":" << native_interaction_aim.lean_dropped()
               << ",\"native_lean_query_rejected\":" << native_interaction_aim.lean_rejected()
               << ",\"native_lean_query_queues\":" << native_interaction_aim.lean_queues()
               << ",\"native_lean_query_trace_failed\":" << (native_interaction_aim.lean_trace_failed()?"true":"false")
               << ",\"native_lean_lifetime_rows\":" << native_interaction_aim.lean_lifetime_rows()
               << ",\"native_lean_lifetime_dropped\":" << native_interaction_aim.lean_lifetime_dropped()
               << ",\"native_lean_lifetime_rejected\":" << native_interaction_aim.lean_lifetime_rejected()
               << ",\"native_lean_lifetime_overflow\":" << native_interaction_aim.lean_lifetime_overflow()
               << ",\"native_lean_lifetime_failed\":" << (native_interaction_aim.lean_lifetime_failed()?"true":"false")
               << ",\"native_camera_trace_requested\":" << (native_camera_trace_requested?"true":"false")
               << ",\"native_camera_trace_hook_disabled\":" << (native_camera_disabled?"true":"false")
               << ",\"native_gameplay_aim_hook_disabled\":" << (gameplay_aim_disabled?"true":"false")
               << ",\"native_gameplay_aim_calls\":" << native_gameplay_aim_trace.calls()
               << ",\"native_gameplay_aim_samples\":" << native_gameplay_aim_trace.samples()
               << ",\"native_gameplay_aim_overlaps\":" << native_gameplay_aim_trace.overlap()
               << ",\"native_left_aim_requested\":" << (native_left_aim_requested?"true":"false")
               << ",\"native_left_aim_hook_disabled\":" << (left_aim_disabled?"true":"false")
               << ",\"native_left_aim_calls\":" << native_left_aim.calls()
               << ",\"native_left_aim_candidates\":" << native_left_aim.candidates()
               << ",\"native_left_aim_applied\":" << native_left_aim.applied()
               << ",\"native_left_aim_rejected\":" << native_left_aim.rejected()
               << ",\"native_left_aim_overlaps\":" << native_left_aim.overlap()
               << ",\"native_left_aim_write_failures\":" << native_left_aim.write_failures()
               << ",\"native_left_aim_rollback_failures\":" << native_left_aim.rollback_failures()
               << ",\"native_left_aim_failed\":" << (native_left_aim.failed()?"true":"false")
               << ",\"native_camera_update_calls\":" << native_camera_trace.calls()
               << ",\"native_camera_trace_samples\":" << native_camera_trace.samples()
               << ",\"native_camera_trace_rejected\":" << native_camera_trace.rejected()
               << ",\"native_camera_trace_errors\":" << native_camera_trace.errors()
               << ",\"native_turning_requested\":" << (native_turning_requested?"true":"false")
               << ",\"native_turning_mode\":" << static_cast<int>(turning_config.mode)
               << ",\"native_snap_degrees\":" << turning_config.snap_degrees
               << ",\"native_smooth_degrees_per_second\":" << turning_config.smooth_degrees_per_second
               << ",\"native_turning_hook_disabled\":" << (turning_disabled?"true":"false")
               << ",\"native_turning_update_calls\":" << native_turning.calls()
               << ",\"native_turning_applied\":" << native_turning.applied()
               << ",\"native_head_follow_applied\":" << native_turning.head_applied()
               << ",\"native_turning_verified\":" << native_turning.verified()
               << ",\"native_turning_cancelled\":" << native_turning.cancelled()
               << ",\"native_menu_mode\":" << native_menu_mode
               << ",\"native_menu_hook_disabled\":" << (menu_disabled?"true":"false")
               << ",\"native_menu_calls\":" << native_menu.calls()
               << ",\"native_menu_rejected\":" << native_menu.rejected()
               << ",\"native_menu_pointer_moves\":" << native_menu.moves()
               << ",\"native_menu_button_events\":" << native_menu.buttons()
               << ",\"native_menu_failed\":" << (native_menu.failed()?"true":"false")
               << ",\"native_menu_error\":" << std::quoted(native_menu.error())
               << ",\"native_turning_failed\":" << (native_turning.failed()?"true":"false")
               << ",\"camera_comfort_requested\":" << (comfort_requested?"true":"false")
               << ",\"camera_comfort_applied\":" << (comfort_applied?"true":"false")
               << ",\"camera_comfort_restored\":" << (comfort_restored?"true":"false")
               << ",\"camera_comfort_preserved_changes\":" << camera_comfort.preserved_changes()
               << ",\"callback_failed\":" << (callback_failed.load()?"true":"false")
               << ",\"callback_error\":" << std::quoted(callback_error_snapshot())
               << ",\"native_cvar_overrides\":" << native_cvar_overrides
               << ",\"physical_swings\":" << physical_swings
               << ",\"taa_history_installed\":" << (taa_history_installed?"true":"false") << ",\"taa_passes\":" << taa_history.passes()
               << ",\"taa_right_swaps\":" << taa_history.right_swaps() << ",\"taa_left_flips\":" << taa_history.left_flips()
               << ",\"taa_fallbacks\":" << taa_history.fallbacks() << ",\"taa_rebuild_waits\":" << taa_history.rebuild_waits()
               << ",\"taa_idle\":" << taa_history.idle() << ",\"taa_rebuilds\":" << taa_history.rebuilds() << ",\"taa_failures\":" << taa_history.failures()
               << ",\"attachment_substitutions\":" << attachment_pose.substitutions() << ",\"attachment_calls\":" << attachment_pose.calls()
               << ",\"xr_recoveries\":" << xr_recoveries << ",\"recoverable_faults\":" << recoverable_faults.load()
               << ",\"arm_upload_requested\":" << (arm_upload_requested?"true":"false")
               << ",\"arm_upload_hook_disabled\":" << (arm_upload_disabled?"true":"false")
               << ",\"arm_upload_seen\":[" << arm_upload_seen[0] << ',' << arm_upload_seen[1] << ']'
               << ",\"arm_upload_samples\":[" << arm_upload_samples[0] << ',' << arm_upload_samples[1] << ']'
               << ",\"arm_upload_rejected\":[" << arm_upload_rejected[0] << ',' << arm_upload_rejected[1] << ']'
               << ",\"arm_palette_copy_requested\":" << (arm_copy_requested?"true":"false")
               << ",\"tracked_arms_requested\":" << (tracked_arms_requested?"true":"false")
               << ",\"native_weapon_attachment_trace_requested\":" << (native_weapon_attachment_requested?"true":"false")
               << ",\"native_weapon_attachment_trace_disabled\":" << (native_attachment_disabled?"true":"false")
               << ",\"native_weapon_attachment_calls\":" << native_weapon_attachment.calls()
               << ",\"native_weapon_attachment_samples\":" << native_weapon_attachment.samples()
               << ",\"native_weapon_attachment_rejected\":" << native_weapon_attachment.rejected()
               << ",\"native_weapon_attachment_source_leaves\":" << native_weapon_attachment.leaves()
               << ",\"native_weapon_attachment_nested\":" << native_weapon_attachment.nested()
               << ",\"native_weapon_attachment_errors\":" << native_weapon_attachment.errors()
               << ",\"native_weapon_model_outer_calls\":" << native_weapon_attachment.outer_calls()
               << ",\"native_weapon_model_owned_outer\":" << native_weapon_attachment.owned_outer()
               << ",\"native_weapon_model_secondary_leaves\":" << native_weapon_attachment.secondary_calls()
               << ",\"native_weapon_model_matches\":" << native_weapon_attachment.model_matches()
               << ",\"native_weapon_model_mismatches\":" << native_weapon_attachment.model_mismatches()
               << ",\"native_weapon_model_observation_rows\":" << native_weapon_attachment.model_observations()
               << ",\"native_weapon_model_gpu_observations\":" << native_weapon_attachment.gpu_observations()
               << ",\"native_weapon_dry_oracle_requested\":" << (native_weapon_attachment_requested && tracked_arms_requested?"true":"false")
               << ",\"native_weapon_dry_active_render_baseline\":" << (native_weapon_attachment_requested && tracked_arms_requested?"true":"false")
               << ",\"native_weapon_dry_outputs_changed\":false"
               << ",\"native_weapon_dry_prepared\":" << native_weapon_attachment.dry().prepared()
               << ",\"native_weapon_dry_completed\":" << native_weapon_attachment.dry().completed()
               << ",\"native_weapon_dry_secondary_matches\":" << native_weapon_attachment.dry().secondary_matches()
               << ",\"native_weapon_dry_before_primary_failures\":" << native_weapon_attachment.dry().before_failures()
               << ",\"native_weapon_dry_after_primary_failures\":" << native_weapon_attachment.dry().after_failures()
               << ",\"native_weapon_dry_revoked\":" << native_weapon_attachment.dry().revoked()
               << ",\"native_weapon_equipment_absent\":" << native_weapon_attachment.equipment_absent()
               << ",\"native_weapon_equipment_changed\":" << native_weapon_attachment.equipment_changed()
               << ",\"native_weapon_equipment_mismatch\":" << native_weapon_attachment.equipment_mismatch()
               << ",\"native_weapon_equipment_refreshes\":" << native_weapon_attachment.equipment_refreshes()
               << ",\"native_weapon_source_calls\":" << native_weapon_attachment.source_receipts().calls()
               << ",\"native_weapon_source_receipts\":" << native_weapon_attachment.source_receipts().receipts()
               << ",\"native_weapon_source_matches\":" << native_weapon_attachment.source_receipts().matches()
               << ",\"native_weapon_source_unmatched\":" << native_weapon_attachment.source_receipts().unmatched()
               << ",\"native_weapon_source_overlaps\":" << native_weapon_attachment.source_receipts().overlaps()
               << ",\"native_weapon_source_noncanonical\":" << native_weapon_attachment.source_receipts().noncanonical()
               << ",\"native_weapon_source_errors\":" << native_weapon_attachment.source_receipts().errors()
               << ",\"native_weapon_source_hook_rva\":5507728,\"native_weapon_source_phase6_verified\":false"
               << ",\"native_weapon_source_stage_calls\":[" << native_weapon_attachment.source_receipts().stage_calls(0) << ','
               << native_weapon_attachment.source_receipts().stage_calls(1) << ',' << native_weapon_attachment.source_receipts().stage_calls(2) << ','
               << native_weapon_attachment.source_receipts().stage_calls(3) << ',' << native_weapon_attachment.source_receipts().stage_calls(4) << ']'
               << ",\"native_weapon_source_owned_stage_calls\":[" << native_weapon_attachment.source_receipts().stage_calls(0,true) << ','
               << native_weapon_attachment.source_receipts().stage_calls(1,true) << ',' << native_weapon_attachment.source_receipts().stage_calls(2,true) << ','
               << native_weapon_attachment.source_receipts().stage_calls(3,true) << ',' << native_weapon_attachment.source_receipts().stage_calls(4,true) << ']'
               << ",\"native_weapon_source_caller_counts\":[" << native_weapon_attachment.source_receipts().caller_calls(0) << ','
               << native_weapon_attachment.source_receipts().caller_calls(1) << ',' << native_weapon_attachment.source_receipts().caller_calls(2) << ','
               << native_weapon_attachment.source_receipts().caller_calls(3) << ',' << native_weapon_attachment.source_receipts().caller_calls(4) << ','
               << native_weapon_attachment.source_receipts().caller_calls(5) << ',' << native_weapon_attachment.source_receipts().caller_calls(6) << ','
               << native_weapon_attachment.source_receipts().caller_calls(7) << ']'
               << ",\"native_weapon_source_owned_caller_counts\":[" << native_weapon_attachment.source_receipts().caller_calls(0,true) << ','
               << native_weapon_attachment.source_receipts().caller_calls(1,true) << ',' << native_weapon_attachment.source_receipts().caller_calls(2,true) << ','
               << native_weapon_attachment.source_receipts().caller_calls(3,true) << ',' << native_weapon_attachment.source_receipts().caller_calls(4,true) << ','
               << native_weapon_attachment.source_receipts().caller_calls(5,true) << ',' << native_weapon_attachment.source_receipts().caller_calls(6,true) << ','
               << native_weapon_attachment.source_receipts().caller_calls(7,true) << ']'
               << ",\"native_weapon_source_last_owned_caller_rva\":" << native_weapon_attachment.source_receipts().last_owned_caller()
               << ",\"native_weapon_source_last_owned_stage\":" << native_weapon_attachment.source_receipts().last_owned_stage()
               << ",\"native_weapon_attachment_writes\":false,\"native_weapon_slot_equivalence_verified\":false"
               << ",\"native_arm_copy_calls\":[" << arm_copy_calls[0].load() << ',' << arm_copy_calls[1].load() << ']'
               << ",\"native_arm_pose_calls\":[" << arm_pose_calls[0].load() << ',' << arm_pose_calls[1].load() << ']'
               << ",\"native_arm_pose_fallbacks\":[" << arm_pose_fallbacks[0].load() << ',' << arm_pose_fallbacks[1].load() << ']'
               << ",\"native_arm_reach_clamps\":[" << arm_pose_clamps[0].load() << ',' << arm_pose_clamps[1].load() << ']'
               << ",\"tracked_arm_units_per_metre\":1.0,\"tracked_arm_metric_scale_verified\":false"
               << ",\"avatar_calibration_applied\":" << (avatar_calibration_applied?"true":"false")
               << ",\"avatar_fit_template_verified\":" << (avatar_template_verified?"true":"false")
               << ",\"avatar_fit_template_checks\":" << avatar_fit_template_checks
               << ",\"avatar_fit_reuses\":" << avatar_fit_reuses
               << ",\"avatar_fit_invalidations\":" << avatar_fit_invalidations
               << ",\"avatar_fit_suspensions\":" << avatar_fit_suspensions
               << ",\"avatar_fit_accepts\":" << avatar_fit_accepts
               << ",\"avatar_fit_event_rows\":" << avatar_fit_events.rows()
               << ",\"avatar_fit_event_dropped\":" << avatar_fit_events.dropped()
               << ",\"avatar_fit_resource_check_bound_ms\":500"
               << ",\"avatar_native_bind_palm_span_native_units\":" << avatar_native_bind_palm_span
               << ",\"avatar_native_bind_shoulder_span_native_units\":" << avatar_native_bind_shoulder_span
               << ",\"avatar_fit_active_at_stop\":" << (avatar_fit_reuse.active_fit().has_value()?"true":"false")
               << ",\"avatar_pose_gate_at_stop\":" << (avatar_pose_gate.load()?"true":"false")
               << ",\"avatar_resource_validation_pending\":" << (avatar_resource_validation_pending?"true":"false")
               << ",\"avatar_calibration_arm_scale\":" << avatar_calibration_arm_scale
               << ",\"avatar_calibration_eye_to_shoulder\":" << avatar_calibration_shoulder_drop
               << ",\"arm_palette_record_bytes\":4476,\"animation_writes\":false"
               << ",\"stereo_requested\":" << (stereo_requested?"true":"false")
               << ",\"stereo_frames\":" << stereo_frames.load() << ",\"distinct_view_frames\":" << distinct_view_frames.load()
               << ",\"measured_separation_game_units\":" << measured_separation.load()
               << ",\"mode_restored\":" << (mode_restored?"true":"false") << ",\"values_restored\":" << (values_restored?"true":"false")
               << ",\"mono_captured\":" << (mono_captured?"true":"false") << ",\"stereo_captured\":" << (stereo_captured?"true":"false")
               << ",\"camera_moved_before_stereo\":" << (camera_moved?"true":"false")
               << ",\"single_eye_requested\":" << (single_eye_requested?"true":"false") << ",\"left_captured\":" << (left_captured?"true":"false")
               << ",\"backend_hook_disabled\":true"
               << ",\"view_setup_requested\":" << (setup_requested?"true":"false") << ",\"view_setup_hook_disabled\":" << (setup_disabled?"true":"false")
               << ",\"view_setup_calls\":" << setup_calls.load() << ",\"view_setup_valid_pairs\":" << setup_valid_pairs.load()
               << ",\"view_setup_matching_pairs\":" << setup_matching_pairs.load()
               << ",\"eye_boundary_requested\":" << (eye_boundary_requested?"true":"false")
               << ",\"eye_boundary_hook_disabled\":" << (eye_boundary_disabled?"true":"false")
               << ",\"eye_begin_calls\":" << eye_begin_calls.load() << ",\"eye_begin_matches\":" << eye_begin_matches.load()
               << ",\"native_eye_capture_requested\":" << (native_eye_capture_requested?"true":"false")
               << ",\"native_eye_pairs\":" << native_eye_pairs << ",\"native_eye_copy_count\":" << native_eye_copy_count
               << ",\"gpu_trace_requested\":" << (gpu_trace_requested?"true":"false")
               << ",\"gpu_traced_frames\":" << gpu_traced_frames.load() << ",\"gpu_events\":" << gpu_events.load()
               << ",\"settled_gpu_trace_armed\":" << (gpu_settled_started?"true":"false")
               << ",\"gpu_dropped_events\":" << gpu_dropped.load() << ",\"gpu_hooks_disabled\":" << (gpu_disabled?"true":"false")
               << ",\"gpu_indexed_draw_samples\":[" << gpu_draw_samples[1] << ',' << gpu_draw_samples[2] << ',' << gpu_draw_samples[3] << ']'
               << ",\"gpu_vertex_draw_samples\":[" << gpu_vertex_draw_samples[1] << ',' << gpu_vertex_draw_samples[2] << ',' << gpu_vertex_draw_samples[3] << ']'
               << ",\"gpu_dispatch_rebindings\":" << gpu_bindings.rebinds()
               << ",\"gpu_dispatch_known_binding_reuses\":" << gpu_bindings.reuses()
               << ",\"gpu_dispatch_retained_bindings\":" << gpu_bindings.created()
               << ",\"gpu_dispatch_poisoned\":" << (gpu_bindings.poisoned()?"true":"false")
               << ",\"gpu_dispatch_reason\":" << std::quoted(gpu_bindings.reason())
               << ",\"gpu_deferred_methods_shared\":[" << (gpu_deferred_methods_shared[0]?"true":"false") << ','
               << (gpu_deferred_methods_shared[1]?"true":"false") << ',' << (gpu_deferred_methods_shared[2]?"true":"false") << ','
               << (gpu_deferred_methods_shared[3]?"true":"false") << ',' << (gpu_deferred_methods_shared[4]?"true":"false") << ','
               << (gpu_deferred_methods_shared[5]?"true":"false") << ',' << (gpu_deferred_methods_shared[6]?"true":"false") << ']'
               << ",\"paired_requested\":" << (paired_requested?"true":"false") << ",\"paired_capture_buffer_binds\":" << paired_capture_binds
               << ",\"paired_bindings_per_eye\":" << paired_binds_per_eye << ",\"paired_baseline_samples\":" << paired_baseline_samples
               << ",\"game_logic_mask_at_capture\":" << captured_logic_mask << ",\"gameplay_at_capture\":" << (captured_logic_mask==2?"true":"false")
               << ",\"camera_object_writes\":" << (native_left_aim.applied()>0?"true":"false")
               << ",\"native_stereo_settings_written\":" << (stereo_ever_enabled?"true":"false")
               << ",\"vr_rendering\":" << (xr_submitted>0?"true":"false") << ",\"xr_requested\":" << (xr_requested?"true":"false")
               << ",\"xr_submitted_game_layers\":" << xr_submitted << ",\"xr_dropped_layers\":" << xr_dropped
               << ",\"xr_gameplay_layers\":" << xr_gameplay_frames << ",\"xr_last_session_state\":" << xr_last_state
               << ",\"xr_menu_quad_layers\":" << xr_menu_layers
               << ",\"xr_render_thread_shutdown\":" << (xr_close_complete.load()?"true":"false")
               << ",\"xr_transfer_pixels_equal\":" << (xr_transfer_mask==3?"true":"false")
               << ",\"xr_capture_pose_epoch\":" << xr_capture_epoch << ",\"xr_last_pose_epoch\":" << xr_pose_epoch
               << ",\"xr_state_events\":" << xr_state_events << ",\"xr_reference_events\":" << xr_reference_events
               << ",\"xr_reference_epoch\":" << xr_reference_epoch
               << ",\"xr_menu_transfer_pixels_equal\":" << (xr_menu_transfer_mask==3?"true":"false")
               << ",\"tracked_gameplay_transfer_frames\":" << xr_tracked_transfer_frames
               << ",\"xr_source_lit_fractions\":[" << xr_source_lit_fractions[0] << ',' << xr_source_lit_fractions[1] << ']'
               << ",\"xr_swapchain_eye_sizes\":[[" << xr_swapchain_eye_sizes[0].width << ',' << xr_swapchain_eye_sizes[0].height << "],[" << xr_swapchain_eye_sizes[1].width << ',' << xr_swapchain_eye_sizes[1].height << "]]"
               << ",\"xr_eye_texture_formats\":[" << xr_eye_texture_formats[0] << ',' << xr_eye_texture_formats[1] << ']'
               << ",\"xr_recommended_eye_sizes\":[[" << xr_recommended_eye_sizes[0].width << ',' << xr_recommended_eye_sizes[0].height << "],[" << xr_recommended_eye_sizes[1].width << ',' << xr_recommended_eye_sizes[1].height << "]]"
               << ",\"full_render_scale_requested\":" << (full_scale_requested?"true":"false") << ",\"full_render_scale_written\":" << (full_scale_written?"true":"false")
               << ",\"full_render_scale_restored\":" << (full_scale_restored?"true":"false")
               << ",\"headset_size_requested\":" << (headset_size_requested?"true":"false")
               << ",\"headset_size_written\":" << (headset_size_written.load()?"true":"false")
               << ",\"headset_size_applied\":" << (headset_size_applied.load()?"true":"false")
               << ",\"headset_size_restored\":" << (headset_size_restored.load()?"true":"false")
               << ",\"cached_output_written\":" << (cached_output_written?"true":"false")
               << ",\"cached_output_restored\":" << (cached_output_restored.load()?"true":"false")
               << ",\"requested_native_output\":[" << requested_output_size.width << ',' << requested_output_size.height << ']'
               << ",\"optical_density_requested\":" << (optical_density_requested?"true":"false")
               << ",\"optical_density_request_applied\":" << (optical_density_applied?"true":"false")
               << ",\"resolution_multiplier_requested\":" << requested_resolution_multiplier
               << ",\"resolution_multiplier_effective\":" << effective_resolution_multiplier
               << ",\"eye_antialiasing_requested\":" << (eye_antialiasing_requested?"true":"false")
               << ",\"smaa_eye_passes\":" << xr_antialiased_eyes
               << ",\"vr_settings_layers\":" << xr_settings_layers
               << ",\"vr_resolution_changes\":" << xr_resolution_changes
               << ",\"native_size_queries\":" << native_size_queries.load() << ",\"native_resizes\":" << native_resizes.load()
               << ",\"native_resize_hresult\":" << native_resize_result.load()
               << ",\"resolution_hooks_disabled\":" << (resolution_disabled?"true":"false")
               << ",\"temporal_aa_override_requested\":" << (temporal_aa_requested?"true":"false")
               << ",\"temporal_aa_override_written\":" << (temporal_aa_written?"true":"false")
               << ",\"temporal_aa_original\":" << saved_temporal_aa << ",\"temporal_aa_restored\":" << (temporal_aa_restored?"true":"false")
               << ",\"head_tracking_requested\":" << (head_requested?"true":"false")
               << ",\"symmetric_frustum_requested\":" << (symmetric_frustum_requested?"true":"false") << ",\"head_setup_seen\":" << head_setup_seen
               << ",\"head_pose_matched\":" << head_matched << ",\"head_pose_writes\":" << head_writes << ",\"head_source_restores\":" << head_restores
               << ",\"head_tracking_applied\":" << (head_writes>0?"true":"false")
               << ",\"render_view_pose_writes\":" << (head_writes>0?"true":"false") << ",\"roomscale_translation_applied\":true"
               << ",\"controller_requested\":" << (controller_requested?"true":"false")
               << ",\"xr_controller_samples\":" << xr_controller_samples
               << ",\"native_controller_reads\":" << controller_bridge.reads()
               << ",\"native_vr_controller_reads\":" << controller_bridge.virtual_reads()
               << ",\"native_nonzero_vr_controller_reads\":" << controller_bridge.nonzero_reads() << ",\"native_lb_reads\":" << controller_bridge.lb_reads() << ",\"wheel_frames\":" << wheel_frames << ",\"hint_layers\":" << (game_xr?game_xr->hint_layers:0) << ",\"lb_inputs\":" << controller_bridge.lb_inputs() << ",\"lb_published\":" << controller_bridge.lb_published() << ",\"wheel_logic\":" << wheel_logic << ",\"native_x_reads\":" << controller_bridge.x_reads()
               << ",\"native_pause_button_reads\":" << controller_bridge.pause_reads()
               << ",\"head_anchor_trace_samples\":" << head_anchor_samples
               << ",\"head_calibration_captures\":" << head_calibration.captures()
               << ",\"native_joystick_focus_calls\":" << native_pad_focus.calls()
               << ",\"native_joystick_focus_overrides\":" << native_pad_focus.overrides()
               << ",\"native_joystick_wakeups\":" << native_pad_focus.wakeups()
               << ",\"native_pad_poll_gates\":" << native_pad_focus.poll_calls()
               << ",\"native_pad_serializer_gates\":" << native_pad_focus.serializer_calls()
               << ",\"native_pad_window_gates\":" << native_pad_focus.window_calls()
               << ",\"native_pad_window_overrides\":" << native_pad_focus.window_overrides()
               << ",\"native_joystick_focus_restored\":" << (pad_disabled?"true":"false")
               << ",\"controller_connection_attempted\":" << (controller_bridge.connection_attempted()?"true":"false")
               << ",\"controller_import_restored\":" << (controller_import_restored?"true":"false")
               << ",\"controller_actions_routed\":" << (controller_requested && xr_controller_samples && controller_bridge.virtual_reads()?"true":"false") << ",\"playable_verified\":false"
               << ",\"dll_retained_until_game_exit\":true}\n";
        if(paired_requested) {
            std::ofstream histogram(output/L"paired-bind-counts.csv",std::ios::trunc);
            histogram << "bindings,presents\n";
            for(std::size_t i=0;i<paired_bind_histogram.size();++i) histogram << i << ',' << paired_bind_histogram[i] << '\n';
        }
        bool stereo_success=!stereo_requested || (stereo_captured && (single_eye_requested || paired_requested?left_captured:distinct_view_frames.load()>0) && mode_restored && values_restored);
        bool gpu_success=!gpu_trace_requested || (gpu_traced_frames.load()==2u+gpu_settled_passes && gpu_events.load()>0 && gpu_dropped.load()==0);
        const bool setup_success=!setup_requested || (setup_valid_pairs.load()>0 && setup_matching_pairs.load()==setup_valid_pairs.load());
        const bool native_camera_success=native_camera_disabled && native_attachment_disabled && (!native_weapon_attachment_requested ||
            (native_weapon_attachment.errors()==0 && native_weapon_attachment.source_receipts().errors()==0)) && (!native_camera_trace_requested ||
            (native_camera_trace.samples()>0 && native_camera_trace.errors()==0)) && left_aim_disabled &&
            (!native_left_aim_requested || (native_left_aim.applied()>0 && !native_left_aim.failed() &&
                native_left_aim.overlap()==0 && native_left_aim.write_failures()==0 && native_left_aim.rollback_failures()==0));
        const bool turning_success=turning_disabled && (!native_turning_requested || !native_turning.failed()) && menu_disabled && !native_menu.failed();
        if(disabled==MH_OK && gpu_disabled && setup_disabled && arm_upload_disabled && native_camera_disabled && native_attachment_disabled && grab_disabled && interaction_disabled && turning_disabled && menu_disabled && pad_disabled && eye_boundary_disabled && resolution_disabled && cached_output_restored.load() && headset_size_restored.load() && controller_import_restored && comfort_restored && mode_restored && values_restored && temporal_aa_restored && full_scale_restored && !graphics_owner.bound() && (!xr_requested || xr_close_complete.load())) started=false;
        return disabled==MH_OK && grab_disabled && (!physical_grab_requested || !simulated_gameplay_script_requested || native_grab.lost()==0) && (!physical_grab_requested || !simulated_input_requested || simulated_gameplay_script_requested || (native_grab.picked()>0 && native_grab.moved()>0 && native_grab.dropped()>0 && native_grab.lost()==0)) && gpu_disabled && setup_disabled && eye_boundary_disabled && resolution_disabled && cached_output_restored.load() && headset_size_restored.load() && (!headset_size_requested || (headset_size_applied.load() && native_size_queries.load())) && setup_success && native_camera_success && turning_success && comfort_restored && (!comfort_requested || comfort_applied) && full_scale_restored && (!full_scale_requested || full_scale_written) && temporal_aa_restored && (!temporal_aa_requested || temporal_aa_written) && !callback_failed.load() && frames.load()>0 && valid_poses.load()>0 && stereo_success && gpu_success &&
            (!xr_requested || xr_submitted>0) && (!head_requested || (head_writes>0 && head_writes==head_restores)) &&
            arm_upload_disabled && (!arm_upload_requested || (arm_upload_samples[0] && arm_upload_samples[1])) &&
            (!arm_copy_requested || (arm_copy_calls[0].load() && arm_copy_calls[1].load())) &&
            (!tracked_arms_requested || (arm_pose_calls[0].load() && arm_pose_calls[1].load())) &&
            (!optical_density_requested || optical_density_applied) &&
            pad_disabled && controller_import_restored && (!controller_requested || (xr_controller_samples && controller_bridge.virtual_reads())) ? 0 : 4;
    } catch(const std::exception& error) {
        native_output_size.clear();
        capturing=false;
        native_left_aim.clear();native_weapon_attachment.clear();
        const bool menu_disabled=native_menu.disable();
        const bool turning_disabled=native_turning.disable();
        const bool grab_disabled=native_grab.disable();
        if(taa_history_installed) taa_history.disable();
        if(attachment_pose_installed) attachment_pose.disable();
        const bool interaction_disabled=native_interaction_aim.disable();
        const bool pad_disabled=native_pad_focus.disable();
        restore_camera_comfort();
        const bool resolution_disabled=disable_resolution_hooks();
        const bool arm_upload_disabled=disable_arm_upload_hook();
        const bool left_aim_disabled=native_left_aim.disable() && native_crosshair.disable() && native_fire_seam_trace.disable() && native_shot_aim.disable() &&
            native_hud_census.disable();
        native_crosshair_installed=fire_seam_installed=native_shot_aim_installed=false;
        const bool gameplay_aim_disabled=native_gameplay_aim_trace.disable() && left_aim_disabled;
        const bool native_camera_disabled=native_camera_trace.disable() && gameplay_aim_disabled;
        const bool native_attachment_disabled=native_weapon_attachment.disable();
        const bool eye_boundary_disabled=disable_eye_boundary_hook();
        if(controller_requested) {controller_bridge.publish({},false);controller_import_restored=controller_bridge.disable();}
        const bool setup_disabled=disable_setup_hook();
        auto gpu_disabled=disable_gpu_hooks();
        // Keep Present available to its owner until all XR/graphics references
        // retire. An exception must not make retained resources reusable.
        bool retained_graphics{};
        {std::lock_guard<std::mutex> lock(log_mutex);
            retained_graphics=game_xr || graphics_owner.bound();
            if(retained_graphics) xr_close_requested.store(true,std::memory_order_release);}
        if(retained_graphics && enabled) {
            const auto deadline=GetTickCount64()+5000;
            while(!xr_close_complete.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(10);
            std::lock_guard<std::mutex> lock(log_mutex);
            retained_graphics=game_xr || graphics_owner.bound();
        }
        if(enabled && !retained_graphics) enabled=MH_DisableHook(target)!=MH_OK;
        crash_observer.remove();
        try {
            std::lock_guard<std::mutex> lock(log_mutex);
            restore_stereo();
            // XR lifetime is owned by Present. Leave a failed callback's
            // resources allocated until a render-thread shutdown or game exit.
            if(game_xr) xr_close_requested.store(true,std::memory_order_release);
            csv.close();
            simulated_gameplay_csv.close();
            xr_timing_csv.close();
            setup_csv.close();
            eye_boundary_csv.close();
            if(setup_disabled) head_anchor_csv.close();
            if(arm_upload_disabled) {std::lock_guard<std::mutex> arm_lock(arm_log_mutex);arm_csv.close();arm_palettes.close();avatar_fit_events_csv.close();}
            {std::lock_guard<std::mutex> gpu_lock(gpu_mutex);gpu_csv.close();gpu_hand_draw_csv.close();
                if(!retained_graphics) {paired_left.Reset();paired_backbuffer.Reset();}}
            if(!output.empty()) {std::ofstream log(output/L"dxgi-probe-failure.txt",std::ios::trunc); log << error.what() << '\n';}
        } catch(...) {}
        if(!retained_graphics && !enabled && gpu_disabled && setup_disabled && arm_upload_disabled && native_camera_disabled && native_attachment_disabled && grab_disabled && interaction_disabled && turning_disabled && menu_disabled && pad_disabled && eye_boundary_disabled && resolution_disabled && cached_output_restored.load() && !headset_size_written.load() && controller_import_restored && comfort_restored && mode_restored && values_restored && temporal_aa_restored && full_scale_restored) started=false;
        if(cleanup_exception_triggered && !output.empty()) try {
            std::lock_guard<std::mutex> lock(log_mutex);
            const bool owner_bound=graphics_owner.bound(),xr_retained=static_cast<bool>(game_xr);
            const bool hooks_disabled=!enabled && gpu_disabled && setup_disabled && arm_upload_disabled &&
                native_camera_disabled && native_attachment_disabled && grab_disabled && interaction_disabled && turning_disabled &&
                menu_disabled && pad_disabled && eye_boundary_disabled && resolution_disabled && controller_import_restored;
            const bool restored=cached_output_restored.load() && !headset_size_written.load() && comfort_restored &&
                mode_restored && values_restored && temporal_aa_restored && full_scale_restored;
            const auto probe_status=ProbeStatus(nullptr);
            std::ofstream report(output/L"simulated-cleanup-exception.json",std::ios::trunc);
            report << "{\"controlled_worker_exception\":true,\"simulated_vr\":true,\"expected_diagnostic_exit\":1"
                   << ",\"submitted_layers_before_exception\":" << xr_submitted
                   << ",\"gameplay_frames_before_exception\":" << xr_gameplay_frames
                   << ",\"graphics_owner_bound\":" << (owner_bound?"true":"false")
                   << ",\"xr_retained\":" << (xr_retained?"true":"false")
                   << ",\"xr_close_complete\":" << (xr_close_complete.load()?"true":"false")
                   << ",\"present_hook_disabled\":" << (!enabled?"true":"false")
                   << ",\"all_hooks_disabled\":" << (hooks_disabled?"true":"false")
                   << ",\"native_restoration_verified\":" << (restored?"true":"false")
                   << ",\"probe_status\":" << probe_status
                   << ",\"resources_reusable\":" << (!owner_bound && !xr_retained && hooks_disabled && restored && xr_close_complete.load() && probe_status==0?"true":"false")
                   << ",\"headset_acceptance\":false}\n";
        } catch(...) {}
        return 1;
    } catch(...) {
        native_output_size.clear();
        capturing=false;
        native_left_aim.clear();native_weapon_attachment.clear();
        native_menu.disable();
        native_grab.disable();
        if(taa_history_installed) taa_history.disable();
        if(attachment_pose_installed) attachment_pose.disable();
        native_interaction_aim.disable();
        native_turning.disable();
        native_pad_focus.disable();
        restore_camera_comfort();
        disable_resolution_hooks();
        disable_arm_upload_hook();
        native_left_aim.disable();native_crosshair.disable();native_fire_seam_trace.disable();native_shot_aim.disable();native_hud_census.disable();native_hud_census_installed=false;native_crosshair_installed=fire_seam_installed=native_shot_aim_installed=false;native_gameplay_aim_trace.disable();native_camera_trace.disable();native_weapon_attachment.disable();
        disable_eye_boundary_hook();
        if(controller_requested) {controller_bridge.publish({},false);controller_import_restored=controller_bridge.disable();}
        disable_setup_hook();
        disable_gpu_hooks();
        crash_observer.remove();
        bool retained_graphics=true;
        try {std::lock_guard<std::mutex> lock(log_mutex);restore_stereo();
            retained_graphics=game_xr || graphics_owner.bound();
            if(retained_graphics) xr_close_requested.store(true,std::memory_order_release);
        } catch(...) {}
        if(enabled && !retained_graphics) MH_DisableHook(target);
        try {std::lock_guard<std::mutex> lock(gpu_mutex);gpu_csv.close();gpu_hand_draw_csv.close();
            if(!retained_graphics) {paired_left.Reset();paired_backbuffer.Reset();}
        } catch(...) {}
        return 1;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) module=instance;
    return TRUE;
}
