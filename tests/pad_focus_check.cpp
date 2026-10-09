#include "../src/pad_focus.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <iostream>

struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    template<class T> void put(std::uintptr_t at,const T& value) {
        for(std::size_t i=0;i<sizeof(T);++i) bytes[at+i]=reinterpret_cast<const unsigned char*>(&value)[i];
    }
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {auto p=bytes.find(at+i);if(p==bytes.end()) return false;static_cast<unsigned char*>(out)[i]=p->second;}
        return true;
    }
};
int main() {
    constexpr std::uintptr_t base=0x140000000,root=base+0x3134140;
    Memory m;m.put(root,base+0x1ae06d8);m.put(root+8,static_cast<unsigned char>(0));
    m.put(root+0x600,static_cast<unsigned char>(0));m.put(root+0x5f8,1);m.put(root+0x5e8,std::uintptr_t{0x120});
    assert(d2vr::native_pad_root(m,base,root));assert(!d2vr::native_pad_root(m,base,root+8));
    d2vr::PadFocusLease lease;assert(!lease.allows(0xe1098,true,100));assert(lease.publish(true,100));
    assert(lease.allows(0xe1098,true,100) && lease.allows(0xe0146,true,350));
    assert(lease.allows_serializer_window(0x3a4ab,true,100));
    assert(!lease.allows_serializer_window(0xe1098,true,100));
    assert(!lease.allows_serializer_window(0x3a4ab,false,100));
    assert(!lease.allows_serializer_window(0x3a4ab,true,351));
    assert(!lease.allows(0xe1098,true,351) && !lease.allows(0xe1098,true,99));
    assert(!lease.allows(0xe1098,false,100) && !lease.allows(0x21f2c0,true,100));
    assert(!lease.publish(true,200));assert(lease.publish(true,1000));
    lease.publish(false,1001);assert(!lease.allows(0xe0146,true,1001));
    assert(!lease.allows_serializer_window(0x3a4ab,true,1001));
    m.put(root+0x600,static_cast<unsigned char>(1));assert(!d2vr::native_pad_root(m,base,root));
    m.put(root+0x600,static_cast<unsigned char>(0));m.put(root+0x5f8,17);assert(!d2vr::native_pad_root(m,base,root));
    m.put(root+0x5f8,1);m.put(root+8,static_cast<unsigned char>(2));assert(!d2vr::native_pad_root(m,base,root));
    std::cout<<"PASS: owned native joystick root and focused lease, poll/serializer scope only, stale/focus release and invalid state rejection; no OS focus changes\n";
}
