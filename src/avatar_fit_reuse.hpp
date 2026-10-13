#pragma once
#include "native_arm_binding_snapshot.hpp"

namespace d2vr::rig {
struct AvatarMeshSurfaceToken {
    std::uintptr_t resource{};
    std::uint32_t kind{},vertices{},triangles{};
};
inline bool operator==(const AvatarMeshSurfaceToken& a,const AvatarMeshSurfaceToken& b) {
    return a.resource==b.resource && a.kind==b.kind && a.vertices==b.vertices && a.triangles==b.triangles;
}
struct AvatarMeshToken {
    std::uintptr_t source{},primary{};
    std::vector<AvatarMeshSurfaceToken> surfaces;
};
inline bool operator==(const AvatarMeshToken& a,const AvatarMeshToken& b) {
    return a.source==b.source && a.primary==b.primary && a.surfaces==b.surfaces;
}
inline bool operator!=(const AvatarMeshToken& a,const AvatarMeshToken& b) {return !(a==b);}
// Constructor 25d090/2368a0 and idMD6 surface list 259910, already used by
// rig_fields.py. Resource identity is conservative session identity, not a
// decoded Emily/Corvo ID. Changed/reloaded resource allocations reject reuse.
template<class Read> std::optional<AvatarMeshToken> avatar_mesh_token(Read& read,std::uintptr_t base,const Arm& arm) {
    const auto descriptor=value<std::uintptr_t>(read,arm.model+0x1fe8);
    if(!descriptor || !pointer(*descriptor) || value<std::uintptr_t>(read,*descriptor+0x50)!=arm.skeleton) return {};
    const auto source=value<std::uintptr_t>(read,*descriptor+8),geometry=value<std::uintptr_t>(read,*descriptor+0x68);
    if(!source || !geometry || !pointer(*source) || !pointer(*geometry) || value<std::uintptr_t>(read,arm.model+0x1bb0)!=source) return {};
    const auto models=list(read,*geometry,16);
    if(!models || !models->packed_count) return {};
    const auto primary=value<std::uintptr_t>(read,models->data);
    if(!primary || !pointer(*primary) || value<std::uintptr_t>(read,*primary)!=base+0x1b0d118) return {};
    const auto surfaces=list(read,*primary+0x58,64);
    if(!surfaces || !surfaces->packed_count) return {};
    AvatarMeshToken result{*source,*primary,{}};
    for(unsigned i=0;i<surfaces->packed_count;++i) {
        const auto surface=value<std::uintptr_t>(read,surfaces->data+i*8);
        if(!surface || !pointer(*surface)) return {};
        const auto kind=value<std::uint32_t>(read,*surface+0x30),vertices=value<std::uint32_t>(read,*surface+0x54),triangles=value<std::uint32_t>(read,*surface+0x58);
        if(!kind || !vertices || !triangles || !*vertices || *vertices>500000 || !*triangles || *triangles>1000000) return {};
        result.surfaces.push_back({*surface,*kind,*vertices,*triangles});
    }
    if(value<std::uintptr_t>(read,arm.model+0x1fe8)!=descriptor || value<std::uintptr_t>(read,*descriptor+0x50)!=arm.skeleton ||
       value<std::uintptr_t>(read,*descriptor+8)!=source || value<std::uintptr_t>(read,*descriptor+0x68)!=geometry ||
       value<std::uintptr_t>(read,arm.model+0x1bb0)!=source) return {};
    const auto model_check=list(read,*geometry,16),surface_check=list(read,*primary+0x58,64);
    if(!model_check || model_check->data!=models->data || model_check->packed_count!=models->packed_count ||
       value<std::uintptr_t>(read,models->data)!=primary || !surface_check || surface_check->data!=surfaces->data ||
       surface_check->packed_count!=surfaces->packed_count) return {};
    for(unsigned i=0;i<result.surfaces.size();++i) {
        const auto& s=result.surfaces[i];
        if(value<std::uintptr_t>(read,surfaces->data+i*8)!=s.resource || value<std::uint32_t>(read,s.resource+0x30)!=s.kind ||
           value<std::uint32_t>(read,s.resource+0x54)!=s.vertices || value<std::uint32_t>(read,s.resource+0x58)!=s.triangles) return {};
    }
    return result;
}
struct AvatarArmTemplate {
    ArmHierarchy parents{};
    ArmChain chain{};
    ForearmHelpers helpers{};
    FingerChains fingers{};
    std::array<std::string,69> labels{};
    std::array<std::array<float,12>,69> inverse{};
    std::array<float,16> bind_wrist{},bind_grip{};
    std::array<float,3> model_scale{};
    std::array<double,2> limb_lengths_native{};
    AvatarMeshToken mesh;
};
inline bool operator==(const AvatarArmTemplate& a,const AvatarArmTemplate& b) {
    return a.parents==b.parents && a.chain.upper==b.chain.upper && a.chain.elbow==b.chain.elbow && a.chain.wrist==b.chain.wrist &&
        a.helpers==b.helpers && a.fingers==b.fingers && a.labels==b.labels && a.inverse==b.inverse &&
        a.bind_wrist==b.bind_wrist && a.bind_grip==b.bind_grip && a.model_scale==b.model_scale &&
        a.limb_lengths_native==b.limb_lengths_native && a.mesh==b.mesh;
}
struct AvatarFitTemplate {
    std::array<AvatarArmTemplate,2> arms;
    double units_per_metre{};
    NativeAvatarMeasurements measurements;
};
inline bool operator==(const AvatarFitTemplate& a,const AvatarFitTemplate& b) {
    return a.arms==b.arms && a.units_per_metre==b.units_per_metre &&
        a.measurements.palm_span_metres==b.measurements.palm_span_metres &&
        a.measurements.shoulder_span_metres==b.measurements.shoulder_span_metres;
}
inline bool complete_avatar_fit_template(const AvatarFitTemplate& key) {
    if(!std::isfinite(key.units_per_metre) || key.units_per_metre<=0 || key.units_per_metre>1000 ||
       !std::isfinite(key.measurements.palm_span_metres) || key.measurements.palm_span_metres<.8 || key.measurements.palm_span_metres>2.5 ||
       !std::isfinite(key.measurements.shoulder_span_metres) || key.measurements.shoulder_span_metres<.15 || key.measurements.shoulder_span_metres>.65) return false;
    for(const auto& arm:key.arms) {
        if(!valid_hierarchy(arm.parents) || arm.chain.upper>=69 || arm.chain.elbow>=69 || arm.chain.wrist>=69 ||
           arm.parents[arm.chain.elbow]!=static_cast<int>(arm.chain.upper) || arm.parents[arm.chain.wrist]!=static_cast<int>(arm.chain.elbow) ||
           !pointer(arm.mesh.source) || !pointer(arm.mesh.primary) || arm.mesh.surfaces.empty() || arm.mesh.surfaces.size()>64 ||
           !std::isfinite(arm.model_scale[0]) || arm.model_scale[0]<1e-4 || arm.model_scale[0]>16 ||
           arm.model_scale[1]!=arm.model_scale[0] || arm.model_scale[2]!=arm.model_scale[0]) return false;
        for(const auto& s:arm.mesh.surfaces) if(!pointer(s.resource) || !s.vertices || s.vertices>500000 || !s.triangles || s.triangles>1000000) return false;
        for(const auto& label:arm.labels) if(label.empty() || label.size()>127) return false;
        for(const auto& rows:arm.inverse) for(float f:rows) if(!std::isfinite(f)) return false;
        for(double n:arm.limb_lengths_native) if(!std::isfinite(n) || n<=0) return false;
    }
    return key.arms[0].model_scale==key.arms[1].model_scale;
}
template<class Read> std::optional<AvatarFitTemplate> avatar_fit_template(Read& read,std::uintptr_t base,
    const NativeArmBindingSnapshot& snapshot,double units_per_metre) {
    try {
        if(!std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000) return {};
        const auto before=player_arms(read,base);if(!before || *before!=snapshot.arms) return {};
        AvatarFitTemplate key{};key.units_per_metre=units_per_metre;
        std::array<Vec3,2> palms{},shoulders{};
        for(unsigned side=0;side<2;++side) {
            const auto& arm=snapshot.arms[side];const auto& binding=snapshot.bindings[side];
            // Resolve the whole binding again only in the bounded refresh lane.
            const auto current=arm_binding(read,base,arm);if(!current) return {};
            const auto mesh=avatar_mesh_token(read,base,arm);if(!mesh) return {};
            auto& out=key.arms[side];out.parents=current->parents;out.chain=current->chain;out.helpers=current->helpers;
            out.fingers=current->finger_chains;out.inverse=current->inverse;out.bind_wrist=current->bind_wrist.f;out.bind_grip=current->bind_grip.f;
            if(out.parents!=binding.parents || out.inverse!=binding.inverse || out.bind_wrist!=binding.bind_wrist.f || out.bind_grip!=binding.bind_grip.f) return {};
            if(out.chain.upper!=binding.chain.upper || out.chain.elbow!=binding.chain.elbow || out.chain.wrist!=binding.chain.wrist ||
               out.helpers!=binding.helpers || out.fingers!=binding.finger_chains) return {};
            const auto names=value<std::uintptr_t>(read,arm.skeleton+0x70);std::array<std::uint16_t,69> ids{},again{};
            if(!names || !pointer(*names) || !read(*names,ids.data(),sizeof(ids))) return {};
            for(unsigned joint=0;joint<69;++joint) {const auto name=joint_name(read,base,ids[joint]);if(!name) return {};out.labels[joint]=*name;}
            if(value<std::uintptr_t>(read,arm.skeleton+0x70)!=names || !read(*names,again.data(),sizeof(again)) || ids!=again) return {};
            if(out.labels!=snapshot.sources[side].labels) return {};
            const auto scale=value<std::array<float,3>>(read,arm.model+0x1890);
            if(!scale || !std::isfinite((*scale)[0]) || (*scale)[0]<1e-4 || (*scale)[0]>16 ||
               (*scale)[1]!=(*scale)[0] || (*scale)[2]!=(*scale)[0]) return {};
            out.model_scale=*scale;out.mesh=*mesh;
            const auto upper=position(bind_transform(out.inverse[out.chain.upper]));
            const auto elbow=position(bind_transform(out.inverse[out.chain.elbow]));
            const auto wrist=position(bind_transform(out.inverse[out.chain.wrist]));
            out.limb_lengths_native={length(elbow-upper)*(*scale)[0],length(wrist-elbow)*(*scale)[0]};
            for(double length_native:out.limb_lengths_native) if(!std::isfinite(length_native) || length_native<=0) return {};
            palms[side]=position(current->bind_grip)*(*scale)[0];shoulders[side]=upper*(*scale)[0];
            if(avatar_mesh_token(read,base,arm)!=mesh || value<std::array<float,3>>(read,arm.model+0x1890)!=scale) return {};
        }
        if(key.arms[0].model_scale!=key.arms[1].model_scale) return {};
        key.measurements={length(palms[1]-palms[0])/units_per_metre,length(shoulders[1]-shoulders[0])/units_per_metre};
        if(key.measurements.palm_span_metres<.8 || key.measurements.palm_span_metres>2.5 ||
           key.measurements.shoulder_span_metres<.15 || key.measurements.shoulder_span_metres>.65) return {};
        const auto after=player_arms(read,base);if(!after || *after!=snapshot.arms) return {};
        for(unsigned side=0;side<2;++side) {
            if(avatar_mesh_token(read,base,snapshot.arms[side])!=std::optional<AvatarMeshToken>(key.arms[side].mesh) ||
               value<std::array<float,3>>(read,snapshot.arms[side].model+0x1890)!=std::optional<std::array<float,3>>(key.arms[side].model_scale)) return {};
            const auto current=arm_binding(read,base,snapshot.arms[side]);
            const auto& arm_key=key.arms[side];
            if(!current || current->parents!=arm_key.parents || current->inverse!=arm_key.inverse ||
               current->chain.upper!=arm_key.chain.upper || current->chain.elbow!=arm_key.chain.elbow || current->chain.wrist!=arm_key.chain.wrist ||
               current->helpers!=arm_key.helpers || current->finger_chains!=arm_key.fingers ||
               current->bind_wrist.f!=arm_key.bind_wrist || current->bind_grip.f!=arm_key.bind_grip) return {};
            const auto names=value<std::uintptr_t>(read,snapshot.arms[side].skeleton+0x70);std::array<std::uint16_t,69> ids{};
            if(!names || !pointer(*names) || !read(*names,ids.data(),sizeof(ids)) || ids!=snapshot.sources[side].ids) return {};
            for(unsigned joint=0;joint<69;++joint) if(joint_name(read,base,ids[joint])!=arm_key.labels[joint]) return {};
        }
        const auto final_owner=player_arms(read,base);if(!final_owner || *final_owner!=snapshot.arms) return {};
        return key;
    } catch(...) {return {};}
}
// Bounded refresh check for mutable resource/scale fields which are outside
// NativeArmBindingCache's skeleton/source identity. No joint-name/bind walk.
template<class Read,class Owned> bool avatar_fit_resources_current_from(Read& read,std::uintptr_t base,
    const NativeArmBindingSnapshot& snapshot,const AvatarFitTemplate& key,Owned& owned) {
    try {
        if(!owned()) return false;
        for(unsigned pass=0;pass<2;++pass) for(unsigned side=0;side<2;++side) {
            const auto& arm=snapshot.arms[side];const auto& expected=key.arms[side];
            const auto before=value<std::array<float,3>>(read,arm.model+0x1890);
            const auto mesh=avatar_mesh_token(read,base,arm);
            const auto after=value<std::array<float,3>>(read,arm.model+0x1890);
            if(!before || !after || *before!=expected.model_scale || *after!=*before ||
               !mesh || *mesh!=expected.mesh) return false;
        }
        return owned();
    } catch(...) {return false;}
}
template<class Read> bool avatar_fit_resources_current(Read& read,std::uintptr_t base,
    const NativeArmBindingSnapshot& snapshot,const AvatarFitTemplate& key) {
    auto owned=[&] {const auto current=player_arms(read,base);return current && *current==snapshot.arms;};
    return avatar_fit_resources_current_from(read,base,snapshot,key,owned);
}
// Present-owned state. A suspension retains a last accepted fit but cannot
// publish it until a complete template is verified in the same XR reference.
class AvatarFitReuse {
    std::optional<AvatarFitTemplate> accepted_template_;
    std::optional<AvatarCalibrationResult> accepted_fit_;
    bool ready_{};
    void discard_fit() {accepted_template_.reset();accepted_fit_.reset();ready_=false;}
public:
    void clear() {discard_fit();}
    void suspend(std::uint64_t epoch) {ready_=false;if(accepted_fit_ && accepted_fit_->reference_epoch!=epoch) discard_fit();}
    bool verify(const std::optional<AvatarFitTemplate>& candidate,std::uint64_t epoch) {
        suspend(epoch);
        if(!candidate || !accepted_fit_ || !accepted_template_) return false;
        if(!complete_avatar_fit_template(*candidate)) {discard_fit();return false;}
        if(!(*candidate==*accepted_template_)) {discard_fit();return false;}
        return ready_=true;
    }
    static bool can_accept(const AvatarFitTemplate& key,const AvatarCalibrationResult& fit,std::uint64_t epoch) {
        const auto valid_dimension=[](const std::optional<double>& value,double low,double high) {
            return !value || (std::isfinite(*value) && *value>=low && *value<=high);
        };
        if(!complete_avatar_fit_template(key) || !epoch || fit.reference_epoch!=epoch ||
           !std::isfinite(fit.palm_span_metres) || fit.palm_span_metres<.9 || fit.palm_span_metres>2.4 ||
           !std::isfinite(fit.arm_scale) || fit.arm_scale<.7 || fit.arm_scale>1.45 ||
           !std::isfinite(fit.shoulder_span_metres) || fit.shoulder_span_metres<.1 || fit.shoulder_span_metres>.95 ||
           !std::isfinite(fit.eye_to_shoulder_metres) || fit.eye_to_shoulder_metres<.05 || fit.eye_to_shoulder_metres>.4 ||
           (fit.standing_eye_height_metres && (!std::isfinite(*fit.standing_eye_height_metres) || *fit.standing_eye_height_metres<.9 || *fit.standing_eye_height_metres>2.2)) ||
           !valid_dimension(fit.torso_width_metres,.15,.8) || fit.torso_width_measured!=fit.torso_width_metres.has_value() ||
           !valid_dimension(fit.arm_reach_metres[0],.25,1.) || !valid_dimension(fit.arm_reach_metres[1],.25,1.) ||
           fit.arm_lengths_measured!=(fit.arm_reach_metres[0].has_value() && fit.arm_reach_metres[1].has_value()) ||
           (fit.shoulder_span_measured && (fit.shoulder_span_metres<.15 || fit.shoulder_span_metres>.65)) ||
           // arm_scale remains the total-span ratio; independently measured
           // reaches need not be equal or scale in that same ratio.
           std::abs(fit.palm_span_metres-key.measurements.palm_span_metres*fit.arm_scale)>1e-9 ||
           (!fit.shoulder_span_measured && std::abs(fit.shoulder_span_metres-key.measurements.shoulder_span_metres)>1e-9)) return false;
        return true;
    }
    bool accept(const AvatarFitTemplate& key,const AvatarCalibrationResult& fit,std::uint64_t epoch) {
        if(!can_accept(key,fit,epoch))return false;
        accepted_template_=key;accepted_fit_=fit;ready_=true;return true;
    }
    bool has_accepted_fit() const {return accepted_fit_.has_value();}
    bool recalibration_required() const {return !accepted_fit_;}
    std::uint64_t accepted_reference_epoch() const {return accepted_fit_?accepted_fit_->reference_epoch:0;}
    bool poses_allowed() const {return ready_;}
    std::optional<AvatarCalibrationResult> active_fit() const {return ready_?accepted_fit_:std::nullopt;}
};
} // namespace d2vr::rig
