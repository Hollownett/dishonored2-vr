#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_wrist_hud.hpp"
#include <unordered_map>
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    constexpr std::uintptr_t base=0x7ff700000000,root=0x100000,callback=0x200000,batches=0x300000,
        wrapper=0x400000,file=0x500000,player=0x600000,iggy=0x700000,name=0x800000;
    constexpr auto manager=root+0x18;
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i) memory[at+i]=p[i];};
    auto path=[&](const char* text) {for(unsigned i=0;i<256;++i) memory[name+i]=0;for(std::size_t i=0;i<std::strlen(text);++i) memory[name+i]=static_cast<unsigned char>(text[i]);};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {for(std::size_t i=0;i<size;++i) {const auto p=memory.find(at+i);if(p==memory.end())return false;static_cast<unsigned char*>(out)[i]=p->second;}return true;};
    put(base+0x2274270,root);put(manager+0x828,callback);put(callback,base+0x1abe800);put(callback+8,manager);put(callback+0x10,base+0x32d40);
    put(manager+8,rig::List{batches,1,256});put(batches+0x10,rig::List{wrapper,1,256});
    put(wrapper,file);put(file+0x1618,player);put(file+0x1608,iggy);put(file+0x1658,name);
    put(wrapper+0xb08,std::array<float,2>{1920,1080});put(wrapper+0x878,rig::List{wrapper+0x900,2,512});
    path("ui/hud.iggy");assert(!wrist_hud_file_owned(read,base,file));
    NativeWristHud::observe_draw(iggy,GetTickCount64());assert(wrist_hud_file_owned(read,base,file));
    constexpr std::uintptr_t method=0x900000,block=0xa00000,slots=0xb00000;
    put(method-0x70,base+0x1c39598);put(method-0x88,base+0x1c398c8);put(method-0x80,base+0x1c390f8);
    auto arguments=[&](unsigned count,double a,double b=0,double c=0) {
        put(method-8,file);put(method,iggy);put(method+8,std::uintptr_t{0xc00000});
        put(block,slots);put(block+0x10,count);put(block+0x14,count);
        const double numbers[]{a,b,c};for(unsigned i=0;i<3;++i){put(slots+i*0x20,std::uint32_t{4});put(slots+i*0x20+0x10,numbers[i]);}
    };
    arguments(4,.73);auto health=wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block);
    assert(health && health->health_valid && health->health_fill==.73 && !health->ammo_valid);
    arguments(6,.24);auto mana=wrist_hud_arguments(read,base,0x6752a8,method,method+0x30,block);
    assert(mana && mana->mana_valid && mana->mana_fill==.24);
    arguments(3,7,42,3);auto ammo=wrist_hud_arguments(read,base,0x673e19,method,method+0x90,block);
    assert(ammo && ammo->ammo_valid && ammo->equipment_id==7 && ammo->ammo_count==42 && ammo->loaded_count==3);
    arguments(3,0,-1,-1);ammo=wrist_hud_arguments(read,base,0x673ef0,method,method+0x90,block);
    assert(ammo && !ammo->ammo_valid && ammo->ammo_count==-1);
    arguments(4,1.2);assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block));
    arguments(4,.8);put(slots,std::uint32_t{3});assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block));
    arguments(3,7,42.5,3);assert(!wrist_hud_arguments(read,base,0x673e19,method,method+0x90,block));
    arguments(4,.8);assert(!wrist_hud_arguments(read,base,0x6749bb,method,method+0xf0,block));
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0x100,block));
    path("ui/notes.iggy");assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block));
    WristHudDiagnostic diagnostic;
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::file_owner&&diagnostic.file==file&&diagnostic.iggy==iggy);
    wrist_hud_diagnostic_raw(read,diagnostic);
    assert(diagnostic.path==name&&std::strcmp(diagnostic.path_text.data(),"ui/notes.iggy")==0);
    assert(diagnostic.count==4&&diagnostic.capacity==4&&diagnostic.slots==slots);
    assert(diagnostic.read_mask&8); // Raw path exists but strict HUD ownership still rejected it.
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0x100,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::receiver);
    path("ui/hud.iggy");arguments(4,.8);put(block+0x14,unsigned{3});
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::argument_block);
    arguments(4,.8);put(slots,std::uint32_t{3});
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::argument_type);
    arguments(4,1.2);
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::argument_range);
    path("ui/hud.iggy");put(callback+8,manager+8);
    assert(wrist_hud_file_owned(read,base,file)); // Gameplay HUD does not belong to the menu callback/registry.
    WristHudDrawIdentity::clear();assert(!wrist_hud_file_owned(read,base,file));
    NativeWristHud::observe_draw(iggy,GetTickCount64()-251);assert(!wrist_hud_file_owned(read,base,file));
    NativeWristHud::observe_draw(iggy+8,GetTickCount64());assert(!wrist_hud_file_owned(read,base,file));
    NativeWristHud::observe_draw(iggy,GetTickCount64());assert(wrist_hud_file_owned(read,base,file));
    arguments(4,.8);put(method-0x70,base+0x1c390f8);
    assert(!wrist_hud_arguments(read,base,0x6749ba,method,method+0xf0,block,&diagnostic));
    assert(diagnostic.stage==WristHudReject::binding);
    static unsigned before_calls{},original_calls{},after_calls{};
    const auto observe=+[](bool before,std::uintptr_t caller,std::uintptr_t m,std::uintptr_t r,std::uintptr_t b,std::uint32_t result) noexcept {
        assert(caller==123&&m==1&&r==2&&b==3);
        if(before) {assert(result==0&&original_calls==0);++before_calls;}
        else {assert(result==17&&original_calls==1);++after_calls;}
    };
    const auto original=[](void* m,void* r,void* b)->std::uint32_t {
        assert(reinterpret_cast<std::uintptr_t>(m)==1&&reinterpret_cast<std::uintptr_t>(r)==2&&reinterpret_cast<std::uintptr_t>(b)==3);
        assert(before_calls==1&&after_calls==0);++original_calls;return 17;
    };
    assert(wrist_hud_observed_call(original,observe,123,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3))==17);
    assert(before_calls==1&&original_calls==1&&after_calls==1);
    std::cout<<"PASS: native-owned HUD argument ratios, integer ammo, invalid owner/caller/type/count rejection\n";
}
