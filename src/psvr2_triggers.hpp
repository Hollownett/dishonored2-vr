#pragma once
// PSVR2 adaptive triggers (ported from the Indurhra fork): BOTH triggers, per hand. A hand holding the pistol gets the pistol's weapon wall,
// a hand holding the crossbow the crossbow's, and a hand holding a power a light vibration while its trigger is pulled. Either hand may
// hold any of them (dual wield). The mod never calls the Sony/Toolkit API itself: it publishes the two wanted profiles into a
// shared-memory word with a 250 ms lease, and DVR_psvr2\dvr_psvr2_bridge.exe (third_party/psvr2/Bridge.cpp, PSVR2Toolkit, MIT, protocol v2)
// applies them. A stale lease, lost focus or the game exiting resets both triggers. Nothing is created, launched or mapped unless the OpenXR
// runtime is SteamVR with a PlayStation VR2 headset, so Quest/VDXR and other headsets are untouched.
// Always logged to <mod home>\artifacts\psvr2.log (headset/runtime names, helper status, every per-hand change): small, on change only.
// Off: build\no-psvr2-triggers (knob) or `psvr2 off`.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include "../third_party/psvr2/Protocol.hpp"
#include "../third_party/psvr2/Shared.hpp"

namespace d2vr {
namespace psvr2 {
using dvrpsvr2::Profile;
inline bool contains_nocase(std::string_view text,std::string_view needle) noexcept {
    if(needle.empty() || text.size()<needle.size()) return false;
    for(std::size_t i=0;i+needle.size()<=text.size();++i) {
        std::size_t j=0;
        while(j<needle.size() && (text[i+j]|32)==(needle[j]|32)) ++j;
        if(j==needle.size()) return true;
    }
    return false;
}
// SteamVR reports PS VR2 through its driver name ("playstation_vr2") or the product name, depending on the version.
inline bool headset(std::string_view runtime,std::string_view system) noexcept {
    return contains_nocase(runtime,"SteamVR") &&
        (contains_nocase(system,"PlayStation VR2") || contains_nocase(system,"playstation_vr2") || contains_nocase(system,"PSVR2") || contains_nocase(system,"PS VR2"));
}
inline const char* name(Profile p) noexcept {return p==Profile::Gun?"pistol":p==Profile::Crossbow?"crossbow":p==Profile::Power?"power":"off";}
// Powers vibrate only while that hand's trigger is pulled a little (the feel of the charge, not an idle buzz).
inline Profile choose(Profile item,bool play,float trigger) noexcept {
    if(!play) return Profile::Off;
    if(item==Profile::Power && trigger<.12f) return Profile::Off;
    return item;
}

class Runtime {
public:
    std::atomic<bool> enabled{true};
    std::atomic<std::uint32_t> profile_changes{};
    void log_to(const std::filesystem::path& file) {log_file_=file;}
    // Render thread, once per Present. items: what each hand holds (0 left, 1 right); triggers: each hand's trigger value.
    void update(std::string_view runtime_name,std::string_view system_name,bool play,const std::array<Profile,2>& items,const std::array<float,2>& triggers,
                const std::filesystem::path& game_dir) {
        const bool psvr=enabled.load() && headset(runtime_name,system_name);
        if(!identity_logged_ && (!runtime_name.empty() || !system_name.empty())) {
            identity_logged_=true;
            log(std::string("runtime \"")+std::string(runtime_name)+"\" system \""+std::string(system_name)+"\" -> "+(psvr?"PS VR2: triggers on":"not PS VR2: triggers stay off")+
                (enabled.load()?"":" (disabled)"));
        }
        std::array<Profile,2> desired{};
        for(unsigned h=0;h<2;++h) desired[h]=psvr?choose(items[h],play,triggers[h]):Profile::Off;
        if(!psvr && !shared_) return; // no mapping, helper or Toolkit call on other headsets
        if(shared_ && InterlockedCompareExchange(&shared_->stop,0,0)) { // the helper stopped (Toolkit missing, command failure): tidy up, retry later
            if(helper_ && WaitForSingleObject(helper_,0)==WAIT_TIMEOUT) return;
            close();
        }
        if(psvr && !attempted_) connect(game_dir);
        if(!shared_ || !helper_) return;
        if(WaitForSingleObject(helper_,0)!=WAIT_TIMEOUT) {
            if(!exit_logged_) {
                exit_logged_=true;exited_at_=GetTickCount64();DWORD code{};GetExitCodeProcess(helper_,&code);
                log("helper exited with code "+std::to_string(code)+" (2 bad arguments or protocol, 3 Toolkit loader missing, 4 Toolkit init failed, 5 trigger command failed, 6 reset failed); retrying in 10 s");
            }
            if(GetTickCount64()-exited_at_>=10000) close(); // e.g. the PS VR2 driver was not up yet: start a fresh helper next frame
            return;
        }
        dvrpsvr2::publish(shared_,desired[0],desired[1]);
        if(desired!=last_) {
            log(std::string("left ")+name(desired[0])+", right "+name(desired[1])+" (holding: left "+name(items[0])+", right "+name(items[1])+")");
            last_=desired;++profile_changes;
        }
        const long st=InterlockedCompareExchange(&shared_->status,0,0);
        if(st!=status_) {
            status_=st;
            log("helper status "+std::to_string(st)+" (2 ready, 3 loader missing, 4 Toolkit unavailable, 6 init failed, 7 driver inactive, 8 command failed), result "+
                std::to_string(InterlockedCompareExchange(&shared_->result,0,0)));
        }
    }
    void stop() noexcept {if(shared_) {dvrpsvr2::publish(shared_,Profile::Off,Profile::Off);InterlockedExchange(&shared_->stop,1);}}
    std::array<Profile,2> last() const noexcept {return last_;}
    long status() const noexcept {return status_;}
    bool helper_running() const noexcept {return helper_ && WaitForSingleObject(helper_,0)==WAIT_TIMEOUT;}
private:
    dvrpsvr2::Shared* shared_{};HANDLE mapping_{},helper_{};bool attempted_{},identity_logged_{},exit_logged_{};
    std::array<Profile,2> last_{Profile::Off,Profile::Off};long status_{-1};std::filesystem::path log_file_;std::uint64_t exited_at_{};
    void log(const std::string& line) noexcept {
        if(log_file_.empty()) return;
        try {
            std::ofstream out(log_file_,std::ios::app);std::time_t t=std::time(nullptr);char stamp[32]{};std::tm tm{};localtime_s(&tm,&t);
            std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",&tm);out<<stamp<<' '<<line<<'\n';
        } catch(...) {}
    }
    void close() noexcept {
        if(helper_) {CloseHandle(helper_);helper_=nullptr;}
        if(shared_) {UnmapViewOfFile(shared_);shared_=nullptr;}
        if(mapping_) {CloseHandle(mapping_);mapping_=nullptr;}
        attempted_=false;exit_logged_=false;status_=-1;
    }
    void connect(const std::filesystem::path& game_dir) {
        attempted_=true;
        const auto name=L"Local\\DVRPsvr2_"+std::to_wstring(GetCurrentProcessId());
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(dvrpsvr2::Shared),name.c_str());
        if(!mapping_) {log("cannot create the shared memory ("+std::to_string(GetLastError())+")");return;}
        shared_=static_cast<dvrpsvr2::Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(dvrpsvr2::Shared)));
        if(!shared_) {log("cannot map the shared memory ("+std::to_string(GetLastError())+")");return;}
        *shared_={};shared_->magicValue=static_cast<LONG>(dvrpsvr2::magic);shared_->version=dvrpsvr2::protocolVersion;
        dvrpsvr2::publish(shared_,Profile::Off,Profile::Off);
        const auto exe=game_dir/L"DVR_psvr2"/L"dvr_psvr2_bridge.exe";
        std::wstring command=L"\""+exe.wstring()+L"\" "+name+L" "+std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi)) {
            log("cannot start "+exe.string()+" (error "+std::to_string(GetLastError())+")");return;
        }
        helper_=pi.hProcess;CloseHandle(pi.hThread);log("helper started: "+exe.string()+" (protocol v2, both triggers)");
    }
};
inline Runtime& runtime() {static Runtime r;return r;}
} // namespace psvr2
} // namespace d2vr
