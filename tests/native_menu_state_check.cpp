#include "../src/native_menu_state.hpp"
#include <unordered_map>
#include <cassert>
#include <iostream>
int main() {
    constexpr std::uintptr_t base=0x7ff700000000,root=0x100000,callback=0x200000,batches=0x300000,wrapper=0x400000,file=0x500000,player=0x600000;
    constexpr auto manager=root+0x18;
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i) memory[at+i]=p[i];};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {const auto p=memory.find(at+i);if(p==memory.end()) return false;static_cast<unsigned char*>(out)[i]=p->second;}return true;
    };
    put(base+0x2274270,root);put(manager+0x828,callback);put(callback,base+0x1abe800);put(callback+8,manager);put(callback+0x10,base+0x32d40);
    put(manager+8,d2vr::rig::List{batches,1,256});put(batches+0x10,d2vr::rig::List{wrapper,1,256});
    put(wrapper,file);put(file+0x1618,player);put(file+0x1608,player+0x100);put(wrapper+0xb08,std::array<float,2>{1920,1080});
    put(wrapper+0x878,d2vr::rig::List{wrapper+0x900,2,512});
    assert(d2vr::native_menu_state(read,base,manager,wrapper));
    std::array<float,2> reset{};const std::array<std::uint32_t,2> reset_bits{0x7149f2ca,0x7149f2ca};
    std::memcpy(reset.data(),reset_bits.data(),sizeof(reset));put(wrapper+0xb08,reset);
    assert(d2vr::native_menu_state(read,base,manager,wrapper));
    reset[1]=0;put(wrapper+0xb08,reset);assert(!d2vr::native_menu_state(read,base,manager,wrapper));
    put(wrapper+0xb08,std::array<float,2>{1e20f,1e20f});assert(!d2vr::native_menu_state(read,base,manager,wrapper));
    put(wrapper+0xb08,std::array<float,2>{1920,1080});
    assert(!d2vr::native_menu_state(read,base,manager+8,wrapper));
    assert(!d2vr::native_menu_state(read,base,manager,wrapper+8));
    put(callback+8,manager+8);assert(!d2vr::native_menu_state(read,base,manager,wrapper));put(callback+8,manager);
    put(wrapper+0xb08,std::array<float,2>{NAN,1080});assert(!d2vr::native_menu_state(read,base,manager,wrapper));put(wrapper+0xb08,std::array<float,2>{1920,1080});
    auto recycled=[&](std::uintptr_t at,void* out,std::size_t size) {const bool ok=read(at,out,size);if(at==wrapper+0xb08) put(file+0x1618,player+8);return ok;};
    assert(!d2vr::native_menu_state(recycled,base,manager,wrapper));
    std::cout<<"PASS: native Iggy callback owner, wrapper alignment and bounds, finite cursor and recycled playback guards\n";
}
