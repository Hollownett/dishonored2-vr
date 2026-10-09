// Read-only use of the production ownership helper; no hooks or engine calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "../src/native_equipment_ownership.hpp"
#include <iostream>
int main(int argc,char** argv) {
    HANDLE process=nullptr,module_snapshot=INVALID_HANDLE_VALUE;
    try {
        if(argc!=2) throw std::runtime_error("Usage: native_equipment_probe.exe <PID>; verify pinned executable SHA256 first");
        std::size_t end{};const auto id=std::stoul(argv[1],&end);
        if(!id || id>MAXDWORD || argv[1][end]) throw std::runtime_error("Invalid PID");
        module_snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,static_cast<DWORD>(id));
        MODULEENTRY32W module{};module.dwSize=sizeof(module);
        if(module_snapshot==INVALID_HANDLE_VALUE || !Module32FirstW(module_snapshot,&module) ||
           _wcsicmp(module.szModule,L"Dishonored2.exe") || module.modBaseSize!=0x41cb000) throw std::runtime_error("Pinned image layout required");
        const auto base=reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
        CloseHandle(module_snapshot);module_snapshot=INVALID_HANDLE_VALUE;
        process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(id));
        if(!process) throw std::runtime_error("Read-only process open failed");
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
            SIZE_T got{};return size<=32768 && ReadProcessMemory(process,reinterpret_cast<void*>(at),out,size,&got) && got==size;
        };
        using namespace d2vr;using namespace d2vr::rig;
        const auto arms=player_arms(read,base);if(!arms) throw std::runtime_error("Owned arms unavailable");
        const NativeArmBindingSnapshot snapshot{*arms,{},{}};
        const auto inventory=value<std::uintptr_t>(read,arms->at(1).body+0x60);
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);
        if(!inventory || !game) throw std::runtime_error("Inventory unavailable");
        const auto item=value<std::uintptr_t>(read,*inventory+0x68);
        if(!item || !pointer(*item)) throw std::runtime_error("Right slot unavailable (sheathed/Empty)");
        const auto child_id=value<std::uint32_t>(read,*item+8);
        if(!child_id || (*child_id>>16)>=0x3fff) throw std::runtime_error("Slot child ID invalid");
        const auto child=value<std::uintptr_t>(read,*game+0x3130b0+(*child_id>>16)*8);
        if(!child || !pointer(*child)) throw std::runtime_error("Child unavailable");
        const auto binding=value<std::uintptr_t>(read,*child+0x358);
        if(!binding) throw std::runtime_error("Attachment unavailable");
        const auto candidate=native_attachment_candidate(read,base,snapshot,*binding,3);
        if(!candidate) throw std::runtime_error("Attachment ownership rejected");
        const auto lease=native_sword_equipment_ownership(read,base,snapshot,*candidate);
        if(!lease || !native_equipment_current(read,*lease)) throw std::runtime_error("Typed Sword ownership rejected");
        std::cout<<"{\"pid\":"<<id<<",\"game_memory_writes\":false,\"engine_calls\":false,\"typed_sword_ownership\":true,\"writer_eligibility\":false,\"item\":"<<lease->item
            <<",\"item_declaration\":"<<lease->item_declaration<<",\"child\":"<<lease->attachment.entity<<",\"equipment\":"<<lease->equipment
            <<",\"equipment_declaration\":"<<lease->equipment_declaration<<",\"equipment_generation\":"<<lease->equipment_identity.generation
            <<",\"binding\":"<<lease->attachment.binding<<",\"parent\":"<<lease->attachment.parent<<",\"body\":"<<lease->attachment.arm.body
            <<",\"child_id\":"<<lease->child_id<<",\"parent_id\":"<<lease->parent_id<<",\"guard_count\":"<<lease->guards.size()<<"}\n";
        CloseHandle(process);return 0;
    } catch(const std::exception& error) {
        if(module_snapshot!=INVALID_HANDLE_VALUE) CloseHandle(module_snapshot);
        if(process) CloseHandle(process);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
