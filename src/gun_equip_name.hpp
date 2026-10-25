#pragma once
// Which left-hand equip pose / decl names mean a drawn pistol or crossbow (the crosshair and the shots then follow the gun's barrel).
// The match is a case-insensitive substring, the same families the hand-collision shape uses ("Gun", "Pistol", "Crossbow"), so a
// differently spelled name after a weapon swap ("GunEquip", "pistol_draw", "CrossBow") does not silently drop the barrel aim.
#include <cctype>
#include <string_view>

namespace d2vr {
inline bool contains_nocase(std::string_view text,std::string_view needle) noexcept {
    if(needle.empty() || text.size()<needle.size()) return false;
    for(std::size_t i=0;i+needle.size()<=text.size();++i) {
        std::size_t j=0;
        while(j<needle.size() && std::tolower(static_cast<unsigned char>(text[i+j]))==std::tolower(static_cast<unsigned char>(needle[j]))) ++j;
        if(j==needle.size()) return true;
    }
    return false;
}
inline bool is_gun_equip_name(std::string_view name) noexcept {
    return contains_nocase(name,"gun") || contains_nocase(name,"pistol") || contains_nocase(name,"crossbow");
}
} // namespace d2vr
