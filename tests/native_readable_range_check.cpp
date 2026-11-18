#include "../src/native_readable_range.hpp"
#include <cassert>
#include <limits>

int main() {
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto page=info.dwPageSize;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(memory);const auto at=reinterpret_cast<std::uintptr_t>(memory);
    assert(d2vr::native_readable_range(at,28));
    assert(d2vr::native_readable_range(at+page-14,28));
    assert(!d2vr::native_readable_range(1,28));
    assert(!d2vr::native_readable_range((std::numeric_limits<std::uintptr_t>::max)()-8,28));
    DWORD old{};assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old));
    assert(!d2vr::native_readable_range(at+page-14,28));
    assert(VirtualProtect(memory+page,page,PAGE_READWRITE|PAGE_GUARD,&old));
    assert(!d2vr::native_readable_range(at+page-14,28));
    MEMORY_BASIC_INFORMATION region{};assert(VirtualQuery(memory+page,&region,sizeof region));
    assert(region.Protect&PAGE_GUARD); // Validation must not consume the guard.
    assert(d2vr::native_readable_range_cached(at,28,1000));
    assert(VirtualProtect(memory,page,PAGE_NOACCESS,&old));
    assert(d2vr::native_readable_range_cached(at,28,1700));  // cached inside the 2.5-3.5 s TTL (v1394; was 0.8-1.2 s)
    assert(d2vr::native_readable_range_cached(at,28,3400));
    assert(!d2vr::native_readable_range_cached(at,28,4600)); // expired -> requeried
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&old));
    assert(!d2vr::native_readable_range_cached(at,28,4620)); // an unreadable region is remembered 200 ms (v1394; was 50 ms)
    assert(!d2vr::native_readable_range_cached(at,28,4790));
    assert(d2vr::native_readable_range_cached(at,28,4810));  // then requeried
    assert(!d2vr::native_readable_range_cached(at+page-14,28,4850)); // the range reaches the guard page
    // v1041: a read that faulted forgets the cached "readable" verdict, so the next call requeries instead of faulting for ~1 s.
    assert(d2vr::native_readable_range_cached(at,28,6000));
    assert(VirtualProtect(memory,page,PAGE_NOACCESS,&old));
    assert(d2vr::native_readable_range_cached(at,28,6100)); // stale-readable (the fault case)
    d2vr::native_readable_range_forget(at,28);
    assert(!d2vr::native_readable_range_cached(at,28,6200));
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&old));
    // v1394: a big committed region (an engine heap) is queried once; reads in its other 64 KB blocks hit the recent-region list.
    {
        constexpr std::size_t big=8u<<20;
        auto* heap=static_cast<unsigned char*>(VirtualAlloc(nullptr,big,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        assert(heap);const auto h=reinterpret_cast<std::uintptr_t>(heap);
        auto& cache=d2vr::native_region_cache();
        const auto q0=cache.queries;
        assert(d2vr::native_readable_range_cached(h,64,10000));
        assert(cache.queries==q0+1);
        const auto hits0=cache.recent_hits;
        for(std::size_t off=(1u<<16);off<big;off+=(1u<<16)+4096) assert(d2vr::native_readable_range_cached(h+off,64,10050));
        assert(cache.queries==q0+1);          // no syscall for the other 127 blocks
        assert(cache.recent_hits>hits0+100);
        // a faulted read inside the region drops the region from the list too: the next read requeries
        d2vr::native_readable_range_forget(h+(3u<<20),64);
        assert(d2vr::native_readable_range_cached(h+(3u<<20)+128,64,10100)); // the forgotten block (slot and list entry gone)
        assert(cache.queries==q0+2);
        // a read that runs past the region's end is not satisfied by the list
        assert(VirtualFree(heap,0,MEM_RELEASE));
    }
    assert(d2vr::native_executable_cached(reinterpret_cast<std::uintptr_t>(&main)));
    assert(!d2vr::native_executable_cached(at));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    assert(!d2vr::native_readable_range(at,28));
}
