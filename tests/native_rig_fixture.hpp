#pragma once
#include "../src/game_rig.hpp"
#include <map>
#include <limits>
#include <cstring>
namespace r=d2vr::rig;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t recycle_at{};
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {
            const auto p=bytes.find(at+i);if(p==bytes.end()) return false;
            static_cast<unsigned char*>(out)[i]=p->second;
        }
        if(at==recycle_at) {recycle_at=0;put(at,r::Identity{99,10});}
        return true;
    }
    template<class T> void put(std::uintptr_t at,const T& v) {
        auto p=reinterpret_cast<const unsigned char*>(&v);
        for(std::size_t i=0;i<sizeof(v);++i) bytes[at+i]=p[i];
    }
    void list(std::uintptr_t at,std::uintptr_t data,std::uint32_t count,std::uint32_t capacity) {
        put(at,r::List{data,count,capacity<<8});
    }
};
constexpr std::uintptr_t base_address=0x140000000,game_address=0x200000000,player_address=0x201000000,body_address=0x202000000,
    main_anim=0x203000000,main_model=0x204000000,components_address=0x208000000;
constexpr std::array<std::uintptr_t,2> animations{0x205000000,0x205010000},models{0x206000000,0x206010000},
    descriptors{0x207000000,0x207010000},skeletons{0x207020000,0x207030000},
    poses{0x209000000,0x209010000},states_address{0x20a000000,0x20a010000},maps{0x20b000000,0x20b010000};
Memory fixture() {
    Memory m;
    m.put(base_address+0x39a5290,game_address);m.put(game_address,base_address+0x1c940a8);m.put(game_address+0x3130b0,player_address);
    m.put(player_address,base_address+0x1c5e428);m.put(player_address+0xf0,main_model);m.put(player_address+0x360,main_model);
    m.put(main_model,base_address+0x1b0d628);m.list(player_address+0x268,components_address,1,1);m.put(components_address,body_address);
    m.put(body_address,base_address+0x1c5a8d8);m.put(body_address+0x20,base_address+0x1c5a9b0);m.put(body_address+0x10,player_address);
    m.put(body_address+0x18,r::Identity{123,10});m.put(body_address+0x58,main_anim);
    m.put(main_anim,base_address+0x1b80408);m.put(main_anim+0x20,base_address+0x1b804f0);m.put(main_anim+0x38,main_model);
    for(unsigned side=0;side<2;++side) {
        const auto a=animations[side];m.put(body_address+(side?0x688:0x378),a);
        m.put(a,base_address+0x1b80408);m.put(a+0x20,base_address+0x1b804f0);m.put(a+0x18,r::Identity{static_cast<int>(200+side),10});
        m.put(a+0x38,models[side]);m.put(models[side],base_address+0x1b0d628);
        m.put(models[side]+0x1fe8,descriptors[side]);m.put(descriptors[side]+0x50,skeletons[side]);
        m.put(skeletons[side]+0x78,std::uint16_t{69});m.put(models[side]+0x1860,std::array<float,15>{});
        m.list(a+0x2d8,poses[side],206,206);m.list(a+0x2e8,states_address[side],4,4);
        m.list(states_address[side]+0x30,maps[side],69,69);
        r::Transform t{};t.f[0]=t.f[5]=t.f[10]=1;
        // Havok SIMD padding is not part of the rigid transform.
        t.f[3]=t.f[7]=t.f[11]=t.f[15]=std::numeric_limits<float>::quiet_NaN();
        for(unsigned i=0;i<69;++i) {
            m.put(maps[side]+i*2,static_cast<std::int16_t>(137+i));t.f[12]=static_cast<float>(i);
            m.put(poses[side]+(137+i)*64,t);
        }
    }
    return m;
}
