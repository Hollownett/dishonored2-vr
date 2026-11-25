#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <atomic>

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
// v1394 (2026-11-19 sim profile, RTX 5070 + Ryzen 5 5600X, diagnostics off): the cache still missed often enough that
// native_readable_range_cached's VirtualQuery was 11.4 % of the render thread's samples (plus read_self_raw 1.9 %), the largest
// mod cost on the CPU critical path. The engine heaps are committed regions of tens to hundreds of MB, but the direct-mapped table
// files a region under ONE 64 KB block, so every other block of the same region missed once per TTL. Now:
//   - a miss in the direct slot first scans the last 32 distinct readable regions (round robin) and refills the slot on a hit;
//   - 2048 direct slots (was 1024);
//   - readable regions are trusted 2.5-3.5 s (was 0.8-1.2 s) and unreadable ones 200 ms (was 50 ms). SEH in read_self_raw still
//     covers an unmap inside the TTL and native_readable_range_forget drops a verdict that faulted, from the slots and the list.
inline constexpr std::uint64_t native_region_readable_ttl_ms=2500,native_region_readable_stagger_ms=1000,native_region_unreadable_ttl_ms=200;
// v1457+ours: Hollow's v1394 recent-region list and TTLs, with a 4-way set-associative table (1024 sets of four, the same memory as
// 2048 direct slots). Indurhra's v1300 profile (sim, AFW, idle) found the direct table's misses were 91% "the slot holds ANOTHER
// still-valid region" (two live regions under one 64 KB-block index); the recent list answers misses inside big regions, the ways
// answer the aliasing. Keys, TTLs, readability rules and forget-on-fault are Hollow's.
struct NativeRegionCache {
    struct Entry {std::uintptr_t begin{},end{};std::uint64_t until{};bool exec{},readable{};};
    static constexpr std::size_t sets=1024,ways=4,recent_count=32;
    Entry entries[sets*ways]{};
    unsigned char next_victim[sets]{};
    Entry recent[recent_count]{};unsigned recent_next{};
    std::uint64_t queries{},recent_hits{}; // status/lab counters (per thread)
    static std::size_t slot(std::uintptr_t a) noexcept {return static_cast<std::size_t>(a>>16)&(sets-1);}
    static bool holds(const Entry& e,std::uintptr_t a,std::size_t n,std::uint64_t now) noexcept {
        return a>=e.begin && a<e.end && now<e.until && (!e.readable || n<=e.end-a);
    }
    // The way of a's set to (re)use for region base b: the same region, else an expired or empty way, else the round-robin victim.
    Entry& way_for(std::uintptr_t a,std::uintptr_t b,std::uint64_t now) noexcept {
        const auto s=slot(a);Entry* set=entries+s*ways;
        for(std::size_t i=0;i<ways;++i) if(set[i].until && set[i].begin==b) return set[i];
        for(std::size_t i=0;i<ways;++i) if(now>=set[i].until) return set[i];
        const auto w=next_victim[s]%ways;next_victim[s]=static_cast<unsigned char>(w+1);return set[w];
    }
    // The entry holding `a`: unreadable whatever n is, readable when the n bytes stay inside its region.
    const Entry* find(std::uintptr_t a,std::size_t n,std::uint64_t now) noexcept {
        Entry* set=entries+slot(a)*ways;
        for(std::size_t w=0;w<ways;++w) if(holds(set[w],a,n,now)) return set+w;
        for(const auto& r:recent) if(r.readable && holds(r,a,n,now)) {Entry& e=way_for(a,r.begin,now);e=r;++recent_hits;return &e;}
        return nullptr;
    }
    // `a` is the queried address: the region is filed under that address's set, and a readable big region in the recent list.
    void add(const MEMORY_BASIC_INFORMATION& r,std::uint64_t now,std::uintptr_t a) noexcept {
        const auto b=reinterpret_cast<std::uintptr_t>(r.BaseAddress);const bool readable=native_region_readable(r);
        const Entry e{b,b+r.RegionSize,now+(readable?native_region_readable_ttl_ms+((a>>16)*37)%native_region_readable_stagger_ms:native_region_unreadable_ttl_ms),
            r.State==MEM_COMMIT && (r.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0,readable};
        way_for(a,b,now)=e;
        if(readable && r.RegionSize>(std::size_t{1}<<16)) {
            for(auto& old:recent) if(old.begin==e.begin) {old=e;return;}
            recent[recent_next++%recent_count]=e;
        }
    }
    // Drops every way of a's set, and every recent region, that holds a (a guarded read faulted there).
    void forget(std::uintptr_t a) noexcept {
        Entry* set=entries+slot(a)*ways;
        for(std::size_t w=0;w<ways;++w) if(a>=set[w].begin && a<set[w].end) set[w]={};
        for(auto& r:recent) if(a>=r.begin && a<r.end) r={};
    }
};
inline NativeRegionCache& native_region_cache() noexcept {thread_local NativeRegionCache cache;return cache;}
// Diagnostic (knob build/region-cache-stats): why lookups miss. Off: one relaxed load per lookup.
struct RegionCacheStats {
    std::atomic<bool> on{};
    std::atomic<std::uint64_t> lookups{},hit_readable{},hit_unreadable{},miss_empty{},miss_expired{},miss_other_region{},miss_past_end{},queries{},query_unreadable{};
};
inline RegionCacheStats& region_cache_stats() noexcept {static RegionCacheStats s;return s;}
inline bool native_readable_range_cached(std::uintptr_t address,std::size_t size,std::uint64_t now_ms) noexcept {
    constexpr std::uintptr_t highest=0x00007fffffffffffULL;
    if(address<0x10000 || !size || address>highest || size-1>highest-address) return false;
    auto& cache=native_region_cache();
    if(const auto* e=cache.find(address,size,now_ms)) {
        if(auto& st=region_cache_stats();st.on.load(std::memory_order_relaxed)) {st.lookups.fetch_add(1,std::memory_order_relaxed);(e->readable?st.hit_readable:st.hit_unreadable).fetch_add(1,std::memory_order_relaxed);}
        return e->readable;
    }
    if(auto& st=region_cache_stats();st.on.load(std::memory_order_relaxed)) {
        st.lookups.fetch_add(1,std::memory_order_relaxed);
        const auto* set=cache.entries+NativeRegionCache::slot(address)*NativeRegionCache::ways;bool live=false,expired=false,past=false;
        for(std::size_t w=0;w<NativeRegionCache::ways;++w) {
            if(!set[w].until) continue;
            if(now_ms>=set[w].until) expired=true;else if(address>=set[w].begin && address<set[w].end) past=true;else live=true;
        }
        (past?st.miss_past_end:live?st.miss_other_region:expired?st.miss_expired:st.miss_empty).fetch_add(1,std::memory_order_relaxed);
    }
    for(auto at=address,end=address+size;at<end;) {
        MEMORY_BASIC_INFORMATION region{};++cache.queries;
        if(auto& st=region_cache_stats();st.on.load(std::memory_order_relaxed)) st.queries.fetch_add(1,std::memory_order_relaxed);
        if(VirtualQuery(reinterpret_cast<const void*>(at),&region,sizeof region)!=sizeof region) return false;
        const auto base=reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if(region.RegionSize>highest-base || base+region.RegionSize<=at) return false;
        cache.add(region,now_ms,at);
        if(!native_region_readable(region)) {
            if(auto& st=region_cache_stats();st.on.load(std::memory_order_relaxed)) st.query_unreadable.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        at=base+region.RegionSize;
    }
    return true;
}
// v1041: a guarded read that faulted after the cache said "readable" (memory freed or decommitted inside the cached window) must not be
// retried every frame for the next ~1 s: each retry is a handled access violation (60 per session in the v1040 record, all one address).
inline void native_readable_range_forget(std::uintptr_t address,std::size_t size) noexcept {
    auto& cache=native_region_cache();
    cache.forget(address);
    if(size) cache.forget(address+size-1);
}
// Committed executable page (return-address / vtable target checks in hot hooks).
inline bool native_executable_cached(std::uintptr_t at) noexcept {
    if(!at) return false;
    const auto now=GetTickCount64();auto& cache=native_region_cache();
    if(const auto* e=cache.find(at,1,now)) return e->exec;
    MEMORY_BASIC_INFORMATION info{};++cache.queries;
    if(VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof info)!=sizeof info) return false;
    cache.add(info,now,at);
    return info.State==MEM_COMMIT && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
}
