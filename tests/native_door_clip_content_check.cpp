#include "../src/native_door_clip_content.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
using Content=d2vr::NativeDoorClipContent;
struct Reader {
    static constexpr std::uintptr_t base=0x10000000,descriptor=0x20000000,buffer=0x30000000,alternate=0x30001000;
    std::array<unsigned char,0x58> object{};
    std::array<unsigned char,Content::resident_size> bytes{};
    unsigned calls{},payload_bytes{},fail_at{},throw_at{},seh_at{},change_at{};
    explicit Reader(bool ccw=true):bytes(ccw?Content::ccw_bytes:Content::cw_bytes) {
        put(0,base+Content::vtable_rva);put(0x38,1.f);put(0x48,buffer);put(0x50,std::uint32_t{Content::resident_size});
    }
    template<class Value> void put(std::size_t offset,const Value& value) {std::memcpy(object.data()+offset,&value,sizeof(value));}
    bool operator()(std::uintptr_t address,void* output,std::size_t size) {
        ++calls;
        if(calls==fail_at)return false;
        if(calls==throw_at)throw std::runtime_error("reader");
#if defined(_MSC_VER)
        if(calls==seh_at)RaiseException(0xe0421234,0,0,nullptr);
#endif
        if(address>=descriptor && address-descriptor<=object.size() && size<=object.size()-(address-descriptor))std::memcpy(output,object.data()+(address-descriptor),size);
        else if((address==buffer || address==alternate) && size==bytes.size()) {std::memcpy(output,bytes.data(),size);payload_bytes+=static_cast<unsigned>(size);}
        else return false;
        if(calls==change_at)put(0x48,alternate);
        return true;
    }
    std::optional<Content::ContentLease> capture(bool ccw=true) {return Content::capture(base,*this,descriptor,ccw);}
};
}
int main() {
    for(const bool ccw:{false,true}) {
        Reader read(ccw);const auto lease=read.capture(ccw);assert(lease && lease->descriptor==Reader::descriptor && lease->buffer==Reader::buffer);
        assert(lease->length==368 && lease->ccw==ccw && lease->duration==1.f && lease->frames==31 && lease->fps==29.999998092651367f);
        assert(read.calls==10 && read.payload_bytes==736 && Content::current(read,*lease));
        assert(!read.capture(!ccw));
        read.bytes[200]^=1;assert(lease->content[200]!=read.bytes[200] && !Content::current(read,*lease));
        read.bytes[200]^=1;read.put(0x48,Reader::alternate);assert(!Content::current(read,*lease));
        const auto moved=read.capture(ccw);assert(moved && moved->buffer==Reader::alternate);
        read.put(0x48,std::uintptr_t{});assert(!Content::current(read,*moved));
    }
    for(const bool ccw:{false,true})for(std::size_t index=0;index<Content::resident_size;++index) {
        Reader read(ccw);read.bytes[index]^=1;assert(!read.capture(ccw));
    }
    for(const std::uint32_t length:{0u,367u,369u,448u,UINT32_MAX}) {Reader read;read.put(0x50,length);assert(!read.capture());}
    for(const float duration:{0.f,-1.f,.999f,1.001f}) {Reader read;read.put(0x38,duration);assert(!read.capture());}
    for(const std::uint32_t duration:{0x7fc01234u,0x7f800000u}) {Reader read;read.put(0x38,duration);assert(!read.capture());}
    {
        Reader read;const auto lease=read.capture();assert(lease);read.put(0x38,2.f);assert(!Content::current(read,*lease));
        read.put(0x38,1.f);read.put(0,Reader::base+Content::vtable_rva+8);assert(!Content::current(read,*lease));
    }
    {
        Reader read;read.put(0,Reader::base+Content::vtable_rva+8);assert(!read.capture());
        assert(!Content::capture(0,read,Reader::descriptor,true));assert(!Content::capture(Reader::base,read,0,true));
    }
    for(unsigned failure=1;failure<=10;++failure) {
        Reader read;read.fail_at=failure;assert(!read.capture());
        Reader throwing;throwing.throw_at=failure;assert(!throwing.capture());
#if defined(_MSC_VER)
        Reader seh;seh.seh_at=failure;assert(!seh.capture());
#endif
    }
    {
        Reader read;read.change_at=5;assert(!read.capture());
        Reader stable;auto lease=stable.capture();assert(lease);lease->content[0]^=1;assert(!Content::current(stable,*lease));
    }
    std::cout<<"native_door_clip_content_check: PASS exact resident content, copied observation and fail-closed readers; no skeleton/pose/runtime claim\n";
}
