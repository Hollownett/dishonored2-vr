#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include "../src/arm_tracking.hpp"
#include "../src/avatar_fit_reuse.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const auto pid=static_cast<DWORD>(std::strtoul(argv[1],nullptr,10));
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
    if(snapshot==INVALID_HANDLE_VALUE) return 3;
    MODULEENTRY32W module{};module.dwSize=sizeof(module);
    const bool found=Module32FirstW(snapshot,&module)!=FALSE;CloseHandle(snapshot);
    if(!found || _wcsicmp(module.szModule,L"Dishonored2.exe") || module.modBaseSize!=0x041cb000) return 4;
    const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process) return 5;
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        SIZE_T got{};
        return at>=0x10000 && size>0 && size<=32768 && at<0x0000800000000000-size &&
            ReadProcessMemory(process,reinterpret_cast<void*>(at),out,size,&got) && got==size;
    };
    const auto base=reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
    const auto arms=d2vr::rig::player_arms(read,base);
    std::array<std::optional<d2vr::rig::ArmBinding>,2> bindings;
    if(arms) for(unsigned side=0;side<2;++side) bindings[side]=d2vr::rig::arm_binding(read,base,(*arms)[side]);
    d2vr::rig::NativeArmBindingCache cache;cache.refresh(read,base);const auto owned=cache.load();
    const auto key=owned?d2vr::rig::avatar_fit_template(read,base,*owned,1):std::nullopt;
    const auto after=d2vr::rig::player_arms(read,base);CloseHandle(process);
    if(!arms || !after || *arms!=*after || !bindings[0] || !bindings[1] || !key) return 6;
    std::cout<<std::setprecision(10)<<"{\"pid\":"<<pid<<",\"game_memory_writes\":false,\"engine_calls\":false,\"exact_binding_guards_passed\":true,\"arms\":[";
    for(unsigned side=0;side<2;++side) {
        const auto& arm=(*arms)[side];const auto& binding=*bindings[side];
        if(side) std::cout<<',';
        std::cout<<"{\"side\":"<<side<<",\"animation\":"<<arm.animation<<",\"model\":"<<arm.model<<",\"skeleton\":"<<arm.skeleton
            <<",\"chain\":["<<binding.chain.upper<<','<<binding.chain.elbow<<','<<binding.chain.wrist<<"],\"bind_wrist\":[";
        for(unsigned i=0;i<16;++i) {if(i)std::cout<<',';std::cout<<binding.bind_wrist.f[i];}
        std::cout<<"],\"bind_grip\":[";
        for(unsigned i=0;i<16;++i) {if(i)std::cout<<',';std::cout<<binding.bind_grip.f[i];}
        std::cout<<"]}";
    }
    std::cout<<"],\"template_key_complete\":"<<(d2vr::rig::complete_avatar_fit_template(*key)?"true":"false")
        <<",\"units_per_metre_assumption\":1,\"physical_metric_scale_verified\":false,\"native_bind_palm_span\":"<<key->measurements.palm_span_metres
        <<",\"native_bind_shoulder_span\":"<<key->measurements.shoulder_span_metres<<",\"mesh_tokens\":[";
    for(unsigned side=0;side<2;++side) {
        if(side)std::cout<<',';const auto& token=key->arms[side].mesh;
        std::cout<<"{\"source\":"<<token.source<<",\"primary\":"<<token.primary<<",\"surfaces\":[";
        for(unsigned i=0;i<token.surfaces.size();++i) {if(i)std::cout<<',';const auto& s=token.surfaces[i];
            std::cout<<"["<<s.resource<<','<<s.kind<<','<<s.vertices<<','<<s.triangles<<']';}
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
