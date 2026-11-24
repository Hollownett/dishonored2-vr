// Host check of src/native_power_cancel.hpp (GitHub issue #6): install-time verification of the UsePower release handler with
// the real code bytes of both games (D2 0x8566f0, DOTO 0x8bf940 placed at the D2 RVA with its resolver call re-aimed), and
// StopAim on a fake power.
// cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /I. tests\native_power_cancel_check.cpp
#include "src/native_power_cancel.hpp"
#include <cstdio>
#include <vector>

static int hook_calls=0;
static void __fastcall fake_original(void*) {}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID* out) {++hook_calls;*out=reinterpret_cast<void*>(&fake_original);return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++hook_calls;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++hook_calls;return MH_OK;}

static int failures=0;
#define EXPECT(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

namespace {
const unsigned char d2_handler[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xba,0x02,0x00,0x00,0x00,0x48,0x83,0xc1,0x58,0xe8,0x05,0x42,0xb8,0xff,0x48,0x8b,0xd8,0x48,0x85,0xc0,0x74,0x64,0x48,0x8b,0x4f,0x10,0xba,0x01,0x00,0x00,0x00,0x4c,0x8d,0x0d,0x0d,0xb1,0x17,0x03,0x44,0x8d,0x42,0x04,0xe8,0xf4,0xc4,0xfa,0xff,0x48,0x3b,0xd8,0x75,0x46,0x33,0xd2,0x48,0x8b,0xcb,0xe8,0xa5,0x55,0xdc,0xff,0x85,0xc0,0x75,0x38,0x38,0x83,0xa4,0x00,0x00,0x00,0x74,0x1e,0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x90,0x20,0x01,0x00,0x00,0x80,0xbb,0xa5,0x00,0x00,0x00,0x00,0x75,0x09,0x80,0xbb,0xa6,0x00,0x00,0x00,0x00,0x74,0x22,0x48,0x8b,0xcf,0x48,0x8b,0x5c,0x24,0x30,0x48,0x83,0xc4};
const unsigned char doto_handler[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xba,0x02,0x00,0x00,0x00,0x48,0x83,0xc1,0x68,0xe8,0xf5,0x56,0xb6,0xff,0x48,0x8b,0xd8,0x48,0x85,0xc0,0x74,0x67,0x48,0x8b,0x4f,0x10,0xba,0x01,0x00,0x00,0x00,0x4c,0x8d,0x0d,0xdd,0xcf,0x1e,0x03,0x44,0x8d,0x42,0x05,0xe8,0x14,0xa5,0xfa,0xff,0x48,0x3b,0xd8,0x75,0x49,0x41,0xb0,0x01,0x33,0xd2,0x48,0x8b,0xcb,0xe8,0x82,0x20,0xdb,0xff,0x85,0xc0,0x75,0x38,0x38,0x83,0xa4,0x00,0x00,0x00,0x74,0x1e,0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x90,0x20,0x01,0x00,0x00,0x80,0xbb,0xa5,0x00,0x00,0x00,0x00,0x75,0x09,0x80,0xbb,0xa6,0x00,0x00,0x00,0x00,0x74,0x22,0x48,0x8b,0xcf,0x48,0x8b,0x5c,0x24,0x30};
static_assert(sizeof(d2_handler)==0x80 && sizeof(doto_handler)==0x80);

struct Image {
    std::vector<unsigned char> bytes;
    explicit Image(const unsigned char* handler,bool aim_resolver_at_d2=false):bytes(d2vr::usepower_release_rva+0x100,0xcc) {
        std::memcpy(bytes.data()+d2vr::usepower_release_rva,handler,0x80);
        if(aim_resolver_at_d2) {   // the DOTO code placed at the D2 RVA: its call must still reach usepower_resolve_rva
            const auto rel=static_cast<std::int32_t>(static_cast<std::intptr_t>(d2vr::usepower_resolve_rva)-static_cast<std::intptr_t>(d2vr::usepower_release_rva+0x1b));
            std::memcpy(bytes.data()+d2vr::usepower_release_rva+0x17,&rel,4);
        }
    }
    std::uintptr_t base() const {return reinterpret_cast<std::uintptr_t>(bytes.data());}
};
const Image* g_image{};
bool read_image(std::uintptr_t at,void* out,std::size_t n) {
    const auto b=g_image->base();
    if(at<b || at+n>b+g_image->bytes.size()) return false;
    std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;
}
bool installs(const Image& img,std::string* why=nullptr) {
    g_image=&img;d2vr::NativePowerCancel c;
    try {c.install(img.base(),img.bytes.size(),&read_image);} catch(const std::exception& e) {if(why) *why=e.what();return false;}
    const auto json=c.status_json();
    if(why) *why=json;
    c.disable();
    return true;
}

struct FakePower {const std::uintptr_t* vt;unsigned char pad[0xa4-sizeof(void*)];unsigned char active;unsigned char cast;};
int stops=0;
void __fastcall fake_stop(void* p) {++stops;static_cast<FakePower*>(p)->active=0;}
}

int main() {
    {   // d2_handler_installs, list offset 0x58
        Image img(d2_handler);std::string s;EXPECT(installs(img,&s));
        EXPECT(s.find("\"list_offset\":88")!=std::string::npos);
    }
    {   // doto_handler_installs, list offset 0x68 (DOTO's extra "mov r8b,1" shifts the cast gate: found by the scan)
        Image img(doto_handler,true);std::string s;EXPECT(installs(img,&s));
        EXPECT(s.find("\"list_offset\":104")!=std::string::npos);
    }
    {   // a changed prologue refuses
        Image img(d2_handler);img.bytes[d2vr::usepower_release_rva+5]=0x56;std::string s;
        EXPECT(!installs(img,&s) && s.find("differs")!=std::string::npos);
    }
    {   // a resolver call that goes elsewhere refuses
        Image img(doto_handler);std::string s;   // DOTO bytes at the D2 RVA without re-aiming: the call misses usepower_resolve_rva
        EXPECT(!installs(img,&s) && s.find("resolver")!=std::string::npos);
    }
    {   // no cast gate (the Cast slot moved) refuses
        Image img(d2_handler);img.bytes[d2vr::usepower_release_rva+0x5f]=0x28;std::string s;   // call [rax+0x128] instead of +0x120
        EXPECT(!installs(img,&s) && s.find("cast gate")!=std::string::npos);
    }
    {   // stop_aim: an aiming power gets StopAim (vt slot 0x128/8) once; it clears the aim
        std::uintptr_t vt[64]{};vt[d2vr::NativePowerCancel::stop_aim_slot]=reinterpret_cast<std::uintptr_t>(&fake_stop);
        FakePower p{};p.vt=vt;p.active=1;
        EXPECT(d2vr::NativePowerCancel::stop_aim(reinterpret_cast<std::uintptr_t>(&p),0,~std::uintptr_t{}));
        EXPECT(stops==1 && p.active==0);
        // not aiming: no call
        EXPECT(!d2vr::NativePowerCancel::stop_aim(reinterpret_cast<std::uintptr_t>(&p),0,~std::uintptr_t{}) && stops==1);
        // a vtable outside the image: no call
        p.active=1;EXPECT(!d2vr::NativePowerCancel::stop_aim(reinterpret_cast<std::uintptr_t>(&p),1,2) && stops==1);
        // null power: no call, no fault
        EXPECT(!d2vr::NativePowerCancel::stop_aim(0,0,~std::uintptr_t{}) && stops==1);
    }
    EXPECT(hook_calls>0);
    if(failures) {std::printf("native_power_cancel_check: %d failure(s)\n",failures);return 1;}
    std::printf("native_power_cancel_check: ok\n");
    return 0;
}
