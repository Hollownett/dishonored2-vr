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
    assert(d2vr::native_readable_range_cached(at,28,1050));  // cached inside TTL
    assert(!d2vr::native_readable_range_cached(at,28,1200)); // expired -> requeried
    assert(!d2vr::native_readable_range_cached(at+page-14,28,1200));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    assert(!d2vr::native_readable_range(at,28));
}
