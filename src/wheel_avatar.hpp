#pragma once
#include "rig_transform.hpp"

namespace d2vr::rig {
// v776 (headset v775: "the hands must still be rendered and tracked while the wheel is open"): the dual-wield hand wheel is drawn at its hand
// and keeps the avatar (the solver poses the body and both arms as in gameplay, a held item follows the hand); the head-front wheel still hides it.
inline bool wheel_hides_avatar(unsigned logic,bool hand_wheel) noexcept {return logic==256 && !hand_wheel;}
// The logics whose tracked arms are drawn: gameplay, and the hand wheel (attachment policy, engine arm hides skipped).
inline bool wheel_tracks_arms(unsigned logic,bool hand_wheel) noexcept {return logic==2 || (logic==256 && hand_wheel);}
// The verified skinning upload accepts caller-owned model-space transforms.
// Move only that render copy out of the wheel's view; native animation, actor
// transforms, materials and other characters retain their original values.
template<std::size_t N>
bool hide_wheel_avatar(std::array<Transform,N>& copy,const std::array<float,15>& frame,
                       unsigned logic,bool owned) noexcept {
    if(logic!=256 || !owned || (N!=20 && N!=69)) return false;
    try {
        const ModelFrame model(frame);
        const auto below=model.model_point(model.origin-Vec3{0,0,100});
        auto result=copy;
        for(auto& joint:result) {
            if(!valid(joint)) return false;
            joint.f[12]+=static_cast<float>(below.x);
            joint.f[13]+=static_cast<float>(below.y);
            joint.f[14]+=static_cast<float>(below.z);
            if(!valid(joint)) return false;
        }
        copy=result;return true;
    } catch(...) {return false;}
}
} // namespace d2vr::rig
