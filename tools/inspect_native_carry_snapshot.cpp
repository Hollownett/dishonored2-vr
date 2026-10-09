#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "../src/native_hand_occupancy.hpp"
#include <iostream>
#include <string>
int main(int argc,char** argv){
    if(argc!=2)return 2;const DWORD pid=static_cast<DWORD>(std::stoul(argv[1]));
    const auto modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);if(modules==INVALID_HANDLE_VALUE)return 3;
    MODULEENTRY32W m{};m.dwSize=sizeof(m);const bool valid=Module32FirstW(modules,&m) && _wcsicmp(m.szModule,L"Dishonored2.exe")==0 && m.modBaseSize==0x41cb000;
    CloseHandle(modules);if(!valid)return 4;const auto base=reinterpret_cast<std::uintptr_t>(m.modBaseAddr);
    const auto process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return 5;
    auto read=[&](std::uintptr_t at,void* out,std::size_t bytes){SIZE_T got{};return bytes<=32768 && ReadProcessMemory(process,reinterpret_cast<const void*>(at),out,bytes,&got) && got==bytes;};
    const auto state=d2vr::rig::native_hand_state(read,base);
    if(!state){CloseHandle(process);std::cout<<"{\"guard\":\"unknown\",\"empty_candidates\":0,\"writes\":false,\"engine_calls\":false}\n";return 6;}
    const auto& c=state->carry;
    std::cout<<"{\"pid\":"<<pid<<",\"player\":"<<c.player<<",\"body\":"<<c.body<<",\"machine\":"<<c.machine<<",\"machine_generation\":"<<c.machine_identity.generation
        <<",\"hold_object_active\":"<<(c.active[0]?"true":"false")<<",\"carry_corpse_active\":"<<(c.active[1]?"true":"false")
        <<",\"carrying\":"<<(state->carrying?"true":"false")<<",\"empty_candidates\":"<<state->empty_candidates
        <<",\"left_slot\":"<<state->slots[0]<<",\"right_slot\":"<<state->slots[1]<<",\"callback_left_mask\":"<<d2vr::rig::native_empty_side(read,base,*state,3,state->arms[0])
        <<",\"callback_right_mask\":"<<d2vr::rig::native_empty_side(read,base,*state,3,state->arms[1])<<",\"writes\":false,\"engine_calls\":false}\n";
    CloseHandle(process);return 0;
}
