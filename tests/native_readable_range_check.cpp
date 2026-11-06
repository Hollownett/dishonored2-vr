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
    assert(d2vr::native_readable_range_cached(at,28,1700));  // cached inside the 0.8-1.2 s TTL
    assert(!d2vr::native_readable_range_cached(at,28,2300)); // expired -> requeried
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&old));
    assert(!d2vr::native_readable_range_cached(at,28,2320)); // an unreadable region is remembered 50 ms
    assert(d2vr::native_readable_range_cached(at,28,2360));  // then requeried
    assert(!d2vr::native_readable_range_cached(at+page-14,28,2400)); // the range reaches the guard page
    // v1041: a read that faulted forgets the cached "readable" verdict, so the next call requeries instead of faulting for ~1 s.
    assert(d2vr::native_readable_range_cached(at,28,3000));
    assert(VirtualProtect(memory,page,PAGE_NOACCESS,&old));
    assert(d2vr::native_readable_range_cached(at,28,3100)); // stale-readable (the fault case)
    d2vr::native_readable_range_forget(at,28);
    assert(!d2vr::native_readable_range_cached(at,28,3200));
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&old));
    assert(d2vr::native_executable_cached(reinterpret_cast<std::uintptr_t>(&main)));
    assert(!d2vr::native_executable_cached(at));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    assert(!d2vr::native_readable_range(at,28));
}
