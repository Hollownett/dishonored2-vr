#include "../src/native_player_camera.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>

struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t recycle_at{},physics{};
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {
            auto found=bytes.find(at+i);if(found==bytes.end()) return false;
            static_cast<unsigned char*>(out)[i]=found->second;
        }
        if(at==recycle_at) {recycle_at=0;put(physics+0x18,d2vr::rig::Identity{77,10});}
        return true;
    }
    template<class T> void put(std::uintptr_t at,const T& value) {
        const auto* p=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];
    }
};
int main() {
    constexpr std::uintptr_t base=0x140000000,game=0x200000000,player=0x201000000,
        camera=0x202000000,body=0x203000000,controller=0x204000000,physics=0x205000000,delegate=0x206000000;
    Memory m;m.physics=physics;
    m.put(base+0x39a5290,game);m.put(game,base+0x1c940a8);m.put(game+0x3130b0,player);m.put(player,base+0x1c5e428);
    m.put(camera,std::array<std::byte,d2vr::camera_read_size>{});m.put(camera+0x25150,std::array<std::byte,0x200>{});
    for(auto [at,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,9>{
        {{camera,0x1c72218},{camera+0x20,0x1c722f8},{body,0x1c5a8d8},{body+0x20,0x1c5a9b0},
         {controller,0x1c72918},{physics,0x1c73268},{physics+0x20,0x1c73340},{physics+0x30,0x1c73378},{delegate+0x160,0x1b02768}}})
        m.put(at,base+table);
    for(auto at:{camera,body,controller,physics}) {m.put(at+0x10,player);m.put(at+0x18,d2vr::rig::Identity{5,10});}
    m.put(camera+0x25158,controller);m.put(camera+0x25160,physics);m.put(camera+0x25170,body);
    constexpr std::uintptr_t components=0x207000000;
    m.put(player+0x268,d2vr::rig::List{components,1,1<<8});m.put(components,camera);
    m.put(physics+0x48,delegate);m.put(delegate+0x168,physics);m.put(delegate+0x120,player+0x300);m.put(delegate+0x1e0,delegate);
    m.put(delegate+0xd0,1.5f);m.put(controller+0xd0,std::array<float,2>{.1f,-.2f});
    m.put(delegate+0xbc,0.f);m.put(delegate+0x40,0.f);m.put(player+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    auto s=d2vr::native_player_camera(m,base,camera);assert(s && s->heading==1.5f && s->physics==physics && s->look[1]==-.2f);
    assert(d2vr::find_native_player_camera(m,base));
    auto baseline=m;
    m.put(delegate+0x168,physics+8);assert(!d2vr::native_player_camera(m,base,camera));m=baseline;
    m.put(delegate+0x120,player+8);assert(!d2vr::native_player_camera(m,base,camera));m=baseline;
    m.put(delegate+0xd0,std::numeric_limits<float>::quiet_NaN());assert(!d2vr::native_player_camera(m,base,camera));m=baseline;
    m.put(camera+0x10,player+8);assert(!d2vr::native_player_camera(m,base,camera));m=baseline;
    m.put(camera+0x18,d2vr::rig::Identity{-1,10});assert(!d2vr::native_player_camera(m,base,camera));m=baseline;
    m.recycle_at=controller+0xd0;assert(!d2vr::native_player_camera(m,base,camera));
    std::cout<<"PASS: native camera/physics/delegate ownership, finite radians, generation and lifetime rejection; no native writes\n";
}
