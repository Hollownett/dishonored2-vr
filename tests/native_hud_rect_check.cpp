#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_rect.hpp"
#include <cassert>
#include <iostream>
using RectCall=void (__fastcall*)(void*,int,int,int,int,int,int);
static RectCall detour;
static std::array<int,6> received;
static void* received_movie;
static bool enabled;
static unsigned creations;
static void __fastcall original(void* movie,int w,int h,int rw,int rh,int rx,int ry) {
    received_movie=movie;received={w,h,rw,rh,rx,ry};
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID hook,LPVOID* trampoline) {
    ++creations;detour=reinterpret_cast<RectCall>(hook);*trampoline=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {if(enabled)return MH_ERROR_ENABLED;enabled=true;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {if(!enabled)return MH_ERROR_DISABLED;enabled=false;return MH_OK;}
int main() {
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1010000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
    constexpr unsigned char prolog[]{0x48,0x85,0xc9,0x0f,0x84,0x8e,0x00,0x00,0x00,0x66};
    std::memcpy(image+0x100cc30,prolog,sizeof prolog);
    using Hud=d2vr::NativeHudRect;const auto base=reinterpret_cast<std::uintptr_t>(image);
    assert(Hud::install(base));assert(Hud::install(base));assert(creations==1);
    Hud::active=true;Hud::scale=.55f;
    auto* movie=image+0x1000;
    detour(movie,1920,1080,1920,1080,0,0);
    assert(received_movie==movie && (received==std::array<int,6>{1920,1080,1920,1080,0,0}));
    detour(movie,2496,2688,2496,1404,0,642);
    assert((received==std::array<int,6>{2496,2688,2496,1404,0,642}));
    assert(!Hud::scaled && !Hud::unshrunk_writes && !Hud::unshrunk_markers && !Hud::markers_corrected());
    assert(Hud::disable());assert(Hud::disable());assert(Hud::install(base));assert(creations==1);
    assert(VirtualFree(image,0,MEM_RELEASE));
    std::cout<<"PASS: native fullscreen damage/marker rect unchanged at 55% preference, no coordinate compensation, retained-hook lifecycle\n";
}
