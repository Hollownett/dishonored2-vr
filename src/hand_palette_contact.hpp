#pragma once
#include "arm_tracking.hpp"
#include "hand_contact.hpp"

namespace d2vr {
class HandPaletteContact {
public:
    static std::optional<HandPaletteContact> bind(const rig::ArmBinding& binding) noexcept {
        try {
            HandPaletteContact result;
            result.wrist_=binding.chain.wrist;result.chains_=binding.finger_chains;
            if(result.wrist_>=69 || !rig::valid_hierarchy(binding.parents))return {};
            std::array<bool,69> used{};used[result.wrist_]=true;
            for(const auto& chain:result.chains_) {
                for(const auto joint:chain) {if(joint>=69 || used[joint] || !rig::in_subtree(binding.parents,joint,result.wrist_))return {};used[joint]=true;}
                if(binding.parents[chain[1]]!=static_cast<int>(chain[0]) || binding.parents[chain[2]]!=static_cast<int>(chain[1]))return {};
            }
            result.geometry_=rig::hand_geometry(binding.fingers.open,binding.parents,binding.finger_chains,binding.palm,result.wrist_,binding.bind_grip);
            result.axes_=palm_basis(result.geometry_);
            if(length(result.axes_.across)<.99 || length(result.axes_.normal)<.99 || length(result.axes_.wrist)<.99)return {};
            for(unsigned finger=0;finger<5;++finger) {
                const auto& geometry=result.geometry_.finger[finger];
                const auto tip=point(binding.bind_grip,geometry.pivot+geometry.bone[0]+geometry.bone[1]+geometry.bone[2]);
                const auto& distal=binding.fingers.open[result.chains_[finger][2]];
                const auto delta=tip-rig::position(distal);
                result.tips_[finger]={dot(delta,rig::column(distal,0)),dot(delta,rig::column(distal,1)),dot(delta,rig::column(distal,2))};
                if(!finite(result.tips_[finger]) || length(result.tips_[finger])<1e-6)return {};
            }
            return result;
        }catch(...) {return {};}
    }
    std::optional<HandProxies> proxies(const std::array<rig::Transform,69>& palette,const std::array<float,15>& model_frame) const noexcept {
        try {
            const rig::ModelFrame model(model_frame);
            if(model.scale[0]!=model.scale[1] || model.scale[0]!=model.scale[2] || !rig::valid(palette[wrist_]))return {};
            for(const auto& chain:chains_)for(const auto joint:chain)if(!rig::valid(palette[joint]))return {};
            std::array<rig::Transform,4> knuckles{};
            for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[chains_[finger][0]];
            const auto palm=rig::hand_bind_grip(palette[wrist_],knuckles);
            HandProxies result{};unsigned index{};
            for(const auto& sphere:hand_palm) {
                const auto local=axes_.across*sphere.across+axes_.normal*sphere.normal+axes_.wrist*sphere.wrist;
                result[index++]={model.world_point(point(palm,local)),sphere.radius*model.scale[0]};
            }
            for(unsigned finger=0;finger<5;++finger) {
                const auto& chain=chains_[finger];const auto radius=geometry_.thickness[finger]+(finger?hand_joint_add:hand_thumb_joint_add);
                const auto first_radius=finger?geometry_.thickness[finger]+hand_knuckle_add:hand_thumb_base_radius;
                for(unsigned segment=0;segment<3;++segment)
                    result[index++]={model.world_point(rig::position(palette[chain[segment]])),(segment?radius:first_radius)*model.scale[0]};
                const auto distal=rig::position(palette[chain[2]]),tip=point(palette[chain[2]],tips_[finger]);
                if(length(tip-distal)<1e-6)return {};
                result[index++]={model.world_point(tip-unit(tip-distal)*(radius*hand_tip_pull)),radius*model.scale[0]};
            }
            for(const auto& sphere:result)if(!finite(sphere.centre) || !std::isfinite(sphere.radius) || sphere.radius<=.0005 || sphere.radius>.15)return {};
            return result;
        }catch(...) {return {};}
    }
private:
    HandPaletteContact()=default;
    static Vec3 point(const rig::Transform& frame,Vec3 local) noexcept {
        return rig::position(frame)+rig::column(frame,0)*local.x+rig::column(frame,1)*local.y+rig::column(frame,2)*local.z;
    }
    rig::HandGeometry geometry_{};
    PalmBasis axes_{};
    rig::FingerChains chains_{};
    std::array<Vec3,5> tips_{};
    unsigned wrist_{};
};
}
