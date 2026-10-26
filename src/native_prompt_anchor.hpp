#pragma once
#include "interaction_ray.hpp"
namespace d2vr {
enum class NativePromptAnchor {none,interaction,takedown,unbound};
// A captured kill/choke glyph belongs to its qualified NPC, even when a hand
// simultaneously hovers a pickup. Never move art with an identity onto another target.
inline NativePromptAnchor native_prompt_anchor(NativeInteractionTargetIdentity captured,
        NativeInteractionTargetIdentity interaction,NativeInteractionTargetIdentity takedown) noexcept {
    if(captured) {
        if(takedown && captured==takedown) return NativePromptAnchor::takedown;
        if(interaction && captured==interaction) return NativePromptAnchor::interaction;
        return NativePromptAnchor::none;
    }
    if(interaction) return NativePromptAnchor::interaction;
    if(takedown) return NativePromptAnchor::takedown;
    return NativePromptAnchor::unbound;
}
}
