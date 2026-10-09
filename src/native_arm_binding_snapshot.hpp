#pragma once
#include "arm_tracking.hpp"
#include <memory>
#include <atomic>

namespace d2vr::rig {
struct NativeArmBindingSource {
    std::uintptr_t names{},inverse{},hierarchy{};
    std::array<std::uint16_t,69> ids{};
    std::array<std::uintptr_t,69> texts{};
    std::array<std::string,69> labels{};
    std::optional<NativeBodyBind> body_bind;
};
inline bool operator==(const NativeArmBindingSource& a,const NativeArmBindingSource& b) {
    return a.names==b.names && a.inverse==b.inverse && a.hierarchy==b.hierarchy && a.ids==b.ids && a.texts==b.texts && a.labels==b.labels && a.body_bind==b.body_bind;
}
inline bool native_arm_pair_owned(const std::array<Arm,2>& arms) noexcept {
    if(arms[0].player!=arms[1].player || arms[0].body!=arms[1].body || arms[0].body_identity!=arms[1].body_identity ||
       arms[0].animation==arms[1].animation || arms[0].model==arms[1].model || arms[0].skeleton==arms[1].skeleton) return false;
    for(unsigned side=0;side<2;++side) {
        const auto& arm=arms[side];
        if(arm.side!=side || !pointer(arm.player) || !pointer(arm.body) || !pointer(arm.animation) ||
           !pointer(arm.model) || !pointer(arm.skeleton) || arm.body_identity.generation==-1 || arm.animation_identity.generation==-1) return false;
    }
    return true;
}
struct NativeArmBindingSnapshot {
    const std::array<Arm,2> arms;
    const std::array<ArmBinding,2> bindings;
    const std::array<NativeArmBindingSource,2> sources{};
    // Keeping this shared snapshot in the callback retains both exact bindings
    // while a checkpoint refresh publishes a replacement on another thread.
    std::optional<unsigned> side_for_model(std::uintptr_t model) const noexcept {
        for(unsigned side=0;side<2;++side) if(arms[side].model==model) return side;
        return {};
    }
};
class NativeArmBindingCache {
    std::shared_ptr<const NativeArmBindingSnapshot> published_;
public:
    std::shared_ptr<const NativeArmBindingSnapshot> load() const noexcept {
        return std::atomic_load_explicit(&published_,std::memory_order_acquire);
    }
    void clear() noexcept {
        std::atomic_store_explicit(&published_,std::shared_ptr<const NativeArmBindingSnapshot>{},std::memory_order_release);
    }
    // Discovery/binding are separate to exercise mid-refresh native lifetime
    // changes; production always uses the exact guarded native readers below.
    template<class Find,class Bind,class Capture,class Valid> bool refresh_from(Find& find,Bind& bind,Capture& capture,Valid& valid) {
        try {
            const auto arms=find();
            if(!arms || !native_arm_pair_owned(*arms)) {clear();return false;}
            const auto previous=load();
            const auto sources=capture(*arms,previous.get());
            if(!sources) {clear();return false;}
            if(previous && previous->arms==*arms && previous->sources==*sources && valid(*previous)) {
                const auto again=capture(*arms,previous.get());const auto current=find();
                if(again && *again==*sources && current && *current==*arms && valid(*previous)) return true;
                clear();return false;
            }
            std::array<ArmBinding,2> bindings{};
            for(unsigned side=0;side<2;++side) {
                const auto binding=bind((*arms)[side]);
                if(!binding) {clear();return false;}
                bindings[side]=*binding;
            }
            std::shared_ptr<const NativeArmBindingSnapshot> next=std::make_shared<NativeArmBindingSnapshot>(NativeArmBindingSnapshot{*arms,std::move(bindings),*sources});
            const auto again=capture(*arms,next.get());const auto current=find();
            if(!again || !(*again==*sources) || !current || *current!=*arms || !native_arm_pair_owned(*current) || !valid(*next)) {clear();return false;}
            std::atomic_store_explicit(&published_,std::move(next),std::memory_order_release);
            return true;
        } catch(...) {clear();return false;}
    }
    template<class Find,class Bind> bool refresh_from(Find& find,Bind& bind) {
        auto capture=[](const std::array<Arm,2>&,const NativeArmBindingSnapshot*) -> std::optional<std::array<NativeArmBindingSource,2>> {return std::array<NativeArmBindingSource,2>{};};
        auto valid=[](const NativeArmBindingSnapshot&) {return true;};
        return refresh_from(find,bind,capture,valid);
    }
    template<class Read> bool refresh(Read& read,std::uintptr_t base) {
        auto find=[&] {return player_arms(read,base);};
        auto bind=[&](const Arm& arm) {return arm_binding(read,base,arm);};
        auto capture=[&](const std::array<Arm,2>& arms,const NativeArmBindingSnapshot* previous) -> std::optional<std::array<NativeArmBindingSource,2>> {
            const auto manager=value<std::uintptr_t>(read,base+0x22995c0);
            if(manager!=base+0x22995d0 || value<std::uintptr_t>(read,*manager)!=base+0x1b093c8) return {};
            const auto entries=list(read,*manager+8,65535);if(!entries) return {};
            std::array<NativeArmBindingSource,2> result{};
            for(unsigned side=0;side<2;++side) {
                auto& source=result[side];const auto& arm=arms[side];
                source.body_bind=native_body_bind(read,base,arm);
                const auto names=value<std::uintptr_t>(read,arm.skeleton+0x70),inverse=value<std::uintptr_t>(read,arm.skeleton+0x120),hierarchy=value<std::uintptr_t>(read,arm.skeleton+0x158);
                if(!names || !inverse || !hierarchy || !pointer(*names) || !pointer(*inverse) || !pointer(*hierarchy)) return {};
                source.names=*names;source.inverse=*inverse;source.hierarchy=*hierarchy;
                if(!read(source.names,source.ids.data(),sizeof(source.ids))) return {};
                for(unsigned joint=0;joint<69;++joint) {
                    if(source.ids[joint]>=entries->packed_count) return {};
                    const auto text=value<std::uintptr_t>(read,entries->data+source.ids[joint]*0x48+0x10);
                    if(!text || *text<0x10000 || *text>=0x0000800000000000-128) return {};source.texts[joint]=*text;
                    bool reused=false;
                    if(previous && previous->arms[side]==arm) {
                        const auto& old=previous->sources[side];const auto& label=old.labels[joint];
                        std::array<char,128> buffer{};
                        if(old.ids[joint]==source.ids[joint] && old.texts[joint]==*text && !label.empty() && label.size()<buffer.size() &&
                           read(*text,buffer.data(),label.size()+1) && buffer[label.size()]=='\0' && std::memcmp(buffer.data(),label.data(),label.size())==0) {
                            source.labels[joint]=label;reused=true;
                        }
                    }
                    if(!reused) {const auto label=joint_name(read,base,source.ids[joint]);if(!label) return {};source.labels[joint]=*label;}
                }
            }
            return result;
        };
        auto valid=[&](const NativeArmBindingSnapshot& snapshot) {
            for(unsigned side=0;side<2;++side) {
                const auto& arm=snapshot.arms[side];const auto& source=snapshot.sources[side];
                if(value<std::uintptr_t>(read,arm.skeleton+0x70)!=source.names || value<std::uintptr_t>(read,arm.skeleton+0x120)!=source.inverse ||
                   value<std::uintptr_t>(read,arm.skeleton+0x158)!=source.hierarchy) return false;
                const auto parents=arm_hierarchy(read,base,snapshot.arms[side]);
                std::array<std::array<float,12>,69> inverse{};
                if(!parents || *parents!=snapshot.bindings[side].parents ||
                   !read(source.inverse,inverse.data(),sizeof(inverse)) || inverse!=snapshot.bindings[side].inverse ||
                   value<std::uintptr_t>(read,arm.skeleton+0x120)!=source.inverse) return false;
            }
            return true;
        };
        return refresh_from(find,bind,capture,valid);
    }
};
} // namespace d2vr::rig
