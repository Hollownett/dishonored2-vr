#pragma once
#include "native_door_owner.hpp"
#include "native_door_clip_content.hpp"

namespace d2vr {
class NativeDoorClipSelection {
public:
    struct Context {
        std::uintptr_t holder{},arena{},clips{},record{},descriptor{};
        std::uint32_t packed_count{},packed_capacity{};
        unsigned char index{};
        bool operator==(const Context& other) const noexcept {
            return holder==other.holder && arena==other.arena && clips==other.clips && record==other.record && descriptor==other.descriptor &&
                packed_count==other.packed_count && packed_capacity==other.packed_capacity && index==other.index;
        }
    };
    struct Lease {
        NativeDoorOwner::Snapshot owner{};
        NativeDoorClipContent::ContentLease content{};
        Context context{};
        bool operator==(const Lease& other) const noexcept {return owner==other.owner && content==other.content && context==other.context;}
    };
    template<class Read> static std::optional<Lease> capture(Read& read,const NativeDoorOwner::Snapshot& owner,bool ccw) noexcept {
        if(!NativeDoorOwner::current(read,owner))return {};
        Context first{},second{};
        if(!inspect(read,owner,ccw,first))return {};
        const auto content=NativeDoorClipContent::capture(owner.base,read,first.descriptor,ccw);
        if(!content || !NativeDoorOwner::current(read,owner) || !inspect(read,owner,ccw,second) || !(first==second))return {};
        return Lease{owner,*content,first};
    }
    template<class Read> static bool current(Read& read,const Lease& lease) noexcept {
        const auto next=capture(read,lease.owner,lease.content.ccw);return next && *next==lease;
    }
private:
    template<class Read> static bool cpp_read(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        try {return read(address,output,size);}catch(...) {return false;}
    }
    template<class Read> static bool safe_read(Read& read,std::uintptr_t address,void* output,std::size_t size) noexcept {
        if(address<0x10000 || address>=0x0000800000000000ull-size)return false;
#if defined(_MSC_VER)
        __try {return cpp_read(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
        return cpp_read(read,address,output,size);
#endif
    }
    template<class Read,class Value> static bool get(Read& read,std::uintptr_t address,Value& value) noexcept {return safe_read(read,address,&value,sizeof(value));}
    template<class Read> static bool inspect(Read& read,const NativeDoorOwner::Snapshot& owner,bool ccw,Context& result) noexcept {
        std::uintptr_t schema{},time{},events{};rig::List clips{};
        if(!get(read,owner.container,result.holder) || !rig::pointer(result.holder) || !get(read,owner.container+8,schema) || schema!=owner.schema ||
           !get(read,result.holder,result.arena) || !rig::pointer(result.arena) || owner.context_offset>owner.arena_size ||
           owner.arena_size-owner.context_offset<0xb78 || result.arena>=0x0000800000000000ull-owner.arena_size)return false;
        const auto context=result.arena+owner.context_offset;
        if(!get(read,context+0x10,time) || time!=owner.container+0x38 || !get(read,context+0x18,events) || events!=owner.container+0x20 ||
           !get(read,context+0xa78,clips))return false;
        const auto count=clips.packed_count&0xffffffu,capacity=clips.packed_capacity>>8;
        result.index=owner.clips[ccw?1:0];result.packed_count=clips.packed_count;result.packed_capacity=clips.packed_capacity;
        if(!rig::pointer(clips.data) || !count || count>128 || count>capacity || result.index>=count ||
           clips.data>=0x0000800000000000ull-static_cast<std::uintptr_t>(count)*0x48)return false;
        result.clips=clips.data;result.record=clips.data+static_cast<std::uintptr_t>(result.index)*0x48;
        return get(read,result.record+0x30,result.descriptor) && rig::pointer(result.descriptor);
    }
};
}
