#include "../src/game_camera.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        std::array<std::byte,d2vr::camera_read_size> data{};
        constexpr std::uintptr_t base=0x140000000;
        std::uintptr_t a=base+d2vr::camera_vtable,b=base+d2vr::camera_secondary_vtable;
        std::memcpy(data.data(),&a,8); std::memcpy(data.data()+0x20,&b,8);
        float h=1.3962634f,v=0.8819849f;
        std::memcpy(data.data()+0xcc,&h,4); std::memcpy(data.data()+0xd0,&v,4);
        std::array<float,3> position{43,-55,5};
        std::array<float,9> basis{1,0,0,0,1,0,0,0,1};
        std::memcpy(data.data()+0xd8,position.data(),12);
        std::memcpy(data.data()+0xfc,basis.data(),36);
        auto pose=d2vr::decode_game_camera(data,base);
        require(pose.has_value() && pose->position==position,"Valid camera rejected");
        require(!d2vr::decode_game_camera(data,base+1),"Mismatched image accepted");
        auto invalid=data; float bad=std::numeric_limits<float>::quiet_NaN();
        std::memcpy(invalid.data()+0xd8,&bad,4);
        require(!d2vr::decode_game_camera(invalid,base),"Non-finite pose accepted");
        invalid=data; bad=-1;
        std::memcpy(invalid.data()+0xfc,&bad,4);
        require(!d2vr::decode_game_camera(invalid,base),"Mirrored basis accepted");
        invalid=data; bad=0;
        std::memcpy(invalid.data()+0xcc,&bad,4);
        require(!d2vr::decode_game_camera(invalid,base),"Invalid FoV accepted");
        std::cout << "PASS: native camera decode, image/type guards, pose/FoV/basis validation\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
