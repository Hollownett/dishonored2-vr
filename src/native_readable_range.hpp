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
// Reuse validated committed regions per thread; SEH in the caller still
// covers an unmap inside the TTL.
// v817: the 32-entry round robin thrashed again in paired stereo (ZwQueryVirtualMemory from this DLL was 7-12% of the
// render thread). Direct-mapped by 64 KB block like ScanReader (native_grab.hpp): 1024 slots, TTL 0.8-1.2 s staggered.
// A region re-protected to PAGE_GUARD inside the TTL is caught by the caller's guard-page filter, not by this cache.
// v819: v818 still spent ~6% of a core here, read_self probing unreadable addresses (scalars read as pointers) with a
// syscall every time. Unreadable regions are remembered too, briefly (50 ms), so memory committed later is seen soon.
inline bool native_region_readable(const MEMORY_BASIC_INFORMATION& r) noexcept {
    if(r.State!=MEM_COMMIT || (r.Protect&PAGE_GUARD)) return false;
    const auto p=r.Protect&0xff;
    return p==PAGE_READONLY || p==PAGE_READWRITE || p==PAGE_WRITECOPY || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
struct NativeRegionCache {
    struct Entry {std::uintptr_t begin{},end{};std::uint64_t until{};bool exec{},readable{};};
    Entry entries[1024]{};
    static std::size_t slot(std::uintptr_t a) noexcept {return static_cast<std::size_t>(a>>16)&1023u;}
    // The entry holding `a`: unreadable whatever n is, readable when the n bytes stay inside its region.
    const Entry* find(std::uintptr_t a,std::size_t n,std::uint64_t now) const noexcept {
        const auto& e=entries[slot(a)];
        return a>=e.begin && a<e.end && now<e.until && (!e.readable || n<=e.end-a)?&e:nullptr;
    }
    // `a` is the queried address: the region is filed under that address's block.
    void add(const MEMORY_BASIC_INFORMATION& r,std::uint64_t now,std::uintptr_t a) noexcept {
        const auto b=reinterpret_cast<std::uintptr_t>(r.BaseAddress);const bool readable=native_region_readable(r);
        entries[slot(a)]={b,b+r.RegionSize,now+(readable?800+((a>>16)*37)%400:50),
            r.State==MEM_COMMIT && (r.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0,readable};
    }
};
inline NativeRegionCache& native_region_cache() noexcept {thread_local NativeRegionCache cache;return cache;}
inline bool native_readable_range_cached(std::uintptr_t address,std::size_t size,std::uint64_t now_ms) noexcept {
    constexpr std::uintptr_t highest=0x00007fffffffffffULL;
    if(address<0x10000 || !size || address>highest || size-1>highest-address) return false;
    auto& cache=native_region_cache();
    if(const auto* e=cache.find(address,size,now_ms)) return e->readable;
    for(auto at=address,end=address+size;at<end;) {
        MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<const void*>(at),&region,sizeof region)!=sizeof region) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>highest-base || base+region.RegionSize<=at) return false;
        cache.add(region,now_ms,at);
        if(!native_region_readable(region)) return false;
        at=base+region.RegionSize;
    }
    return true;
}
// Committed executable page (return-address / vtable target checks in hot hooks).
inline bool native_executable_cached(std::uintptr_t at) noexcept {
    if(!at) return false;
    const auto now=GetTickCount64();auto& cache=native_region_cache();
    if(const auto* e=cache.find(at,1,now)) return e->exec;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof info)!=sizeof info) return false;
    cache.add(info,now,at);
    return info.State==MEM_COMMIT && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
}
