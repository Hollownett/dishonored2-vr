#pragma once
#include "grab_beam.hpp"
#include <optional>

namespace d2vr {
// Native deferred recording can straddle the increment at Present entry.
inline bool native_note_recording_frame(std::uint64_t frame,std::uint64_t present) noexcept {
    return frame && (frame==present || (frame>present && frame-present==1));
}
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
// Contact uses the actual displayed page in tracking metres. The extra retention
// margin only prevents chatter after contact; it cannot start a remote swipe.
inline std::optional<double> note_touch_y(Vec3 contact,const NotePanelReceipt& panel,bool retaining=false) {
    try {
        if(!finite(contact) || !finite(panel.pose.position) || !std::isfinite(panel.width+panel.height) || panel.width<=0 || panel.height<=0) return {};
        const auto q=normalized(panel.pose.orientation);
        const auto offset=contact-panel.pose.position;
        const double x=dot(offset,rotated(q,{1,0,0})),y=dot(offset,rotated(q,{0,1,0})),z=dot(offset,rotated(q,{0,0,1}));
        const double edge=retaining?.008:0.,depth=retaining?.022:.012;
        if(!std::isfinite(x+y+z) || std::abs(z)>depth || std::abs(x)>panel.width*.5+edge || std::abs(y)>panel.height*.5+edge) return {};
        return y;
    }catch(...) {return {};}
}

class NoteTouchScroll {
    bool active_{},armed_{},touching_{};
    std::uint64_t epoch_{},reference_{},generation_{},tick_{};
    double previous_y_{};
public:
    float update(bool active,std::optional<Vec3> contact,const std::optional<NotePanelReceipt>& panel,float stick,
                 std::uint64_t now,std::uint64_t epoch,std::uint64_t reference,std::uint64_t displayed_frame) {
        const bool valid=active && contact && finite(*contact) && panel && panel->current(now,epoch,reference,displayed_frame);
        const bool continuous=valid && active_ && epoch==epoch_ && reference==reference_ &&
            panel->generation==generation_ && now>=tick_ && now-tick_<=100;
        const double dt=continuous?(now-tick_)*.001:0.;
        if(!continuous) {touching_=false;armed_=false;}
        float swipe=0;
        if(valid) {
            const auto y=note_touch_y(*contact,*panel,touching_);
            if(!y) {touching_=false;armed_=true;}
            else if(continuous && armed_ && !touching_) {touching_=true;armed_=false;previous_y_=*y;}
            else if(touching_) {
                const double delta=*y-previous_y_;
                if(std::abs(delta)>=.15) {touching_=false;armed_=false;}
                else if(dt>0) {previous_y_=*y;swipe=static_cast<float>(std::clamp(-delta/(dt*.35),-1.,1.));}
            }
        }
        active_=valid;epoch_=epoch;reference_=reference;tick_=now;
        generation_=panel?panel->generation:0;
        if(!active) return 0;
        if(std::isfinite(stick) && std::abs(stick)>.15f) return std::clamp(stick,-1.f,1.f);
        return swipe;
    }
};
// Ray hit in page metres (Y upwards); aim may be remote, as in the existing menu pointer.
inline std::optional<double> note_pointer_axis(const TrackedPose& aim,const NotePanelReceipt& panel,bool horizontal) {
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
        return horizontal?x:y;
    }catch(...) {return {};}
}
inline std::optional<double> note_pointer_y(const TrackedPose& aim,const NotePanelReceipt& panel) {
    return note_pointer_axis(aim,panel,false);
}
}
