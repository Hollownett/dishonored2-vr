#include "../src/avatar_fit_reuse.hpp"
#include <cassert>
#include <limits>
#include <map>
#include <cstring>

int main() {
    using namespace d2vr;
    using namespace d2vr::rig;
    AvatarFitTemplate key{};key.units_per_metre=1;key.measurements={1.5,.3};
    for(unsigned side=0;side<2;++side) {
        auto& arm=key.arms[side];arm.parents.fill(0);arm.parents[0]=-1;arm.chain={2,3,4};arm.parents[3]=2;arm.parents[4]=3;
        arm.model_scale={1,1,1};arm.limb_lengths_native={.26,.27};
        arm.mesh={0x10000+side*0x100,0x20000+side*0x100,{{0x30000+side*0x100,1,100,50}}};
        for(unsigned joint=0;joint<69;++joint) {arm.labels[joint]=(side?"right":"left")+std::to_string(joint);arm.inverse[joint]={1,0,0,0,0,1,0,0,0,0,1,0};}
    }
    const AvatarCalibrationResult fit{7,1.6,.3,.22,1.6/1.5,{}};
    AvatarFitReuse gate;
    assert(gate.recalibration_required() && !gate.poses_allowed());
    assert(!gate.verify(key,7) && !gate.active_fit());
    assert(gate.accept(key,fit,7) && gate.active_fit());
    auto inferred_shoulders=fit;inferred_shoulders.shoulder_span_metres=.32;
    assert(!gate.accept(key,inferred_shoulders,7));
    inferred_shoulders.shoulder_span_measured=true;
    assert(gate.accept(key,inferred_shoulders,7));
    assert(gate.accept(key,fit,7));
    gate.suspend(7);assert(gate.has_accepted_fit() && !gate.active_fit());
    assert(!gate.poses_allowed());
    assert(!gate.verify({},7) && gate.has_accepted_fit() && !gate.active_fit());
    // Runtime entity/model/skeleton addresses are intentionally absent from
    // this owned template, so checkpoint owner replacement cannot change it.
    assert(gate.verify(key,7) && gate.active_fit()->arm_scale==fit.arm_scale);
    auto rejects=[&](const AvatarFitTemplate& changed) {
        assert(gate.accept(key,fit,7));assert(!gate.verify(changed,7));
        assert(!gate.has_accepted_fit() && !gate.active_fit());
        assert(!gate.poses_allowed());
    };
    auto changed=key;changed.arms[1].parents[8]=2;rejects(changed);
    changed=key;changed.arms[1].inverse[4][3]+=.000001f;rejects(changed);
    changed=key;changed.arms[1].labels[4]="different_wrist";rejects(changed);
    changed=key;changed.arms[1].chain.wrist=5;rejects(changed);
    changed=key;changed.arms[1].helpers[0]=5;rejects(changed);
    changed=key;changed.arms[1].fingers[0][0]=5;rejects(changed);
    changed=key;changed.arms[1].bind_grip[12]+=.00001f;rejects(changed);
    changed=key;changed.arms[1].model_scale={1.01f,1.01f,1.01f};rejects(changed);
    changed=key;changed.arms[1].limb_lengths_native[0]+=.000001;rejects(changed);
    changed=key;changed.measurements.palm_span_metres+=.000001;rejects(changed);
    changed=key;changed.units_per_metre=2;rejects(changed);
    // Distinct mesh resources reject even deliberately identical bone data.
    changed=key;changed.arms[1].mesh.primary+=0x1000;rejects(changed);
    changed=key;changed.arms[1].mesh.surfaces[0].vertices+=1;rejects(changed);
    assert(gate.accept(key,fit,7));gate.suspend(8);
    assert(!gate.has_accepted_fit() && !gate.verify(key,8));
    assert(!gate.accept(key,fit,8));
    auto incomplete=key;incomplete.arms[1].mesh.surfaces.clear();
    assert(!gate.accept(incomplete,fit,7));
    auto wrong_dimensions=fit;wrong_dimensions.palm_span_metres=1.7;
    assert(!gate.accept(key,wrong_dimensions,7));
    assert(gate.accept(key,fit,7));auto invalid=fit;invalid.arm_scale=std::numeric_limits<double>::quiet_NaN();
    assert(!gate.accept(key,invalid,7));assert(gate.active_fit());
    {
        auto independent=fit;independent.shoulder_span_metres=.44;independent.shoulder_span_measured=true;
        independent.torso_width_metres=.47;independent.torso_width_measured=true;
        independent.arm_reach_metres={.53,.63};independent.arm_lengths_measured=true;
        assert(same_avatar_calibration(independent,independent));
        auto different=independent;different.arm_reach_metres[0]=.54;assert(!same_avatar_calibration(independent,different));
        different=independent;different.torso_width_metres=.48;assert(!same_avatar_calibration(independent,different));
        different=independent;different.torso_width_measured=false;assert(!same_avatar_calibration(independent,different));
        different=independent;different.arm_lengths_measured=false;assert(!same_avatar_calibration(independent,different));
        different=independent;different.standing_eye_height_metres=1.7;assert(!same_avatar_calibration(independent,different));
        assert(gate.accept(key,independent,7));
        const auto preserves=[&](const AvatarCalibrationResult& candidate) {
            assert(!gate.accept(key,candidate,7));
            const auto retained=gate.active_fit();assert(retained && gate.poses_allowed());
            assert(same_avatar_calibration(*retained,independent));
            assert(gate.verify(key,7));
        };
        for(const double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),.149,.801}) {
            auto candidate=independent;candidate.torso_width_metres=bad;preserves(candidate);
        }
        for(unsigned side=0;side<2;++side)for(const double bad:{std::numeric_limits<double>::quiet_NaN(),-.5,.249,1.001}) {
            auto candidate=independent;candidate.arm_reach_metres[side]=bad;preserves(candidate);
        }
        auto candidate=independent;candidate.torso_width_measured=false;preserves(candidate);
        candidate=independent;candidate.torso_width_metres.reset();preserves(candidate);
        candidate=independent;candidate.arm_lengths_measured=false;preserves(candidate);
        candidate=independent;candidate.arm_reach_metres[1].reset();preserves(candidate);
        candidate=independent;candidate.arm_reach_metres={};preserves(candidate);
        candidate=independent;candidate.shoulder_span_metres=.149;preserves(candidate);
        candidate=independent;candidate.shoulder_span_metres=.651;preserves(candidate);
        // Partial supplied measurements are supported without pretending that
        // both arms have been measured. All boundary values remain inclusive.
        candidate=independent;candidate.arm_reach_metres[1].reset();candidate.arm_lengths_measured=false;
        assert(gate.accept(key,candidate,7));assert(gate.active_fit()->arm_reach_metres[0]==.53 && !gate.active_fit()->arm_reach_metres[1]);
        for(const double width:{.15,.8}) {
            candidate=independent;candidate.torso_width_metres=width;assert(gate.accept(key,candidate,7));
        }
        candidate=independent;candidate.arm_reach_metres={.25,1.};assert(gate.accept(key,candidate,7));
        assert(gate.accept(key,fit,7)); // legacy T-pose-only metadata remains supported
    }
    gate.clear();assert(!gate.has_accepted_fit() && !gate.active_fit());
    assert(gate.recalibration_required() && !gate.poses_allowed());
    // Same-space retry cancellation retains accepted fit, while reference
    // change while closed invalidates both wizard and reuse gate.
    AvatarCalibration wizard;TrackedPose head{{0,1.7,0},{}};
    std::array<TrackedPose,2> hands{{{{-.8,1.48,0},{}},{{.8,1.48,0},{}}}};
    wizard.begin(7,key.measurements);
    for(unsigned i=0;i<=40;++i) wizard.observe(head,hands,3,true,true,i*25,7);
    assert(wizard.result(7));assert(gate.accept(key,*wizard.result(7),7));
    wizard.begin(7,key.measurements);wizard.observe(head,hands,3,true,true,2000,7);
    gate.suspend(7);assert(wizard.synchronize(7,false)==AvatarCalibrationState::ready);
    assert(gate.verify(key,7) && wizard.result(7));
    wizard.synchronize(8,false);gate.suspend(8);assert(!wizard.result(8) && !gate.active_fit());
    auto unreadable=[](std::uintptr_t,void*,std::size_t) {return false;};
    NativeArmBindingSnapshot empty{};
    assert(!avatar_fit_template(unreadable,0x140000000,empty,1));
    // Verified mesh resource tokens survive relocated runtime model wrappers,
    // but reject resource/count changes and incomplete mid-capture reads.
    std::map<std::uintptr_t,std::vector<unsigned char>> memory;
    auto put=[&](std::uintptr_t at,const auto& value) {
        auto& bytes=memory[at];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));
    };
    const std::uintptr_t base=0x140000000,descriptor=0x50000,geometry=0x60000,primary=0x70000,surface=0x80000;
    Arm arm{};arm.model=0x40000;arm.skeleton=0x90000;
    put(arm.model+0x1fe8,descriptor);put(descriptor+0x50,arm.skeleton);put(descriptor+8,std::uintptr_t{0xa0000});
    put(descriptor+0x68,geometry);put(arm.model+0x1bb0,std::uintptr_t{0xa0000});
    put(geometry,List{0xb0000,1,1<<8});put(0xb0000,primary);put(primary,base+0x1b0d118);
    put(primary+0x58,List{0xc0000,1,1<<8});put(0xc0000,surface);
    put(surface+0x30,std::uint32_t{2});put(surface+0x54,std::uint32_t{100});put(surface+0x58,std::uint32_t{50});
    auto reader=[&](std::uintptr_t at,void* out,std::size_t size) {
        const auto found=memory.find(at);if(found==memory.end() || found->second.size()!=size) return false;
        std::memcpy(out,found->second.data(),size);return true;
    };
    const auto token=avatar_mesh_token(reader,base,arm);assert(token);
    auto replacement=arm;replacement.model=0xd0000;
    put(replacement.model+0x1fe8,descriptor);put(replacement.model+0x1bb0,std::uintptr_t{0xa0000});
    assert(avatar_mesh_token(reader,base,replacement)==token);
    put(surface+0x54,std::uint32_t{101});assert(avatar_mesh_token(reader,base,arm)!=token);
    unsigned descriptor_reads{};
    auto interrupted=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at==arm.model+0x1fe8 && ++descriptor_reads>1) return false;
        return reader(at,out,size);
    };
    assert(!avatar_mesh_token(interrupted,base,arm));
    put(surface+0x54,std::uint32_t{100});
    put(arm.model+0x1890,std::array<float,3>{1,1,1});
    put(replacement.model+0x1890,std::array<float,3>{1,1,1});
    NativeArmBindingSnapshot resource_snapshot{{arm,replacement},{},{}};
    auto resources_key=key;resources_key.arms[0].mesh=resources_key.arms[1].mesh=*token;
    unsigned owner_reads{};
    auto owned=[&] {++owner_reads;return true;};
    assert(avatar_fit_resources_current_from(reader,base,resource_snapshot,resources_key,owned));
    assert(owner_reads==2);
    // Same binding/snapshot identity, only model scale changes in place.
    put(replacement.model+0x1890,std::array<float,3>{1.01f,1.01f,1.01f});
    assert(!avatar_fit_resources_current_from(reader,base,resource_snapshot,resources_key,owned));
    put(replacement.model+0x1890,std::array<float,3>{1,1,1});
    put(surface+0x54,std::uint32_t{102});
    assert(!avatar_fit_resources_current_from(reader,base,resource_snapshot,resources_key,owned));
    put(surface+0x54,std::uint32_t{100});
    const auto new_primary=primary+0x1000;
    put(new_primary,base+0x1b0d118);put(new_primary+0x58,List{0xc0000,1,1<<8});put(0xb0000,new_primary);
    assert(!avatar_fit_resources_current_from(reader,base,resource_snapshot,resources_key,owned));
    put(0xb0000,primary);
    unsigned scale_reads{};
    auto changing_scale=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at==arm.model+0x1890 && ++scale_reads==2) put(at,std::array<float,3>{1.02f,1.02f,1.02f});
        return reader(at,out,size);
    };
    assert(!avatar_fit_resources_current_from(changing_scale,base,resource_snapshot,resources_key,owned));
    put(arm.model+0x1890,std::array<float,3>{1,1,1});
    unsigned replacement_descriptor_reads{};
    auto cross_side_change=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at==replacement.model+0x1fe8 && ++replacement_descriptor_reads==1)
            put(arm.model+0x1890,std::array<float,3>{1.03f,1.03f,1.03f});
        return reader(at,out,size);
    };
    assert(!avatar_fit_resources_current_from(cross_side_change,base,resource_snapshot,resources_key,owned));
    put(arm.model+0x1890,std::array<float,3>{1,1,1});
    owner_reads=0;auto replaced_owner=[&] {return ++owner_reads==1;};
    assert(!avatar_fit_resources_current_from(reader,base,resource_snapshot,resources_key,replaced_owner));
    descriptor_reads=0;
    assert(!avatar_fit_resources_current_from(interrupted,base,resource_snapshot,resources_key,owned));
    assert(!avatar_fit_resources_current(unreadable,base,resource_snapshot,resources_key));
}
