#include "../src/native_torso_fit.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <limits>
#include <cstring>
namespace fixture_data {
#include "native_rig_fixture.hpp"
}
int main() {
    using namespace fixture_data;
    auto memory=fixture();
    constexpr std::uintptr_t descriptor=0x211000000,skeleton=0x212000000,names=0x213000000,inverse=0x214000000,
        name_pool=0x215000000,text=0x216000000,source=0x217000000,geometry=0x218000000,models_list=0x219000000,
        primary=0x21a000000,surfaces=0x21b000000,surface=0x21c000000;
    memory.put(main_anim+0x18,r::Identity{321,10});memory.put(main_model+0x1fe8,descriptor);
    memory.put(descriptor+0x50,skeleton);memory.put(skeleton,base_address+0x1b0bd08);
    memory.put(skeleton+0x78,std::uint16_t{20});memory.put(skeleton+0x70,names);memory.put(skeleton+0x120,inverse);
    memory.put(base_address+0x22995c0,base_address+0x22995d0);memory.put(base_address+0x22995d0,base_address+0x1b093c8);
    memory.list(base_address+0x22995d8,name_pool,20,20);
    for(unsigned joint=0;joint<20;++joint) {
        memory.put(names+joint*2,static_cast<std::uint16_t>(joint));memory.put(name_pool+joint*0x48+0x10,text+joint*128);
        const std::string label=joint==0?"origin":joint==4?"center_spine03_jnt":joint==8?"camera_jnt":"joint_"+std::to_string(joint);
        for(unsigned i=0;i<128;++i)memory.put(text+joint*128+i,static_cast<unsigned char>(i<label.size()?label[i]:0));
        memory.put(inverse+joint*48,std::array<float,12>{1,0,0,0,0,1,0,0,0,0,1,0});
    }
    memory.put(descriptor+8,source);memory.put(main_model+0x1bb0,source);memory.put(descriptor+0x68,geometry);
    memory.put(main_model+0x1890,std::array<float,3>{1,1,1});
    memory.list(geometry,models_list,1,1);memory.put(models_list,primary);memory.put(primary,base_address+0x1b0d118);
    memory.list(primary+0x58,surfaces,1,1);memory.put(surfaces,surface);memory.put(surface+0x30,std::uint32_t{0});
    const auto resource=[&](const std::string& name) {for(size_t i=0;i<=name.size();++i)memory.put(source+i,static_cast<unsigned char>(name[i]));};
    resource("models/characters/player/emily/body.md6");
    memory.put(surface+0x54,std::uint32_t{7951});memory.put(surface+0x58,std::uint32_t{13504});
    const auto arms=r::player_arms(memory,base_address);assert(arms);
    const auto token=d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]);assert(token);
    assert(token->body.model==main_model && token->mesh.primary==primary && token->width_metres==.25142084062099457);
    assert(d2vr::native_torso_fit_current(memory,base_address,*token));
    unsigned cheap_reads{};
    auto cheap=[&](std::uintptr_t at,void* out,std::size_t size) {
        ++cheap_reads;assert(at!=names && at!=inverse && at!=source && at!=name_pool && at!=text);
        return memory(at,out,size);
    };
    assert(d2vr::native_torso_fit_current(cheap,base_address,*token) && cheap_reads<45);
    const auto original=memory;
    memory.put(main_model+0x1890,std::array<float,3>{1.2f,1.2f,1.2f});
    assert(!d2vr::native_torso_fit_current(memory,base_address,*token));
    const auto scaled=d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]);assert(scaled);
    assert(std::abs(scaled->width_metres-token->width_metres*1.2f)<1e-9);
    memory.put(main_model+0x1890,std::array<float,3>{1,1.2f,1});assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory=original;memory.put(player_address+0xf0,std::uintptr_t{0x300000000});assert(!d2vr::native_torso_fit_current(memory,base_address,*token));
    memory=original;memory.put(main_anim+0x18,r::Identity{999,10});assert(!d2vr::native_torso_fit_current(memory,base_address,*token));
    memory=original;memory.put(game_address+0x3130b0,std::uintptr_t{0x300000000});assert(!d2vr::native_torso_fit_current(memory,base_address,*token));
    memory=original;
    resource("models/characters/player/corvo/body.md6");assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory.put(surface+0x54,std::uint32_t{10391});memory.put(surface+0x58,std::uint32_t{18259});
    const auto corvo=d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]);assert(corvo && corvo->width_metres==.353106752038002);
    resource("models/characters/player/unknown/body.md6");assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory=original;memory.put(surface+0x30,std::uint32_t{1});assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory=original;memory.put(main_model+0x1bb0,std::uintptr_t{0x300000000});assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory=original;memory.put(body_address+0x18,r::Identity{999,10});assert(!d2vr::native_torso_fit_token(memory,base_address,(*arms)[0]));
    memory=original;unsigned reads{};
    auto changed=[&](std::uintptr_t at,void* out,std::size_t size) {
        const auto ok=memory(at,out,size);
        if(at==surface+0x54 && ++reads==2) memory.put(surface+0x54,std::uint32_t{7952});
        return ok;
    };
    assert(!d2vr::native_torso_fit_token(changed,base_address,(*arms)[0]));
    memory=original;reads=0;
    auto changed_name=[&](std::uintptr_t at,void* out,std::size_t size) {
        const auto ok=memory(at,out,size);if(at==source && ++reads==1)memory.put(source,static_cast<unsigned char>('x'));return ok;
    };
    assert(!d2vr::native_torso_fit_token(changed_name,base_address,(*arms)[0]));
    std::cout<<"PASS: owned body wrapper/mesh torso baseline and malformed/replaced/mid-read identity rejection\n";
}
