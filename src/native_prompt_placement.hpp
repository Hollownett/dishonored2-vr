#pragma once
#include "native_prompt_entry.hpp"
#include "tracked_pose.hpp"

namespace d2vr {
// Pinned native 8780e9: testb $0x2,0xd0(%rcx); jz 878186 selects the
// world-cache branch. Other model paths require native transforms: use the
// entity's world origin rather than guessing those transforms here.
template<class Read> std::optional<Vec3> native_retained_prompt_anchor(Read& read,std::uintptr_t base,
        const native_prompt_entry::Receipt& receipt,bool character=false) {
    using native_prompt_entry::value;
    const auto entry=native_prompt_entry::refresh(read,base,receipt);
    if(!entry)return {};
    const auto flags=value<std::uint8_t>(read,entry->owner+0xd0);
    const auto model=value<std::uintptr_t>(read,entry->owner+0xf0);
    if(!flags || !model)return {};
    // NPC models are skinned; their cache is not the static world AABB above.
    // Restore the existing NPC prompt placement from its entity origin. Dead
    // and downed bodies (+d1 bits08/20, the existing downed-state policy)
    // use the low carry-label height. Native +Z maps to XR +Y.
    const auto character_flags=character?value<std::uint8_t>(read,entry->owner+0xd1):std::optional<std::uint8_t>{};
    if(character && !character_flags)return {};
    std::optional<Vec3> anchor;
    std::optional<std::uintptr_t> table;
    if(!character && !(*flags&2u) && native_prompt_entry::pointer(*model)) {
        table=value<std::uintptr_t>(read,*model);
        if(table && *table==base+0x1b0dd88) {
            const auto box=value<std::array<float,8>>(read,*model+0x1840);
            if(box) {
                const Vec3 minimum{(*box)[0],(*box)[1],(*box)[2]},maximum{(*box)[4],(*box)[5],(*box)[6]};
                if(finite(minimum) && finite(maximum) && minimum.x<=maximum.x && minimum.y<=maximum.y && minimum.z<=maximum.z)
                    anchor=minimum+(maximum-minimum)*.5;
            }
        }
    }
    if(!anchor) {
        const auto origin=value<std::array<float,3>>(read,entry->owner+0x300);
        if(origin) {const Vec3 point{(*origin)[0],(*origin)[1],(*origin)[2]};if(finite(point))anchor=point;}
    }
    if(anchor && character)anchor->z+=(*character_flags&0x28u)?.35:1.95;
    // Both the model branch and the retained HUD source must still identify
    // the values read above. Array compaction is permitted by refresh.
    if(!anchor || value<std::uint8_t>(read,entry->owner+0xd0)!=flags ||
       value<std::uintptr_t>(read,entry->owner+0xf0)!=model ||
       (character && value<std::uint8_t>(read,entry->owner+0xd1)!=character_flags) ||
       (table && value<std::uintptr_t>(read,*model)!=table) ||
       !native_prompt_entry::refresh(read,base,*entry))return {};
    return anchor;
}
} // namespace d2vr
