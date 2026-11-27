#pragma once
#include <cstddef>
#include <cstring>
#include <excpt.h>

// Guarded native memory copies: a hardware fault (an access violation on memory the game just freed or unmapped) inside a __try block
// must reach its __except. cl catches it in __try { std::memcpy(...) } under /EHsc. clang-cl (the Linux build) does not: it treats the
// memcpy / memcmp / memmove builtins as unable to throw, so their calls are not covered by the __try region (only async exceptions, /EHa,
// would cover them) and the fault crashes the game (2026-11-26: Death of the Outsider crashed in read_self's memcpy while a save loaded:
// the page was freed between the readability check and the copy). Calling them through volatile pointers makes the calls opaque, so the
// __try covers them under both compilers. Every std::memcpy / memcmp / memmove / memset inside a __try in src/ goes through these.
// tests/seh_copy_check.cpp proves it under the build's own flags. EXCEPT src/bodyless_motion_guard.hpp (see its seh_read: its faults must reach the engine's handler).
namespace d2vr {
inline void* (*volatile seh_memcpy)(void*,const void*,std::size_t)=&std::memcpy;
inline void* (*volatile seh_memmove)(void*,const void*,std::size_t)=&std::memmove;
inline int (*volatile seh_memcmp)(const void*,const void*,std::size_t)=&std::memcmp;
inline void* (*volatile seh_memset)(void*,int,std::size_t)=&std::memset;

inline bool seh_copy(void* dst,const void* src,std::size_t size) noexcept {
    __try {seh_memcpy(dst,src,size);return true;}
    __except(GetExceptionCode()==0xC0000005UL /*access violation*/ || GetExceptionCode()==0xC0000006UL /*in-page error*/ ||
             GetExceptionCode()==0x80000001UL /*guard page*/ ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {return false;}
}
}
