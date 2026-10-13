#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace d2vr {
// Discovery slots contain scalars as well as pointers. Check every region before
// dereferencing; the caller still needs SEH for a concurrent unmap/protect race.
inline bool native_readable_range(std::uintptr_t address,std::size_t size) noexcept {
    constexpr std::uintptr_t highest=0x00007fffffffffffULL;
    if(address<0x10000 || !size || address>highest || size-1>highest-address) return false;
    const auto end=address+size;
    while(address<end) {
        MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)!=sizeof region ||
           region.State!=MEM_COMMIT || (region.Protect&PAGE_GUARD)) return false;
        const auto protection=region.Protect&0xff;
        if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>highest-base || base+region.RegionSize<=address) return false;
        address=base+region.RegionSize;
    }
    return true;
}

// read_self runs thousands of times per body upload; a VirtualQuery syscall per
// read cost ~16 ms/frame in v226 and still 13% of the render thread with 8 slots.
// Reuse validated committed regions briefly per thread; SEH in the caller still
// covers an unmap inside the TTL.
// ponytail: 32-entry round robin, 100 ms TTL; a region re-protected to PAGE_GUARD
// inside the TTL is caught by the caller's guard-page filter, not by this cache.
struct NativeRegionCache {
    struct Entry {std::uintptr_t begin{},end{};std::uint64_t at{};bool exec{};};
    Entry entries[32]{};unsigned next{};
    const Entry* find(std::uintptr_t a,std::size_t n,std::uint64_t now) const noexcept {
        for(const auto& e:entries) if(a>=e.begin && a+n<=e.end && now-e.at<100) return &e;
        return nullptr;
    }
    void add(const MEMORY_BASIC_INFORMATION& r,std::uint64_t now) noexcept {
        const auto b=reinterpret_cast<std::uintptr_t>(r.BaseAddress);
        entries[next++%32]={b,b+r.RegionSize,now,(r.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0};
    }
};
inline NativeRegionCache& native_region_cache() noexcept {thread_local NativeRegionCache cache;return cache;}
inline bool native_readable_range_cached(std::uintptr_t address,std::size_t size,std::uint64_t now_ms) noexcept {
    constexpr std::uintptr_t highest=0x00007fffffffffffULL;
    if(address<0x10000 || !size || address>highest || size-1>highest-address) return false;
    auto& cache=native_region_cache();
    if(cache.find(address,size,now_ms)) return true;
    for(auto at=address,end=address+size;at<end;) {
        MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<const void*>(at),&region,sizeof region)!=sizeof region ||
           region.State!=MEM_COMMIT || (region.Protect&PAGE_GUARD)) return false;
        const auto protection=region.Protect&0xff;
        if(protection!=PAGE_READONLY && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
           protection!=PAGE_EXECUTE_READ && protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>highest-base || base+region.RegionSize<=at) return false;
        cache.add(region,now_ms);at=base+region.RegionSize;
    }
    return true;
}
// Committed executable page (return-address / vtable target checks in hot hooks).
inline bool native_executable_cached(std::uintptr_t at) noexcept {
    if(!at) return false;
    const auto now=GetTickCount64();auto& cache=native_region_cache();
    if(const auto* e=cache.find(at,1,now)) return e->exec;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof info)!=sizeof info || info.State!=MEM_COMMIT) return false;
    cache.add(info,now);
    return (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
}
