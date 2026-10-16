#pragma once
#include "arm_tracking.hpp"

namespace d2vr::rig {
// Exact 0x541270 -> 0x25efe0 contract. This helper never writes engine memory,
// changes visibility/materials, or installs a hook. Its copy lives through the
// caller's single synchronous original upload call.
struct BodyUpload {
    std::array<Transform,20> joints{};
    std::array<float,15> model_frame{};
    std::int16_t first_transform{};
};

struct BodyMaterialRoute {
    std::uintptr_t draw_records{},overrides{};
    std::uint32_t draw_count{},override_count{};
    std::uint8_t model_flags{};
    std::vector<std::uintptr_t> draw_materials,override_materials;
};
// Read-only inventory of the owned model-local material route. No material type
// ABI or mutation is inferred from these pointers.
template<class Read> std::optional<BodyMaterialRoute> body_material_route(Read& read,
    std::uintptr_t base,const Arm& owned_arm,const NativeBodyBind& binding) {
    const auto current=native_body_bind(read,base,owned_arm);
    if(!current || !(*current==binding)) return {};
    const auto record_list=value<std::uintptr_t>(read,binding.model+0x1c00);
    const auto flags=value<std::uint8_t>(read,binding.model+0x1c30);
    if(!record_list || !pointer(*record_list) || !flags) return {};
    const auto records=list(read,*record_list,64),overrides=list(read,binding.model+0x18a0,64);
    if(!records || !overrides) return {};
    BodyMaterialRoute result{records->data,overrides->data,records->packed_count,
        overrides->packed_count,*flags,{},{}};
    for(unsigned i=0;i<records->packed_count;++i) {
        const auto material=value<std::uintptr_t>(read,records->data+i*0x80);
        if(!material || !pointer(*material)) return {};
        result.draw_materials.push_back(*material);
    }
    for(unsigned i=0;i<overrides->packed_count;++i) {
        const auto material=value<std::uintptr_t>(read,overrides->data+i*8);
        if(!material || (*material && !pointer(*material))) return {};
        result.override_materials.push_back(*material);
    }
    const auto after=native_body_bind(read,base,owned_arm);
    const auto records_after=list(read,*record_list,64),overrides_after=list(read,binding.model+0x18a0,64);
    if(!after || !(*after==binding) || value<std::uintptr_t>(read,binding.model+0x1c00)!=record_list ||
       value<std::uint8_t>(read,binding.model+0x1c30)!=flags || !records_after || !overrides_after ||
       records_after->data!=records->data || records_after->packed_count!=records->packed_count ||
       overrides_after->data!=overrides->data || overrides_after->packed_count!=overrides->packed_count) return {};
    for(unsigned i=0;i<records->packed_count;++i)
        if(value<std::uintptr_t>(read,records->data+i*0x80)!=result.draw_materials[i]) return {};
    for(unsigned i=0;i<overrides->packed_count;++i)
        if(value<std::uintptr_t>(read,overrides->data+i*8)!=result.override_materials[i]) return {};
    return result;
}

template<class Read> std::optional<BodyUpload> body_upload(Read& read,std::uintptr_t base,
    const Arm& owned_arm,const NativeBodyBind& binding,std::uintptr_t model,
    std::uintptr_t source,std::uint32_t caller_count) {
    if(caller_count!=20 || model!=binding.model || !pointer(source)) return {};
    const auto current=native_body_bind(read,base,owned_arm);
    if(!current || !(*current==binding)) return {};
    const auto transforms=list(read,binding.animation+0x2d8,4096);
    const auto states=list(read,binding.animation+0x2e8,4);
    if(!transforms || !states || states->packed_count!=4) return {};
    const auto mapping=list(read,states->data+0x30,4096);
    if(!mapping || mapping->packed_count!=20) return {};
    std::array<std::int16_t,20> indices{};
    if(!read(mapping->data,indices.data(),sizeof(indices)) || indices[0]<0) return {};
    for(unsigned i=0;i<20;++i) if(indices[i]!=indices[0]+static_cast<int>(i) ||
        static_cast<unsigned>(indices[i])>=transforms->packed_count) return {};
    if(source!=transforms->data+static_cast<std::uintptr_t>(indices[0])*64) return {};
    BodyUpload result{};result.first_transform=indices[0];
    if(!read(source,result.joints.data(),sizeof(result.joints)) ||
       !read(model+0x1860,result.model_frame.data(),sizeof(result.model_frame))) return {};
    for(const auto& joint:result.joints) if(!valid(joint)) return {};
    try {(void)ModelFrame(result.model_frame);} catch(const std::invalid_argument&) {return {};}
    const auto after=native_body_bind(read,base,owned_arm);
    const auto again_transforms=list(read,binding.animation+0x2d8,4096);
    const auto again_states=list(read,binding.animation+0x2e8,4);
    const auto again_mapping=list(read,states->data+0x30,4096);
    std::array<std::int16_t,20> again{};
    if(!after || !(*after==binding) || !again_transforms || !again_states || !again_mapping ||
       again_transforms->data!=transforms->data || again_transforms->packed_count!=transforms->packed_count ||
       again_states->data!=states->data || again_states->packed_count!=states->packed_count ||
       again_mapping->data!=mapping->data || again_mapping->packed_count!=mapping->packed_count ||
       !read(mapping->data,again.data(),sizeof(again)) || again!=indices) return {};
    return result;
}

template<class Read> std::optional<std::string> body_resource_name(Read& read,std::uintptr_t text) {
    if(!pointer(text)) return {};
    std::string name;
    for(unsigned i=0;i<256;++i) {
        const auto c=value<std::uint8_t>(read,text+i);if(!c) return {};
        if(!*c) return name;
        if(*c<32 || *c>126) return {};
        name.push_back(static_cast<char>(*c));
    }
    return {};
}

// The source is an already loaded material in this player's owned arm draw
// records, not a global material search or a newly constructed game object.
template<class Read> std::optional<std::uintptr_t> body_material_candidate(Read& read,
    std::uintptr_t base,const Arm& owned_arm,const NativeBodyBind& binding) {
    const auto body=native_body_bind(read,base,owned_arm);
    const auto arms=player_arms(read,base);
    if(!body || !(*body==binding) || !arms) return {};
    const auto source=value<std::uintptr_t>(read,binding.descriptor+8);
    const auto source_name=source?body_resource_name(read,*source):std::nullopt;
    if(!source_name) return {};
    const bool corvo=*source_name=="models/characters/player/corvo/body.md6";
    const bool emily=*source_name=="models/characters/player/emily/body.md6";
    if(!corvo && !emily) return {};
    for(const auto& arm:*arms) {
        const auto records_at=value<std::uintptr_t>(read,arm.model+0x1c00);
        const auto records=records_at && pointer(*records_at)?list(read,*records_at,64):std::nullopt;
        if(!records) continue;
        for(unsigned i=0;i<records->packed_count;++i) {
            const auto material=value<std::uintptr_t>(read,records->data+i*0x80);
            const auto text=material && pointer(*material)?value<std::uintptr_t>(read,*material+8):std::nullopt;
            const auto name=text?body_resource_name(read,*text):std::nullopt;
            if(!name) continue;
            const bool match=corvo?*name=="models/characters/player/corvo/textures/player_corvo_body.material":
                (*name=="models/characters/female/emily/textures/emily_body.material" ||
                 *name=="models/characters/player/emily/textures/player_emily_body.material");
            if(!match) continue;
            const auto after=native_body_bind(read,base,owned_arm);
            const auto again=player_arms(read,base);
            if(!after || !(*after==binding) || !again || !((*again)[arm.side]==arm) ||
               value<std::uintptr_t>(read,binding.descriptor+8)!=source ||
               body_resource_name(read,*source)!=source_name ||
               value<std::uintptr_t>(read,arm.model+0x1c00)!=records_at ||
               value<std::uintptr_t>(read,records->data+i*0x80)!=material ||
               value<std::uintptr_t>(read,*material+8)!=text || body_resource_name(read,*text)!=name) return {};
            return material;
        }
    }
    return {};
}

// Only call from the exact owned native upload/renderer scope. SetAll is native
// vtable+0x58 (0x2367b0); Refresh is 0x235990 (rebuild + bounds). No destructor
// invokes game code. Restore uses the same validated scope and preserves foreign edits.
class BodyMaterialOverride {
    NativeBodyBind owner_{};
    std::uintptr_t original_{},replacement_{},original_draw_{};
    // The textured body material is a loaded resource that outlives weapon swaps and
    // reloads; the arm draw records it is first found in do not. Remember it.
    std::uintptr_t remembered_{};
    bool active_{},verified_{},restoring_{};
    bool original_drawable_{};
    std::uint64_t refresh_tick_{},refreshes_{},refresh_deferred_{};
public:
    enum class Reason {ready,scope_or_abi,route_or_material,owner_or_override,visibility,refresh_failed,retry_deferred,foreign_restore};
private:
    Reason reason_=Reason::ready;
    template<class Read> static bool abi(Read& read,std::uintptr_t base) {
        return value<std::uintptr_t>(read,base+0x1b0d628+0x58)==base+0x2367b0 &&
            value<std::uintptr_t>(read,base+0x1b0d628+0xb8)==base+0x25ed30 &&
            value<std::uintptr_t>(read,base+0x1b0d628+0x78)==base+0x25f3c0;
    }
    // The body model carries several override slots (4 observed live, all the hidden material);
    // SetAll writes every slot, so all must agree before and after.
    static bool all_equal(const std::vector<std::uintptr_t>& v,std::uintptr_t value) {
        for(auto m:v) if(m!=value) return false;
        return true;
    }
public:
    // Same character only: the remembered material must still name this body's texture.
    template<class Read> static bool still_body_material(Read& read,std::uintptr_t material,const NativeBodyBind& owner) {
        const auto source=value<std::uintptr_t>(read,owner.descriptor+8);
        const auto body=source?body_resource_name(read,*source):std::nullopt;
        const auto text=value<std::uintptr_t>(read,material+8);
        const auto name=text?body_resource_name(read,*text):std::nullopt;
        if(!body || !name) return false;
        if(*body=="models/characters/player/corvo/body.md6") return *name=="models/characters/player/corvo/textures/player_corvo_body.material";
        return *body=="models/characters/player/emily/body.md6" &&
            (*name=="models/characters/female/emily/textures/emily_body.material" || *name=="models/characters/player/emily/textures/player_emily_body.material");
    }
    Reason reason() const noexcept {return reason_;}
    std::uint64_t refreshes() const noexcept {return refreshes_;}
    std::uint64_t refresh_deferred() const noexcept {return refresh_deferred_;}
    bool active() const noexcept {return active_ && verified_;}
    bool restoration_pending() const noexcept {return active_;}
    std::optional<bool> original_drawable(const NativeBodyBind& owner) const {
        return owns(owner)?std::optional<bool>(original_drawable_):std::nullopt;
    }
    bool owns(const NativeBodyBind& owner) const noexcept {return active_ && owner_==owner;}
    // Forget only after the integrator proves the old model owner has retired.
    // This performs no write through a recycled model address.
    void retire(const NativeBodyBind& owner) noexcept {
        if(owner_==owner) {active_=verified_=restoring_=false;owner_={};original_=replacement_=original_draw_=0;refresh_tick_=0;}
    }
    template<class Read,class SetAll,class Refresh> bool apply(Read& read,std::uintptr_t base,
        const Arm& arm,const NativeBodyBind& owner,bool owned_render_scope,SetAll set_all,Refresh refresh,std::uint64_t tick=0) {
        reason_=Reason::ready;
        if(!owned_render_scope || !abi(read,base)) {reason_=Reason::scope_or_abi;return false;}
        if(active_ && !(owner_==owner)) {reason_=Reason::owner_or_override;return false;}
        const auto route=body_material_route(read,base,arm,owner);
        auto material=active_?(still_body_material(read,replacement_,owner)?std::optional<std::uintptr_t>(replacement_):std::nullopt):
            body_material_candidate(read,base,arm,owner);
        if(material) remembered_=*material;
        else if(!active_ && remembered_ && still_body_material(read,remembered_,owner)) material=remembered_;
        if(!route || !material || route->draw_count!=1) {reason_=Reason::route_or_material;return false;}
        if(active_) {
            // A pending restore (e.g. avatar hidden while the weapon wheel is open) is cancelled by
            // re-applying: it used to latch and keep the body invisible for the rest of the session.
            restoring_=false;
            if(!(owner_==owner) || *material!=replacement_ || route->override_count<1) {reason_=Reason::owner_or_override;return false;}
            if(!all_equal(route->override_materials,replacement_)) {
                // The game reset the body to its shadow-only material (mantle/state changes): re-apply
                // our textured override instead of failing forever (the body stayed invisible).
                if(tick && refresh_tick_ && tick>=refresh_tick_ && tick-refresh_tick_<250) {++refresh_deferred_;reason_=Reason::retry_deferred;return verified_=false;}
                set_all(reinterpret_cast<void*>(owner.model),reinterpret_cast<void*>(replacement_));
                refresh_tick_=tick;++refreshes_;refresh(reinterpret_cast<void*>(owner.model));
                const auto after=body_material_route(read,base,arm,owner);
                verified_=after && after->draw_count==1 && after->override_count>=1 && all_equal(after->override_materials,replacement_) &&
                    after->draw_materials[0]==replacement_;
                if(!verified_) reason_=Reason::refresh_failed;
                return verified_;
            }
            if(route->draw_materials[0]==replacement_) {
                if((route->model_flags&0x10)!=0) return verified_=true;
                // Visibility is a separate owned bit transaction; refreshing
                // an already-correct material cannot repair that bit.
                reason_=Reason::visibility;return verified_=false;
            }
            // Failed rebuilds used to retry on every upload. Revalidate every
            // request, but bound the expensive native retry for this owner.
            if(tick && refresh_tick_ && tick>=refresh_tick_ && tick-refresh_tick_<250) {
                ++refresh_deferred_;reason_=Reason::retry_deferred;return verified_=false;
            }
            // Refresh only while the same verified owner still has our override.
            refresh_tick_=tick;++refreshes_;refresh(reinterpret_cast<void*>(owner.model));
            const auto after=body_material_route(read,base,arm,owner);
            verified_=after && after->draw_count==1 && after->draw_materials[0]==replacement_ &&
                (after->model_flags&0x10)!=0;
            if(!verified_) reason_=Reason::refresh_failed;
            return verified_;
        }
        const auto original=route->override_count?route->override_materials[0]:0;
        if(!all_equal(route->override_materials,original)) {reason_=Reason::owner_or_override;return false;}
        original_drawable_=(route->model_flags&0x10)!=0;
        set_all(reinterpret_cast<void*>(owner.model),reinterpret_cast<void*>(*material));
        owner_=owner;original_=original;original_draw_=route->draw_materials[0];replacement_=*material;active_=true;restoring_=false;
        refresh_tick_=tick;++refreshes_;refresh(reinterpret_cast<void*>(owner.model));
        const auto after=body_material_route(read,base,arm,owner);
        verified_=after && after->draw_count==1 && after->override_count>=1 && all_equal(after->override_materials,replacement_) &&
            after->draw_materials[0]==replacement_ && (after->model_flags&0x10)!=0;
        if(!verified_) reason_=Reason::refresh_failed;
        return verified_;
    }
    template<class Read,class SetAll,class Refresh> bool restore(Read& read,std::uintptr_t base,
        const Arm& arm,bool owned_render_scope,SetAll set_all,Refresh refresh) {
        if(!active_) return true;
        if(!owned_render_scope || !abi(read,base)) return false;
        const auto route=body_material_route(read,base,arm,owner_);
        if(!route || route->draw_count!=1 || route->override_count<1) return false;
        const bool replacement=all_equal(route->override_materials,replacement_);
        const bool original=restoring_ && all_equal(route->override_materials,original_);
        if(!replacement && !original) {reason_=Reason::foreign_restore;return false;}
        // A failed post-write read or refresh must not strand the transaction
        // after our exact original override has already been restored.
        restoring_=true;
        if(replacement)set_all(reinterpret_cast<void*>(owner_.model),reinterpret_cast<void*>(original_));
        if(!original || route->draw_materials[0]!=original_draw_)refresh(reinterpret_cast<void*>(owner_.model));
        const auto after=body_material_route(read,base,arm,owner_);
        if(!after || after->draw_count!=1 || after->draw_materials[0]!=original_draw_ || after->override_count<1 || !all_equal(after->override_materials,original_)) return false;
        active_=verified_=restoring_=false;return true;
    }
    template<class Read,class Enable> bool enable_drawable_owned(Read& read,std::uintptr_t base,
        const Arm& arm,const NativeBodyBind& current,bool owned_render_scope,Enable enable) {
        if(!owned_render_scope || !active_ || !(owner_==current) || !abi(read,base))return false;
        const auto route=body_material_route(read,base,arm,current);
        if(!route || route->draw_count!=1 || route->override_count<1 ||
           !all_equal(route->override_materials,replacement_) || route->draw_materials[0]!=replacement_)return false;
        // Original bit was captured before SetAll. Mutate only this one bit
        // after ownership and every material record have been revalidated.
        enable(current.model);
        const auto after=body_material_route(read,base,arm,current);
        return verified_=after && after->draw_count==1 && after->override_count>=1 &&
            all_equal(after->override_materials,replacement_) && after->draw_materials[0]==replacement_ && (after->model_flags&0x10)!=0;
    }
    template<class Read,class SetAll,class Refresh> bool restore_owned(Read& read,std::uintptr_t base,
        const Arm& arm,const NativeBodyBind& current,bool owned_render_scope,SetAll set_all,Refresh refresh) {
        if(!active_) return true;
        if(!(owner_==current)) return false;
        return restore(read,base,arm,owned_render_scope,set_all,refresh);
    }
};

// Only rigid, model-space 20-joint copies are accepted. Avatar scaling requires
// separate proof of the native attachment/skinning scale contract.
class BodyPalettePublication {
    std::mutex mutex_;
    struct Frame {
        NativeBodyBind owner{};
        std::array<Transform,20> joints{};
        std::uint64_t epoch{},tick{};
    } frame_;
public:
    void clear() {std::lock_guard<std::mutex> lock(mutex_);frame_={};}
    void publish(const NativeBodyBind& owner,const std::array<Transform,20>& joints,
        std::uint64_t epoch,std::uint64_t tick) {
        if(!epoch || !pointer(owner.model) || !pointer(owner.animation) || !pointer(owner.skeleton))
            throw std::invalid_argument("Invalid owned body publication");
        for(const auto& joint:joints) if(!valid(joint)) throw std::invalid_argument("Invalid body palette");
        std::lock_guard<std::mutex> lock(mutex_);frame_={owner,joints,epoch,tick};
    }
    std::optional<std::array<Transform,20>> palette(const NativeBodyBind& owner,
        std::uint64_t epoch,std::uint64_t tick,bool native_authored) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(native_authored || !epoch || frame_.epoch!=epoch || !(frame_.owner==owner) ||
           tick<frame_.tick || tick-frame_.tick>250) return {};
        return frame_.joints;
    }
};
} // namespace d2vr::rig

