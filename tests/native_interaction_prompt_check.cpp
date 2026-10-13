#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_interaction_prompt.hpp"
#include <cassert>
#include <iostream>
#include <unordered_map>
static constexpr std::uintptr_t fixture_base=0x140000000,fixture_iggy=0x700000,fixture_file=0x500000;
static unsigned constructs,destroys,gets;
static bool fail_get;
static void* last_object;
struct Box {unsigned char tag;unsigned char padding[7];double value;};
static Box box{7,{},37.625};
static bool read(std::uintptr_t address,void* output,std::size_t size) {
    SIZE_T copied{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&copied) && copied==size;
}
static void* __fastcall construct(void* output,void* input,const char* path) {
    ++constructs;last_object=output;assert(input==reinterpret_cast<void*>(fixture_file));assert(std::strcmp(path,"_interactionWindow")==0);
    auto* bytes=static_cast<unsigned char*>(output);const auto table=reinterpret_cast<std::uintptr_t>(bytes+0x28);
    const std::uint32_t index=1;std::memcpy(bytes+8,&fixture_iggy,8);std::memcpy(bytes+0x18,&table,8);std::memcpy(bytes+0x20,&index,4);return output;
}
static void __fastcall destroy(void* object) {assert(object==last_object);++destroys;}
static std::uint64_t __fastcall get(const void* record,std::uint64_t name,const char* text,void* flags,const char*,std::uint32_t* status) {
    ++gets;assert(record==static_cast<unsigned char*>(last_object)+0x28 && !name && !flags);
    *status=fail_get?0x3e9u:0u;
    if(std::strcmp(text,"x")==0)return reinterpret_cast<std::uintptr_t>(&box)+1;
    if(std::strcmp(text,"y")==0)return static_cast<std::uint64_t>(static_cast<std::int64_t>(-12)*2);
    if(std::strcmp(text,"width")==0)return 400*2;
    if(std::strcmp(text,"height")==0)return 100*2;
    assert(std::strcmp(text,"visible")==0);return fixture_base+0x2424049;
}
int main() {
    using namespace d2vr;auto reader=&read;
    // Gameplay HUD ownership does not depend on the frontend menu batches.
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    const auto put=[&](std::uintptr_t at,const auto& value) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i) memory[at+i]=bytes[i];
    };
    auto fixture_read=[&](std::uintptr_t at,void* output,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {
            const auto found=memory.find(at+i);if(found==memory.end()) return false;
            static_cast<unsigned char*>(output)[i]=found->second;
        }return true;
    };
    constexpr std::uintptr_t element=0x900000,block=0xa00000,path=0xb00000;
    put(element,fixture_base+0x1b3a3c8);put(element+0x90,fixture_file);
    put(element+0x98,fixture_iggy);put(element+0xa0,std::uintptr_t{0xc00000});
    put(fixture_file+0x1608,fixture_iggy);put(fixture_file+0x1658,path);
    put(path,"ui/hud.iggy");put(block+0x14,std::uint32_t{6});
    WristHudDrawIdentity::clear();
    assert(!native_prompt_call_owner(fixture_read,fixture_base,0x38f963,element+0x98,element+0x188,block));
    NativeWristHud::observe_draw(fixture_iggy,GetTickCount64());
    assert(native_prompt_call_owner(fixture_read,fixture_base,0x38f963,element+0x98,element+0x188,block));
    put(element,fixture_base+0x1c390f8); // Valid equipment widget is not Interactives.
    assert(!native_prompt_call_owner(fixture_read,fixture_base,0x38f963,element+0x98,element+0x188,block));
    put(element,fixture_base+0x1b3a3c8);
    assert(!native_prompt_call_owner(fixture_read,fixture_base,0x38f962,element+0x98,element+0x188,block));
    assert(!native_prompt_call_owner(fixture_read,fixture_base,0x38f963,element+0x98,element+0x180,block));
    put(block+0x14,std::uint32_t{5});
    assert(!native_prompt_call_owner(fixture_read,fixture_base,0x38f963,element+0x98,element+0x188,block));
    assert(native_prompt_number(reader,84)==42);
    assert(native_prompt_number(reader,static_cast<std::uint64_t>(-24ll))==-12);
    assert(native_prompt_number(reader,reinterpret_cast<std::uintptr_t>(&box)+1)==37.625);
    assert(!native_prompt_number(reader,7));assert(!native_prompt_number(reader,0xdead0001));
    box.tag=3;assert(!native_prompt_number(reader,reinterpret_cast<std::uintptr_t>(&box)+1));box.tag=7;
    box.value=NAN;assert(!native_prompt_number(reader,reinterpret_cast<std::uintptr_t>(&box)+1));box.value=37.625;
    const NativePromptCall owner{fixture_file,fixture_iggy,0x900000};const NativeInteractionPrompt::Api api{construct,destroy,get};
    const auto result=NativeInteractionPrompt::probe(read,fixture_base,owner,api);
    assert(result.valid && result.fields==31 && result.x==37.625 && result.y==-12 && result.width==400 && result.height==100 && result.visible);
    assert(constructs==1 && destroys==1 && gets==5);
    fail_get=true;const auto failed=NativeInteractionPrompt::probe(read,fixture_base,owner,api);
    assert(!failed.valid && failed.status==0x3e9 && failed.fields==0 && constructs==2 && destroys==2);
    std::cout<<"PASS: exact prompt path, boxed/fractional and signed native numbers, original geometry/visibility reads, getter failure and wrapper destruction\n";
}

