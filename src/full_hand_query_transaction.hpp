#pragma once
#include "full_hand_contact.hpp"
#include "rig_transform.hpp"

namespace d2vr {
// A local contact correction, never a swept-clearance certificate. Native queries
// and the arm-only solve belong to the caller's admitted game callback.
class FullHandQueryTransaction {
public:
    using Palette=std::array<rig::Transform,69>;
    enum class State {invalid,interrupted,raw_query,correction_limit,arm_solve,geometry,final_query,penetrating,ready};
    struct Result {
        State state{State::invalid};
        Palette palette{};
        HandProxies proxies{};
        Vec3 shift{};
        unsigned casts{};
        bool contact{};
        std::vector<FullHandTargetWitness::Witness> raw_targets,final_targets;
        bool ready() const noexcept {return state==State::ready;}
    };
    static constexpr double probe_offset=.0015,max_shift=.08,clearance_slop=.0005;
    struct Working {
        Result result{};
        HandProxies from{},final_from{};
        std::array<WorldTraceQueryResult,hand_proxy_count> queries{};
        FullHandContactResult reduced{},clearance{};
        std::optional<Palette> solved;
        std::optional<HandProxies> fitted;
    };
    template<class Cast,class Solve,class Geometry,class Current>
    static void run(const WorldTrackedPose& raw,const Palette& raw_palette,const HandProxies& proxies,
                    const NativeWorldQuerySource& source,Cast& cast,Solve& solve,Geometry& geometry,Current& current,Working& w) {
        w={};auto& result=w.result;
        if(!grab_pose_valid(raw) || !source.valid() || !anatomical_shape_valid(proxies))return;
        const auto reject=[&](State state) {result.state=state;};
        if(!current()) {reject(State::interrupted);return;}
        w.fitted=geometry(raw_palette);
        if(!w.fitted || !same_geometry(*w.fitted,proxies)) {reject(State::geometry);return;}
        w.from=proxies;
        for(auto& sphere:w.from)sphere.centre=sphere.centre+Vec3{0,0,probe_offset};
        for(unsigned i=0;i<hand_proxy_count;++i) {
            if(!current()) {reject(State::interrupted);return;}
            w.queries[i]=cast(w.from[i].centre,proxies[i].centre,proxies[i].radius);++result.casts;
            if(!w.queries[i].complete) {reject(State::raw_query);return;}
        }
        w.reduced=full_hand_contact_reduce(w.from,proxies,w.queries,source,0,max_shift);
        if(!w.reduced.valid || w.reduced.blocked) {reject(State::correction_limit);return;}
        if(!current()) {reject(State::interrupted);return;}
        auto target=raw;target.position=target.position+w.reduced.shift;
        w.solved=solve(target);
        if(!w.solved) {reject(State::arm_solve);return;}
        // Torso-width skin helpers may be affine. The fitted hand joints are
        // checked as rigid by the geometry adapter below.
        for(const auto& joint:*w.solved)for(const auto component:joint.f)
            if(!std::isfinite(component)) {reject(State::geometry);return;}
        w.fitted=geometry(*w.solved);
        if(!w.fitted || !anatomical_shape_valid(*w.fitted)) {reject(State::geometry);return;}
        for(unsigned i=0;i<hand_proxy_count;++i)
            if(std::abs((*w.fitted)[i].radius-proxies[i].radius)>1e-7 ||
               length((*w.fitted)[i].centre-proxies[i].centre)>max_shift+.005) {reject(State::geometry);return;}
        w.final_from=*w.fitted;
        for(auto& sphere:w.final_from)sphere.centre=sphere.centre+Vec3{0,0,probe_offset};
        w.queries={};
        for(unsigned i=0;i<hand_proxy_count;++i) {
            if(!current()) {reject(State::interrupted);return;}
            w.queries[i]=cast(w.final_from[i].centre,(*w.fitted)[i].centre,(*w.fitted)[i].radius);++result.casts;
            if(!w.queries[i].complete) {reject(State::final_query);return;}
        }
        w.clearance=full_hand_contact_reduce(w.final_from,*w.fitted,w.queries,source,0,max_shift);
        if(!w.clearance.valid || w.clearance.blocked) {reject(State::final_query);return;}
        // Check every witnessed final plane, including opposed planes whose net
        // correction could cancel. A corrected raw pose is not this geometry.
        for(const auto& plane:w.clearance.planes)
            if(plane.depth>clearance_slop) {reject(State::penetrating);return;}
        if(!current()) {reject(State::interrupted);return;}
        result.palette=*w.solved;result.proxies=*w.fitted;result.shift=w.reduced.shift;
        result.contact=length(result.shift)>1e-6;result.state=State::ready;
    }
    static bool same_geometry(const HandProxies& a,const HandProxies& b,double tolerance=1e-6) noexcept {
        if(!anatomical_shape_valid(a) || !anatomical_shape_valid(b))return false;
        for(unsigned i=0;i<hand_proxy_count;++i)
            if(std::abs(a[i].radius-b[i].radius)>1e-7 || length(a[i].centre-b[i].centre)>tolerance)return false;
        return true;
    }
    template<class Geometry>static std::optional<Palette> rebase(const Result& result,const std::array<float,15>& from,
            const std::array<float,15>& to,Geometry&& geometry) {
        if(!result.ready())return {};
        try {
            const rig::ModelFrame old_frame(from),new_frame(to);
            for(unsigned axis=0;axis<3;++axis)if(old_frame.scale[axis]!=new_frame.scale[axis])return {};
            Palette output=result.palette;
            for(auto& joint:output) {
                for(unsigned axis=0;axis<3;++axis) {
                    const auto direction=new_frame.model_direction(old_frame.world_direction(rig::column(joint,axis)));
                    joint.f[axis*4]=static_cast<float>(direction.x);joint.f[axis*4+1]=static_cast<float>(direction.y);joint.f[axis*4+2]=static_cast<float>(direction.z);
                }
                const auto point=new_frame.model_point(old_frame.world_point(rig::position(joint)));
                joint.f[12]=static_cast<float>(point.x);joint.f[13]=static_cast<float>(point.y);joint.f[14]=static_cast<float>(point.z);
            }
            const auto fitted=geometry(output,to);
            if(!fitted || !same_geometry(*fitted,result.proxies,.0001))return {};
            return output;
        }catch(...) {return {};}
    }
};
}
