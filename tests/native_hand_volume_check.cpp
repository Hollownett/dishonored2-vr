#include "../src/native_hand_volume.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace d2vr;
constexpr std::uintptr_t base=0x140000000;
unsigned calls{};bool corrupt{};
bool read_code(std::uintptr_t at,void* out,std::size_t size) {
    if(at==base+0xca6550 && size==NativeHandVolume::constructor_code.size()) std::memcpy(out,NativeHandVolume::constructor_code.data(),size);
    else if(at==base+0x95370 && size==NativeHandVolume::overlap_queue_code.size()) {std::memcpy(out,NativeHandVolume::overlap_queue_code.data(),size);if(corrupt) static_cast<unsigned char*>(out)[0x7c]^=1;}
    else return false;return true;
}
void* __fastcall construct(void* out,const float* origin,float radius) {
    ++calls;assert(reinterpret_cast<std::uintptr_t>(out)%16==0 && reinterpret_cast<std::uintptr_t>(origin)%16==0);
    assert(origin[0]==0 && origin[1]==0 && origin[2]==0 && origin[3]==0 && radius==static_cast<float>(interaction_volume_radius));
    const std::uintptr_t vt=base+0x1ec1e78;const std::uint32_t reference=0x1ffff;
    auto* bytes=static_cast<std::byte*>(out);std::memcpy(bytes,&vt,8);std::memcpy(bytes+0x10,&reference,4);
    std::memcpy(bytes+0x28,&radius,4);std::memcpy(bytes+0x50,origin,12);return out;
}
int main(int argc,char** argv) {
    static_assert(interaction_volume_radius==.45 && grab_acquire_distance==.25,"pickup reach must not shrink NPC discovery");
    corrupt=argc>1 && std::string(argv[1])=="reject";
    const auto shape=NativeHandVolume::shape(base,read_code,construct);
    if(corrupt) {assert(shape==0 && calls==0);corrupt=false;assert(NativeHandVolume::shape(base,read_code,construct)==0 && calls==0);std::cout<<"PASS: changed overlap ABI rejected before constructor\n";return 0;}
    assert(shape && calls==1);
    for(unsigned session=0;session<8;++session) assert(NativeHandVolume::shape(base,read_code,construct)==shape && calls==1);
    assert(NativeHandVolume::shape(base+1,read_code,construct)==0);
    InteractionRay input{};const Vec3 left{-1,2,3},right{1,2,3};
    for(auto centre:{left,right}) for(auto direction:{Vec3{1,0,0},Vec3{0,-1,0},Vec3{0,0,1}}) {
        const auto sweep=hand_volume_ray(input,centre,direction);assert(sweep);
        assert(sweep->f[0]==centre.x && sweep->f[1]==centre.y && sweep->f[2]==centre.z && sweep->f[7]==1);
        assert(std::abs(length(Vec3{sweep->f[4],sweep->f[5],sweep->f[6]})-.001)<1e-8);
        const auto direct=hand_volume_direct_ray(input,centre,direction);assert(direct && direct->f[7]==static_cast<float>(interaction_volume_radius));
        assert(std::abs(length(Vec3{direct->f[4],direct->f[5],direct->f[6]})-1)<1e-6);
        for(unsigned i=0;i<3;++i)if(direct->f[4+i]==0)assert(direct->f[8+i]==3.402820018e38f);
    }
    assert(!hand_volume_ray(input,left,{2,0,0}));assert(!hand_volume_ray(input,{NAN,0,0},{1,0,0}));
    assert(!hand_volume_ray(input,{1e100,0,0},{1,0,0})); // finite double cannot enter native float ABI
    reinterpret_cast<std::byte*>(shape)[0x28]^=std::byte{1};assert(NativeHandVolume::shape(base,read_code,construct)==0 && calls==1);
    std::cout<<"PASS: pinned constructor, immutable process lifetime, centre symmetry, bounded direct/sweep and mutation rejection\n";
}
