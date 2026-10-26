#pragma once
#include "interaction_ray.hpp"
#include "native_prompt_entry.hpp"
namespace d2vr {
enum class NativePromptAnchor {none,interaction,takedown,unbound,retained};
// A captured kill/choke glyph belongs to its qualified NPC, even when a hand
// simultaneously hovers a pickup. Never move art with an identity onto another target.
inline NativePromptAnchor native_prompt_anchor(NativeInteractionTargetIdentity captured,
        NativeInteractionTargetIdentity interaction,NativeInteractionTargetIdentity takedown,
        std::optional<native_prompt_entry::Receipt> retained_entry={}) noexcept {
    // The native entry/entity is its own source. Its consumer must revalidate
    // the retained source before reading its anchor; no hover can replace it.
    if(retained_entry)return NativePromptAnchor::retained;
    if(captured) {
        if(takedown && captured==takedown) return NativePromptAnchor::takedown;
        if(interaction && captured==interaction) return NativePromptAnchor::interaction;
        return NativePromptAnchor::none;
    }
    // A sticky root or an in-flight event has no target receipt. Its native
    // pixels may remain visible, but may never inherit an unrelated live target.
    return NativePromptAnchor::unbound;
}
}
