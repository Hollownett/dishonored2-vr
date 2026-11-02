#pragma once
// v780 (user, headset v779: "the elixirs are still hard to reach; put the health elixir on B"): the game's own quick use of a health elixir,
// where its keyboard R (action 35, 0x4de150) and the wheel's B ("ConsumeHealthPotion" callback 0x4f5fb0) both end:
// arkCpntHealthEvolved::ConsumeHealthPotion 0x7194a0 (checks again, removes one elixir, heals, posts the HUD message and the sound).
// The player's health component: GetComponent 0x877720(player, typeinfo base+0x39ae8e0, 2 = started). Game thread only.
#include <array>
#include <cstdint>

namespace d2vr {
struct NativeQuickHealth {
    enum Result : int {none,drank,cannot,empty,no_component,code_differs}; // cannot: dead, health full, or not allowed (0x718ce0)
    template<class Read> static Result consume(std::uintptr_t base,Read& read) noexcept {
        struct Pin {std::uintptr_t rva;std::array<unsigned char,12> bytes;};
        static constexpr std::array<Pin,4> pins{{
            {0x7194a0,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48,0x8b}},   // ConsumeHealthPotion
            {0x718ce0,{0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x48,0x8b}},   // CanConsumeHealthPotion (al)
            {0x719890,{0x40,0x53,0x48,0x83,0xec,0x20,0x83,0xb9,0xf0,0x00,0x00,0x00}},   // health elixirs count (eax)
            {0x877720,{0x48,0x89,0x5c,0x24,0x10,0x44,0x89,0x44,0x24,0x18,0x55,0x56}}}}; // GetComponent
        for(const auto& p:pins) {std::array<unsigned char,12> b{};if(!read(base+p.rva,b.data(),b.size()) || b!=p.bytes) return code_differs;}
        std::uintptr_t game{},player{};
        if(!read(base+0x39a5290,&game,8) || !game || !read(game+0x3130b0,&player,8) || !player) return no_component;
        const auto comp=reinterpret_cast<std::uintptr_t (__fastcall*)(std::uintptr_t,std::uintptr_t,int)>(base+0x877720)(player,base+0x39ae8e0,2);
        if(!comp) return no_component;
        if(reinterpret_cast<int (__fastcall*)(std::uintptr_t)>(base+0x719890)(comp)<=0) return empty;
        if(!reinterpret_cast<bool (__fastcall*)(std::uintptr_t)>(base+0x718ce0)(comp)) return cannot;
        reinterpret_cast<void (__fastcall*)(std::uintptr_t)>(base+0x7194a0)(comp);
        return drank;
    }
};
} // namespace d2vr
