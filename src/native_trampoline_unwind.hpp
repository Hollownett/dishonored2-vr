#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

namespace d2vr {
// Wine bounds a dynamic function table by [BaseAddress, BaseAddress+last EndAddress) and stops at the first table covering a pc, so each trampoline table
// uses the trampoline as BaseAddress and keeps its unwind block ABOVE it (RVAs are unsigned), 64 KB-stepped, within UINT32_MAX of the trampoline.
inline void* alloc_unwind_above(std::uintptr_t trampoline) noexcept {
    const auto aligned=trampoline&~std::uintptr_t{0xffff};
    for(unsigned slot=1;slot<=4096;++slot) {
        const auto address=aligned+static_cast<std::uintptr_t>(slot)*0x10000;
        if(address<aligned || address-trampoline>UINT32_MAX-4096)return nullptr;
        if(auto* block=VirtualAlloc(reinterpret_cast<void*>(address),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return block;
    }
    return nullptr;
}
}
