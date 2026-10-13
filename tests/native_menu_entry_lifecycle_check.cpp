#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_menu_entry.hpp"
#include <cassert>
#include <iostream>
#include <set>
static std::set<void*> created,enabled;
static bool reject_enable=false;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* original) {
    assert(created.insert(target).second);*original=target;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    if(reject_enable) return MH_ERROR_MEMORY_PROTECT;
    return enabled.insert(target).second?MH_OK:MH_ERROR_ENABLED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID target) {created.erase(target);enabled.erase(target);return MH_OK;}
int main() {
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(image);
    constexpr unsigned char load[]{0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0x70,0x01,0x00,0x00,0x48};
    constexpr unsigned char hub[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89};
    std::memcpy(image+0x2e4d0,load,sizeof load);std::memcpy(image+0x34800,hub,sizeof hub);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    using Entry=d2vr::NativeMenuEntry;
    Entry::requested=true;assert(Entry::install(base));assert(!Entry::requested && Entry::enabled);
    assert(created.size()==2 && enabled.size()==2);
    Entry::disable();assert(enabled.size()==2); // Patched movies outlive a VR session.
    Entry::requested=true;assert(Entry::install(base));assert(!Entry::requested && Entry::enabled);
    assert(created.size()==2 && enabled.size()==2); // Reuse rather than recreate the hooks.
    reject_enable=true;Entry::requested=true;
    assert(!Entry::install(base) && !Entry::enabled && !Entry::requested);
    reject_enable=false;assert(Entry::install(base) && Entry::enabled);
    assert(VirtualFree(image,0,MEM_RELEASE));
    std::cout<<"PASS: retained native menu hooks reinstall, stale click reset and actual enable failure rejection\n";
}
