#include "../src/native_slash_speed.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Fake engine memory: real buffers in this process, read with memcpy.
static bool read_mem(std::uintptr_t at,void* out,std::size_t n) {std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;}
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000;
    alignas(16) static unsigned char machine[0x400]{},attributes[0x60]{},pool[0x200]{},state[0x120]{},entries[0x28*4]{};
    static const char text[]="ARK_ATTRIBUTE_SLASH_SPEED_FACTOR";
    const auto put=[](void* at,std::uintptr_t v) {std::memcpy(at,&v,8);};
    put(machine+0x330,reinterpret_cast<std::uintptr_t>(attributes));put(machine+0x90,reinterpret_cast<std::uintptr_t>(pool));
    put(attributes,base+NativeSlashSpeed::attributes_vtable_rva);put(attributes+0x48,reinterpret_cast<std::uintptr_t>(entries));
    put(pool+0x10+5*8,reinterpret_cast<std::uintptr_t>(state));put(state,base+NativeSlashSpeed::slash_vtable_rva);
    const std::int32_t index=2;std::memcpy(state+0x108,&index,4);
    unsigned char* entry=entries+2*0x28;put(entry,reinterpret_cast<std::uintptr_t>(text));
    const auto value=[&] {float v;std::memcpy(&v,entry+0x24,4);return v;};
    const auto set=[&](float v) {std::memcpy(entry+0x24,&v,4);};
    auto exchange=[](std::uintptr_t at,std::uint64_t before,std::uint64_t after) {
        std::uint64_t now;std::memcpy(&now,reinterpret_cast<void*>(at),8);if(now!=before) return false;std::memcpy(reinterpret_cast<void*>(at),&after,8);return true;};
    auto read=&read_mem;const auto m=reinterpret_cast<std::uintptr_t>(machine);

    set(1.f);NativeSlashSpeed s;
    expect(s.apply(read,base,m,1.7f,exchange) && value()==1.7f,"game default 1.0 becomes the VR factor");
    expect(s.apply(read,base,m,1.7f,exchange) && value()==1.7f,"re-apply keeps it");
    s.restore(read,exchange);expect(value()==1.f,"stop restores 1.0");
    set(1.25f);NativeSlashSpeed t;
    expect(!t.apply(read,base,m,1.7f,exchange) && value()==1.25f,"a game modifier is left alone");
    set(1.f);std::memcpy(entry,"\0\0\0\0\0\0\0\0",8);put(entry,reinterpret_cast<std::uintptr_t>("ARK_ATTRIBUTE_BLINK_ASSAULT"));
    expect(!t.apply(read,base,m,1.7f,exchange) && value()==1.f,"another attribute name is never written");
    if(!failures) std::printf("PASS: slash speed factor written only over the game's 1.0, restored, other attributes untouched\n");
    return failures?1:0;
}
