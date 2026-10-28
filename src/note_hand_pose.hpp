#pragma once
#include "arm_tracking.hpp"
#include "note_reading.hpp"
#include "note_tablet.hpp"

namespace d2vr {
struct NoteHandPoseResult {
    // Rigid palette in XR metres, with native model-axis columns expressed in XR.
    // Skin with inverse-bind coordinates scaled by model_scale before this palette.
    std::array<rig::Transform,69> joints{};
    std::array<bool,69> hand{};
    Vec3 index_tip{}; // same distal transform/tip extension used by the visible hand
};

// Owned bind data only. No animation scheduler, native writes, or full-body state.
// palm is the calibrated native bind_grip frame expressed directly in XR metres;
// its columns are bind_grip forward/left/up, exactly as ArmTracking uses them.
class NoteHandPose {
    rig::ArmBinding binding_{};
    rig::HandGeometry geometry_{};
    Vec3 index_tip_local_{};
    double model_scale_{1};
    static Vec3 point(const rig::Transform& t,Vec3 p) {
        return rig::position(t)+rig::column(t,0)*p.x+rig::column(t,1)*p.y+rig::column(t,2)*p.z;
    }
public:
    struct Controls {
        WorldTrackedPose palm{};
        rig::FingerAngles turns{};
        std::array<std::array<Vec3,3>,5> axes{}; // bind/model frame
    };
    explicit NoteHandPose(const rig::ArmBinding& binding,double model_scale=1):binding_(binding),model_scale_(model_scale) {
        if(!std::isfinite(model_scale) || model_scale<=0 || model_scale>16 || binding.chain.wrist>=69 || !rig::valid_hierarchy(binding.parents))
            throw std::invalid_argument("Invalid menu hand bind/scale");
        const auto scale=[&](rig::Transform& t) {
            if(!rig::valid(t)) throw std::invalid_argument("Invalid menu hand reference");
            for(unsigned a=12;a<15;++a)t.f[a]=static_cast<float>(t.f[a]*model_scale);
        };
        for(auto& t:binding_.fingers.open)scale(t);
        scale(binding_.bind_grip);scale(binding_.bind_wrist);
        geometry_=rig::hand_geometry(binding_.fingers.open,binding_.parents,binding_.finger_chains,binding_.palm,binding_.chain.wrist,binding_.bind_grip);
        const auto& finger=geometry_.finger[1];
        const auto tip=point(binding_.bind_grip,finger.pivot+finger.bone[0]+finger.bone[1]+finger.bone[2]);
        const auto& distal=binding_.fingers.open[binding_.finger_chains[1][2]];
        const auto delta=tip-rig::position(distal);
        index_tip_local_={dot(delta,rig::column(distal,0)),dot(delta,rig::column(distal,1)),dot(delta,rig::column(distal,2))};
    }
    double model_scale() const {return model_scale_;}
    Vec3 index_tip_local() const {return index_tip_local_;}
    // The page rides the tracked left palm like a tablet held by its left edge (thumb in front, fingers behind):
    // every wrist turn and roll turns the page with it, and the hand is drawn exactly where the controller is.
    TrackedPose page(const WorldTrackedPose& palm,double width,double height) const {
        const auto tablet=rig::note_tablet(geometry_,palm,width,height);
        return {tablet.centre,normalized(basis_quaternion(tablet.right,tablet.up,tablet.front))};
    }
    Controls controls(unsigned side,const WorldTrackedPose& tracked_palm,const NotePanelReceipt& page,
                      rig::FingerIkState& contact_state,double dt,float squeeze=0) const {
        rig::require_frame(tracked_palm,"Invalid XR menu palm",true);
        if(side>1 || !std::isfinite(dt) || dt<0 || dt>.1 || !std::isfinite(squeeze) || squeeze<0 || squeeze>1)
            throw std::invalid_argument("Invalid menu hand controls");
        const auto& palm=tracked_palm;
        auto geometry=geometry_;
        rig::FingerAngles turns{};
        if(!side) {
            if(!finite(page.pose.position) || !std::isfinite(page.width+page.height) || page.width<=0 || page.height<=0)
                throw std::invalid_argument("Invalid actual native page");
            const auto q=normalized(page.pose.orientation);
            rig::NoteTablet tablet{};
            tablet.centre=page.pose.position;tablet.right=rotated(q,{1,0,0});tablet.up=rotated(q,{0,1,0});tablet.front=rotated(q,{0,0,1});
            tablet.shape.frame={tablet.centre-tablet.front*(rig::note_tablet_thickness*.5),{tablet.right,tablet.up,tablet.front}};
            tablet.shape.half={page.width*.5,page.height*.5,rig::note_tablet_thickness*.5};
            turns=rig::note_tablet_fingers(geometry,palm,tablet,contact_state,dt);
            geometry=rig::note_tablet_hand(geometry);
        } else {
            // Index stays extended for direct touch; the other digits follow grip. The free hand's contact solve lays the
            // thumb on the folded fingers (or caps it beside the index) instead of closing it into the palm.
            const float curl=std::max(.15f,squeeze);const std::array<float,5> wants{curl,0.f,curl,curl,curl};
            turns=rig::finger_turns(geometry,rig::finger_contact(geometry,palm,nullptr,0,dt,contact_state,1.f,rig::finger_solve_all,&wants),wants);
            const auto straight=rig::finger_turn_at(geometry.finger[1],1,0.); // the touching index: straight, below the free hand's relaxed floor
            for(unsigned s=0;s<3;++s)turns[1][s]=static_cast<float>(straight[s]);
        }
        return {palm,turns,rig::hinge_axes(geometry,binding_.bind_grip)};
    }
    NoteHandPoseResult pose(unsigned side,const WorldTrackedPose& tracked_palm,const NotePanelReceipt& page,
                            rig::FingerIkState& contact_state,double dt,float squeeze=0,std::optional<TrackedPose> head={}) const {
        const auto input=controls(side,tracked_palm,page,contact_state,dt,squeeze);
        const auto& palm=input.palm;
        auto palette=binding_.fingers.open;
        const auto& axes=input.axes;
        // Use the existing subtree rotation primitive; downstream hinges ride
        // their current joint frame exactly as in finger_points/contact geometry.
        for(unsigned f=0;f<5;++f)for(unsigned s=0;s<3;++s) {
            const auto joint=binding_.finger_chains[f][s];
            const auto& rest=binding_.fingers.open[joint];const auto& current=palette[joint];
            const auto a=axes[f][s];
            const auto axis=rig::column(current,0)*dot(a,rig::column(rest,0))+rig::column(current,1)*dot(a,rig::column(rest,1))+rig::column(current,2)*dot(a,rig::column(rest,2));
            rig::rotate_subtree(palette,binding_.parents,joint,rig::axis_angle(axis,input.turns[f][s]),rig::position(current));
        }
        rig::Transform target{};target.f[15]=1;
        for(unsigned a=0;a<3;++a) {
            target.f[a*4]=static_cast<float>(palm.basis[a].x);target.f[a*4+1]=static_cast<float>(palm.basis[a].y);target.f[a*4+2]=static_cast<float>(palm.basis[a].z);
        }
        target.f[12]=static_cast<float>(palm.position.x);target.f[13]=static_cast<float>(palm.position.y);target.f[14]=static_cast<float>(palm.position.z);
        NoteHandPoseResult result{};
        for(unsigned joint=0;joint<69;++joint) {
            result.joints[joint]=rig::rigid_retarget(palette[joint],binding_.bind_grip,target);
            result.hand[joint]=rig::in_subtree(binding_.parents,joint,binding_.chain.wrist);
        }
        result.index_tip=point(result.joints[binding_.finger_chains[1][2]],index_tip_local_);
        if(head) {
            if(!finite(head->position))throw std::invalid_argument("Invalid XR menu head");
            const auto q=normalized(head->orientation);const Vec3 up{0,1,0};
            auto forward=rotated(q,{0,0,-1});forward.y=0;
            if(length(forward)<1e-6) {const auto r=rotated(q,{1,0,0});forward={r.z,0,-r.x};}
            forward=unit(forward);const auto left=cross(up,forward);
            rig::ArmTrackingFrame frame{};frame.head_anchor={head->position,{forward,left,up}};
            auto shoulder=rig::ArmTracking::shoulder_world(frame,{},side);
            const auto& bind=binding_.fingers.open;const auto chain=binding_.chain;
            const double upper=length(rig::position(bind[chain.elbow])-rig::position(bind[chain.upper]));
            const double lower=length(rig::position(bind[chain.wrist])-rig::position(bind[chain.elbow]));
            const auto wrist=result.joints[chain.wrist];const auto reach=rig::position(wrist)-shoulder;
            const double d=length(reach),lo=std::abs(upper-lower)+.001,hi=(upper+lower)*.995;
            if(!std::isfinite(d) || hi<lo)throw std::invalid_argument("Invalid native menu sleeve lengths");
            // Keep the exact controller/holding wrist even at the near/far
            // limits. Move the shoulder bridge instead of stretching the hand
            // or dropping its geometry when a controller is beyond arm reach.
            const auto direction=d>1e-6?reach*(1/d):forward;
            shoulder=rig::position(wrist)-direction*std::clamp(d,lo,hi);
            rig::Upload owned{};owned.joints=result.joints;
            const auto source=rig::calibrated_arm_source(owned,binding_.parents,chain,shoulder,1.);
            const auto outward=left*(side?-1.:1.);
            const auto pole=rig::boneworks_elbow_pole(rig::position(wrist)-shoulder,forward,outward,up,upper+lower,0);
            auto sleeve=rig::pose_arm(source,binding_.parents,chain,wrist,pole,&binding_.helpers,true,1.06);
            for(unsigned joint=0;joint<69;++joint)if(!result.hand[joint])result.joints[joint]=sleeve.joints[joint];
        }
        return result;
    }
};
}
