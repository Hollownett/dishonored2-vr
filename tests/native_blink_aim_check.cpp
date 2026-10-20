// Pinned bytes, hand-axis build (pitch clamp), pre-call routing gates, no restore after the call, the observer, and the
// install-time refusal of the native Blink aim router. MinHook is stubbed as in tests\native_shot_aim_check.cpp.
#include "../src/native_blink_aim.hpp"
#include <iostream>
#include <vector>

static constexpr std::uintptr_t base=0x140000000,image_size=0x4000000;
static unsigned hook_calls=0;
static bool created=false,bad_call=false,bad_thunk=false,bad_prologue=false;
static void* g_camera=nullptr;static std::array<float,12> seen_at_original{};static unsigned original_runs=0;
static void fake_original() {
    ++original_runs;
    if(g_camera) std::memcpy(seen_at_original.data(),static_cast<char*>(g_camera)+0x2527c,sizeof(seen_at_original));
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* out) {
    ++hook_calls;if(created) return MH_ERROR_ALREADY_CREATED;created=true;*out=reinterpret_cast<void*>(&fake_original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++hook_calls;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++hook_calls;return MH_OK;}

static bool read_mem(std::uintptr_t at,void* out,std::size_t size) {
    if(at>=base && at<base+image_size) {
        if(size==32 && at==base+d2vr::blink_aim_rva && !bad_prologue) {std::memcpy(out,d2vr::blink_aim_prologue.data(),size);return true;}
        if(size==32 && at==base+d2vr::blink_aim_thunk_rva && !bad_thunk) {std::memcpy(out,d2vr::blink_aim_thunk_prologue.data(),size);return true;}
        if(size==5 && at==base+d2vr::blink_aim_call_rva) {
            const auto displacement=static_cast<std::int32_t>(base+d2vr::blink_aim_rva-(at+5))+(bad_call?1:0);
            auto* bytes=static_cast<unsigned char*>(out);bytes[0]=0xe8;std::memcpy(bytes+1,&displacement,4);return true;
        }
        return false;
    }
    std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;
}
static bool no_read(std::uintptr_t,void*,std::size_t) {return false;}
static UINT mask=2;
static UINT logic() {return mask;}
static bool camera_ok=true;
static std::uintptr_t owner_camera=0;static UINT64 applied_tick=0;
static bool camera_check(std::uintptr_t) {return camera_ok;}
static std::uintptr_t owner() {return owner_camera;}
static UINT64 applied() {return applied_tick;}

static int failures=0;
static void check(bool value,const char* what) {if(!value) {++failures;std::cout<<"FAIL "<<what<<'\n';}}

int main() {
    check(d2vr::blink_aim_rva==0x6205e0 && d2vr::blink_aim_thunk_rva==0x954d00 && d2vr::blink_aim_call_rva==0x954d2e,"pinned rvas");
    check(d2vr::blink_aim_prologue[0]==std::byte{0x48} && d2vr::blink_aim_prologue[1]==std::byte{0x8b} && d2vr::blink_aim_prologue[2]==std::byte{0xc4} &&
          d2vr::blink_aim_prologue[28]==std::byte{0x16} && d2vr::blink_aim_prologue[29]==std::byte{0x0b} && d2vr::blink_aim_prologue[31]==std::byte{0},"aim prologue bytes");
    check(d2vr::blink_aim_thunk_prologue[0]==std::byte{0x48} && d2vr::blink_aim_thunk_prologue[28]==std::byte{0x73} &&
          d2vr::blink_aim_thunk_prologue[29]==std::byte{0x1f} && d2vr::blink_aim_thunk_prologue[31]==std::byte{0x90},"thunk prologue bytes");
    if(!failures) std::cout<<"PASS: pinned rvas and both prologues\n";

    // Hand axis.
    d2vr::WorldTrackedPose hand{};
    hand.position={4,5,6};hand.basis[0]={0,1,0};hand.basis[1]={-1,0,0};hand.basis[2]={0,0,1};
    auto axis=d2vr::blink_aim_axis_from_hand(hand);
    check(axis && (*axis)[0]==0 && (*axis)[1]==1 && (*axis)[2]==0 && (*axis)[3]==-1 && (*axis)[8]==1,"level hand passes through, rows forward/left/up");
    d2vr::WorldTrackedPose skewed=hand;skewed.basis[1]={0,1,.3};
    check(!d2vr::blink_aim_axis_from_hand(skewed),"non orthogonal hand rejected");
    // Pitched 60 degrees up with roll kept.
    const double c=std::cos(1.0471975512),sn=std::sin(1.0471975512);
    d2vr::WorldTrackedPose steep=hand;steep.basis[0]={0,c,sn};steep.basis[1]={-1,0,0};steep.basis[2]={0,-sn,c};
    axis=d2vr::blink_aim_axis_from_hand(steep);
    check(axis && std::abs((*axis)[2]-sn)<1e-5,"60 degree pitch unchanged");
    // Straight up (+pitch 90) and 89 down clamp to 85 degrees, heading kept.
    d2vr::WorldTrackedPose up=hand;up.basis[0]={0,0,1};up.basis[1]={-1,0,0};up.basis[2]={0,-1,0};
    check(!d2vr::blink_aim_axis_from_hand(up),"vertical hand without a heading rejected");
    const double c89=std::cos(89*3.14159265358979/180),s89=std::sin(89*3.14159265358979/180);
    d2vr::WorldTrackedPose down=hand;down.basis[0]={c89,0,-s89};down.basis[1]={0,1,0};down.basis[2]={s89,0,c89};
    axis=d2vr::blink_aim_axis_from_hand(down);
    check(axis && std::abs((*axis)[2]+std::sin(85*3.14159265358979/180))<1e-4 && (*axis)[0]>0 && std::abs((*axis)[1])<1e-6,"89 degrees down clamps to -85 keeping heading");
    check(axis && d2vr::valid_aim_frame(*axis),"clamped frame stays orthonormal right-handed");
    if(!failures) std::cout<<"PASS: hand axis rows, rejection and +-85 degree pitch clamp\n";

    // Fake exe-free memory: power block and camera block.
    std::vector<unsigned char> power_block(0x300,0),camera_block(0x25400,0);
    auto* power=power_block.data();auto* camera=camera_block.data();
    const std::uintptr_t cam_value=reinterpret_cast<std::uintptr_t>(camera);
    std::memcpy(power+0x160,&cam_value,8);power[0xa4]=1;power[0xa5]=0;
    const std::array<float,12> native{10,20,30, 1,0,0, 0,1,0, 0,0,1};
    std::memcpy(camera+0x2527c,native.data(),48);
    g_camera=camera;owner_camera=cam_value;applied_tick=GetTickCount64();

    d2vr::NativeBlinkAim router;
    check(router.disable() && !router.routing() && router.calls()==0,"no-op disable before install");
    bool rejected=false;
    try {router.install(0,nullptr,nullptr,nullptr,nullptr,nullptr);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0,"missing readers rejected before hooks");
    rejected=false;
    try {router.install(base,no_read,logic,camera_check,owner,applied);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0 && !router.installed(),"unreadable image refused");
    bad_prologue=true;rejected=false;
    try {router.install(base,read_mem,logic,camera_check,owner,applied);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0 && !router.installed(),"prologue mismatch refuses install");
    bad_prologue=false;bad_thunk=true;rejected=false;
    try {router.install(base,read_mem,logic,camera_check,owner,applied);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0 && !router.installed(),"thunk mismatch refuses install");
    bad_thunk=false;bad_call=true;rejected=false;
    try {router.install(base,read_mem,logic,camera_check,owner,applied);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0 && !router.installed(),"wrong call site refuses install");
    bad_call=false;
    router.install(base,read_mem,logic,camera_check,owner,applied,true);
    check(router.installed() && router.routing() && hook_calls==2,"verified install hooks once");
    if(!failures) std::cout<<"PASS: install-time verification and refusal\n";

    const auto run=[&]() {
        original_runs=0;seen_at_original={};
        d2vr::NativeBlinkAim::detour(power);
    };
    const auto camera_axis=[&]() {std::array<float,9> a{};std::memcpy(a.data(),camera+0x25288,36);return a;};
    const auto camera_origin=[&]() {std::array<float,3> a{};std::memcpy(a.data(),camera+0x2527c,12);return a;};
    const auto reset=[&]() {std::memcpy(camera+0x2527c,native.data(),48);};
    const auto publish=[&](std::uint64_t age=0) {router.publish(hand,5,GetTickCount64()-age,true);};

    publish();run();
    check(original_runs==1 && router.routed()==1 && router.last_reason()==0,"good hand routes");
    check(seen_at_original[3]==0 && seen_at_original[4]==1 && seen_at_original[5]==0 && seen_at_original[6]==-1,"hand frame was in place when the original ran");
    check(seen_at_original[0]==10 && seen_at_original[1]==20 && seen_at_original[2]==30,"native origin kept");
    check(camera_axis()==*d2vr::blink_aim_axis_from_hand(hand) && camera_origin()==std::array<float,3>{10,20,30},"no restore after the call");
    check(router.aim_calls()==1 && router.cam_matches()==1 && router.left_applied_frames()==1,"observer counts the matching camera and the left-aim write");
    auto sample=router.last_sample();
    check(sample.aim_active && sample.cam_matches_left && sample.routed && sample.camera==cam_value,"sample records the camera");
    const std::array<float,6> result{1,2,3,7,8,9};
    // Result floats the original would have produced.
    std::memcpy(power+0x1e8,result.data(),24);publish();reset();run();
    check(router.last_sample().result==result,"destination floats observed after the call");
    check(router.status_json().find("\"last_dest\":[7,8,9]")!=std::string::npos,"status json carries last_dest");

    const auto expect_rejected=[&](int reason,const char* what) {
        const auto before=router.routed();reset();run();
        check(original_runs==1 && router.routed()==before && router.last_reason()==reason && camera_axis()==std::array<float,9>{1,0,0,0,1,0,0,0,1},what);
    };
    publish(300);expect_rejected(5,"stale pose rejected (300 ms)");
    router.clear();expect_rejected(5,"no pose rejected");
    publish();mask=1;expect_rejected(4,"non gameplay logic mask rejected");mask=2;
    publish();camera_ok=false;expect_rejected(7,"authored view or foreign camera rejected");camera_ok=true;
    router.publish(skewed,5,GetTickCount64(),true);expect_rejected(6,"bad hand basis rejected");
    router.publish(hand,5,GetTickCount64(),false);expect_rejected(5,"inactive publication rejected");
    publish();power[0xa4]=0;
    {const auto calls=router.aim_calls();reset();run();check(original_runs==1 && router.aim_calls()==calls && camera_axis()==std::array<float,9>{1,0,0,0,1,0,0,0,1},"aim inactive: original only, nothing written");}
    power[0xa4]=1;power[0xa5]=1;
    {const auto calls=router.aim_calls();reset();run();check(router.aim_calls()==calls && camera_axis()==std::array<float,9>{1,0,0,0,1,0,0,0,1},"cast in progress counts as not aiming");}
    power[0xa5]=0;
    owner_camera=cam_value+8;publish();reset();run();
    check(router.last_reason()==0 && !router.last_sample().cam_matches_left,"observer flags a camera that differs from the left-aim owner");
    owner_camera=cam_value;
    if(!failures) std::cout<<"PASS: routing gates, no restore, observer and status\n";

    // Knob: observer stays, override off.
    check(router.disable() && !router.routing() && !router.installed(),"clean disable");
    router.install(base,read_mem,logic,camera_check,owner,applied,false);
    publish();reset();run();
    check(!router.routing() && router.last_reason()==1 && original_runs==1 && router.aim_calls()==1 &&
          camera_axis()==std::array<float,9>{1,0,0,0,1,0,0,0,1} && router.last_sample().aim_active,"no-hand-blink: observer only");
    check(router.disable() && hook_calls>=5,"second session disables cleanly");
    rejected=false;
    try {router.install(base+0x10000,read_mem,logic,camera_check,owner,applied);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && !router.installed(),"retained hook cannot move to a different image base");
    if(!failures) std::cout<<"PASS: observe-only knob and re-install\n";
    if(failures) {std::cout<<"FAILURES: "<<failures<<'\n';return 1;}
    std::cout<<"PASS: native blink aim\n";
    return 0;
}
