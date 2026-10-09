// Read-only check of the same ownership/name/bind decoder used by the mod.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "../src/arm_tracking.hpp"
#include <iostream>
#include <iomanip>

int main(int argc,char** argv) {
    HANDLE process=INVALID_HANDLE_VALUE,snapshot=INVALID_HANDLE_VALUE;
    try {
        if(argc!=2) throw std::runtime_error("Usage: arm_binding_probe.exe <Dishonored2 PID>");
        std::size_t end{};const auto id=std::stoul(argv[1],&end);
        if(!id || id>MAXDWORD || argv[1][end]) throw std::runtime_error("Invalid process ID");
        process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(id));
        if(!process) throw std::runtime_error("Cannot open game for read-only inspection");
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,static_cast<DWORD>(id));
        if(snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read game module list");
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(!Module32FirstW(snapshot,&entry) || _wcsicmp(entry.szModule,L"Dishonored2.exe") || entry.modBaseSize!=0x041cb000)
            throw std::runtime_error("Require the pinned Dishonored 2 image layout");
        const auto base=reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
            SIZE_T got{};return size<=16384 && ReadProcessMemory(process,reinterpret_cast<void*>(at),out,size,&got) && got==size;
        };
        const auto arms=d2vr::rig::player_arms(read,base);
        if(!arms) throw std::runtime_error("Owned player arms not available");
        std::array<d2vr::rig::ArmBinding,2> bindings{};
        for(unsigned side=0;side<2;++side) {
            const auto binding=d2vr::rig::arm_binding(read,base,(*arms)[side]);
            if(!binding) throw std::runtime_error("Owned named palm/wrist binding rejected");
            bindings[side]=*binding;
        }
        std::cout<<std::setprecision(9)<<"{\"pid\":"<<id<<",\"game_memory_writes\":false,\"physical_alignment_verified\":false,\"arms\":[";
        for(unsigned side=0;side<2;++side) {
            const auto& b=bindings[side];const auto w=d2vr::rig::position(b.bind_wrist),g=d2vr::rig::position(b.bind_grip);
            if(side) std::cout<<',';
            std::cout<<"{\"side\":\""<<(side?"right":"left")<<"\",\"chain\":["<<b.chain.upper<<','<<b.chain.elbow<<','<<b.chain.wrist
                <<"],\"bind_wrist\":["<<w.x<<','<<w.y<<','<<w.z<<"],\"bind_grip\":["<<g.x<<','<<g.y<<','<<g.z
                <<"],\"wrist_to_palm\":"<<d2vr::length(g-w)<<'}';
        }
        std::cout<<"]}\n";CloseHandle(snapshot);CloseHandle(process);return 0;
    } catch(const std::exception& error) {
        if(snapshot!=INVALID_HANDLE_VALUE) CloseHandle(snapshot);
        if(process && process!=INVALID_HANDLE_VALUE) CloseHandle(process);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
