#include "../src/native_gameplay_aim.hpp"
#include <cassert>
#include <iostream>
#include <vector>
static constexpr std::uintptr_t base=0x140000000;
static unsigned creates{},enables{},disables{},fail_enable{};
static bool bad_call{};
static void __fastcall original(void*) {}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* out) {
    ++creates;*out=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {
    ++enables;return enables==fail_enable ? MH_ERROR_NOT_EXECUTABLE : MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++disables;return MH_OK;}
static std::vector<unsigned char> hex(const char* text) {
    std::vector<unsigned char> bytes;
    while(*text) {char part[]{text[0],text[1],0};bytes.push_back(static_cast<unsigned char>(std::strtoul(part,nullptr,16)));text+=2;}
    return bytes;
}
static bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(size==32 && (at==base+0x7f6a80 || at==base+0x7f4b30)) {
        const auto bytes=hex(at==base+0x7f6a80 ?
            "48897c242055488d6c24d04881ec30010000488bf9488b4910488d55400f29bc" :
            "4c8bdc574881ec80000000488bf984d20f845e010000440fb7819a510200488b");
        std::memcpy(out,bytes.data(),size);return true;
    }
    if(size==5 && (at==base+0x9533ee || at==base+0x7f7275)) {
        const auto target=at==base+0x9533ee ? base+0x7f6a80 : base+0x7f4b30;
        const auto displacement=static_cast<std::int32_t>(target-at-5)+(bad_call ? 1 : 0);
        auto* bytes=static_cast<unsigned char*>(out);bytes[0]=0xe8;std::memcpy(bytes+1,&displacement,4);return true;
    }
    return false;
}
static bool write(std::uintptr_t,const void*,std::size_t) {assert(false);return false;}
static UINT logic() {return 2;}
int main() {
    d2vr::NativeGameplayAim writer;
    bad_call=true;bool rejected=false;
    try {writer.install(base,read,logic,write,L"build/native-aim-install-test.csv");}
    catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && creates==0 && enables==0 && writer.disable());
    bad_call=false;writer.install(base,read,logic,write,L"build/native-aim-install-test.csv");
    assert(creates==2 && enables==2 && !writer.routing() && writer.applied()==0);
    assert(writer.disable() && disables==2 && !writer.routing());
    fail_enable=enables+2;rejected=false;
    try {writer.install(base,read,logic,write,L"build/native-aim-install-test.csv",true);}
    catch(const std::runtime_error&) {rejected=true;}
    assert(rejected && disables==3 && !writer.routing() && writer.disable());
    fail_enable=0;writer.install(base,read,logic,write,L"build/native-aim-install-test.csv",true);
    assert(writer.routing() && writer.applied()==0 && writer.disable() && !writer.routing());
    std::cout<<"PASS: runtime caller rejection, default-off install, enable failure rollback, explicit route and cleanup\n";
}
