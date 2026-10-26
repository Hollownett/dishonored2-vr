#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr::native_prompt_entry {
struct Receipt {
    std::uintptr_t element{},entry{},owner{};
    std::uint32_t key{},spawn{};
};
// The native removal pass compacts this array. A relocated record is still
// the same source; a reused key, entity address or spawn is not.
inline bool same_source(const Receipt& a,const Receipt& b) noexcept {
    return a.element==b.element && a.key==b.key && a.owner==b.owner && a.spawn==b.spawn;
}
// Steam17576867 executable SHA256 b671f824...6180. Add 38f7d0 loads
// element+80 / low24(element+88), stride40; argument0 comes from entry+0.
// After its Add call, 38f972 resolves entry+3c through 6dc560 -> 7b5e00.
// These are retained native entry/entity receipts, never a latest hover query.
template<class Read> bool verified_abi(Read& read,std::uintptr_t base) {
    struct Region {std::uintptr_t rva;std::size_t size;std::uint64_t hash;};
    constexpr Region regions[]{
        {0x38f7d0,557,0x7dde7a197e64b528ull},
        {0x38f690,309,0x43ee4b153264f007ull},
        // 39040f..41e: an unrefreshed active4 becomes removal8. 3904ba..582
        // removes it and moves the final record into its slot.
        {0x3903b0,869,0xf45c8ec5bcbf0104ull},
        {0x28700,54,0x57609a1b3f48ef70ull},
        {0x6dc560,37,0xf3c251b8736d7cf9ull},
        {0x7b5e00,42,0x9c47ab4bb4a5a6fdull},
    };
    for(const auto& region:regions) {
        std::array<unsigned char,869> bytes{};
        if(!read(base+region.rva,bytes.data(),region.size))return false;
        std::uint64_t hash=14695981039346656037ull;
        for(std::size_t i=0;i<region.size;++i)hash=(hash^bytes[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    return true;
}
template<class T,class Read> std::optional<T> value(Read& read,std::uintptr_t address) {
    T out{};if(!read(address,&out,sizeof(out)))return {};return out;
}
inline bool pointer(std::uintptr_t address) {return address>=0x10000 && address<0x0000800000000000ull;}
enum class State {pending,active,pending_or_active};
template<class Read> std::optional<Receipt> resolve_key(Read& read,std::uintptr_t base,
        std::uintptr_t element,std::uint32_t key,State state) {
    if(!pointer(element))return {};
    const auto entries=value<std::uintptr_t>(read,element+0x80);
    const auto packed=value<std::uint32_t>(read,element+0x88);
    if(!entries || !pointer(*entries) || !packed)return {};
    const unsigned length=*packed&0xffffffu;
    if(!length || length>256)return {};
    std::optional<Receipt> result;std::uintptr_t selected_game{};
    std::array<unsigned char,0x40> selected{};
    for(unsigned i=0;i<length;++i) {
        std::array<unsigned char,0x40> bytes{};
        const auto entry=*entries+i*0x40ull;
        if(!read(entry,bytes.data(),bytes.size()))return {};
        std::uint32_t candidate{},spawn{};std::memcpy(&candidate,bytes.data(),4);std::memcpy(&spawn,bytes.data()+0x3c,4);
        if(candidate!=key)continue;
        // Reject duplicate keys even when only one entry is pending/visible.
        if(result)return {};
        const unsigned required=state==State::pending?1u:state==State::active?4u:5u;
        if(!(bytes[0x3a]&required) || (bytes[0x3a]&8u) || spawn==0x3fff0010u || (spawn>>16)>=0x3fff)return {};
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !pointer(*game))return {};
        const auto owner=value<std::uintptr_t>(read,*game+0x3130b0+(spawn>>16)*8ull);
        if(!owner || !pointer(*owner) || value<std::uint32_t>(read,*owner+0xa8)!=std::optional<std::uint32_t>{spawn})return {};
        result=Receipt{element,entry,*owner,key,spawn};selected=bytes;selected_game=*game;
    }
    if(!result || value<std::uintptr_t>(read,element+0x80)!=entries || value<std::uint32_t>(read,element+0x88)!=packed)return {};
    std::array<unsigned char,0x40> again{};
    if(!read(result->entry,again.data(),again.size()) || again!=selected ||
       value<std::uintptr_t>(read,base+0x39a5290)!=std::optional<std::uintptr_t>{selected_game} ||
       value<std::uintptr_t>(read,selected_game+0x3130b0+(result->spawn>>16)*8ull)!=std::optional<std::uintptr_t>{result->owner} ||
       value<std::uint32_t>(read,result->owner+0xa8)!=std::optional<std::uint32_t>{result->spawn})return {};
    return result;
}
// Does not consult or change hand selection. The entity's spawn generation
// and the still-retained entry own this artwork for its native lifetime.
// Pending is allowed only while retaining an Add event; Draw requires active4.
template<class Read> std::optional<Receipt> refresh(Read& read,std::uintptr_t base,
        const Receipt& receipt,bool require_active=true) {
    const auto current=resolve_key(read,base,receipt.element,receipt.key,require_active?State::active:State::pending_or_active);
    return current && same_source(*current,receipt)?current:std::nullopt;
}
// Invoke on the existing validated native UI callback, after successful 28a90
// and BEFORE returning to Add38f963/Gauge38f743. Caller must first verify ABI,
// movie/method ownership with native_prompt_call_owner. Unknown/ambiguous
// entries cannot authorize a world target. A Receipt identifies an ENTITY;
// a consumer must separately validate component generation/epoch if joining
// it to an existing component identity.
template<class Read> std::optional<std::uint32_t> event_key(Read& read,std::uintptr_t caller,std::uintptr_t block) {
    const unsigned argc=caller==0x38f963?6u:caller==0x38f743?2u:caller==0x3906a9?1u:0u;
    if(!argc || !pointer(block))return {};
    const auto count=value<std::uint32_t>(read,block+0x14),capacity=value<std::uint32_t>(read,block+0x10);
    const auto slots=value<std::uintptr_t>(read,block);
    if(!count || *count!=argc || !capacity || *capacity<argc || *capacity>16 || !slots || !pointer(*slots))return {};
    const auto type=value<std::uint32_t>(read,*slots);
    const auto number=value<double>(read,*slots+0x10);
    if(!type || *type!=4 || !number || !std::isfinite(*number) || *number<0 || *number>2147483647. || std::floor(*number)!=*number)return {};
    return static_cast<std::uint32_t>(*number);
}
template<class Read> std::optional<Receipt> resolve(Read& read,std::uintptr_t base,
        std::uintptr_t element,std::uintptr_t caller,std::uintptr_t block) {
    if(caller!=0x38f963 && caller!=0x38f743)return {};
    const auto key=event_key(read,caller,block);
    return key?resolve_key(read,base,element,*key,caller==0x38f963?State::pending:State::active):std::nullopt;
}
}
