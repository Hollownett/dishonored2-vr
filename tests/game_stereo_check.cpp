#include "../src/game_stereo.hpp"
#include <iostream>
#include <limits>

int main() {
    constexpr std::uintptr_t base=0x7ff712340000;
    std::array<std::byte,80> data{};
    for(int eye=0;eye<2;++eye) {
        auto at=data.data()+eye*40;
        std::uintptr_t label=base+0x1ab2848;
        std::array<float,4> viewport{eye==0?0.0f:0.5f,0.0f,0.5f,1.0f};
        float side=eye==0?-1.0f:1.0f;
        std::memcpy(at,&label,8);std::memcpy(at+12,viewport.data(),16);
        std::memcpy(at+32,&side,4);at[28]=at[29]=std::byte{1};
    }
    auto require=[](bool result) {if(!result) {std::cerr << "Stereo metadata guard failed\n";return false;}return true;};
    if(!require(d2vr::valid_stereo_metadata(data,base))) return 1;
    if(!require(!d2vr::valid_stereo_metadata(data,base+0x10000))) return 1;
    auto wrong=data;wrong[28]=std::byte{0};
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    wrong=data;std::uint32_t split_screen=1;std::memcpy(wrong.data()+48,&split_screen,4);
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    wrong=data;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(wrong.data()+52,&nan,4);
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    std::cout << "PASS: stereo metadata, relocation, viewport, flags and split-screen rejection\n";
}
