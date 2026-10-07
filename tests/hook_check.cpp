#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>
#include <iostream>

using Function=int (*)(int);
static Function original{};
// Multiple instructions ensure the test exercises a real trampoline.
__declspec(noinline) int arithmetic(int value) { return (value*7+19) ^ (value>>1); }
int intercepted(int value) { return original(value)+1000; }

int main() {
    auto fail=[](const char* message) {std::cerr << message << '\n'; return 1;};
    Function volatile call=&arithmetic;
    int before[20]{};
    for(int i=0;i<20;++i) before[i]=call(i);
    if(MH_Initialize()!=MH_OK) return fail("Hook initialization failed");
    void* target=reinterpret_cast<void*>(&arithmetic);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&intercepted),reinterpret_cast<void**>(&original))!=MH_OK)
        return fail("Hook creation failed");
    if(MH_EnableHook(target)!=MH_OK) return fail("Hook enable failed");
    for(int i=0;i<20;++i) if(call(i)!=before[i]+1000) return fail("Trampoline did not preserve result");
    if(MH_DisableHook(target)!=MH_OK) return fail("Hook disable failed");
    for(int i=0;i<20;++i) if(call(i)!=before[i]) return fail("Original function not restored");
    if(MH_Uninitialize()!=MH_OK) return fail("Hook cleanup failed");
    std::cout << "PASS: native hook, trampoline preservation, disable and original restoration\n";
}
