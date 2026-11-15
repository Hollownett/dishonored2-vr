#pragma once
#include <Windows.h>
#include "Protocol.hpp"

namespace dvrpsvr2 {
// Windows Interlocked operations, naturally aligned, between two x64 processes.
// No C++ standard-library objects or pointers cross the process boundary.
struct alignas(8) Shared {
    LONG magicValue;
    LONG version;
    alignas(8) volatile LONG64 desired;
    volatile LONG stop;
    volatile LONG status;
    volatile LONG result;
    volatile LONG appliedCount;
};
static_assert(offsetof(Shared, desired) == 8);
inline std::uint32_t tick() { return static_cast<std::uint32_t>(GetTickCount64()); }
inline void publish(Shared* shared, Profile left, Profile right) {
    InterlockedExchange64(&shared->desired, static_cast<LONG64>(pack(tick(), left, right)));
}
inline void status(Shared* shared, Status value, int result = 0) {
    InterlockedExchange(&shared->result, result);
    InterlockedExchange(&shared->status, static_cast<LONG>(value));
}
}
