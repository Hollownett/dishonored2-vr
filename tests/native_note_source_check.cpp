#include "../src/native_note_source.hpp"
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
namespace r=d2vr::rig;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::function<void(std::uintptr_t)> before_read;
    template<class T> void put(std::uintptr_t at,const T& value) {
        const auto* source=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(T);++i)bytes[at+i]=source[i];
    }
    void list(std::uintptr_t at,std::uintptr_t data,unsigned count,unsigned capacity) {
        put(at,r::List{data,count,capacity<<8});
    }
    bool operator()(std::uintptr_t at,void* target,std::size_t size) {
        if(before_read)before_read(at);
        auto* out=static_cast<unsigned char*>(target);
        for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end())return false;out[i]=it->second;}
        return true;
    }
};
constexpr std::uintptr_t kAnimation=0x100000,kTransforms=0x200000,kStates=0x300000,kMapping=0x400000,kModel=0x500000;
Memory fixture(unsigned count) {
    Memory m;m.list(kAnimation+0x2d8,kTransforms,count+3,count+3);m.list(kAnimation+0x2e8,kStates,4,4);
    m.list(kStates+0x30,kMapping,count,count);
    for(unsigned i=0;i<count;++i)m.put(kMapping+i*2,static_cast<std::int16_t>(i+3));
    m.put(kModel+0x1fb2,std::int8_t{1});m.put(kModel+0x1fb3,std::int8_t{0});
    m.put(kModel+0x2000,std::uint32_t{0xa0000000u|((count+7u)&~7u)});
    m.put(kModel+0x2008,std::uintptr_t{0x600000});m.put(kModel+0x2040,std::uintptr_t{0x700000});
    return m;
}
int main() {
    for(const unsigned count:{20u,69u}) {
        auto m=fixture(count);assert(r::render_palette_source(m,kAnimation,count)==kTransforms+3*64);
        const auto buffers=r::native_skin_buffers(m,kModel,count);assert(buffers);
        assert((*buffers==std::array<std::uintptr_t,2>{0x700000,0x600000}));
        auto bad=m;bad.put(kMapping,std::int16_t{-1});assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;bad.put(kMapping+2,std::int16_t{5});assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;bad.list(kAnimation+0x2d8,kTransforms,count+2,count+3);assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;bad.list(kAnimation+0x2e8,kStates,3,4);assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;bad.list(kStates+0x30,kMapping,count-1,count);assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;unsigned hits=0;bad.before_read=[&](std::uintptr_t at) {
            if(at==kMapping && ++hits==2)bad.put(kMapping+2,std::int16_t{9});
        };assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;hits=0;bad.before_read=[&](std::uintptr_t at) {
            if(at==kAnimation+0x2d8 && ++hits==2)bad.list(kAnimation+0x2d8,kTransforms+0x1000,count+3,count+3);
        };assert(!r::render_palette_source(bad,kAnimation,count));
        bad=m;bad.put(kModel+0x1fb2,std::int8_t{3});assert(!r::native_skin_buffers(bad,kModel,count));
        bad=m;bad.put(kModel+0x1fb3,std::int8_t{-1});assert(!r::native_skin_buffers(bad,kModel,count));
        bad=m;bad.put(kModel+0x2000,std::uint32_t{count});assert(!r::native_skin_buffers(bad,kModel,count));
        bad=m;bad.put(kModel+0x2040,std::uintptr_t{});assert(!r::native_skin_buffers(bad,kModel,count));
        bad=m;hits=0;bad.before_read=[&](std::uintptr_t at) {
            if(at==kModel+0x2040 && ++hits==2)bad.put(kModel+0x2040,std::uintptr_t{0x800000});
        };assert(!r::native_skin_buffers(bad,kModel,count));
        bad=m;hits=0;bad.before_read=[&](std::uintptr_t at) {
            if(at==kModel+0x1fb2 && ++hits==2)bad.put(kModel+0x1fb2,std::int8_t{0});
        };assert(!r::native_skin_buffers(bad,kModel,count));
    }
    auto m=fixture(20);assert(!r::render_palette_source(m,kAnimation,21));assert(!r::native_skin_buffers(m,kModel,21));
    assert(!r::render_palette_source(m,0,20));assert(!r::native_skin_buffers(m,0,20));
    std::cout<<"PASS: 20/69 render sources, contiguous range and route stability, padded skin count and buffer/index replacement rejection\n";
}
