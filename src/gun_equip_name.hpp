#pragma once
// Conservative equip-pose routing for barrel aim, plus a broad diagnostic decl matcher.
#include <cctype>
#include <cstdint>
#include <string>
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
// Restore the v612 barrel-routing rule; the broader matcher below is diagnostic only.
inline bool is_barrel_equip_name(std::string_view name) noexcept {
    return name=="Gun" || name.find("rossbow")!=name.npos;
}
// Any slot/model/equip change invalidates the old aim filter on its owning render thread.
class AimEquipTracker {
    std::uintptr_t item_{},model_{};std::string name_;
    std::uint64_t revision_{};
public:
    bool update(std::uintptr_t item,std::uintptr_t model,std::string_view name) {
        if(item==item_ && model==model_ && name==name_) return false;
        item_=item;model_=model;name_=name;++revision_;return true;
    }
    std::uint64_t revision() const {return revision_;}
};
inline bool is_gun_equip_name(std::string_view name) noexcept {
    return contains_nocase(name,"gun") || contains_nocase(name,"pistol") || contains_nocase(name,"crossbow");
}
// A gadget the left hand throws or places (grenades, stun mines, spring razors): the fist holds it, opening the hand lets it go.
inline bool is_gadget_equip_name(std::string_view name) noexcept {
    return contains_nocase(name,"grenade") || contains_nocase(name,"mine") || contains_nocase(name,"razor");
}
} // namespace d2vr
