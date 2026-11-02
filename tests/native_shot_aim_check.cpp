// Pure hand-frame build and 12+36 byte save/swap/restore checks for the native
// shot aim router, plus a compile-only hook surface (MinHook is stubbed exactly
// as tests\native_fire_seam_trace_check.cpp does).
#include "../src/native_shot_aim.hpp"
#include <iostream>

static constexpr std::uintptr_t base=0x140000000;
static unsigned hook_calls=0;
static bool created=false;
static bool bad_call{};
static void fake_original() {}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* out) {
    ++hook_calls;if(created) return MH_ERROR_ALREADY_CREATED;
    created=true;*out=reinterpret_cast<void*>(&fake_original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++hook_calls;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++hook_calls;return MH_OK;}
static bool no_read(std::uintptr_t,void*,std::size_t) {return false;}
static bool install_read(std::uintptr_t at,void* out,std::size_t size) {
    if(size==32 && at==base+d2vr::shot_aim_rva) {std::memcpy(out,d2vr::shot_aim_prologue.data(),size);return true;}
    if(size==5) for(const auto& call:d2vr::shot_aim_calls) if(at==base+call[0]) {
        const auto displacement=static_cast<std::int32_t>(base+call[1]-(at+5))+(bad_call?1:0);
        auto* bytes=static_cast<unsigned char*>(out);bytes[0]=0xe8;std::memcpy(bytes+1,&displacement,4);return true;
    }
    return false;
}
static UINT logic() {return 2;}

static int failures=0;
static void check(bool value,const char* what) {if(!value) {++failures;std::cout<<"FAIL "<<what<<'\n';}}

int main() {
    check(d2vr::shot_aim_rva==0x639970,"pinned launch rva");
    check(d2vr::shot_aim_prologue[0]==std::byte{0x48} && d2vr::shot_aim_prologue[1]==std::byte{0x89} &&
          d2vr::shot_aim_prologue[4]==std::byte{0x10} && d2vr::shot_aim_prologue[5]==std::byte{0x48} &&
          d2vr::shot_aim_prologue[31]==std::byte{0x00},"pinned prologue bytes");
    check(d2vr::shot_aim_calls[0][0]==0x633419 && d2vr::shot_aim_calls[0][1]==0x639970 &&
          d2vr::shot_aim_calls[1][0]==0x62b8cc && d2vr::shot_aim_calls[1][1]==0x639970,"pinned call sites");
    if(!failures) std::cout<<"PASS: pinned launch rva, prologue bytes and both verified call sites\n";

    d2vr::WorldTrackedPose hand{};
    hand.position={4,5,6};hand.basis[0]={1,0,0};hand.basis[1]={0,1,0};hand.basis[2]={0,0,1};
    const auto frame=d2vr::shot_aim_from_hand(hand);
    check(frame.has_value(),"orthonormal hand frame accepted");
    check(frame && frame->origin==std::array<float,3>{4,5,6},"hand origin copied");
    check(frame && frame->axis[0]==1 && frame->axis[4]==1 && frame->axis[8]==1,"rows in forward/left/up order");
    d2vr::WorldTrackedPose sheared=hand;sheared.basis[1]={1,0,0};
    check(!d2vr::shot_aim_from_hand(sheared),"parallel rows rejected");
    d2vr::WorldTrackedPose scaled=hand;scaled.basis[2]={0,0,2};
    check(!d2vr::shot_aim_from_hand(scaled),"non unit rows rejected");
    d2vr::WorldTrackedPose flipped=hand;flipped.basis[2]={0,0,-1};
    check(!d2vr::shot_aim_from_hand(flipped),"left handed frame rejected");
    d2vr::WorldTrackedPose skewed=hand;skewed.basis[1]={0,1,.3};
    check(!d2vr::shot_aim_from_hand(skewed),"non orthogonal rows rejected");
    if(!failures) std::cout<<"PASS: hand frame rows, order, and orthonormality rejection\n";

    // Canaries prove the helper touches exactly 12 origin and 36 frame bytes.
    std::array<float,5> origin{10,20,30,111,222};
    std::array<float,11> axis{1,0,0,0,1,0,0,0,1,333,444};
    const std::array<float,5> native_origin=origin;const std::array<float,11> native_axis=axis;
    d2vr::GameplayAim planned{};
    planned.origin={1,2,3};
    planned.axis={0,0,1,-1,0,0,0,1,0};
    d2vr::ShotAimSave saved{};
    d2vr::shot_aim_save(origin.data(),axis.data(),saved);
    check(std::memcmp(saved.origin.data(),native_origin.data(),12)==0 &&
          std::memcmp(saved.axis.data(),native_axis.data(),36)==0,"save copies 12+36 bytes");
    d2vr::shot_aim_apply(origin.data(),axis.data(),planned);
    check(std::memcmp(origin.data(),planned.origin.data(),12)==0 &&
          std::memcmp(axis.data(),planned.axis.data(),36)==0,"swap writes the hand frame");
    d2vr::shot_aim_restore(origin.data(),axis.data(),saved);
    check(std::memcmp(origin.data(),native_origin.data(),12)==0 &&
          std::memcmp(axis.data(),native_axis.data(),36)==0,"restore returns the native frame");
    check(origin[3]==native_origin[3] && origin[4]==native_origin[4] && axis[9]==native_axis[9] &&
          axis[10]==native_axis[10],"canaries outside the 12+36 bytes untouched");
    if(!failures) std::cout<<"PASS: 12 byte origin and 36 byte frame save/swap/restore\n";

    (void)sizeof(d2vr::NativeShotAim);
    d2vr::NativeShotAim router;
    router.publish(hand,7,GetTickCount64(),true);router.clear();
    check(router.disable() && !router.routing() && router.hooks()==0 && router.calls()==0 && router.routed()==0 &&
          router.rejected()==0 && router.overlap()==0 && router.rows()==0,"no-op disable before install");
    bool rejected=false;
    try {router.install(0,nullptr,nullptr,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && router.disable() && hook_calls==0,"missing readers rejected before hooks");
    rejected=false;
    try {router.install(base,no_read,logic,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && router.disable() && hook_calls==0 && router.hooks()==0,"mismatched prologue rejected before hooks");
    bad_call=true;rejected=false;
    try {router.install(base,install_read,logic,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==0 && router.hooks()==0,"wrong launch call site rejected before hooks");
    bad_call=false;
    router.install(base,install_read,logic,L"build/native-shot-aim-test.csv",true);
    check(router.routing() && router.hooks()==1 && hook_calls==2,"routed install after the verified prologue");
    check(router.disable() && router.hooks()==0 && !router.routing(),"clean disable restores the prologue");
    router.install(base,install_read,logic,L"build/native-shot-aim-test.csv",true);
    check(router.routing() && router.hooks()==1 && hook_calls==4,"second install reuses owned disabled trampoline");
    check(router.disable() && hook_calls==5,"second session disables cleanly");
    {   // v776 (headset v775: six unrouted shots, no reason in the log): the last column says why a row was not routed
        std::ifstream in("build/native-shot-aim-test.csv");std::string header;std::getline(in,header);
        check(header.size()>7 && header.compare(header.size()-7,7,",reason")==0,"the log's last column is the reject reason");
    }
    rejected=false;
    try {router.install(base+0x10000,install_read,logic,L"unused.csv",true);} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && hook_calls==5 && !router.routing(),"retained hook cannot move to a different image base");
    if(!failures) std::cout<<"PASS: hook surface, prepatch rejection, two-session reuse and changed-image rejection\n";
    if(failures) {std::cout<<"FAILURES: "<<failures<<'\n';return 1;}
    std::cout<<"PASS: native shot aim\n";
    return 0;
}
