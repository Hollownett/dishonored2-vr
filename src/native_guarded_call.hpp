#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace d2vr {
// Runs f() under a structured-exception guard. The build is /EHsc, so catch(...) does not see an access violation raised
// inside the engine; only __try/__except does. Returns true when f returned normally and false on any SEH exception.
// MSVC C2712: this body must not hold an object with a destructor, so it takes the callable by reference.
template<class F> bool guarded_call(F& f) noexcept {
    __try {f();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
} // namespace d2vr
