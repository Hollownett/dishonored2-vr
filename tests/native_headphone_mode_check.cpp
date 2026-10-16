#include "../src/native_headphone_mode.hpp"
#include <cstdio>
#include <cstdlib>
using H=d2vr::NativeHeadphoneMode;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
std::uintptr_t base{},main_pointer{};unsigned char flags=0x24;std::uint64_t cache{};
unsigned calls{},last_rule{},last_output{},last_id{},pointer_reads{},switch_at{},fail_at{};
std::uintptr_t switch_to{},queued_owner{};
bool corrupt{},bad_name{},unreadable{};int result=1;
int __fastcall fake_queue(std::uint32_t rule,std::uint32_t output,std::uint32_t id) {
    ++calls;last_rule=rule;last_output=output;last_id=id;queued_owner=main_pointer;return result;
}
bool read_mem(std::uintptr_t at,void* out,std::size_t n) {
    if(at==base+H::queue_rva && n==H::queue_code.size()) {
        std::memcpy(out,H::queue_code.data(),n);if(corrupt)static_cast<unsigned char*>(out)[0]^=1;return true;
    }
    const char* name=bad_name?"s_invalidHeadphone":"s_useHeadphoneMode";
    if(at==base+H::cvar_rva+0x30 && n==8){const auto p=reinterpret_cast<std::uintptr_t>(name);std::memcpy(out,&p,n);return true;}
    if(at==reinterpret_cast<std::uintptr_t>(name) && n==19){std::memcpy(out,name,n);return true;}
    if(at==base+H::main_pointer_rva && n==8){
        ++pointer_reads;if(unreadable || pointer_reads==fail_at)return false;
        if(pointer_reads==switch_at)main_pointer=switch_to;
        std::memcpy(out,&main_pointer,n);return true;
    }
    if(at==main_pointer && n==1){std::memcpy(out,&flags,n);return true;}
    if(at==base+H::cvar_rva+0x28 && n==8){std::memcpy(out,&cache,n);return true;}
    return false;
}
int main() {
    // The fake base makes the actual production typed call land on fake_queue;
    // Read supplies exact pinned queue bytes and the guarded native state.
    base=reinterpret_cast<std::uintptr_t>(&fake_queue)-H::queue_rva;
    main_pointer=reinterpret_cast<std::uintptr_t>(&flags);
    H mode;CHECK(H::verify(base,read_mem));CHECK(!mode.enabled());
    CHECK(mode.install(base,read_mem) && mode.enabled() && calls==1);
    CHECK(last_rule==1 && last_output==0x20 && last_id==0 && flags==0x24 && cache==0);
    CHECK(mode.install(base,read_mem) && calls==1);
    CHECK(mode.restore() && calls==2 && last_rule==0 && flags==0x24 && cache==0);
    CHECK(mode.restore() && calls==2);
    flags=0x64;cache=0x3f80000000000001ull;
    CHECK(mode.install(base,read_mem) && mode.restore() && calls==4 && last_rule==1);
    CHECK(flags==0x64 && cache==0x3f80000000000001ull);
    flags=0x24;cache=0;CHECK(mode.install(base,read_mem));cache=2;
    CHECK(mode.restore() && !mode.owned() && calls==5 && cache==2);
    cache=0;CHECK(mode.install(base,read_mem));flags=0x64;
    CHECK(mode.restore() && !mode.owned() && calls==6 && flags==0x64);
    corrupt=true;CHECK(!mode.install(base,read_mem));corrupt=false;
    bad_name=true;CHECK(!mode.install(base,read_mem));bad_name=false;
    flags=0;CHECK(!mode.install(base,read_mem) && calls==6);
    flags=0x24;result=2;CHECK(!mode.install(base,read_mem) && calls==7);
    result=1;CHECK(mode.install(base,read_mem));flags=0;
    CHECK(!mode.restore() && mode.owned() && calls==8);flags=0x24;CHECK(mode.restore() && calls==9);
    // Owner replacement retires the old baseline without queueing it.
    H owner;main_pointer=0x1000;flags=0x24;cache=0;
    CHECK(owner.install(base,read_mem));const auto start=calls;
    main_pointer=0x2000;flags=0x64;cache=0x3f80000000000001ull;
    CHECK(owner.restore() && !owner.owned() && calls==start);
    CHECK(owner.install(base,read_mem) && queued_owner==0x2000 && last_rule==1);
    CHECK(owner.restore() && last_rule==1 && calls==start+2);
    // Active install rebinds a fresh replacement and its own baseline.
    main_pointer=0x3000;flags=0x64;CHECK(owner.install(base,read_mem));
    main_pointer=0x4000;flags=0x24;cache=0;
    CHECK(owner.install(base,read_mem) && queued_owner==0x4000 && last_rule==1);
    CHECK(owner.restore() && last_rule==0 && queued_owner==0x4000);
    // A readable null ends the previous owner; a later instance can bind.
    CHECK(owner.install(base,read_mem));auto before=calls;main_pointer=0;
    CHECK(!owner.install(base,read_mem) && !owner.owned() && calls==before);
    main_pointer=0x5000;flags=0x64;CHECK(owner.install(base,read_mem));
    before=calls;main_pointer=0;CHECK(owner.restore() && !owner.owned() && calls==before);
    main_pointer=0x6000;flags=0x24;CHECK(owner.install(base,read_mem));
    // Unreadable identity and same-owner inactivity defer, retaining receipt.
    before=calls;unreadable=true;
    CHECK(!owner.restore() && owner.owned() && calls==before);
    CHECK(!owner.install(base,read_mem) && owner.owned() && calls==before);
    unreadable=false;flags=0;CHECK(!owner.restore() && owner.owned() && calls==before);
    flags=0x24;result=2;CHECK(!owner.restore() && owner.owned() && calls==before+1);
    result=1;CHECK(owner.restore() && !owner.owned() && last_rule==0);
    // Replacement between the identity sandwich reads must never queue.
    main_pointer=0x7000;flags=0x24;CHECK(owner.install(base,read_mem));before=calls;
    switch_to=0x8000;switch_at=pointer_reads+2;
    CHECK(owner.restore() && !owner.owned() && calls==before);switch_at=0;
    // Replacement in the final prequeue snapshot retires restoration as well.
    CHECK(owner.install(base,read_mem));before=calls;switch_to=0x9000;switch_at=pointer_reads+4;
    CHECK(owner.restore() && !owner.owned() && calls==before);switch_at=0;
    // New installation cannot queue with identity changing after baseline capture.
    before=calls;switch_to=0xa000;switch_at=pointer_reads+5;
    CHECK(!owner.install(base,read_mem) && !owner.owned() && calls==before);switch_at=0;
    CHECK(owner.install(base,read_mem));before=calls;
    // A failed sandwich read is uncertainty, not a replacement receipt.
    fail_at=pointer_reads+2;CHECK(!owner.restore() && owner.owned() && calls==before);fail_at=0;
    cache=2;CHECK(owner.restore() && !owner.owned() && calls==before);
    // Native preference changes on the same active owner are not overridden.
    cache=0;CHECK(owner.install(base,read_mem));before=calls;cache=2;
    CHECK(!owner.install(base,read_mem) && owner.owned() && calls==before);
    CHECK(owner.restore() && !owner.owned() && calls==before);
    std::puts("native_headphone_mode_check ok: lifecycle replacement, null, deferral, identity sandwiches, preferences and queue failures");
}
