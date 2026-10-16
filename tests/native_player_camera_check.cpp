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
    m=baseline;
    constexpr std::uintptr_t machine=0x208000000,pool=0x209000000,mode=0x20a000000,shadow=0x20b000000;
    m.put(camera+0x25200,7);m.put(camera+0x25204,7);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view() && !s->tracked_shadow_walk());
    m.put(controller+0x58,machine);m.put(controller+0x172,std::uint8_t{0});
    m.put(machine,base+0x1c73d58);m.put(machine+0x10,player);m.put(machine+0x90,pool);
    m.put(machine+0x18,d2vr::rig::Identity{12,10});
    m.put(player+0x268,d2vr::rig::List{components,2,2<<8});m.put(components+8,machine);
    m.put(pool+8,mode);m.put(mode,base+0x1c83ed8);m.put(mode+0x60,2u);
    for(const auto& entry:d2vr::native_action_camera_states) m.put(pool+0x10+entry.index*8,std::uintptr_t{});
    const auto shadow_state=[&](unsigned index) {
        m.put(pool+0x10+index*8,shadow);m.put(shadow,base+0x1c7ed28);
        m.put(shadow+8,index);m.put(shadow+0x10,machine);m.put(shadow+0x20,std::uint8_t{1});
        m.put(shadow+0x18,shadow+0x1000);m.put(shadow+0x1000,base+0x1c7ebf8);m.put(shadow+0x1060,index);
    };
    shadow_state(0);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->tracked_shadow_walk() && !s->authored_view());
    const auto ground=m;
    m.put(machine+0x18,d2vr::rig::Identity{-1,10});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(machine+0x18,d2vr::rig::Identity{12,0});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(components+8,controller);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;
    constexpr std::uintptr_t assassin=0x20c000000;
    m.put(pool+0x10+13*8,assassin);m.put(assassin,base+0x1c83468);
    m.put(assassin+8,13u);m.put(assassin+0x10,machine);m.put(assassin+0x20,std::uint8_t{1});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;
    m.put(pool+0x10,std::uintptr_t{});shadow_state(1);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->tracked_shadow_walk() && !s->authored_view());
    m=ground;m.put(controller+0x172,std::uint8_t{1});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(shadow,base+0x1c85628);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(shadow+0x10,player);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(shadow+8,1u);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(shadow+0x20,std::uint8_t{0});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(mode+0x60,0u);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(camera+0x25204,0);
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(camera+0x251b9,std::uint8_t{1});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;m.put(camera+0x25260,std::uint8_t{1});
    s=d2vr::native_player_camera(m,base,camera);assert(s && s->authored_view());
    m=ground;
    bool changed=false;
    auto racing=[&](std::uintptr_t at,void* out,std::size_t size) {
        const bool ok=m(at,out,size);
        if(at==shadow+0x20 && !changed) {changed=true;m.put(controller+0x172,std::uint8_t{1});}
        return ok;
    };
    s=d2vr::native_player_camera(racing,base,camera);assert(changed && s && s->authored_view());
    for(unsigned mutation=0;mutation<8;++mutation) {
        m=ground;changed=false;
        auto changing_owner=[&](std::uintptr_t at,void* out,std::size_t size) {
            const bool ok=m(at,out,size);
            if(at==shadow+0x20 && !changed) {
                changed=true;
                switch(mutation) {
                    case 0:m.put(machine+0x18,d2vr::rig::Identity{13,10});break;
                    case 1:m.put(machine+0x90,pool+0x1000);break;
                    case 2:m.put(components+8,controller);break;
                    case 3:m.put(player+0x268,d2vr::rig::List{components,1,2<<8});break;
                    case 4:m.put(camera+0x25158,controller+0x1000);break;
                    case 5:m.put(camera+0x25200,0);break;
                    case 6:m.put(camera+0x18,d2vr::rig::Identity{6,10});break;
                    case 7:m.put(game+0x3130b0,player+0x1000);break;
                }
            }
            return ok;
        };
        s=d2vr::native_player_camera(changing_owner,base,camera);
        assert(changed && (!s || (s->authored_view() && !s->tracked_shadow_walk())));
    }
    std::cout<<"PASS: native camera/physics/delegate ownership, finite radians, generation and lifetime rejection; no native writes\n";
}
