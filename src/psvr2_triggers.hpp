#pragma once
// PSVR2 adaptive triggers (ported from the Indurhra fork, docs/PSVR2-TRIGGERS.md): the LEFT trigger gets a weapon wall for the pistol and
// the crossbow and a light vibration for powers. The mod never calls the Sony/Toolkit API itself: it publishes the wanted profile into a
// shared-memory word with a 250 ms lease, and DVR_psvr2\dvr_psvr2_bridge.exe (third_party/psvr2/Bridge.cpp, PSVR2Toolkit, MIT) applies
// it. A stale lease, lost focus or the game exiting resets the left trigger. Nothing is created, launched or mapped unless the OpenXR
// runtime is SteamVR with a PlayStation VR2 headset, so Quest/VDXR and other headsets are untouched.
// Off: build\no-psvr2-triggers (knob) or `psvr2 off`.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include "../third_party/psvr2/Protocol.hpp"
#include "../third_party/psvr2/Shared.hpp"

namespace d2vr {
namespace psvr2 {
using dvrpsvr2::Profile;
inline bool headset(std::string_view runtime,std::string_view system) noexcept {
    return runtime.find("SteamVR")!=runtime.npos &&
        (system.find("PlayStation VR2")!=system.npos || system.find("PSVR2")!=system.npos || system.find("PS VR2")!=system.npos || system.find("playstation_vr2")!=system.npos);
}
// The left hand's native equip pose name (dxgi_probe.cpp left_equip_name): "Gun", "...rossbow...", a power, "Empty", the Heart...
inline Profile classify(std::string_view equip) noexcept {
    if(equip.empty() || equip=="Empty" || equip.find("eart")!=equip.npos) return Profile::Off;
    if(equip=="Gun" || equip.find("istol")!=equip.npos) return Profile::Gun;
    if(equip.find("rossbow")!=equip.npos) return Profile::Crossbow;
    for(auto power:{"Bend","Blink","Dark","Devouring","Swarm","Doppel","Grapple","Far","Mesmer","Phantasmal","Domino","Possess","Shadow","Shade","Wind","Power"})
        if(equip.find(power)!=equip.npos) return Profile::Power;
    return Profile::Off;
}
// Powers vibrate only while the trigger is pulled a little (the effect is the feel of the charge, not an idle buzz).
inline Profile choose(Profile item,bool play,float left_trigger) noexcept {
    if(!play) return Profile::Off;
    if(item==Profile::Power && left_trigger<.12f) return Profile::Off;
    return item;
}

class Runtime {
public:
    std::atomic<bool> enabled{true};
    std::atomic<std::uint32_t> profile_changes{};
    // Render thread, once per Present.
    void update(std::string_view runtime_name,std::string_view system_name,bool play,std::string_view left_equip,float left_trigger,const std::filesystem::path& game_dir) {
        const bool psvr=enabled.load() && headset(runtime_name,system_name);
        Profile desired=psvr?choose(classify(left_equip),play,left_trigger):Profile::Off;
        if(!psvr && !shared_) return; // no mapping, helper or Toolkit call on other headsets
        if(shared_ && InterlockedCompareExchange(&shared_->stop,0,0)) { // the helper stopped (Toolkit missing, command failure): tidy up, retry later
            if(helper_ && WaitForSingleObject(helper_,0)==WAIT_TIMEOUT) return;
            close();
        }
        if(psvr && !attempted_) connect(game_dir);
        if(!shared_ || !helper_) return;
        if(WaitForSingleObject(helper_,0)!=WAIT_TIMEOUT) return;
        dvrpsvr2::publish(shared_,desired);
        if(desired!=last_) {last_=desired;++profile_changes;}
        status_=InterlockedCompareExchange(&shared_->status,0,0);
    }
    void stop() noexcept {if(shared_) {dvrpsvr2::publish(shared_,Profile::Off);InterlockedExchange(&shared_->stop,1);}}
    Profile last() const noexcept {return last_;}
    long status() const noexcept {return status_;} // dvrpsvr2::Status: 2 ready, 3 loader missing, 7 driver inactive ...
    bool helper_running() const noexcept {return helper_ && WaitForSingleObject(helper_,0)==WAIT_TIMEOUT;}
private:
    dvrpsvr2::Shared* shared_{};HANDLE mapping_{},helper_{};bool attempted_{};Profile last_{Profile::Off};long status_{};
    void close() noexcept {
        if(helper_) {CloseHandle(helper_);helper_=nullptr;}
        if(shared_) {UnmapViewOfFile(shared_);shared_=nullptr;}
        if(mapping_) {CloseHandle(mapping_);mapping_=nullptr;}
        attempted_=false;
    }
    void connect(const std::filesystem::path& game_dir) {
        attempted_=true;
        const auto name=L"Local\\DVRPsvr2_"+std::to_wstring(GetCurrentProcessId());
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(dvrpsvr2::Shared),name.c_str());
        if(!mapping_) return;
        shared_=static_cast<dvrpsvr2::Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(dvrpsvr2::Shared)));
        if(!shared_) return;
        *shared_={};shared_->magicValue=static_cast<LONG>(dvrpsvr2::magic);shared_->version=1;dvrpsvr2::publish(shared_,Profile::Off);
        const auto exe=game_dir/L"DVR_psvr2"/L"dvr_psvr2_bridge.exe";
        std::wstring command=L"\""+exe.wstring()+L"\" "+name+L" "+std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi)) return;
        helper_=pi.hProcess;CloseHandle(pi.hThread);
    }
};
inline Runtime& runtime() {static Runtime r;return r;}
} // namespace psvr2
} // namespace d2vr
