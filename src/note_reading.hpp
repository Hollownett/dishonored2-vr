#pragma once
#include "grab_beam.hpp"
#include <optional>

namespace d2vr {
// Captured with the hand publication, never reconstructed from a newer head pose.
struct NoteViewMapping {
    TrackedPose origin{};
    WorldTrackedPose anchor{};
    Vec3 controller_centre{};
    double width{},height{};
    bool active{};
    std::uint64_t generation{};
    std::optional<TrackedPose> panel(Vec3 centre,Vec3 right,Vec3 up,Vec3 front) const {
        if(!active) return {};
        const auto convert=[&](Vec3 p) {return world_to_xr_controller(p,origin,anchor,1.,controller_centre,origin.position);};
        const auto p=convert(centre),x=convert(centre+right),y=convert(centre+up),z=convert(centre+front);
        if(!p || !x || !y || !z) return {};
        return TrackedPose{*p,normalized(basis_quaternion(*x-*p,*y-*p,*z-*p))};
    }
};
struct NotePanelReceipt {
    TrackedPose pose{};
    double width{},height{};
    std::uint64_t tick{},pose_epoch{},reference_epoch{},frame{};
    std::uint64_t generation{};
    bool current(std::uint64_t now,std::uint64_t epoch,std::uint64_t reference,std::uint64_t displayed_frame) const {
        return frame && frame==displayed_frame && tick && now>=tick && now-tick<=100 && pose_epoch==epoch && reference_epoch==reference &&
            std::isfinite(width+height) && width>0 && height>0 && finite(pose.position);
    }
};

// A drag starts only on a fresh trigger edge over the visible page. Losing the
// panel, tracking or focus requires release before another drag can begin.
class NoteScroll {
    bool held_{},dragging_{},active_{};
    std::uint64_t epoch_{},reference_{},tick_{};
    double previous_y_{};
public:
    float update(bool active,bool trigger,std::optional<double> panel_y,float stick,
                 std::uint64_t now,std::uint64_t epoch,std::uint64_t reference) {
        const bool continuous=active && active_ && epoch==epoch_ && reference==reference_ && now>=tick_ && now-tick_<=100;
        if(!continuous) dragging_=false;
        const bool press=continuous && trigger && !held_;
        const double dt=continuous?(now-tick_)*.001:0.;
        active_=active;epoch_=epoch;reference_=reference;tick_=now;
        float drag=0;
        if(!active || !trigger || !panel_y || !std::isfinite(*panel_y)) dragging_=false;
        else if(press) {dragging_=true;previous_y_=*panel_y;}
        else if(dragging_) {
            const double delta=*panel_y-previous_y_;
            if(dt>0 && std::abs(delta)<.15) {previous_y_=*panel_y;drag=static_cast<float>(std::clamp(-delta/(dt*.35),-1.,1.));}
            else if(std::abs(delta)>=.15) dragging_=false;
        }
        held_=trigger;
        if(!active) return 0;
        if(std::isfinite(stick) && std::abs(stick)>.15f) return std::clamp(stick,-1.f,1.f);
        return drag;
    }
};
// Ray hit in page metres (Y upwards); aim may be remote, as in the existing menu pointer.
inline std::optional<double> note_pointer_y(const TrackedPose& aim,const NotePanelReceipt& panel) {
    try {
        const auto q=normalized(panel.pose.orientation),a=normalized(aim.orientation);
        const auto normal=rotated(q,{0,0,1}),direction=rotated(a,{0,0,-1});
        const double denominator=dot(direction,normal);
        if(!finite(aim.position) || denominator>=-1e-5) return {};
        const double distance=dot(panel.pose.position-aim.position,normal)/denominator;
        if(distance<0 || distance>2.) return {};
        const auto hit=aim.position+direction*distance-panel.pose.position;
        const double x=dot(hit,rotated(q,{1,0,0})),y=dot(hit,rotated(q,{0,1,0}));
        if(std::abs(x)>panel.width*.5 || std::abs(y)>panel.height*.5) return {};
        return y;
    }catch(...) {return {};}
}
}
